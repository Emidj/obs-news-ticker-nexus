// SPDX-License-Identifier: GPL-2.0-or-later
#include "shoutout-source.hpp"
#include "font-properties.hpp"
#include "adapters/shoutout-draw.hpp"
#include <obs-module.h>
#include <QThread>
#include <algorithm>
#include <cstring>
namespace oto::sources {
namespace {
struct Handle {std::shared_ptr<ShoutoutInstance> instance;std::weak_ptr<ShoutoutHub> hub;};
std::int64_t clockValue(visual::MotionClock::time_point time){return std::chrono::duration_cast<std::chrono::nanoseconds>(time.time_since_epoch()).count();}
void notice(const std::shared_ptr<ShoutoutInstance> &instance,QString text,bool repaint=true){
	{std::lock_guard<std::mutex> lock(instance->mutex);instance->notice=std::move(text);}
	if(repaint && instance->source)if(auto *source=obs_weak_source_get_source(instance->source.get())){obs_source_update_properties(source);obs_source_release(source);}
}
void update(void *data,obs_data_t *settings)noexcept{
	try{auto &handle=*static_cast<Handle *>(data);ShoutoutSettings next;
		next.times={int(std::clamp(obs_data_get_int(settings,"entry_ms"),100LL,10000LL)),int(std::clamp(obs_data_get_int(settings,"hold_ms"),0LL,60000LL)),int(std::clamp(obs_data_get_int(settings,"exit_ms"),100LL,10000LL))};
		next.nameFamily=fonts::overrideFamily(settings,fonts::names);next.labelFamily=fonts::overrideFamily(settings,fonts::labels);
		next.preset=QString::fromUtf8(obs_data_get_string(settings,"preset"));next.nameColor=QColor::fromRgba(qRgba(int(obs_data_get_int(settings,"name_color")&255),int((obs_data_get_int(settings,"name_color")>>8)&255),int((obs_data_get_int(settings,"name_color")>>16)&255),255));next.detailsColor=QColor::fromRgba(qRgba(int(obs_data_get_int(settings,"details_color")&255),int((obs_data_get_int(settings,"details_color")>>8)&255),int((obs_data_get_int(settings,"details_color")>>16)&255),255));next.inheritNameColor=obs_data_get_bool(settings,"inherit_name_color");next.inheritDetailsColor=obs_data_get_bool(settings,"inherit_details_color");
		{std::lock_guard<std::mutex> lock(handle.instance->mutex);handle.instance->settings=std::move(next);handle.instance->testUser=QString::fromUtf8(obs_data_get_string(settings,"test_user")).trimmed().left(64);}
		handle.instance->preferences=true;if(const auto hub=handle.hub.lock())hub->request(handle.instance);
	}catch(...){blog(LOG_ERROR,"[obs-twitch-overlays] Shoutout settings failed");}
}
void visible(void *data,bool show)noexcept{
	try{auto &handle=*static_cast<Handle *>(data);handle.instance->visible=show;
		if(!show){++handle.instance->nonce;handle.instance->reset=true;std::atomic_store(&handle.instance->frame,ShoutoutFramePtr{});handle.instance->completion=0;}
		if(const auto hub=handle.hub.lock())hub->request(handle.instance);
	}catch(...){}
}
bool previewButton(obs_properties_t *props,obs_property_t *property,void *data)noexcept{
	if(!data)return false;auto &handle=*static_cast<Handle *>(data);const auto hub=handle.hub.lock();const auto key=obs_property_name(property);
	const bool end=std::strcmp(key,"preview_end")==0,ok=hub && hub->preview(handle.instance,std::strcmp(key,"preview_long")==0,end);
	QString message;{std::lock_guard<std::mutex> lock(handle.instance->mutex);message=handle.instance->notice;}
	obs_property_set_description(obs_properties_get(props,"preview_status"),ok && !message.isEmpty()?message.toUtf8().constData():obs_module_text(ok?(end?"Alert.PreviewStopped":"Alert.PreviewActive"):"Bar.PreviewFailed"));
	// OBS queues the refresh after this callback returns. Reloading the source
	// properties here would destroy the WidgetInfo still handling this click.
	return true;
}
}
ShoutoutHub::ShoutoutHub(std::shared_ptr<PluginCore> core,std::shared_ptr<visual::VisualEngine> visual,std::shared_ptr<ObsVisualRenderer> renderer,std::shared_ptr<const presets::Registry> presets,std::shared_ptr<visual::AssetManager> assets,std::shared_ptr<twitch::Service> twitch):core_(std::move(core)),visual_(std::move(visual)),renderer_(std::move(renderer)),presets_(std::move(presets)),twitch_(std::move(twitch)),assets_(std::move(assets)){}
void ShoutoutHub::request(const std::shared_ptr<ShoutoutInstance> &instance){
	if(!running_ || instance->queued.exchange(true))return;const auto weak=weak_from_this();const auto item=std::weak_ptr<ShoutoutInstance>(instance);
	if(!QMetaObject::invokeMethod(this,[weak,item]{const auto hub=weak.lock();const auto source=item.lock();if(!hub)return;if(source)source->queued=false;if(!hub->running_)return;if(source && !source->registered){hub->instances_.push_back(source);source->registered=true;}hub->refresh();},Qt::QueuedConnection))instance->queued=false;
}
void ShoutoutHub::prepare(const std::shared_ptr<ShoutoutInstance> &instance,const state::SnapshotPtr &snapshot){
	std::lock_guard<std::mutex> lock(instance->mutex);
	if(instance->reset.exchange(false)){instance->cursor.reset();instance->waiting.reset();instance->resolving=false;}
	if(!instance->visible || (instance->failed && !instance->preferences)){if(!instance->visible)core_->presentations().pause(instance->presentation);else core_->presentations().remove(instance->presentation);instance->cursor.reset();instance->waiting.reset();instance->resolving=false;instance->completion=0;std::atomic_store(&instance->frame,ShoutoutFramePtr{});return;}
	execution::PresentationTypes types{};types[state::slot(events::EventType::Shoutout)]=true;
	const bool liveChanged=core_->presentations().subscribe(instance->presentation,core_->selectedChannel(),types,core_->panelTestChannel());
	if(instance->resolving){if(!twitch_->active()){++instance->nonce;instance->resolving=false;instance->notice=obs_module_text("Alert.UserFailed");}return;}
	const bool preferences=instance->preferences.exchange(false);auto before=std::atomic_load(&instance->frame);
	if(liveChanged && instance->previewChannel.empty() && (!before || (!(state::ChannelKey{before->event.event->data().origin,before->event.event->data().channelId}==core_->selectedChannel()) && !(core_->panelTestChannel() && state::ChannelKey{before->event.event->data().origin,before->event.event->data().channelId}==*core_->panelTestChannel())))){before.reset();instance->waiting.reset();std::atomic_store(&instance->frame,ShoutoutFramePtr{});instance->completion=0;}
	const auto now=visual::MotionClock::now();
	if(before && std::chrono::duration<double,std::milli>(now-before->started).count()>=before->settings.times.total()){
		if(!instance->previewChannel.empty()){core_->releaseSourcePreview(instance->previewChannel);instance->previewChannel.clear();instance->cursor.reset();}
		else core_->presentations().finish(instance->presentation);
		before.reset();std::atomic_store(&instance->frame,ShoutoutFramePtr{});instance->completion=0;core_->log(LogLevel::Info,"Shoutout completed; transparent and idle");
	}
	const auto channel=instance->previewChannel.empty()?core_->selectedChannel():state::ChannelKey{events::EventOrigin::Preview,instance->previewChannel};
	const bool changed=instance->cursor.synchronize(snapshot,channel);
	if(changed){before.reset();instance->waiting.reset();std::atomic_store(&instance->frame,ShoutoutFramePtr{});instance->completion=0;}
	if(before && !preferences)return;
	state::LatestEvent entry;if(before)entry=before->event;else if(instance->waiting)entry=*instance->waiting;else if(!instance->previewChannel.empty()){if(const auto *next=instance->cursor.next(snapshot))entry=*next;}else if(const auto next=core_->presentations().next(instance->presentation))entry=*next;
	if(!entry.event)return;
	const auto &target=std::get<events::ShoutoutEvent>(entry.event->data().payload).target;visual::ImagePtr portrait=before?before->profile:visual::ImagePtr{};
	if(!before && entry.event->data().origin!=events::EventOrigin::Simulation && !(core_->panelTestChannel() && state::ChannelKey{entry.event->data().origin,entry.event->data().channelId}==*core_->panelTestChannel())){const auto id=QString::fromStdString(target.id);auto status=twitch_->profileStatus(id);if(status==twitch::ProfileStatus::Missing){twitch_->requestProfile(id);status=twitch_->profileStatus(id);}if(status==twitch::ProfileStatus::Pending){instance->waiting=entry;return;}portrait=twitch_->profileImage(id);}
	const auto &definition=presets_->resolve(instance->settings.preset);if(!definition.shoutoutStyle)throw std::runtime_error("Shoutout style missing");
	const auto &style=*definition.shoutoutStyle;const auto frame=prepareShoutoutFrame(*visual_,instance->settings,style,entry,std::move(portrait),before?before->started:visual::MotionClock::now(),assets_->load(style.atlas));
	instance->waiting.reset();std::atomic_store(&instance->frame,frame);instance->completion=clockValue(frame->started+std::chrono::milliseconds(frame->settings.times.total()));instance->failed=false;
	if(!before)core_->log(LogLevel::Info,frame->profile?"Shoutout started; profile ready; shared renderer":"Shoutout started; profile fallback; shared renderer");
}
void ShoutoutHub::refresh(){
	if(!running_ || previewTransaction_)return;instances_.erase(std::remove_if(instances_.begin(),instances_.end(),[](const auto &item){return item.expired();}),instances_.end());
	bool visible=false;const auto snapshot=core_->sharedSnapshot();for(const auto &weak:instances_)if(const auto item=weak.lock()){try{prepare(item,snapshot);}catch(...){item->failed=true;item->completion=0;std::atomic_store(&item->frame,ShoutoutFramePtr{});core_->presentations().remove(item->presentation);core_->log(LogLevel::Error,"Shoutout source preparation failed");}visible=visible || (item->visible && !item->failed);}
	twitch_->setShoutoutsEnabled(visible);
}
bool ShoutoutHub::preview(const std::shared_ptr<ShoutoutInstance> &instance,bool longText,bool end)noexcept{
	if(!running_ || QThread::currentThread()!=thread())return false;
	try{const auto nonce=++instance->nonce;instance->resolving=false;previewTransaction_=true;core_->releaseSourcePreview(instance->previewChannel);instance->previewChannel.clear();instance->cursor.reset();instance->waiting.reset();std::atomic_store(&instance->frame,ShoutoutFramePtr{});instance->completion=0;previewTransaction_=false;
		if(end){notice(instance,obs_module_text("Alert.PreviewStopped"),false);refresh();return true;}
		QString login;{std::lock_guard<std::mutex> lock(instance->mutex);login=instance->testUser;}
		if(login.startsWith('@'))login.remove(0,1);login=login.toLower();
		if(login.isEmpty()){notice(instance,obs_module_text("Alert.PreviewActive"),false);startPreview(instance,longText,{{"preview-viewer","DemoViewer"},"demoviewer","TEST · Just Chatting",125000});return true;}
		instance->resolving=true;notice(instance,obs_module_text("Alert.UserLoading"),false);
		twitch_->resolveShoutoutUser(login,[weak=weak_from_this(),item=std::weak_ptr<ShoutoutInstance>(instance),nonce,longText](std::optional<events::ShoutoutEvent> user){const auto hub=weak.lock();const auto source=item.lock();if(!hub || !hub->running_ || !source || !source->visible || source->nonce!=nonce)return;source->resolving=false;try{if(!user){notice(source,obs_module_text("Alert.UserFailed"));return;}notice(source,obs_module_text("Alert.PreviewActive"));hub->startPreview(source,longText,std::move(*user));}catch(...){hub->previewTransaction_=false;notice(source,obs_module_text("Bar.PreviewFailed"));}});return true;
	}catch(...){previewTransaction_=false;refresh();return false;}
}
void ShoutoutHub::startPreview(const std::shared_ptr<ShoutoutInstance> &instance,bool longText,events::ShoutoutEvent user){
	previewTransaction_=true;instance->previewChannel=core_->createShoutoutPreview(longText,std::move(user.target),std::move(user.login),std::move(user.category),user.followers);instance->cursor.synchronize(core_->sharedSnapshot(),{events::EventOrigin::Preview,instance->previewChannel},true);instance->preferences=false;previewTransaction_=false;refresh();
}
void ShoutoutHub::remove(const std::shared_ptr<ShoutoutInstance> &instance){const auto weak=weak_from_this();QMetaObject::invokeMethod(this,[weak,instance]{if(const auto hub=weak.lock();hub && hub->running_){try{hub->core_->presentations().remove(instance->presentation);hub->core_->releaseSourcePreview(instance->previewChannel);hub->refresh();}catch(...){}}},Qt::QueuedConnection);}
void ShoutoutHub::tick(const std::shared_ptr<ShoutoutInstance> &instance)noexcept{
	if(!running_ || !instance->visible || instance->failed)return;auto end=instance->completion.load();if(end && clockValue(visual::MotionClock::now())>=end && instance->completion.compare_exchange_strong(end,0))try{request(instance);}catch(...){}
}
void ShoutoutHub::draw(const std::shared_ptr<ShoutoutInstance> &instance)noexcept{
	if(!running_ || !instance->visible || instance->failed)return;const auto frame=std::atomic_load(&instance->frame);if(!frame)return;try{drawShoutoutFrame(*renderer_,*frame,visual::MotionClock::now());}catch(...){instance->failed=true;instance->completion=0;request(instance);}
}
void ShoutoutHub::stop()noexcept{
	if(!running_.exchange(false))return;twitch_->setShoutoutsEnabled(false);for(const auto &weak:instances_)if(const auto item=weak.lock())core_->presentations().remove(item->presentation);obs_enter_graphics();for(const auto &weak:instances_)if(const auto item=weak.lock()){std::atomic_store(&item->frame,ShoutoutFramePtr{});item->completion=0;}renderer_.reset();visual_.reset();obs_leave_graphics();instances_.clear();
}
void registerShoutoutSource(const std::shared_ptr<ShoutoutHub> &hub){
	obs_source_info info{};info.id="oto_shoutout_card";info.type=OBS_SOURCE_TYPE_INPUT;info.output_flags=OBS_SOURCE_VIDEO|OBS_SOURCE_CUSTOM_DRAW;
	info.type_data=new std::weak_ptr<ShoutoutHub>(hub);info.free_type_data=[](void *data){delete static_cast<std::weak_ptr<ShoutoutHub> *>(data);};info.get_name=[](void *){return obs_module_text("Shoutout.Name");};
	info.create=[](obs_data_t *settings,obs_source_t *source)->void *{try{const auto *owner=static_cast<std::weak_ptr<ShoutoutHub> *>(obs_source_get_type_data(source));const auto hub=owner?owner->lock():nullptr;if(!hub)return nullptr;auto handle=std::make_unique<Handle>(Handle{std::make_shared<ShoutoutInstance>(),hub});handle->instance->source=std::shared_ptr<obs_weak_source_t>(obs_source_get_weak_source(source),obs_weak_source_release);update(handle.get(),settings);return handle.release();}catch(...){return nullptr;}};
	info.destroy=[](void *data){if(!data)return;auto *handle=static_cast<Handle *>(data);handle->instance->visible=false;++handle->instance->nonce;if(const auto hub=handle->hub.lock())hub->remove(handle->instance);delete handle;};
	info.update=update;info.show=[](void *data){visible(data,true);};info.hide=[](void *data){visible(data,false);};info.get_width=[](void *)->uint32_t{return 800;};info.get_height=[](void *)->uint32_t{return 800;};
	info.video_tick=[](void *data,float){if(data){auto &h=*static_cast<Handle *>(data);if(const auto hub=h.hub.lock())hub->tick(h.instance);}};
	info.video_render=[](void *data,gs_effect_t *){if(data){auto &h=*static_cast<Handle *>(data);if(const auto hub=h.hub.lock())hub->draw(h.instance);}};
	info.get_defaults=[](obs_data_t *settings){obs_data_set_default_string(settings,"preset","neon-dj");obs_data_set_default_int(settings,"entry_ms",2000);obs_data_set_default_int(settings,"hold_ms",6000);obs_data_set_default_int(settings,"exit_ms",700);obs_data_set_default_int(settings,"name_color",0xffffff);obs_data_set_default_int(settings,"details_color",0xffffff);obs_data_set_default_bool(settings,"inherit_name_color",true);obs_data_set_default_bool(settings,"inherit_details_color",true);fonts::defaults(settings,"Segoe UI");};
	info.get_properties=[](void *data)->obs_properties_t *{auto *props=obs_properties_create();obs_properties_add_text(props,"notice",obs_module_text("Shoutout.Notice"),OBS_TEXT_INFO);
		auto *style=obs_properties_add_list(props,"preset",obs_module_text("Bar.Style"),OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_STRING);
		if(data)if(const auto hub=static_cast<Handle *>(data)->hub.lock())for(const auto &definition:hub->presets().entries())if(definition.shoutoutStyle)obs_property_list_add_string(style,definition.name.toUtf8().constData(),definition.id.toUtf8().constData());
		obs_properties_add_int(props,"entry_ms",obs_module_text("Alert.Entry"),100,10000,100);obs_properties_add_int(props,"hold_ms",obs_module_text("Alert.Hold"),0,60000,100);obs_properties_add_int(props,"exit_ms",obs_module_text("Alert.Exit"),100,10000,100);
		obs_properties_add_color(props,"name_color",obs_module_text("Shoutout.NameColor"));obs_properties_add_bool(props,"inherit_name_color",obs_module_text("Shoutout.StyleColor"));obs_properties_add_color(props,"details_color",obs_module_text("Shoutout.DetailsColor"));obs_properties_add_bool(props,"inherit_details_color",obs_module_text("Shoutout.StyleColor"));
		obs_properties_add_text(props,"test_user",obs_module_text("Shoutout.TestUser"),OBS_TEXT_DEFAULT);obs_properties_add_button(props,"preview",obs_module_text("Bar.PreviewTest"),previewButton);obs_properties_add_button(props,"preview_long",obs_module_text("Bar.PreviewLong"),previewButton);obs_properties_add_button(props,"preview_end",obs_module_text("Bar.PreviewEnd"),previewButton);obs_properties_add_text(props,"preview_status",obs_module_text("Shoutout.PreviewNotice"),OBS_TEXT_INFO);
		if(data){auto &handle=*static_cast<Handle *>(data);std::lock_guard<std::mutex> lock(handle.instance->mutex);if(!handle.instance->notice.isEmpty())obs_property_set_description(obs_properties_get(props,"preview_status"),handle.instance->notice.toUtf8().constData());}
		fonts::addProperties(props,obs_module_text("Bar.ValueFont"),obs_module_text("Font.Labels"),obs_module_text("Bar.FontInherit"),obs_module_text("Font.Help"));return props;};
	obs_register_source(&info);blog(LOG_INFO,"[obs-twitch-overlays] Shoutout Card registered (800x800; !so @user; streamer/moderators; no audio)");
}
}
