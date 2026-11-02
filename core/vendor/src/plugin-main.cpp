// SPDX-License-Identifier: GPL-2.0-or-later
#include <obs-module.h>
#include <obs-frontend-api.h>

#include "ui/main-panel.hpp"
#include "core/plugin-core.hpp"
#include "adapters/json-config-store.hpp"
#include "adapters/obs-logger.hpp"
#include "adapters/obs-visual-renderer.hpp"
#include "adapters/json-daily-store.hpp"
#include "sources/supporter-source.hpp"
#include "sources/event-list-source.hpp"
#include "sources/alert-source.hpp"
#include "sources/shoutout-source.hpp"
#include "sources/hype-source.hpp"
#include "sources/chat-source.hpp"
#include "twitch/service.hpp"
#include "twitch/shared-provider.hpp"
#include "twitch/shared-runtime.hpp"
#include "twitch-core-diagnostics.hpp"
#include <memory>
#include <stdexcept>
#include <QDir>
#include <QFileInfo>
#include "localization.hpp"

OBS_DECLARE_MODULE()
// NEXUS follows the operating system, independently of OBS's UI override.
const char *obs_module_text(const char *key){return oto::i18n::text(key);}
bool obs_module_get_string(const char *key,const char **out){
    if(!oto::i18n::catalog().contains(key))return false;*out=oto::i18n::text(key);return true;
}
void obs_module_set_locale(const char *){(void)oto::i18n::catalog();}
void obs_module_free_locale(){} // Immutable embedded catalogs live with the module.

namespace {
bool callbackRegistered = false;
std::shared_ptr<oto::PluginCore> core;
std::shared_ptr<oto::visual::VisualEngine> visual;
std::shared_ptr<oto::ObsVisualRenderer> renderer;
std::shared_ptr<oto::sources::SupporterHub> supporter;
std::shared_ptr<oto::sources::EventListHub> eventList;
std::shared_ptr<oto::sources::AlertHub> alerts;
std::shared_ptr<oto::sources::ShoutoutHub> shoutouts;
std::shared_ptr<oto::sources::HypeHub> hype;
std::shared_ptr<oto::sources::ChatHub> chat;
std::shared_ptr<oto::chat::EmoteCache> chatMedia;
std::shared_ptr<oto::twitch::Service> twitch;
std::shared_ptr<const oto::presets::Registry> presets;
std::shared_ptr<oto::visual::AssetManager> assets;

void releaseVisuals() noexcept
{
	if (!renderer && !visual) return;
	if (supporter) supporter->stop();
	if (eventList) eventList->stop();
	if (alerts) alerts->stop();
	if (shoutouts) shoutouts->stop();
	if (hype) hype->stop();
	if (chat) chat->stop();
	if (chatMedia) chatMedia->stop();
	chatMedia.reset();
	if (renderer) renderer->clear();
	if (visual) visual->clear();
	if (assets) assets->clear();
	renderer.reset(); visual.reset();
	assets.reset();presets.reset();
	if (core) core->log(oto::LogLevel::Info, "Visual engine and GPU cache released");
}

void onFrontendEvent(enum obs_frontend_event event, void *)
{
	if (event != OBS_FRONTEND_EVENT_EXIT)
		return;

	// EXIT is the last point at which OBS guarantees the frontend API is alive.
	if(core){core->setStateChanged({});core->presentations().setChanged({});} if(twitch){twitch->setChanged({});twitch->setProfilesChanged({});nexus_twitch_runtime_release();}
	twitch.reset();
	oto::ui::destroyMainPanel();
	releaseVisuals();
	if (core && !nexus_twitch_runtime_active())
		core->stop();
	obs_frontend_remove_event_callback(onFrontendEvent, nullptr);
	callbackRegistered = false;
}
} // namespace

MODULE_EXPORT int nexus_overlay_uses_shared_runtime(){return 1;}

MODULE_EXPORT const char *obs_module_name(void)
{
	return "OBS Twitch Overlays NEXUS";
}

MODULE_EXPORT const char *obs_module_description(void)
{
	return "OBS Twitch Overlays NEXUS: shared plugin core and native control panel.";
}

bool obs_module_load(void)
{
	try {
        NexusTwitchCoreInfo identity{};
        if(!nexus_twitch_core_info(&identity,sizeof(identity))||!nexus::compatibleCore(identity,NEXUS_OVERLAY_MIN_VERSION,NEXUS_OVERLAY_MAX_VERSION,NEXUS_OVERLAY_ABI))throw std::runtime_error("Incompatible Twitch Core: requires ABI 6 and version >=1.0.0, <2.0.0");
        blog(LOG_INFO,"[obs-twitch-overlays] Twitch Core %s ABI %u loaded from %s",identity.version,identity.abi,nexus::corePath(identity).toUtf8().constData());
		if (core)
			return core->running();
		char *configPath = obs_module_config_path("settings.json");
		const auto filename = QString::fromUtf8(configPath ? configPath : "");
		bfree(configPath);
		char *dailyPath = obs_module_config_path("daily-totals.json");
		const auto dailyFilename = QString::fromUtf8(dailyPath ? dailyPath : ""); bfree(dailyPath);
		core = std::make_shared<oto::PluginCore>(std::make_unique<oto::JsonConfigStore>(filename),
						 std::make_unique<oto::ObsLogger>(), std::make_unique<oto::JsonDailyStore>(dailyFilename));
		core->start();
		char *sessionPath = obs_module_config_path("../nexus-twitch-core/twitch-session.bin");
		const auto sessionFilename = QString::fromUtf8(sessionPath ? sessionPath : ""); bfree(sessionPath);
		nexus_twitch_runtime_set_locale(obs_get_locale());
        twitch = oto::twitch::retainRuntime(core,sessionFilename);
        if(!twitch)throw std::runtime_error("Shared Twitch runtime unavailable");
		visual = std::make_shared<oto::visual::VisualEngine>();
		renderer = std::make_shared<oto::ObsVisualRenderer>();
		char *presetPath=obs_module_file("presets/registry.json");
		const auto presetFilename=QString::fromUtf8(presetPath?presetPath:"");bfree(presetPath);
		presets=std::make_shared<const oto::presets::Registry>(oto::presets::Registry::fromFile(presetFilename));
		assets=std::make_shared<oto::visual::AssetManager>(QDir(QFileInfo(presetFilename).absolutePath()).filePath("assets"));
		supporter = std::make_shared<oto::sources::SupporterHub>(core,visual,renderer,presets,assets);
		eventList = std::make_shared<oto::sources::EventListHub>(core,visual,renderer,presets,assets);
		twitch->setProfileAssets(assets);
		alerts=std::make_shared<oto::sources::AlertHub>(core,visual,renderer,presets,assets,twitch);
		shoutouts=std::make_shared<oto::sources::ShoutoutHub>(core,visual,renderer,presets,assets,twitch);
		hype=std::make_shared<oto::sources::HypeHub>(core,visual,renderer,twitch,presets,assets);
		chatMedia=std::make_shared<oto::chat::EmoteCache>(visual,[weak=std::weak_ptr<oto::twitch::Service>(twitch)](QString url,std::function<void(oto::twitch::HttpReply)> done){if(const auto service=weak.lock())service->fetchChatResource(std::move(url),std::move(done));else done({});});
		chatMedia->enablePersonalFeed();
		chat=std::make_shared<oto::sources::ChatHub>(core,visual,renderer,chatMedia,presets,assets);
		chatMedia->setChanged([weak=std::weak_ptr<oto::sources::ChatHub>(chat)]{if(const auto hub=weak.lock())hub->refresh();});
		core->presentations().setChanged([alert=std::weak_ptr<oto::sources::AlertHub>(alerts),shoutout=std::weak_ptr<oto::sources::ShoutoutHub>(shoutouts)]{
			// Completion can occur inside source preparation under its mutex. Wake
			// both hubs after that call returns, never refresh reentrantly.
			if(const auto hub=alert.lock())QMetaObject::invokeMethod(hub.get(),[alert,shoutout]{if(const auto a=alert.lock())a->refresh();if(const auto s=shoutout.lock())s->refresh();},Qt::QueuedConnection);
		});
		twitch->setProfilesChanged([weak=std::weak_ptr<oto::sources::AlertHub>(alerts),shoutout=std::weak_ptr<oto::sources::ShoutoutHub>(shoutouts)]{if(const auto hub=weak.lock())hub->refresh();if(const auto hub=shoutout.lock())hub->refresh();});
		core->setStateChanged([weak = std::weak_ptr<oto::sources::SupporterHub>(supporter), list = std::weak_ptr<oto::sources::EventListHub>(eventList), alert=std::weak_ptr<oto::sources::AlertHub>(alerts),shoutout=std::weak_ptr<oto::sources::ShoutoutHub>(shoutouts),meter=std::weak_ptr<oto::sources::HypeHub>(hype),liveChat=std::weak_ptr<oto::sources::ChatHub>(chat)] {
			if (const auto owner = weak.lock()) owner->refresh();
			if (const auto owner = list.lock()) owner->refresh();
			if (const auto owner = alert.lock()) owner->refresh();
			if (const auto owner = shoutout.lock()) owner->refresh();
			if (const auto owner = meter.lock()) owner->refresh();
			if (const auto owner = liveChat.lock()) owner->refresh();
		});
		if (!oto::ui::createMainPanel(core, visual, renderer, twitch))
			throw std::runtime_error("Panel initialization failed");
		obs_frontend_add_event_callback(onFrontendEvent, nullptr);
		callbackRegistered = true;
		oto::sources::registerSupporterSource(supporter);
		oto::sources::registerEventListSource(eventList);
		oto::sources::registerAlertSource(alerts);
		oto::sources::registerShoutoutSource(shoutouts);
		oto::sources::registerHypeSource(hype);
		oto::sources::registerChatSource(chat);
		core->log(oto::LogLevel::Info, "Module loaded (version " OTO_VERSION ")");
		return true;
	} catch (...) {
		if (callbackRegistered) {
			obs_frontend_remove_event_callback(onFrontendEvent, nullptr);
			callbackRegistered = false;
		}
		oto::ui::destroyMainPanel();
		if(core){core->setStateChanged({});core->presentations().setChanged({});} if(twitch){twitch->setChanged({});twitch->setProfilesChanged({});nexus_twitch_runtime_release();}
		twitch.reset();
		releaseVisuals();
		supporter.reset();
		eventList.reset();
		alerts.reset();
		shoutouts.reset();
		hype.reset();
		chat.reset();
		if (core && !nexus_twitch_runtime_active())
			core->stop();
		core.reset();
		oto::ObsLogger().write(oto::LogLevel::Error, "Module initialization failed; resources released");
		return false;
	}
}

void obs_module_unload(void)
{
	if(core){core->setStateChanged({});core->presentations().setChanged({});} if(twitch){twitch->setChanged({});twitch->setProfilesChanged({});nexus_twitch_runtime_release();}
	twitch.reset();
	// Normal shutdown already cleaned up during EXIT; this also handles unload
	// while the frontend is still alive. Both paths are idempotent.
	if (callbackRegistered) {
		obs_frontend_remove_event_callback(onFrontendEvent, nullptr);
		callbackRegistered = false;
	}
	oto::ui::destroyMainPanel();
	releaseVisuals();
	supporter.reset();
	eventList.reset();
	alerts.reset();
	shoutouts.reset();
	hype.reset();
	chat.reset();
	if (core && !nexus_twitch_runtime_active())
		core->stop();
	core.reset();
	oto::ObsLogger().write(oto::LogLevel::Info, "Module unloaded");
}





