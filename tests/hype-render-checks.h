#include "../shared/twitch-hype-api.h"
#include "hype-anchor-checks.h"
struct hype_reader{int index;char values[5][64];};
static void read_hype_child(obs_source_t *parent,obs_source_t *child,void *context){UNUSED_PARAMETER(parent);struct hype_reader *r=context;int i=r->index++;if(i<9||i>=14)return;obs_data_t *data=obs_source_get_settings(child);snprintf(r->values[i-9],64,"%s",obs_data_get_string(data,"text"));obs_data_release(data);}
static bool transparent_region(const uint8_t *pixels,unsigned width,unsigned x,unsigned y,unsigned rw,unsigned rh)
{for(unsigned row=y;row<y+rh;row++)for(unsigned col=x;col<x+rw;col++)if(pixels[((size_t)row*width+col)*4+3])return false;return true;}
static void hype_checks(void)
{
 hype_anchor_checks();
 obs_data_t *settings=obs_data_create();obs_data_set_int(settings,"style",STYLE_NEWS);obs_data_set_int(settings,"width",1920);obs_data_set_int(settings,"upper_height",42);obs_data_set_int(settings,"lower_height",31);
 obs_data_set_bool(settings,"clock_enabled",false);obs_data_set_bool(settings,"map_enabled",false);obs_data_set_string(settings,"header_text","LIVE");obs_data_set_string(settings,"upper_text","Example artist — Example song");obs_data_set_string(settings,"lower_text","LAST FOLLOW — ExampleViewer");obs_data_set_string(settings,"weather_text","");
 obs_source_t *source=obs_source_create_private("obs_news_ticker_nexus","Hype layout QA",settings);obs_source_inc_showing(source);settle();uint8_t *base=capture(source);
 obs_data_set_bool(settings,"hype_enabled",true);obs_source_update(source,settings);settle();uint8_t *idle=capture(source);
 check("Hype reserves fixed transparent space without moving existing band pixels",obs_source_get_height(source)==96&&transparent_region(idle,1920,0,0,1920,28)&&!memcmp(base,idle+1920*23*4,1920*73*4));free(base);free(idle);
 obs_data_set_bool(settings,"hype_preview",true);obs_data_set_bool(settings,"hype_blink",false);obs_source_update(source,settings);settle();os_sleep_ms(400);uint8_t *first=capture(source);
 check("Hype preview is short and the unused top right remains transparent",!transparent_region(first,1920,0,0,1248,28)&&transparent_region(first,1920,1248,0,672,28));free(first);
 obs_data_set_int(settings,"hype_width",900);obs_data_set_int(settings,"hype_height",40);obs_source_update(source,settings);settle();first=capture(source);
 check("independent Hype width and height apply without changing ticker width",obs_source_get_width(source)==1920&&obs_source_get_height(source)==108&&!transparent_region(first,1920,0,0,900,40)&&transparent_region(first,1920,900,0,1020,40));free(first);
 obs_data_set_int(settings,"hype_width",0);obs_data_set_int(settings,"hype_height",28);obs_data_set_bool(settings,"hype_blink",true);obs_data_set_int(settings,"hype_blink_ms",1000);obs_source_update(source,settings);settle();first=capture(source);os_sleep_ms(500);uint8_t *next=capture(source);
 check("Hype message blinks while breaking badge and level stay fixed",!region_equal(first,next,1920,340,2,400,24)&&region_equal(first,next,1920,10,2,300,24)&&region_equal(first,next,1920,760,2,170,24));free(first);free(next);
 obs_data_t *saved=obs_save_source(source),*stored=obs_data_get_obj(saved,"settings");check("Hype settings persist independently",obs_data_get_bool(stored,"hype_enabled")&&obs_data_get_bool(stored,"hype_blink")&&obs_data_get_int(stored,"hype_height")==28);obs_data_release(stored);obs_data_release(saved);
 FILE *manifest=fopen("validation/hype-styles.tsv","w");
 for(int style=0;style<=STYLE_DARK;style++){
  obs_data_set_int(settings,"style",style);obs_data_set_bool(settings,"hype_blink",false);obs_source_update(source,settings);settle();first=capture(source);unsigned height=obs_source_get_height(source);
  bool premultiplied=true;for(size_t i=0;i<(size_t)1920*height;i++)if(first[i*4]>first[i*4+3]||first[i*4+1]>first[i*4+3]||first[i*4+2]>first[i*4+3])premultiplied=false;
  char label[100];snprintf(label,sizeof(label),"Hype renders inside its short region with premultiplied alpha, style %d",style);check(label,premultiplied&&!transparent_region(first,1920,0,0,1248,28)&&transparent_region(first,1920,1248,0,672,28));
  char path[160];snprintf(path,sizeof(path),"validation/hype-style-%02d.rgba",style);FILE *out=fopen(path,"wb");if(out){fwrite(first,1,(size_t)1920*height*4,out);fclose(out);}if(manifest)fprintf(manifest,"%d\t1920\t%u\n",style,height);free(first);
 }
 if(manifest)fclose(manifest);
 obs_data_set_bool(settings,"hype_preview",false);obs_source_update(source,settings);settle();os_sleep_ms(400);first=capture(source);check("leaving preview fades away without a real live train",transparent_region(first,1920,0,0,1920,28));free(first);
 wchar_t dll[32768];GetFullPathNameW(L"build/twitch-fixture/nexus-twitch-core.dll",32768,dll,NULL);HMODULE core=LoadLibraryW(dll);
 GetFullPathNameW(L"build/twitch-fixture/obs-frontend-api.dll",32768,dll,NULL);HMODULE frontend=LoadLibraryW(dll);
 void (*post_load)(void)=(void *)(uintptr_t)GetProcAddress(GetModuleHandleW(L"obs-news-ticker-nexus.dll"),"obs_module_post_load");
 void (*frontend_emit)(enum obs_frontend_event)=(void *)(uintptr_t)GetProcAddress(frontend,"fixture_frontend_emit");
 void (*emit_hype)(const NexusHypeSnapshot *)=(void *)(uintptr_t)GetProcAddress(core,"fixture_emit_hype");
 check("controlled Hype lifecycle loads",post_load&&frontend_emit&&emit_hype);
 if(post_load&&frontend_emit&&emit_hype){
  post_load();frontend_emit(OBS_FRONTEND_EVENT_FINISHED_LOADING);
  NexusHypeSnapshot live={0};live.size=sizeof(live);live.version=1;live.active=live.authorized=live.present=1;live.instance=1;live.level=3;live.progress=3350;live.goal=5000;
  strcpy(live.account,"fixture-channel");strcpy(live.train_id,"live-train");live.started_ms=(int64_t)((np_now()-10)*1000);live.expires_ms=(int64_t)((np_now()+134)*1000);
  emit_hype(&live);settle();os_sleep_ms(400);first=capture(source);struct hype_reader values={0};obs_source_enum_active_sources(source,read_hype_child,&values);
  check("live Hype feed reaches native level percentage and countdown",!strcmp(values.values[2],"LEVEL 3")&&!strcmp(values.values[3],"67%")&&strstr(values.values[4],"02:")&&!transparent_region(first,1920,0,0,1248,28));free(first);
  live.level=4;live.progress=800;live.goal=1000;emit_hype(&live);settle();memset(&values,0,sizeof(values));obs_source_enum_active_sources(source,read_hype_child,&values);
  check("live Hype level and progress updates reach the alert",!strcmp(values.values[2],"LEVEL 4")&&!strcmp(values.values[3],"80%"));
  live.expires_ms=(int64_t)((np_now()-1)*1000);emit_hype(&live);settle();os_sleep_ms(400);first=capture(source);check("live Hype expiry removes alert without waiting for end",transparent_region(first,1920,0,0,1920,28));free(first);
  live.expires_ms=(int64_t)((np_now()+120)*1000);emit_hype(&live);settle();os_sleep_ms(400);live.present=0;emit_hype(&live);settle();os_sleep_ms(400);first=capture(source);check("live Hype end fades away and preserves reserved source height",transparent_region(first,1920,0,0,1920,28)&&obs_source_get_height(source)==100);free(first);
  void (*emit_preview)(const NexusHypeSnapshot *)=(void *)(uintptr_t)GetProcAddress(core,"fixture_emit_hype_preview");
  check("Overlays TEST route is available",emit_preview!=NULL);
  if(emit_preview){
   obs_data_set_bool(settings,"hype_preview",true);obs_source_update(source,settings);settle();
   live.present=1;live.authorized=0;live.level=2;live.progress=750;live.goal=1000;strcpy(live.account,"panel-test-1");strcpy(live.train_id,"panel-test-1");live.expires_ms=(int64_t)((np_now()+90)*1000);emit_preview(&live);settle();os_sleep_ms(300);
   memset(&values,0,sizeof(values));obs_source_enum_active_sources(source,read_hype_child,&values);check("Overlays TEST reaches the ticker as labelled simulated data",strstr(values.values[1],"TEST")&&!strcmp(values.values[2],"LEVEL 2")&&!strcmp(values.values[3],"75%"));
   obs_data_set_bool(settings,"hype_preview",false);obs_source_update(source,settings);settle();
   live.present=0;emit_preview(&live);settle();os_sleep_ms(400);first=capture(source);check("ending Overlays TEST removes the simulated alert",transparent_region(first,1920,0,0,1920,28));free(first);
  }

 }
 obs_source_dec_showing(source);obs_source_release(source);obs_data_release(settings);
}
