/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../src/twitch-observer.h"
#include <stdio.h>
#include <string.h>
static NexusTwitchCallback callback;
static void *context;
static int releases,checks,failures;
static bool closing,fail_acquire;
static char authorization_url[2048];
static void check(const char *name,bool ok)
{printf("%s %s\n",ok?"PASS":"FAIL",name);checks++;failures+=!ok;}
static NexusTwitchMessage message(uint32_t kind)
{
    NexusTwitchMessage m={0};m.size=sizeof(m);m.kind=kind;m.active=1;m.authorized=1;
    strcpy(m.account,"channel-a");strcpy(m.actor,"raider");strcpy(m.event_id,"event-1");return m;
}
static uint64_t acquire(NexusTwitchCallback cb,void *ctx)
{
    callback=cb;context=ctx;
    if(closing){NexusTwitchMessage m=message(NEXUS_CLOSING);cb(ctx,&m);}
    return fail_acquire?0:7;
}
static void release(uint64_t id){if(id==7)releases++;callback=NULL;}
static int snapshot(uint64_t id,NexusTwitchMessage *out,uint32_t size)
{if(id!=7||size<sizeof(*out))return 0;*out=message(NEXUS_STATE);strcpy(out->verification,authorization_url);return 1;}
static void send(NexusTwitchMessage m){callback(context,&m);}
static NexusHypeCallback hype_callback;static void *hype_context;
static NexusHypeSnapshot hype_message(void){NexusHypeSnapshot h={0};h.size=sizeof(h);h.version=1;h.active=h.authorized=h.present=1;h.level=3;h.goal=5000;h.progress=3350;h.started_ms=1000;h.expires_ms=135000;strcpy(h.account,"channel-a");strcpy(h.train_id,"train-1");return h;}
static int bind_hype(uint64_t id,NexusHypeCallback cb,void *ctx){if(id!=7)return 0;hype_callback=cb;hype_context=ctx;return 1;}
static int snapshot_hype(uint64_t id,NexusHypeSnapshot *out,uint32_t bytes){if(id!=7||bytes<sizeof(*out))return 0;*out=hype_message();return 1;}
static NexusHypeCallback preview_callback;static void *preview_context;
static int bind_preview(uint64_t id,NexusHypeCallback cb,void *ctx){if(id!=7)return 0;preview_callback=cb;preview_context=ctx;return 1;}
static int snapshot_preview(uint64_t id,NexusHypeSnapshot *out,uint32_t bytes){if(id!=7||bytes<sizeof(*out))return 0;memset(out,0,sizeof(*out));out->size=sizeof(*out);out->version=1;return 1;}
int main(void)
{
    NexusTwitchApi api={0};api.size=sizeof(api);api.abi=NEXUS_TWITCH_ABI;
    api.acquire=acquire;api.release=release;api.snapshot=snapshot;
    struct twitch_snapshot s;
    check("missing provider is optional",!twitch_observer_attach(NULL));
    api.abi=99;check("reject incompatible ABI",!twitch_observer_attach(&api));api.abi=NEXUS_TWITCH_ABI;
    api.size--;check("reject short API",!twitch_observer_attach(&api));api.size=sizeof(api);
    check("attach read-only provider with mutation functions absent",twitch_observer_attach(&api));
    twitch_observer_read(&s);check("snapshot establishes authorized channel",s.status==TWITCH_READY&&!strcmp(s.account,"channel-a"));
    NexusHypeApi hype_api={sizeof(hype_api),1,bind_hype,snapshot_hype};NexusHypeSnapshot h;
    check("optional Hype extension attaches",twitch_observer_attach_hype(&hype_api));twitch_observer_read_hype(&h);
    check("Hype snapshot copies validated progress and deadline",h.present&&h.level==3&&h.progress==3350&&h.expires_ms==135000);
    h=hype_message();strcpy(h.account,"other-channel");hype_callback(hype_context,&h);twitch_observer_read_hype(&h);check("foreign Hype cannot enter ticker",!h.present);
    h=hype_message();h.goal=0;hype_callback(hype_context,&h);twitch_observer_read_hype(&h);check("invalid Hype goal rejected",!h.present);
    h=hype_message();hype_callback(hype_context,&h);h.present=0;hype_callback(hype_context,&h);twitch_observer_read_hype(&h);check("Hype end clears its snapshot",!h.present);
    NexusHypeApi preview_api={sizeof(preview_api),1,bind_preview,snapshot_preview};bool is_preview=false;
    check("separate Overlays preview extension attaches",twitch_observer_attach_hype_preview(&preview_api));
    h=hype_message();hype_callback(hype_context,&h);
    h.authorized=0;strcpy(h.account,"panel-test-1");preview_callback(preview_context,&h);twitch_observer_read_hype_mode(&h,&is_preview);
    check("explicit panel TEST overrides live display without authorization",is_preview&&h.present&&!h.authorized);
    h.present=0;preview_callback(preview_context,&h);twitch_observer_read_hype_mode(&h,&is_preview);
    check("ending panel TEST restores the live Hype snapshot",!is_preview&&h.present&&h.authorized&&h.progress==3350);
    char url[2048];
    strcpy(authorization_url,"https://id.twitch.tv/oauth2/authorize?fixture=1");
    check("only explicit Twitch authorization URL is exposed",twitch_observer_authorization_url(url,sizeof(url))&&!strcmp(url,authorization_url));
    strcpy(authorization_url,"https://id.twitch.tv.evil.example/oauth2/authorize?fixture=1");
    check("lookalike authorization domain rejected",!twitch_observer_authorization_url(url,sizeof(url)));
    strcpy(authorization_url,"file:///C:/test.exe");
    check("non-HTTPS authorization target rejected",!twitch_observer_authorization_url(url,sizeof(url)));authorization_url[0]=0;
    check("read-only provider cannot authorize or disconnect",!twitch_observer_authorize()&&!twitch_observer_disconnect());
    NexusTwitchMessage m=message(NEXUS_RAID);send(m);twitch_observer_read(&s);
    check("raid copied from borrowed callback",!strcmp(s.last_raid,"raider"));
    strcpy(m.actor,"duplicate");send(m);twitch_observer_read(&s);check("duplicate event ignored",!strcmp(s.last_raid,"raider"));
    strcpy(m.account,"channel-b");strcpy(m.event_id,"event-2");send(m);twitch_observer_read(&s);
    check("foreign channel ignored",!strcmp(s.last_raid,"raider"));
    m=message(NEXUS_RAID);memset(m.actor,'x',sizeof(m.actor));send(m);twitch_observer_read(&s);
    check("unterminated actor ignored",!strcmp(s.last_raid,"raider"));
    m=message(NEXUS_RAID);strcpy(m.actor,"<b>html</b>");strcpy(m.event_id,"markup");send(m);twitch_observer_read(&s);
    check("reject markup in raid login",!strcmp(s.last_raid,"raider"));
    m=message(NEXUS_RAID);m.instance=99;send(m);twitch_observer_read(&s);
    check("ignore old or foreign provider instance",!strcmp(s.last_raid,"raider"));
    m=message(NEXUS_STATE);m.instance=2;send(m);twitch_observer_read(&s);
    check("provider restart clears raid on same account",!s.last_raid[0]);
    m=message(NEXUS_STATE);strcpy(m.account,"channel-b");send(m);twitch_observer_read(&s);
    check("account switch clears previous raid",!s.last_raid[0]);
    m=message(NEXUS_STATE);m.authorized=0;send(m);twitch_observer_read(&s);
    check("authorization loss clears account",s.status==TWITCH_WAITING&&!s.account[0]);
    send(message(NEXUS_RAID));twitch_observer_read(&s);check("ignore events while unauthorized",!s.last_raid[0]);
    send(message(NEXUS_STATE));send(message(NEXUS_RAID));send(message(NEXUS_CLOSING));
    twitch_observer_read(&s);check("closing invalidates lease and data",s.status==TWITCH_CLOSED&&!s.last_raid[0]);
    twitch_observer_detach();check("closing lease never released twice",releases==0);
    check("reattach",twitch_observer_attach(&api));twitch_observer_detach();twitch_observer_detach();
    check("detach releases once",releases==1);
    closing=true;check("synchronous closing during acquire",!twitch_observer_attach(&api));
    closing=false;fail_acquire=true;check("failed acquire",!twitch_observer_attach(&api));
    printf("RESULT checks=%d failures=%d\n",checks,failures);return failures?1:0;
}
