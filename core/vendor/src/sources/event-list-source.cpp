// SPDX-License-Identifier: GPL-2.0-or-later
#include "event-list-source.hpp"
#include "event-list-properties.hpp"
#include "adapters/event-list-draw.hpp"
#include <obs-module.h>
#include <QThread>
#include <algorithm>
#include <cstring>
#include <stdexcept>
namespace oto::sources {
namespace {
constexpr const char *keys[]={"follow","sub","cheer","gift","raid"};
constexpr const char *labels[]={"List.Follow","List.Sub","List.Cheer","List.Gift","List.Raid"};
struct Handle {std::shared_ptr<ListInstance> instance;std::weak_ptr<EventListHub> hub;};
bool animationModified(obs_properties_t *props,obs_property_t *,obs_data_t *settings) noexcept
{
	const bool enabled=obs_data_get_bool(settings,"animate");
	obs_property_set_enabled(obs_properties_get(props,"entry_side"),enabled);
	obs_property_set_enabled(obs_properties_get(props,"motion_ms"),enabled);return true;
}
void update(void *data,obs_data_t *settings) noexcept
{
	try {
		auto &handle=*static_cast<Handle *>(data);ListSettings next;
		next.presetId=QString::fromUtf8(obs_data_get_string(settings,"preset")).left(48);if(next.presetId.isEmpty())next.presetId=QStringLiteral("minimal");
		if(const auto hub=handle.hub.lock()){next.style=hub->style(next.presetId);next.family=next.style.family;}
		next.width=int(std::clamp(obs_data_get_int(settings,"width"),420LL,1200LL));next.rows=int(std::clamp(obs_data_get_int(settings,"rows"),1LL,10LL));next.rowHeight=int(std::clamp(obs_data_get_int(settings,"row_height"),48LL,96LL));
		next.animate=obs_data_get_bool(settings,"animate");
		next.durationMs=int(std::clamp(obs_data_get_int(settings,"motion_ms"),1000LL,12000LL));
		next.backgroundOpacity=int(std::clamp(obs_data_get_int(settings,"background_opacity"),0LL,100LL));
		const auto side=obs_data_get_int(settings,"entry_side");next.entrance=side>=0 && side<=3?static_cast<ListEntrance>(side):ListEntrance::Appearance;
		for(std::size_t i=0;i<5;++i)next.types[i]=obs_data_get_bool(settings,keys[i]);
		readListTypography(next,settings);
		{std::lock_guard<std::mutex> lock(handle.instance->mutex);handle.instance->settings=next;handle.instance->width=next.width;handle.instance->height=next.height();}
		if(const auto hub=handle.hub.lock())hub->request(handle.instance);
	}catch(...){blog(LOG_ERROR,"[obs-twitch-overlays] Event List update failed");}
}
void visible(void *data,bool show) noexcept
{
	try {
		auto &handle=*static_cast<Handle *>(data);
		{std::lock_guard<std::mutex> lock(handle.instance->mutex);handle.instance->visible=show;if(!show)std::atomic_store(&handle.instance->frame,ListFramePtr{});}
		if(const auto hub=handle.hub.lock())hub->request(handle.instance);
	}catch(...){blog(LOG_ERROR,"[obs-twitch-overlays] Event List visibility failed");}
}
bool previewButton(obs_properties_t *props,obs_property_t *property,void *data) noexcept
{
	if(!data)return false;auto &handle=*static_cast<Handle *>(data);const auto hub=handle.hub.lock();const auto *key=obs_property_name(property);
	const bool end=std::strcmp(key,"preview_end")==0,ok=hub && hub->preview(handle.instance,std::strcmp(key,"preview_long")==0,end);
	obs_property_set_description(obs_properties_get(props,"preview_status"),obs_module_text(ok?(end?"Bar.PreviewStopped":"List.PreviewActive"):"Bar.PreviewFailed"));return true;
}
}
EventListHub::EventListHub(std::shared_ptr<PluginCore> core,std::shared_ptr<visual::VisualEngine> visual,std::shared_ptr<ObsVisualRenderer> renderer,std::shared_ptr<const presets::Registry> presets,std::shared_ptr<visual::AssetManager> assets):core_(std::move(core)),visual_(std::move(visual)),renderer_(std::move(renderer)),presets_(std::move(presets)),assets_(std::move(assets))
{
	if(!presets_->resolve(QStringLiteral("minimal")).eventListStyle)throw std::runtime_error("Minimal Event List preset missing");
	labels_={QString::fromUtf8(obs_module_text("Bar.Anonymous")),QString::fromUtf8(obs_module_text("List.BitsDetail")),QString::fromUtf8(obs_module_text("List.GiftsDetail")),QString::fromUtf8(obs_module_text("List.RaidDetail")),QString::fromUtf8(obs_module_text("List.SubDetail")),QString::fromUtf8(obs_module_text("List.MonthsDetail")),QString::fromUtf8(obs_module_text("List.Empty"))};
	const char *badgeKeys[]={"List.BadgeFollow","List.BadgeSub","List.BadgeBits","List.BadgeGift","List.BadgeRaid"};
	for(size_t i=0;i<5;++i)labels_.badges[i]=QString::fromUtf8(obs_module_text(badgeKeys[i]));
}
void EventListHub::request(const std::shared_ptr<ListInstance> &instance)
{
	if(!running_ || instance->queued.exchange(true))return;const auto weak=weak_from_this();const auto item=std::weak_ptr<ListInstance>(instance);
	if(!QMetaObject::invokeMethod(this,[weak,item]{
		const auto hub=weak.lock();const auto source=item.lock();if(!hub)return;if(source)source->queued=false;if(!hub->running_)return;
		if(source && !source->registered){hub->instances_.push_back(source);source->registered=true;}hub->refresh();
	},Qt::QueuedConnection))instance->queued=false;
}
void EventListHub::prepare(const std::shared_ptr<ListInstance> &instance,const state::SnapshotPtr &snapshot,bool demoEntrance)
{
	if(!running_ || !instance->visible)return;std::lock_guard<std::mutex> lock(instance->mutex);
	const auto channel=instance->previewChannel.empty()?core_->displayChannel():state::ChannelKey{events::EventOrigin::Preview,instance->previewChannel};
	const auto view=eventListView(snapshot,instance->settings,labels_,channel,demoEntrance);const auto before=std::atomic_load(&instance->frame);
	visual::ImagePtr background;const auto &reference=instance->settings.style.backgroundImage;
	if(!view.items.empty() && !reference.isEmpty())background=before && before->background && before->settings.style.backgroundImage==reference?before->background:assets_->load(reference);
	const auto frame=prepareEventListFrame(*visual_,instance->settings,labels_,view,before,visual::MotionClock::now(),demoEntrance,std::move(background));
	if(frame!=before && instance->visible)std::atomic_store(&instance->frame,frame);instance->failed=false;
}
void EventListHub::refresh()
{
	if(!running_ || previewTransaction_)return;
	instances_.erase(std::remove_if(instances_.begin(),instances_.end(),[](const auto &weak){return weak.expired();}),instances_.end());
	const auto snapshot=core_->sharedSnapshot();
	for(const auto &weak:instances_)if(const auto item=weak.lock()) {
		try {prepare(item,snapshot);}
		catch(...){item->failed=true;core_->log(LogLevel::Error,"Event List preparation failed; source suspended until next update");}
	}
}
bool EventListHub::preview(const std::shared_ptr<ListInstance> &instance,bool longText,bool end) noexcept
{
	if(!running_ || QThread::currentThread()!=thread())return false;
	try {
		previewTransaction_=true;core_->releaseSourcePreview(instance->previewChannel);instance->previewChannel.clear();
		if(!end)instance->previewChannel=core_->createSourcePreview(longText,true);
		prepare(instance,core_->sharedSnapshot(),!end);
		previewTransaction_=false;refresh();return true;
	}catch(...){previewTransaction_=false;refresh();core_->log(LogLevel::Error,"Event List preview failed; saved counters preserved");return false;}
}
void EventListHub::remove(const std::shared_ptr<ListInstance> &instance)
{
	const auto weak=weak_from_this();QMetaObject::invokeMethod(this,[weak,instance]{if(const auto hub=weak.lock();hub && hub->running_){try{hub->core_->releaseSourcePreview(instance->previewChannel);instance->previewChannel.clear();hub->refresh();}catch(...){hub->core_->log(LogLevel::Error,"Event List preview cleanup failed");}}},Qt::QueuedConnection);
}
void EventListHub::draw(const std::shared_ptr<ListInstance> &instance) noexcept
{
	if(!running_ || !instance->visible || instance->failed)return;const auto frame=std::atomic_load(&instance->frame);if(!frame)return;
	try {
		if(instance->drawn.lock()!=frame){instance->drawn=frame;instance->settled=false;}
		const auto now=instance->settled && !frame->hasScrolling?visual::MotionClock::time_point{}:visual::MotionClock::now();
		instance->settled=drawEventListFrame(*renderer_,*frame,now,instance->settled);
	}catch(...){if(!instance->failed.exchange(true))core_->log(LogLevel::Error,"Event List drawing failed; source suspended until next update");}
}
void EventListHub::stop() noexcept
{
	obs_enter_graphics();if(!running_.exchange(false)){obs_leave_graphics();return;}
	for(const auto &weak:instances_)if(const auto item=weak.lock())std::atomic_store(&item->frame,ListFramePtr{});
	renderer_.reset();visual_.reset();obs_leave_graphics();instances_.clear();core_->log(LogLevel::Info,"Event List sources stopped");
}
void registerEventListSource(const std::shared_ptr<EventListHub> &hub)
{
	obs_source_info info{};info.id="oto_event_list";info.type=OBS_SOURCE_TYPE_INPUT;info.output_flags=OBS_SOURCE_VIDEO|OBS_SOURCE_CUSTOM_DRAW;
	info.get_name=[](void *){return obs_module_text("List.Name");};info.type_data=new std::weak_ptr<EventListHub>(hub);info.free_type_data=[](void *data){delete static_cast<std::weak_ptr<EventListHub> *>(data);};
	info.create=[](obs_data_t *settings,obs_source_t *source)->void * {
		try {const auto weak=*static_cast<std::weak_ptr<EventListHub> *>(obs_source_get_type_data(source));if(weak.expired())return nullptr;auto handle=std::make_unique<Handle>(Handle{std::make_shared<ListInstance>(),weak});update(handle.get(),settings);return handle.release();}
		catch(...){blog(LOG_ERROR,"[obs-twitch-overlays] Event List creation failed");return nullptr;}
	};
	info.destroy=[](void *data){visible(data,false);auto *handle=static_cast<Handle *>(data);try{if(const auto hub=handle->hub.lock())hub->remove(handle->instance);}catch(...){}delete handle;};
	info.update=update;info.show=[](void *data){visible(data,true);};info.hide=[](void *data){visible(data,false);};
	info.get_width=[](void *data)->uint32_t{return static_cast<Handle *>(data)->instance->width.load();};info.get_height=[](void *data)->uint32_t{return static_cast<Handle *>(data)->instance->height.load();};
	info.video_render=[](void *data,gs_effect_t *){const auto &handle=*static_cast<Handle *>(data);if(const auto owner=handle.hub.lock())owner->draw(handle.instance);};
	info.get_defaults=[](obs_data_t *settings){
		obs_data_set_default_int(settings,"width",600);obs_data_set_default_int(settings,"rows",8);obs_data_set_default_int(settings,"row_height",64);obs_data_set_default_bool(settings,"animate",true);
		obs_data_set_default_int(settings,"entry_side",0);
		obs_data_set_default_int(settings,"motion_ms",6000);
		obs_data_set_default_int(settings,"background_opacity",100);
		obs_data_set_default_string(settings,"preset","minimal");
		obs_data_set_default_int(settings,"name_color",0xffffff);obs_data_set_default_int(settings,"label_color",0xffffff);
		obs_data_set_default_bool(settings,"use_style_name_color",true);obs_data_set_default_bool(settings,"use_style_label_color",true);
		for(const auto *key:keys)obs_data_set_default_bool(settings,key,true);
		fonts::defaults(settings,"Segoe UI");
	};
	info.get_properties=[](void *data){
		auto *p=obs_properties_create();obs_properties_add_text(p,"notice",obs_module_text("List.Notice"),OBS_TEXT_INFO);obs_properties_add_text(p,"preview_status",obs_module_text("Bar.PreviewNotice"),OBS_TEXT_INFO);
		auto *preset=obs_properties_add_list(p,"preset",obs_module_text("Bar.Style"),OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_STRING);
		const auto hub=data?static_cast<Handle *>(data)->hub.lock():nullptr;
		if(hub) {for(const auto &definition:hub->presets().entries())if(definition.eventListStyle)obs_property_list_add_string(preset,definition.name.toUtf8().constData(),definition.id.toUtf8().constData());}
		else obs_property_list_add_string(preset,"Minimal","minimal");
		auto *opacity=obs_properties_add_int_slider(p,"background_opacity",obs_module_text("List.BackgroundOpacity"),0,100,1);
		obs_property_set_long_description(opacity,obs_module_text("List.BackgroundOpacityHelp"));
		obs_properties_add_button(p,"preview_test",obs_module_text("Bar.PreviewTest"),previewButton);obs_properties_add_button(p,"preview_long",obs_module_text("Bar.PreviewLong"),previewButton);obs_properties_add_button(p,"preview_end",obs_module_text("Bar.PreviewEnd"),previewButton);
		fonts::addRoleProperties(p,fonts::names,obs_module_text("Bar.ValueFont"),obs_module_text("Bar.FontInherit"),obs_module_text("List.FontHelp"));
		obs_properties_add_color(p,"name_color",obs_module_text("List.NameColor"));
		auto *nameInherit=obs_properties_add_bool(p,"use_style_name_color",obs_module_text("Shoutout.StyleColor"));obs_property_set_modified_callback(nameInherit,listColorInheritanceModified);
		fonts::addRoleProperties(p,fonts::labels,obs_module_text("List.LabelFont"),obs_module_text("Bar.FontInherit"),obs_module_text("List.FontHelp"));
		obs_properties_add_color(p,"label_color",obs_module_text("List.LabelColor"));
		auto *labelInherit=obs_properties_add_bool(p,"use_style_label_color",obs_module_text("Shoutout.StyleColor"));obs_property_set_modified_callback(labelInherit,listColorInheritanceModified);
		obs_properties_add_int(p,"width",obs_module_text("Bar.Width"),420,1200,10);obs_properties_add_int(p,"rows",obs_module_text("List.Rows"),1,10,1);obs_properties_add_int(p,"row_height",obs_module_text("List.RowHeight"),48,96,1);
		auto *animate=obs_properties_add_bool(p,"animate",obs_module_text("List.Animate"));obs_property_set_modified_callback(animate,animationModified);
		auto *side=obs_properties_add_list(p,"entry_side",obs_module_text("List.Entrance"),OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_INT);
		obs_property_list_add_int(side,obs_module_text("List.EntranceAppearance"),0);obs_property_list_add_int(side,obs_module_text("List.EntranceLeft"),1);obs_property_list_add_int(side,obs_module_text("List.EntranceRight"),2);obs_property_list_add_int(side,obs_module_text("List.EntranceTop"),3);
		obs_property_set_long_description(side,obs_module_text("List.EntranceHelp"));
		auto *duration=obs_properties_add_int(p,"motion_ms",obs_module_text("Bar.MotionDuration"),1000,12000,100);
		obs_property_set_long_description(duration,obs_module_text("List.DurationHelp"));
		for(std::size_t i=0;i<5;++i)obs_properties_add_bool(p,keys[i],obs_module_text(labels[i]));return p;
	};
	obs_register_source(&info);blog(LOG_INFO,"[obs-twitch-overlays] Event List source registered (Minimal)");
}
}
