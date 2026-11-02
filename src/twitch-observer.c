/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "twitch-observer.h"
#include <windows.h>
#include <string.h>
static SRWLOCK lock=SRWLOCK_INIT;
static struct twitch_snapshot state;
static const NexusTwitchApi *provider;
static uint64_t lease;
static char seen[32][256];
static unsigned seen_next;
static uint64_t instance;
static const NexusTickerApi *feed_api;
static NexusTickerSnapshot feed;
static NexusHypeSnapshot hype,preview_hype;
static uint64_t provider_instance;
static bool terminated(const char *text,size_t size)
{ return memchr(text,0,size)!=NULL; }
static bool identifier(const char *text,size_t size)
{
    if(!terminated(text,size)||!text[0])return false;
    for(const unsigned char *p=(const unsigned char *)text;*p;p++)
        if(*p<32||*p==127)return false;
    return true;
}
static bool login_name(const char *text,size_t size)
{
    if(!identifier(text,size))return false;
    for(const char *p=text;*p;p++)
        if(!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||
             (*p>='0'&&*p<='9')||*p=='_'))return false;
    return true;
}
static void reset(enum twitch_status status)
{
    memset(&state,0,sizeof(state));state.status=status;
    state.feed_version=feed_api?NEXUS_TICKER_VERSION:0;
    memset(&feed,0,sizeof(feed));
    memset(&hype,0,sizeof(hype));
    memset(&preview_hype,0,sizeof(preview_hype));
    memset(seen,0,sizeof(seen));seen_next=0;instance=0;
}
static void receive(void *unused,const NexusTwitchMessage *message)
{
    (void)unused;
    if(!message||message->size<sizeof(*message))return;
    AcquireSRWLockExclusive(&lock);
    if(message->kind==NEXUS_CLOSING){
        provider=NULL;lease=0;feed_api=NULL;provider_instance=0;reset(TWITCH_CLOSED);
    }else if(message->kind==NEXUS_STATE){
        provider_instance=message->instance;
        if(!message->active||!message->authorized||
           !identifier(message->account,sizeof(message->account)))reset(TWITCH_WAITING);
        else {
            if(strcmp(state.account,message->account)||instance!=message->instance)reset(TWITCH_READY);
            instance=message->instance;
            state.status=TWITCH_READY;
            memcpy(state.account,message->account,sizeof(state.account));
        }
    }else if(message->kind==NEXUS_RAID&&state.status==TWITCH_READY&&
             message->instance==instance&&
             identifier(message->account,sizeof(message->account))&&
             !strcmp(state.account,message->account)&&
             identifier(message->event_id,sizeof(message->event_id))&&
             login_name(message->actor,sizeof(message->actor))){
        bool duplicate=false;
        for(unsigned i=0;i<32;i++)if(!strcmp(seen[i],message->event_id))duplicate=true;
        if(!duplicate){
            memcpy(seen[seen_next++%32],message->event_id,sizeof(message->event_id));
            memcpy(state.event_id,message->event_id,sizeof(state.event_id));
            memcpy(state.last_raid,message->actor,sizeof(state.last_raid));
        }
    }
    ReleaseSRWLockExclusive(&lock);
}
void twitch_observer_read(struct twitch_snapshot *out)
{ AcquireSRWLockShared(&lock);*out=state;ReleaseSRWLockShared(&lock); }
void twitch_observer_read_feed(NexusTickerSnapshot *out)
{AcquireSRWLockShared(&lock);*out=feed;ReleaseSRWLockShared(&lock);}
static void receive_feed(void *unused,const NexusTickerSnapshot *message)
{
    (void)unused;
    if(!message||message->size<sizeof(*message)||message->version!=NEXUS_TICKER_VERSION)return;
    AcquireSRWLockExclusive(&lock);
    memset(&feed,0,sizeof(feed));
    if(message->kind==NEXUS_TICKER_UPDATE&&message->active&&message->authorized&&
       state.status==TWITCH_READY&&message->instance==instance&&
       identifier(message->account,sizeof(message->account))&&!strcmp(message->account,state.account)){
        bool valid=true;
        for(unsigned i=0;i<NEXUS_TICKER_COUNT;i++){
            const NexusTickerEvent *e=&message->latest[i];
            if(e->present&&(!identifier(e->id,sizeof(e->id))||
               !terminated(e->actor,sizeof(e->actor))||!terminated(e->actor_id,sizeof(e->actor_id))))valid=false;
        }
        if(valid)feed=*message;
    }
    ReleaseSRWLockExclusive(&lock);
}
bool twitch_observer_attach_feed(const NexusTickerApi *api)
{
    feed_api=NULL;
    if(!provider||!lease||!api||api->size<sizeof(*api)||api->version!=NEXUS_TICKER_VERSION||!api->bind||!api->snapshot)return false;
    if(!api->bind(lease,receive_feed,NULL))return false;
    feed_api=api;
    AcquireSRWLockExclusive(&lock);state.feed_version=NEXUS_TICKER_VERSION;ReleaseSRWLockExclusive(&lock);
    NexusTickerSnapshot initial={0};
    if(api->snapshot(lease,&initial,sizeof(initial)))receive_feed(NULL,&initial);
    return true;
}
bool twitch_observer_authorize(void)
{return provider&&lease&&feed_api&&feed_api->authorize&&feed_api->authorize(lease)!=0;}
bool twitch_observer_disconnect(void)
{return provider&&lease&&provider->disconnect&&provider->disconnect(lease)!=0;}
bool twitch_observer_authorization_url(char *out,uint32_t size)
{
    if(!out||!size)return false;out[0]=0;
    NexusTwitchMessage message={0};
    if(!provider||!lease||!provider->snapshot(lease,&message,sizeof(message))||
       !terminated(message.verification,sizeof(message.verification)))return false;
    const char *prefix="https://id.twitch.tv/oauth2/authorize?";
    if(strncmp(message.verification,prefix,strlen(prefix))||strlen(message.verification)>=size)return false;
    strcpy(out,message.verification);return true;
}
void twitch_observer_detach(void)
{
    const NexusTwitchApi *old=provider;uint64_t old_lease=lease;
    provider=NULL;lease=0;feed_api=NULL;
    if(old&&old_lease)old->release(old_lease);
    AcquireSRWLockExclusive(&lock);reset(TWITCH_MISSING);ReleaseSRWLockExclusive(&lock);
}
bool twitch_observer_attach(const NexusTwitchApi *api)
{
    twitch_observer_detach();
    if(!api||api->abi!=NEXUS_TWITCH_ABI||api->size<sizeof(*api)||
       !api->acquire||!api->release||!api->snapshot)return false;
    provider=api;
    AcquireSRWLockExclusive(&lock);reset(TWITCH_WAITING);ReleaseSRWLockExclusive(&lock);
    uint64_t acquired=api->acquire(receive,NULL);
    /* Closing may synchronously invalidate the provider during acquire. */
    if(!provider)return false;
    lease=acquired;
    if(!lease){twitch_observer_detach();return false;}
    NexusTwitchMessage initial={0};initial.size=sizeof(initial);
    if(api->snapshot(lease,&initial,sizeof(initial)))receive(NULL,&initial);
    return provider!=NULL&&lease!=0;
}

static void receive_hype(void *unused,const NexusHypeSnapshot *message)
{
 (void)unused;if(!message||message->size<sizeof(*message)||message->version!=NEXUS_HYPE_VERSION)return;
 AcquireSRWLockExclusive(&lock);memset(&hype,0,sizeof(hype));
 if(state.status==TWITCH_READY&&message->active&&message->authorized&&message->instance==instance&&identifier(message->account,sizeof(message->account))&&!strcmp(state.account,message->account)){
  if(!message->present||(identifier(message->train_id,sizeof(message->train_id))&&message->level&&message->goal&&message->started_ms>0&&message->expires_ms>message->started_ms))hype=*message;
 }
 ReleaseSRWLockExclusive(&lock);
}
void twitch_observer_read_hype(NexusHypeSnapshot *out){AcquireSRWLockShared(&lock);*out=hype;ReleaseSRWLockShared(&lock);}
bool twitch_observer_attach_hype(const NexusHypeApi *api){
 if(!provider||!lease||!api||api->size<sizeof(*api)||api->version!=NEXUS_HYPE_VERSION||!api->bind||!api->snapshot)return false;
 if(!api->bind(lease,receive_hype,NULL))return false;
 NexusHypeSnapshot initial={0};if(api->snapshot(lease,&initial,sizeof(initial)))receive_hype(NULL,&initial);return true;
}

static void receive_hype_preview(void *unused,const NexusHypeSnapshot *message)
{
 (void)unused;if(!message||message->size<sizeof(*message)||message->version!=NEXUS_HYPE_VERSION)return;
 AcquireSRWLockExclusive(&lock);memset(&preview_hype,0,sizeof(preview_hype));
 if(provider&&lease&&message->present&&message->active&&!message->authorized&&message->instance==provider_instance&&identifier(message->account,sizeof(message->account))&&!strncmp(message->account,"panel-test-",11)&&identifier(message->train_id,sizeof(message->train_id))&&message->level&&message->goal&&message->started_ms>0&&message->expires_ms>message->started_ms)preview_hype=*message;
 ReleaseSRWLockExclusive(&lock);
}
void twitch_observer_read_hype_mode(NexusHypeSnapshot *out,bool *preview){AcquireSRWLockShared(&lock);*preview=preview_hype.present!=0;*out=*preview?preview_hype:hype;ReleaseSRWLockShared(&lock);}
bool twitch_observer_attach_hype_preview(const NexusHypeApi *api){
 if(!provider||!lease||!api||api->size<sizeof(*api)||api->version!=NEXUS_HYPE_VERSION||!api->bind||!api->snapshot)return false;
 if(!api->bind(lease,receive_hype_preview,NULL))return false;NexusHypeSnapshot initial={0};if(api->snapshot(lease,&initial,sizeof(initial)))receive_hype_preview(NULL,&initial);return true;
}
