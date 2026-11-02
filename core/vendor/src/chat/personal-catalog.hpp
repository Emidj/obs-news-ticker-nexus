// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "emote-cache.hpp"
#include <QPointer>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
namespace oto::chat {
// Per-author, read-only catalogs. Never register a channel set as a personal set.
class PersonalCatalog final:public QObject {
public:
 PersonalCatalog(EmoteCache::Fetch fetch,EmoteCache::Clock clock,std::function<void()> changed):fetch_(std::move(fetch)),clock_(std::move(clock)),changed_(std::move(changed)){
  retry_.setSingleShot(true);connect(&retry_,&QTimer::timeout,this,[this]{for(auto &[id,u]:users_)if(u.watched && !u.pending && clock_()>=u.expires)request(id,u);pump();schedule();});
  socketRetry_.setSingleShot(true);connect(&socketRetry_,&QTimer::timeout,this,[this]{openFeed();});
 }
 ~PersonalCatalog()override{stop();}
 void enableFeed(){feedEnabled_=true;if(active_)openFeed();}
 void select(QString channel,bool active){
  if(channel_!=channel){++epoch_;queue_.clear();users_.clear();channel_=channel;closeFeed();}
  if(active_!=active){active_=active;++epoch_;queue_.clear();for(auto &[id,u]:users_)u.pending=false;if(!active){retry_.stop();closeFeed();}}
  if(active_){openFeed();schedule();}
 }
 void ensure(const QString &id){if(!active_ || !running_ || !validUser(id) || !fetch_)return;
  if(!users_.count(id)){if(users_.size()>=32){auto victim=users_.begin();for(auto it=users_.begin();it!=users_.end();++it)if(it->second.used<victim->second.used)victim=it;users_.erase(victim);}users_[id].generation=++generation_;}
  auto &u=users_[id];u.watched=true;u.used=++used_;if(!u.pending && clock_()>=u.expires)request(id,u);pump();schedule();
 }
 const CatalogEntry *find(const QString &id,const QString &word)const{const auto user=users_.find(id);if(user==users_.end())return nullptr;for(const auto &provider:user->second.entries){const auto e=provider.find(word);if(e!=provider.end())return &e->second;}return nullptr;}
 bool ingestBTTV(const QByteArray &bytes){
  if(!active_ || bytes.size()>128*1024)return false;const auto root=QJsonDocument::fromJson(bytes).object();if(root.value("name")!="lookup_user")return false;const auto data=root.value("data").toObject();const auto id=data.value("providerId").toString();if(!validUser(id) || !data.value("pro").isBool() || !data.value("emotes").isArray())return false;
  if(!users_.count(id)){if(users_.size()>=32){auto victim=users_.begin();for(auto it=users_.begin();it!=users_.end();++it)if(it->second.used<victim->second.used)victim=it;users_.erase(victim);}users_[id].generation=++generation_;}users_[id].used=++used_;
  const auto old=users_[id].entries[1];auto &target=users_[id].entries[1];target.clear();if(data.value("pro").toBool()){
   const auto parsed=parseCatalog(1,QJsonDocument(data.value("emotes").toArray()).toJson(QJsonDocument::Compact),false);for(const auto &e:parsed){if(target.size()>=256)break;target.emplace(e.name,e);}
  }if(old!=target)changed_();return true;
 }
 void stop(){if(!running_)return;running_=false;active_=false;++epoch_;retry_.stop();closeFeed();queue_.clear();users_.clear();fetch_={};changed_={};}
private:
 struct User {std::array<std::map<QString,CatalogEntry>,3> entries;std::map<QString,CatalogEntry> sevenStaged;std::uint64_t generation=0,used=0;bool pending=false,sevenValid=false,watched=false;int outstanding=0,sevenRemaining=0;std::chrono::steady_clock::time_point expires{};};
 struct Request {QString user,url,set,owner;int provider=0;std::uint64_t epoch=0,generation=0;};
 static bool validUser(const QString &id){static const QRegularExpression re("^[0-9]{1,32}$");return re.match(id).hasMatch();}
 static bool validSeven(const QString &id){static const QRegularExpression re("^[A-Za-z0-9]{24,32}$");return re.match(id).hasMatch();}
 void request(const QString &id,User &u){if(queue_.size()>60)return;u.pending=true;u.outstanding=2;u.expires=clock_()+std::chrono::minutes(5);queue_.push_back({id,"https://7tv.io/v3/users/twitch/"+id,{}, {},0,epoch_,u.generation});queue_.push_back({id,"https://api.frankerfacez.com/v1/user/id/"+id,{}, {},2,epoch_,u.generation});}
 void pump(){while(running_ && active_ && busy_<2 && !queue_.empty()){
  auto r=queue_.front();queue_.pop_front();const auto current=users_.find(r.user);if(r.epoch!=epoch_ || current==users_.end() || current->second.generation!=r.generation)continue;++busy_;const auto weak=QPointer<PersonalCatalog>(this);fetch_(r.url,[this,weak,r](twitch::HttpReply reply){if(!weak || !running_)return;--busy_;const auto found=users_.find(r.user);if(active_ && r.epoch==epoch_ && found!=users_.end() && found->second.generation==r.generation){auto &u=found->second;
   if(reply.status==200 && !reply.networkError && reply.body.size()<=2*1024*1024){const auto root=QJsonDocument::fromJson(reply.body).object();std::optional<std::vector<CatalogEntry>> parsed;
    if(r.provider==2){const auto user=root.value("user").toObject();if(user.value("twitch_id").toVariant().toString()==r.user && user.value("emote_sets").isArray() && root.value("sets").isObject()){
     QJsonObject filtered;const auto sets=root.value("sets").toObject();for(const auto &id:user.value("emote_sets").toArray()){const auto key=QString::number(id.toInt());if(sets.contains(key))filtered.insert(key,sets.value(key));}parsed=parseCatalogSnapshot(2,QJsonDocument(QJsonObject{{"sets",filtered}}).toJson(),true);}
    }else if(r.set.isEmpty()){
     if(root.value("id").toString()==r.user && root.value("platform").toString().compare("TWITCH",Qt::CaseInsensitive)==0){const auto user=root.value("user").toObject();const auto owner=user.value("id").toString();if(validSeven(owner)){
      parsed=std::vector<CatalogEntry>{};u.sevenStaged.clear();u.sevenValid=true;u.sevenRemaining=0;int added=0;for(const auto &v:user.value("emote_sets").toArray()){const auto set=v.toObject();const auto id=set.value("id").toString();if((set.value("flags").toInt()&4) && validSeven(id) && added<4 && queue_.size()<64){++added;++u.outstanding;++u.sevenRemaining;queue_.push_back({r.user,"https://7tv.io/v3/emote-sets/"+id,id,owner,0,r.epoch,r.generation});}}
     }}
    }else if(root.value("id").toString()==r.set && (root.value("flags").toInt()&4) && (root.value("owner").isNull() || root.value("owner").toObject().value("id").toString()==r.owner)){
     auto snapshot=root;if(!snapshot.contains("emotes"))snapshot.insert("emotes",QJsonArray{});parsed=parseCatalogSnapshot(0,QJsonDocument(snapshot).toJson(),false);
    }
    if(parsed){auto before=u.entries[r.provider];auto &target=r.provider==0?u.sevenStaged:u.entries[r.provider];if(r.provider!=0)target.clear();for(const auto &e:*parsed){if(target.size()>=256)break;target[e.name]=e;}
     if(r.provider==0 && u.sevenRemaining==0)u.entries[0]=u.sevenStaged;
     if(before!=u.entries[r.provider])QTimer::singleShot(0,this,[this]{if(running_ && active_)changed_();});
    }else if(r.provider==0 && !r.set.isEmpty())u.sevenValid=false;
   }else if(r.provider==0 && !r.set.isEmpty())u.sevenValid=false;
   if(r.provider==0 && !r.set.isEmpty() && --u.sevenRemaining==0 && u.sevenValid){const bool changed=u.entries[0]!=u.sevenStaged;u.entries[0]=std::move(u.sevenStaged);if(changed)QTimer::singleShot(0,this,[this]{if(running_ && active_)changed_();});
   }
   if(--u.outstanding<=0)u.pending=false;
  }pump();schedule();});
 }}
 void schedule(){retry_.stop();if(!active_ || !running_ || users_.empty())return;auto due=clock_()+std::chrono::minutes(5);bool waiting=false;for(const auto &[id,u]:users_)if(u.watched && !u.pending){due=std::min(due,u.expires);waiting=true;}if(waiting)retry_.start(int(std::clamp<std::int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(due-clock_()).count(),1000,300000)));}
 void closeFeed(){socketRetry_.stop();if(network_)network_->stop();socket_=0;socketFailures_=0;}
 void openFeed(){if(!running_ || !active_ || !feedEnabled_ || socket_ || socketRetry_.isActive() || !validUser(channel_))return;
  if(!network_)network_=std::make_unique<twitch::Transport>();const auto epoch=epoch_;
  try{socket_=network_->websocket("wss://sockets.betterttv.net/ws",[this,epoch]{if(epoch!=epoch_ || !active_)return;socketFailures_=0;network_->sendText(socket_,QJsonDocument(QJsonObject{{"name","join_channel"},{"data",QJsonObject{{"name","twitch:"+channel_}}}}).toJson(QJsonDocument::Compact));},[this,epoch](QByteArray b){if(epoch==epoch_)ingestBTTV(b);},[this,epoch](twitch::SocketClose){if(epoch!=epoch_ || !active_)return;socket_=0;socketRetry_.start(std::min(300000,5000*(1<<std::min(socketFailures_++,6))));});}
  catch(...){socket_=0;socketRetry_.start(30000);}
 }
 EmoteCache::Fetch fetch_;EmoteCache::Clock clock_;std::function<void()> changed_;std::map<QString,User> users_;std::deque<Request> queue_;QTimer retry_,socketRetry_;std::unique_ptr<twitch::Transport> network_;twitch::Transport::Id socket_=0;QString channel_;int busy_=0,socketFailures_=0;std::uint64_t epoch_=0,generation_=0,used_=0;bool running_=true,active_=false,feedEnabled_=false;
};
}
