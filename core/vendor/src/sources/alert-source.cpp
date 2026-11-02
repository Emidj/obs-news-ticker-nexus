// SPDX-License-Identifier: GPL-2.0-or-later
#include "alert-source.hpp"
#include "font-properties.hpp"
#include "adapters/alert-draw.hpp"
#include <obs-module.h>
#include <QThread>
#include <cstring>
#include <algorithm>
namespace oto::sources {
namespace {
constexpr const char *keys[]={"follow","sub","cheer","gift","raid"};
constexpr const char *labels[]={"List.Follow","List.Sub","List.Cheer","List.Gift","List.Raid"};
struct Handle {std::shared_ptr<AlertInstance> instance;std::weak_ptr<AlertHub> hub;};
void notice(const std::shared_ptr<AlertInstance> &instance,QString value,bool repaint=true){
	{std::lock_guard<std::mutex> lock(instance->mutex);instance->previewNotice=std::move(value);}
	if(repaint && instance->source)if(auto *source=obs_weak_source_get_source(instance->source.get())){obs_source_update_properties(source);obs_source_release(source);}
}
std::int64_t clockValue(visual::MotionClock::time_point value){return std::chrono::duration_cast<std::chrono::nanoseconds>(value.time_since_epoch()).count();}
const events::User *actor(const events::EventPayload &payload){return std::visit([](const auto &item)->const events::User *{using T=std::decay_t<decltype(item)>;if constexpr(std::is_same_v<T,events::FollowEvent>)return &item.follower;else if constexpr(std::is_same_v<T,events::SubEvent>)return &item.subscriber;else if constexpr(std::is_same_v<T,events::CheerEvent>)return item.user?&*item.user:nullptr;else if constexpr(std::is_same_v<T,events::GiftEvent>)return item.gifter?&*item.gifter:nullptr;else if constexpr(std::is_same_v<T,events::RaidEvent>)return &item.raider;else return nullptr;},payload);}
void update(void *data,obs_data_t *settings)noexcept{
	try{auto &h=*static_cast<Handle *>(data);AlertSettings next;next.presetId=QString::fromUtf8(obs_data_get_string(settings,"preset")).left(48);if(next.presetId.isEmpty())next.presetId="neon-dj";
		next.times={int(std::clamp(obs_data_get_int(settings,"entry_ms"),100LL,10000LL)),int(std::clamp(obs_data_get_int(settings,"hold_ms"),0LL,60000LL)),int(std::clamp(obs_data_get_int(settings,"exit_ms"),100LL,10000LL))};
		for(int n=0;n<5;++n)next.types[n]=obs_data_get_bool(settings,keys[n]);next.nameFamily=fonts::overrideFamily(settings,fonts::names);next.labelFamily=fonts::overrideFamily(settings,fonts::labels);
		{std::lock_guard<std::mutex> lock(h.instance->mutex);h.instance->settings=std::move(next);h.instance->testUser=QString::fromUtf8(obs_data_get_string(settings,"test_user")).trimmed().left(64);}h.instance->preferences=true;if(const auto hub=h.hub.lock())hub->request(h.instance);
	}catch(...){blog(LOG_ERROR,"[obs-twitch-overlays] Animated Alerts update failed");}
}
void visible(void *data,bool show)noexcept{try{auto &h=*static_cast<Handle *>(data);{std::lock_guard<std::mutex> lock(h.instance->mutex);h.instance->visible=show;if(!show){++h.instance->previewNonce;std::atomic_store(&h.instance->frame,AlertFramePtr{});h.instance->completion=0;}}if(const auto hub=h.hub.lock())hub->request(h.instance);}catch(...){}}
bool previewButton(obs_properties_t *props,obs_property_t *property,void *data)noexcept{
	if(!data)return false;auto &h=*static_cast<Handle *>(data);const auto hub=h.hub.lock();const auto key=obs_property_name(property);int variant=0;
	for(int n=0;n<6;++n)if(std::strcmp(key,("test_"+std::to_string(n)).c_str())==0)variant=n;
	const bool end=std::strcmp(key,"preview_end")==0,ok=hub && hub->preview(h.instance,variant,std::strcmp(key,"preview_long")==0,end);
	QString message;{std::lock_guard<std::mutex> lock(h.instance->mutex);message=h.instance->previewNotice;}
	obs_property_set_description(obs_properties_get(props,"preview_status"),ok && !message.isEmpty()?message.toUtf8().constData():obs_module_text(ok?(end?"Alert.PreviewStopped":"Alert.PreviewActive"):"Bar.PreviewFailed"));return true;
}
}
AlertHub::AlertHub(std::shared_ptr<PluginCore> core,std::shared_ptr<visual::VisualEngine> visual,std::shared_ptr<ObsVisualRenderer> renderer,std::shared_ptr<const presets::Registry> presets,std::shared_ptr<visual::AssetManager> assets,std::shared_ptr<twitch::Service> twitch):core_(std::move(core)),visual_(std::move(visual)),renderer_(std::move(renderer)),presets_(std::move(presets)),assets_(std::move(assets)),twitch_(std::move(twitch)){}
void AlertHub::request(const std::shared_ptr<AlertInstance> &instance){
	if(!running_ || instance->queued.exchange(true))return;const auto weak=weak_from_this();const auto item=std::weak_ptr<AlertInstance>(instance);
	if(!QMetaObject::invokeMethod(this,[weak,item]{const auto hub=weak.lock();const auto source=item.lock();if(!hub)return;if(source)source->queued=false;if(!hub->running_)return;if(source && !source->registered){hub->instances_.push_back(source);source->registered=true;}hub->refresh();},Qt::QueuedConnection))instance->queued=false;
}
void AlertHub::prepare(const std::shared_ptr<AlertInstance> &instance,const state::SnapshotPtr &snapshot){
	if(!running_)return;std::lock_guard<std::mutex> lock(instance->mutex);
	if(!instance->visible || (instance->failed && !instance->preferences)){if(!instance->visible)core_->presentations().pause(instance->presentation);else core_->presentations().remove(instance->presentation);instance->cursor.reset();instance->waiting.reset();instance->completion=0;instance->resolving=false;std::atomic_store(&instance->frame,AlertFramePtr{});return;}
	execution::PresentationTypes types{};std::copy(instance->settings.types.begin(),instance->settings.types.end(),types.begin());
	const bool liveChanged=core_->presentations().subscribe(instance->presentation,core_->selectedChannel(),types,core_->panelTestChannel());
	if(instance->resolving){if(!twitch_->active()){++instance->previewNonce;instance->resolving=false;instance->previewNotice=obs_module_text("Alert.UserFailed");}return;}
	const bool preferences=instance->preferences.exchange(false);auto before=std::atomic_load(&instance->frame);
	const auto now=visual::MotionClock::now();
	if(liveChanged && instance->previewChannel.empty() && (!before || (!(state::ChannelKey{before->event->data().origin,before->event->data().channelId}==core_->selectedChannel()) && !(core_->panelTestChannel() && state::ChannelKey{before->event->data().origin,before->event->data().channelId}==*core_->panelTestChannel())) || !instance->settings.types[state::slot(events::typeOf(before->event->data().payload))])){before.reset();std::atomic_store(&instance->frame,AlertFramePtr{});instance->waiting.reset();instance->completion=0;}
	if(before && std::chrono::duration<double,std::milli>(now-before->started).count()>=before->settings.times.total()){
		if(!instance->previewChannel.empty()){core_->releaseSourcePreview(instance->previewChannel);instance->previewChannel.clear();instance->cursor.reset();}
		else core_->presentations().finish(instance->presentation);
		before.reset();std::atomic_store(&instance->frame,AlertFramePtr{});instance->completion=0;core_->log(LogLevel::Info,"Animated Alerts completed; transparent and idle");
	}
	const auto channel=instance->previewChannel.empty()?core_->selectedChannel():state::ChannelKey{events::EventOrigin::Preview,instance->previewChannel};
	const bool changed=instance->cursor.synchronize(snapshot,channel);
	if(changed){before.reset();std::atomic_store(&instance->frame,AlertFramePtr{});instance->waiting.reset();instance->completion=0;}
	if(before && !preferences)return;
	if(before && !instance->settings.types[state::slot(events::typeOf(before->event->data().payload))]){before.reset();std::atomic_store(&instance->frame,AlertFramePtr{});instance->completion=0;}
	state::LatestEvent entry;
	if(before){entry.event=before->event;entry.displayName=before->displayName;}
	else if(instance->waiting)entry=*instance->waiting;
	else if(!instance->previewChannel.empty()){if(const auto *next=instance->cursor.next(snapshot,instance->settings.types))entry=*next;}
	else if(const auto next=core_->presentations().next(instance->presentation))entry=*next;
	if(!entry.event)return;
	if(!instance->settings.types[state::slot(events::typeOf(entry.event->data().payload))]){instance->waiting.reset();return;}
	visual::ImagePtr portrait=before?before->profileImage:visual::ImagePtr{};
	if(const auto *user=actor(entry.event->data().payload);!before && user && !(core_->panelTestChannel() && state::ChannelKey{entry.event->data().origin,entry.event->data().channelId}==*core_->panelTestChannel()) && (entry.event->data().origin==events::EventOrigin::Twitch || entry.event->data().origin==events::EventOrigin::Preview)){
		const auto userId=QString::fromStdString(user->id);auto status=twitch_->profileStatus(userId);
		if(status==twitch::ProfileStatus::Missing){twitch_->requestProfile(userId);status=twitch_->profileStatus(userId);}
		if(status==twitch::ProfileStatus::Pending){if(!instance->waiting)core_->log(LogLevel::Info,"Animated Alerts waiting for profile image; timeline not started");instance->waiting=entry;return;}
		portrait=twitch_->profileImage(userId);
	}
	const auto *definition=presets_->find(instance->settings.presetId);if(!definition || !definition->alertStyle)definition=presets_->find("neon-dj");if(!definition || !definition->alertStyle)throw std::runtime_error("Alert preset missing");
	const auto &style=*definition->alertStyle;
	const auto variant=alertVariant(entry.event->data().payload);const auto &clip=style.iconAnimations[variant];
	const auto frame=prepareAlertFrame(*visual_,instance->settings,style,entry,clip.isEmpty()?assets_->load(style.atlas):visual::ImagePtr{},std::move(portrait),before?before->started:visual::MotionClock::now(),clip.isEmpty()?visual::ImagePtr{}:assets_->load(clip));
	instance->waiting.reset();std::atomic_store(&instance->frame,frame);instance->completion=clockValue(frame->started+std::chrono::milliseconds(frame->settings.times.total()));instance->failed=false;
	if(!before)core_->log(LogLevel::Info,QStringLiteral("Animated Alerts started: variant %1; profile %2; entry/hold/exit %3/%4/%5 ms; style %6").arg(frame->variant).arg(frame->profileImage?"ready":"fallback").arg(frame->settings.times.entry).arg(frame->settings.times.hold).arg(frame->settings.times.exit).arg(definition->id).toStdString());
}
void AlertHub::refresh(){
	if(!running_ || previewTransaction_)return;instances_.erase(std::remove_if(instances_.begin(),instances_.end(),[](const auto &item){return item.expired();}),instances_.end());const auto snapshot=core_->sharedSnapshot();
	for(const auto &weak:instances_)if(const auto item=weak.lock())try{prepare(item,snapshot);}catch(...){item->failed=true;item->completion=0;std::atomic_store(&item->frame,AlertFramePtr{});core_->presentations().remove(item->presentation);core_->log(LogLevel::Error,"Animated Alerts preparation failed; source suspended");}
}
bool AlertHub::preview(const std::shared_ptr<AlertInstance> &instance,int variant,bool longText,bool end)noexcept{
	if(!running_ || QThread::currentThread()!=thread())return false;
	try{const auto nonce=++instance->previewNonce;instance->resolving=false;previewTransaction_=true;core_->releaseSourcePreview(instance->previewChannel);instance->previewChannel.clear();instance->cursor.reset();instance->waiting.reset();std::atomic_store(&instance->frame,AlertFramePtr{});instance->completion=0;previewTransaction_=false;
		if(end){notice(instance,obs_module_text("Alert.PreviewStopped"),false);refresh();return true;}
		QString login;{std::lock_guard<std::mutex> lock(instance->mutex);login=instance->testUser;}
		if(login.isEmpty()){notice(instance,obs_module_text("Alert.PreviewActive"),false);startPreview(instance,variant,longText,twitch_->previewActor());return true;}
		instance->resolving=true;notice(instance,obs_module_text("Alert.UserLoading"),false);
		twitch_->resolvePreviewUser(login,[weak=weak_from_this(),item=std::weak_ptr<AlertInstance>(instance),nonce,variant,longText](std::optional<events::User> user){
			const auto hub=weak.lock();const auto instance=item.lock();if(!hub || !hub->running_ || !instance || !instance->visible || instance->previewNonce!=nonce)return;
			instance->resolving=false;
			try{if(!user){notice(instance,obs_module_text("Alert.UserFailed"));return;}notice(instance,QString::fromUtf8(obs_module_text("Alert.TestUserPhoto")).arg(QString::fromStdString(user->displayName)));hub->startPreview(instance,variant,longText,std::move(*user));}
			catch(...){hub->previewTransaction_=false;notice(instance,obs_module_text("Bar.PreviewFailed"));}
		});return true;
	}catch(...){previewTransaction_=false;refresh();core_->log(LogLevel::Error,"Animated Alerts TEST failed; saved counters preserved");return false;}
}
void AlertHub::startPreview(const std::shared_ptr<AlertInstance> &instance,int variant,bool longText,events::User user){
	constexpr events::EventType types[]={events::EventType::Follow,events::EventType::Sub,events::EventType::Gift,events::EventType::Gift,events::EventType::Raid,events::EventType::Cheer};
	previewTransaction_=true;instance->previewChannel=core_->createAlertPreview(types[std::clamp(variant,0,5)],variant==3,longText,std::move(user));instance->cursor.synchronize(core_->sharedSnapshot(),{events::EventOrigin::Preview,instance->previewChannel},true);instance->preferences=false;previewTransaction_=false;refresh();
}
void AlertHub::remove(const std::shared_ptr<AlertInstance> &instance){const auto weak=weak_from_this();QMetaObject::invokeMethod(this,[weak,instance]{if(const auto hub=weak.lock();hub && hub->running_){try{hub->core_->presentations().remove(instance->presentation);hub->core_->releaseSourcePreview(instance->previewChannel);instance->previewChannel.clear();hub->refresh();}catch(...){}}},Qt::QueuedConnection);}
void AlertHub::tick(const std::shared_ptr<AlertInstance> &instance)noexcept{
	if(!running_ || !instance->visible || instance->failed)return;auto end=instance->completion.load();if(!end)return;
	if(clockValue(visual::MotionClock::now())>=end && instance->completion.compare_exchange_strong(end,0))try{request(instance);}catch(...){}
}
void AlertHub::draw(const std::shared_ptr<AlertInstance> &instance)noexcept{
	if(!running_ || !instance->visible || instance->failed)return;const auto frame=std::atomic_load(&instance->frame);if(!frame || !renderer_)return;
	try{drawAlertFrame(*renderer_,*frame,visual::MotionClock::now());}catch(const std::exception &error){instance->failed=true;instance->completion=0;blog(LOG_ERROR,"[obs-twitch-overlays] Animated Alerts GPU draw failed: %s",error.what());request(instance);}catch(...){instance->failed=true;instance->completion=0;blog(LOG_ERROR,"[obs-twitch-overlays] Animated Alerts GPU draw failed");request(instance);}
}
void AlertHub::stop()noexcept{
	if(!running_.exchange(false))return;for(const auto &weak:instances_)if(const auto item=weak.lock())core_->presentations().remove(item->presentation);obs_enter_graphics();for(const auto &weak:instances_)if(const auto item=weak.lock()){std::atomic_store(&item->frame,AlertFramePtr{});item->completion=0;}renderer_.reset();visual_.reset();obs_leave_graphics();instances_.clear();core_->log(LogLevel::Info,"Animated Alerts source stopped; shared cursors released");
}
void registerAlertSource(const std::shared_ptr<AlertHub> &hub){
	obs_source_info info{};info.id="oto_animated_alerts";info.type=OBS_SOURCE_TYPE_INPUT;info.output_flags=OBS_SOURCE_VIDEO|OBS_SOURCE_CUSTOM_DRAW;
	info.type_data=new std::weak_ptr<AlertHub>(hub);info.free_type_data=[](void *data){delete static_cast<std::weak_ptr<AlertHub> *>(data);};info.get_name=[](void *){return obs_module_text("Alert.Name");};
	info.create=[](obs_data_t *settings,obs_source_t *source)->void *{try{const auto *owner=static_cast<std::weak_ptr<AlertHub> *>(obs_source_get_type_data(source));const auto hub=owner?owner->lock():nullptr;if(!hub)return nullptr;auto handle=std::make_unique<Handle>(Handle{std::make_shared<AlertInstance>(),hub});handle->instance->source=std::shared_ptr<obs_weak_source_t>(obs_source_get_weak_source(source),obs_weak_source_release);update(handle.get(),settings);return handle.release();}catch(...){return nullptr;}};
	info.destroy=[](void *data){if(!data)return;auto *handle=static_cast<Handle *>(data);handle->instance->visible=false;++handle->instance->previewNonce;if(const auto hub=handle->hub.lock())hub->remove(handle->instance);delete handle;};
	info.update=update;info.show=[](void *data){visible(data,true);};info.hide=[](void *data){visible(data,false);};info.get_width=[](void *)->uint32_t{return 1000;};info.get_height=[](void *)->uint32_t{return 1000;};
	info.video_tick=[](void *data,float){if(!data)return;auto &h=*static_cast<Handle *>(data);if(const auto hub=h.hub.lock())hub->tick(h.instance);};
	info.video_render=[](void *data,gs_effect_t *){if(!data)return;auto &h=*static_cast<Handle *>(data);if(const auto hub=h.hub.lock())hub->draw(h.instance);};
	info.get_defaults=[](obs_data_t *settings){obs_data_set_default_string(settings,"preset","neon-dj");obs_data_set_default_int(settings,"entry_ms",1500);obs_data_set_default_int(settings,"hold_ms",3000);obs_data_set_default_int(settings,"exit_ms",1500);for(const auto *key:keys)obs_data_set_default_bool(settings,key,true);fonts::defaults(settings,"Segoe UI Black");};
	info.get_properties=[](void *data)->obs_properties_t *{auto *props=obs_properties_create();obs_properties_add_text(props,"notice",obs_module_text("Alert.Notice"),OBS_TEXT_INFO);auto *style=obs_properties_add_list(props,"preset",obs_module_text("Bar.Style"),OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_STRING);if(data){auto &h=*static_cast<Handle *>(data);if(const auto hub=h.hub.lock())for(const auto &preset:hub->presets().entries())if(preset.alertStyle)obs_property_list_add_string(style,preset.name.toUtf8().constData(),preset.id.toUtf8().constData());}
		obs_properties_add_int(props,"entry_ms",obs_module_text("Alert.Entry"),100,10000,100);obs_properties_add_int(props,"hold_ms",obs_module_text("Alert.Hold"),0,60000,100);obs_properties_add_int(props,"exit_ms",obs_module_text("Alert.Exit"),100,10000,100);
		obs_properties_add_text(props,"test_user",obs_module_text("Alert.TestUser"),OBS_TEXT_DEFAULT);
		constexpr const char *captions[]={"Alert.TestFollow","Alert.TestSub","Alert.TestGift","Alert.TestPack","Alert.TestRaid","Alert.TestCheer"};for(int n=0;n<6;++n)obs_properties_add_button(props,("test_"+std::to_string(n)).c_str(),obs_module_text(captions[n]),previewButton);
		obs_properties_add_button(props,"preview_long",obs_module_text("Bar.PreviewLong"),previewButton);obs_properties_add_button(props,"preview_end",obs_module_text("Bar.PreviewEnd"),previewButton);obs_properties_add_text(props,"preview_status",obs_module_text("Alert.PreviewNotice"),OBS_TEXT_INFO);
		if(data){auto &h=*static_cast<Handle *>(data);std::lock_guard<std::mutex> lock(h.instance->mutex);if(!h.instance->previewNotice.isEmpty())obs_property_set_description(obs_properties_get(props,"preview_status"),h.instance->previewNotice.toUtf8().constData());}
		fonts::addProperties(props,obs_module_text("Bar.ValueFont"),obs_module_text("Font.Labels"),obs_module_text("Bar.FontInherit"),obs_module_text("Font.Help"));for(int n=0;n<5;++n)obs_properties_add_bool(props,keys[n],obs_module_text(labels[n]));return props;};
	obs_register_source(&info);blog(LOG_INFO,"[obs-twitch-overlays] Animated Alerts source registered (1000x1000; shared renderer and activity cursor; no audio)");
}
}
