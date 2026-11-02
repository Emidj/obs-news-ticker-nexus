/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "twitch/ticker-feed.hpp"
#include "core/plugin-core.hpp"
#include "adapters/json-daily-store.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <iostream>
using namespace oto;
struct Config:IConfigStore{ConfigLoadResult load()override{return {ConfigStatus::Loaded,{false}};}bool save(Settings)override{return true;}};
struct Log:ILogger{void write(LogLevel,std::string_view)const noexcept override{}};
static int checks,failures;
static void check(bool value,const char *name){checks++;failures+=!value;std::cout<<(value?"PASS ":"FAIL ")<<name<<"\n";}
int main(int argc,char **argv){
 QCoreApplication app(argc,argv);QTemporaryDir dir;
 auto filename=dir.filePath("history.json");
 auto core=std::make_shared<PluginCore>(std::make_unique<Config>(),std::make_unique<Log>(),std::make_unique<JsonDailyStore>(filename));core->start();
 auto push=[&](std::string id,events::EventPayload payload,events::EventOrigin origin=events::EventOrigin::Twitch){
  auto result=core->processEvent({id,"123",std::chrono::system_clock::now(),origin,std::move(payload)});
  core->processPendingEvents(32);return bool(result);
 };
 check(push("follow",events::FollowEvent{{"1","Seguidor ñ"}}),"follow accepted");
 check(push("sub",events::SubEvent{{"2","Subscriber"},events::SubscriptionTier::Tier2,1,true}),"new subscription accepted");
 check(push("cheer",events::CheerEvent{{},250}),"anonymous cheer accepted");
 check(push("gift",events::GiftEvent{{},5,events::SubscriptionTier::Tier1}),"anonymous gift accepted");
 check(push("raid",events::RaidEvent{{"3","Raider"},42,"raider"}),"raid accepted");
 auto feed=twitch::tickerFeed(core->sharedSnapshot(),"123",true,true,9);
 check(feed.size==sizeof(feed)&&feed.instance==9,"fixed C snapshot shape");
 check(feed.latest[0].present&&std::string(feed.latest[0].actor)=="Seguidor ñ","UTF8 follow name");
 check(feed.latest[2].anonymous&&feed.latest[2].amount==250,"anonymous bits retained");
 check(feed.latest[3].anonymous&&feed.latest[3].amount==5,"anonymous gifts retained");
 check(feed.latest[4].amount==42,"raid viewer count retained");
 check(feed.daily_subs==6&&feed.daily_bits==250,"gifts counted once in observed daily subs");
 check(!push("gift",events::GiftEvent{{},5,events::SubscriptionTier::Tier1}),"duplicate rejected");
 check(push("resub",events::SubEvent{{"2","Subscriber"},events::SubscriptionTier::Tier2,12,false}),"resub message accepted");
 check(push("gift-recipient",events::SubEvent{{"4","Recipient"},events::SubscriptionTier::Tier1,1,false}),"gift recipient accepted");
 feed=twitch::tickerFeed(core->sharedSnapshot(),"123",true,true,9);
 check(feed.daily_subs==6,"resub and gift recipient do not count twice");
 check(!twitch::tickerFeed(core->sharedSnapshot(),"999",true,true,9).latest[0].present,"account isolation");
 check(!twitch::tickerFeed(core->sharedSnapshot(),"123",true,false,9).latest[0].present,"unauthorized hides history");
 check(!twitch::tickerFeed(core->sharedSnapshot(),"123",false,true,9).daily_subs,"inactive hides totals");
 check(push("sim",events::CheerEvent{{},999},events::EventOrigin::Simulation),"simulation accepted separately");
 check(twitch::tickerFeed(core->sharedSnapshot(),"123",true,true,9).daily_bits==250,"simulation excluded from Twitch totals");
 core->stop();core.reset();
 core=std::make_shared<PluginCore>(std::make_unique<Config>(),std::make_unique<Log>(),std::make_unique<JsonDailyStore>(filename));core->start();
 feed=twitch::tickerFeed(core->sharedSnapshot(),"123",true,true,10);
 check(feed.daily_subs==6&&feed.daily_bits==250,"observed totals survive restart");
 check(feed.latest[0].present&&feed.latest[2].present&&feed.latest[3].present,"stored activity restored quietly");
 check(feed.latest[4].present&&feed.latest[4].amount==42,"last raid restored without replay");
 check(core->sharedSnapshot()->channels.at({events::EventOrigin::Twitch,"123"})->activityDelivery==0,"restored history is not delivered as a live alert");
 auto old=std::make_shared<state::Snapshot>(*core->sharedSnapshot());
 auto oldChannel=std::make_shared<state::ChannelState>(*old->channels.at({events::EventOrigin::Twitch,"123"}));oldChannel->daily.day=20200101;
 old->channels[{events::EventOrigin::Twitch,"123"}]=oldChannel;
 auto nextDay=twitch::tickerFeed(old,"123",true,true,10);
 check(!nextDay.daily_subs&&!nextDay.daily_bits&&nextDay.latest[4].present,"new local day resets totals and retains last events");
 const auto corrupt=dir.filePath("corrupt.json");{QFile file(corrupt);file.open(QIODevice::WriteOnly);file.write("invalid-json");}
 JsonDailyStore protectedStore(corrupt);bool refused=false;try{protectedStore.load();}catch(...){refused=true;}
 QFile protectedFile(corrupt);protectedFile.open(QIODevice::ReadOnly);
 check(refused&&!protectedStore.save({})&&protectedFile.readAll()=="invalid-json","invalid history protected from overwrite");
 events::HypeTrainEvent train;train.stage=events::HypeTrainStage::Begin;train.level=3;train.goal=5000;train.total=8900;train.progress=3350;train.trainId="train-live";
 train.startedAt=std::chrono::system_clock::now()-std::chrono::seconds(10);train.expiresAt=std::chrono::system_clock::now()+std::chrono::seconds(134);
 check(push("hype-begin",train),"live Hype begin accepted");
 auto hype=twitch::hypeFeed(core->sharedSnapshot(),"123",true,true,10);
 check(hype.present&&hype.level==3&&hype.progress==3350&&hype.goal==5000&&hype.expires_ms>hype.started_ms,"Hype publishes real level progress goal and expiry");
 check(!twitch::hypeFeed(core->sharedSnapshot(),"999",true,true,10).present,"Hype never crosses channels");
 check(!twitch::hypeFeed(core->sharedSnapshot(),"123",true,false,10).present,"Hype cleared without authorization");
 train.stage=events::HypeTrainStage::Progress;train.progress=4000;train.total=9550;
 check(push("hype-progress",train),"live Hype progress accepted");
 check(twitch::hypeFeed(core->sharedSnapshot(),"123",true,true,10).progress==4000,"Hype progress reaches extension");
 auto expired=std::make_shared<state::Snapshot>(*core->sharedSnapshot());auto expiredChannel=std::make_shared<state::ChannelState>(*expired->channels.at({events::EventOrigin::Twitch,"123"}));
 expiredChannel->hypeTrain->expiresAt=std::chrono::system_clock::now()-std::chrono::seconds(1);expired->channels[{events::EventOrigin::Twitch,"123"}]=expiredChannel;
 check(!twitch::hypeFeed(expired,"123",true,true,10).present,"expired Hype cannot remain visible");
 train.stage=events::HypeTrainStage::End;check(push("hype-end",train),"live Hype end accepted");
 check(!twitch::hypeFeed(core->sharedSnapshot(),"123",true,true,10).present,"ended Hype disappears");
 check(bool(core->simulatePanelEvent(events::EventType::HypeTrain)),"Overlays panel Hype simulation accepted");
 auto preview=twitch::hypePreviewFeed(core->sharedSnapshot(),core->panelTestChannel(),10);
 check(preview.present&&preview.active&&!preview.authorized&&preview.progress==750,"Overlays TEST has its own unauthenticated preview snapshot");
 check(!twitch::hypeFeed(core->sharedSnapshot(),"123",true,true,10).present,"panel TEST cannot resurrect a real ended Hype Train");
 core->endPanelTest();check(!twitch::hypePreviewFeed(core->sharedSnapshot(),core->panelTestChannel(),10).present,"ending Overlays TEST clears preview");
 core->stop();std::cout<<"RESULT checks="<<checks<<" failures="<<failures<<"\n";return failures?1:0;
}
