// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "twitch-core-api.h"
#include <functional>
namespace dst {
class TwitchCoreClient final {
public:
 ~TwitchCoreClient(){detach();}
 bool attach(const NexusTwitchApi *api);
 void detach();
 bool snapshot(NexusTwitchMessage &state)const;
 bool authorize(){return available()&&api_->authorize&&api_->authorize(lease_);}
 bool disconnect(){return available()&&api_->disconnect&&api_->disconnect(lease_);}
 bool channelInfo(NexusTwitchReply reply,void *context){return available()&&api_->channel_info&&api_->channel_info(lease_,reply,context);}
 bool searchCategories(const char *query,uint32_t bytes,NexusTwitchReply reply,void *context){return available()&&api_->search_categories&&api_->search_categories(lease_,query,bytes,reply,context);}
 bool updateChannel(const char *json,uint32_t bytes,NexusTwitchReply reply,void *context){return available()&&api_->update_channel&&api_->update_channel(lease_,json,bytes,reply,context);}
 bool updateTitle(const char *title,uint32_t bytes,NexusTwitchReply reply,void *context){return available()&&api_->update_title&&api_->update_title(lease_,title,bytes,reply,context);}
 bool guardedUpdate(const char *json,uint32_t bytes,NexusTwitchReply reply,void *context){return available()&&api_->guarded_update&&api_->guarded_update(lease_,json,bytes,reply,context);}
 bool available()const{return api_&&lease_;}
 void setChanged(std::function<void(uint32_t)> callback){changed_=std::move(callback);}
 void setMessage(std::function<void(const NexusTwitchMessage &)> callback){message_=std::move(callback);}
private:
 static void receive(void *context,const NexusTwitchMessage *message);
 const NexusTwitchApi *api_=nullptr;
 uint64_t lease_=0;
 std::function<void(uint32_t)> changed_;
 std::function<void(const NexusTwitchMessage &)> message_;
};
}
