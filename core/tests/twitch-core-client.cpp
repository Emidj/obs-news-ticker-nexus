// SPDX-License-Identifier: GPL-2.0-or-later
#include "twitch-core-client.hpp"
namespace dst {
bool TwitchCoreClient::attach(const NexusTwitchApi *api){
 detach();if(!api||api->abi!=NEXUS_TWITCH_ABI||api->size<sizeof(NexusTwitchApi)||!api->acquire||!api->release||!api->snapshot)return false;
 api_=api;lease_=api_->acquire(receive,this);if(!lease_)api_=nullptr;return available();
}
void TwitchCoreClient::detach(){
 const auto *api=api_;auto lease=lease_;api_=nullptr;lease_=0;if(api&&lease)api->release(lease);
}
bool TwitchCoreClient::snapshot(NexusTwitchMessage &state)const{return available()&&api_->snapshot(lease_,&state,sizeof(state));}
void TwitchCoreClient::receive(void *context,const NexusTwitchMessage *message){
 if(!context||!message||message->size<sizeof(NexusTwitchMessage))return;
 auto &client=*static_cast<TwitchCoreClient*>(context);
 if(message->kind==NEXUS_CLOSING){client.api_=nullptr;client.lease_=0;}
 if(client.changed_)client.changed_(message->kind);
 if(client.message_)client.message_(*message);
}
}
