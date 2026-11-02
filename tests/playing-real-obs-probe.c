/* SPDX-License-Identifier: GPL-2.0-or-later -- test-only portable OBS probe */
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <windows.h>
#include <psapi.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <graphics/vec2.h>
#include "../src/now-playing.h"
#include "../src/ticker-style.h"
OBS_DECLARE_MODULE()
static obs_source_t *provider,*ticker;static HANDLE worker,stopper;static HWND window;static FILE *logfile;static int checks,failures;
static bool provider_active,ticker_showing;
static obs_scene_t *test_scene;static obs_sceneitem_t *ticker_item;
static obs_source_t *previous_output;
static LONG rendered_frames,iteration;
static unsigned soak_seconds;
static bool real_dj,open_properties,hype_preview_qa;
static char finish_path[32768];
static void render_count(void *unused,uint32_t width,uint32_t height){UNUSED_PARAMETER(unused);UNUSED_PARAMETER(width);UNUSED_PARAMETER(height);InterlockedIncrement(&rendered_frames);}
typedef void (*event_fn)(obs_frontend_event_cb,void*);static event_fn remove_event;
static void check(const char *name,bool ok){fprintf(logfile,"%s %s\n",ok?"PASS":"FAIL",name);fflush(logfile);checks++;failures+=!ok;}
struct text_read {int index;char upper[1024],lower[1024],weather[512],level[64],percent[64];};
static void child(obs_source_t *parent,obs_source_t *source,void *data){UNUSED_PARAMETER(parent);struct text_read *r=data;int i=r->index++;if(i!=1&&i!=2&&i!=7&&i!=11&&i!=12)return;obs_data_t *s=obs_source_get_settings(source);snprintf(i==1?r->upper:i==2?r->lower:i==7?r->weather:i==11?r->level:r->percent,i>=11?64:i==7?512:1024,"%s",obs_data_get_string(s,"text"));obs_data_release(s);}
static bool wait_track(const char *expected){
 for(int n=0;n<100;n++){
  if(WaitForSingleObject(stopper,50)==WAIT_OBJECT_0)return false;
  struct np_sample sample;np_read_playing(&sample);struct text_read text={0};obs_source_enum_active_sources(ticker,child,&text);
  bool data=*expected?sample.status==NP_PLAYING&&!strcmp(sample.text,expected):sample.status!=NP_PLAYING;
  if(data&&!strcmp(text.upper,expected)&&!strcmp(text.lower,"LOWER UNCHANGED"))return true;
 }
 return false;
}
static void change(void *action){
 if(!provider)return;
 intptr_t value=(intptr_t)action;
 if(value==1){obs_data_t *s=obs_source_get_settings(provider);obs_data_set_string(s,"demo_title","Track B");obs_source_update(provider,s);obs_data_release(s);}
 else if(value==6){obs_data_t *s=obs_source_get_settings(provider);obs_data_set_string(s,"demo_title","Track C hidden");obs_source_update(provider,s);obs_data_release(s);}
 else if(value==2){if(provider_active)obs_source_dec_active(provider);provider_active=false;}
 else if(value==3){if(!provider_active)obs_source_inc_active(provider);provider_active=true;}
 else if(value==4){if(provider_active)obs_source_dec_active(provider);provider_active=false;obs_source_remove(provider);obs_source_release(provider);provider=NULL;}
 else if(value==7||value==9||value==11){obs_data_t *s=obs_source_get_settings(ticker);obs_data_set_bool(s,"hype_enabled",true);obs_data_set_int(s,"hype_height",value==9?40:28);obs_source_update(ticker,s);obs_data_release(s);}
 else if(value==8||value==10){struct vec2 pos;obs_sceneitem_get_pos(ticker_item,&pos);check(value==8?"real OBS activation preserves the original ticker baseline":"real OBS Hype height adjustment preserves the ticker baseline",fabsf(pos.y-(value==8?677:665))<.5f);}
 else if(value==5){
  LONG n=InterlockedCompareExchange(&iteration,0,0);char title[64];snprintf(title,sizeof(title),"Soak %03ld",(long)n);
  obs_data_t *s=obs_source_get_settings(provider);obs_data_set_string(s,"demo_title",title);obs_source_update(provider,s);obs_data_release(s);
  int style=1+(n%16);s=obs_source_get_settings(ticker);obs_data_set_int(s,"style",style);obs_data_set_int(s,"motion",n%4);
  obs_data_set_int(s,"upper_height",compact_style(style)?36:42);obs_data_set_int(s,"lower_height",31);obs_source_update(ticker,s);obs_data_release(s);
 }
}
static void release_sources(void){
 if(test_scene){obs_remove_main_render_callback(render_count,NULL);obs_set_output_source(0,previous_output);obs_source_release(previous_output);previous_output=NULL;if(hype_preview_qa)obs_source_remove(obs_scene_get_source(test_scene));obs_scene_release(test_scene);test_scene=NULL;ticker_item=NULL;}
 if(provider){if(provider_active)obs_source_dec_active(provider);provider_active=false;obs_source_remove(provider);obs_source_release(provider);provider=NULL;}
 if(ticker){if(ticker_showing)obs_source_dec_showing(ticker);ticker_showing=false;if(hype_preview_qa)obs_source_remove(ticker);obs_source_release(ticker);ticker=NULL;}
}
static void cleanup(void *unused){
 UNUSED_PARAMETER(unused);SetEvent(stopper);if(worker)WaitForSingleObject(worker,2000);
 release_sources();
 if(window)PostMessageW(window,WM_CLOSE,0,0);
}
static DWORD WINAPI run_live(void *unused){
 UNUSED_PARAMETER(unused);
 char last_provider[1024]="",last_upper[1024]="";enum np_status last_status=NP_DISABLED;
 unsigned songs=0,settled=0;bool cleared=false,news_ok=true;ULONGLONG changed=GetTickCount64(),started=changed;
 fprintf(logfile,"LIVE_DJ real VirtualDJ automatic provider; demo disabled; no broadcast\n");fflush(logfile);
 while(GetTickCount64()-started<600000&&WaitForSingleObject(stopper,100)!=WAIT_OBJECT_0){
  if(*finish_path&&GetFileAttributesA(finish_path)!=INVALID_FILE_ATTRIBUTES)break;
  struct np_sample sample;np_read_playing(&sample);struct text_read text={0};obs_source_enum_active_sources(ticker,child,&text);
  if(sample.status!=last_status||strcmp(last_provider,sample.text)){
   if(sample.status==NP_PLAYING&&strcmp(last_provider,sample.text))songs++;
   changed=GetTickCount64();last_status=sample.status;snprintf(last_provider,sizeof(last_provider),"%s",sample.text);
   fprintf(logfile,"PROVIDER utc=%.3f status=%d text=%s\n",np_now(),sample.status,sample.text);fflush(logfile);
  }
  if(strcmp(last_upper,text.upper)){snprintf(last_upper,sizeof(last_upper),"%s",text.upper);fprintf(logfile,"TICKER utc=%.3f text=%s\n",np_now(),text.upper);fflush(logfile);}
  if(strcmp(text.lower,"LOWER UNCHANGED"))news_ok=false;
  if(GetTickCount64()-changed>2000){
   if(sample.status==NP_PLAYING&&*sample.text&&!strcmp(text.upper,sample.text))settled++;
   if(songs&&sample.status!=NP_PLAYING&&!*text.upper)cleared=true;
  }
 }
 check("real DJ supplies at least two track publications",songs>=2);
 check("ticker receives settled real DJ text",settled>0);
 check("real DJ pause or stop clears upper text",cleared);
 check("live DJ changes preserve lower news",news_ok);
 check("real DJ ticker renders frames",InterlockedCompareExchange(&rendered_frames,0,0)>30);
 fprintf(logfile,"LIVE_SUMMARY elapsed_ms=%llu songs=%u settled_samples=%u frames=%ld\n",GetTickCount64()-started,songs,settled,(long)InterlockedCompareExchange(&rendered_frames,0,0));
 fprintf(logfile,"RESULT checks=%d failures=%d\n",checks,failures);fflush(logfile);obs_queue_task(OBS_TASK_UI,cleanup,NULL,false);return 0;
}
static DWORD WINAPI run(void *unused){
 UNUSED_PARAMETER(unused);
 check("real OBS Playing public procedure reaches the real ticker",wait_track("Bridge QA — Track A"));
 if(hype_preview_qa){obs_queue_task(OBS_TASK_UI,change,(void*)(intptr_t)7,true);Sleep(500);obs_queue_task(OBS_TASK_UI,change,(void*)(intptr_t)8,true);obs_queue_task(OBS_TASK_UI,change,(void*)(intptr_t)9,true);Sleep(400);obs_queue_task(OBS_TASK_UI,change,(void*)(intptr_t)10,true);obs_queue_task(OBS_TASK_UI,change,(void*)(intptr_t)11,true);Sleep(400);struct text_read text={0};obs_source_enum_active_sources(ticker,child,&text);check("real OBS renders configured Hype demo text",!strcmp(text.level,"LEVEL 3")&&!strcmp(text.percent,"67%"));}
 obs_queue_task(OBS_TASK_UI,change,(void*)(intptr_t)1,false);
 check("real Playing track update reaches ticker with lower news preserved",wait_track("Bridge QA — Track B"));
 obs_queue_task(OBS_TASK_UI,change,(void*)(intptr_t)2,false);
 Sleep(4000);
 check("hidden real Playing source keeps publishing beyond demand timeout",wait_track("Bridge QA — Track B"));
 obs_queue_task(OBS_TASK_UI,change,(void*)(intptr_t)6,false);
 check("hidden real Playing publishes new songs without activating its OBS source",wait_track("Bridge QA — Track C hidden")&&!obs_source_active(provider));
 obs_queue_task(OBS_TASK_UI,change,(void*)(intptr_t)1,false);
 obs_queue_task(OBS_TASK_UI,change,(void*)(intptr_t)3,false);
 check("real source reactivation restores current track",wait_track("Bridge QA — Track B"));
 if(soak_seconds){
  ULONGLONG started=GetTickCount64();unsigned cycles=0;
  while(GetTickCount64()-started<(ULONGLONG)soak_seconds*1000&&WaitForSingleObject(stopper,0)!=WAIT_OBJECT_0){
   InterlockedExchange(&iteration,(LONG)cycles);obs_queue_task(OBS_TASK_UI,change,(void*)(intptr_t)5,false);
   char expected[128],label[128];snprintf(expected,sizeof(expected),"Bridge QA — Soak %03u",cycles);snprintf(label,sizeof(label),"soak track %03u style %u motion %u",cycles,1+cycles%16,cycles%4);
   check(label,wait_track(expected));
   PROCESS_MEMORY_COUNTERS_EX memory={0};memory.cb=sizeof(memory);
   if(GetProcessMemoryInfo(GetCurrentProcess(),(PROCESS_MEMORY_COUNTERS*)&memory,sizeof(memory)))fprintf(logfile,"SAMPLE elapsed_ms=%llu frames=%ld private_bytes=%llu working_set=%llu\n",GetTickCount64()-started,(long)InterlockedCompareExchange(&rendered_frames,0,0),(unsigned long long)memory.PrivateUsage,(unsigned long long)memory.WorkingSetSize);
   fflush(logfile);cycles++;if(WaitForSingleObject(stopper,4500)==WAIT_OBJECT_0)break;
  }
  check("soak completed its requested duration",GetTickCount64()-started>=(ULONGLONG)soak_seconds*1000);
  check("all sixteen styles exercised",cycles>=16);
  check("portable OBS rendered continuously",InterlockedCompareExchange(&rendered_frames,0,0)>(LONG)soak_seconds*10);
  struct text_read text={0};obs_source_enum_active_sources(ticker,child,&text);fprintf(logfile,"WEATHER %s\n",text.weather);
  check("live weather remains available after style and song changes",strstr(text.weather,"°C")!=NULL);
 }
 obs_queue_task(OBS_TASK_UI,change,(void*)(intptr_t)4,false);
 check("removed real Playing source clears ticker without a stale title",wait_track(""));
 fprintf(logfile,"RESULT checks=%d failures=%d\n",checks,failures);fflush(logfile);obs_queue_task(OBS_TASK_UI,cleanup,NULL,false);return 0;
}
static void frontend(enum obs_frontend_event event,void *unused){
 UNUSED_PARAMETER(unused);
 if(event==OBS_FRONTEND_EVENT_FINISHED_LOADING){
  obs_data_t *s=obs_data_create();obs_data_set_bool(s,"demo_enabled",true);obs_data_set_string(s,"demo_artist","Bridge QA");obs_data_set_string(s,"demo_title","Track A");obs_data_set_bool(s,"twitch_profile_auto",false);obs_data_set_bool(s,"chat_enabled",false);obs_data_set_bool(s,"artwork_enabled",false);obs_data_set_int(s,"normal_preset",0);obs_data_set_int(s,"text_layout",1);
  if(real_dj){obs_data_set_bool(s,"demo_enabled",false);obs_data_set_string(s,"provider","virtualdj");obs_data_set_int(s,"mode",0);obs_data_set_bool(s,"show_existing",true);obs_data_set_int(s,"hide_seconds",0);}
  provider=obs_source_create("obs_playing_next_development","Bridge QA temporary Playing",s,NULL);obs_data_release(s);check("real Playing module creates its source",provider!=NULL);
  s=obs_data_create();obs_data_set_int(s,"upper_mode",2);obs_data_set_string(s,"lower_text","LOWER UNCHANGED");obs_data_set_int(s,"motion",0);obs_data_set_bool(s,"clock_enabled",soak_seconds>0);
  if(soak_seconds||hype_preview_qa){obs_data_set_int(s,"style",STYLE_NEWS);obs_data_set_int(s,"width",1370);obs_data_set_int(s,"weather_mode",1);obs_data_set_string(s,"weather_city","Madrid, Spain");}
  if(open_properties){
   obs_data_array_t *list=obs_data_array_create();const char *cities[]={"Madrid (ES)","Londres (GB)"};
   for(int i=0;i<2;i++){obs_data_t *entry=obs_data_create();obs_data_set_string(entry,"value",cities[i]);obs_data_array_push_back(list,entry);obs_data_release(entry);}obs_data_set_array(s,"weather_locations",list);obs_data_array_release(list);
   obs_data_set_bool(s,"weather_rotate",true);obs_data_set_int(s,"weather_interval",5);obs_data_set_bool(s,"info_align_right",true);
  }
  if(real_dj){obs_data_set_int(s,"style",STYLE_NEWS);obs_data_set_int(s,"width",1370);obs_data_set_int(s,"upper_height",42);obs_data_set_int(s,"lower_height",31);}
  if(hype_preview_qa){obs_data_set_bool(s,"hype_enabled",false);obs_data_set_bool(s,"hype_preview",true);obs_data_set_int(s,"hype_width",900);obs_data_set_int(s,"hype_height",28);}
  ticker=hype_preview_qa?obs_source_create("obs_news_ticker_nexus","Bridge QA temporary ticker",s,NULL):obs_source_create_private("obs_news_ticker_nexus","Bridge QA temporary ticker",s);obs_data_release(s);check("real ticker module creates automatic receiver",ticker!=NULL);
  if(provider&&ticker){
   if(soak_seconds||real_dj||hype_preview_qa){previous_output=obs_get_output_source(0);test_scene=hype_preview_qa?obs_scene_create("Ticker temporary soak"):obs_scene_create_private("Ticker temporary soak");ticker_item=obs_scene_add(test_scene,ticker);if(hype_preview_qa){struct vec2 pos;vec2_set(&pos,20,700);obs_sceneitem_set_pos(ticker_item,&pos);}obs_set_output_source(0,obs_scene_get_source(test_scene));obs_add_main_render_callback(render_count,NULL);}
   obs_source_inc_active(provider);provider_active=true;obs_source_inc_showing(ticker);ticker_showing=true;worker=CreateThread(NULL,0,real_dj?run_live:run,NULL,0,NULL);
   if(open_properties){typedef void (*open_fn)(obs_source_t*);open_fn show=(open_fn)(void*)GetProcAddress(GetModuleHandleW(L"obs-frontend-api.dll"),"obs_frontend_open_source_properties");if(show)show(ticker);}

  }else cleanup(NULL);
 }else if(event==OBS_FRONTEND_EVENT_EXIT){SetEvent(stopper);if(worker)WaitForSingleObject(worker,2000);release_sources();}
}
bool obs_module_load(void){
 char armed[8],path[32768];if(!GetEnvironmentVariableA("NEXUS_TICKER_BRIDGE_SELFTEST",armed,sizeof(armed))||strcmp(armed,"1"))return false;
 if(!GetEnvironmentVariableA("NEXUS_TICKER_BRIDGE_LOG",path,sizeof(path)))return false;
 logfile=fopen(path,"wb");if(!logfile)return false;
 char seconds[16];if(GetEnvironmentVariableA("NEXUS_TICKER_SOAK_SECONDS",seconds,sizeof(seconds))){unsigned n=0;if(sscanf(seconds,"%u",&n)==1&&n>=90&&n<=1800)soak_seconds=n;}
 char hype_flag[8];hype_preview_qa=GetEnvironmentVariableA("NEXUS_TICKER_HYPE_PREVIEW_QA",hype_flag,sizeof(hype_flag))&&!strcmp(hype_flag,"1");
 char live[8];real_dj=GetEnvironmentVariableA("NEXUS_TICKER_REAL_DJ",live,sizeof(live))&&!strcmp(live,"1");
 char show[8];open_properties=GetEnvironmentVariableA("NEXUS_TICKER_OPEN_PROPERTIES",show,sizeof(show))&&!strcmp(show,"1");
 if(real_dj)GetEnvironmentVariableA("NEXUS_TICKER_FINISH_PATH",finish_path,sizeof(finish_path));
 HMODULE api=GetModuleHandleW(L"obs-frontend-api.dll");event_fn add=(event_fn)(void*)GetProcAddress(api,"obs_frontend_add_event_callback");remove_event=(event_fn)(void*)GetProcAddress(api,"obs_frontend_remove_event_callback");
 typedef void *(*window_fn)(void);window_fn get=(window_fn)(void*)GetProcAddress(api,"obs_frontend_get_main_window_handle");window=get?(HWND)get():NULL;
 stopper=CreateEventW(NULL,TRUE,FALSE,NULL);if(!add||!remove_event||!stopper)return false;add(frontend,NULL);return true;
}
void obs_module_unload(void){
 if(remove_event)remove_event(frontend,NULL);if(stopper)SetEvent(stopper);if(worker){WaitForSingleObject(worker,2000);CloseHandle(worker);}if(stopper)CloseHandle(stopper);if(logfile)fclose(logfile);
}
