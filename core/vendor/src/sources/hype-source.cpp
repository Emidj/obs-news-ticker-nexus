// SPDX-License-Identifier: GPL-2.0-or-later
#include "hype-source.hpp"
#include "font-properties.hpp"
#include "adapters/hype-draw.hpp"
#include <obs-module.h>
#include <QThread>
#include <cstring>
namespace oto::sources {
namespace {
struct Handle{std::shared_ptr<HypeInstance> instance;std::weak_ptr<HypeHub> hub;};
QColor color(obs_data_t *data,const char *key){const auto v=obs_data_get_int(data,key);return QColor(int(v&255),int((v>>8)&255),int((v>>16)&255));}
void update(void *data,obs_data_t *settings)noexcept {
	try {
		auto &handle=*static_cast<Handle *>(data);HypeSettings next;
		next.presetId=QString::fromUtf8(obs_data_get_string(settings,"preset"));if(const auto hub=handle.hub.lock())next.style=hub->style(next.presetId);
		next.numberFamily=next.labelFamily=next.style.family;next.accent=next.style.accent;next.numbers=next.style.numbers;next.labels=next.style.labels;
		next.width=int(std::clamp(obs_data_get_int(settings,"width"),420LL,1920LL));next.height=int(std::clamp(obs_data_get_int(settings,"height"),120LL,400LL));
		next.backgroundOpacity=int(std::clamp(obs_data_get_int(settings,"background_opacity"),0LL,100LL));
		next.entranceMs=int(std::clamp(obs_data_get_int(settings,"entry_ms"),100LL,5000LL));next.exitMs=int(std::clamp(obs_data_get_int(settings,"exit_ms"),100LL,5000LL));next.holdMs=int(std::clamp(obs_data_get_int(settings,"hold_ms"),0LL,30000LL));
		const auto nf=fonts::overrideFamily(settings,fonts::names),lf=fonts::overrideFamily(settings,fonts::labels);if(!nf.isEmpty())next.numberFamily=nf;if(!lf.isEmpty())next.labelFamily=lf;
		next.numberSize=fonts::overridePixelSize(settings,fonts::names);next.labelSize=fonts::overridePixelSize(settings,fonts::labels);
		next.fillMode=int(std::clamp(obs_data_get_int(settings,"fill_mode"),0LL,2LL));
		next.labelsFromStyle=obs_data_get_bool(settings,"inherit_labels_color");next.artworkTitle=lf.isEmpty() && next.labelSize==0 && next.labelsFromStyle;next.numbersFromStyle=obs_data_get_bool(settings,"inherit_numbers_color");
		if(!obs_data_get_bool(settings,"inherit_accent_color"))next.accent=color(settings,"accent_color");if(!obs_data_get_bool(settings,"inherit_numbers_color"))next.numbers=color(settings,"numbers_color");if(!obs_data_get_bool(settings,"inherit_labels_color"))next.labels=color(settings,"labels_color");
		{std::lock_guard<std::mutex> lock(handle.instance->mutex);handle.instance->settings=next;handle.instance->width=next.width;handle.instance->height=next.height;}
		if(const auto hub=handle.hub.lock())hub->request(handle.instance);
	}catch(...){blog(LOG_ERROR,"[obs-twitch-overlays] Hype Meter update failed");}
}
void visible(void *data,bool show)noexcept {
	try{auto &handle=*static_cast<Handle *>(data);{std::lock_guard<std::mutex> lock(handle.instance->mutex);handle.instance->visible=show;if(!show)std::atomic_store(&handle.instance->frame,HypeFramePtr{});}if(const auto hub=handle.hub.lock())hub->request(handle.instance);}catch(...){}
}
bool previewButton(obs_properties_t *props,obs_property_t *property,void *data)noexcept {
	if(!data)return false;const auto &handle=*static_cast<Handle *>(data);const char *name=obs_property_name(property);
	const int action=std::strcmp(name,"test_progress")==0?1:std::strcmp(name,"test_level")==0?2:std::strcmp(name,"test_finish")==0?3:std::strcmp(name,"preview_end")==0?4:std::strcmp(name,"preview_long")==0?5:0;
	const auto hub=handle.hub.lock();const bool ok=hub && hub->preview(handle.instance,action);
	obs_property_set_description(obs_properties_get(props,"preview_status"),obs_module_text(ok?(action==4?"Alert.PreviewStopped":"Hype.PreviewActive"):"Hype.PreviewFailed"));
	return true; // Never reload the source properties inline inside WidgetInfo::ButtonClicked.
}
}
HypeHub::HypeHub(std::shared_ptr<PluginCore> core,std::shared_ptr<visual::VisualEngine> visual,std::shared_ptr<ObsVisualRenderer> renderer,std::shared_ptr<twitch::Service> twitch,std::shared_ptr<const presets::Registry> presets,std::shared_ptr<visual::AssetManager> assets):core_(std::move(core)),visual_(std::move(visual)),renderer_(std::move(renderer)),twitch_(std::move(twitch)),presets_(std::move(presets)),assets_(std::move(assets)) {
	labels_={QString::fromUtf8(obs_module_text("Hype.Title")),QString::fromUtf8(obs_module_text("Hype.Level")),QString::fromUtf8(obs_module_text("Hype.Ended")),QString::fromUtf8(obs_module_text("Hype.Waiting")),QString::fromUtf8(obs_module_text("Hype.Remaining")),QString::fromUtf8(obs_module_text("Hype.Contributors"))};
	timer_.setSingleShot(true);connect(&timer_,&QTimer::timeout,this,[this]{refresh();});
}
presets::HypeStyle HypeHub::style(const QString &id)const {if(presets_){const auto &p=presets_->resolve(id);if(p.hypeStyle)return *p.hypeStyle;}return {};}
void HypeHub::request(const std::shared_ptr<HypeInstance> &item) {
	if(!running_ || item->queued.exchange(true))return;const auto weak=weak_from_this();const auto instance=std::weak_ptr<HypeInstance>(item);
	if(!QMetaObject::invokeMethod(this,[weak,instance]{const auto hub=weak.lock();const auto source=instance.lock();if(!hub)return;if(source)source->queued=false;if(!hub->running_)return;if(source && !source->registered){source->registered=true;hub->instances_.push_back(source);}hub->refresh();},Qt::QueuedConnection))item->queued=false;
}
void HypeHub::prepare(const std::shared_ptr<HypeInstance> &item,const state::SnapshotPtr &snapshot,bool entrance) {
	if(!item->visible)return;std::lock_guard<std::mutex> lock(item->mutex);
	const auto key=item->previewChannel.empty()?core_->displayChannel():state::ChannelKey{events::EventOrigin::Preview,item->previewChannel};
	const auto before=std::atomic_load(&item->frame);const auto view=hypeView(snapshot,key,std::chrono::system_clock::now(),item->settings.holdMs,item->settings.exitMs);
	const bool fresh=item->initialized && snapshot && item->generation==snapshot->generation && item->observedChannel==key && view.visible && item->observedTrain!=view.trainId;
	visual::ImagePtr art,thanks;
	if(view.visible && assets_ && !item->settings.style.atlas.isEmpty() && (!before || !(before->settings==item->settings))){
		art=assets_->loadRegion(item->settings.style.atlas,item->settings.style.atlasCrop);
		if(!item->settings.style.thanksAtlas.isEmpty())thanks=assets_->loadRegion(item->settings.style.thanksAtlas,item->settings.style.thanksCrop);
	}
	std::atomic_store(&item->frame,prepareHypeFrame(*visual_,item->settings,labels_,view,before,visual::MotionClock::now(),entrance || fresh,std::move(art),std::move(thanks)));item->failed=false;
	item->initialized=true;item->observedChannel=key;item->generation=snapshot?snapshot->generation:0;if(view.visible)item->observedTrain=view.trainId;
}
void HypeHub::refresh() {
	if(!running_ || previewTransaction_)return;instances_.erase(std::remove_if(instances_.begin(),instances_.end(),[](const auto &w){return w.expired();}),instances_.end());
	const auto snapshot=core_->sharedSnapshot();bool active=false,visible=false;
	for(const auto &weak:instances_)if(const auto item=weak.lock()) {
		visible=visible || item->visible;
		try{prepare(item,snapshot);active=active || (item->visible && bool(std::atomic_load(&item->frame)));}
		catch(...){item->failed=true;core_->log(LogLevel::Error,"Hype Meter preparation failed");}
	}
	twitch_->setHypeMeterEnabled(visible);
	if(active){if(!timer_.isActive())timer_.start(1000);}else timer_.stop();
}
bool HypeHub::preview(const std::shared_ptr<HypeInstance> &item,int action)noexcept {
	if(!running_ || QThread::currentThread()!=thread())return false;
	try {
		previewTransaction_=true;
		if(action==0 || action==4 || action==5){core_->releaseSourcePreview(item->previewChannel);item->previewChannel.clear();}
		if(action==0 || action==5)item->previewChannel=core_->createHypePreview(action==5);
		else if(action!=4){if(item->previewChannel.empty())throw std::runtime_error("Start a private Hype TEST first");core_->updateHypePreview(item->previewChannel,action==3?events::HypeTrainStage::End:events::HypeTrainStage::Progress,action==2);}
		prepare(item,core_->sharedSnapshot(),action==0 || action==5);previewTransaction_=false;refresh();return true;
	}catch(...){previewTransaction_=false;refresh();return false;}
}
void HypeHub::remove(const std::shared_ptr<HypeInstance> &item) {
	const auto weak=weak_from_this();QMetaObject::invokeMethod(this,[weak,item]{if(const auto hub=weak.lock();hub && hub->running_){try{hub->core_->releaseSourcePreview(item->previewChannel);item->previewChannel.clear();hub->refresh();}catch(...){}}},Qt::QueuedConnection);
}
void HypeHub::draw(const std::shared_ptr<HypeInstance> &item)noexcept {
	if(!running_ || !item->visible || item->failed)return;const auto frame=std::atomic_load(&item->frame);if(!frame)return;
	try {
		drawHypeFrame(*renderer_,*frame,visual::MotionClock::now());
	}catch(...){if(!item->failed.exchange(true))core_->log(LogLevel::Error,"Hype Meter rendering failed");}
}
void HypeHub::stop()noexcept {
	timer_.stop();obs_enter_graphics();if(!running_.exchange(false)){obs_leave_graphics();return;}
	for(const auto &w:instances_)if(const auto item=w.lock())std::atomic_store(&item->frame,HypeFramePtr{});
	renderer_.reset();visual_.reset();obs_leave_graphics();instances_.clear();core_->log(LogLevel::Info,"Hype Meter stopped");
}
void registerHypeSource(const std::shared_ptr<HypeHub> &hub) {
	obs_source_info info{};info.id="oto_hype_train";info.type=OBS_SOURCE_TYPE_INPUT;info.output_flags=OBS_SOURCE_VIDEO|OBS_SOURCE_CUSTOM_DRAW;
	info.get_name=[](void *){return obs_module_text("Hype.Name");};info.type_data=new std::weak_ptr<HypeHub>(hub);info.free_type_data=[](void *p){delete static_cast<std::weak_ptr<HypeHub> *>(p);};
	info.create=[](obs_data_t *settings,obs_source_t *source)->void *{try{const auto weak=*static_cast<std::weak_ptr<HypeHub> *>(obs_source_get_type_data(source));if(weak.expired())return nullptr;auto handle=std::make_unique<Handle>(Handle{std::make_shared<HypeInstance>(),weak});update(handle.get(),settings);return handle.release();}catch(...){return nullptr;}};
	info.destroy=[](void *data){visible(data,false);auto *h=static_cast<Handle *>(data);try{if(const auto hub=h->hub.lock())hub->remove(h->instance);}catch(...){}delete h;};
	info.update=update;info.show=[](void *data){visible(data,true);};info.hide=[](void *data){visible(data,false);};
	info.get_width=[](void *p)->uint32_t{return static_cast<Handle *>(p)->instance->width;};info.get_height=[](void *p)->uint32_t{return static_cast<Handle *>(p)->instance->height;};
	info.video_render=[](void *p,gs_effect_t *){const auto &h=*static_cast<Handle *>(p);if(const auto hub=h.hub.lock())hub->draw(h.instance);};
	info.get_defaults=[](obs_data_t *s){obs_data_set_default_string(s,"preset","neon-dj");obs_data_set_default_int(s,"width",900);obs_data_set_default_int(s,"height",180);obs_data_set_default_int(s,"background_opacity",85);obs_data_set_default_int(s,"fill_mode",2);obs_data_set_default_int(s,"entry_ms",2500);obs_data_set_default_int(s,"exit_ms",1500);obs_data_set_default_int(s,"hold_ms",4000);obs_data_set_default_int(s,"accent_color",0xffebcd00);obs_data_set_default_int(s,"numbers_color",0xffffffff);obs_data_set_default_int(s,"labels_color",0xffdacbb9);obs_data_set_default_bool(s,"inherit_accent_color",true);obs_data_set_default_bool(s,"inherit_numbers_color",true);obs_data_set_default_bool(s,"inherit_labels_color",true);fonts::defaults(s,"Segoe UI");};
	info.get_properties=[](void *data) {
		auto *p=obs_properties_create();obs_properties_add_text(p,"notice",obs_module_text("Hype.Notice"),OBS_TEXT_INFO);
		auto *style=obs_properties_add_list(p,"preset",obs_module_text("Bar.Style"),OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_STRING);const auto hub=data?static_cast<Handle *>(data)->hub.lock():nullptr;
		if(hub && hub->presets()){for(const auto &definition:hub->presets()->entries())if(definition.hypeStyle)obs_property_list_add_string(style,definition.name.toUtf8().constData(),definition.id.toUtf8().constData());}else obs_property_list_add_string(style,"Neon DJ","neon-dj");
		auto *fill=obs_properties_add_list(p,"fill_mode",obs_module_text("Hype.FillMode"),OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_INT);
		obs_property_list_add_int(fill,obs_module_text("Hype.FillFixed"),0);obs_property_list_add_int(fill,obs_module_text("Hype.FillTraffic"),1);obs_property_list_add_int(fill,obs_module_text("Hype.FillAnimated"),2);
		obs_properties_add_int(p,"width",obs_module_text("Bar.Width"),420,1920,10);obs_properties_add_int(p,"height",obs_module_text("Bar.Height"),120,400,10);
		obs_properties_add_int_slider(p,"background_opacity",obs_module_text("List.BackgroundOpacity"),0,100,1);obs_properties_add_color(p,"accent_color",obs_module_text("Hype.Accent"));
		obs_properties_add_bool(p,"inherit_accent_color",obs_module_text("Shoutout.StyleColor"));
		obs_properties_add_button(p,"preview_test",obs_module_text("Bar.PreviewTest"),previewButton);obs_properties_add_button(p,"test_progress",obs_module_text("Hype.TestProgress"),previewButton);obs_properties_add_button(p,"test_level",obs_module_text("Hype.TestLevel"),previewButton);obs_properties_add_button(p,"test_finish",obs_module_text("Hype.TestFinish"),previewButton);obs_properties_add_button(p,"preview_end",obs_module_text("Bar.PreviewEnd"),previewButton);
		obs_properties_add_button(p,"preview_long",obs_module_text("Bar.PreviewLong"),previewButton);
		obs_properties_add_text(p,"preview_status",obs_module_text("Hype.PreviewNotice"),OBS_TEXT_INFO);
		obs_properties_add_int(p,"entry_ms",obs_module_text("Alert.Entry"),100,5000,50);obs_properties_add_int(p,"hold_ms",obs_module_text("Hype.EndHold"),0,30000,100);obs_properties_add_int(p,"exit_ms",obs_module_text("Alert.Exit"),100,5000,50);
		fonts::addProperties(p,obs_module_text("Hype.NumberFont"),obs_module_text("Font.Labels"),obs_module_text("Bar.FontInherit"),obs_module_text("Hype.FontHelp"));
		obs_properties_add_color(p,"numbers_color",obs_module_text("Hype.NumberColor"));obs_properties_add_bool(p,"inherit_numbers_color",obs_module_text("Shoutout.StyleColor"));obs_properties_add_color(p,"labels_color",obs_module_text("Hype.LabelColor"));obs_properties_add_bool(p,"inherit_labels_color",obs_module_text("Shoutout.StyleColor"));return p;
	};
	obs_register_source(&info);blog(LOG_INFO,"[obs-twitch-overlays] Hype Train Meter registered (staged GPU animation; shared state; no audio)");
}
}
