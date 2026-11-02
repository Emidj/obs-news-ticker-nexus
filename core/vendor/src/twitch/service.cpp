#include "localization.hpp"
// SPDX-License-Identifier: GPL-2.0-or-later
#include "service.hpp"
#include <cmath>
#include "protocol.hpp"
#include <QJsonDocument>
#include <QJsonParseError>
#include <QUrl>
#include <QUrlQuery>
#include <QRegularExpression>
#include <QRandomGenerator>
#include <QDateTime>
#include <QCryptographicHash>
#include <algorithm>
namespace oto::twitch {
namespace {
QByteArray form(std::initializer_list<std::pair<QByteArray,QString>> values){QByteArray result;for(const auto &[k,v]:values){if(!result.isEmpty())result+='&';result+=k+'='+QUrl::toPercentEncoding(v);}return result;}
const QList<QByteArray> formHeaders{"Content-Type: application/x-www-form-urlencoded"};
bool tokenString(const QJsonValue &v){const auto s=v.toString();return v.isString() && !s.isEmpty() && s.size()<=4096 && !s.contains('\r') && !s.contains('\n');}
bool reconnectUrl(const QUrl &url,const QUrl &endpoint){return url.scheme()==endpoint.scheme() && url.host()==endpoint.host() && url.path()==endpoint.path() && url.userInfo().isEmpty() && url.fragment().isEmpty() && url.port()==endpoint.port();}
}
Service::Service(std::shared_ptr<PluginCore> core,QString filename,Endpoints endpoints):core_(std::move(core)),store_(std::move(filename)),endpoints_(std::move(endpoints)){
	for(auto *timer:{&pollTimer_,&validationTimer_,&expiryTimer_,&keepalive_,&retry_,&handoffDeadline_,&shoutoutDelay_})timer->setSingleShot(true);
	connect(&shoutoutDelay_,&QTimer::timeout,this,[this]{pumpShoutouts();});
	connect(&pollTimer_,&QTimer::timeout,this,[this]{poll();});
	connect(&validationTimer_,&QTimer::timeout,this,[this]{validate(false);});
	connect(&expiryTimer_,&QTimer::timeout,this,[this]{refresh();});
	connect(&keepalive_,&QTimer::timeout,this,[this]{diagnostic("Twitch receive deadline expired");reconnect();});
	connect(&retry_,&QTimer::timeout,this,[this]{openSocket(endpoints_.socket);});
	connect(&handoffDeadline_,&QTimer::timeout,this,[this]{reconnect();});
	const auto loaded=store_.load(session_);storageAvailable_=loaded!=SessionLoad::Unavailable;
	if(!storageAvailable_)status_=i18n::qtext("Status.ProtectedRead");
	if(loaded==SessionLoad::Loaded && session_.enabled)QTimer::singleShot(0,this,[this]{if(!stopped_)connectAccount(session_.client);});
}
Service::~Service(){stop();}
void Service::notifyChanged(){if(changed_)changed_();if(sharedChanged_){try{sharedChanged_();}catch(...){}}}
void Service::channelInformation(std::function<void(HttpReply)> done){
 if(!authorized_ || stopped_){QTimer::singleShot(0,this,[done=std::move(done)]{done({});});return;}
 http(endpoints_.channels+QByteArray("?broadcaster_id=")+account_.toUtf8(),{},{"Authorization: Bearer "+session_.access.toUtf8(),"Client-Id: "+session_.client.toUtf8()},std::move(done));
}
void Service::streamInformation(std::function<void(HttpReply)> done){
 if(!authorized_||stopped_){QTimer::singleShot(0,this,[done=std::move(done)]{done({});});return;}
 auto url=QUrl::fromEncoded(endpoints_.channels);const auto path=url.path();url.setPath(path.left(path.lastIndexOf('/')+1)+"streams");
 QUrlQuery query;query.addQueryItem("user_id",account_);query.addQueryItem("first","1");url.setQuery(query);
 http(url.toEncoded(),{},{"Authorization: Bearer "+session_.access.toUtf8(),"Client-Id: "+session_.client.toUtf8()},std::move(done));
}
void Service::status(QString text){status_=std::move(text);notifyChanged();}
void Service::searchCategories(QString query,std::function<void(HttpReply)> done){
 if(!authorized_||stopped_||query.trimmed().isEmpty()||query.size()>100){QTimer::singleShot(0,this,[done=std::move(done)]{done({400,{},false});});return;}
 http(endpoints_.categories+"?first=20&query="+QUrl::toPercentEncoding(query.trimmed()),{},{"Authorization: Bearer "+session_.access.toUtf8(),"Client-Id: "+session_.client.toUtf8()},std::move(done));
}
void Service::updateChannel(QByteArray payload,std::function<void(HttpReply)> done){
 if(!canManageBroadcast()||stopped_){QTimer::singleShot(0,this,[done=std::move(done)]{done({403,{},false});});return;}
 const auto epoch=epoch_;
 const auto completion=std::make_shared<std::function<void(HttpReply)>>(std::move(done));
 try{transport_.request(endpoints_.channels+"?broadcaster_id="+account_.toUtf8(),payload,{"Authorization: Bearer "+session_.access.toUtf8(),"Client-Id: "+session_.client.toUtf8(),"Content-Type: application/json"},[this,epoch,completion](HttpReply reply){if(epoch==epoch_&&!stopped_)(*completion)(std::move(reply));},128*1024,"PATCH");}
 catch(...){QTimer::singleShot(0,this,[this,epoch,completion]{if(epoch==epoch_&&!stopped_)(*completion)({});});}
}
void Service::diagnostic(QString text){
	// Explicit diagnostics only. Never include message bodies, IDs, tokens or URLs.
	if(core_->settings().diagnosticsEnabled)core_->log(LogLevel::Info,text.toStdString());
}
void Service::clearNetwork(){
	browser_.stop();authorized_=false;manageGranted_=false;browserAttempt_=false;browserFallback_=false;
	shoutoutBusy_=false;shoutoutAccepted_={};
	chatWriteGranted_=false;
	profileBusy_=false;profiles_.clear();shoutoutUsers_.clear();historyRequested_=false;
	++epoch_;for(auto *timer:{&pollTimer_,&validationTimer_,&expiryTimer_,&keepalive_,&retry_,&handoffDeadline_,&shoutoutDelay_})timer->stop();
	transport_.stop();socket_=migrating_=0;active_=false;refreshing_=renewalValidation_=false;code_.clear();verification_.clear();deviceCode_.clear();sessionId_.clear();pendingSubscriptions_={};activeSubscriptions_.clear();
	auto resources=std::move(chatResources_);chatResources_.clear();
	QTimer::singleShot(0,this,[resources=std::move(resources)]()mutable{for(auto &[id,done]:resources)done({});});
	QTimer::singleShot(0,this,[this]{if(!stopped_ && profilesChanged_)profilesChanged_();});
}
void Service::fetchChatResource(QString value,std::function<void(HttpReply)> done){
	// Internal catalog keys map only to this fixed Helix endpoint. Never send
	// credentials to a URL obtained from a message or a catalog response.
	static const QRegularExpression badgeKey("^oto://badges/(global|[0-9]{1,32})$");
	static const QRegularExpression cheerKey("^oto://cheermotes/([0-9]{1,32})$");const auto cheer=cheerKey.match(value);
	const auto badge=badgeKey.match(value);QByteArray target;QList<QByteArray> headers;
	if(badge.hasMatch() || cheer.hasMatch()){
		if(stopped_ || !active_ || session_.access.isEmpty() || chatResources_.size()>=8){QTimer::singleShot(0,this,[done=std::move(done)]{done({425,{},false});});return;}
		target=cheer.hasMatch()?endpoints_.cheermotes+QByteArray("?broadcaster_id=")+cheer.captured(1).toUtf8():endpoints_.chatBadges+(badge.captured(1)=="global"?QByteArray("/global"):QByteArray("?broadcaster_id=")+badge.captured(1).toUtf8());
		headers={"Authorization: Bearer "+session_.access.toUtf8(),"Client-Id: "+session_.client.toUtf8()};
	}else{
	const QUrl url(value);static const QStringList hosts{"7tv.io","api.betterttv.net","api.frankerfacez.com","static-cdn.jtvnw.net","cdn.7tv.app","cdn.betterttv.net","cdn.frankerfacez.com","d3aqoihi2n8ty8.cloudfront.net"};
	if(stopped_ || chatResources_.size()>=8 || value.size()>512 || url.scheme()!="https" || !hosts.contains(url.host()) || !url.userInfo().isEmpty() || url.port()!=-1 || url.hasFragment()){
		QTimer::singleShot(0,this,[done=std::move(done)]{done({});});return;
	}
	target=url.toEncoded();
	}
	// Public media shares the transport but never carries OAuth headers.
	auto requestId=std::make_shared<Transport::Id>(0);
	try {
		*requestId=transport_.request(target,{},headers,[this,requestId](HttpReply reply){const auto found=chatResources_.find(*requestId);if(found==chatResources_.end())return;auto callback=std::move(found->second);chatResources_.erase(found);callback(std::move(reply));},2*1024*1024);
		chatResources_[*requestId]=std::move(done);
	} catch (...) {
		QTimer::singleShot(0,this,[done=std::move(done)]{done({});});
	}
}
void Service::stop(){stopped_=true;changed_={};profilesChanged_={};clearNetwork();shoutoutCommands_.clear();pendingShoutoutMessages_.clear();}
void Service::connectAccount(QString client,AuthorizationFlow flow){
	if(batchFailed_){status(i18n::qtext("Status.BatchBeforeReconnect"));return;}
	if(!storageAvailable_){status(i18n::qtext("Status.ProtectedUnavailable"));return;}
	client=client.trimmed();if(client.isEmpty())client=this->client();static const QRegularExpression id("^[A-Za-z0-9]{10,128}$");
	if(!id.match(client).hasMatch()){status(i18n::qtext("Status.InvalidClient"));return;}
	clearNetwork();stopped_=false;retries_=0;
	Session saved;if(store_.load(saved)==SessionLoad::Loaded && saved.client==client)session_=std::move(saved);else session_={client,{},{},false};
	session_.client=client;active_=true;account_.clear();login_.clear();
	browserFallback_=flow==AuthorizationFlow::Browser;
	if(!session_.access.isEmpty())validate(true);else if(flow==AuthorizationFlow::Browser)browserAuthorization();else device();
}
void Service::disconnectAccount(){
	shoutoutCommands_.clear();pendingShoutoutMessages_.clear();
	clearNetwork();session_.enabled=false;
	if(storageAvailable_ && !store_.save(session_)){status(i18n::qtext("Status.DisconnectSave"));return;}
	core_->selectChannel({events::EventOrigin::Simulation,"sim-channel"});status(i18n::qtext("Status.DisconnectProtected"));
}
void Service::authorizeBroadcast(){
 requestManage_=true;
 if(!active_){connectAccount({},AuthorizationFlow::Browser);return;}
 if(authorized_&&!manageGranted_){clearNetwork();active_=true;stopped_=false;browserAuthorization();}
}
void Service::fail(QString text,bool forget){
	const auto preserve=browserAttempt_ || chatAuthorization_;
	clearNetwork();if(preserve){forget=false;chatAuthorization_=false;}if(forget){session_.access.clear();session_.refresh.clear();session_.enabled=false;if(!store_.save(session_))text+=i18n::qtext("Status.ProtectedUpdate");}
	status(std::move(text));
}
void Service::http(QByteArray url,QByteArray body,QList<QByteArray> headers,std::function<void(HttpReply)> done){
	const auto epoch=epoch_;
	try {transport_.request(url,body,headers,[this,epoch,done=std::move(done)](HttpReply reply)mutable{if(epoch!=epoch_ || stopped_)return;try{done(std::move(reply));}catch(...){fail(i18n::qtext("Status.ResponseError"));}});}
	catch(...){fail(i18n::qtext("Status.RequestFailed"));}
}
void Service::browserAuthorization(){
	browserFallback_=false;browserAttempt_=true;authorized_=false;
	auto scopes=requiredScopes();if(chatAuthorization_)scopes.append("user:write:chat");if(requestManage_)scopes.append("channel:manage:broadcast");const auto epoch=epoch_;
	const auto url=browser_.start(session_.client,scopes,[this,epoch](QString token){
		if(epoch!=epoch_ || stopped_)return;
		if(token.isEmpty()){fail(i18n::qtext("Status.AuthorizationIncomplete"));return;}
		// An implicit grant has no refresh token. Validate before persisting it;
		// failed/declined attempts leave the previous protected grant intact.
		session_.access=std::move(token);session_.refresh.clear();verification_.clear();validate(true,false);
	},endpoints_.browserPort);
	if(url.isEmpty()){fail(i18n::qtext("Status.LocalPort"));return;}
	verification_=url.toString(QUrl::FullyEncoded);status(i18n::qtext("Status.Awaiting"));
}
void Service::device(){
	status(i18n::qtext("Status.RequestCode"));
	auto scopes=requiredScopes();if(chatAuthorization_)scopes.append("user:write:chat");if(requestManage_)scopes.append("channel:manage:broadcast");
	http(endpoints_.auth+"device",form({{"client_id",session_.client},{"scopes",scopes.join(' ')}}),formHeaders,[this](HttpReply r){
		const auto o=QJsonDocument::fromJson(r.body).object();
		const auto seconds=o.value("expires_in").toInt(),interval=o.value("interval").toInt();
		const QUrl url(o.value("verification_uri").toString());
		if(r.networkError || r.status!=200 || !tokenString(o.value("device_code")) || !tokenString(o.value("user_code")) || seconds<=0 || seconds>3600 || interval<=0 || interval>60 ||
			url.scheme()!="https" || url.host()!="www.twitch.tv" || url.path()!="/activate" || !url.userInfo().isEmpty()){
			fail(i18n::qtext("Status.CodeFailed").arg(r.status));return;
		}
		deviceCode_=o.value("device_code").toString();code_=o.value("user_code").toString();verification_=url.toString();
		authorizationSeconds_=seconds;pollSeconds_=interval;authorizationClock_.start();pollTimer_.start(interval*1000);status(i18n::qtext("Status.UseCode"));
	});
}
void Service::poll(){
	if(authorizationClock_.elapsed()>=qint64(authorizationSeconds_)*1000){fail(i18n::qtext("Status.CodeExpired"));return;}
	auto scopes=requiredScopes();if(chatAuthorization_)scopes.append("user:write:chat");if(requestManage_)scopes.append("channel:manage:broadcast");
	http(endpoints_.auth+"token",form({{"client_id",session_.client},{"scopes",scopes.join(' ')},{"device_code",deviceCode_},{"grant_type","urn:ietf:params:oauth:grant-type:device_code"}}),formHeaders,[this](HttpReply r){
		if(r.networkError){pollTimer_.start(std::max(5000,pollSeconds_*1000));status(i18n::qtext("Status.AwaitingNetwork"));return;}
		const auto o=QJsonDocument::fromJson(r.body).object();const auto error=o.value("message").toString(o.value("error").toString());
		if(r.status==400 && (error=="authorization_pending" || error=="slow_down")){
			if(error=="slow_down")pollSeconds_=std::min(60,pollSeconds_+5);pollTimer_.start(pollSeconds_*1000);return;
		}
		tokens(r,false);
	});
}
void Service::tokens(const HttpReply &r,bool refreshing){
	refreshing_=false;const auto o=QJsonDocument::fromJson(r.body).object();
	if(refreshing && core_->settings().diagnosticsEnabled)diagnostic(QStringLiteral("Twitch authorization renewal response: HTTP %1; network error %2").arg(r.status).arg(r.networkError?1:0));
	if(r.networkError || r.status!=200 || !tokenString(o.value("access_token")) || !tokenString(o.value("refresh_token"))){
		fail(refreshing?i18n::qtext("Status.RenewFailed"):i18n::qtext("Status.AuthorizationFailed"),!r.networkError);return;
	}
	session_.access=o.value("access_token").toString();session_.refresh=o.value("refresh_token").toString();session_.enabled=true;
	// Public refresh tokens are single use: atomically persist the rotated token before proceeding.
	if(!store_.save(session_)){fail(i18n::qtext("Status.ProtectedAuthSave"));return;}
	if(refreshing){renewalValidation_=true;diagnostic("Twitch authorization renewal persisted");}
	pollTimer_.stop();code_.clear();verification_.clear();deviceCode_.clear();validate(socket_==0,false);
}
void Service::validate(bool first,bool allowRefresh){
	if(!active_ || refreshing_)return;
	if(first)status(i18n::qtext("Status.Checking"));
	http(endpoints_.auth+"validate",{}, {"Authorization: OAuth "+session_.access.toUtf8()},[this,first,allowRefresh](HttpReply r){
		if(r.status==401 && allowRefresh && !session_.refresh.isEmpty()){refresh();return;}
		if(r.status==401 && allowRefresh && browserFallback_){browserAuthorization();return;}
		if(r.networkError){fail(i18n::qtext("Status.ValidationFailed"));return;}
		const auto o=QJsonDocument::fromJson(r.body).object();const auto expires=o.value("expires_in").toInt();
		if(core_->settings().diagnosticsEnabled)diagnostic(QStringLiteral("Twitch authorization lifetime seconds: %1").arg(expires));
		QStringList granted;for(const auto &s:o.value("scopes").toArray())granted.append(s.toString());
		bool scopes=true;for(const auto &s:requiredScopes())scopes=scopes && granted.contains(s);
		chatWriteGranted_=granted.contains("user:write:chat");
		shoutoutChatStatus_=chatWriteGranted_?i18n::qtext("Status.ChatAuthorized"):i18n::qtext("Status.ChatMissing");
		const auto user=o.value("user_id").toString();static const QRegularExpression userId("^[0-9]{1,128}$");
		if(r.status!=200 || o.value("client_id").toString()!=session_.client || !userId.match(user).hasMatch() || expires<=0 || !scopes || (!account_.isEmpty() && user!=account_)){
			fail(i18n::qtext("Status.InvalidAuthorization"),true);return;
		}
		if(requestManage_ && !granted.contains("channel:manage:broadcast")){if(browserFallback_){browserAuthorization();return;}fail(QStringLiteral("Falta el permiso channel:manage:broadcast"));return;}
		manageGranted_=granted.contains("channel:manage:broadcast");
		account_=user;login_=o.value("login").toString();session_.enabled=true;
		chatAuthorization_=false;
		if(!store_.save(session_)){fail(i18n::qtext("Status.ProtectedSave"));return;}
		browserAttempt_=false;browserFallback_=false;authorized_=true;
		const bool renewed=renewalValidation_;renewalValidation_=false;
		if(renewed)diagnostic("Twitch renewed authorization validated");
		core_->selectChannel({events::EventOrigin::Twitch,account_.toStdString()});
		const auto lifetime=session_.refresh.isEmpty()?expires:std::max(1,expires-60);
		validationTimer_.start(60*60*1000);expiryTimer_.start(int(std::min<qint64>(qint64(lifetime)*1000,24*60*60*1000)));
		if(first || !socket_)openSocket(endpoints_.socket);
		else if(renewed && !migrating_ && index_>=pendingSubscriptions_.size())status(i18n::qtext("Status.Connected").arg(login_).arg(accepted_).arg(pendingSubscriptions_.size()).arg(failed_?i18n::qtext("Status.Limited"):""));
		pumpProfiles();
	});
}
ProfileStatus Service::profileStatus(const QString &userId){
	if(userId.isEmpty() || stopped_ || !active_ || account_.isEmpty() || profileAssets_.expired())return ProfileStatus::Unavailable;
	const auto found=profiles_.find(userId);if(found==profiles_.end())return ProfileStatus::Missing;
	if(found->second.status!=ProfileStatus::Pending && (std::chrono::steady_clock::now()>=found->second.expires || (found->second.status==ProfileStatus::Ready && found->second.image.expired())))return ProfileStatus::Missing;
	return found->second.status;
}
visual::ImagePtr Service::profileImage(const QString &userId){
	const auto found=profiles_.find(userId);if(found!=profiles_.end() && profileStatus(userId)==ProfileStatus::Ready)return found->second.image.lock();return {};
}
void Service::requestProfile(const QString &userId){
	static const QRegularExpression id("^[0-9]{1,128}$");if(!id.match(userId).hasMatch() || profileStatus(userId)!=ProfileStatus::Missing)return;
	// One owner and one optional lookup in flight. Never create a network owner
	// or profile cache per source; leave capacity for EventSub/authentication.
	for(auto it=profiles_.begin();it!=profiles_.end();)if(it->second.status!=ProfileStatus::Pending && it->second.image.expired() && std::chrono::steady_clock::now()>=it->second.expires)it=profiles_.erase(it);else ++it;
	if(profiles_.size()>=64 && profiles_.find(userId)==profiles_.end()){const auto old=std::find_if(profiles_.begin(),profiles_.end(),[](const auto &entry){return entry.second.status!=ProfileStatus::Pending;});if(old==profiles_.end())return;profiles_.erase(old);}
	profiles_[userId]={ProfileStatus::Pending};pumpProfiles();
}
void Service::resolvePreviewUser(QString login,std::function<void(std::optional<events::User>)> done){
	login=login.trimmed().toLower();if(login.startsWith('@'))login.remove(0,1);
	static const QRegularExpression valid("^[a-z0-9_]{1,25}$");
	if(!valid.match(login).hasMatch() || stopped_ || !active_ || account_.isEmpty()){
		QTimer::singleShot(0,this,[done=std::move(done)]{done({});});return;
	}
	const auto epoch=epoch_;
	try{transport_.request(endpoints_.users+"?login="+QUrl::toPercentEncoding(login),{}, {"Client-Id: "+session_.client.toUtf8(),"Authorization: Bearer "+session_.access.toUtf8()},[this,epoch,login,done](HttpReply reply){
		if(epoch!=epoch_ || stopped_)return;std::optional<events::User> user;
		try{const auto rows=QJsonDocument::fromJson(reply.body).object().value("data").toArray();
			if(!reply.networkError && reply.status==200 && rows.size()==1){const auto record=rows[0].toObject();const auto id=record.value("id").toString(),name=record.value("display_name").toString(record.value("login").toString());
				static const QRegularExpression numeric("^[0-9]{1,128}$");if(numeric.match(id).hasMatch() && record.value("login").toString().compare(login,Qt::CaseInsensitive)==0 && !name.isEmpty() && name.size()<=128)user=events::User{id.toStdString(),name.toStdString()};}
		}catch(...){}
		done(std::move(user));
	});}catch(...){QTimer::singleShot(0,this,[done=std::move(done)]{done({});});}
}
void Service::resolveShoutoutUser(QString login,std::function<void(std::optional<events::ShoutoutEvent>)> done)
{
	login=login.trimmed().toLower();if(login.startsWith('@'))login.remove(0,1);
	const auto cached=shoutoutUsers_.find(login);if(active_ && cached!=shoutoutUsers_.end() && cached->second.expires>std::chrono::steady_clock::now()){const auto value=cached->second.value;QTimer::singleShot(0,this,[done,value]{done(value);});return;}
	const auto epoch=epoch_;resolvePreviewUser(login,[this,epoch,login,done](std::optional<events::User> user){
		if(epoch!=epoch_ || stopped_)return;if(!user){done({});return;}
		auto value=std::make_shared<events::ShoutoutEvent>(events::ShoutoutEvent{*user,login.toStdString()});const auto id=QString::fromStdString(user->id);requestProfile(id);
		const auto headers=QList<QByteArray>{"Client-Id: "+session_.client.toUtf8(),"Authorization: Bearer "+session_.access.toUtf8()};
		// Metadata is fetched once per target and shared by all cards. Unavailable
		// API fields stay empty; example follower counts never become live data.
		const auto finish=[this,epoch,login,value,done]{if(epoch!=epoch_ || stopped_)return;if(shoutoutUsers_.size()>=32)shoutoutUsers_.erase(shoutoutUsers_.begin());shoutoutUsers_[login]={*value,std::chrono::steady_clock::now()+std::chrono::minutes(5)};done(*value);};
		try{transport_.request(endpoints_.channels+"?broadcaster_id="+id.toUtf8(),{},headers,[this,epoch,id,headers,value,finish](HttpReply reply){
			if(epoch!=epoch_ || stopped_)return;const auto rows=QJsonDocument::fromJson(reply.body).object().value("data").toArray();if(!reply.networkError && reply.status==200 && rows.size()==1 && rows[0].toObject()["broadcaster_id"].toString()==id){const auto category=rows[0].toObject()["game_name"].toString();if(category.size()<=128)value->category=category.toStdString();}
			try{transport_.request(endpoints_.followers+"?broadcaster_id="+id.toUtf8()+"&first=1",{},headers,[this,epoch,value,finish](HttpReply reply){if(epoch!=epoch_ || stopped_)return;const auto object=QJsonDocument::fromJson(reply.body).object();const auto total=object["total"].toDouble(-1);if(!reply.networkError && reply.status==200 && total>=0 && total<=9007199254740991.0 && std::floor(total)==total)value->followers=std::uint64_t(total);finish();});}catch(...){finish();}
		});}catch(...){finish();}
	});
}
void Service::finishProfile(const QString &userId,visual::ImagePtr image){
	const auto found=profiles_.find(userId);if(found==profiles_.end())return;
	found->second={image?ProfileStatus::Ready:ProfileStatus::Unavailable,std::chrono::steady_clock::now()+std::chrono::minutes(image?60:5),image};
	profileBusy_=false;
	core_->log(LogLevel::Info,image?"Twitch profile image ready in shared cache":"Twitch profile image unavailable; fallback enabled");
	QTimer::singleShot(0,this,[this]{if(!stopped_ && profilesChanged_)profilesChanged_();});pumpProfiles();
}
void Service::pumpProfiles(){
	if(profileBusy_ || stopped_ || !active_ || account_.isEmpty() || refreshing_ || renewalValidation_ || index_<pendingSubscriptions_.size())return;
	const auto found=std::find_if(profiles_.begin(),profiles_.end(),[](const auto &entry){return entry.second.status==ProfileStatus::Pending;});if(found==profiles_.end())return;
	const auto userId=found->first;const auto epoch=epoch_;profileBusy_=true;
	try{transport_.request(endpoints_.users+"?id="+QUrl::toPercentEncoding(userId),{}, {"Client-Id: "+session_.client.toUtf8(),"Authorization: Bearer "+session_.access.toUtf8()},[this,epoch,userId](HttpReply reply){
		if(epoch!=epoch_ || stopped_)return;
		try{
			const auto data=QJsonDocument::fromJson(reply.body).object().value("data").toArray();
			if(reply.networkError || reply.status!=200 || data.size()!=1 || data[0].toObject().value("id").toString()!=userId){finishProfile(userId);return;}
			const QUrl url(data[0].toObject().value("profile_image_url").toString()),cdn=QUrl::fromEncoded(endpoints_.avatarCdn);
			if(url.scheme()!=cdn.scheme() || url.host()!=cdn.host() || url.port()!=cdn.port() || !url.path().startsWith(cdn.path()) || !url.userInfo().isEmpty() || !url.fragment().isEmpty() || url.toEncoded().size()>2048){finishProfile(userId);return;}
			// CDN receives no authorization/client headers. Redirects remain off.
			transport_.request(url.toEncoded(),{},{},[this,epoch,userId](HttpReply imageReply){
				if(epoch!=epoch_ || stopped_)return;visual::ImagePtr image;
				try{if(!imageReply.networkError && imageReply.status==200)if(const auto assets=profileAssets_.lock())image=assets->ingestProfileImage(userId,imageReply.body);}catch(...){}
				finishProfile(userId,std::move(image));
			},1024*1024);
		}catch(...){finishProfile(userId);}
	});}catch(...){finishProfile(userId);}
}
void Service::renewAuthorization(){if(canRenewAuthorization()){if(session_.refresh.isEmpty()){clearNetwork();active_=true;stopped_=false;browserAuthorization();}else refresh();}}
void Service::restoreFollowers()
{
	if(historyRequested_ || stopped_ || !active_ || account_.isEmpty())return;
	historyRequested_=true;const state::ChannelKey key{events::EventOrigin::Twitch,account_.toStdString()};
	http(endpoints_.followers+"?broadcaster_id="+QUrl::toPercentEncoding(account_)+"&first=64",{}, {"Client-Id: "+session_.client.toUtf8(),"Authorization: Bearer "+session_.access.toUtf8()},[this,key](HttpReply reply){
		try{
			const auto document=QJsonDocument::fromJson(reply.body);const auto data=document.object().value("data");
			if(reply.networkError || reply.status!=200 || !document.isObject() || !data.isArray() || data.toArray().size()>64)throw std::runtime_error("Follower history unavailable");
			std::vector<events::EventDraft> drafts;
			for(const auto &value:data.toArray()){
				const auto row=value.toObject();const auto time=QDateTime::fromString(row.value("followed_at").toString(),Qt::ISODateWithMs);
				const auto id=row.value("user_id").toString(),name=row.value("user_name").toString();static const QRegularExpression validId("^[0-9]{1,32}$");
				if(!value.isObject() || !validId.match(id).hasMatch() || name.isEmpty() || !time.isValid() || time.toMSecsSinceEpoch()<=0 || time>QDateTime::currentDateTimeUtc().addSecs(300))throw std::runtime_error("Invalid follower history");
				const auto ms=time.toMSecsSinceEpoch();
				drafts.push_back({"history-follow-"+id.toStdString()+"-"+std::to_string(ms),key.channelId,std::chrono::system_clock::time_point(std::chrono::milliseconds(ms)),key.origin,events::FollowEvent{{id.toStdString(),name.toStdString()}}});
			}
			core_->restoreChannelHistory(key,std::move(drafts));
			core_->log(LogLevel::Info,"Twitch recent follows restored quietly to shared history");
		}catch(...){core_->log(LogLevel::Warning,"Twitch follow history unavailable; saved history and live connection preserved");}
	});
}
void Service::refresh(){
	if(!active_ || refreshing_ || renewalValidation_)return;
	if(session_.refresh.isEmpty()){fail(i18n::qtext("Status.Expired"),true);return;}
	refreshing_=true;expiryTimer_.stop();diagnostic("Twitch authorization renewal started");status(i18n::qtext("Status.Renewing"));
	http(endpoints_.auth+"token",form({{"client_id",session_.client},{"refresh_token",session_.refresh},{"grant_type","refresh_token"}}),formHeaders,[this](HttpReply r){tokens(r,true);});
}
void Service::setHypeMeterEnabled(bool enabled) {
	hypeMeterEnabled_=enabled;if(enabled)restoreHypeTrain();
}
void Service::restoreHypeTrain() {
	if(!hypeMeterEnabled_ || stopped_ || !active_ || account_.isEmpty() || sessionId_.isEmpty() || index_<pendingSubscriptions_.size() || hypeStatusSession_==sessionId_)return;
	hypeStatusSession_=sessionId_;const auto sid=sessionId_,account=account_;
	// Timestamp the request, not the eventual response. Newer EventSub data wins.
	const auto requested=std::chrono::system_clock::now();
	http(endpoints_.hypeStatus+"?broadcaster_id="+QUrl::toPercentEncoding(account),{},
		{"Client-Id: "+session_.client.toUtf8(),"Authorization: Bearer "+session_.access.toUtf8()},[this,sid,account,requested](HttpReply reply){
		if(sid!=sessionId_ || account!=account_)return;
		try {
			const auto document=QJsonDocument::fromJson(reply.body);const auto data=document.object().value("data");
			if(reply.networkError || reply.status!=200 || !document.isObject() || !data.isArray() || data.toArray().size()!=1)throw std::runtime_error("Hype status unavailable");
			const auto current=data.toArray().first().toObject().value("current");
			if(current.isNull()){core_->log(LogLevel::Info,"Twitch Hype status: no active train");return;}
			if(!current.isObject() || current.toObject().value("broadcaster_user_id").toString()!=account)throw std::runtime_error("Invalid Hype status channel");
			const auto hype=hypeTrain(current.toObject(),events::HypeTrainStage::Progress);if(!hype)throw std::runtime_error("Invalid Hype status");
			const auto accepted=core_->processEvent({"hype-status-"+sid.toStdString(),account.toStdString(),requested,events::EventOrigin::Twitch,*hype});
			if(accepted){drain(account);core_->log(LogLevel::Info,"Twitch active Hype Train restored to shared state");}
		}catch(...){core_->log(LogLevel::Warning,"Twitch Hype status unavailable; live EventSub preserved");}
	});
}
void Service::openSocket(QByteArray url,bool migration){
	if(!active_ || stopped_ || (migration && migrating_) || (!migration && socket_))return;
	const auto epoch=epoch_;auto id=std::make_shared<Transport::Id>(0);
	try {
		*id=transport_.websocket(url,[]{},[this,epoch,id](QByteArray bytes){if(epoch!=epoch_)return;try{receive(*id,std::move(bytes));}catch(...){fail(i18n::qtext("Status.EventFailed"));}},[this,epoch,id](SocketClose close){if(epoch!=epoch_)return;if(core_->settings().diagnosticsEnabled)diagnostic(QStringLiteral("Twitch transport close: cause %1; peer %2; network %3").arg(static_cast<int>(close.cause)).arg(close.peerCode).arg(static_cast<int>(close.networkCode)));lost(*id);});
		if(migration){migrating_=*id;handoffDeadline_.start(30000);status(i18n::qtext("Status.Migrating"));}
		else{socket_=*id;keepalive_.start(20000);status(i18n::qtext("Status.Connecting"));}
	}catch(...){reconnect();}
}
void Service::receive(Transport::Id id,QByteArray bytes){
	if(id!=socket_ && id!=migrating_)return;
	QJsonParseError error;const auto doc=QJsonDocument::fromJson(bytes,&error);
	if(error.error!=QJsonParseError::NoError || !doc.isObject()){reconnect();return;}
	const auto o=doc.object(),meta=o.value("metadata").toObject(),payload=o.value("payload").toObject();
	const auto type=meta.value("message_type").toString();
	if(type=="session_welcome"){
		diagnostic("Twitch session welcome received");
		const auto session=payload.value("session").toObject();const auto sid=session.value("id").toString();const auto seconds=session.value("keepalive_timeout_seconds").toInt();
		if(sid.isEmpty() || sid.size()>256 || seconds<=0 || seconds>600){reconnect();return;}
		keepaliveMs_=seconds*1000+1000;sessionId_=sid;
		if(id==migrating_){transport_.cancel(socket_);socket_=migrating_;migrating_=0;handoffDeadline_.stop();status(i18n::qtext("Status.Connected").arg(login_).arg(accepted_).arg(pendingSubscriptions_.size()).arg(failed_?i18n::qtext("Status.Limited"):""));}
		else{activeSubscriptions_.clear();pendingSubscriptions_=subscriptions(account_,sessionId_);index_=accepted_=failed_=0;subscribe();}
		keepalive_.start(keepaliveMs_);return;
	}
	if(id==migrating_)return;
	if(type=="session_keepalive"){diagnostic("Twitch keepalive received");keepalive_.start(keepaliveMs_);return;}
	if(type=="notification"){
		diagnostic("Twitch notification received");
		keepalive_.start(keepaliveMs_);const auto draft=notification(o,account_);
		if(!draft){core_->log(LogLevel::Warning,"Unsupported or invalid Twitch notification rejected");return;}
		const auto result=core_->processEvent(*draft);
		if(core_->settings().diagnosticsEnabled)diagnostic(QStringLiteral("Twitch event submitted: %1; engine error %2; queue result %3")
			.arg(QString::fromLatin1(events::typeName(events::typeOf(draft->payload))))
			.arg(static_cast<int>(result.error))
			.arg(static_cast<int>(result.queue.status)));
		if(result){drain(account_);receiveShoutout(*draft);}
		else if(result.queue.status==execution::EnqueueStatus::Full && receiveShoutout(*draft)){drain(account_);core_->log(LogLevel::Warning,"Chat queue full; valid Shoutout retained for deferred admission");}
		else if(result.queue.status==execution::EnqueueStatus::Full){fail(i18n::qtext("Status.QueueFull"));core_->log(LogLevel::Error,"Twitch queue overflow; connection stopped");}
		return;
	}
	if(type=="session_reconnect"){
		diagnostic("Twitch server requested session migration");
		const QUrl url(payload.value("session").toObject().value("reconnect_url").toString());
		if(!reconnectUrl(url,QUrl::fromEncoded(endpoints_.socket))){diagnostic("Twitch migration endpoint rejected");reconnect();return;}openSocket(url.toEncoded(),true);return;
	}
	if(type=="revocation"){
		const auto subscription=payload.value("subscription").toObject();const auto reason=subscription.value("status").toString();
		if(reason=="authorization_revoked" || reason=="user_removed")fail(i18n::qtext("Status.Revoked"),true);
		else if(activeSubscriptions_.erase({subscription.value("type").toString(),subscription.value("version").toString()})){
			++failed_;accepted_=int(activeSubscriptions_.size());
			if(!accepted_){fail(i18n::qtext("Status.AllDisabled"));return;}
			status(i18n::qtext("Status.EventDisabled").arg(login_).arg(accepted_).arg(pendingSubscriptions_.size()));
		}
	}
}
void Service::lost(Transport::Id id){
	diagnostic("Twitch transport closed");
	if(id==migrating_){migrating_=0;reconnect();return;}
	if(id!=socket_)return;
	socket_=0;if(migrating_){keepalive_.stop();return;}reconnect();
}
void Service::reconnect(){
	if(!active_ || stopped_)return;
	transport_.cancel(socket_);transport_.cancel(migrating_);socket_=migrating_=0;keepalive_.stop();handoffDeadline_.stop();
	pendingSubscriptions_={};sessionId_.clear();activeSubscriptions_.clear();
	const int delay=std::min(60000,1000*(1<<std::min(retries_++,6)))+int(QRandomGenerator::global()->bounded(500));
	retry_.start(delay);status(i18n::qtext("Status.Reconnecting"));
}
void Service::subscribe(){
	if(!socket_ || sessionId_.isEmpty())return;
	if(index_>=pendingSubscriptions_.size()){
		if(!accepted_){fail(i18n::qtext("Status.NoEvents"));return;}
		retries_=0;status(i18n::qtext("Status.Connected").arg(login_).arg(accepted_).arg(pendingSubscriptions_.size()).arg(failed_?i18n::qtext("Status.Limited"):""));restoreFollowers();restoreHypeTrain();pumpProfiles();pumpShoutouts();if(profilesChanged_)profilesChanged_();return;
	}
	const auto sid=sessionId_;const auto epoch=epoch_;const auto socket=socket_;
	http(endpoints_.api,QJsonDocument(pendingSubscriptions_[index_].toObject()).toJson(QJsonDocument::Compact),
		{"Content-Type: application/json","Client-Id: "+session_.client.toUtf8(),"Authorization: Bearer "+session_.access.toUtf8()},[this,sid,epoch,socket](HttpReply r){
		if(epoch!=epoch_ || sid!=sessionId_ || socket!=socket_)return;
		if(r.status==401){transport_.cancel(socket_);socket_=0;keepalive_.stop();sessionId_.clear();refresh();return;}
		if(r.networkError || r.status==429 || r.status>=500){reconnect();return;}
		if(r.status==202){const auto subscription=pendingSubscriptions_[index_].toObject();activeSubscriptions_.emplace(subscription.value("type").toString(),subscription.value("version").toString());accepted_=int(activeSubscriptions_.size());}else ++failed_;
		++index_;subscribe();
	});
}
void Service::drain(QString channel){
	drainChannels_.insert(channel);if(batchQueued_ || batchFailed_ || stopped_)return;batchQueued_=true;
	QTimer::singleShot(0,this,[this]{batchQueued_=false;if(stopped_ || drainChannels_.empty())return;const auto channel=*drainChannels_.begin();
		try{const auto batch=core_->processPendingEventsFor(events::EventOrigin::Twitch,channel.toStdString());
			if(core_->settings().diagnosticsEnabled)diagnostic(QStringLiteral("Twitch batch applied: %1; revision %2").arg(qulonglong(batch.processed)).arg(qulonglong(batch.snapshot?batch.snapshot->revision:0)));
			publishShoutoutMessages(batch.delivered); for(const auto &event:batch.delivered)if(sharedEvent_){try{sharedEvent_(event);}catch(...){}}
			if(!shoutoutCommands_.empty() && !shoutoutDelay_.isActive())shoutoutDelay_.start(0);
			if(batch.processed<32)drainChannels_.erase(channel);if(!drainChannels_.empty())drain(*drainChannels_.begin());}
		catch(...){batchFailed_=true;failedChannel_=channel;fail(i18n::qtext("Status.BatchRetained"));core_->log(LogLevel::Error,"Twitch batch retained after persistence failure");}
	});
}
void Service::retryBatch(){if(!batchFailed_)return;batchFailed_=false;drain(failedChannel_);status(i18n::qtext("Status.BatchRetry"));}
bool Service::receiveShoutout(const events::EventDraft &draft)
{
	const auto *chat=std::get_if<events::ChatEvent>(&draft.payload);
	if(!chat || !shoutoutsEnabled_)return false;
	const auto target=shoutoutTarget(*chat);if(target.isEmpty())return false;
	if(std::any_of(shoutoutCommands_.begin(),shoutoutCommands_.end(),[&](const auto &pending){return pending.chat.id==draft.id && pending.chat.channelId==draft.channelId;}))return true;
	shoutoutCommands_.push_back({draft,target,{}});
	core_->log(LogLevel::Info,"Shoutout command retained in shared service backlog");
	pumpShoutouts();
	return true;
}
void Service::setShoutoutsEnabled(bool enabled) noexcept
{
	const bool changed=shoutoutsEnabled_!=enabled;shoutoutsEnabled_=enabled;
	if(changed && enabled && !shoutoutCommands_.empty())shoutoutDelay_.start(0);
}
void Service::pumpShoutouts()
{
	if(stopped_ || !active_ || !socket_ || shoutoutBusy_ || !shoutoutsEnabled_ || batchFailed_ || shoutoutCommands_.empty())return;
	if(shoutoutCommands_.front().chat.channelId!=account_.toStdString()){
		core_->log(LogLevel::Warning,"Queued Shoutout cancelled after account change");shoutoutCommands_.pop_front();shoutoutDelay_.start(0);return;
	}
	const auto now=std::chrono::steady_clock::now();
	if(!shoutoutCommands_.front().resolved && shoutoutAccepted_!=std::chrono::steady_clock::time_point{} && now-shoutoutAccepted_<std::chrono::seconds(5)){
		const auto remaining=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::seconds(5)-(now-shoutoutAccepted_)).count();shoutoutDelay_.start(int(remaining)+1);return;
	}
	const auto channel=shoutoutCommands_.front().chat.channelId;
	const auto id=shoutoutCommands_.front().chat.id;const auto target=shoutoutCommands_.front().target;
	if(shoutoutCommands_.front().resolved){
		try{
			const auto derived=QString::fromLatin1(QCryptographicHash::hash(QByteArray::fromStdString(id),QCryptographicHash::Sha256).toHex());
			events::EventDraft event{"so-"+derived.toStdString(),channel,shoutoutCommands_.front().chat.occurredAt,events::EventOrigin::Twitch,*shoutoutCommands_.front().resolved};
			const auto messageId=event.id;
			// Prepare message bookkeeping before admission; allocation failure must
			// not admit a card whose message cannot be tracked.
			pendingShoutoutMessages_.emplace(messageId,target);
			const auto result=core_->processEvent(std::move(event));
			if(result || result.queue.status==execution::EnqueueStatus::Duplicate){
				if(!result)pendingShoutoutMessages_.erase(messageId);
				shoutoutCommands_.pop_front();drain(account_);shoutoutDelay_.start(0);core_->log(LogLevel::Info,"Shoutout delivered to central queue");
			}else{pendingShoutoutMessages_.erase(messageId);drain(account_);shoutoutDelay_.start(250);core_->log(LogLevel::Warning,"Shoutout admission deferred; command retained");}
		}catch(...){core_->log(LogLevel::Error,"Shoutout preparation failed; command retained");}
		return;
	}
	shoutoutBusy_=true;shoutoutAccepted_=now;const auto epoch=epoch_;
	core_->log(LogLevel::Info,"Shoutout command accepted; resolving target with shared Twitch service");
	resolveShoutoutUser(target,[this,epoch,channel,id](std::optional<events::ShoutoutEvent> user){
		if(epoch!=epoch_ || stopped_)return;shoutoutBusy_=false;
		if(!active_ || channel!=account_.toStdString() || shoutoutCommands_.empty() || shoutoutCommands_.front().chat.id!=id)return;
		if(!user){core_->log(LogLevel::Warning,"Shoutout target unavailable; command rejected, remaining backlog retained");shoutoutCommands_.pop_front();shoutoutDelay_.start(0);return;}
		shoutoutCommands_.front().resolved=std::move(*user);pumpShoutouts();
	});
}
void Service::authorizeChatMessages(AuthorizationFlow flow)
{
	if(!canRenewAuthorization() || canSendChat())return;
	clearNetwork();chatAuthorization_=true;active_=true;stopped_=false;
	// Keep the existing protected session on disk until the new token is received.
	if(flow==AuthorizationFlow::Browser)browserAuthorization();else device();
}
void Service::publishShoutoutMessages(const std::vector<events::EventPtr> &delivered)
{
	for(const auto &event:delivered){const auto pending=pendingShoutoutMessages_.find(event->data().id);if(pending==pendingShoutoutMessages_.end())continue;const auto login=pending->second;pendingShoutoutMessages_.erase(pending);if(event->data().channelId==account_.toStdString())sendShoutoutMessage(login);}
}
void Service::sendShoutoutMessage(QString login)
{
	if(!canSendChat()){core_->log(LogLevel::Warning,"Shoutout chat message not sent: user:write:chat is not authorized");notifyChanged();return;}
	const auto message=shoutoutMessage(login);if(message.isEmpty())return;const auto epoch=epoch_;
	try{const QJsonObject body{{"broadcaster_id",account_},{"sender_id",account_},{"message",message}};
		transport_.request(endpoints_.chatMessages,QJsonDocument(body).toJson(QJsonDocument::Compact),{"Content-Type: application/json","Client-Id: "+session_.client.toUtf8(),"Authorization: Bearer "+session_.access.toUtf8()},[this,epoch](HttpReply reply){
			if(epoch!=epoch_ || stopped_)return;const auto data=QJsonDocument::fromJson(reply.body).object().value("data").toArray();
			const bool sent=!reply.networkError && reply.status==200 && data.size()==1 && data[0].toObject().value("is_sent").toBool();
			shoutoutChatStatus_=sent?i18n::qtext("Status.ChatSent"):i18n::qtext("Status.ChatUnconfirmed");
			core_->log(sent?LogLevel::Info:LogLevel::Warning,sent?"Shoutout chat message confirmed":"Shoutout chat message unconfirmed; no automatic retry");notifyChanged();
		});
	}catch(...){shoutoutChatStatus_=i18n::qtext("Status.ChatSendFailed");core_->log(LogLevel::Warning,"Shoutout chat send could not start");notifyChanged();}
}
}


