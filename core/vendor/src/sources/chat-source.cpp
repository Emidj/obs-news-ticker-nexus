// SPDX-License-Identifier: GPL-2.0-or-later
#include "chat-source.hpp"
#include "font-properties.hpp"
#include "adapters/chat-draw.hpp"
#include <obs-module.h>
#include <QThread>
#include <cstring>
namespace oto::sources {
namespace {
struct Handle {std::shared_ptr<ChatInstance> instance;std::weak_ptr<ChatHub> hub;};
bool layoutChanged(obs_properties_t *,obs_property_t *,obs_data_t *s)noexcept {
	const auto w=obs_data_get_int(s,"width"),h=obs_data_get_int(s,"height");const bool horizontal=obs_data_get_int(s,"layout")==1;
	if(horizontal && w==600 && h==700){obs_data_set_int(s,"width",1920);obs_data_set_int(s,"height",100);}
	if(!horizontal && w==1920 && h==100){obs_data_set_int(s,"width",600);obs_data_set_int(s,"height",700);}return true;
}
void update(void *data,obs_data_t *s)noexcept {try{
	auto &h=*static_cast<Handle *>(data);ChatSettings next;
	next.presetId=QString::fromUtf8(obs_data_get_string(s,"preset")).left(48);if(next.presetId.isEmpty())next.presetId="minimal";
	if(const auto hub=h.hub.lock())next.style=hub->style(next.presetId);
	next.showFrame=obs_data_get_bool(s,"show_frame");
	next.nameFamily=next.messageFamily=next.style.family;next.nameColor=next.style.name;next.messageColor=next.style.message;
	next.width=int(std::clamp(obs_data_get_int(s,"width"),320LL,1920LL));next.height=int(std::clamp(obs_data_get_int(s,"height"),100LL,1080LL));
	next.messages=int(std::clamp(obs_data_get_int(s,"messages"),1LL,12LL));next.entryMs=int(std::clamp(obs_data_get_int(s,"entry_ms"),0LL,2000LL));next.backgroundOpacity=int(std::clamp(obs_data_get_int(s,"background_opacity"),0LL,100LL));
	next.layout=obs_data_get_int(s,"layout")==1?ChatLayout::Horizontal:ChatLayout::Vertical;next.showCommands=obs_data_get_bool(s,"show_commands");next.twitchColors=obs_data_get_bool(s,"twitch_colors");next.thirdParty=obs_data_get_bool(s,"third_party");next.animatedEmotes=obs_data_get_bool(s,"animated_emotes");
	const auto names=fonts::overrideFamily(s,fonts::names),message=fonts::overrideFamily(s,fonts::labels);if(!names.isEmpty())next.nameFamily=names;if(!message.isEmpty())next.messageFamily=message;
	const auto ns=fonts::overridePixelSize(s,fonts::names),ms=fonts::overridePixelSize(s,fonts::labels);if(ns)next.nameSize=ns;if(ms)next.messageSize=ms;
	const auto color=[&](const char *key){const auto v=uint32_t(obs_data_get_int(s,key));return QColor(v&255,(v>>8)&255,(v>>16)&255);};
	if(!obs_data_get_bool(s,"use_style_name_color"))next.nameColor=color("name_color");
	if(!obs_data_get_bool(s,"use_style_message_color"))next.messageColor=color("message_color");
	{std::lock_guard<std::mutex> lock(h.instance->mutex);h.instance->settings=next;h.instance->width=next.width;h.instance->height=next.height;}if(const auto hub=h.hub.lock())hub->request(h.instance);
}catch(...){blog(LOG_ERROR,"[obs-twitch-overlays] Chat update failed");}}
void visible(void *data,bool show)noexcept {try{auto &h=*static_cast<Handle *>(data);{std::lock_guard<std::mutex> lock(h.instance->mutex);h.instance->visible=show;if(!show)std::atomic_store(&h.instance->frame,ChatFramePtr{});}if(const auto hub=h.hub.lock())hub->request(h.instance);}catch(...){}}
bool previewButton(obs_properties_t *props,obs_property_t *p,void *data)noexcept {
	if(!data)return false;auto &h=*static_cast<Handle *>(data);const auto hub=h.hub.lock();const auto *key=obs_property_name(p);const bool end=std::strcmp(key,"preview_end")==0;
	const auto effects=std::strcmp(key,"preview_cheermotes")==0?ChatPreviewEffects::Cheermotes:std::strcmp(key,"preview_ffz_grow")==0?ChatPreviewEffects::FFZGrow:std::strcmp(key,"preview_ffz_cursed")==0?ChatPreviewEffects::FFZCursed:std::strcmp(key,"preview_bttv_wide")==0?ChatPreviewEffects::BTTVWide:std::strcmp(key,"preview_bttv_mood")==0?ChatPreviewEffects::BTTVMood:std::strcmp(key,"preview_bttv")==0?ChatPreviewEffects::BTTVTransforms:(std::strcmp(key,"preview_effects")==0?ChatPreviewEffects::FFZMirrors:ChatPreviewEffects::None);
	const bool ok=hub && hub->preview(h.instance,std::strcmp(key,"preview_long")==0,end,std::strcmp(key,"preview_add")==0,effects);
	obs_property_set_description(obs_properties_get(props,"preview_status"),obs_module_text(ok?(end?"Bar.PreviewStopped":"Chat.PreviewActive"):"Bar.PreviewFailed"));return true;
}
bool clearButton(obs_properties_t *,obs_property_t *,void *data)noexcept {try{if(data){auto &h=*static_cast<Handle *>(data);if(const auto hub=h.hub.lock())hub->clear(h.instance);}return true;}catch(...){return false;}}
bool refreshEmotesButton(obs_properties_t *props,obs_property_t *,void *data)noexcept {try{
	if(!data)return false;auto &h=*static_cast<Handle *>(data);const auto hub=h.hub.lock();const bool ok=hub && hub->refreshEmotes();
	obs_property_set_description(obs_properties_get(props,"catalog_status"),obs_module_text(ok?"Chat.CatalogRequested":"Chat.CatalogWait"));return true;
}catch(...){return false;}}
bool moderationButton(obs_properties_t *props,obs_property_t *p,void *data)noexcept {
	if(!data)return false;auto &h=*static_cast<Handle *>(data);const auto hub=h.hub.lock();const auto *key=obs_property_name(p);
	const auto action=std::strcmp(key,"preview_delete")==0?events::ChatModerationAction::DeleteMessage:std::strcmp(key,"preview_user")==0?events::ChatModerationAction::ClearUser:events::ChatModerationAction::ClearAll;
	const bool ok=hub && hub->moderatePreview(h.instance,action);
	obs_property_set_description(obs_properties_get(props,"preview_status"),obs_module_text(ok?"Chat.PreviewActive":"Chat.ModerationPreviewRequired"));return true;
}
}
ChatHub::ChatHub(std::shared_ptr<PluginCore> core,std::shared_ptr<visual::VisualEngine> visual,std::shared_ptr<ObsVisualRenderer> renderer,std::shared_ptr<chat::EmoteCache> media,std::shared_ptr<const presets::Registry> presets,std::shared_ptr<visual::AssetManager> assets):core_(std::move(core)),visual_(std::move(visual)),renderer_(std::move(renderer)),media_(std::move(media)),presets_(std::move(presets)),assets_(std::move(assets)){}
void ChatHub::request(const std::shared_ptr<ChatInstance> &instance){
	if(!running_ || instance->queued.exchange(true))return;const auto weak=weak_from_this();const auto item=std::weak_ptr<ChatInstance>(instance);
	if(!QMetaObject::invokeMethod(this,[weak,item]{const auto hub=weak.lock();const auto source=item.lock();if(!hub)return;if(source)source->queued=false;if(!hub->running_)return;if(source && !source->registered){hub->instances_.push_back(source);source->registered=true;}hub->refresh();},Qt::QueuedConnection))instance->queued=false;
}
void ChatHub::prepare(const std::shared_ptr<ChatInstance> &instance,const state::SnapshotPtr &snapshot,bool entrance){
	if(!running_ || !instance->visible)return;std::lock_guard<std::mutex> lock(instance->mutex);
	const auto key=instance->previewChannel.empty()?core_->displayChannel():state::ChannelKey{events::EventOrigin::Preview,instance->previewChannel};
	if(!(key==instance->channel) || instance->generation!=snapshot->generation){instance->channel=key;instance->generation=snapshot->generation;instance->cleared=0;}
	media_->ensureBadges(QString::fromStdString(core_->selectedChannel().channelId));
	media_->ensureCheermotes(QString::fromStdString(core_->selectedChannel().channelId));
	const auto view=chatView(snapshot,key,instance->settings,instance->cleared);const auto before=std::atomic_load(&instance->frame);
	if(instance->settings.thirdParty && key.origin==events::EventOrigin::Twitch)for(const auto &message:view.messages)media_->ensureAuthor(QString::fromStdString(std::get<events::ChatEvent>(message.event->data().payload).author.id));
	visual::ImagePtr artwork;const auto &style=instance->settings.style;
	if(instance->settings.showFrame && !style.atlas.isEmpty() && assets_)artwork=before && before->artwork && before->settings.style.atlas==style.atlas && before->settings.style.crop==style.crop?before->artwork:assets_->loadRegion(style.atlas,style.crop);
	const auto frame=prepareChatFrame(*visual_,instance->settings,view,before,visual::MotionClock::now(),entrance,media_.get(),std::move(artwork));if(frame!=before && instance->visible)std::atomic_store(&instance->frame,frame);instance->failed=false;
}
void ChatHub::refresh(){
	if(!running_ || transaction_)return;instances_.erase(std::remove_if(instances_.begin(),instances_.end(),[](const auto &w){return w.expired();}),instances_.end());const auto snapshot=core_->sharedSnapshot();
	bool catalogActive=false;for(const auto &w:instances_)if(const auto item=w.lock()){std::lock_guard<std::mutex> lock(item->mutex);catalogActive=catalogActive || (item->visible && item->settings.thirdParty);}
	if(!catalogActive)media_->setCatalogActive(false);media_->ensureChannel(QString::fromStdString(core_->selectedChannel().channelId));if(catalogActive)media_->setCatalogActive(true);
	for(const auto &w:instances_)if(const auto item=w.lock())try{prepare(item,snapshot);}catch(...){item->failed=true;core_->log(LogLevel::Error,"Chat preparation failed; source suspended until next update");}
}
bool ChatHub::preview(const std::shared_ptr<ChatInstance> &instance,bool longText,bool end,bool append,ChatPreviewEffects effects)noexcept {
	if(!running_ || QThread::currentThread()!=thread())return false;
	try{transaction_=true;
		if(append && !instance->previewChannel.empty())core_->appendChatPreview(instance->previewChannel,longText);
		else{core_->releaseSourcePreview(instance->previewChannel);instance->previewChannel.clear();if(!end)instance->previewChannel=core_->createChatPreview(longText,effects);}
		prepare(instance,core_->sharedSnapshot(),!end && !append);transaction_=false;refresh();return true;
	}catch(...){transaction_=false;refresh();core_->log(LogLevel::Error,"Chat preview failed");return false;}
}
void ChatHub::clear(const std::shared_ptr<ChatInstance> &instance){
	{std::lock_guard<std::mutex> lock(instance->mutex);const auto snapshot=core_->sharedSnapshot();const auto key=instance->previewChannel.empty()?core_->displayChannel():state::ChannelKey{events::EventOrigin::Preview,instance->previewChannel};instance->channel=key;instance->generation=snapshot->generation;const auto found=snapshot->channels.find(key);instance->cleared=found==snapshot->channels.end()?0:found->second->delivered[size_t(events::EventType::Chat)];}refresh();
}
bool ChatHub::moderatePreview(const std::shared_ptr<ChatInstance> &instance,events::ChatModerationAction action)noexcept {
	if(!running_ || instance->previewChannel.empty() || QThread::currentThread()!=thread())return false;
	try{transaction_=true;core_->moderateChatPreview(instance->previewChannel,action);transaction_=false;refresh();return true;}
	catch(...){transaction_=false;refresh();core_->log(LogLevel::Error,"Chat moderation preview failed");return false;}
}
void ChatHub::remove(const std::shared_ptr<ChatInstance> &instance){const auto weak=weak_from_this();QMetaObject::invokeMethod(this,[weak,instance]{if(const auto hub=weak.lock();hub && hub->running_){hub->core_->releaseSourcePreview(instance->previewChannel);instance->previewChannel.clear();hub->refresh();}},Qt::QueuedConnection);}
void ChatHub::draw(const std::shared_ptr<ChatInstance> &instance)noexcept {if(!running_ || !instance->visible || instance->failed)return;const auto frame=std::atomic_load(&instance->frame);if(!frame)return;try{drawChatFrame(*renderer_,*frame,visual::MotionClock::now());}catch(...){if(!instance->failed.exchange(true))core_->log(LogLevel::Error,"Chat drawing failed");}}
void ChatHub::stop()noexcept {obs_enter_graphics();if(!running_.exchange(false)){obs_leave_graphics();return;}for(const auto &w:instances_)if(const auto item=w.lock())std::atomic_store(&item->frame,ChatFramePtr{});renderer_.reset();visual_.reset();media_->setCatalogActive(false);media_.reset();obs_leave_graphics();instances_.clear();}
void registerChatSource(const std::shared_ptr<ChatHub> &hub){
	obs_source_info info{};info.id="oto_live_chat";info.type=OBS_SOURCE_TYPE_INPUT;info.output_flags=OBS_SOURCE_VIDEO|OBS_SOURCE_CUSTOM_DRAW;
	info.get_name=[](void *){return obs_module_text("Chat.Name");};info.type_data=new std::weak_ptr<ChatHub>(hub);info.free_type_data=[](void *d){delete static_cast<std::weak_ptr<ChatHub> *>(d);};
	info.create=[](obs_data_t *s,obs_source_t *source)->void *{try{const auto weak=*static_cast<std::weak_ptr<ChatHub> *>(obs_source_get_type_data(source));if(weak.expired())return nullptr;auto h=std::make_unique<Handle>(Handle{std::make_shared<ChatInstance>(),weak});update(h.get(),s);return h.release();}catch(...){return nullptr;}};
	info.destroy=[](void *d){visible(d,false);auto *h=static_cast<Handle *>(d);try{if(const auto hub=h->hub.lock())hub->remove(h->instance);}catch(...){}delete h;};
	info.update=update;info.show=[](void *d){visible(d,true);};info.hide=[](void *d){visible(d,false);};info.get_width=[](void *d)->uint32_t{return static_cast<Handle *>(d)->instance->width;};info.get_height=[](void *d)->uint32_t{return static_cast<Handle *>(d)->instance->height;};info.video_render=[](void *d,gs_effect_t *){const auto &h=*static_cast<Handle *>(d);if(const auto hub=h.hub.lock())hub->draw(h.instance);};
	info.get_defaults=[](obs_data_t *s){obs_data_set_default_string(s,"preset","minimal");for(const auto &[key,value]:std::initializer_list<std::pair<const char *,int>>{{"width",600},{"height",700},{"messages",8},{"entry_ms",350},{"background_opacity",85},{"layout",0},{"name_color",0xf5d764},{"message_color",0xfff4eb}})obs_data_set_default_int(s,key,value);for(const auto *key:{"show_frame","show_commands","twitch_colors","third_party","animated_emotes","use_style_name_color","use_style_message_color"})obs_data_set_default_bool(s,key,true);fonts::defaults(s,"Segoe UI");};
	info.get_properties=[](void *data){
		auto *p=obs_properties_create();obs_properties_add_text(p,"notice",obs_module_text("Chat.Notice"),OBS_TEXT_INFO);obs_properties_add_text(p,"preview_status",obs_module_text("Bar.PreviewNotice"),OBS_TEXT_INFO);
		for(const auto &[key,label]:std::initializer_list<std::pair<const char *,const char *>>{{"preview_test","Bar.PreviewTest"},{"preview_add","Chat.Add"},{"preview_long","Bar.PreviewLong"},{"preview_end","Bar.PreviewEnd"},{"preview_effects","Chat.TestEffects"},{"preview_bttv","Chat.TestBTTV"},{"preview_bttv_mood","Chat.TestBTTVMood"},{"preview_bttv_wide","Chat.TestBTTVWide"},{"preview_ffz_cursed","Chat.TestFFZCursed"},{"preview_ffz_grow","Chat.TestFFZGrow"},{"preview_cheermotes","Chat.TestCheermotes"}})obs_properties_add_button(p,key,obs_module_text(label),previewButton);
		for(const auto &[key,label]:std::initializer_list<std::pair<const char *,const char *>>{{"preview_delete","Chat.TestDelete"},{"preview_user","Chat.TestUser"},{"preview_clear","Chat.TestClear"}})obs_properties_add_button(p,key,obs_module_text(label),moderationButton);
		obs_properties_add_button(p,"clear",obs_module_text("Chat.Clear"),clearButton);
		obs_properties_add_button(p,"refresh_emotes",obs_module_text("Chat.RefreshEmotes"),refreshEmotesButton);
		obs_properties_add_text(p,"catalog_status",obs_module_text("Chat.CatalogAuto"),OBS_TEXT_INFO);
		auto *preset=obs_properties_add_list(p,"preset",obs_module_text("Bar.Style"),OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_STRING);
		const auto hub=data?static_cast<Handle *>(data)->hub.lock():nullptr;
		if(hub && hub->registry()){for(const auto &definition:hub->registry()->entries())if(definition.chatStyle)obs_property_list_add_string(preset,definition.name.toUtf8().constData(),definition.id.toUtf8().constData());}
		else obs_property_list_add_string(preset,"Minimal","minimal");
		obs_properties_add_bool(p,"show_frame",obs_module_text("Chat.Frame"));
		auto *layout=obs_properties_add_list(p,"layout",obs_module_text("Chat.Layout"),OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_INT);obs_property_list_add_int(layout,obs_module_text("Chat.Vertical"),0);obs_property_list_add_int(layout,obs_module_text("Chat.Horizontal"),1);obs_property_set_modified_callback(layout,layoutChanged);
		obs_properties_add_int(p,"width",obs_module_text("Bar.Width"),320,1920,10);obs_properties_add_int(p,"height",obs_module_text("Bar.Height"),100,1080,10);obs_properties_add_int(p,"messages",obs_module_text("Chat.Messages"),1,12,1);obs_properties_add_int(p,"entry_ms",obs_module_text("Chat.Entry"),0,2000,50);obs_properties_add_int_slider(p,"background_opacity",obs_module_text("List.BackgroundOpacity"),0,100,1);
		fonts::addProperties(p,obs_module_text("Chat.NameFont"),obs_module_text("Chat.MessageFont"),obs_module_text("Bar.FontInherit"),obs_module_text("Font.Help"));
		obs_properties_add_color(p,"name_color",obs_module_text("Chat.NameColor"));obs_properties_add_bool(p,"use_style_name_color",obs_module_text("Shoutout.StyleColor"));obs_properties_add_color(p,"message_color",obs_module_text("Chat.MessageColor"));obs_properties_add_bool(p,"use_style_message_color",obs_module_text("Shoutout.StyleColor"));
		obs_properties_add_bool(p,"twitch_colors",obs_module_text("Chat.TwitchColors"));obs_properties_add_bool(p,"show_commands",obs_module_text("Chat.Commands"));obs_properties_add_bool(p,"third_party",obs_module_text("Chat.ThirdParty"));obs_properties_add_bool(p,"animated_emotes",obs_module_text("Chat.AnimatedEmotes"));return p;
	};obs_register_source(&info);blog(LOG_INFO,"[obs-twitch-overlays] Live Chat source registered (shared presets, vertical/horizontal)");
}
}
