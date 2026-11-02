// SPDX-License-Identifier: GPL-2.0-or-later
#include "emote-cache.hpp"
#include "personal-catalog.hpp"
#include <QBuffer>
#include <QImageReader>
#include <QPainter>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrl>
#include <QThread>
#include <QPointer>
namespace oto::chat {
namespace {
unsigned bttvPrefixEffect(const QString &word){
	if(word=="w!")return Wide;if(word=="h!")return FlipX;if(word=="v!")return FlipY;
	if(word=="l!")return TurnLeft;if(word=="r!")return TurnRight;
	if(word=="p!")return Party;if(word=="s!")return Shake;if(word=="c!")return Cursed;return 0;
}
unsigned bttvCatalogEffect(const QString &id){
	if(id=="64e3b31920cb0d25d950a9f9")return Wide;
	if(id=="6468f845aee1f7f47567709b")return FlipX;
	if(id=="6468f89caee1f7f4756770c2")return FlipY;
	if(id=="6468f869aee1f7f4756770a8")return TurnLeft;
	if(id=="6468f883aee1f7f4756770b5")return TurnRight;
	if(id=="65cbe7dbaed093b2eaf87c65")return Party;
	if(id=="662953eeea369c0ece39eb93")return Shake;
	if(id=="6468f7acaee1f7f47567708e")return Cursed;return 0;
}
}
QRect Emote::frame(int elapsed)const {
	if(ends.empty())return {0,0,cell,cell};const int t=std::max(0,elapsed)%duration();const int index=int(std::upper_bound(ends.begin(),ends.end(),t)-ends.begin());return {(index%columns)*cell,(index/columns)*cell,cell,cell};
}
bool publicEmoteUrl(const QString &value){
	const QUrl u(value);if(value.size()>512 || !u.isValid() || u.scheme()!="https" || !u.userInfo().isEmpty() || u.hasFragment() || u.port()!=-1)return false;
	const auto host=u.host(),path=u.path();return publicCheerUrl(value) || (host=="static-cdn.jtvnw.net" && path.startsWith("/emoticons/v2/")) || (host=="cdn.7tv.app" && path.startsWith("/emote/")) || (host=="cdn.betterttv.net" && path.startsWith("/emote/")) || (host=="cdn.frankerfacez.com" && path.startsWith("/emote/"));
}
bool publicBadgeUrl(const QString &value){
	const QUrl u(value);static const QRegularExpression path("^/badges/v1/[A-Za-z0-9_-]{1,128}/[123]$");
	return value.size()<=512 && u.isValid() && u.scheme()=="https" && u.host()=="static-cdn.jtvnw.net" && u.userInfo().isEmpty() && u.port()==-1 && !u.hasFragment() && !u.hasQuery() && path.match(u.path()).hasMatch();
}
std::optional<std::vector<BadgeEntry>> parseBadgeCatalog(const QByteArray &bytes){
	if(bytes.size()>2*1024*1024)return {};const auto doc=QJsonDocument::fromJson(bytes);
	if(!doc.isObject() || !doc.object().value("data").isArray())return {};
	static const QRegularExpression id("^[A-Za-z0-9_-]{1,64}$");std::vector<BadgeEntry> result;
	for(const auto &v:doc.object().value("data").toArray()){
		const auto set=v.toObject();const auto setId=set.value("set_id").toString();if(!id.match(setId).hasMatch())continue;
		for(const auto &version:set.value("versions").toArray()){
			const auto item=version.toObject();const auto versionId=item.value("id").toString();auto url=item.value("image_url_4x").toString();
			if(!publicBadgeUrl(url))url=item.value("image_url_2x").toString();if(!publicBadgeUrl(url))url=item.value("image_url_1x").toString();
			if(id.match(versionId).hasMatch() && publicBadgeUrl(url) && result.size()<4096)result.push_back({setId,versionId,url});
		}
	}return result;
}
std::optional<std::vector<CatalogEntry>> parseCatalogSnapshot(int provider,const QByteArray &bytes,bool channel){
	std::vector<CatalogEntry> result;if(bytes.size()>2*1024*1024)return {};
	const auto doc=QJsonDocument::fromJson(bytes);
	if(provider<0 || provider>2)return {};
	const auto root=doc.object();
	if(provider==0){if(!doc.isObject())return {};if(channel){if(root.value("emote_set").isNull())return result;if(!root.value("emote_set").isObject() || !root.value("emote_set").toObject().value("emotes").isArray())return {};}else if(!root.value("emotes").isArray())return {};}
	if(provider==1 && (channel?(!doc.isObject() || !root.value("channelEmotes").isArray() || !root.value("sharedEmotes").isArray()):!doc.isArray()))return {};
	if(provider==2 && (!doc.isObject() || !root.value("sets").isObject() || (!channel && !root.value("default_sets").isArray())))return {};
	const auto add=[&](QString name,QString url,bool zeroWidth=false,unsigned effects=0,bool modifier=false,bool prefix=false,QRectF layerRect={0,0,1,1}){
		if(url.startsWith("//"))url="https:"+url;
		if(result.size()<2048 && name.size()>0 && name.size()<=64 && !name.contains(QRegularExpression("\\s")) && publicEmoteUrl(url))result.push_back({name,url,(channel?10:0)+(3-provider),zeroWidth,effects,modifier,prefix,layerRect});
	};
	if(provider==0){ // 7TV v3
		const auto root=doc.object();const auto set=channel?root.value("emote_set").toObject():root;
		for(const auto &v:set.value("emotes").toArray()){
			const auto e=v.toObject(),data=e.value("data").toObject(),host=data.value("host").toObject();QString file;
			for(const auto &f:host.value("files").toArray()){const auto o=f.toObject();if(o.value("name").toString()=="2x.webp")file=o.value("name").toString();}
			// v3 image flags live on data, not the active emote/set flags.
			if(!file.isEmpty())add(e.value("name").toString(),host.value("url").toString()+"/"+file,(data.value("flags").toInt() & 256)!=0);
		}
	}else if(provider==1){ // BetterTTV v3
		QJsonArray list=doc.array();if(channel){list=doc.object().value("channelEmotes").toArray();for(const auto &v:doc.object().value("sharedEmotes").toArray())list.append(v);}
		// IDs with overlay treatment in BetterTTV's official emotes/style.css.
		// API modifier=true describes transforms; it does not mean an image layer.
		const QStringList overlays={"5849c9a4f52be01a7ee5f79d","567b5b520e984428652809b6","58487cc6f52be01a7ee5f205","5849c9c8f52be01a7ee5f79e","567b5c080e984428652809ba","567b5dc00e984428652809bd","5e76d338d6581c3724c0f0b2","5e76d399d6581c3724c0f0b8"};
		for(const auto &v:list){const auto e=v.toObject();const auto id=e.value("id").toString();const auto effect=e.value("modifier").isBool() && e.value("modifier").toBool()?bttvCatalogEffect(id):0u;static const QRegularExpression valid("^[a-zA-Z0-9]{1,64}$");if(valid.match(id).hasMatch())add(e.value("code").toString(),"https://cdn.betterttv.net/emote/"+id+"/2x",overlays.contains(id),effect,effect!=0,effect!=0);}
	}else if(provider==2){ // FFZ: only default global sets, all sets in a room.
		const auto root=doc.object();const auto sets=root.value("sets").toObject();const auto defaults=root.value("default_sets").toArray();
		for(auto it=sets.begin();it!=sets.end();++it){if(!channel && !defaults.contains(it.key().toInt()))continue;if(!it.value().isObject() || !it.value().toObject().value("emoticons").isArray())return {};
			for(const auto &v:it.value().toObject().value("emoticons").toArray()){const auto e=v.toObject();auto urls=e.value("animated").toObject();if(urls.isEmpty())urls=e.value("urls").toObject();const auto url=urls.value("2").toString(urls.value("1").toString());const int flags=e.value("modifier_flags").toInt();const bool modifier=e.value("modifier").isBool() && e.value("modifier").toBool() && flags>1 && !(flags & ~262143);
				const unsigned effects=modifier?ffzEffects(unsigned(flags)):0u;
				
                const bool accessory=e.value("modifier").isBool() && e.value("modifier").toBool() && !modifier && !(flags&1);
                QRectF rect(0,0,1,1);QString offset=e.value("modifier_offset").toString();const int id=e.value("id").toInt();
                if(offset.isEmpty()){if(id==59847)offset="0 15px 15px 0";else if(id==70852)offset="0 5px 20px 0";else if(id==70854)offset="30px 0 0";else if(id==147049)offset="4px 1px 0 3px";}
                if(accessory && !offset.isEmpty()){
                    const auto parts=offset.split(QRegularExpression("\\s+"),Qt::SkipEmptyParts);std::vector<double> n;bool ok=parts.size()>=1 && parts.size()<=4;
                    static const QRegularExpression px("^([0-9]{1,2})(?:px)?$");
                    for(const auto &part:parts){const auto m=px.match(part);const double value=m.captured(1).toDouble();if(!m.hasMatch() || value>39){ok=false;break;}n.push_back(value/40);}
                    if(ok){const double top=n[0],right=n.size()>1?n[1]:top,bottom=n.size()>2?n[2]:top,left=n.size()>3?n[3]:right;
                        if(left+right<1 && top+bottom<1)rect={left,top,1-left-right,1-top-bottom};}
                }
                add(e.value("name").toString(),url,accessory,effects,modifier,modifier && e.value("modifier_prefix").toBool(),rect);}
		}
	}return result;
}
std::vector<CatalogEntry> parseCatalog(int provider,const QByteArray &bytes,bool channel){return parseCatalogSnapshot(provider,bytes,channel).value_or(std::vector<CatalogEntry>{});}
EmotePtr decodeEmote(visual::VisualEngine &engine,const QByteArray &bytes){
	if(bytes.isEmpty() || bytes.size()>2*1024*1024)return {};
	QBuffer buffer;buffer.setData(bytes);buffer.open(QIODevice::ReadOnly);QImageReader reader(&buffer);const auto size=reader.size();
	if(!size.isValid() || size.width()>512 || size.height()>512)return {};
	std::vector<QImage> frames;std::vector<int> ends;int elapsed=0;std::uint64_t pixels=0;
	do{
		if(frames.size()>=256 || pixels+std::uint64_t(size.width())*size.height()>8*1024*1024){ // Keep first frame when animation exceeds budget.
			frames.resize(1);ends={100};break;
		}
		const auto image=reader.read();if(image.isNull())break;
		pixels+=std::uint64_t(image.width())*image.height();frames.push_back(image.scaled(56,56,Qt::KeepAspectRatio,Qt::SmoothTransformation));
		elapsed+=std::clamp(reader.nextImageDelay(),20,10000);ends.push_back(elapsed);
	}while(reader.supportsAnimation() && reader.canRead());
	if(frames.empty())return {};
	const int columns=std::min(16,int(frames.size()));QImage atlas(columns*56,((int(frames.size())+columns-1)/columns)*56,QImage::Format_RGBA8888_Premultiplied);atlas.fill(Qt::transparent);
	{QPainter painter(&atlas);for(int i=0;i<int(frames.size());++i){const auto &f=frames[i];painter.drawImage(QPoint((i%columns)*56+(56-f.width())/2,(i/columns)*56+(56-f.height())/2),f);}}
	auto emote=std::make_shared<Emote>();emote->atlas=engine.prepareRaster(std::move(atlas));emote->ends=std::move(ends);emote->columns=columns;return emote;
}
EmoteCache::EmoteCache(std::shared_ptr<visual::VisualEngine> visual,Fetch fetch,Clock clock):visual_(std::move(visual)),fetch_(std::move(fetch)),clock_(std::move(clock)){
	personal_=std::make_unique<PersonalCatalog>(fetch_,clock_,[this]{notify();});
	cheerRetry_.setSingleShot(true);connect(&cheerRetry_,&QTimer::timeout,this,[this]{if(running_)notify();});
	catalogTimer_.setSingleShot(true);connect(&catalogTimer_,&QTimer::timeout,this,[this]{refreshCatalogs();});
	badgeRetry_.setSingleShot(true);connect(&badgeRetry_,&QTimer::timeout,this,[this]{if(running_)notify();});
}
EmoteCache::~EmoteCache(){stop();}
void EmoteCache::ensureAuthor(const QString &id){if(running_)personal_->ensure(id);}
void EmoteCache::enablePersonalFeed(){if(running_)personal_->enableFeed();}
bool EmoteCache::ingestPersonalBTTV(const QByteArray &b){return running_ && personal_->ingestBTTV(b);}
void EmoteCache::ensureCheermotes(const QString &channel){
	static const QRegularExpression digits("^[0-9]{1,32}$");if(!running_ || !digits.match(channel).hasMatch())return;
	if(channel!=cheerChannel_){cheerChannel_=channel;++cheerEpoch_;cheerPending_=false;cheers_.clear();cheerExpires_={};cheerRetryDue_={};cheerRetry_.stop();}
	if(cheerPending_ || clock_()<cheerExpires_ || clock_()<cheerRetryDue_ || !fetch_)return;
	cheerPending_=true;if(!enqueue({"oto://cheermotes/"+channel,channel,-3,cheerEpoch_})){cheerPending_=false;cheerRetryDue_=clock_()+std::chrono::seconds(5);cheerRetry_.start(5000);}
}
void EmoteCache::ensureBadges(const QString &channel){
	static const QRegularExpression digits("^[0-9]{1,32}$");if(!running_ || !digits.match(channel).hasMatch())return;
	if(badgeChannel_!=channel){badgeChannel_=channel;for(auto it=badges_.begin();it!=badges_.end();)if(!it->first.isEmpty() && it->first!=channel)it=badges_.erase(it);else ++it;}
	const auto now=std::chrono::steady_clock::now();
	for(const auto key:{QString{},channel}){auto &b=badges_[key];if(b.pending || now<b.retry || now<b.expires || !fetch_)continue;
		b.pending=true;if(!enqueue({"oto://badges/"+(key.isEmpty()?QString("global"):key),key,-2})){b.pending=false;b.retry=now+std::chrono::seconds(5);if(!badgeRetry_.isActive())badgeRetry_.start(5000);}
	}
}
bool EmoteCache::ingestBadges(const QString &channel,const QByteArray &bytes){
	static const QRegularExpression digits("^[0-9]{1,32}$");if(!running_ || (!channel.isEmpty() && (channel!=badgeChannel_ || !digits.match(channel).hasMatch())))return false;
	const auto parsed=parseBadgeCatalog(bytes);if(!parsed)return false;
	auto &b=badges_[channel];b.entries.clear();for(const auto &e:*parsed)b.entries[{e.setId,e.version}]=e.url;
	b.pending=false;b.retry={};b.expires=std::chrono::steady_clock::now()+std::chrono::hours(1);return true;
}
std::vector<QString> EmoteCache::badgeUrls(const std::vector<events::ChatBadge> &badges)const{
	std::vector<QString> urls;for(const auto &badge:badges){const auto key=std::make_pair(QString::fromStdString(badge.setId),QString::fromStdString(badge.version));QString url;
		for(const auto catalogKey:{badgeChannel_,QString{}}){const auto catalog=badges_.find(catalogKey);if(catalog==badges_.end())continue;const auto found=catalog->second.entries.find(key);if(found!=catalog->second.entries.end()){url=found->second;break;}}
		urls.push_back(url);
	}return urls;
}
void EmoteCache::notify(){++revision_;if(changed_)changed_();}
bool EmoteCache::enqueue(Request r){if(!fetch_ || !running_ || pending_.size()>=128 || !requested_.insert(r.url).second)return false;pending_.push_back(std::move(r));pump();return true;}
// A channel switch owns a new epoch, including A -> B -> A. Late replies
// release their network slot but cannot replace the new channel's snapshot.
void EmoteCache::discardCatalogRequests(bool all){
	for(auto it=pending_.begin();it!=pending_.end();){
		if(it->provider>=0 && (all || (!it->channel.isEmpty() && it->channel!=channel_))){
			const auto c=catalogs_.find(it->channel);if(c!=catalogs_.end() && c->second.epoch==it->epoch)c->second.providers[it->provider].pending=false;
			requested_.erase(it->url);it=pending_.erase(it);
		}else ++it;
	}
}
void EmoteCache::ensureChannel(const QString &channel){
	static const QRegularExpression digits("^[0-9]{1,32}$");if(!running_)return;
	const auto next=digits.match(channel).hasMatch()?channel:QString{};
	if(channel_!=next){channel_=next;discardCatalogRequests(channel_.isEmpty());for(auto it=catalogs_.begin();it!=catalogs_.end();)if(!it->first.isEmpty())it=catalogs_.erase(it);else ++it;}
	if(!channel_.isEmpty()){if(!catalogs_.count(channel_))catalogs_[channel_].epoch=++catalogEpoch_;if(!catalogs_.count(QString{}))catalogs_[QString{}].epoch=++catalogEpoch_;}
	personal_->select(channel_,catalogActive_ && !channel_.isEmpty());
	refreshCatalogs();
}
void EmoteCache::setCatalogActive(bool active){
	if(catalogActive_==active)return;catalogActive_=active;
	personal_->select(channel_,active && !channel_.isEmpty());
	if(!active){catalogTimer_.stop();discardCatalogRequests(true);}else refreshCatalogs();
}
void EmoteCache::refreshCatalogs(){
	catalogTimer_.stop();if(!running_ || !catalogActive_ || channel_.isEmpty() || !fetch_)return;const auto now=clock_();
	for(const auto key:{QString{},channel_}){
		const QStringList urls=key.isEmpty()?QStringList{"https://7tv.io/v3/emote-sets/global","https://api.betterttv.net/3/cached/emotes/global","https://api.frankerfacez.com/v1/set/global"}:QStringList{"https://7tv.io/v3/users/twitch/"+key,"https://api.betterttv.net/3/cached/users/twitch/"+key,"https://api.frankerfacez.com/v1/room/id/"+key};
		for(int i=0;i<3;++i){auto &c=catalogs_[key];auto &p=c.providers[i];if(p.pending || now<p.retry || now<p.expires)continue;
			p.pending=true;if(!enqueue({urls[i],key,i,c.epoch})){p.pending=false;p.retry=now+std::chrono::seconds(5);}
		}
	}scheduleCatalogs();
}
void EmoteCache::scheduleCatalogs(){
	catalogTimer_.stop();if(!running_ || !catalogActive_ || channel_.isEmpty() || !fetch_)return;
	const auto now=clock_();auto due=now+std::chrono::hours(1);bool waiting=false;
	for(const auto &[key,c]:catalogs_)for(const auto &p:c.providers)if(!p.pending){due=std::min(due,std::max(p.retry,p.expires));waiting=true;}
	if(waiting)catalogTimer_.start(int(std::clamp<std::int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(due-now).count(),1,3600000)));
}
bool EmoteCache::requestCatalogRefresh(){
	if(!running_ || !catalogActive_ || channel_.isEmpty() || !fetch_)return false;const auto now=clock_();
	if(lastManual_ && now-*lastManual_<std::chrono::seconds(30))return false;lastManual_=now;
	for(auto &[key,c]:catalogs_)for(auto &p:c.providers)if(!p.pending)p.expires={};refreshCatalogs();return true;
}
void EmoteCache::catalog(const Request &r,const twitch::HttpReply &reply){
	const auto found=catalogs_.find(r.channel);if(found==catalogs_.end() || found->second.epoch!=r.epoch){scheduleCatalogs();return;}
	auto &c=found->second;auto &p=c.providers[r.provider];p.pending=false;
	const auto parsed=reply.status==200 && !reply.networkError?parseCatalogSnapshot(r.provider,reply.body,!r.channel.isEmpty()):std::nullopt;
	if(!parsed){p.expires={};const int delay=5*(1<<std::min(p.failures++,6u));p.retry=clock_()+std::chrono::seconds(std::min(delay,300));scheduleCatalogs();return;}
	p.entries.clear();for(const auto &e:*parsed)p.entries[e.name]=e;p.failures=0;p.retry={};p.expires=clock_()+(r.channel.isEmpty()?std::chrono::seconds(3600):std::chrono::seconds(300));
	std::map<QString,CatalogEntry> merged;for(const auto &provider:c.providers)for(const auto &[name,e]:provider.entries)if(merged.size()<2048)merged.emplace(name,e);
	const bool changed=merged!=c.entries;c.entries=std::move(merged);scheduleCatalogs();if(changed)notify();
}
std::vector<Token> EmoteCache::tokens(const events::ChatEvent &chat,bool thirdParty)const {
	std::vector<Token> result;
	const auto text=[&](const QString &value){
		if(!thirdParty){result.push_back({value,{}});return;}
		static const QRegularExpression words("\\S+|\\s+");auto matches=words.globalMatch(value);
		while(matches.hasNext()){const auto word=matches.next().captured();QString url;int priority=-1;bool zeroWidth=false;unsigned effects=0;bool modifier=false,prefix=false;QRectF layerRect(0,0,1,1);
			if(const auto personal=personal_->find(QString::fromStdString(chat.author.id),word)){priority=30;url=personal->url;zeroWidth=personal->zeroWidth;effects=personal->effects;modifier=personal->modifier;prefix=personal->prefix;layerRect=personal->layerRect;}
			for(const auto key:{QString{},channel_}){const auto catalog=catalogs_.find(key);if(catalog==catalogs_.end())continue;const auto emote=catalog->second.entries.find(word);if(emote!=catalog->second.entries.end() && emote->second.priority>priority){priority=emote->second.priority;url=emote->second.url;zeroWidth=emote->second.zeroWidth;effects=emote->second.effects;modifier=emote->second.modifier;prefix=emote->second.prefix;layerRect=emote->second.layerRect;}}
			// BTTV's supported prefix operators also work without their menu images.
			// A real emote from a higher-priority catalog keeps its normal meaning.
			if(url.isEmpty() && (effects=bttvPrefixEffect(word))!=0){modifier=true;prefix=true;}
			if(!result.empty() && url.isEmpty() && !modifier && result.back().url.isEmpty() && !result.back().modifier)result.back().text+=word;else result.push_back({word,url,zeroWidth,-1,effects,modifier,prefix,layerRect});
		}
	};
	if(chat.fragments.empty())text(QString::fromStdString(chat.message));else for(const auto &f:chat.fragments){
		
        if(!f.cheerPrefix.empty()){
            const CheerTier *selected=nullptr;for(const auto &tier:cheers_)if(tier.prefix==QString::fromStdString(f.cheerPrefix).toLower() && tier.minimum<=f.cheerBits && (!selected || tier.minimum>selected->minimum))selected=&tier;
            if(selected){result.push_back({QString::fromStdString(f.cheerPrefix),selected->url});Token amount;amount.text=QString::number(f.cheerBits);amount.color=selected->color;result.push_back(amount);}else text(QString::fromStdString(f.text));
        }else if(f.emoteId.empty())text(QString::fromStdString(f.text));else result.push_back({QString::fromStdString(f.text),QStringLiteral("https://static-cdn.jtvnw.net/emoticons/v2/%1/%2/dark/2.0").arg(QString::fromStdString(f.emoteId),f.animated?"animated":"static")});
	}return composeTokens(result);
}
std::vector<Token> composeTokens(const std::vector<Token> &input){
	const auto separator=[](const Token &t){return t.url.isEmpty() && t.text.trimmed().isEmpty() && !t.text.contains('\n') && !t.text.contains('\r') && !t.text.contains(QChar(0x2028)) && !t.text.contains(QChar(0x2029));};
	auto tokens=input;std::vector<bool> consumed(tokens.size());std::vector<size_t> spaces;int target=-1;
	// Prefix modifiers point forward; postfix modifiers and overlays point back.
	for(int i=int(tokens.size())-1;i>=0;--i){auto &token=tokens[size_t(i)];
		if(!token.url.isEmpty() && !token.modifier && !token.zeroWidth){target=i;spaces.clear();}
		else if(target>=0 && separator(token))spaces.push_back(size_t(i));
		else if(target>=0 && token.modifier && token.prefix){tokens[size_t(target)].effects|=token.effects;consumed[size_t(i)]=true;for(auto p:spaces)consumed[p]=true;spaces.clear();}
		else{target=-1;spaces.clear();}
	}
	std::vector<Token> result;int base=-1;
	for(size_t i=0;i<tokens.size();++i){if(consumed[i])continue;auto token=tokens[i];if(base>=0 && (result[size_t(base)].effects & NoSpace) && separator(token))continue;
		if(token.modifier && !token.prefix && base>=0){while(int(result.size())>base+1 && separator(result.back()))result.pop_back();result[size_t(base)].effects|=token.effects;continue;}
		if(!token.url.isEmpty() && token.zeroWidth && base>=0){
			while(int(result.size())>base+1 && separator(result.back()))result.pop_back();token.layerOf=base;result.push_back(std::move(token));continue;
		}
		token.layerOf=-1;token.layerRect={0,0,1,1};if(token.modifier){token.effects=0;base=-1;}else if(!token.url.isEmpty())base=int(result.size());else if(!separator(token))base=-1;
		result.push_back(std::move(token));
	}
	for(auto &token:result)if(token.layerOf>=0)token.effects=result[size_t(token.layerOf)].effects;
	return result;
}
EmotePtr EmoteCache::get(const QString &url){
	if(!publicEmoteUrl(url))return {};
	return getResource(url);
}
EmotePtr EmoteCache::getBadge(const QString &url){if(!publicBadgeUrl(url))return {};return getResource(url);}
EmotePtr EmoteCache::getResource(const QString &url){
	for(auto it=cache_.begin();it!=cache_.end();++it)if(it->url==url){auto image=it->image;const auto retry=it->retry;cache_.splice(cache_.begin(),cache_,it);if(!image && std::chrono::steady_clock::now()>=retry)enqueue({url,{},-1});return image;}
	enqueue({url,{},-1});return {};
}
bool EmoteCache::ingest(const QString &url,const QByteArray &bytes){
	if(!publicEmoteUrl(url) && !publicBadgeUrl(url))return false;const auto media=decodeEmote(*visual_,bytes);if(!media)return false;
	const size_t size=size_t(media->atlas->image.sizeInBytes());
	for(auto it=cache_.begin();it!=cache_.end();)if(it->url==url){if(it->image)bytes_-=size_t(it->image->atlas->image.sizeInBytes());it=cache_.erase(it);}else ++it;
	while(cache_.size()>=128 || bytes_+size>16*1024*1024){auto victim=cache_.end();for(auto it=cache_.end();it!=cache_.begin();){--it;if(!it->image || it->image.use_count()==1){victim=it;break;}}if(victim==cache_.end())return false;if(victim->image)bytes_-=size_t(victim->image->atlas->image.sizeInBytes());cache_.erase(victim);}
	cache_.push_front({url,media,{}});bytes_+=size;return true;
}
void EmoteCache::pump(){
	while(running_ && busy_<2 && !pending_.empty()){
		auto request=pending_.front();pending_.pop_front();++busy_;
		const auto weak=QPointer<EmoteCache>(this);fetch_(request.url,[this,weak,request](twitch::HttpReply reply){
			if(!weak || !running_)return;
			--busy_;requested_.erase(request.url);if(!running_)return;
			if(request.provider==-3){
                if(request.channel==cheerChannel_ && request.epoch==cheerEpoch_){cheerPending_=false;const auto parsed=reply.status==200 && !reply.networkError?parseCheermotes(reply.body):std::nullopt;
                    if(parsed){cheers_=*parsed;cheerRetryDue_={};cheerExpires_=clock_()+std::chrono::hours(1);}else{const int delay=reply.status==425?5000:300000;cheerRetryDue_=clock_()+std::chrono::milliseconds(delay);cheerRetry_.start(delay);}notify();}
            }else if(request.provider==-2){
				const auto found=badges_.find(request.channel);
				if(found!=badges_.end()){
					const bool ok=reply.status==200 && !reply.networkError && ingestBadges(request.channel,reply.body);
					if(!ok){found->second.pending=false;const int delay=reply.status==425?5000:300000;found->second.retry=std::chrono::steady_clock::now()+std::chrono::milliseconds(delay);if(!badgeRetry_.isActive() || badgeRetry_.remainingTime()>delay)badgeRetry_.start(delay);}
					notify();
				}
			}else if(request.provider>=0){catalog(request,reply);}
			else{const bool ok=reply.status==200 && !reply.networkError && ingest(request.url,reply.body);if(!ok){if(cache_.size()>=128){for(auto it=cache_.begin();it!=cache_.end();)if(!it->image){it=cache_.erase(it);break;}else ++it;}if(cache_.size()<128)cache_.push_front({request.url,{},std::chrono::steady_clock::now()+std::chrono::minutes(5)});}notify();}
			pump();
		});
	}
}
void EmoteCache::stop(){if(!running_)return;running_=false;personal_->stop();cheerRetry_.stop();cheers_.clear();badgeRetry_.stop();catalogTimer_.stop();changed_={};fetch_={};pending_.clear();requested_.clear();cache_.clear();catalogs_.clear();badges_.clear();bytes_=0;}
}
