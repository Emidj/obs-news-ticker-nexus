// SPDX-License-Identifier: GPL-2.0-or-later
#include "supporter-source.hpp"
#include "supporter-typography.hpp"
#include "supporter-language.hpp"
#include "adapters/supporter-draw.hpp"
#include <obs-module.h>
#include <QDateTime>
#include <QTimeZone>
#include <QThread>
#include <algorithm>
#include <cstring>

namespace oto::sources {
namespace {
constexpr const char *fieldKeys[] = {"last_follow","last_sub","last_cheer","last_gift","daily_bits","daily_subs"};
constexpr const char *labelKeys[] = {"Bar.LastFollow","Bar.LastSub","Bar.LastCheer","Bar.LastGift","Bar.DailyBits","Bar.DailySubs"};
struct SourceHandle { std::shared_ptr<BarInstance> instance; std::weak_ptr<SupporterHub> hub; };
void migrateFontSettings(obs_data_t *settings)
{
	const auto legacy=QString::fromUtf8(obs_data_get_string(settings,"value_font")).left(128);
	if(!obs_data_has_user_value(settings,"name_font") && !legacy.isEmpty()) {
		auto *font=obs_data_create();
		obs_data_set_string(font,"face",legacy.toUtf8().constData());
		obs_data_set_int(font,"size",12);
		obs_data_set_obj(settings,"name_font",font);obs_data_release(font);
	}
	if(!obs_data_has_user_value(settings,"use_style_font"))
		obs_data_set_bool(settings,"use_style_font",legacy.isEmpty());
}
bool previewButton(obs_properties_t *properties, obs_property_t *property, void *data) noexcept
{
	if (!data) return false;
	auto &handle = *static_cast<SourceHandle *>(data);
	const auto hub = handle.hub.lock();
	const auto *name = obs_property_name(property);
	const bool end = std::strcmp(name,"preview_end") == 0;
	const bool ok = hub && hub->preview(handle.instance, std::strcmp(name,"preview_long") == 0, end);
	obs_property_set_description(obs_properties_get(properties,"preview_status"),
		obs_module_text(ok ? (end ? "Bar.PreviewStopped" : "Bar.PreviewActive") : "Bar.PreviewFailed"));
	return true;
}
void update(void *data, obs_data_t *settings) noexcept
{
	try {
		auto &handle = *static_cast<SourceHandle *>(data);
		BarSettings next;
		readSupporterLanguage(next,settings);
		next.width = int(std::clamp(obs_data_get_int(settings,"width"), 600LL, 1920LL));
		next.height = int(std::clamp(obs_data_get_int(settings,"height"), 80LL, 240LL));
		next.motion = static_cast<visual::Motion>(std::clamp(obs_data_get_int(settings,"motion"),0LL,3LL));
		next.durationMs = int(std::clamp(obs_data_get_int(settings,"motion_ms"),100LL,5000LL));
		for (size_t i = 0; i < 6; ++i) next.fields[i] = obs_data_get_bool(settings,fieldKeys[i]);
		next.presetId=QString::fromUtf8(obs_data_get_string(settings,"preset")).left(48);
		if(next.presetId.isEmpty())next.presetId=QStringLiteral("minimal");
		migrateFontSettings(settings);
		readSupporterTypography(next,settings);
		if(const auto hub=handle.hub.lock())next.style=hub->presets().resolve(next.presetId).style;
		{
			std::lock_guard<std::mutex> lock(handle.instance->settingsMutex);
			handle.instance->settings = next;
			handle.instance->width = next.width; handle.instance->height = next.height;
			// Keep the old immutable plan until GUI preparation replaces it atomically.
		}
		if (const auto hub = handle.hub.lock()) hub->request(handle.instance);
	} catch (...) { blog(LOG_ERROR,"[obs-twitch-overlays] Supporter source update failed"); }
}
void setVisible(void *data, bool visible) noexcept
{
	try {
		auto &handle = *static_cast<SourceHandle *>(data);
		{
			std::lock_guard<std::mutex> lock(handle.instance->settingsMutex);
			handle.instance->visible = visible;
			if (!visible) std::atomic_store(&handle.instance->frame, BarFramePtr{});
		}
		if (const auto hub = handle.hub.lock()) hub->request(handle.instance);
	} catch (...) { blog(LOG_ERROR,"[obs-twitch-overlays] Supporter visibility update failed"); }
}
}
SupporterHub::SupporterHub(std::shared_ptr<PluginCore> core, std::shared_ptr<visual::VisualEngine> visual,
	std::shared_ptr<ObsVisualRenderer> renderer, std::shared_ptr<const presets::Registry> presets,
	std::shared_ptr<visual::AssetManager> assets) : core_(std::move(core)), visual_(std::move(visual)), renderer_(std::move(renderer)),
	presets_(std::move(presets)),assets_(std::move(assets))
{
	midnight_.setSingleShot(true); midnight_.setTimerType(Qt::PreciseTimer);
	QObject::connect(&midnight_, &QTimer::timeout, this, [this] {
		try { refresh(); } catch (...) { core_->log(LogLevel::Error,"Daily source refresh failed"); }
	});
}
void SupporterHub::request(const std::shared_ptr<BarInstance> &instance)
{
	if (!running_ || instance->queued.exchange(true)) return;
	const auto weak = weak_from_this(); const auto weakInstance = std::weak_ptr<BarInstance>(instance);
	const auto accepted = QMetaObject::invokeMethod(this, [weak, weakInstance] {
		const auto hub = weak.lock(); const auto item = weakInstance.lock();
		if (!hub) return;
		if (!item) { if (hub->running_) hub->scheduleDay(); return; }
		item->queued = false;
		if (!hub->running_) return;
		try {
			if (!item->registered) { hub->instances_.push_back(item); item->registered = true; }
			hub->prepare(item, hub->core_->sharedSnapshot(), state::localDay(std::chrono::system_clock::now()));
			hub->scheduleDay();
		} catch (...) { hub->core_->log(LogLevel::Error,"Supporter source preparation failed"); }
	}, Qt::QueuedConnection);
	if (!accepted) instance->queued = false;
}
void SupporterHub::prepare(const std::shared_ptr<BarInstance> &instance, const state::SnapshotPtr &snapshot, int today)
{
	if (!instance->visible || !running_) return;
	// update/hide callbacks can run on OBS threads. Hold settings through publication
	// so an older preparation cannot overwrite a newer settings invalidation.
	std::lock_guard<std::mutex> lock(instance->settingsMutex);
	auto labels = supporterLabels(instance->settings.language);
	if (!instance->previewChannel.empty() || core_->panelTestChannel()) for (auto &title : labels.titles) title.prepend(QStringLiteral("TEST · "));
	const auto values = supporterValues(snapshot, today, labels, instance->previewChannel, core_->displayChannel());
	const auto previous = std::atomic_load(&instance->frame);
	const auto before = visual_->stats();
	visual::ImagePtr background,icons;
	if(std::any_of(instance->settings.fields.begin(),instance->settings.fields.end(),[](bool field){return field;})) {
		const auto &style=instance->settings.style;
		if(!style.backgroundImage.isEmpty())background=previous && previous->background && previous->settings.style.backgroundImage==style.backgroundImage ? previous->background : assets_->load(style.backgroundImage);
		if(!style.iconImage.isEmpty())icons=previous && previous->icons && previous->settings.style.iconImage==style.iconImage ? previous->icons : assets_->load(style.iconImage);
	}
	const auto context=supporterContext(snapshot,instance->previewChannel,core_->displayChannel());
	const auto frame = prepareSupporterFrame(*visual_,instance->settings,labels,values,previous,visual::MotionClock::now(),std::move(background),std::move(icons),context);
	if (frame == previous) return;
	if (instance->visible) std::atomic_store(&instance->frame, frame);
	instance->failed = false;
	size_t changed = 0;
	for (size_t i=0;i<6;++i) if (frame->layers[i].frame && (!previous || frame->layers[i].frame != previous->layers[i].frame)) ++changed;
	const auto animated=std::count_if(frame->layers.begin(),frame->layers.end(),[](const auto &layer){return layer.frame && layer.started!=visual::MotionClock::time_point{};});
	core_->log(LogLevel::Info, QStringLiteral("Supporter update: %1 value textures changed; %2 rasterizations; %3 cache hits; %4 animated fields")
		.arg(qulonglong(changed)).arg(qulonglong(visual_->stats().renders-before.renders)).arg(qulonglong(visual_->stats().hits-before.hits)).arg(qulonglong(animated)).toStdString());
}
void SupporterHub::scheduleDay()
{
	instances_.erase(std::remove_if(instances_.begin(),instances_.end(),[](const auto &item) { return item.expired(); }),instances_.end());
	const bool visible = std::any_of(instances_.begin(),instances_.end(),[](const auto &weak) { const auto item = weak.lock(); return item && item->visible; });
	midnight_.stop();
	if (!visible || !running_) return;
	const auto now = QDateTime::currentDateTime();
	const auto next = now.date().addDays(1).startOfDay(QTimeZone::systemTimeZone());
	midnight_.start(int(std::clamp<qint64>(now.msecsTo(next) + 50, 100, 26 * 60 * 60 * 1000)));
}
void SupporterHub::refresh()
{
	if (!running_ || previewTransaction_) return;
	const auto snapshot = core_->sharedSnapshot(); const auto today = state::localDay(std::chrono::system_clock::now());
	for (const auto &weak : instances_) if (const auto item = weak.lock()) prepare(item,snapshot,today);
	scheduleDay();
}
bool SupporterHub::preview(const std::shared_ptr<BarInstance> &instance, bool longText, bool end) noexcept
{
	if (!running_ || QThread::currentThread() != thread()) return false;
	struct Transaction { bool &flag; Transaction(bool &value):flag(value){flag=true;} ~Transaction(){flag=false;} } transaction(previewTransaction_);
	try {
		core_->releaseSourcePreview(instance->previewChannel);
		instance->previewChannel.clear();
		if (!end) instance->previewChannel = core_->createSourcePreview(longText);
		prepare(instance,core_->sharedSnapshot(),state::localDay(std::chrono::system_clock::now()));
		scheduleDay(); return true;
	} catch (...) {
		core_->log(LogLevel::Error,"Source preview failed; daily counters preserved");
		try { prepare(instance,core_->sharedSnapshot(),state::localDay(std::chrono::system_clock::now())); } catch (...) {}
		return false;
	}
}
void SupporterHub::remove(const std::shared_ptr<BarInstance> &instance)
{
	const auto weak = weak_from_this();
	QMetaObject::invokeMethod(this, [weak, instance] {
		if (const auto hub = weak.lock(); hub && hub->running_) {
			try { hub->core_->releaseSourcePreview(instance->previewChannel); instance->previewChannel.clear(); }
			catch (...) { hub->core_->log(LogLevel::Error,"Preview cleanup failed"); }
			hub->scheduleDay();
		}
	}, Qt::QueuedConnection);
}
void SupporterHub::draw(const std::shared_ptr<BarInstance> &instance) noexcept
{
	if (!running_ || !instance->visible || instance->failed) return;
	const auto frame = std::atomic_load(&instance->frame);
	if (!frame) return;
	try {
		if (instance->drawn.lock() != frame) { instance->drawn = frame; instance->settled = false; }
		const auto now = instance->settled && !frame->hasScrolling ? visual::MotionClock::time_point{} : visual::MotionClock::now();
		instance->settled = drawSupporterFrame(*renderer_,*frame,now,instance->settled);
	}
	catch (...) { if (!instance->failed.exchange(true)) core_->log(LogLevel::Error,"Supporter drawing failed; suspended until next update"); }
}
void SupporterHub::stop() noexcept
{
	// Serializes shutdown with in-flight video_render, without waiting on the UI
	// from an OBS callback. Queued UI callbacks become no-ops and are destroyed on unload.
	obs_enter_graphics();
	if (!running_.exchange(false)) { obs_leave_graphics(); return; }
	for (const auto &weak : instances_) if (const auto item = weak.lock()) std::atomic_store(&item->frame,BarFramePtr{});
	renderer_.reset(); visual_.reset();
	obs_leave_graphics(); midnight_.stop(); instances_.clear();
	core_->log(LogLevel::Info,"Supporter sources stopped");
}
void registerSupporterSource(const std::shared_ptr<SupporterHub> &hub)
{
	obs_source_info info{};
	info.id = "oto_supporter_bar"; info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW;
	info.get_name = [](void *) { return obs_module_text("Bar.Name"); };
	info.type_data = new std::weak_ptr<SupporterHub>(hub);
	info.free_type_data = [](void *data) { delete static_cast<std::weak_ptr<SupporterHub> *>(data); };
	info.create = [](obs_data_t *settings, obs_source_t *source) -> void * {
		try {
			auto weak = *static_cast<std::weak_ptr<SupporterHub> *>(obs_source_get_type_data(source));
			if (weak.expired()) return nullptr;
			auto handle = std::make_unique<SourceHandle>(SourceHandle{std::make_shared<BarInstance>(),weak});
			update(handle.get(),settings); return handle.release();
		} catch (...) { blog(LOG_ERROR,"[obs-twitch-overlays] Supporter creation failed"); return nullptr; }
	};
	info.destroy = [](void *data) {
		setVisible(data,false); auto *handle = static_cast<SourceHandle *>(data);
		try { if (const auto hub = handle->hub.lock()) hub->remove(handle->instance); } catch (...) {}
		delete handle;
	};
	info.update = update;
	info.show = [](void *data) { setVisible(data,true); }; info.hide = [](void *data) { setVisible(data,false); };
	info.get_width = [](void *data) -> uint32_t { return static_cast<SourceHandle *>(data)->instance->width.load(); };
	info.get_height = [](void *data) -> uint32_t { return static_cast<SourceHandle *>(data)->instance->height.load(); };
	info.video_render = [](void *data, gs_effect_t *) { const auto &handle = *static_cast<SourceHandle *>(data); if (const auto owner = handle.hub.lock()) owner->draw(handle.instance); };
	info.get_defaults = [](obs_data_t *settings) {
		obs_data_set_default_int(settings,"width",1080); obs_data_set_default_int(settings,"height",96);
		obs_data_set_default_int(settings,"motion",int(visual::Motion::StyleChange)); obs_data_set_default_int(settings,"motion_ms",2000);
		obs_data_set_default_string(settings,"preset","minimal");
		obs_data_set_default_string(settings,"language","auto");
		obs_data_set_default_string(settings,"value_font","");
		fonts::defaults(settings,"Arial");
		for (auto key : fieldKeys) obs_data_set_default_bool(settings,key,true);
	};
	info.get_properties = [](void *data) {
		auto *properties = obs_properties_create();
		auto *language=obs_properties_add_list(properties,"language",obs_module_text("Language.Source"),OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_STRING);
		obs_property_list_add_string(language,obs_module_text("Language.Automatic"),"auto");
		for(const auto &item:nexus::localization::languages)obs_property_list_add_string(language,item.name,item.code);
		obs_property_set_long_description(language,obs_module_text("Language.SourceHelp"));
		obs_properties_add_text(properties,"notice",obs_module_text("Bar.SimulationNotice"),OBS_TEXT_INFO);
		obs_properties_add_text(properties,"preview_status",obs_module_text("Bar.PreviewNotice"),OBS_TEXT_INFO);
		obs_properties_add_button(properties,"preview_test",obs_module_text("Bar.PreviewTest"),previewButton);
		obs_properties_add_button(properties,"preview_long",obs_module_text("Bar.PreviewLong"),previewButton);
		obs_properties_add_button(properties,"preview_end",obs_module_text("Bar.PreviewEnd"),previewButton);
		auto *preset=obs_properties_add_list(properties,"preset",obs_module_text("Bar.Preset"),OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_STRING);
		const auto hub=data?static_cast<SourceHandle *>(data)->hub.lock():std::shared_ptr<SupporterHub>{};
		if(hub) {
			for(const auto &item:hub->presets().entries())obs_property_list_add_string(preset,item.name.toUtf8().constData(),item.id.toUtf8().constData());
			const auto instance=static_cast<SourceHandle *>(data)->instance;std::lock_guard<std::mutex> lock(instance->settingsMutex);
			if(!hub->presets().find(instance->settings.presetId)) {
				const auto name=QString::fromUtf8(obs_module_text("Bar.PresetUnavailable")).arg(instance->settings.presetId);
				obs_property_list_add_string(preset,name.toUtf8().constData(),instance->settings.presetId.toUtf8().constData());
			}
		} else obs_property_list_add_string(preset,"Minimal","minimal");
		fonts::addProperties(properties,obs_module_text("Bar.ValueFont"),obs_module_text("Font.Labels"),
			obs_module_text("Bar.FontInherit"),obs_module_text("Font.Help"));
		obs_property_set_long_description(obs_properties_get(properties,fonts::names.font),obs_module_text("Bar.FontHelp"));
		obs_property_set_long_description(obs_properties_get(properties,fonts::labels.font),obs_module_text("Bar.LabelFontHelp"));
		auto *motion = obs_properties_add_list(properties,"motion",obs_module_text("Bar.Motion"),OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_INT);
		obs_property_list_add_int(motion,obs_module_text("Bar.MotionNone"),0);
		obs_property_list_add_int(motion,obs_module_text("Bar.MotionFade"),1);
		obs_property_list_add_int(motion,obs_module_text("Bar.MotionSlide"),2);
		obs_property_list_add_int(motion,obs_module_text("Bar.MotionStyle"),3);
		obs_properties_add_int(properties,"motion_ms",obs_module_text("Bar.MotionDuration"),100,5000,50);
		obs_properties_add_int(properties,"width",obs_module_text("Bar.Width"),600,1920,10);
		obs_properties_add_int(properties,"height",obs_module_text("Bar.Height"),80,240,1);
		for (size_t i = 0; i < 6; ++i) obs_properties_add_bool(properties,fieldKeys[i],obs_module_text(labelKeys[i]));
		return properties;
	};
	obs_register_source(&info);
}
}
