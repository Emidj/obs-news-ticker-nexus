// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "localization.hpp"
#include "transport.hpp"
#include "session-store.hpp"
#include "browser-authorization.hpp"
#include "core/plugin-core.hpp"
#include <QJsonArray>
#include <QElapsedTimer>
#include <set>
#include <utility>
#include <deque>
#include "visual/asset-manager.hpp"
namespace oto::twitch {
struct Endpoints {
	QByteArray auth="https://id.twitch.tv/oauth2/",api="https://api.twitch.tv/helix/eventsub/subscriptions",socket="wss://eventsub.wss.twitch.tv/ws";
	QByteArray users="https://api.twitch.tv/helix/users",avatarCdn="https://static-cdn.jtvnw.net/jtv_user_pictures/";
	QByteArray followers="https://api.twitch.tv/helix/channels/followers";
	QByteArray chatMessages="https://api.twitch.tv/helix/chat/messages";
	QByteArray channels="https://api.twitch.tv/helix/channels";
	QByteArray hypeStatus="https://api.twitch.tv/helix/hypetrain/status";
	QByteArray chatBadges="https://api.twitch.tv/helix/chat/badges";
	QByteArray cheermotes="https://api.twitch.tv/helix/bits/cheermotes";
	quint16 browserPort=3000;
 QByteArray categories="https://api.twitch.tv/helix/search/categories";
};
enum class AuthorizationFlow { Device,Browser };
enum class ProfileStatus { Missing,Pending,Ready,Unavailable };
class Service final:public QObject {
public:
	Service(std::shared_ptr<PluginCore> core,QString filename,Endpoints endpoints={});
	~Service()override;
	// Public application identifier; never a client secret or a user credential.
	static QString defaultClient(){return QStringLiteral("xdxao5ubz0csvj28n0s70b7k3g0lnx");}
	void connectAccount(QString client={},AuthorizationFlow flow=AuthorizationFlow::Device);
	void disconnectAccount();
 void authorizeBroadcast();
 bool canManageBroadcast()const{return authorized_&&manageGranted_;}
	void stop();
    state::SnapshotPtr tickerSnapshot()const{return core_->sharedSnapshot();}
    const std::optional<state::ChannelKey> &tickerPanelTest()const{return core_->panelTestChannel();}
	void retryBatch();
	void renewAuthorization();
	bool canRenewAuthorization()const{return active_ && authorized_ && !refreshing_ && !renewalValidation_ && socket_ && !migrating_ && !account_.isEmpty() && index_>=pendingSubscriptions_.size();}
	QString client()const{return session_.client.isEmpty()?defaultClient():session_.client;}
	QString status()const{return status_;}
	QString code()const{return code_;}
	QString verificationUrl()const{return verification_;}
	bool active()const{return active_;}
	bool authorized()const{return authorized_;}
	bool retainedBatch()const{return batchFailed_;}
	void setChanged(std::function<void()> changed){changed_=std::move(changed);}
	// One ABI provider fans out to external consumers; legacy Overlay UI remains independent.
	void setSharedCallbacks(std::function<void()> changed,std::function<void(const events::EventPtr &)> event){sharedChanged_=std::move(changed);sharedEvent_=std::move(event);}
 void streamInformation(std::function<void(HttpReply)> done);
	void channelInformation(std::function<void(HttpReply)> done);
 void searchCategories(QString query,std::function<void(HttpReply)> done);
 void updateChannel(QByteArray payload,std::function<void(HttpReply)> done);
	void setProfileAssets(std::shared_ptr<visual::AssetManager> assets){profileAssets_=std::move(assets);}
	void setProfilesChanged(std::function<void()> changed){profilesChanged_=std::move(changed);}
	ProfileStatus profileStatus(const QString &userId);
	visual::ImagePtr profileImage(const QString &userId);
	void requestProfile(const QString &userId);
	void resolvePreviewUser(QString login,std::function<void(std::optional<events::User>)> done);
	void resolveShoutoutUser(QString login,std::function<void(std::optional<events::ShoutoutEvent>)> done);
	void setShoutoutsEnabled(bool enabled) noexcept;
	void setHypeMeterEnabled(bool enabled);
	bool canSendChat() const noexcept {return active_ && chatWriteGranted_;}
	QString shoutoutChatStatus() const {return shoutoutChatStatus_;}
	void authorizeChatMessages(AuthorizationFlow flow=AuthorizationFlow::Device);
	void fetchChatResource(QString url,std::function<void(HttpReply)> done);
	events::User previewActor()const{return {account_.toStdString(),login_.toStdString()};}
private:
	void notifyChanged();
 bool requestManage_=false,manageGranted_=false;
	std::function<void()> sharedChanged_;
	std::function<void(const events::EventPtr &)> sharedEvent_;
	std::map<Transport::Id,std::function<void(HttpReply)>> chatResources_;
	struct ShoutoutLookup {events::ShoutoutEvent value;std::chrono::steady_clock::time_point expires;};
	std::map<QString,ShoutoutLookup> shoutoutUsers_;
	bool receiveShoutout(const events::EventDraft &chat);
	void pumpShoutouts();
	struct PendingShoutout {events::EventDraft chat;QString target;std::optional<events::ShoutoutEvent> resolved;};
	std::deque<PendingShoutout> shoutoutCommands_;
	void publishShoutoutMessages(const std::vector<events::EventPtr> &delivered);
	void sendShoutoutMessage(QString login);
	std::map<std::string,QString> pendingShoutoutMessages_;
	bool chatWriteGranted_=false,chatAuthorization_=false;
	QString shoutoutChatStatus_;
	bool shoutoutsEnabled_=false,shoutoutBusy_=false;
	std::chrono::steady_clock::time_point shoutoutAccepted_{};
	void pumpProfiles();void finishProfile(const QString &userId,visual::ImagePtr image={});
	void restoreFollowers();bool historyRequested_=false;
	void restoreHypeTrain();bool hypeMeterEnabled_=false;QString hypeStatusSession_;
	struct Profile {ProfileStatus status=ProfileStatus::Missing;std::chrono::steady_clock::time_point expires{};std::weak_ptr<const visual::ImageData> image;};
	std::map<QString,Profile> profiles_;std::weak_ptr<visual::AssetManager> profileAssets_;std::function<void()> profilesChanged_;bool profileBusy_=false;
	void status(QString text);
	void diagnostic(QString text);
	void device();void poll();void tokens(const HttpReply &reply,bool refreshing);
	void browserAuthorization();
	void validate(bool first,bool allowRefresh=true);void refresh();void openSocket(QByteArray url,bool migration=false);
	void receive(Transport::Id id,QByteArray bytes);void lost(Transport::Id id);void reconnect();void subscribe();
	void drain(QString channel);void fail(QString text,bool forget=false);void clearNetwork();
	void http(QByteArray url,QByteArray body,QList<QByteArray> headers,std::function<void(HttpReply)> done);
	std::shared_ptr<PluginCore> core_;Transport transport_;SessionStore store_;Session session_;Endpoints endpoints_;
	BrowserAuthorization browser_;bool authorized_=false,browserAttempt_=false,browserFallback_=false;
	QString status_=i18n::qtext("Status.Disconnected"),code_,verification_,deviceCode_,account_,login_,sessionId_;
	QTimer pollTimer_,validationTimer_,expiryTimer_,keepalive_,retry_,handoffDeadline_,shoutoutDelay_;
	QElapsedTimer authorizationClock_;int authorizationSeconds_=0,pollSeconds_=5,retries_=0,accepted_=0,failed_=0,index_=0,keepaliveMs_=10000;
	std::uint64_t epoch_=0;Transport::Id socket_=0,migrating_=0;QJsonArray pendingSubscriptions_;
	bool active_=false,refreshing_=false,renewalValidation_=false,storageAvailable_=true,batchQueued_=false,batchFailed_=false,stopped_=false;
	QString failedChannel_;std::function<void()> changed_;
	std::set<QString> drainChannels_;
	std::set<std::pair<QString,QString>> activeSubscriptions_;
};
}
