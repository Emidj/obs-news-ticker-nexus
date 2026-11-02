// SPDX-License-Identifier: GPL-2.0-or-later
#include "twitch/shared-provider.hpp"
#include "twitch-core-client.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <iostream>
#include <thread>
#include <QUrlQuery>
#include <QElapsedTimer>
#include <QThread>
#include "twitch-core-diagnostics.hpp"
#include "twitch-ticker-api.h"
#include "twitch-hype-api.h"
extern "C" __declspec(dllimport) const NexusHypeApi *nexus_twitch_core_get_hype_api(uint32_t,uint32_t);
extern "C" __declspec(dllimport) const NexusHypeApi *nexus_twitch_core_get_hype_preview_api(uint32_t,uint32_t);
static void hypeChanged(void *context,const NexusHypeSnapshot *){++*static_cast<int*>(context);}
struct PreviewProbe{NexusHypeSnapshot last{};int calls=0;};
static void previewChanged(void *context,const NexusHypeSnapshot *message){auto &probe=*static_cast<PreviewProbe*>(context);probe.last=*message;++probe.calls;}
static void pumpPreview(){QElapsedTimer elapsed;elapsed.start();while(elapsed.elapsed()<350){QCoreApplication::processEvents();QThread::msleep(10);}}
extern "C" __declspec(dllimport) const NexusTickerApi *nexus_twitch_core_get_ticker_api(uint32_t,uint32_t);
using namespace oto;
struct FeedNotifications {int count=0,closing=0;};
static void feedChanged(void *context,const NexusTickerSnapshot *message){auto &state=*static_cast<FeedNotifications *>(context);++state.count;if(message->kind==NEXUS_TICKER_CLOSING)++state.closing;}
static_assert(sizeof(NexusTickerEvent)==672&&sizeof(NexusTickerSnapshot)==3544,"Windows x64 ticker ABI layout changed");
struct Config:IConfigStore{ConfigLoadResult load()override{return {ConfigStatus::Loaded,{false}};}bool save(Settings)override{return true;}};
struct Log:ILogger{void write(LogLevel,std::string_view)const noexcept override{}};
#define CHECK(x) do{if(!(x)){std::cerr<<"FAIL "<<__LINE__<<"\n";return 1;}}while(false)
int main(int argc,char **argv){QCoreApplication app(argc,argv);QTemporaryDir dir;
 NexusTwitchCoreInfo info{};CHECK(!nexus_twitch_core_info(nullptr,sizeof(info)));CHECK(!nexus_twitch_core_info(&info,1));CHECK(nexus_twitch_core_info(&info,sizeof(info))&&nexus::compatibleCore(info));CHECK(QString::fromLatin1(info.version)=="1.3.1"&&nexus::corePath(info).endsWith("nexus-twitch-core.dll"));
 info.abi=7;CHECK(!nexus::compatibleCore(info));info.abi=6;std::strcpy(info.version,"2.0.0");CHECK(!nexus::compatibleCore(info));std::strcpy(info.version,"1.1.0");CHECK(nexus::compatibleCore(info));
 auto core=std::make_shared<PluginCore>(std::make_unique<Config>(),std::make_unique<Log>());core->start();
 auto service=std::make_shared<twitch::Service>(core,dir.filePath("session.bin"));
 int legacy=0;service->setChanged([&]{++legacy;});twitch::publishSharedService(service);
 const auto *api=nexus_twitch_core_get_api(NEXUS_TWITCH_ABI,sizeof(NexusTwitchApi));CHECK(api);
 CHECK(!nexus_twitch_core_get_api(99,sizeof(NexusTwitchApi))&&!nexus_twitch_core_get_api(NEXUS_TWITCH_ABI,0));
 dst::TwitchCoreClient first,second;int a=0,b=0;
 first.setChanged([&](uint32_t){++a;});second.setChanged([&](uint32_t){++b;});
 CHECK(first.attach(api)&&second.attach(api));NexusTwitchMessage x{},y{};
 CHECK(first.snapshot(x)&&second.snapshot(y)&&x.instance==y.instance&&!x.active);
 const auto *panelApi=nexus_twitch_core_get_hype_preview_api(1,sizeof(NexusHypeApi));CHECK(panelApi);auto panelLease=api->acquire(nullptr,nullptr);CHECK(panelLease);PreviewProbe probe;CHECK(panelApi->bind(panelLease,previewChanged,&probe));
 CHECK(bool(core->simulatePanelEvent(events::EventType::HypeTrain)));pumpPreview();
 CHECK(probe.calls>0&&probe.last.present&&probe.last.active&&!probe.last.authorized&&std::string(probe.last.account).rfind("panel-test-",0)==0);
 core->endPanelTest();pumpPreview();CHECK(!probe.last.present);
 api->release(panelLease);const auto stoppedCalls=probe.calls;pumpPreview();CHECK(probe.calls==stoppedCalls);

 service->connectAccount("bad");CHECK(legacy==1&&a==1&&b==1);
 first.detach();service->connectAccount("bad");CHECK(legacy==2&&a==1&&b==2&&second.available());
 uint64_t rejected=99;std::thread foreign([&]{rejected=api->acquire(nullptr,nullptr);});foreign.join();CHECK(rejected==0);
 auto stale=api->acquire(nullptr,nullptr);CHECK(stale);
 api->release(stale);CHECK(!api->snapshot(stale,&x,sizeof(x)));
 twitch::unpublishSharedService();CHECK(!second.available()&&b==3&&!api->snapshot(stale,&x,sizeof(x)));
 CHECK(!api->acquire(nullptr,nullptr));second.detach();
 // The provider may be registered again; stale handles never address a new owner.
 twitch::publishSharedService(service);CHECK(first.attach(api)&&first.snapshot(y)&&y.instance!=x.instance);
 twitch::unpublishSharedService();service->stop();core->stop();
 // Independent runtime, two plugin owners: no Overlay DLL and no browser UI.
 const auto path=dir.filePath("standalone/session.bin").toUtf8();
 nexus_twitch_runtime_set_locale("fr-FR"); // No OBS instance is required by the shared runtime.
 CHECK(nexus_twitch_runtime_retain(path.constData()));
 CHECK(nexus_twitch_runtime_retain(path.constData()));
 CHECK(first.attach(api)&&second.attach(api));
 CHECK(first.snapshot(x)&&second.snapshot(y)&&x.instance==y.instance&&!x.active);
 nexus_twitch_runtime_release();CHECK(second.snapshot(y));
 CHECK(second.authorize());CHECK(second.snapshot(y)&&y.active&&!y.authorized);
 CHECK(QUrlQuery(QUrl(QString::fromUtf8(y.verification))).queryItemValue("scope",QUrl::FullyDecoded).split(' ').contains("channel:manage:broadcast"));
 CHECK(second.disconnect());CHECK(second.snapshot(y)&&!y.active&&!*y.verification);
 first.detach();nexus_twitch_runtime_release();CHECK(!second.available()&&!nexus_twitch_runtime_active());
 // Ticker-only authorization does not request broadcast editing or chat writing.
 CHECK(nexus_twitch_runtime_retain(path.constData()));
 const auto *feed=nexus_twitch_core_get_ticker_api(1,sizeof(NexusTickerApi));CHECK(feed);
 CHECK(!nexus_twitch_core_get_ticker_api(99,sizeof(NexusTickerApi))&&!nexus_twitch_core_get_ticker_api(1,0));
 auto tickerLease=api->acquire(nullptr,nullptr);CHECK(tickerLease);
 auto otherTicker=api->acquire(nullptr,nullptr);CHECK(otherTicker);
 const auto *hypeApi=nexus_twitch_core_get_hype_api(1,sizeof(NexusHypeApi));CHECK(hypeApi);
 CHECK(!nexus_twitch_core_get_hype_api(99,sizeof(NexusHypeApi))&&!nexus_twitch_core_get_hype_api(1,0));
 int hypeChanges=0;CHECK(hypeApi->bind(tickerLease,hypeChanged,&hypeChanges));NexusHypeSnapshot hype{};
 CHECK(hypeApi->snapshot(tickerLease,&hype,sizeof(hype))&&!hype.present&&!hype.authorized);
 FeedNotifications notices,otherNotices;CHECK(feed->bind(tickerLease,feedChanged,&notices)&&feed->bind(otherTicker,feedChanged,&otherNotices));
 NexusTickerSnapshot ticker{};CHECK(feed->snapshot(tickerLease,&ticker,sizeof(ticker))&&!ticker.authorized&&!*ticker.account);
 CHECK(feed->authorize(tickerLease));CHECK(api->snapshot(tickerLease,&x,sizeof(x)));
 const auto scopes=QUrlQuery(QUrl(QString::fromUtf8(x.verification))).queryItemValue("scope",QUrl::FullyDecoded).split(' ');
 CHECK(scopes.contains("moderator:read:followers")&&scopes.contains("channel:read:subscriptions")&&scopes.contains("bits:read"));
 CHECK(!scopes.contains("user:write:chat")&&!scopes.contains("channel:manage:broadcast"));
 CHECK(notices.count>0&&otherNotices.count>0&&hypeChanges>0);
 api->release(otherTicker);const auto releasedCount=otherNotices.count;
 CHECK(api->disconnect(tickerLease));CHECK(otherNotices.count==releasedCount);
 nexus_twitch_runtime_release();CHECK(notices.closing==1&&otherNotices.closing==0);
 CHECK(!feed->snapshot(tickerLease,&ticker,sizeof(ticker))&&!nexus_twitch_runtime_active());
 CHECK(!hypeApi->snapshot(tickerLease,&hype,sizeof(hype)));
 CHECK(nexus_twitch_runtime_retain(path.constData()));
 const auto *previewApi=nexus_twitch_core_get_hype_preview_api(1,sizeof(NexusHypeApi));CHECK(previewApi&&!nexus_twitch_core_get_hype_preview_api(1,0));
 auto previewLease=api->acquire(nullptr,nullptr);CHECK(previewLease);int previewChanges=0;CHECK(previewApi->bind(previewLease,hypeChanged,&previewChanges));
 nexus_twitch_runtime_release();CHECK(!previewApi->snapshot(previewLease,&hype,sizeof(hype)));
 std::cout<<"PASS: one owner, two consumers, legacy notifications, release isolation, ABI/thread checks, shutdown invalidation, stale lease rejection\n";
}

