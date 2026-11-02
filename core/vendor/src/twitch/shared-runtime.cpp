// SPDX-License-Identifier: GPL-2.0-or-later
#include "shared-runtime.hpp"
#include "shared-provider.hpp"
#include "adapters/json-config-store.hpp"
#include "adapters/json-daily-store.hpp"
#include "adapters/obs-logger.hpp"
#include <QCoreApplication>
#include <QThread>
#include <QFileInfo>
#include <QDir>
#include "localization.hpp"
namespace {
std::shared_ptr<oto::PluginCore> standalone;
std::shared_ptr<oto::PluginCore> runtimeCore;
std::shared_ptr<oto::twitch::Service> service;
unsigned owners=0;
bool ui(){return QCoreApplication::instance()&&QThread::currentThread()==QCoreApplication::instance()->thread();}
}
namespace oto::twitch {
std::shared_ptr<Service> retainRuntime(std::shared_ptr<PluginCore> core,QString session){
 if(!ui()||session.isEmpty())return {};
 if(service){++owners;return service;}
 if(!core){
  const auto directory=QFileInfo(session).absolutePath();
  standalone=std::make_shared<PluginCore>(std::make_unique<JsonConfigStore>(QDir(directory).filePath("core-settings.json")),std::make_unique<ObsLogger>(),std::make_unique<oto::JsonDailyStore>(QDir(directory).filePath("ticker-history.json")));
  standalone->start();core=standalone;
 }
 try{runtimeCore=core;service=std::make_shared<Service>(core,session);publishSharedService(service);owners=1;return service;}
 catch(...){service.reset();runtimeCore.reset();if(standalone)standalone->stop();standalone.reset();throw;}
}
}
extern "C" int nexus_twitch_runtime_retain(const char *path){
 if(!path)return 0;try{return oto::twitch::retainRuntime({},QString::fromUtf8(path))?1:0;}catch(...){return 0;}
}
extern "C" void nexus_twitch_runtime_release(){
 if(!ui()||!owners)return;
 if(--owners)return;
 oto::twitch::unpublishSharedService();if(service)service->stop();service.reset();
 if(runtimeCore)runtimeCore->stop();runtimeCore.reset();standalone.reset();
}
extern "C" int nexus_twitch_runtime_active(){return ui()&&owners!=0;}
extern "C" void nexus_twitch_runtime_set_locale(const char *locale){if(ui()&&locale)nexus::localization::setApplicationLanguage(QString::fromUtf8(locale));}

#include "twitch-core-version.h"
#include <windows.h>
#include <cstring>
static_assert(NEXUS_TWITCH_CORE_ABI==NEXUS_TWITCH_ABI,"Version manifest ABI differs from API");
extern "C" int nexus_twitch_core_info(NexusTwitchCoreInfo *out,uint32_t size){
 if(!out||size<sizeof(NexusTwitchCoreInfo))return 0;
 *out={};out->size=sizeof(*out);out->abi=NEXUS_TWITCH_ABI;
 std::memcpy(out->version,NEXUS_TWITCH_CORE_VERSION,sizeof(NEXUS_TWITCH_CORE_VERSION));
 HMODULE module=nullptr;
 if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
     reinterpret_cast<LPCWSTR>(&nexus_twitch_core_info),&module))return 0;
 auto length=GetModuleFileNameW(module,reinterpret_cast<wchar_t*>(out->module_path),32768);
 return length>0&&length<32768;
}
