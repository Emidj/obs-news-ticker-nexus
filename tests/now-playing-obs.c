/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <obs.h>
#include <stdio.h>
#include <string.h>
#include "../src/now-playing.h"
static char payload[17000];static bool enabled=true;static int checks,failures;
static unsigned demands;
static void demand(void *unused,calldata_t *data){(void)unused;(void)data;demands++;}
static void check(const char *name,bool ok){printf("%s %s\n",ok?"PASS":"FAIL",name);checks++;failures+=!ok;}
static const char *name(void *unused){(void)unused;return "Playing fixture";}
static void snapshot(void *unused,calldata_t *data){(void)unused;calldata_set_string(data,"json",payload);}
static void *create(obs_data_t *settings,obs_source_t *source){(void)settings;if(enabled){proc_handler_add(obs_source_get_proc_handler(source),"void nexus_now_playing_v1(out string json)",snapshot,NULL);proc_handler_add(obs_source_get_proc_handler(source),"void nexus_now_playing_demand_v1()",demand,NULL);}return source;}
static void destroy(void *unused){(void)unused;}
static void remove_source(obs_source_t *source){obs_source_remove(source);obs_source_release(source);}
static void set(const char *state,double age){snprintf(payload,sizeof(payload),"{\"schema\":\"nexus.now-playing.v1\",\"state\":\"%s\",\"artist\":\"DJ\",\"title\":\"Tema\",\"updated_at\":%.3f}",state,np_now()-age);}
int main(void){
 if(!obs_startup("en-US",NULL,NULL))return 2;
 struct obs_source_info info={.id="obs_playing_next_development",.type=OBS_SOURCE_TYPE_INPUT,.get_name=name,.create=create,.destroy=destroy};obs_register_source(&info);
 struct np_sample sample;np_read_playing(&sample);check("absent source waits without inventing a track",sample.status==NP_WAITING);
 enabled=false;obs_source_t *source=obs_source_create(info.id,"Legacy provider",NULL,NULL);np_read_playing(&sample);check("missing public procedure is explicit",sample.status==NP_PROVIDER_UNSUPPORTED);remove_source(source);
 enabled=true;source=obs_source_create(info.id,"Provider",NULL,NULL);set("playing",0);np_read_playing(&sample);check("public source supplies owned artist/title",sample.status==NP_PLAYING&&!strcmp(sample.text,"DJ — Tema"));
 check("ticker renews read demand without activating hidden Playing",demands==1&&!obs_source_active(source));
 payload[0]=0;check("returned text survives provider buffer changes",!strcmp(sample.text,"DJ — Tema"));
 set("paused",0);np_read_playing(&sample);check("pause removes active text",sample.status==NP_PAUSED&&!sample.text[0]);
 set("stopped",0);np_read_playing(&sample);check("stop clears active text",sample.status==NP_STOPPED&&!sample.text[0]);
 strcpy(payload,"{}");np_read_playing(&sample);check("invalid response is rejected",sample.status==NP_INVALID);
 memset(payload,'x',sizeof(payload)-1);payload[sizeof(payload)-1]=0;np_read_playing(&sample);check("oversized response is rejected",sample.status==NP_INVALID);
 set("playing",30);np_read_playing(&sample);check("stale publisher timestamps expire",np_current_status(&sample,np_now(),15)==NP_STALE);
 set("playing",-30);np_read_playing(&sample);check("future timestamps are rejected",np_current_status(&sample,np_now(),15)==NP_FUTURE);
 obs_source_t *second=obs_source_create(info.id,"Second",NULL,NULL);set("playing",0);np_read_playing(&sample);check("multiple sources do not silently choose a song",sample.status==NP_PROVIDER_AMBIGUOUS&&!sample.text[0]);remove_source(second);
 np_read_playing(&sample);check("single remaining source resumes",sample.status==NP_PLAYING);remove_source(source);np_read_playing(&sample);check("source removal clears the track",sample.status==NP_WAITING&&!sample.text[0]);
 obs_shutdown();printf("RESULT checks=%d failures=%d\n",checks,failures);return failures?1:0;
}
