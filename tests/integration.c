/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <obs.h>
#include <obs-frontend-api.h>
#include <sys/stat.h>
#include <util/platform.h>
#include <graphics/vec4.h>
#include <graphics/vec2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/block-animation.h"
#include "../src/ping-pong.h"
#include "../src/local-clock.h"
#include "../src/now-playing.h"
#include "../shared/twitch-core-api.h"
#include "../shared/twitch-ticker-api.h"
#include <windows.h>
#include <math.h>
#include "../src/ticker-style.h"
#include "../src/reference-profiles.h"

static int checks,failures;
static SRWLOCK playing_lock=SRWLOCK_INIT;
static char playing_json[2048];
static const char *playing_name(void *unused){UNUSED_PARAMETER(unused);return "Playing public bridge fixture";}
static void playing_snapshot(void *unused,calldata_t *args){UNUSED_PARAMETER(unused);AcquireSRWLockShared(&playing_lock);calldata_set_string(args,"json",playing_json);ReleaseSRWLockShared(&playing_lock);}
static void *playing_create(obs_data_t *settings,obs_source_t *source){UNUSED_PARAMETER(settings);proc_handler_add(obs_source_get_proc_handler(source),"void nexus_now_playing_v1(out string json)",playing_snapshot,NULL);return source;}
static void playing_destroy(void *unused){UNUSED_PARAMETER(unused);}
static void playing_state(const char *state,const char *title){AcquireSRWLockExclusive(&playing_lock);snprintf(playing_json,sizeof(playing_json),"{\"schema\":\"nexus.now-playing.v1\",\"state\":\"%s\",\"artist\":\"DJ Bridge\",\"title\":\"%s\",\"updated_at\":%.3f}",state,title,np_now());ReleaseSRWLockExclusive(&playing_lock);}
static volatile LONG twitch_property_updates;
static void count_twitch_properties(void *context,calldata_t *call)
{(void)context;(void)call;InterlockedIncrement(&twitch_property_updates);}
static LONG property_updates(void)
{return InterlockedCompareExchange(&twitch_property_updates,0,0);}
static void check(const char *label,bool ok)
{ printf("%s %s\n",ok?"PASS":"FAIL",label); checks++; failures+=!ok; }
static uint64_t capture_time;
static char capture_lower[512];
static char capture_clock[32];
static char capture_upper[1024];
struct compact_reader {int index,count;char text[12][256];};
static void read_compact_text(obs_source_t *parent,obs_source_t *child,void *context)
{
    UNUSED_PARAMETER(parent);struct compact_reader *r=context;if(r->index++<14||r->count>=12)return;
    obs_data_t *settings=obs_source_get_settings(child);snprintf(r->text[r->count++],256,"%s",obs_data_get_string(settings,"text"));obs_data_release(settings);
}
static bool lower_has(const char *value)
{return *value?strstr(capture_lower,value)!=NULL:!*capture_lower;}
struct lower_reader { int index;char *text;size_t size;char *clock;size_t clock_size;char *upper;size_t upper_size; };
static void read_child_text(obs_source_t *parent,obs_source_t *child,void *param)
{
    UNUSED_PARAMETER(parent);struct lower_reader *reader=param;
    int index=reader->index++;if(index!=1 && index!=2 && index!=6)return;
    obs_data_t *s=obs_source_get_settings(child);
    if(index==1)snprintf(reader->upper,reader->upper_size,"%s",obs_data_get_string(s,"text"));
    else if(index==2)snprintf(reader->text,reader->size,"%s",obs_data_get_string(s,"text"));
    else snprintf(reader->clock,reader->clock_size,"%s",obs_data_get_string(s,"text"));
    obs_data_release(s);
}
struct capture_request { obs_source_t *source; uint8_t *pixels; uint64_t time; char lower[512],clock[32],upper[1024]; };
static void capture_task(void *param)
{
    struct capture_request *request=param;
    obs_source_t *s=request->source;
    uint32_t w=obs_source_get_width(s),h=obs_source_get_height(s);
    uint8_t *out=malloc((size_t)w*h*4);
    obs_enter_graphics();
    gs_texrender_t *rt=gs_texrender_create(GS_RGBA,GS_ZS_NONE);
    gs_stagesurf_t *stage=gs_stagesurface_create(w,h,GS_RGBA);
    if(!gs_texrender_begin(rt,w,h)) exit(20);
    struct vec4 zero; vec4_zero(&zero); gs_clear(GS_CLEAR_COLOR,&zero,0,0);
    gs_ortho(0,(float)w,0,(float)h,-100,100);
    gs_blend_state_push(); gs_enable_depth_test(false); gs_set_cull_mode(GS_NEITHER);
    gs_blend_function(GS_BLEND_ONE,GS_BLEND_INVSRCALPHA);
    gs_set_linear_srgb(false); gs_enable_framebuffer_srgb(false);
    obs_source_video_render(s);
    gs_blend_state_pop(); gs_texrender_end(rt);
    gs_stage_texture(stage,gs_texrender_get_texture(rt));
    uint8_t *map; uint32_t stride;
    if(!gs_stagesurface_map(stage,&map,&stride)) exit(21);
    for(uint32_t y=0;y<h;y++) memcpy(out+(size_t)y*w*4,map+(size_t)y*stride,(size_t)w*4);
    gs_stagesurface_unmap(stage); gs_stagesurface_destroy(stage); gs_texrender_destroy(rt);
    obs_leave_graphics(); request->pixels=out; request->time=os_gettime_ns();
    struct lower_reader reader={.text=request->lower,.size=sizeof(request->lower),.clock=request->clock,.clock_size=sizeof(request->clock),.upper=request->upper,.upper_size=sizeof(request->upper)};
    obs_source_enum_active_sources(s,read_child_text,&reader);
}
static uint8_t *capture(obs_source_t *s)
{
    struct capture_request request={.source=s};
    obs_queue_task(OBS_TASK_GRAPHICS,capture_task,&request,true);
    capture_time=request.time;
    snprintf(capture_lower,sizeof(capture_lower),"%s",request.lower);
    snprintf(capture_clock,sizeof(capture_clock),"%s",request.clock);
    snprintf(capture_upper,sizeof(capture_upper),"%s",request.upper);
    return request.pixels;
}
static bool region_equal(const uint8_t *a,const uint8_t *b,int width,int x,int y,int w,int h)
{
    for(int row=y;row<y+h;row++) if(memcmp(a+((size_t)row*width+x)*4,b+((size_t)row*width+x)*4,(size_t)w*4)) return false;
    return true;
}
static int displacement(const uint8_t *a,const uint8_t *b)
{
    uint64_t best_error=UINT64_MAX; int best=0;
    for(int shift=0;shift<=160;shift++) {
        uint64_t error=0;
        for(int y=48;y<72;y++) for(int x=100;x<400;x++) error+=(uint64_t)abs(a[((size_t)y*640+x+shift)*4+3]-b[((size_t)y*640+x)*4+3]);
        if(error<best_error) {best_error=error;best=shift;}
    }
    return best;
}
static void barrier(void *unused) { UNUSED_PARAMETER(unused); }
static void settle(void)
{
    for(int i=0;i<3;i++) {
        os_sleep_ms(40);
        obs_queue_task(OBS_TASK_GRAPHICS,barrier,NULL,true);
    }
}
static bool load(const char *dll,const char *data)
{
    obs_module_t *m=NULL;
    return obs_open_module(&m,dll,data)==MODULE_SUCCESS && obs_init_module(m);
}
static void apply(obs_source_t *s,obs_data_t *data)
{ obs_source_update(s,data); settle(); }
static bool write_now_fixture(const char *path,const char *state,const char *artist,const char *title,double stamp,const char *schema)
{
    wchar_t target[1024],temporary[1030];
    if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,target,1024))return false;
    swprintf(temporary,1030,L"%ls.tmp",target);
    obs_data_t *json=obs_data_create();
    obs_data_set_string(json,"schema",schema?schema:"nexus.now-playing.v1");obs_data_set_string(json,"state",state);
    obs_data_set_double(json,"updated_at",stamp);obs_data_set_string(json,"artist",artist);obs_data_set_string(json,"title",title);
    const char *text=obs_data_get_json(json);DWORD size=(DWORD)strlen(text),written=0;
    HANDLE file=CreateFileW(temporary,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    bool ok=file!=INVALID_HANDLE_VALUE && WriteFile(file,text,size,&written,NULL) && written==size;
    if(file!=INVALID_HANDLE_VALUE){FlushFileBuffers(file);CloseHandle(file);}
    obs_data_release(json);
    return ok && MoveFileExW(temporary,target,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
}
static bool wait_now(obs_source_t *s,const char *status_word,const char *text)
{
    uint64_t started=os_gettime_ns();
    do {
        uint8_t *pixels=capture(s);free(pixels);
        obs_properties_t *p=obs_source_properties(s);
        const char *status=obs_property_description(obs_properties_get(p,"now_status"));
        bool matched=strstr(status,status_word) && !strcmp(capture_upper,text);
        obs_properties_destroy(p);if(matched){settle();return true;}os_sleep_ms(50);
    }while(os_gettime_ns()-started<4000000000ULL);
    return false;
}
static void reference_clock(obs_source_t *parent,obs_source_t *child,void *context)
{
    UNUSED_PARAMETER(parent);int *index=context;if((*index)++!=6)return;
    obs_data_t *s=obs_source_get_settings(child);obs_data_set_string(s,"text","15:04");obs_source_update(child,s);obs_data_release(s);
}
struct text_metrics {int index;FILE *file;const char *style;};
static void record_text_metrics(obs_source_t *parent,obs_source_t *child,void *context)
{
    UNUSED_PARAMETER(parent);struct text_metrics *m=context;int i=m->index++;if(i>=9)return;
    obs_data_t *s=obs_source_get_settings(child),*font=obs_data_get_obj(s,"font");
    fprintf(m->file,"%s\t%d\t%u\t%u\t%lld\t%s\n",m->style,i,obs_source_get_width(child),obs_source_get_height(child),obs_data_get_int(font,"size"),obs_data_get_string(s,"text"));
    obs_data_release(font);obs_data_release(s);
}
static void catalog(obs_source_t *source)
{
    const char *names[]={"basic","news","light","world","breaking","business","sports","clean","map","red","ultra","prime","glass","neon","sport","magazine","dark"};
    const char *labels[]={"","LATEST","","STREAM NEWS","BREAKING","STREAM STATS","LIVE","","","","","","","","","",""};
    const char *songs[]={"","Armin van Buuren — Blah Blah Blah","Boris Brejcha — Gravity","Charlotte de Witte — Overdrive","Anyma — Eternity","Fisher — Losing It","Daft Punk — Around the World","CamelPhat — Cola","Solomun — Home","Eric Prydz — Opus","Anyma — Eternity","Armin van Buuren — Blah Blah Blah","Charlotte de Witte — Overdrive","Daft Punk — Around the World","CamelPhat — Cola","Boris Brejcha — Gravity","ARTBAT — Horizon"};
    const char *weather[]={"","23° Madrid","20° Viena","21° Berlín","19° Tokio","26° Seúl","24° París","22° Roma","18° Ámsterdam","23° Atenas","23° Madrid","23° Madrid","21° Berlín","24° París","22° Roma","20° Viena","23° Madrid"};
    const char *news[][4]={
        {"","","",""},
        {"LAST FOLLOW — DJMUSIC123","LAST GIFTED — DJMike (5 Subs)","LAST RAID — TechnoFamily (125 viewers)","DAILY SUBS — 12"},
        {"LAST FOLLOW — DJMUSIC123","LAST RAID — TechnoFamily (125 viewers)","DAILY SUBS — 12","DAILY CHEERS — 2.3K"},
        {"LAST SUB — ElectroVibes","LAST CHEER — RaveMan (500 Bits)","DAILY CHEERS — 2.3K","NEXT: SPECIAL SET 22:00"},
        {"LAST FOLLOW — DJMUSIC123","LAST GIFTED — DJMike (5 Subs)","LAST RAID — TechnoFamily (78 viewers)","DAILY SUBS — 12"},
        {"LAST FOLLOW — DJMUSIC123","LAST SUB — ElectroVibes","LAST GIFTED — DJMike (5 Subs)","DAILY CHEERS — 2.3K"},
        {"LAST RAID — TechnoFamily (125 viewers)","LAST CHEER — RaveMan (250 Bits)","DAILY SUBS — 6","FOLLOW THE MUSIC"},
        {"LAST FOLLOW — DJMUSIC123","LAST SUB — ElectroVibes","LAST GIFTED — DJMike (5 Subs)","DAILY CHEERS — 2.3K"},
        {"LAST FOLLOW — DJMUSIC123","LAST RAID — TechnoFamily (78 viewers)","DAILY SUBS — 12","DAILY CHEERS — 2.3K"},
        {"LAST SUB — ElectroVibes","LAST GIFTED — DJMike (3 Subs)","LAST CHEER — RaveMan (500 Bits)","NEXT: SPECIAL SET 22:00"}
    };
    obs_data_t *data=obs_source_get_settings(source);
    FILE *manifest=fopen("validation/styles-catalog.tsv","w");
    FILE *metrics=fopen("validation/style-text-metrics.tsv","w");
    for(int style=1;style<17;style++){
        obs_data_set_int(data,"style",style);apply(source,data);
        obs_properties_t *props=obs_source_properties(source);
        obs_property_button_clicked(obs_properties_get(props,"apply_reference"),NULL);settle();
        obs_data_set_int(data,"motion",0);obs_data_set_int(data,"upper_mode",0);
        obs_data_set_bool(data,"map_animated",false);obs_data_set_bool(data,"clock_enabled",true);
        obs_data_set_string(data,"header_text",style>=11?(style==12||style==14||style==15?"ON AIR":"LIVE"):"NEXUS");
        obs_data_set_string(data,"header_subtitle","LIVE");obs_data_set_string(data,"upper_label","NOW PLAYING");obs_data_set_string(data,"lower_label",labels[style]);
        obs_data_set_string(data,"upper_text",songs[style]);obs_data_set_string(data,"weather_text",weather[style]);
        if(compact_style(style)){
            obs_data_set_string(data,"lower_text","♥ DJMusic123");obs_data_set_string(data,"lower_text_2","★ ElectroVibes");
            obs_data_set_string(data,"lower_text_3",style==12||style==14||style==16?"🎁 DJMike (3)":"🎁 DJMike (5)");
            obs_data_set_string(data,"lower_text_4",style==12?"◁ TechnoFamily (78)":"◁ TechnoFamily (125)");
        }else{
            const char *keys[]={"lower_text","lower_text_2","lower_text_3","lower_text_4"};
            for(int i=0;i<4;i++)obs_data_set_string(data,keys[i],news[style][i]);
        }
        obs_data_set_string(data,"lower_text_5","");
        if(style==10){
            obs_data_set_string(data,"lower_text","♥ FOLLOW DJMUSIC123");obs_data_set_string(data,"lower_text_2","🎁 GIFTED DJMike (5)");
            obs_data_set_string(data,"lower_text_3","★ SUB ElectroVibes");obs_data_set_string(data,"lower_text_4","◁ RAID TechnoFamily (78)");obs_data_set_string(data,"lower_text_5","◇ CHEER RaveMan (250)");
        }
        apply(source,data);uint32_t w=obs_source_get_width(source),h=obs_source_get_height(source);
        int clock_index=0;obs_source_enum_active_sources(source,reference_clock,&clock_index);settle();
        struct text_metrics measurement={.file=metrics,.style=names[style]};obs_source_enum_active_sources(source,record_text_metrics,&measurement);
        uint8_t *frame=capture(source);char path[256];snprintf(path,sizeof(path),"validation/style-%s.rgba",names[style]);FILE *out=fopen(path,"wb");
        if(out){fwrite(frame,1,(size_t)w*h*4,out);fclose(out);}else check("catalog output opens",false);
        bool alpha=true;for(size_t i=0;i<(size_t)w*h;i++)if(frame[i*4]>frame[i*4+3]||frame[i*4+1]>frame[i*4+3]||frame[i*4+2]>frame[i*4+3])alpha=false;
        char test[128];snprintf(test,sizeof(test),"%s frame keeps premultiplied alpha",names[style]);check(test,alpha);
        check("catalog group matches selected design",obs_data_get_int(data,"style_group")== (style>=10?1:0));
        const struct reference_profile *profile=&reference_profiles[style];
        float expected=(float)(profile->top+profile->bottom+profile->above)*w/profile->width;
        check("reference preset preserves its own aspect ratio",fabsf((float)h-(compact_style(style)?2:10)-expected)<2.1f);
        if(style==1||style==4){
            int separators=0;uint32_t start=(uint32_t)obs_data_get_int(data,"upper_height");
            for(uint32_t y=start+3;y<h-7;y++)for(uint32_t x=400;x<1200&&x<w-15;x++){
                size_t at=((size_t)y*w+x)*4;
                if(frame[at]>100&&frame[at]>frame[at+1]*2&&frame[at]>frame[at+2]*2)separators++;
            }
            check("normal news uses independently colored red separators",separators>8);
        }
        check("catalog lists only the selected style group",obs_property_list_item_count(obs_properties_get(props,"style"))==(style>=10?7:10));
        if(style==2)check("Light has no subtitle, news label or map settings",!obs_property_visible(obs_properties_get(props,"header_subtitle"))&&!obs_property_visible(obs_properties_get(props,"lower_label"))&&!obs_property_visible(obs_properties_get(props,"map_enabled")));
        if(style==14)check("Compact Sport has an angled right edge rather than rounded capsule",frame[((size_t)10*w+w-11)*4+3]>200&&frame[((size_t)(h-9)*w+w-11)*4+3]<10);
        if(style==14||style==15){
            const struct reference_profile *rp=&reference_profiles[style];
            int gap_x=(style==14?475:471)-rp->x;
            gap_x=(int)(gap_x*w/(float)rp->width)-6;int touching=0;
            for(uint32_t y=10;y<h-10;y++){size_t p=((size_t)y*w+gap_x)*4;bool ink=style==14?(frame[p]<90&&frame[p+1]<90&&frame[p+2]<90):(frame[p]>160&&frame[p+1]>160&&frame[p+2]>160);if(ink)touching++;}
            check("Compact song text leaves a clear gap after the original music icon",touching<=2);
        }
        if(style==10){
            struct compact_reader parts={0};obs_source_enum_active_sources(source,read_compact_text,&parts);
            check("Ultra includes its five reference categories and compact labels",parts.count==5&&!strcmp(parts.text[0],"FOLLOW DJMUSIC123")&&!strcmp(parts.text[1],"GIFTED DJMike (5)")&&!strcmp(parts.text[2],"SUB ElectroVibes")&&!strcmp(parts.text[3],"RAID TechnoFamily (78)")&&!strcmp(parts.text[4],"CHEER RaveMan (250)"));
        }else if(style>10){
            struct compact_reader parts={0};obs_source_enum_active_sources(source,read_compact_text,&parts);
            check("compact entries keep separate native text settings",parts.count==4&&!strcmp(parts.text[0],"DJMusic123")&&!strcmp(parts.text[1],"ElectroVibes")&&!strcmp(parts.text[2],style==12||style==14||style==16?"DJMike (3)":"DJMike (5)")&&!strcmp(parts.text[3],style==12?"TechnoFamily (78)":"TechnoFamily (125)"));
            check("compact item preparation preserves the full news line",lower_has("DJMusic123")&&lower_has("ElectroVibes")&&lower_has("TechnoFamily"));
            char caption[256];snprintf(caption,sizeof(caption),"%s",obs_data_get_string(data,"header_text"));
            obs_data_set_string(data,"header_text","");apply(source,data);settle();uint8_t *blank=capture(source);
            const int ink_boxes[][4]={{76,28,119,42},{60,27,143,42},{65,27,120,43},{65,25,145,42},{70,29,148,44},{61,23,114,38}};
            const int *box=ink_boxes[style-11];int old_ink=0,residual_edges=0;
            for(int cy=box[1];cy<box[3];cy++)for(int cx=box[0];cx<box[2];cx++){
                uint32_t px=(uint32_t)(cx*w/(float)profile->width),py=1+(uint32_t)(cy*(h-2)/(float)profile->top);
                size_t at=((size_t)py*w+px)*4;
                bool ink=style==14?(blank[at]<45&&blank[at+1]<45&&blank[at+2]<45):(blank[at]>238&&blank[at+1]>238&&blank[at+2]>238);
                if(ink)old_ink++;
                if(cx>box[0]+3&&cx<box[2]-3){
                    for(int channel=0;channel<3;channel++)if(abs((int)blank[at-4+channel]-2*(int)blank[at+channel]+(int)blank[at+4+channel])>12){residual_edges++;break;}
                }
            }
            check("empty Compact caption does not retain lettering from its artwork",old_ink<5);
            check("empty Compact caption does not retain sharp glyph-shaped edges",residual_edges<20);
            char empty_path[256];snprintf(empty_path,sizeof(empty_path),"validation/header-blank-%s.rgba",names[style]);FILE *empty=fopen(empty_path,"wb");if(empty){fwrite(blank,1,(size_t)w*h*4,empty);fclose(empty);}free(blank);
            obs_data_set_string(data,"header_text",caption);apply(source,data);settle();
            if(style==11){
                obs_data_t *font=obs_data_get_obj(data,"font");long long size=obs_data_get_int(font,"size");
                obs_data_set_int(font,"size",size+2);obs_data_set_obj(data,"font",font);apply(source,data);settle();uint8_t *edited=capture(source);
                check("editing the reference caption font switches to editable native lettering",!region_equal(frame,edited,w,15,1,210,h-2));free(edited);
                obs_data_set_int(font,"size",size);obs_data_set_obj(data,"font",font);obs_data_release(font);apply(source,data);settle();
                uint32_t saved_bg=(uint32_t)obs_data_get_int(data,"header_bg"),saved_color=(uint32_t)obs_data_get_int(data,"header_color");
                obs_data_set_int(data,"header_bg",(saved_bg&0xffffff)|0x80000000);apply(source,data);settle();edited=capture(source);int opaque_caption=0;
                for(uint32_t y=15;y<h-10;y++)for(uint32_t x=80;x<150;x++){size_t at=((size_t)y*w+x)*4;if(edited[at]>230&&edited[at+1]>230&&edited[at+2]>230&&edited[at+3]>240)opaque_caption++;}
                check("caption opacity stays independent of a translucent reference backing",opaque_caption>10);free(edited);
                obs_data_set_int(data,"header_bg",saved_bg);obs_data_set_int(data,"header_color",0xff00ff00);apply(source,data);settle();edited=capture(source);int green_caption=0;
                for(uint32_t y=15;y<h-10;y++)for(uint32_t x=80;x<150;x++){size_t at=((size_t)y*w+x)*4;if(edited[at+1]>180&&edited[at+1]>edited[at]*2&&edited[at+1]>edited[at+2]*2)green_caption++;}
                check("editing the reference caption color renders native colored lettering",green_caption>10);free(edited);
                obs_data_set_int(data,"header_color",saved_color);apply(source,data);settle();
            }
        }
        if(style==16){
            const char *keys[]={"lower_text","lower_text_2","lower_text_3","lower_text_4","lower_text_5"};char saved[5][512];
            for(int i=0;i<5;i++){snprintf(saved[i],sizeof(saved[i]),"%s",obs_data_get_string(data,keys[i]));obs_data_set_string(data,keys[i],"");}
            apply(source,data);settle();uint8_t *empty_news=capture(source);int old_amount=0;
            for(int cy=20;cy<47;cy++)for(int cx=profile->width-40;cx<profile->width-30;cx++){
                uint32_t px=(uint32_t)(cx*w/(float)profile->width),py=1+(uint32_t)(cy*(h-2)/(float)profile->top);size_t at=((size_t)py*w+px)*4;
                if(empty_news[at]>180&&empty_news[at+1]>180&&empty_news[at+2]>180)old_amount++;
            }
            check("Dark painted tail does not bake the reference raid amount into empty news",old_amount==0);free(empty_news);
            for(int i=0;i<5;i++)obs_data_set_string(data,keys[i],saved[i]);apply(source,data);settle();
        }
        fprintf(manifest,"%s\t%u\t%u\n",names[style],w,h);free(frame);obs_properties_destroy(props);
    }
    fclose(manifest);fclose(metrics);
    obs_properties_t *groups=obs_source_properties(source);
    const char *saved_header=obs_data_get_string(data,"header_text");char header_copy[256];snprintf(header_copy,sizeof(header_copy),"%s",saved_header);
    obs_data_set_int(data,"style_group",0);obs_property_modified(obs_properties_get(groups,"style_group"),data);apply(source,data);
    check("switching to Normal selects a normal style without replacing text",obs_data_get_int(data,"style")==1&&!strcmp(obs_data_get_string(data,"header_text"),header_copy));
    obs_data_set_int(data,"style_group",1);obs_property_modified(obs_properties_get(groups,"style_group"),data);apply(source,data);
    check("switching to Compact selects Prime and preserves text",obs_data_get_int(data,"style")==11&&!strcmp(obs_data_get_string(data,"header_text"),header_copy));
    obs_data_set_bool(data,"clock_enabled",false);apply(source,data);uint8_t *before_release=capture(source);
    obs_data_t *json=obs_data_create_from_json(obs_data_get_json(data));obs_source_t *copy=obs_source_create_private("obs_news_ticker_nexus","Compact group reload",json);
    obs_data_t *restored=obs_source_get_settings(copy);check("compact group survives scene serialization",obs_data_get_int(restored,"style_group")==1&&obs_data_get_int(restored,"style")==11);
    obs_data_release(restored);obs_source_release(copy);obs_data_release(json);settle();uint8_t *after_release=capture(source);
    check("reference graphics remain valid after another source is destroyed",!memcmp(before_release,after_release,(size_t)obs_source_get_width(source)*obs_source_get_height(source)*4));free(before_release);free(after_release);
    obs_data_set_int(data,"upper_bg",0);obs_data_set_int(data,"lower_bg",0);obs_data_set_int(data,"header_bg",0);
    obs_data_set_int(data,"header_color",0x80ffffff);obs_data_set_int(data,"text_color",0x80ffffff);obs_data_set_int(data,"lower_text_color",0x80ffffff);apply(source,data);
    uint8_t *partial=capture(source);bool premult=true;int peak=0;
    for(size_t i=0;i<(size_t)obs_source_get_width(source)*obs_source_get_height(source);i++){int a=partial[i*4+3];if(a>peak)peak=a;if(partial[i*4]>a||partial[i*4+1]>a||partial[i*4+2]>a)premult=false;}
    check("reference icons preserve partial premultiplied opacity",premult&&peak>40&&peak<170);free(partial);
    obs_data_set_int(data,"header_color",0);obs_data_set_int(data,"text_color",0);apply(source,data);partial=capture(source);int red_icon_pixels=0;
    uint32_t pw=obs_source_get_width(source),ph=obs_source_get_height(source);
    for(uint32_t y=5;y<ph-5;y++)for(uint32_t x=0;x<pw;x++){size_t p=((size_t)y*pw+x)*4;if(partial[p]>60&&partial[p]>partial[p+1]*2&&partial[p]>partial[p+2]*2)red_icon_pixels++;}
    check("event icon opacity follows the news band independently of Now Playing",red_icon_pixels>15);free(partial);
    obs_properties_destroy(groups);obs_data_release(data);
}
struct weather_reader {int index;char text[512];};
static void weather_child(obs_source_t *parent,obs_source_t *source,void *context)
{
    UNUSED_PARAMETER(parent);struct weather_reader *r=context;if(r->index++!=7)return;
    obs_data_t *s=obs_source_get_settings(source);snprintf(r->text,sizeof(r->text),"%s",obs_data_get_string(s,"text"));obs_data_release(s);
}
static void weather_list(obs_data_t *settings,const char *first,const char *second)
{
    obs_data_array_t *list=obs_data_array_create();const char *values[]={first,second};
    for(unsigned i=0;i<2;i++)if(values[i]){obs_data_t *entry=obs_data_create();obs_data_set_string(entry,"value",values[i]);obs_data_array_push_back(list,entry);obs_data_release(entry);}
    obs_data_set_array(settings,"weather_locations",list);obs_data_array_release(list);
}
static bool weather_list_is(obs_data_t *settings,unsigned count,const char *first,const char *second)
{
    obs_data_array_t *list=obs_data_get_array(settings,"weather_locations");bool ok=obs_data_array_count(list)==count;
    if(first){obs_data_t *entry=obs_data_array_item(list,0);ok=ok&&!strcmp(obs_data_get_string(entry,"value"),first);obs_data_release(entry);}
    if(second){obs_data_t *entry=obs_data_array_item(list,1);ok=ok&&!strcmp(obs_data_get_string(entry,"value"),second);obs_data_release(entry);}
    obs_data_array_release(list);return ok;
}
struct clock_editor {int index;const char *text;};
static void edit_clock_child(obs_source_t *parent,obs_source_t *source,void *context)
{
    UNUSED_PARAMETER(parent);struct clock_editor *r=context;if(r->index++!=6)return;
    obs_data_t *settings=obs_source_get_settings(source);obs_data_set_string(settings,"text",r->text);obs_data_set_int(settings,"opacity",0);obs_source_update(source,settings);obs_data_release(settings);
}
static void layout_checks(void)
{
    obs_data_t *s=obs_data_create();obs_data_set_int(s,"style",STYLE_NEWS);obs_data_set_int(s,"width",1370);obs_data_set_int(s,"upper_height",42);obs_data_set_int(s,"lower_height",31);
    obs_data_set_int(s,"now_width",100);obs_data_set_bool(s,"clock_enabled",true);obs_data_set_bool(s,"clock_seconds",true);obs_data_set_bool(s,"map_enabled",false);obs_data_set_string(s,"weather_text","18°C Madrid");obs_data_set_string(s,"upper_text","QA");
    obs_source_t *source=obs_source_create_private("obs_news_ticker_nexus","Clock layout regression",s);obs_source_inc_showing(source);settle();
    const char *locales[]={"es-ES","en-US","it-IT","fr-FR","de-DE"};
    const char *design_labels[]={"Diseño y tamaño","Design and size","Design e dimensioni","Style et dimensions","Design und Größe"};
    for(unsigned language=0;language<5;language++){
        obs_set_locale(locales[language]);obs_properties_t *translated=obs_source_properties(source);
        check("native properties load the selected NEXUS language",!strcmp(obs_property_description(obs_properties_get(translated,"section_design")),design_labels[language]));
        obs_properties_destroy(translated);
    }
    obs_set_locale("es-ES");
    obs_data_set_int(s,"header_color",0xff123456);obs_source_update(source,s);
    obs_properties_t *grouped=obs_source_properties(source);
    obs_properties_apply_settings(grouped,s);
    check("opening properties preserves custom palette",obs_data_get_int(s,"header_color")==0xff123456);
    obs_data_set_int(s,"style",STYLE_LIGHT);obs_property_modified(obs_properties_get(grouped,"style"),s);
    check("selecting Light automatically applies its palette",obs_data_get_int(s,"header_color")==0xffd64c05&&obs_data_get_int(s,"upper_bg")==0xfff4e5d2);
    obs_data_set_int(s,"header_color",0xff123456);obs_properties_apply_settings(grouped,s);
    check("rebuilding properties preserves edits within the selected style",obs_data_get_int(s,"header_color")==0xff123456);
    obs_data_set_int(s,"style_group",1);obs_property_modified(obs_properties_get(grouped,"style_group"),s);
    check("switching groups applies the new style palette",obs_data_get_int(s,"style")==STYLE_PRIME&&obs_data_get_int(s,"header_color")==0xffffffff);
    obs_data_set_int(s,"style_group",0);obs_property_modified(obs_properties_get(grouped,"style_group"),s);
    check("settings separate music, news and weather into native groups",obs_property_get_type(obs_properties_get(grouped,"section_now"))==OBS_PROPERTY_GROUP&&obs_property_get_type(obs_properties_get(grouped,"section_news"))==OBS_PROPERTY_GROUP&&obs_property_get_type(obs_properties_get(grouped,"section_weather"))==OBS_PROPERTY_GROUP);
    obs_data_set_int(s,"now_motion",1);obs_property_modified(obs_properties_get(grouped,"now_motion"),s);
    check("grouped music motion still exposes ping pong controls",obs_property_visible(obs_properties_get(grouped,"now_speed"))&&obs_property_visible(obs_properties_get(grouped,"now_pause")));
    obs_properties_destroy(grouped);
    struct clock_editor edit={0,"1:1"};obs_source_enum_active_sources(source,edit_clock_child,&edit);settle();uint8_t *first=capture(source);
    edit.index=0;edit.text="88:88:88 PM";obs_source_enum_active_sources(source,edit_clock_child,&edit);settle();uint8_t *next=capture(source);
    check("clock digit widths cannot move the weather block",region_equal(first,next,1370,650,10,690,27));free(first);free(next);
    obs_data_set_bool(s,"info_align_right",true);obs_data_set_int(s,"weather_width",320);obs_source_update(source,s);settle();edit.index=0;obs_source_enum_active_sources(source,edit_clock_child,&edit);settle();first=capture(source);
    obs_data_set_string(s,"weather_text","18°C Washington D. C.");obs_source_update(source,s);settle();edit.index=0;obs_source_enum_active_sources(source,edit_clock_child,&edit);settle();next=capture(source);
    check("right-aligned city lengths leave outer clock column stable",region_equal(first,next,1370,1210,10,140,27));free(first);free(next);
    obs_data_t *saved=obs_save_source(source),*settings=obs_data_get_obj(saved,"settings");check("right alignment and weather width are saved",obs_data_get_bool(settings,"info_align_right")&&obs_data_get_int(settings,"weather_width")==320);obs_data_release(settings);obs_data_release(saved);
    obs_data_set_int(s,"style",STYLE_MAP);obs_data_set_string(s,"header_text","");obs_data_set_bool(s,"map_enabled",true);obs_data_set_bool(s,"map_animated",false);obs_data_set_int(s,"header_bg",0xff0000ff);obs_data_set_bool(s,"clock_enabled",false);obs_data_set_string(s,"weather_text","");obs_source_update(source,s);settle();first=capture(source);
    FILE *out=fopen("validation/map-header-empty.rgba","wb");if(out){fwrite(first,1,1370*73*4,out);fclose(out);}obs_data_set_string(s,"header_text","DJ TEST");obs_source_update(source,s);settle();next=capture(source);
    check("Map Style caption remains editable over cleaned artwork",!region_equal(first,next,1370,15,5,170,36));out=fopen("validation/map-header-editable.rgba","wb");if(out){fwrite(next,1,1370*73*4,out);fclose(out);}free(first);free(next);
    obs_data_set_int(s,"style",STYLE_NEWS);obs_data_set_string(s,"header_text","");obs_source_update(source,s);settle();first=capture(source);
    obs_source_t *fresh=obs_source_create_private("obs_news_ticker_nexus","Fresh News map",s);obs_source_inc_showing(fresh);settle();next=capture(fresh);
    check("News Channel map survives switching from Map Style",region_equal(first,next,1370,1140,0,228,73));free(first);free(next);
    first=capture(source);obs_data_set_bool(s,"map_enabled",false);obs_source_update(source,s);settle();next=capture(source);
    check("News Channel enabled map produces visible geography",!region_equal(first,next,1370,1140,0,228,73));free(first);free(next);
    obs_source_dec_showing(fresh);obs_source_release(fresh);
    obs_data_set_int(s,"style",STYLE_SPORT);obs_data_set_int(s,"motion",0);
    obs_data_set_string(s,"lower_text","FOLLOW AAAAAAAAAAAAAAAAAAAA | SUB BBBBBBBBBBBBBBBBBBBB | RAID CCCCCCCCCCCCCCCCCCCC");
    obs_source_update(source,s);settle();first=capture(source);os_sleep_ms(300);next=capture(source);
    check("Compact Sport scrolling cannot paint over the diagonal end cap",region_equal(first,next,1370,1320,1,35,70));free(first);free(next);
    obs_data_set_int(s,"style",STYLE_DARK);obs_source_update(source,s);settle();first=capture(source);os_sleep_ms(300);next=capture(source);
    check("Compact Dark scrolling cannot paint over the green end cap",region_equal(first,next,1370,1285,1,70,70));free(first);free(next);
    obs_data_set_int(s,"style",STYLE_ULTRA);obs_data_set_int(s,"now_width",600);
    obs_data_set_string(s,"upper_text","AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
    obs_source_update(source,s);settle();first=capture(source);
    obs_data_set_string(s,"upper_text","");obs_source_update(source,s);settle();next=capture(source);
    check("Ultra Compact 600px Now Playing extends beyond old reference slot",!region_equal(first,next,1370,500,10,150,45));free(first);free(next);
    obs_data_t *font=obs_data_create();obs_data_set_string(font,"face","Ibrand");obs_data_set_int(font,"size",26);obs_data_set_obj(s,"font",font);obs_data_release(font);
    obs_data_set_int(s,"style",STYLE_NEWS);obs_data_set_string(s,"header_text","LIVE");obs_data_set_string(s,"header_subtitle","ON AIR");obs_data_set_string(s,"upper_label","NOW PLAYING");obs_data_set_string(s,"lower_label","LATEST");
    obs_data_set_int(s,"now_width",600);obs_data_set_string(s,"upper_text","Indy Lopez — Filterbeatz (Extended Mix)");obs_source_update(source,s);settle();first=capture(source);
    FILE *font_preview=fopen("validation/ibrand-caption-fit.rgba","wb");if(font_preview){fwrite(first,1,1370*73*4,font_preview);fclose(font_preview);}free(first);
    obs_source_dec_showing(source);obs_source_release(source);obs_data_release(s);
}
static void weather_live(obs_source_t *source)
{
    obs_data_t *s=obs_source_get_settings(source);
    obs_data_set_bool(s,"info_align_right",true);obs_data_set_bool(s,"map_enabled",false);
    obs_data_set_int(s,"style",STYLE_NEWS);obs_data_set_int(s,"width",1370);obs_data_set_int(s,"upper_height",42);obs_data_set_int(s,"lower_height",31);
    obs_data_set_string(s,"upper_text","Weather QA");obs_data_set_string(s,"lower_text","Lower unchanged");obs_data_set_string(s,"weather_text","Manual preserved");
    obs_data_set_int(s,"weather_mode",1);obs_data_set_bool(s,"weather_rotate",false);weather_list(s,"Madrid, Spain",NULL);obs_source_update(source,s);settle();
    obs_properties_t *p=obs_source_properties(source);
    check("automatic weather shows city and hides manual text",obs_property_visible(obs_properties_get(p,"weather_locations"))&&!obs_property_visible(obs_properties_get(p,"weather_text")));obs_properties_destroy(p);
    struct weather_reader r={0};
    for(int i=0;i<300;i++){r.index=0;r.text[0]=0;obs_source_enum_active_sources(source,weather_child,&r);if(strstr(r.text,"Madrid"))break;os_sleep_ms(100);}
    printf("LIVE WEATHER %s\n",r.text);
    check("real HTTP weather reaches native GDI text",strstr(r.text,"Madrid (ES)")&&strstr(r.text,"°C")&&!strstr(r.text,"Open-Meteo"));
    uint8_t *pixels=capture(source);check("weather updates preserve upper and lower text",!strcmp(capture_upper,"Weather QA")&&!strcmp(capture_lower,"Lower unchanged"));
    FILE *out=fopen("validation/weather-live-preview.rgba","wb");if(out){fwrite(pixels,1,1370*73*4,out);fclose(out);}free(pixels);
    obs_data_t *saved=obs_save_source(source),*settings=obs_data_get_obj(saved,"settings");
    check("weather city and mode survive save",obs_data_get_int(settings,"weather_mode")==1&&weather_list_is(settings,1,"Madrid, Spain",NULL));obs_data_release(settings);obs_data_release(saved);
    obs_data_set_bool(s,"weather_rotate",true);obs_data_set_int(s,"weather_interval",5);weather_list(s,"Madrid, Spain","London, United Kingdom");obs_source_update(source,s);
    p=obs_source_properties(source);check("rotation exposes extra cities and interval",obs_property_visible(obs_properties_get(p,"weather_locations"))&&obs_property_visible(obs_properties_get(p,"weather_interval")));obs_properties_destroy(p);
    bool london=false,madrid=false,fade_out=false,fade_in=false;obs_properties_t *held_editor=obs_source_properties(source);
    signal_handler_connect(obs_source_get_signal_handler(source),"update_properties",count_twitch_properties,NULL);LONG before_updates=property_updates();
    for(int i=0;i<400;i++){
        r.index=0;obs_source_enum_active_sources(source,weather_child,&r);uint8_t *frame=capture(source);int peak=0;
        for(int y=10;y<37;y++)for(int x=1110;x<1340;x++){int value=frame[((size_t)y*1370+x)*4];if(value>peak)peak=value;}free(frame);
        if(strstr(r.text,"Madrid")&&peak>60&&peak<240)fade_out=true;
        if(strstr(r.text,"London (GB)")){london=true;if(peak>60&&peak<240)fade_in=true;if(peak>=245)break;}
        os_sleep_ms(20);
    }
    check("city transition fades old and new weather text",fade_out&&fade_in);
    check("city editor receives no asynchronous rebuild",property_updates()==before_updates);
    check("status can be explicitly refreshed",obs_property_button_clicked(obs_properties_get(held_editor,"refresh_status"),source));obs_properties_destroy(held_editor);
    signal_handler_disconnect(obs_source_get_signal_handler(source),"update_properties",count_twitch_properties,NULL);

    printf("ROTATED WEATHER %s\n",r.text);
    check("world weather rotates to second real city",london);
    for(int i=0;i<150;i++){r.index=0;obs_source_enum_active_sources(source,weather_child,&r);if(strstr(r.text,"Madrid")){madrid=true;break;}os_sleep_ms(100);}
    check("world weather wraps past empty cities to first",madrid);
    pixels=capture(source);free(pixels);check("weather rotation leaves music and lower news intact",!strcmp(capture_upper,"Weather QA")&&!strcmp(capture_lower,"Lower unchanged"));
    saved=obs_save_source(source);settings=obs_data_get_obj(saved,"settings");check("rotation configuration survives save",obs_data_get_bool(settings,"weather_rotate")&&obs_data_get_int(settings,"weather_interval")==5&&weather_list_is(settings,2,"Madrid, Spain","London, United Kingdom"));obs_data_release(settings);obs_data_release(saved);
    obs_data_set_bool(s,"weather_rotate",false);obs_source_update(source,s);settle();r.index=0;obs_source_enum_active_sources(source,weather_child,&r);check("disabling rotation preserves primary city",strstr(r.text,"Madrid")!=NULL);
    obs_data_set_int(s,"weather_mode",0);obs_source_update(source,s);settle();r.index=0;obs_source_enum_active_sources(source,weather_child,&r);
    check("manual weather text restored",!strcmp(r.text,"Manual preserved"));
    obs_data_set_int(s,"weather_mode",1);weather_list(s,NULL,NULL);obs_source_update(source,s);settle();r.index=0;obs_source_enum_active_sources(source,weather_child,&r);
    check("blank city clears previous temperature",!r.text[0]);
    obs_data_release(s);
}
#include "hype-render-checks.h"
int main(int argc,char **argv)
{
    if(argc!=4) return 2;
    check("ping-pong waits at the left edge",ping_pong_offset(.25,100,50,.5)==0);
    check("ping-pong travels to the right edge",ping_pong_offset(1.5,100,50,.5)==50);
    check("ping-pong waits at the right edge",ping_pong_offset(2.75,100,50,.5)==100);
    check("ping-pong reverses toward the start",ping_pong_offset(4,100,50,.5)==50);
    check("ping-pong cycle returns to start",ping_pong_offset(5,100,50,.5)==0);
    check("fitting titles have zero ping-pong offset",ping_pong_offset(2,0,50,.5)==0);

    char data[2048],graphics[2048],textdll[2048],textdata[2048];
    snprintf(data,sizeof(data),"%s/data/libobs/",argv[1]);
    snprintf(graphics,sizeof(graphics),"%s/bin/64bit/libobs-d3d11.dll",argv[1]);
    snprintf(textdll,sizeof(textdll),"%s/obs-plugins/64bit/obs-text.dll",argv[1]);
    snprintf(textdata,sizeof(textdata),"%s/data/obs-plugins/obs-text",argv[1]);
    if(!obs_startup("es-ES",NULL,NULL)) return 3;
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wdeprecated-declarations"
    obs_add_data_path(data);
    #pragma clang diagnostic pop
    struct obs_video_info vi={.graphics_module=graphics,.fps_num=30,.fps_den=1,
        .base_width=1920,.base_height=1080,.output_width=1920,.output_height=1080,
        .output_format=VIDEO_FORMAT_RGBA,.gpu_conversion=false,.colorspace=VIDEO_CS_709,
        .range=VIDEO_RANGE_FULL,.scale_type=OBS_SCALE_BILINEAR};
    if(obs_reset_video(&vi)!=OBS_VIDEO_SUCCESS) return 4;
    if(!load(textdll,textdata) || !load(argv[2],argv[3])) return 5;
    obs_source_t *s=obs_source_create_private("obs_news_ticker_nexus","Ticker",NULL);
    check("source creates with native GDI+ children",s!=NULL);
    if(!s || !obs_source_get_width(s)) {obs_source_release(s);obs_shutdown();return 6;}
    obs_source_inc_showing(s);
    settle();
    if(getenv("NEXUS_TICKER_HYPE_QA")){hype_checks();obs_source_dec_showing(s);obs_source_release(s);obs_shutdown();printf("RESULT checks=%d failures=%d\n",checks,failures);return failures?1:0;}
    if(getenv("NEXUS_TICKER_CATALOG")){
        catalog(s);obs_source_dec_showing(s);obs_source_release(s);obs_shutdown();
        printf("RESULT checks=%d failures=%d\n",checks,failures);return failures?1:0;
    }
    if(getenv("NEXUS_TICKER_LAYOUT_QA")){
        layout_checks();obs_source_dec_showing(s);obs_source_release(s);obs_shutdown();printf("RESULT checks=%d failures=%d\n",checks,failures);return failures?1:0;
    }
    if(getenv("NEXUS_TICKER_WEATHER_QA")){
        weather_live(s);obs_source_dec_showing(s);obs_source_release(s);obs_shutdown();
        printf("RESULT checks=%d failures=%d\n",checks,failures);return failures?1:0;
    }
    obs_data_t *weather_defaults=obs_source_get_settings(s);
    check("weather remains manual by default",obs_data_get_int(weather_defaults,"weather_mode")==0);
    check("new sources preselect capital rotation without enabling HTTP",obs_data_get_bool(weather_defaults,"weather_rotate")&&obs_data_get_int(weather_defaults,"weather_mode")==0);
    check("all twenty-four capital defaults are stored",weather_list_is(weather_defaults,24,"Madrid (ES)","Londres (GB)"));
    check("world weather interval defaults to fifteen seconds",obs_data_get_int(weather_defaults,"weather_interval")==15);
    obs_data_release(weather_defaults);
    {
    obs_data_t *legacy=obs_data_create();obs_data_set_string(legacy,"weather_city","Custom A");obs_data_set_string(legacy,"weather_city_2","Custom B");obs_data_set_bool(legacy,"weather_rotate",false);
    obs_source_t *legacy_source=obs_source_create_private("obs_news_ticker_nexus","Legacy weather migration",legacy);obs_data_release(legacy);
    legacy=obs_source_get_settings(legacy_source);check("legacy city order and fixed mode preserved",weather_list_is(legacy,2,"Custom A","Custom B")&&!obs_data_get_bool(legacy,"weather_rotate"));
    obs_properties_t *legacy_props=obs_source_properties(legacy_source);check("native editable city list exists",obs_property_get_type(obs_properties_get(legacy_props,"weather_locations"))==OBS_PROPERTY_EDITABLE_LIST);
    check("default capitals can be explicitly restored",obs_property_button_clicked(obs_properties_get(legacy_props,"weather_capitals"),legacy_source));
    check("restore loads all capitals and rotation",weather_list_is(legacy,24,"Madrid (ES)","Londres (GB)")&&obs_data_get_bool(legacy,"weather_rotate"));obs_properties_destroy(legacy_props);
    weather_list(legacy,NULL,NULL);obs_source_update(legacy_source,legacy);obs_data_t *empty_saved=obs_save_source(legacy_source),*empty_settings=obs_data_get_obj(empty_saved,"settings");check("explicit empty list remains empty",weather_list_is(empty_settings,0,NULL,NULL));obs_data_release(empty_settings);obs_data_release(empty_saved);
    obs_data_release(legacy);obs_source_release(legacy_source);
    legacy=obs_data_create();obs_data_set_string(legacy,"weather_city","");obs_data_set_string(legacy,"weather_city_2","Second city");obs_data_set_bool(legacy,"weather_rotate",false);legacy_source=obs_source_create_private("obs_news_ticker_nexus","Legacy empty primary",legacy);obs_data_release(legacy);legacy=obs_source_get_settings(legacy_source);
    check("legacy empty primary is not replaced by secondary",weather_list_is(legacy,2,"","Second city"));obs_data_release(legacy);obs_source_release(legacy_source);

    }
    layout_checks();
    check("default source dimensions",obs_source_get_width(s)==1920 && obs_source_get_height(s)==100);
    obs_properties_t *p=obs_source_properties(s);
    check("Spanish properties",!strcmp(obs_property_description(obs_properties_get(p,"width")),"Anchura (px)"));
    check("editable header",obs_properties_get(p,"header_text")!=NULL);
    check("independent band height properties",obs_properties_get(p,"upper_height") && obs_properties_get(p,"lower_height") && !obs_properties_get(p,"height"));
    check("author link",strstr(obs_property_description(obs_properties_get(p,"credit")),"https://discord.gg/dhcqZPDYh")!=NULL);
    obs_properties_destroy(p);
    uint8_t *pixels=capture(s);
    FILE *f=fopen("validation/phase1-preview.rgba","wb");
    if(!f) return 7;
    fwrite(pixels,1,1920*100*4,f); fclose(f);
    int bright=0;
    for(int i=0;i<1920*100;i++) if(pixels[i*4]>180 && pixels[i*4+1]>180 && pixels[i*4+2]>180) bright++;
    check("real text pixels rendered",bright>100);
    free(pixels);
    obs_data_t *settings=obs_source_get_settings(s);
    obs_data_set_bool(settings,"map_enabled",false);
    obs_data_set_string(settings,"header_text","");
    obs_data_set_string(settings,"upper_text","");
    obs_data_set_string(settings,"lower_text","");
    obs_data_set_int(settings,"upper_bg",0x800000ff);
    obs_data_set_int(settings,"lower_bg",0);
    apply(s,settings);
    pixels=capture(s);
    check("premultiplied half-alpha background",abs(pixels[0]-128)<=1 && pixels[1]==0 && pixels[2]==0 && abs(pixels[3]-128)<=1);
    size_t pos=(size_t)1920*75*4;
    check("transparent lower band has zero RGB",pixels[pos]==0 && pixels[pos+1]==0 && pixels[pos+2]==0 && pixels[pos+3]==0);
    free(pixels);
    obs_data_set_int(settings,"upper_height",30); obs_data_set_int(settings,"lower_height",90);
    obs_data_set_int(settings,"upper_bg",0xff0000ff); obs_data_set_int(settings,"lower_bg",0xffff0000);
    apply(s,settings); pixels=capture(s);
    size_t last_top=(size_t)1920*29*4,first_bottom=(size_t)1920*30*4;
    check("unequal heights set exact GPU band boundary",obs_source_get_height(s)==120 && pixels[last_top]==255 && pixels[last_top+2]==0 && pixels[first_bottom]==0 && pixels[first_bottom+2]==255);
    free(pixels);
    obs_data_set_int(settings,"upper_height",60); apply(s,settings);
    check("changing upper height preserves lower height",obs_source_get_height(s)==150 && obs_data_get_int(settings,"lower_height")==90);
    obs_data_set_int(settings,"lower_height",24); apply(s,settings);
    check("changing lower height preserves upper height",obs_source_get_height(s)==84 && obs_data_get_int(settings,"upper_height")==60);
    obs_data_set_int(settings,"width",1); obs_data_set_int(settings,"upper_height",-1); obs_data_set_int(settings,"lower_height",99999); apply(s,settings);
    check("invalid dimensions bounded independently",obs_source_get_width(s)==320 && obs_source_get_height(s)==424);
    obs_data_set_int(settings,"width",640); obs_data_set_int(settings,"upper_height",36); obs_data_set_int(settings,"lower_height",44);
    obs_data_set_string(settings,"header_text","DJ EMIMAS — Música en directo");
    obs_data_set_string(settings,"lower_text","A long custom message with Unicode: España");
    apply(s,settings);
    obs_data_t *saved=obs_save_source(s);
    obs_data_t *saved_settings=obs_data_get_obj(saved,"settings");
    obs_source_t *second=obs_source_create_private("obs_news_ticker_nexus","Reloaded",saved_settings);
    obs_data_release(saved_settings);
    obs_source_inc_showing(second); settle();
    check("source serialization reload",second && obs_source_get_width(second)==640 && obs_source_get_height(second)==80);
    obs_data_t *restored=obs_source_get_settings(second);
    check("unequal heights survive save/load",obs_data_get_int(restored,"upper_height")==36 && obs_data_get_int(restored,"lower_height")==44);
    check("Unicode header survives save/load",!strcmp(obs_data_get_string(restored,"header_text"),"DJ EMIMAS — Música en directo"));
    obs_data_release(restored); obs_data_release(saved);
    pixels=capture(second); check("small layout renders",pixels!=NULL); free(pixels);
    obs_data_t *disk=obs_data_create_from_json(obs_data_get_json(settings));
    obs_source_t *third=obs_source_create_private("obs_news_ticker_nexus","JSON reload",disk);
    obs_data_release(disk); settle();
    check("JSON settings reload with fresh defaults",third && obs_source_get_width(third)==640);
    obs_source_release(third);
    obs_data_t *legacy=obs_data_create_from_json("{\"height\":101,\"width\":640}");
    obs_source_t *old=obs_source_create_private("obs_news_ticker_nexus","Legacy height",legacy);
    settle();
    obs_data_t *migrated=obs_source_get_settings(old);
    check("legacy odd total migrates without resizing",obs_source_get_height(old)==101 && obs_data_get_int(migrated,"upper_height")==50 && obs_data_get_int(migrated,"lower_height")==51);
    obs_data_set_int(migrated,"lower_height",80); apply(old,migrated);
    check("legacy total does not override new height controls",obs_source_get_height(old)==130);
    obs_data_release(migrated); obs_data_release(legacy); obs_source_release(old);
    obs_data_set_int(settings,"style",1);
    obs_data_set_int(settings,"width",1920);
    obs_data_set_int(settings,"upper_height",56); obs_data_set_int(settings,"lower_height",36);
    obs_data_set_string(settings,"header_text","DJ EMIMAS");
    obs_data_set_string(settings,"header_subtitle","ON AIR");
    obs_data_set_string(settings,"upper_text","Armin van Buuren — Blah Blah Blah (demo manual)");
    obs_data_set_string(settings,"lower_text","MENSAJE PERSONALIZADO  •  Bienvenidos al directo  •  Contenido de prueba");
    apply(s,settings);
    check("selecting style preserves custom colors",obs_data_get_int(settings,"upper_bg")==0xff0000ff);
    p=obs_source_properties(s);
    obs_property_modified(obs_properties_get(p,"style"),settings);
    check("news style exposes editable labels",obs_property_visible(obs_properties_get(p,"upper_label")) && obs_property_visible(obs_properties_get(p,"header_subtitle")));
    check("palette button applies",obs_property_button_clicked(obs_properties_get(p,"apply_palette"),NULL));
    settle();
    check("palette preserves dimensions and header",obs_source_get_height(s)==92 && !strcmp(obs_data_get_string(settings,"header_text"),"DJ EMIMAS"));
    check("news palette provides separate readable text colors",obs_data_get_int(settings,"text_color")==0xffffffff && obs_data_get_int(settings,"lower_text_color")==0xff21170e);
    pixels=capture(s);
    f=fopen("validation/news-channel-preview.rgba","wb"); if(!f) return 8;
    fwrite(pixels,1,1920*92*4,f); fclose(f);
    int dark_text=0;
    for(int y=58;y<90;y++) for(int x=450;x<1900;x++) {
        size_t at=((size_t)y*1920+x)*4;
        if(pixels[at]<70 && pixels[at+1]<70 && pixels[at+2]<70 && pixels[at+3]==255) dark_text++;
    }
    check("news renderer draws dark text on light lower band",dark_text>100);
    free(pixels);
    disk=obs_data_create_from_json(obs_data_get_json(settings));
    third=obs_source_create_private("obs_news_ticker_nexus","News reload",disk); settle();
    restored=obs_source_get_settings(third);
    check("news layout and palette survive JSON reload",obs_data_get_int(restored,"style")==1 && obs_data_get_int(restored,"lower_text_color")==0xff21170e && obs_source_get_height(third)==92);
    obs_data_release(restored); obs_data_release(disk); obs_source_release(third);
    const char *texts[]={"header_text","upper_text","lower_text","header_subtitle","upper_label","lower_label"};
    for(int i=0;i<6;i++) obs_data_set_string(settings,texts[i],"");
    obs_data_set_int(settings,"header_bg",0); obs_data_set_int(settings,"upper_bg",0); obs_data_set_int(settings,"lower_bg",0);
    obs_data_set_int(settings,"width",320); obs_data_set_int(settings,"upper_height",24); obs_data_set_int(settings,"lower_height",400);
    apply(s,settings); pixels=capture(s);
    bool clear=true;
    for(size_t i=0;i<320*424*4;i++) if(pixels[i]!=0) {clear=false;break;}
    check("news minimum width and unequal heights retain transparent zero RGB",clear);
    free(pixels);
    obs_data_set_int(settings,"style",0); obs_data_set_int(settings,"motion",1);
    obs_data_set_int(settings,"scroll_speed",120); obs_data_set_int(settings,"scroll_gap",80);
    obs_data_set_int(settings,"width",640); obs_data_set_int(settings,"upper_height",40); obs_data_set_int(settings,"lower_height",40);
    obs_data_set_int(settings,"text_color",0xffffffff); obs_data_set_int(settings,"lower_text_color",0xffffffff);
    obs_data_set_string(settings,"header_text","LIVE");
    obs_data_set_string(settings,"upper_text","ABCDEFGHIJKLMNOPQRSTUVWXYZ abcdefghijklmnopqrstuvwxyz 0123456789 — texto largo sin reducir para leerlo completo");
    obs_data_set_string(settings,"lower_text","Mensaje inferior en movimiento: Bienvenidos al directo, gracias por acompañarnos. Texto manual de demostración.");
    apply(s,settings);
    obs_property_modified(obs_properties_get(p,"motion"),settings);
    check("scroll controls become visible",obs_property_visible(obs_properties_get(p,"scroll_speed")) && obs_property_visible(obs_properties_get(p,"scroll_gap")));
    uint8_t *first=capture(s); uint64_t first_time=capture_time;
    os_sleep_ms(400);
    uint8_t *next=capture(s); double elapsed=(capture_time-first_time)/1e9;
    int shift=displacement(first,next);
    check("scroll advances at configured pixels per second",elapsed<1 && abs(shift-(int)(elapsed*120))<=8);
    check("only lower news moves while Now Playing stays fixed",region_equal(first,next,640,16,0,608,40) && !region_equal(first,next,640,100,40,524,40));
    check("header stays fixed while content scrolls",region_equal(first,next,640,0,40,90,40));
    bool clipped=true;
    for(int y=0;y<80;y++) for(int x=0;x<640;x++) {
        if((x>=624 || (y<40 && x<16)) && next[((size_t)y*640+x)*4+3]!=0) clipped=false;
    }
    check("moving text is clipped to band margins",clipped);
    free(first); free(next);
    obs_data_set_int(settings,"scroll_speed",240); apply(s,settings);
    first=capture(s); first_time=capture_time; os_sleep_ms(300); next=capture(s);
    elapsed=(capture_time-first_time)/1e9; shift=displacement(first,next);
    check("speed changes affect actual GPU displacement",elapsed<.6 && abs(shift-(int)(elapsed*240))<=12);
    free(first); free(next);
    obs_source_dec_showing(s); settle(); first=capture(s); os_sleep_ms(200); next=capture(s);
    check("hidden source pauses scroll",!memcmp(first,next,640*80*4));
    free(first); free(next); obs_source_inc_showing(s);
    obs_data_set_int(settings,"motion",0); apply(s,settings);
    first=capture(s); os_sleep_ms(150); next=capture(s);
    check("static mode remains still",!memcmp(first,next,640*80*4));
    free(first); free(next);
    obs_data_set_int(settings,"motion",1);
    obs_data_set_int(settings,"text_color",0x80ffffff); obs_data_set_int(settings,"lower_text_color",0x80ffffff);
    obs_data_set_int(settings,"header_color",0x80ffffff); apply(s,settings);
    pixels=capture(s);
    FILE *alpha_frame=fopen("validation/scroll-alpha.rgba","wb"); if(alpha_frame) {fwrite(pixels,1,640*80*4,alpha_frame);fclose(alpha_frame);}
    bool premultiplied=true,has_alpha=false;
    for(size_t i=0;i<640*80;i++) {
        uint8_t alpha=pixels[i*4+3]; if(alpha) has_alpha=true;
        if(alpha>130 || pixels[i*4]>alpha+2 || pixels[i*4+1]>alpha+2 || pixels[i*4+2]>alpha+2) premultiplied=false;
    }
    check("scroll composite preserves partial premultiplied alpha",premultiplied && has_alpha);
    free(pixels);
    obs_scene_t *scene=obs_scene_create("Transformed ticker fixture");
    obs_sceneitem_t *item=obs_scene_add(scene,s);
    struct vec2 scale,pos2; vec2_set(&scale,.5f,.5f); vec2_set(&pos2,100,100);
    obs_sceneitem_set_scale(item,&scale); obs_sceneitem_set_pos(item,&pos2); obs_sceneitem_set_rot(item,10);
    obs_source_t *scene_source=obs_scene_get_source(scene); pixels=capture(scene_source);
    uint32_t scene_w=obs_source_get_width(scene_source),scene_h=obs_source_get_height(scene_source);
    bool outside=false,inside=false;
    for(uint32_t y=0;y<scene_h;y++) for(uint32_t x=0;x<scene_w;x++) if(pixels[((size_t)y*scene_w+x)*4+3]) {
        if(x<90 || x>420 || y<99 || y>200) outside=true; else inside=true;
    }
    check("scaled rotated scene keeps scroll inside source bounds",inside && !outside);
    free(pixels); obs_source_remove(scene_source); obs_scene_release(scene); settle();
    obs_data_set_int(settings,"width",320); obs_data_set_int(settings,"scroll_speed",400); obs_data_set_int(settings,"scroll_gap",16);
    obs_data_set_string(settings,"header_text",""); obs_data_set_string(settings,"upper_text","ABC"); obs_data_set_string(settings,"lower_text","XYZ");
    apply(s,settings);
    int previous=-1,wraps=0;
    for(int frame=0;frame<13;frame++) {
        pixels=capture(s); int left=320;
        for(int y=48;y<72;y++) for(int x=16;x<304;x++) if(pixels[((size_t)y*320+x)*4+3] && x<left) left=x;
        if(left<320) {if(previous>=0 && left>previous+60) wraps++; previous=left;}
        free(pixels); os_sleep_ms(80);
    }
    check("short text repeats after leaving the band",wraps>0);
    obs_data_set_int(settings,"style",1); obs_data_set_int(settings,"motion",1);
    obs_data_set_int(settings,"scroll_speed",110); obs_data_set_int(settings,"width",960);
    obs_data_set_int(settings,"upper_height",56); obs_data_set_int(settings,"lower_height",36);
    obs_data_set_string(settings,"header_text","DJ EMIMAS"); obs_data_set_string(settings,"header_subtitle","ON AIR");
    obs_data_set_string(settings,"upper_label","NOW PLAYING"); obs_data_set_string(settings,"lower_label","LATEST");
    obs_data_set_string(settings,"upper_text","Armin van Buuren — Blah Blah Blah • Demostración manual del scroll continuo");
    obs_data_set_string(settings,"lower_text","MENSAJE PERSONALIZADO • Bienvenidos al directo • Gracias por acompañarnos • Contenido de prueba");
    apply(s,settings); obs_property_button_clicked(obs_properties_get(p,"apply_palette"),NULL); settle();
    FILE *animation=fopen("validation/scroll-preview.rgba","wb"),*timing=fopen("validation/scroll-preview-timing.txt","w");
    if(!animation || !timing) return 9;
    first=NULL;
    bool animation_changed=false,fixed_labels=true;
    for(int frame=0;frame<24;frame++) {
        next=capture(s);
        if(!first) {first=malloc(960*92*4);memcpy(first,next,960*92*4);}
        else {
            if(memcmp(first,next,960*92*4)) animation_changed=true;
            if(!region_equal(first,next,960,0,0,145,92)) fixed_labels=false;
        }
        fwrite(next,1,960*92*4,animation); fprintf(timing,"%llu\n",(unsigned long long)capture_time);
        free(next); os_sleep_ms(80);
    }
    fclose(animation); fclose(timing); free(first);
    check("news style renders moving preview with fixed header",animation_changed && fixed_labels);
    disk=obs_data_create_from_json(obs_data_get_json(settings));
    third=obs_source_create_private("obs_news_ticker_nexus","Scroll reload",disk); settle(); restored=obs_source_get_settings(third);
    check("motion and speed survive JSON reload",obs_data_get_int(restored,"motion")==1 && obs_data_get_int(restored,"scroll_speed")==110);
    obs_data_release(restored); obs_data_release(disk); obs_source_release(third);
    check("block entry starts hidden and reaches midpoint",block_reveal(0,.5,1,.5)==0 && fabs(block_reveal(.25,.5,1,.5)-.5)<.0001);
    check("block holds fully revealed",block_reveal(.5,.5,1,.5)==1 && block_reveal(1.4,.5,1,.5)==1);
    check("block returns behind anchor then pauses",fabs(block_reveal(1.75,.5,1,.5)-.5)<.0001 && block_reveal(2.2,.5,1,.5)==0);
    check("block cycle repeats without discontinuity",block_reveal(2.5,.5,1,.5)==0 && fabs(block_reveal(2.75,.5,1,.5)-.5)<.0001);
    obs_data_set_int(settings,"style",0); obs_data_set_int(settings,"motion",2);
    obs_data_set_int(settings,"width",640); obs_data_set_int(settings,"upper_height",40); obs_data_set_int(settings,"lower_height",40);
    obs_data_set_int(settings,"block_transition",150); obs_data_set_int(settings,"block_hold",3000); obs_data_set_int(settings,"block_pause",300);
    obs_data_set_int(settings,"upper_bg",0xff404040); obs_data_set_int(settings,"lower_bg",0xff404040);
    obs_data_set_string(settings,"header_text","LIVE"); obs_data_set_string(settings,"upper_text","ABC"); obs_data_set_string(settings,"lower_text","XYZ");
    apply(s,settings); os_sleep_ms(200);
    obs_property_modified(obs_properties_get(p,"motion"),settings);
    check("block timings replace scroll controls",obs_property_visible(obs_properties_get(p,"block_hold")) && obs_property_visible(obs_properties_get(p,"block_transition")) && !obs_property_visible(obs_properties_get(p,"scroll_speed")));
    first=capture(s);
    check("short lower block uses content width",first[((size_t)44*640+145)*4]!=64 && first[((size_t)44*640+250)*4]==64);
    os_sleep_ms(150); next=capture(s);
    check("block remains still during hold",!memcmp(first,next,640*80*4)); free(first);free(next);
    obs_data_set_string(settings,"lower_text","This longer block expands automatically within the available band width"); apply(s,settings); os_sleep_ms(200);
    pixels=capture(s);
    check("long lower block expands without resizing source",pixels[((size_t)44*640+260)*4]!=64 && obs_source_get_width(s)==640 && obs_source_get_height(s)==80);
    free(pixels);
    obs_data_set_int(settings,"block_transition",1000); obs_data_set_int(settings,"block_hold",500); obs_data_set_int(settings,"block_pause",100);
    apply(s,settings); obs_source_dec_showing(s); settle(); first=capture(s); os_sleep_ms(200); next=capture(s);
    check("hidden source pauses block entry",!memcmp(first,next,640*80*4)); free(first);free(next);obs_source_inc_showing(s);
    obs_data_set_int(settings,"block_transition",150);obs_data_set_int(settings,"block_hold",3000);obs_data_set_int(settings,"block_pause",300);
    obs_data_set_int(settings,"upper_bg",0);obs_data_set_int(settings,"lower_bg",0);obs_data_set_int(settings,"header_bg",0);
    obs_data_set_int(settings,"text_color",0x80ffffff);obs_data_set_int(settings,"lower_text_color",0x80ffffff);
    apply(s,settings);os_sleep_ms(200);pixels=capture(s);
    bool block_alpha=true; int body_alpha=0;
    for(int y=40;y<80;y++) for(int x=110;x<624;x++) {
        size_t at=((size_t)y*640+x)*4; int alpha=pixels[at+3]; if(alpha>body_alpha) body_alpha=alpha;
        if(pixels[at]>alpha+2 || pixels[at+1]>alpha+2 || pixels[at+2]>alpha+2) block_alpha=false;
    }
    check("block text stays premultiplied without double alpha",block_alpha && body_alpha>=120 && body_alpha<=130);free(pixels);
    obs_data_set_int(settings,"style",1);obs_data_set_int(settings,"width",960);obs_data_set_int(settings,"upper_height",56);obs_data_set_int(settings,"lower_height",36);
    obs_data_set_int(settings,"block_transition",350);obs_data_set_int(settings,"block_hold",700);obs_data_set_int(settings,"block_pause",300);
    obs_data_set_string(settings,"header_text","DJ EMIMAS");obs_data_set_string(settings,"upper_text","Demo manual: Armin van Buuren — Blah Blah Blah");
    obs_data_set_string(settings,"lower_text","Bienvenidos al directo • Mensaje personalizado");
    apply(s,settings);obs_property_button_clicked(obs_properties_get(p,"apply_palette"),NULL);settle();
    obs_data_set_string(settings,"upper_text","");obs_data_set_string(settings,"lower_text","");apply(s,settings);
    uint8_t *empty_news=capture(s);
    obs_data_set_string(settings,"upper_text","Demo manual: Armin van Buuren — Blah Blah Blah");
    obs_data_set_string(settings,"lower_text","Bienvenidos al directo • Mensaje personalizado");apply(s,settings);
    animation=fopen("validation/blocks-preview.rgba","wb");timing=fopen("validation/blocks-preview-timing.txt","w");if(!animation||!timing)return 10;
    first=NULL;animation_changed=false;fixed_labels=true;bool stable_frame=false,fully_hidden=false,block_clipped=true;uint8_t *previous_frame=NULL;
    for(int frame=0;frame<36;frame++) {
        next=capture(s);
        if(!first) {first=malloc(960*92*4);memcpy(first,next,960*92*4);}
        else {if(memcmp(first,next,960*92*4))animation_changed=true;if(!region_equal(first,next,960,0,0,145,92))fixed_labels=false;}
        if(region_equal(empty_news,next,960,0,56,960,36))fully_hidden=true;
        if(!region_equal(empty_news,next,960,944,0,16,92) || !region_equal(empty_news,next,960,0,0,360,56) || !region_equal(empty_news,next,960,0,56,277,36))block_clipped=false;
        if(previous_frame && !memcmp(previous_frame,next,960*92*4))stable_frame=true;
        free(previous_frame);previous_frame=next;
        fwrite(next,1,960*92*4,animation);fprintf(timing,"%llu\n",(unsigned long long)capture_time);os_sleep_ms(80);
    }
    fclose(animation);fclose(timing);free(previous_frame);free(first);free(empty_news);
    check("news blocks animate and hold with fixed header",animation_changed && stable_frame && fixed_labels);
    check("blocks fully retract during pause",fully_hidden);
    check("blocks stay behind fixed labels and inside right margin",block_clipped);
    disk=obs_data_create_from_json(obs_data_get_json(settings));third=obs_source_create_private("obs_news_ticker_nexus","Blocks reload",disk);settle();restored=obs_source_get_settings(third);
    check("block mode and timings survive JSON reload",obs_data_get_int(restored,"motion")==2 && obs_data_get_int(restored,"block_transition")==350 && obs_data_get_int(restored,"block_hold")==700 && obs_data_get_int(restored,"block_pause")==300);
    obs_data_release(restored);obs_data_release(disk);obs_source_release(third);
    obs_data_set_int(settings,"style",0);obs_data_set_int(settings,"motion",3);obs_data_set_int(settings,"width",640);
    obs_data_set_int(settings,"upper_height",40);obs_data_set_int(settings,"lower_height",40);
    obs_data_set_int(settings,"message_hold",500);obs_data_set_int(settings,"message_fade",250);
    obs_data_set_int(settings,"upper_bg",0);obs_data_set_int(settings,"lower_bg",0);obs_data_set_int(settings,"header_bg",0);
    obs_data_set_int(settings,"text_color",0xffffffff);obs_data_set_int(settings,"lower_text_color",0x80ffffff);
    obs_data_set_string(settings,"header_text","");obs_data_set_string(settings,"upper_text","UPPER FIXED");
    obs_data_set_string(settings,"lower_text","AAA");obs_data_set_string(settings,"lower_text_2","   ");obs_data_set_string(settings,"lower_text_3","BBBBB");
    obs_data_set_string(settings,"lower_text_4","");obs_data_set_string(settings,"lower_text_5","CC");apply(s,settings);
    obs_property_modified(obs_properties_get(p,"motion"),settings);
    check("alternating controls replace block timings",obs_property_visible(obs_properties_get(p,"message_hold")) && obs_property_visible(obs_properties_get(p,"message_fade")) && !obs_property_visible(obs_properties_get(p,"block_hold")));
    char observed[16][512]={{0}};int observed_count=0;bool upper_fixed=true,fade_alpha=true,partial_fade=false;
    first=NULL;
    for(int frame=0;frame<40;frame++) {
        next=capture(s);
        if(!first){first=malloc(640*80*4);memcpy(first,next,640*80*4);}
        else if(!region_equal(first,next,640,0,0,640,40))upper_fixed=false;
        if(observed_count<16 && (!observed_count || strcmp(observed[observed_count-1],capture_lower)))snprintf(observed[observed_count++],512,"%s",capture_lower);
        int max_alpha=0;
        for(int y=40;y<80;y++)for(int x=16;x<624;x++) {
            size_t at=((size_t)y*640+x)*4;int alpha=next[at+3];if(alpha>max_alpha)max_alpha=alpha;
            if(alpha>130 || next[at]>alpha+2 || next[at+1]>alpha+2 || next[at+2]>alpha+2)fade_alpha=false;
        }
        if(max_alpha>5 && max_alpha<120)partial_fade=true;
        free(next);os_sleep_ms(80);
    }
    free(first);
    check("non-block news joins all nonblank messages with separators",observed_count==1&&!strcmp(observed[0],"AAA   •   BBBBB   •   CC"));
    check("alternating leaves upper band unchanged",upper_fixed);
    check("fade scales premultiplied RGB and alpha together",fade_alpha && partial_fade);
    obs_source_dec_showing(s);settle();first=capture(s);os_sleep_ms(250);next=capture(s);
    check("hidden source pauses alternating sequence",!memcmp(first,next,640*80*4));free(first);free(next);obs_source_inc_showing(s);
    obs_data_set_int(settings,"motion",2);obs_data_set_int(settings,"block_transition",150);obs_data_set_int(settings,"block_hold",500);obs_data_set_int(settings,"block_pause",250);
    obs_data_set_string(settings,"lower_text","ONE");obs_data_set_string(settings,"lower_text_3","TWO");obs_data_set_string(settings,"lower_text_5","");apply(s,settings);
    char old_message[512]="";bool hidden_switch=true;int message_changes=0;
    for(int frame=0;frame<23;frame++) {
        pixels=capture(s);
        if(*old_message && strcmp(old_message,capture_lower)) {
            message_changes++;
            for(int y=40;y<80;y++)for(int x=0;x<640;x++)if(pixels[((size_t)y*640+x)*4+3])hidden_switch=false;
        }
        snprintf(old_message,sizeof(old_message),"%s",capture_lower);free(pixels);os_sleep_ms(80);
    }
    check("blocks rotate only while lower card is hidden",message_changes>0 && hidden_switch);
    const char *empty_keys[]={"lower_text","lower_text_2","lower_text_3","lower_text_4","lower_text_5"};
    for(int i=0;i<5;i++)obs_data_set_string(settings,empty_keys[i],i%2?" \t ":"");
    apply(s,settings);pixels=capture(s);bool empty_lower=true;
    for(size_t i=640*40;i<640*80;i++)if(pixels[i*4+3])empty_lower=false;
    check("all empty messages leave lower content blank",empty_lower && !*capture_lower);free(pixels);
    const char *demo_messages[]={"Mensaje 1 • Bienvenidos al directo","Mensaje 2 • Gracias por acompañarnos","Mensaje 3 • Música y comunidad","Mensaje 4 • Disfruta de la sesión","Mensaje 5 • Demostración manual NEXUS"};
    for(int i=0;i<5;i++)obs_data_set_string(settings,empty_keys[i],demo_messages[i]);
    obs_data_set_int(settings,"style",1);obs_data_set_int(settings,"motion",3);obs_data_set_int(settings,"width",960);
    obs_data_set_int(settings,"upper_height",56);obs_data_set_int(settings,"lower_height",36);obs_data_set_int(settings,"message_fade",120);
    obs_data_set_string(settings,"header_text","DJ EMIMAS");obs_data_set_string(settings,"upper_text","Now Playing • Demostración manual");
    apply(s,settings);obs_property_button_clicked(obs_properties_get(p,"apply_palette"),NULL);settle();
    animation=fopen("validation/alternating-preview.rgba","wb");timing=fopen("validation/alternating-preview-timing.txt","w");if(!animation||!timing)return 11;
    observed_count=0;char order[16][512]={{0}};
    for(int frame=0;frame<64;frame++) {
        pixels=capture(s);
        if(observed_count<16 && (!observed_count || strcmp(order[observed_count-1],capture_lower)))snprintf(order[observed_count++],512,"%s",capture_lower);
        fwrite(pixels,1,960*92*4,animation);fprintf(timing,"%llu\n",(unsigned long long)capture_time);free(pixels);os_sleep_ms(80);
    }
    fclose(animation);fclose(timing);
    bool joined_five=observed_count==1;for(int i=0;i<5;i++)if(!strstr(order[0],demo_messages[i]))joined_five=false;
    check("all five messages stay together outside dynamic blocks",joined_five);
    disk=obs_data_create_from_json(obs_data_get_json(settings));third=obs_source_create_private("obs_news_ticker_nexus","Five-message reload",disk);settle();restored=obs_source_get_settings(third);
    bool five_saved=obs_data_get_int(restored,"motion")==3 && obs_data_get_int(restored,"message_fade")==120;
    for(int i=0;i<5;i++)if(strcmp(obs_data_get_string(restored,empty_keys[i]),demo_messages[i]))five_saved=false;
    check("five Unicode messages and alternating mode survive JSON reload",five_saved);
    obs_data_release(restored);obs_data_release(disk);obs_source_release(third);
    obs_data_set_int(settings,"motion",0);apply(s,settings);pixels=capture(s);free(pixels);
    check("static mode returns to first configured message",lower_has(demo_messages[0]));
    char formatted[32];
    check("clock formats midnight in 24 hours",local_clock_format(formatted,sizeof(formatted),0,0,0,false,false) && !strcmp(formatted,"00:00"));
    check("clock formats midnight and noon in 12 hours",local_clock_format(formatted,sizeof(formatted),0,0,0,true,false) && !strcmp(formatted,"12:00 AM") && local_clock_format(formatted,sizeof(formatted),12,0,0,true,false) && !strcmp(formatted,"12:00 PM"));
    check("clock formats seconds and afternoon suffix",local_clock_format(formatted,sizeof(formatted),13,5,9,true,true) && !strcmp(formatted,"01:05:09 PM"));
    check("clock rejects invalid time and small buffers",!local_clock_format(formatted,sizeof(formatted),24,0,0,false,false) && !*formatted && !local_clock_format(formatted,2,23,59,59,false,true));
    obs_property_modified(obs_properties_get(p,"clock_enabled"),settings);
    check("clock is disabled by default with hidden options",!obs_data_get_bool(settings,"clock_enabled") && !obs_property_visible(obs_properties_get(p,"clock_format")));
    first=capture(s);
    obs_data_set_bool(settings,"clock_enabled",true);obs_data_set_bool(settings,"clock_seconds",true);obs_data_set_int(settings,"clock_format",0);apply(s,settings);
    obs_property_modified(obs_properties_get(p,"clock_enabled"),settings);
    check("enabling clock exposes format and seconds",obs_property_visible(obs_properties_get(p,"clock_format")) && obs_property_visible(obs_properties_get(p,"clock_seconds")));
    next=capture(s);
    SYSTEMTIME local_now;GetLocalTime(&local_now);
    char now_text[32],previous_text[32];unsigned total=local_now.wHour*3600u+local_now.wMinute*60u+local_now.wSecond,prior=(total+86399)%86400;
    local_clock_format(now_text,sizeof(now_text),local_now.wHour,local_now.wMinute,local_now.wSecond,false,true);
    local_clock_format(previous_text,sizeof(previous_text),prior/3600,(prior/60)%60,prior%60,false,true);
    check("native clock follows Windows local time",!strcmp(capture_clock,now_text) || !strcmp(capture_clock,previous_text));
    check("clock appears on right and preserves lower band",!region_equal(first,next,960,800,0,144,56) && region_equal(first,next,960,0,56,960,36));
    char earlier_clock[32];snprintf(earlier_clock,sizeof(earlier_clock),"%s",capture_clock);
    free(first);first=next;os_sleep_ms(1200);settle();next=capture(s);
    check("seconds advance without changing lower message pixels",strcmp(earlier_clock,capture_clock) && !region_equal(first,next,960,800,0,144,56) && region_equal(first,next,960,0,56,960,36));
    free(first);free(next);
    obs_data_set_bool(settings,"clock_enabled",false);apply(s,settings);first=capture(s);
    obs_data_set_bool(settings,"clock_enabled",true);obs_data_set_int(settings,"clock_format",1);obs_data_set_bool(settings,"clock_seconds",false);apply(s,settings);next=capture(s);
    check("runtime 12 hour clock displays AM or PM",strlen(capture_clock)==8 && (strstr(capture_clock," AM") || strstr(capture_clock," PM")));
    free(next);obs_data_set_bool(settings,"clock_enabled",false);apply(s,settings);next=capture(s);
    check("hiding clock restores original message area",!memcmp(first,next,960*92*4) && !*capture_clock);free(first);free(next);
    obs_data_set_int(settings,"width",320);obs_data_set_int(settings,"upper_height",24);obs_data_set_int(settings,"lower_height",37);apply(s,settings);first=capture(s);
    obs_data_set_bool(settings,"clock_enabled",true);obs_data_set_bool(settings,"clock_seconds",true);apply(s,settings);next=capture(s);
    check("clock at minimum width preserves margins and lower height",obs_source_get_height(s)==61 && region_equal(first,next,320,304,0,16,24) && region_equal(first,next,320,0,24,320,37));free(first);free(next);
    disk=obs_data_create_from_json(obs_data_get_json(settings));third=obs_source_create_private("obs_news_ticker_nexus","Clock reload",disk);settle();restored=obs_source_get_settings(third);
    check("clock options survive JSON reload",obs_data_get_bool(restored,"clock_enabled") && obs_data_get_bool(restored,"clock_seconds") && obs_data_get_int(restored,"clock_format")==1);
    obs_data_release(restored);obs_data_release(disk);obs_source_release(third);
    obs_data_set_int(settings,"style",0);obs_data_set_int(settings,"motion",1);obs_data_set_int(settings,"width",640);
    obs_data_set_int(settings,"upper_height",40);obs_data_set_int(settings,"lower_height",37);obs_data_set_int(settings,"clock_format",0);obs_data_set_bool(settings,"clock_seconds",false);
    obs_data_set_string(settings,"upper_text","ABCDEFGHIJKLMNOPQRSTUVWXYZ — a long scrolling track title with a separate fixed clock");apply(s,settings);
    first=capture(s);snprintf(earlier_clock,sizeof(earlier_clock),"%s",capture_clock);os_sleep_ms(200);next=capture(s);
    check("Basic scroll keeps Now Playing and clock fixed",strlen(capture_clock)==5 && region_equal(first,next,640,16,0,460,40) && (strcmp(earlier_clock,capture_clock) || region_equal(first,next,640,540,0,84,40)));
    free(first);free(next);
    obs_data_set_int(settings,"style",1);obs_data_set_int(settings,"motion",0);obs_data_set_bool(settings,"clock_seconds",true);
    obs_data_set_int(settings,"width",960);obs_data_set_int(settings,"upper_height",56);obs_data_set_int(settings,"lower_height",36);
    obs_data_set_int(settings,"clock_format",0);obs_data_set_string(settings,"upper_text","Armin van Buuren — Blah Blah Blah (demo manual)");apply(s,settings);
    animation=fopen("validation/clock-preview.rgba","wb");timing=fopen("validation/clock-preview-timing.txt","w");if(!animation||!timing)return 12;
    for(int frame=0;frame<16;frame++) {pixels=capture(s);fwrite(pixels,1,960*92*4,animation);fprintf(timing,"%llu\n",(unsigned long long)capture_time);free(pixels);os_sleep_ms(80);}
    fclose(animation);fclose(timing);
    struct np_sample sample;
    const char *valid_now="{\"schema\":\"nexus.now-playing.v1\",\"state\":\"playing\",\"updated_at\":1000,\"artist\":\"Artista\",\"title\":\"Tema ñ\"}";
    check("Now Playing parses explicit playback and Unicode",np_parse(valid_now,strlen(valid_now),&sample) && sample.status==NP_PLAYING && !strcmp(sample.text,"Artista — Tema ñ"));
    check("Now Playing expires old data and rejects future timestamps",np_current_status(&sample,1016,15)==NP_STALE && np_current_status(&sample,990,15)==NP_FUTURE);
    const char *title_only="{\"schema\":\"nexus.now-playing.v1\",\"state\":\"playing\",\"updated_at\":1000,\"title\":\"Solo\"}";
    const char *wrong_artist="{\"schema\":\"nexus.now-playing.v1\",\"state\":\"playing\",\"updated_at\":1000,\"title\":\"Solo\",\"artist\":123}";
    check("Now Playing accepts absent artist but rejects wrong artist type",np_parse(title_only,strlen(title_only),&sample) && !strcmp(sample.text,"Solo") && !np_parse(wrong_artist,strlen(wrong_artist),&sample));
    const char *paused_now="{\"schema\":\"nexus.now-playing.v1\",\"state\":\"paused\",\"updated_at\":1000,\"artist\":\"Old\",\"title\":\"Old title\"}";
    check("Now Playing paused state carries no active text",np_parse(paused_now,strlen(paused_now),&sample) && sample.status==NP_PAUSED && !*sample.text);
    const char *bad_now="{\"schema\":\"wrong\",\"state\":\"playing\",\"updated_at\":1000,\"title\":\"Fake\"}";
    check("Now Playing rejects unsupported schema",!np_parse(bad_now,strlen(bad_now),&sample));
    const char *control_now="{\"schema\":\"nexus.now-playing.v1\",\"state\":\"playing\",\"updated_at\":1000,\"title\":\"Bad\\nTitle\"}";
    char too_large[16385];memset(too_large,'x',sizeof(too_large));
    check("Now Playing rejects controls and oversized payloads",!np_parse(control_now,strlen(control_now),&sample) && !np_parse(too_large,sizeof(too_large),&sample));
    wchar_t fixture_wide[1024];char fixture_path[4096];
    GetFullPathNameW(L"validation\\fixture-now-playing-ñ.json",1024,fixture_wide,NULL);
    WideCharToMultiByte(CP_UTF8,0,fixture_wide,-1,fixture_path,sizeof(fixture_path),NULL,NULL);
    check("Now Playing fixture publishes atomically",write_now_fixture(fixture_path,"playing","Artista","Tema ñ",np_now(),NULL));
    bool service=np_start();struct np_session *reader_a=service?np_acquire(fixture_path):NULL,*reader_b=service?np_acquire(fixture_path):NULL;
    check("Now Playing shares a reader for the same local file",reader_a && reader_a==reader_b);
    check("Now Playing rejects relative and UNC paths without reading",!np_acquire("relative.json") && !np_acquire("\\\\server\\share\\track.json"));
    np_release(reader_a);bool reader_live=false;
    for(int attempt=0;attempt<60;attempt++){np_read(reader_b,&sample);if(sample.status==NP_PLAYING){reader_live=true;break;}os_sleep_ms(20);}
    check("shared reader survives another consumer closing",reader_live && !strcmp(sample.text,"Artista — Tema ñ"));np_release(reader_b);np_stop();
    obs_data_set_int(settings,"upper_mode",1);obs_data_set_string(settings,"now_path",fixture_path);obs_data_set_bool(settings,"now_fallback",false);
    obs_data_set_string(settings,"upper_text","MANUAL FALLBACK");obs_data_set_int(settings,"now_max_age",15);apply(s,settings);
    obs_property_modified(obs_properties_get(p,"upper_mode"),settings);
    check("Now Playing JSON controls become visible",obs_property_visible(obs_properties_get(p,"now_path")) && obs_property_visible(obs_properties_get(p,"now_max_age")));
    check("native ticker receives track through Unicode file path",wait_now(s,"reproduciendo","Artista — Tema ñ"));
    first=capture(s);char lower_before[512];snprintf(lower_before,sizeof(lower_before),"%s",capture_lower);
    write_now_fixture(fixture_path,"playing","DJ Test","Second track",np_now(),NULL);
    check("file update changes active upper title",wait_now(s,"reproduciendo","DJ Test — Second track"));next=capture(s);
    check("external track changes preserve lower content and pixels",!strcmp(lower_before,capture_lower) && region_equal(first,next,960,0,56,960,36));free(first);free(next);
    write_now_fixture(fixture_path,"paused","DJ Test","Second track",np_now(),NULL);
    check("paused input clears old song without manual fallback",wait_now(s,"pausa",""));
    write_now_fixture(fixture_path,"playing","DJ Test","Old track",np_now()-120,NULL);
    check("expired input never displays old track",wait_now(s,"caducados",""));
    write_now_fixture(fixture_path,"playing","DJ Test","Wrong contract",np_now(),"unsupported.v9");
    check("invalid schema clears active input",wait_now(s,"no válidos",""));
    write_now_fixture(fixture_path,"playing","DJ Test","Future track",np_now()+60,NULL);
    check("future dated input is rejected",wait_now(s,"adelantada",""));
    DeleteFileW(fixture_wide);
    check("deleted input clears Now Playing",wait_now(s,"no encontrado",""));
    obs_data_set_bool(settings,"now_fallback",true);apply(s,settings);
    check("manual fallback is used only when explicitly enabled",wait_now(s,"no encontrado","MANUAL FALLBACK"));
    obs_data_set_bool(settings,"now_fallback",false);obs_data_set_int(settings,"now_max_age",3);apply(s,settings);
    write_now_fixture(fixture_path,"playing","DJ Test","Heartbeat expiry",np_now(),NULL);
    bool fresh_received=wait_now(s,"reproduciendo","DJ Test — Heartbeat expiry");os_sleep_ms(3300);
    check("heartbeat expires even if file remains unchanged",fresh_received && wait_now(s,"caducados",""));
    disk=obs_data_create_from_json(obs_data_get_json(settings));third=obs_source_create_private("obs_news_ticker_nexus","JSON input reload",disk);settle();restored=obs_source_get_settings(third);
    check("JSON input path and timeout survive save/load",obs_data_get_int(restored,"upper_mode")==1 && obs_data_get_int(restored,"now_max_age")==3 && !strcmp(obs_data_get_string(restored,"now_path"),fixture_path));
    obs_data_release(restored);obs_data_release(disk);obs_source_release(third);
    obs_data_set_int(settings,"now_max_age",15);apply(s,settings);
    write_now_fixture(fixture_path,"playing","DJ Test","JSON input demonstration",np_now(),NULL);wait_now(s,"reproduciendo","DJ Test — JSON input demonstration");
    pixels=capture(s);f=fopen("validation/now-playing-preview.rgba","wb");if(!f)return 13;fwrite(pixels,1,960*92*4,f);fclose(f);free(pixels);
    obs_data_set_int(settings,"upper_mode",0);apply(s,settings);pixels=capture(s);free(pixels);
    check("switching back to manual restores saved text",!strcmp(capture_upper,"MANUAL FALLBACK"));DeleteFileW(fixture_wide);
    struct obs_source_info playing_info={.id="obs_playing_next_development",.type=OBS_SOURCE_TYPE_INPUT,.get_name=playing_name,.create=playing_create,.destroy=playing_destroy};obs_register_source(&playing_info);
    playing_state("playing","Native track 1");obs_source_t *playing_provider=obs_source_create(playing_info.id,"Public Playing provider",NULL,NULL);
    obs_data_set_int(settings,"upper_mode",2);obs_data_set_bool(settings,"now_fallback",false);apply(s,settings);
    obs_property_modified(obs_properties_get(p,"upper_mode"),settings);
    check("automatic Playing mode hides the JSON path",!obs_property_visible(obs_properties_get(p,"now_path"))&&obs_property_visible(obs_properties_get(p,"now_status")));
    check("public OBS procedure supplies ticker track without a file",wait_now(s,"reproduciendo","DJ Bridge — Native track 1"));
    playing_state("playing","Native track 2");check("public track replacement reaches fixed Now Playing",wait_now(s,"reproduciendo","DJ Bridge — Native track 2"));
    playing_state("paused","");check("public pause clears active Now Playing",wait_now(s,"pausa",""));
    disk=obs_data_create_from_json(obs_data_get_json(settings));third=obs_source_create_private("obs_news_ticker_nexus","Public bridge reload",disk);restored=obs_source_get_settings(third);
    check("automatic Playing mode survives scene serialization",obs_data_get_int(restored,"upper_mode")==2);obs_data_release(restored);obs_data_release(disk);obs_source_release(third);
    obs_source_remove(playing_provider);obs_source_release(playing_provider);check("removed public provider clears the ticker",wait_now(s,"esperando",""));
    obs_data_set_int(settings,"upper_mode",0);apply(s,settings);
    /* Controlled ABI provider loaded only inside this isolated test process. */
    wchar_t fixture_dll[32768];
    GetFullPathNameW(L"build/twitch-fixture/nexus-twitch-core.dll",32768,fixture_dll,NULL);
    HMODULE fake_core=LoadLibraryW(fixture_dll);
    GetFullPathNameW(L"build/twitch-fixture/obs-frontend-api.dll",32768,fixture_dll,NULL);
    HMODULE fake_frontend=LoadLibraryW(fixture_dll);
    typedef void (*frontend_emit_fn)(enum obs_frontend_event);
    typedef int (*frontend_count_fn)(void);
    frontend_emit_fn frontend_emit=fake_frontend?(frontend_emit_fn)(void *)GetProcAddress(fake_frontend,"fixture_frontend_emit"):NULL;
    frontend_count_fn frontend_count=fake_frontend?(frontend_count_fn)(void *)GetProcAddress(fake_frontend,"fixture_frontend_subscribers"):NULL;
    check("controlled frontend lifecycle loads",frontend_emit&&frontend_count);
    if(!frontend_emit||!frontend_count)return 16;
    HMODULE ticker_module=GetModuleHandleW(L"obs-news-ticker-nexus.dll");
    typedef void (*post_load_fn)(void);
    typedef void (*emit_fn)(const NexusTwitchMessage *);
    post_load_fn post_load=ticker_module?(post_load_fn)(void *)GetProcAddress(ticker_module,"obs_module_post_load"):NULL;
    emit_fn emit=fake_core?(emit_fn)(void *)GetProcAddress(fake_core,"fixture_emit"):NULL;
    check("controlled Twitch ABI provider loads",fake_core&&post_load&&emit);
    if(!fake_core||!post_load||!emit)return 14;
    signal_handler_connect(obs_source_get_signal_handler(s),"update_properties",count_twitch_properties,NULL);
    post_load();
    post_load();
    check("post-load registers frontend lifecycle once",frontend_count()==1);
    obs_properties_t *startup_props=obs_source_properties(s);
    const char *startup_status=obs_property_description(obs_properties_get(startup_props,"twitch_status"));
    check("observer defers attachment until all owners initialized",strstr(startup_status,"no disponible")!=NULL);
    obs_properties_destroy(startup_props);
    frontend_emit(OBS_FRONTEND_EVENT_FINISHED_LOADING);
    startup_props=obs_source_properties(s);
    startup_status=obs_property_description(obs_properties_get(startup_props,"twitch_status"));
    check("finished-loading attaches shared observer",strstr(startup_status,"preparado")!=NULL);
    obs_properties_destroy(startup_props);
    obs_data_set_int(settings,"motion",0);obs_data_set_string(settings,"lower_text","CUSTOM FALLBACK");
    for(int i=2;i<=5;i++){char key[32];snprintf(key,sizeof(key),"lower_text_%d",i);obs_data_set_string(settings,key,"");}
    obs_data_set_bool(settings,"raid_enabled",true);apply(s,settings);
    pixels=capture(s);free(pixels);check("no raid retains custom message",lower_has("CUSTOM FALLBACK"));
    NexusTwitchMessage raid={0};raid.size=sizeof(raid);raid.kind=NEXUS_RAID;raid.instance=1;
    strcpy(raid.account,"fixture-channel");strcpy(raid.actor,"TestRaider");strcpy(raid.event_id,"raid-1");
    emit(&raid);os_sleep_ms(400);settle();pixels=capture(s);
    check("raid reaches native lower text",lower_has("LAST RAID — TestRaider"));
    check("raid does not rebuild an editable properties dialog",property_updates()==0);
    LONG updates_before=property_updates();
    f=fopen("validation/twitch-raid-preview.rgba","wb");if(!f)return 15;
    fwrite(pixels,1,960*92*4,f);fclose(f);free(pixels);
    strcpy(raid.actor,"DuplicateRaider");emit(&raid);os_sleep_ms(400);settle();pixels=capture(s);free(pixels);
    check("duplicate raid does not replace native text",lower_has("LAST RAID — TestRaider"));
    check("unchanged Twitch state does not repeat refresh",property_updates()==updates_before);
    strcpy(raid.account,"foreign-channel");strcpy(raid.event_id,"raid-2");emit(&raid);os_sleep_ms(400);settle();pixels=capture(s);free(pixels);
    check("foreign channel cannot enter lower band",lower_has("LAST RAID — TestRaider"));
    disk=obs_data_create_from_json(obs_data_get_json(settings));
    third=obs_source_create_private("obs_news_ticker_nexus","Raid option reload",disk);settle();
    restored=obs_source_get_settings(third);
    check("raid opt-in and custom fallback survive save/load",obs_data_get_bool(restored,"raid_enabled")&&!strcmp(obs_data_get_string(restored,"lower_text"),"CUSTOM FALLBACK"));
    obs_data_release(restored);obs_data_release(disk);obs_source_release(third);
    for(int mode=1;mode<=3;mode++){
        obs_data_set_int(settings,"motion",mode);apply(s,settings);os_sleep_ms(400);settle();pixels=capture(s);free(pixels);
        check("raid and custom text follow selected layout",mode==2?(lower_has("LAST RAID — TestRaider")||lower_has("CUSTOM FALLBACK")):(lower_has("LAST RAID — TestRaider")&&lower_has("CUSTOM FALLBACK")));
    }
    obs_data_set_bool(settings,"raid_rotate",true);
    obs_data_set_string(settings,"lower_text_3","SECOND MESSAGE");
    obs_data_set_int(settings,"block_transition",150);obs_data_set_int(settings,"block_hold",500);obs_data_set_int(settings,"block_pause",100);
    obs_data_set_int(settings,"message_hold",500);obs_data_set_int(settings,"message_fade",100);
    for(int mode=2;mode<=2;mode++){
        obs_data_set_int(settings,"motion",mode);apply(s,settings);
        bool saw_raid=false,saw_first=false,saw_second=false,unknown=false;
        for(int frame=0;frame<34;frame++){
            pixels=capture(s);free(pixels);
            if(lower_has("LAST RAID — TestRaider"))saw_raid=true;
            else if(lower_has("CUSTOM FALLBACK"))saw_first=true;
            else if(lower_has("SECOND MESSAGE"))saw_second=true;
            else unknown=true;
            os_sleep_ms(100);
        }
        check("raid rotates with sparse custom messages",saw_raid&&saw_first&&saw_second&&!unknown);
    }
    disk=obs_data_create_from_json(obs_data_get_json(settings));third=obs_source_create_private("obs_news_ticker_nexus","Raid rotation reload",disk);settle();
    restored=obs_source_get_settings(third);
    check("raid rotation survives save/load",obs_data_get_bool(restored,"raid_rotate"));
    obs_data_release(restored);obs_data_release(disk);obs_source_release(third);
    obs_data_set_string(settings,"lower_text","");obs_data_set_string(settings,"lower_text_3","");apply(s,settings);
    os_sleep_ms(1100);settle();pixels=capture(s);free(pixels);
    check("raid-only sequence remains valid without custom messages",lower_has("LAST RAID — TestRaider"));
    obs_data_set_bool(settings,"raid_rotate",false);obs_data_set_string(settings,"lower_text","CUSTOM FALLBACK");
    obs_data_set_bool(settings,"raid_enabled",false);apply(s,settings);pixels=capture(s);free(pixels);
    check("disable raid restores custom message",lower_has("CUSTOM FALLBACK"));
    strcpy(raid.account,"fixture-channel");strcpy(raid.actor,"NextRaider");strcpy(raid.event_id,"raid-3");
    updates_before=property_updates();emit(&raid);os_sleep_ms(400);settle();pixels=capture(s);free(pixels);
    check("opt-out keeps custom text during new raid",lower_has("CUSTOM FALLBACK"));
    check("background raid leaves editor widgets intact",property_updates()==updates_before);
    obs_properties_t *twitch_properties=obs_source_properties(s);
    obs_property_t *last_raid=obs_properties_get(twitch_properties,"twitch_last_raid");
    check("refreshed properties contain latest accepted login",last_raid&&!strcmp(obs_property_description(last_raid),"NextRaider"));
    obs_properties_destroy(twitch_properties);
    updates_before=property_updates();strcpy(raid.event_id,"raid-4");emit(&raid);os_sleep_ms(400);settle();
    check("repeated actor cannot force an editor rebuild",property_updates()==updates_before);
    obs_data_set_bool(settings,"raid_enabled",true);apply(s,settings);
    updates_before=property_updates();
    raid.kind=NEXUS_STATE;raid.active=0;raid.authorized=0;emit(&raid);
    os_sleep_ms(400);settle();pixels=capture(s);free(pixels);
    check("disconnect clears raid from native text",lower_has("CUSTOM FALLBACK"));
    check("disconnect cannot rebuild an active city editor",property_updates()==updates_before);
    typedef void (*emit_feed_fn)(const NexusTickerSnapshot *);
    emit_feed_fn emit_feed=(emit_feed_fn)(void *)GetProcAddress(fake_core,"fixture_emit_feed");
    check("extended ticker feed export loads",emit_feed!=NULL);if(!emit_feed)return 17;
    raid.kind=NEXUS_STATE;raid.active=1;raid.authorized=1;strcpy(raid.account,"fixture-channel");emit(&raid);
    NexusTickerSnapshot feed={0};feed.size=sizeof(feed);feed.version=1;feed.kind=1;feed.active=feed.authorized=1;feed.instance=1;
    strcpy(feed.account,"fixture-channel");feed.daily_subs=6;feed.daily_bits=250;feed.local_day=20261024;
    for(int i=0;i<5;i++){
        feed.latest[i].present=1;snprintf(feed.latest[i].id,sizeof(feed.latest[i].id),"feed-%d",i);
        strcpy(feed.latest[i].actor,"Seguidor ñ");strcpy(feed.latest[i].actor_id,"123");
    }
    feed.latest[2].anonymous=1;feed.latest[2].amount=250;feed.latest[3].anonymous=1;feed.latest[3].amount=5;feed.latest[4].amount=42;
    emit_feed(&feed);
    const char *feed_keys[]={"twitch_follow","twitch_sub","twitch_cheer","twitch_gift","raid_enabled","twitch_daily_subs","twitch_daily_bits"};
    const char *expected_feed[]={"LAST FOLLOW — Seguidor ñ","LAST SUB — Seguidor ñ","LAST CHEER — Anonymous — 250","LAST GIFTED — Anonymous — 5","LAST RAID — Seguidor ñ — 42","DAILY SUBS (recorded) — 6","DAILY BITS (recorded) — 250"};
    obs_data_set_int(settings,"motion",0);obs_data_set_bool(settings,"raid_rotate",false);
    for(int i=0;i<7;i++){
        for(int j=0;j<7;j++)obs_data_set_bool(settings,feed_keys[j],i==j);
        apply(s,settings);os_sleep_ms(350);settle();pixels=capture(s);free(pixels);
        check("extended Twitch event reaches native lower band",lower_has(expected_feed[i]));
    }
    for(int j=0;j<7;j++)obs_data_set_bool(settings,feed_keys[j],true);
    obs_data_set_bool(settings,"raid_rotate",true);obs_data_set_int(settings,"motion",2);apply(s,settings);
    unsigned seen_feed=0;bool saw_custom=false;
    for(int frame=0;frame<85;frame++){
        pixels=capture(s);free(pixels);
        for(int i=0;i<7;i++)if(lower_has(expected_feed[i]))seen_feed|=1u<<i;
        if(lower_has("CUSTOM FALLBACK"))saw_custom=true;
        os_sleep_ms(100);
    }
    check("all seven Twitch items rotate with custom message",seen_feed==127&&saw_custom);
    for(int j=0;j<7;j++)obs_data_set_bool(settings,feed_keys[j],j==0);
    apply(s,settings);bool custom_during_hidden_events=false;
    for(int frame=0;frame<24;frame++){
        snprintf(feed.latest[1].id,sizeof(feed.latest[1].id),"unselected-sub-%d",frame);emit_feed(&feed);
        os_sleep_ms(100);pixels=capture(s);free(pixels);
        if(lower_has("CUSTOM FALLBACK"))custom_during_hidden_events=true;
    }
    check("unselected Twitch events do not interrupt visible rotation",custom_during_hidden_events);
    strcpy(feed.account,"foreign");emit_feed(&feed);os_sleep_ms(350);settle();pixels=capture(s);free(pixels);
    check("foreign extended snapshot clears visible account data",lower_has("CUSTOM FALLBACK"));
    /* Upper-band contract: fixed area, fixed lettering and track-only fade. */
    for(int j=0;j<7;j++)obs_data_set_bool(settings,feed_keys[j],false);
    obs_data_set_bool(settings,"clock_enabled",false);obs_data_set_bool(settings,"map_enabled",false);
    obs_data_set_int(settings,"style",0);obs_data_set_int(settings,"upper_mode",0);
    obs_data_set_int(settings,"width",640);obs_data_set_int(settings,"upper_height",40);obs_data_set_int(settings,"lower_height",40);
    obs_data_set_int(settings,"now_width",160);obs_data_set_string(settings,"header_text","");
    obs_data_set_int(settings,"upper_bg",0);obs_data_set_int(settings,"lower_bg",0);obs_data_set_int(settings,"text_color",0xffffffff);
    obs_data_set_string(settings,"upper_text","FIXED NOW PLAYING");
    obs_data_set_string(settings,"lower_text","AAA");obs_data_set_string(settings,"lower_text_2","BBB");obs_data_set_string(settings,"lower_text_3","");obs_data_set_string(settings,"lower_text_4","CCC");
    for(int mode=0;mode<4;mode++){
        obs_data_set_int(settings,"motion",mode);apply(s,settings);
        first=capture(s);os_sleep_ms(180);next=capture(s);
        check("Now Playing stays fixed in every lower-band mode",region_equal(first,next,640,0,0,640,40));free(first);free(next);
        if(mode!=2)check("non-block modes contain the entire separated news line",!strcmp(capture_lower,"AAA   •   BBB   •   CCC"));
    }
    obs_data_set_int(settings,"motion",0);obs_data_set_string(settings,"upper_text","MMMMMMMMMMMM");apply(s,settings);first=capture(s);
    obs_data_set_string(settings,"upper_text","MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM");apply(s,settings);next=capture(s);
    check("long titles keep the same letter size inside the fixed width",region_equal(first,next,640,16,0,160,40));
    bool outside_now=false;for(int y=0;y<40;y++)for(int x=176;x<640;x++)if(next[((size_t)y*640+x)*4+3])outside_now=true;
    check("Now Playing clips exactly at the configured width",!outside_now);free(first);free(next);
    obs_data_set_int(settings,"now_motion",1);obs_data_set_int(settings,"now_speed",200);obs_data_set_int(settings,"now_pause",0);
    obs_data_set_string(settings,"upper_text","ABCDEFGHIJKLMNOPQRSTUVWXYZ 1234567890");apply(s,settings);first=capture(s);os_sleep_ms(250);next=capture(s);
    check("overflowing Now Playing moves in ping-pong mode",!region_equal(first,next,640,16,0,160,40));
    check("ping-pong preserves lower news pixels",region_equal(first,next,640,0,40,640,40));
    outside_now=false;for(int y=0;y<40;y++)for(int x=176;x<640;x++)if(next[((size_t)y*640+x)*4+3])outside_now=true;
    check("ping-pong stays clipped to its fixed viewport",!outside_now);free(first);free(next);
    obs_data_set_string(settings,"upper_text","OK");apply(s,settings);first=capture(s);os_sleep_ms(250);next=capture(s);
    check("short Now Playing remains still with ping-pong enabled",region_equal(first,next,640,0,0,640,40));free(first);free(next);
    disk=obs_data_create_from_json(obs_data_get_json(settings));third=obs_source_create_private("obs_news_ticker_nexus","Ping-pong reload",disk);settle();restored=obs_source_get_settings(third);
    check("ping-pong settings survive save and reload",obs_data_get_int(restored,"now_motion")==1&&obs_data_get_int(restored,"now_speed")==200&&obs_data_get_int(restored,"now_pause")==0);obs_data_release(restored);obs_data_release(disk);obs_source_release(third);
    obs_data_set_int(settings,"now_motion",0);
    obs_data_set_int(settings,"upper_mode",1);obs_data_set_int(settings,"now_max_age",15);obs_data_set_bool(settings,"now_fallback",false);apply(s,settings);
    write_now_fixture(fixture_path,"playing","A","Old track",np_now(),NULL);
    check("fade fixture receives initial track",wait_now(s,"reproduciendo","A — Old track"));os_sleep_ms(500);settle();first=capture(s);
    write_now_fixture(fixture_path,"playing","B","New track",np_now(),NULL);
    bool faded_old=false,faded_new=false,got_new=false;
    for(int frame=0;frame<60;frame++){
        next=capture(s);int peak=0;for(int y=0;y<40;y++)for(int x=16;x<176;x++){int a=next[((size_t)y*640+x)*4+3];if(a>peak)peak=a;}
        if(!strcmp(capture_upper,"A — Old track")&&peak>0&&peak<240)faded_old=true;
        if(!strcmp(capture_upper,"B — New track")){got_new=true;if(peak>0&&peak<240)faded_new=true;}
        free(next);os_sleep_ms(40);
    }
    check("song replacement fades old text out and new text in",faded_old&&faded_new&&got_new);
    next=capture(s);check("song fade leaves lower news pixels unchanged",region_equal(first,next,640,0,40,640,40));free(first);free(next);
    disk=obs_data_create_from_json(obs_data_get_json(settings));third=obs_source_create_private("obs_news_ticker_nexus","Fixed upper width reload",disk);settle();restored=obs_source_get_settings(third);
    check("Now Playing width survives save and reload",obs_data_get_int(restored,"now_width")==160);obs_data_release(restored);obs_data_release(disk);obs_source_release(third);
    obs_data_set_int(settings,"upper_mode",0);DeleteFileW(fixture_wide);
    /* Actual reference-design frames; all names/weather below are demo data. */
    obs_data_set_int(settings,"style",1);obs_data_set_int(settings,"width",1370);obs_data_set_int(settings,"upper_height",42);obs_data_set_int(settings,"lower_height",31);
    obs_data_set_int(settings,"now_width",350);obs_data_set_int(settings,"motion",1);obs_data_set_int(settings,"scroll_speed",90);
    obs_data_set_bool(settings,"map_enabled",true);obs_data_set_bool(settings,"map_animated",true);
    obs_data_set_bool(settings,"clock_enabled",true);obs_data_set_bool(settings,"clock_seconds",false);
    obs_data_set_string(settings,"header_text","NEXUS");obs_data_set_string(settings,"header_subtitle","LIVE");
    obs_data_set_string(settings,"upper_label","NOW PLAYING");obs_data_set_string(settings,"lower_label","LATEST");
    obs_data_set_string(settings,"upper_text","Armin van Buuren — Blah Blah Blah");obs_data_set_string(settings,"weather_text","23° Madrid");
    obs_data_set_string(settings,"lower_text","LAST FOLLOW — DJMUSIC123");obs_data_set_string(settings,"lower_text_2","LAST GIFTED — DJMike (5 Subs)");
    obs_data_set_string(settings,"lower_text_3","LAST RAID — TechnoFamily (125 viewers)");obs_data_set_string(settings,"lower_text_4","DAILY SUBS — 12");
    obs_data_t *reference_font=obs_data_create();obs_data_set_string(reference_font,"face","Arial");obs_data_set_int(reference_font,"size",18);obs_data_set_int(reference_font,"flags",0);obs_data_set_obj(settings,"font",reference_font);obs_data_release(reference_font);
    apply(s,settings);obs_property_button_clicked(obs_properties_get(p,"apply_palette"),NULL);settle();
    animation=fopen("validation/broadcast-preview.rgba","wb");timing=fopen("validation/broadcast-preview-timing.txt","w");if(!animation||!timing)return 18;
    obs_source_dec_showing(s);
    check("reference preset applies without replacing editable texts",obs_property_button_clicked(obs_properties_get(p,"apply_reference"),NULL));settle();
    obs_data_t *reference_settings=obs_source_get_settings(s);
    check("reference preset preserves names and independent band sizes",!strcmp(obs_data_get_string(reference_settings,"header_text"),"NEXUS")&&!strcmp(obs_data_get_string(reference_settings,"upper_text"),"Armin van Buuren — Blah Blah Blah")&&obs_data_get_int(reference_settings,"upper_height")==44&&obs_data_get_int(reference_settings,"lower_height")==30&&obs_data_get_int(reference_settings,"now_width")==350);
    obs_data_release(reference_settings);
    obs_data_set_int(settings,"upper_height",42);obs_data_set_int(settings,"lower_height",31);apply(s,settings);
    next=capture(s);FILE *reference_still=fopen("validation/broadcast-still.rgba","wb");if(!reference_still)return 19;
    fwrite(next,1,1370*73*4,reference_still);fclose(reference_still);free(next);obs_source_inc_showing(s);
    first=capture(s);bool moving_map=false,fixed_header=true;
    for(int frame=0;frame<50;frame++){
        next=capture(s);if(!region_equal(first,next,1370,1140,0,228,73))moving_map=true;
        if(!region_equal(first,next,1370,0,0,680,45))fixed_header=false;
        fwrite(next,1,1370*73*4,animation);fprintf(timing,"%llu\n",(unsigned long long)capture_time);free(next);os_sleep_ms(80);
    }
    fclose(animation);fclose(timing);free(first);
    check("reference map animates without moving the header or Now Playing",moving_map&&fixed_header);
    obs_data_set_int(settings,"motion",0);obs_data_set_bool(settings,"map_animated",false);apply(s,settings);first=capture(s);os_sleep_ms(250);next=capture(s);
    check("map animation can be disabled independently",region_equal(first,next,1370,1140,0,228,73));free(first);free(next);
    check("existing sources retain original map animation defaults",obs_data_get_int(settings,"map_speed")==100&&obs_data_get_int(settings,"map_intensity")==100);
    obs_data_set_bool(settings,"clock_enabled",false);obs_data_set_bool(settings,"map_animated",true);
    obs_data_set_int(settings,"map_intensity",0);obs_data_set_int(settings,"map_speed",200);apply(s,settings);
    first=capture(s);os_sleep_ms(300);next=capture(s);
    check("zero map intensity removes motion while preserving the map",region_equal(first,next,1370,1140,0,228,73));free(next);
    obs_data_set_bool(settings,"map_enabled",false);apply(s,settings);next=capture(s);
    check("zero intensity does not hide the map",!region_equal(first,next,1370,1140,0,228,73));free(first);free(next);
    obs_data_set_bool(settings,"map_enabled",true);obs_data_set_int(settings,"map_intensity",200);apply(s,settings);
    first=capture(s);os_sleep_ms(400);next=capture(s);
    check("stronger faster map animation leaves both text bands fixed",!region_equal(first,next,1370,1140,0,228,73)&&region_equal(first,next,1370,0,0,1000,73));free(first);free(next);
    disk=obs_data_create_from_json(obs_data_get_json(settings));third=obs_source_create_private("obs_news_ticker_nexus","Map controls reload",disk);settle();restored=obs_source_get_settings(third);
    check("map speed and intensity survive save and reload",obs_data_get_int(restored,"map_speed")==200&&obs_data_get_int(restored,"map_intensity")==200);
    obs_data_release(restored);obs_data_release(disk);obs_source_release(third);
    obs_data_set_int(settings,"style",2);apply(s,settings);obs_property_modified(obs_properties_get(p,"style"),settings);
    check("light style exposes text controls and hides all map controls",!obs_property_visible(obs_properties_get(p,"map_enabled"))&&!obs_property_visible(obs_properties_get(p,"map_animated"))&&!obs_property_visible(obs_properties_get(p,"map_speed"))&&!obs_property_visible(obs_properties_get(p,"map_intensity"))&&obs_property_visible(obs_properties_get(p,"upper_label")));
    check("light style selection preserves custom colors",obs_data_get_int(settings,"upper_bg")==0xff1b0c03);
    obs_property_button_clicked(obs_properties_get(p,"apply_reference"),NULL);settle();
    check("reference layout preserves selected light design and source texts",obs_data_get_int(settings,"style")==2&&!strcmp(obs_data_get_string(settings,"header_text"),"NEXUS")&&!strcmp(obs_data_get_string(settings,"upper_text"),"Armin van Buuren — Blah Blah Blah"));
    check("light palette provides dark text on pale panels",obs_data_get_int(settings,"upper_bg")==0xfff4e5d2&&obs_data_get_int(settings,"text_color")==0xff281608&&obs_data_get_int(settings,"header_color")==0xffd64c05);
    obs_data_set_int(settings,"upper_height",42);obs_data_set_int(settings,"lower_height",31);
    obs_data_set_bool(settings,"clock_enabled",false);obs_data_set_bool(settings,"map_animated",false);obs_data_set_int(settings,"motion",0);apply(s,settings);
    first=capture(s);obs_data_set_bool(settings,"map_enabled",false);apply(s,settings);next=capture(s);
    check("light design never renders or reserves space for a saved enabled map",region_equal(first,next,1370,0,0,1370,73));free(first);free(next);
    obs_data_set_bool(settings,"map_enabled",true);apply(s,settings);
    pixels=capture(s);FILE *light_preview=fopen("validation/light-preview.rgba","wb");if(!light_preview)return 20;fwrite(pixels,1,1370*73*4,light_preview);fclose(light_preview);
    bool light_alpha=true;for(size_t i=0;i<1370*73;i++){int a=pixels[i*4+3];if(pixels[i*4]>a||pixels[i*4+1]>a||pixels[i*4+2]>a)light_alpha=false;}
    check("light design preserves premultiplied alpha and transparent margins",light_alpha&&pixels[3]==0&&pixels[(1370*73-1)*4+3]==0);free(pixels);
    first=capture(s);obs_data_set_int(settings,"motion",1);apply(s,settings);os_sleep_ms(300);next=capture(s);
    check("light design scrolls news while Now Playing remains fixed",region_equal(first,next,1370,15,5,900,37)&&!region_equal(first,next,1370,250,42,700,26));free(first);free(next);
    disk=obs_data_create_from_json(obs_data_get_json(settings));third=obs_source_create_private("obs_news_ticker_nexus","Light design reload",disk);settle();restored=obs_source_get_settings(third);
    check("light design and palette survive save and reload",obs_data_get_int(restored,"style")==2&&obs_data_get_int(restored,"text_color")==0xff281608);obs_data_release(restored);obs_data_release(disk);obs_source_release(third);
    obs_data_set_int(settings,"style",11);obs_data_set_int(settings,"motion",1);
    obs_data_set_bool(settings,"clock_enabled",false);
    const char *compact_messages[]={"lower_text","lower_text_2","lower_text_3","lower_text_4","lower_text_5"};
    for(int i=0;i<5;i++)obs_data_set_string(settings,compact_messages[i],"");
    for(int i=0;i<7;i++)obs_data_set_bool(settings,feed_keys[i],i==0||i==3);
    strcpy(feed.account,"fixture-channel");strcpy(feed.latest[0].actor,"LiveFollower");strcpy(feed.latest[3].actor,"GiftActor");feed.latest[3].anonymous=0;emit_feed(&feed);
    apply(s,settings);os_sleep_ms(400);settle();first=capture(s);
    struct compact_reader compact_parts={0};obs_source_enum_active_sources(s,read_compact_text,&compact_parts);
    check("compact Twitch events render distinct actors and amounts",compact_parts.count==2&&!strcmp(compact_parts.text[0],"LiveFollower")&&!strcmp(compact_parts.text[1],"GiftActor (5)")&&lower_has("LiveFollower")&&lower_has("GiftActor (5)"));
    os_sleep_ms(250);next=capture(s);check("compact events scroll independently of fixed Now Playing",region_equal(first,next,1370,15,5,680,63)&&!region_equal(first,next,1370,780,5,540,63));free(first);free(next);
    obs_data_set_int(settings,"style",10);apply(s,settings);settle();pixels=capture(s);free(pixels);
    compact_parts=(struct compact_reader){0};obs_source_enum_active_sources(s,read_compact_text,&compact_parts);
    check("Ultra formats live events with its own category labels",compact_parts.count==2&&!strcmp(compact_parts.text[0],"FOLLOW LiveFollower")&&!strcmp(compact_parts.text[1],"GIFTED GiftActor (5)")&&lower_has("FOLLOW LiveFollower")&&lower_has("GIFTED GiftActor (5)"));
    raid.kind=NEXUS_STATE;raid.active=0;raid.authorized=0;emit(&raid);os_sleep_ms(400);settle();pixels=capture(s);free(pixels);
    compact_parts=(struct compact_reader){0};obs_source_enum_active_sources(s,read_compact_text,&compact_parts);
    check("compact events clear on shared Twitch disconnect",compact_parts.count==0&&!capture_lower[0]);
    signal_handler_disconnect(obs_source_get_signal_handler(s),"update_properties",count_twitch_properties,NULL);
    raid.kind=NEXUS_CLOSING;emit(&raid);
    frontend_emit(OBS_FRONTEND_EVENT_EXIT);
    FreeLibrary(fake_core);
    obs_properties_destroy(p);
    obs_source_dec_showing(second); obs_source_release(second);
    obs_data_release(settings);
    obs_source_dec_showing(s); obs_source_release(s);
    obs_shutdown();
    check("module unload unregisters frontend callback",frontend_count()==0);
    FreeLibrary(fake_frontend);
    printf("RESULT checks=%d failures=%d\n",checks,failures);
    return failures?1:0;
}



