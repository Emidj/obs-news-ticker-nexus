/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <sys/stat.h>
#include <util/platform.h>
#include "../shared/twitch-core-runtime.h"
#include <util/bmem.h>
#include <graphics/vec4.h>
#include <windows.h>
#include <shellapi.h>
#include <math.h>
#include <string.h>
#include <ctype.h>
#include "block-animation.h"
#include "ping-pong.h"
#include "local-clock.h"
#include "now-playing.h"
#include "weather.h"
#include "twitch-observer.h"
#include "ticker-version.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-news-ticker-nexus", "en-US")
MODULE_EXPORT const char *obs_module_name(void) { return "OBS News Ticker NEXUS"; }
MODULE_EXPORT const char *obs_module_author(void) { return "DjEmiMas"; }
MODULE_EXPORT const char *obs_module_description(void) { return "Native news ticker with Now Playing, World Weather, Twitch and Hype Train breaking news"; }

enum { HEADER, UPPER, LOWER, SUBTITLE, UPPER_LABEL, LOWER_LABEL, CLOCK, WEATHER, MUSIC, HYPE_BADGE, HYPE_MESSAGE, HYPE_LEVEL, HYPE_PERCENT, HYPE_TIME, TEXT_COUNT };
#include "ticker-style.h"
#include "reference-profiles.h"
enum { MOTION_STATIC, MOTION_SCROLL, MOTION_BLOCKS, MOTION_ALTERNATE };
static bool style_has_map(int style);
struct ticker;
static void compact_stream(struct ticker *t,float x,float y,float available,float band);
static const char *legacy_weather_city_keys[]={"weather_city","weather_city_2","weather_city_3","weather_city_4","weather_city_5"};
static const char *message_keys[]={"lower_text","lower_text_2","lower_text_3","lower_text_4","lower_text_5"};
struct ticker {
    SRWLOCK lock;
    obs_source_t *self;
    obs_source_t *text[TEXT_COUNT];
    bool has_text[TEXT_COUNT];
    int style;
    int motion;
    uint32_t scroll_speed, scroll_gap;
    uint32_t block_transition, block_hold, block_pause;
    double block_time[2];
    uint32_t message_hold, message_fade;
    double alternate_time;
    char *messages[5];
    char *joined_news;
    int valid_messages[5],message_count,message_index;
    uint8_t message_wait;
    bool raid_rotate;
    double raid_poll;
    struct twitch_snapshot twitch_seen;
    NexusTickerSnapshot feed_seen;
    unsigned twitch_flags;
    int event_count;
    int event_kind[7];
    char event_text[7][512];
    obs_source_t *compact_items[12];
    gs_texrender_t *compact_capture[12];
    bool compact_captured[12];
    uint8_t compact_refresh[12];
    int compact_kind[12],compact_count;
    gs_effect_t *fade_effect;
    gs_texture_t *reference_map,*design_atlas,*compact_atlas;
    bool shared_graphics;
    bool reference_caption;
    bool map_enabled,map_animated;
    uint32_t map_speed,map_intensity;
    double map_time;
    bool clock_enabled,clock_twelve,clock_seconds;
    bool info_right;
    bool hype_enabled,hype_preview,hype_blink,hype_custom;
    uint32_t hype_blink_ms;
    uint32_t hype_width,hype_height,hype_fg,hype_bg,hype_accent;
    float hype_alpha;double hype_clock,hype_demo_end;
    char hype_text[5][64];
    gs_texrender_t *hype_render;
    uint32_t weather_width;
    float clock_layout_width,clock_layout_height;
    double clock_poll;
    char clock_value[32];
    bool weather_auto;
    struct weather_session *weather_sessions[WEATHER_CITIES];
    bool weather_rotating;
    unsigned weather_interval;
    struct weather_rotation weather_rotation;
    enum weather_status weather_status;
    char *weather_cities[WEATHER_CITIES];
    char weather_value[512],weather_pending[512];
    float weather_alpha;
    unsigned weather_stage,weather_wait;
    double weather_poll;
    int upper_mode;
    bool now_fallback;
    uint32_t now_max_age;
    double now_poll;
    char *now_path,*manual_upper;
    struct np_session *now_session;
    enum np_status now_status;
    uint8_t now_wait;
    char now_value[1024];
    uint32_t now_width;
    bool now_ping_pong;
    uint32_t now_speed,now_pause;
    double now_time;
    float now_alpha;
    int now_stage;
    char now_pending[1024];
    double phase[2], period[2];
    char *content[2];
    gs_texrender_t *clip[2];
    gs_texrender_t *text_capture[TEXT_COUNT];
    bool text_captured[TEXT_COUNT];
    float caption_top[TEXT_COUNT],caption_height[TEXT_COUNT];
    uint8_t refresh[TEXT_COUNT];
    uint32_t width, upper_height, lower_height, header_bg, upper_bg, lower_bg;
    uint32_t upper_color,lower_color;
};
static struct {gs_texture_t *map,*design,*compact;unsigned users;} shared_graphics;
static void prepare_compact_items(struct ticker *t,bool force);
static uint32_t bounded(long long n, uint32_t lo, uint32_t hi)
{ return n < lo ? lo : n > hi ? hi : (uint32_t)n; }
static const char *name(void *unused)
{ UNUSED_PARAMETER(unused); return "OBS News Ticker NEXUS"; }
static void select_weather(struct ticker *t,double seconds,bool showing,char *text,size_t size)
{
    struct weather_sample samples[WEATHER_CITIES];bool ready[WEATHER_CITIES]={false};
    unsigned count=t->weather_rotating?WEATHER_CITIES:1;
    for(unsigned i=0;i<count;i++){
        weather_read(t->weather_sessions[i],&samples[i]);
        if(!t->weather_sessions[i]&&t->weather_cities[i]&&*t->weather_cities[i])samples[i].status=WEATHER_LOCATION;
        ready[i]=samples[i].status==WEATHER_READY;
        for(unsigned j=0;j<i;j++)if(t->weather_sessions[i]&&t->weather_sessions[i]==t->weather_sessions[j])ready[i]=false;
    }
    unsigned selected=weather_rotate(&t->weather_rotation,ready,count,seconds,showing,t->weather_interval);
    t->weather_status=samples[selected].status;weather_format(&samples[selected],np_now(),text,size);
}
static obs_data_array_t *default_weather_locations(void)
{
    obs_data_array_t *list=obs_data_array_create();
    for(unsigned i=0;i<weather_capital_count();i++){
        obs_data_t *entry=obs_data_create();obs_data_set_string(entry,"value",weather_capital_selection(i));obs_data_array_push_back(list,entry);obs_data_release(entry);
    }
    return list;
}
static void migrate_weather_locations(obs_data_t *settings)
{
    if(obs_data_has_user_value(settings,"weather_locations"))return;
    bool legacy=false;
    for(unsigned i=0;i<5;i++)if(obs_data_has_user_value(settings,legacy_weather_city_keys[i]))legacy=true;
    obs_data_array_t *list=legacy?obs_data_array_create():obs_data_get_array(settings,"weather_locations");
    if(legacy){
        unsigned count=0;for(unsigned i=0;i<5;i++)if(*obs_data_get_string(settings,legacy_weather_city_keys[i]))count=i+1;
        for(unsigned i=0;i<count;i++){
            const char *city=obs_data_get_string(settings,legacy_weather_city_keys[i]);
            obs_data_t *entry=obs_data_create();obs_data_set_string(entry,"value",city);obs_data_array_push_back(list,entry);obs_data_release(entry);
        }
        if(!obs_data_has_user_value(settings,"weather_rotate"))obs_data_set_bool(settings,"weather_rotate",false);
    }
    obs_data_set_array(settings,"weather_locations",list);obs_data_array_release(list);
}
static void set_weather_text(struct ticker *t,const char *value)
{
    snprintf(t->weather_value,sizeof(t->weather_value),"%s",value);
    obs_data_t *settings=obs_source_get_settings(t->text[WEATHER]);obs_data_set_string(settings,"text",value);obs_source_update(t->text[WEATHER],settings);obs_data_release(settings);
    t->has_text[WEATHER]=*value!=0;t->text_captured[WEATHER]=false;t->refresh[WEATHER]=2;t->weather_wait=2;
}
static void measure_clock(struct ticker *t)
{
    obs_data_t *settings=obs_source_get_settings(t->text[CLOCK]),*font=obs_data_get_obj(settings,"font");
    int size=(int)obs_data_get_int(font,"size"),flags=(int)obs_data_get_int(font,"flags");wchar_t face[LF_FACESIZE]=L"Arial";
    MultiByteToWideChar(CP_UTF8,0,obs_data_get_string(font,"face"),-1,face,LF_FACESIZE);
    HDC dc=CreateCompatibleDC(NULL);HFONT handle=CreateFontW(-size,0,0,0,flags&OBS_FONT_BOLD?FW_BOLD:FW_NORMAL,!!(flags&OBS_FONT_ITALIC),false,false,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,face);
    t->clock_layout_width=(float)(size*(t->clock_seconds?8:5));t->clock_layout_height=(float)size*1.3f;
    if(dc&&handle){
        HGDIOBJ old=SelectObject(dc,handle);TEXTMETRICW metric={0};GetTextMetricsW(dc,&metric);int maximum=0;
        for(int digit=0;digit<10;digit++)for(int pm=0;pm<2;pm++){
            wchar_t sample[24];swprintf(sample,24,L"%ls%ls",t->clock_seconds?L"88:88:88":L"88:88",t->clock_twelve?(pm?L" PM":L" AM"):L"");
            for(wchar_t *c=sample;*c;c++)if(*c==L'8')*c=(wchar_t)(L'0'+digit);
            SIZE measured={0};GetTextExtentPoint32W(dc,sample,(int)wcslen(sample),&measured);if(measured.cx>maximum)maximum=measured.cx;
        }
        t->clock_layout_width=(float)maximum+size*.5f;t->clock_layout_height=fmaxf(1,(float)metric.tmHeight);SelectObject(dc,old);
    }
    if(handle)DeleteObject(handle);if(dc)DeleteDC(dc);obs_data_release(font);obs_data_release(settings);
}
static const char *upper_value(struct ticker *t)
{
    if(!t->upper_mode)return t->manual_upper?t->manual_upper:"";
    if(t->now_status==NP_PLAYING)return t->now_value;
    return t->now_fallback && t->manual_upper?t->manual_upper:"";
}
static void set_now_text(struct ticker *t,const char *value)
{
    t->now_time=0;
    bfree(t->content[0]);t->content[0]=bstrdup(value);
    obs_data_t *s=obs_source_get_settings(t->text[UPPER]);
    obs_data_set_string(s,"text",value);obs_source_update(t->text[UPPER],s);obs_data_release(s);
    t->has_text[UPPER]=*value!='\0';t->text_captured[UPPER]=false;t->refresh[UPPER]=2;t->now_wait=2;
}
static void refresh_now_playing(struct ticker *t)
{
    if(!t->upper_mode)return;
    if(t->now_session||t->upper_mode==2) {
        struct np_sample sample;if(t->upper_mode==2)np_read_playing(&sample);else np_read(t->now_session,&sample);
        t->now_status=np_current_status(&sample,np_now(),t->now_max_age);
        snprintf(t->now_value,sizeof(t->now_value),"%s",t->now_status==NP_PLAYING?sample.text:"");
    }
    const char *value=upper_value(t);
    if(t->now_status!=NP_PLAYING){
        t->now_stage=0;t->now_alpha=1;t->now_pending[0]=0;
        if(!t->content[0]||strcmp(t->content[0],value))set_now_text(t,value);
        return;
    }
    if(t->content[0]&&!strcmp(t->content[0],value)){
        if(t->now_stage==1){t->now_stage=3;t->now_pending[0]=0;}
        return;
    }
    snprintf(t->now_pending,sizeof(t->now_pending),"%s",value);
    if(t->content[0]&&*t->content[0])t->now_stage=1;
    else {set_now_text(t,t->now_pending);t->now_alpha=0;t->now_stage=2;}
}
static void format_local_time(struct ticker *t,char *out,size_t size)
{
    SYSTEMTIME now;GetLocalTime(&now);
    local_clock_format(out,size,now.wHour,now.wMinute,now.wSecond,t->clock_twelve,t->clock_seconds);
}
static void refresh_clock(struct ticker *t)
{
    if(!t->clock_enabled) return;
    char value[32];format_local_time(t,value,sizeof(value));
    if(!strcmp(t->clock_value,value)) return;
    snprintf(t->clock_value,sizeof(t->clock_value),"%s",value);
    obs_data_t *s=obs_source_get_settings(t->text[CLOCK]);
    obs_data_set_string(s,"text",value);obs_source_update(t->text[CLOCK],s);obs_data_release(s);
    t->text_captured[CLOCK]=false;t->refresh[CLOCK]=2;t->has_text[CLOCK]=true;
}
static bool nonblank(const char *text)
{
    for(;*text;text++) if(!isspace((unsigned char)*text)) return true;
    return false;
}
static int sequence_count(struct ticker *t)
{
    return t->motion==MOTION_BLOCKS?t->message_count+t->event_count:t->joined_news&&*t->joined_news?1:0;
}
static const char *active_message(struct ticker *t)
{
    if(t->motion!=MOTION_BLOCKS)return t->joined_news?t->joined_news:"";
    int count=sequence_count(t);
    if(!count)return "";
    int index=t->message_index%count;
    if(index<t->event_count)return t->event_text[index];
    index-=t->event_count;
    return t->messages[t->valid_messages[index]];
}
static void rebuild_news(struct ticker *t)
{
    const char *items[12];int count=0;size_t length=0;
    const char *separator=t->style<=STYLE_NEWS?"   •   ":"   |   ";size_t gap=strlen(separator);
    for(int i=0;i<t->event_count;i++)items[count++]=t->event_text[i];
    for(int i=0;i<t->message_count;i++)items[count++]=t->messages[t->valid_messages[i]];
    for(int i=0;i<count;i++)length+=strlen(items[i])+(i?gap:0);
    if(t->joined_news&&strlen(t->joined_news)==length){
        const char *cursor=t->joined_news;bool same=true;
        for(int i=0;i<count;i++){
            if(i){if(memcmp(cursor,separator,gap))same=false;cursor+=gap;}
            size_t n=strlen(items[i]);if(memcmp(cursor,items[i],n))same=false;cursor+=n;
        }
        if(same)return;
    }
    char *value=bmalloc(length+1),*cursor=value;
    for(int i=0;i<count;i++){
        if(i){memcpy(cursor,separator,gap);cursor+=gap;}
        size_t n=strlen(items[i]);memcpy(cursor,items[i],n);cursor+=n;
    }
    *cursor=0;bfree(t->joined_news);t->joined_news=value;
}
static void read_raid(struct ticker *t)
{
    struct twitch_snapshot snapshot;twitch_observer_read(&snapshot);
    NexusTickerSnapshot feed;twitch_observer_read_feed(&feed);t->event_count=0;
    const char *prefix[]={"LAST FOLLOW — ","LAST SUB — ","LAST CHEER — ","LAST GIFTED — ","LAST RAID — "};
    const unsigned ultra_order[]={0,3,1,4,2,5,6};
    for(unsigned position=0;position<7;position++){
        unsigned i=t->style==STYLE_ULTRA?ultra_order[position]:position;
        if(!(t->twitch_flags&(1u<<i)))continue;
        char *out=t->event_text[t->event_count];out[0]=0;
        if(i<5&&feed.size&&feed.latest[i].present){
            const NexusTickerEvent *e=&feed.latest[i];const char *actor=e->anonymous?"Anonymous":e->actor;
            if(i==2||i==3||i==4)snprintf(out,512,"%s%s — %llu",prefix[i],actor,(unsigned long long)e->amount);
            else snprintf(out,512,"%s%s",prefix[i],actor);
        }else if(i==4&&!feed.size&&snapshot.status==TWITCH_READY&&snapshot.last_raid[0])
            snprintf(out,512,"%s%s",prefix[4],snapshot.last_raid);
        else if(i>=5&&feed.size)
            snprintf(out,512,"%s%llu",(i==5?"DAILY SUBS (recorded) — ":"DAILY BITS (recorded) — "),(unsigned long long)(i==5?feed.daily_subs:feed.daily_bits));
        if(out[0]){
            if(compact_style(t->style)&&i<5&&feed.size&&feed.latest[i].present){
                const NexusTickerEvent *e=&feed.latest[i];const char *actor=e->anonymous?"Anonymous":e->actor;
                if(t->style==STYLE_ULTRA){
                    const char *category[]={"FOLLOW","SUB","CHEER","GIFTED","RAID"};
                    if(i==2||i==3||i==4)snprintf(out,512,"%s %s (%llu)",category[i],actor,(unsigned long long)e->amount);else snprintf(out,512,"%s %s",category[i],actor);
                }else if(i==2||i==3||i==4)snprintf(out,512,"%s (%llu)",actor,(unsigned long long)e->amount);else snprintf(out,512,"%s",actor);
            }
            t->event_kind[t->event_count]=(int)i;t->event_count++;
        }
    }
    rebuild_news(t);
}
static void refresh_raid(struct ticker *t,bool new_event)
{
    int previous_count=t->event_count;
    read_raid(t);
    if(t->style!=STYLE_BASIC)prepare_compact_items(t,false);
    if(new_event||previous_count!=t->event_count)t->message_index=0;
    const char *value=active_message(t);
    if(!new_event&&t->content[1]&&!strcmp(t->content[1],value))return;
    bfree(t->content[1]);t->content[1]=bstrdup(value);
    obs_data_t *s=obs_source_get_settings(t->text[LOWER]);
    obs_data_set_string(s,"text",value);obs_source_update(t->text[LOWER],s);obs_data_release(s);
    t->has_text[LOWER]=*value!='\0';t->text_captured[LOWER]=false;
    t->refresh[LOWER]=2;t->message_wait=2;
    t->phase[1]=0;t->block_time[1]=0;t->alternate_time=0;
}
static void next_message(struct ticker *t,int count)
{
    int total=sequence_count(t);
    if(total<2 || count%total==0) return;
    t->message_index=(t->message_index+count)%total;
    const char *message=active_message(t);
    bfree(t->content[1]);t->content[1]=bstrdup(message);
    obs_data_t *s=obs_source_get_settings(t->text[LOWER]);
    obs_data_set_string(s,"text",message);obs_source_update(t->text[LOWER],s);obs_data_release(s);
    t->text_captured[LOWER]=false;t->refresh[LOWER]=2;t->message_wait=2;
    t->has_text[LOWER]=*message!='\0';
}
static void advance_messages(struct ticker *t,double old,double elapsed,double hidden_start,double cycle)
{
    double steps=floor((old+elapsed-hidden_start)/cycle)-floor((old-hidden_start)/cycle);
    int total=sequence_count(t);
    if(steps>0 && total>1) next_message(t,(int)fmod(steps,total));
}
static void migrate_heights(obs_data_t *s)
{
    /* The original setting was a total split in half, including odd totals.
     * Persist the split once; explicit per-band settings always take priority. */
    if(!obs_data_has_user_value(s,"height")) return;
    uint32_t legacy=bounded(obs_data_get_int(s,"height"),48,400);
    if(!obs_data_has_user_value(s,"upper_height")) obs_data_set_int(s,"upper_height",legacy/2);
    if(!obs_data_has_user_value(s,"lower_height")) obs_data_set_int(s,"lower_height",legacy-legacy/2);
}
static void defaults(obs_data_t *s)
{
    obs_data_set_default_int(s,"width",1920);
    obs_data_set_default_int(s,"upper_height",50);
    obs_data_set_default_int(s,"lower_height",50);
    obs_data_set_default_int(s,"style",STYLE_BASIC);
    obs_data_set_default_int(s,"motion",MOTION_STATIC);
    obs_data_set_default_int(s,"scroll_speed",90);
    obs_data_set_default_int(s,"scroll_gap",80);
    obs_data_set_default_int(s,"block_transition",500);
    obs_data_set_default_int(s,"block_hold",3000);
    obs_data_set_default_int(s,"block_pause",600);
    obs_data_set_default_int(s,"message_hold",5000);
    obs_data_set_default_int(s,"message_fade",300);
    obs_data_set_default_bool(s,"clock_enabled",false);
    obs_data_set_default_int(s,"clock_format",0);
    obs_data_set_default_bool(s,"info_align_right",false);
    obs_data_set_default_int(s,"weather_width",320);
    obs_data_set_default_bool(s,"clock_seconds",false);
    obs_data_set_default_int(s,"upper_mode",0);
    obs_data_set_default_int(s,"now_width",600);
    obs_data_set_default_bool(s,"hype_enabled",false);
    obs_data_set_default_bool(s,"hype_preview",false);
    obs_data_set_default_bool(s,"hype_blink",true);
    obs_data_set_default_int(s,"hype_blink_ms",700);
    obs_data_set_default_bool(s,"hype_custom_colors",false);
    obs_data_set_default_int(s,"hype_width",0);
    obs_data_set_default_int(s,"hype_height",28);
    obs_data_set_default_int(s,"hype_color",0xffffffff);
    obs_data_set_default_int(s,"hype_bg",0xff180770);
    obs_data_set_default_int(s,"hype_accent",0xff1515db);
    obs_data_set_default_int(s,"now_motion",0);
    obs_data_set_default_int(s,"now_speed",40);
    obs_data_set_default_int(s,"now_pause",1000);
    obs_data_set_default_bool(s,"map_enabled",true);
    obs_data_set_default_bool(s,"map_animated",true);
    obs_data_set_default_int(s,"map_speed",100);
    obs_data_set_default_int(s,"map_intensity",100);
    obs_data_set_default_string(s,"weather_text","");
    obs_data_set_default_int(s,"weather_mode",0);
    obs_data_array_t *locations=default_weather_locations();obs_data_set_default_array(s,"weather_locations",locations);obs_data_array_release(locations);
    obs_data_set_default_bool(s,"weather_rotate",true);
    obs_data_set_default_int(s,"weather_interval",15);
    obs_data_set_default_bool(s,"raid_enabled",false);
    obs_data_set_default_bool(s,"raid_rotate",false);
    obs_data_set_default_string(s,"now_path","");
    obs_data_set_default_int(s,"now_max_age",15);
    obs_data_set_default_bool(s,"now_fallback",false);
    migrate_heights(s);
    obs_data_set_default_string(s,"header_text","LIVE");
    obs_data_set_default_string(s,"upper_text","NOW PLAYING  |  Manual preview");
    obs_data_set_default_string(s,"lower_text","Your custom message");
    for(int i=1;i<5;i++) obs_data_set_default_string(s,message_keys[i],"");
    obs_data_set_default_string(s,"header_subtitle","ON AIR");
    obs_data_set_default_string(s,"upper_label","NOW PLAYING");
    obs_data_set_default_string(s,"lower_label","LATEST");
    obs_data_set_default_int(s,"header_color",0xffffffff);
    obs_data_set_default_int(s,"text_color",0xffffffff);
    obs_data_set_default_int(s,"lower_text_color",obs_data_get_int(s,"text_color"));
    obs_data_set_default_int(s,"header_bg",0xff2424b8);
    obs_data_set_default_int(s,"upper_bg",0xf021140b);
    obs_data_set_default_int(s,"lower_bg",0xf0332012);
    /* Defaults can be applied again to settings retained by obs_save_source.
     * Preserve the existing nested object rather than replacing its lifetime. */
    obs_data_t *font=obs_data_get_default_obj(s,"font");
    if(!font) {
        font=obs_data_create();
        obs_data_set_default_string(font,"face","Arial");
        obs_data_set_default_int(font,"size",26);
        obs_data_set_default_int(font,"flags",0);
        obs_data_set_default_obj(s,"font",font);
    }
    obs_data_release(font);
}
static void hype_palette(struct ticker *t,obs_data_t *settings)
{
 t->hype_fg=0xffffffff;t->hype_bg=0xff180770;t->hype_accent=0xff1515db;
 switch(t->style){
 case STYLE_BASIC:t->hype_bg=0xff201710;break;
 case STYLE_WORLD:case STYLE_BUSINESS:case STYLE_MAP:t->hype_bg=0xff48220a;t->hype_accent=0xffef7500;break;
 case STYLE_SPORTS:t->hype_bg=0xff742500;t->hype_accent=0xffe36300;break;
 case STYLE_CLEAN:case STYLE_ULTRA:t->hype_bg=0xff281710;t->hype_accent=0xffe02d48;break;
 case STYLE_RED:t->hype_bg=0xff0c0707;break;
 case STYLE_LIGHT:t->hype_fg=0xff281008;t->hype_bg=0xfff7e9dc;t->hype_accent=0xfffff9f5;break;
 case STYLE_GLASS:t->hype_bg=0xff773d18;t->hype_accent=0xffffa133;break;
 case STYLE_NEON:t->hype_bg=0xff410b28;t->hype_accent=0xffed11df;break;
 case STYLE_SPORT:t->hype_fg=0xff100d0a;t->hype_bg=0xfff8f4ef;t->hype_accent=0xffeeeeee;break;
 case STYLE_MAGAZINE:t->hype_bg=0xff650e3a;t->hype_accent=0xffff25b5;break;
 case STYLE_DARK:t->hype_bg=0xff151710;t->hype_accent=0xff1ac4ad;break;
 default:break;
 }
 if(obs_data_get_bool(settings,"hype_custom_colors")){t->hype_fg=(uint32_t)obs_data_get_int(settings,"hype_color");t->hype_bg=(uint32_t)obs_data_get_int(settings,"hype_bg");t->hype_accent=(uint32_t)obs_data_get_int(settings,"hype_accent");}
}
static void hype_value(struct ticker *t,int slot,const char *value)
{
 if(!strcmp(t->hype_text[slot],value))return;
 snprintf(t->hype_text[slot],sizeof(t->hype_text[slot]),"%s",value);
 int index=HYPE_BADGE+slot;t->has_text[index]=*value!=0;
 obs_data_t *settings=obs_source_get_settings(t->text[index]);obs_data_set_string(settings,"text",value);obs_source_update(t->text[index],settings);obs_data_release(settings);
 t->text_captured[index]=false;t->refresh[index]=2;
}
static void hype_tick(struct ticker *t,float seconds)
{
 if(!t->hype_enabled){t->hype_alpha=0;return;}
 t->hype_clock=fmod(t->hype_clock+seconds,fmax(.2,t->hype_blink_ms/1000.));
 NexusHypeSnapshot h;bool panel_preview=false;twitch_observer_read_hype_mode(&h,&panel_preview);double now=np_now();
 bool valid=t->hype_enabled&&h.present&&h.active&&(h.authorized||panel_preview)&&h.goal&&h.level&&h.expires_ms/1000.>now;
 if(t->hype_enabled&&t->hype_preview&&!panel_preview){if(t->hype_demo_end<=now)t->hype_demo_end=now+134;h.level=3;h.progress=67;h.goal=100;h.expires_ms=(int64_t)(t->hype_demo_end*1000);valid=true;}
 float target=valid?1:0;t->hype_alpha+=fmaxf(-seconds/.25f,fminf(seconds/.25f,target-t->hype_alpha));
 if(!valid)return;
 char level[64],percent[64],remaining[64];
 unsigned pct=(unsigned)fmin(100.,floor(100.*(double)h.progress/(double)h.goal));
 unsigned left=(unsigned)fmin(59999.,ceil(fmax(0,h.expires_ms/1000.-now)));
 snprintf(level,sizeof(level),"LEVEL %u",h.level);snprintf(percent,sizeof(percent),"%u%%",pct);snprintf(remaining,sizeof(remaining),"%02u:%02u",left/60,left%60);
 hype_value(t,0,"BREAKING NEWS");hype_value(t,1,panel_preview?"TEST · WE HAVE A HYPE TRAIN":"WE HAVE A HYPE TRAIN");hype_value(t,2,level);hype_value(t,3,percent);hype_value(t,4,remaining);
}
#include "hype-anchor.h"
static void update(void *data, obs_data_t *s)
{
    struct ticker *t=data;
    struct hype_anchor_request *anchor=begin_hype_anchor(t,s);
    long long selected_style=obs_data_get_int(s,"style");
    int next_style=selected_style>=STYLE_BASIC&&selected_style<=STYLE_DARK?(int)selected_style:STYLE_BASIC;
    obs_data_set_int(s,"style_group",compact_style(next_style)?1:0);
    const char *keys[]={"header_text","upper_text","lower_text","header_subtitle","upper_label","lower_label","clock_text","weather_text","music_icon","hype_badge","hype_message","hype_level","hype_percent","hype_time"};
    migrate_heights(s);
    AcquireSRWLockExclusive(&t->lock);
    t->style=next_style;
    bool automatic=obs_data_get_int(s,"weather_mode")==1&&t->style!=STYLE_BASIC;
    migrate_weather_locations(s);
    char *queries[WEATHER_CITIES];obs_data_array_t *locations=obs_data_get_array(s,"weather_locations");
    size_t location_count=obs_data_array_count(locations);
    for(unsigned i=0;i<WEATHER_CITIES;i++){
        obs_data_t *entry=i<location_count?obs_data_array_item(locations,i):NULL;queries[i]=bstrdup(obs_data_get_string(entry,"value"));obs_data_release(entry);
    }
    obs_data_array_release(locations);
    bool rotating=obs_data_get_bool(s,"weather_rotate");
    bool weather_changed=automatic!=t->weather_auto||rotating!=t->weather_rotating;
    for(unsigned i=0;i<WEATHER_CITIES;i++)if(!t->weather_cities[i]||strcmp(t->weather_cities[i],queries[i]))weather_changed=true;
    if(weather_changed){
        struct weather_session *next[WEATHER_CITIES]={0};
        for(unsigned i=0;i<WEATHER_CITIES;i++){
            const char *city=queries[i];
            if(automatic&&(rotating||i==0)&&nonblank(city)){
                for(unsigned j=0;j<WEATHER_CITIES;j++)if(t->weather_sessions[j]&&!_stricmp(city,t->weather_cities[j])){next[i]=weather_acquire(city);break;}
            }
        }
        for(unsigned i=0;i<WEATHER_CITIES;i++){weather_release(t->weather_sessions[i]);t->weather_sessions[i]=NULL;}
        for(unsigned i=0;i<WEATHER_CITIES;i++){
            const char *city=queries[i];
            if(!next[i]&&automatic&&(rotating||i==0)&&nonblank(city))next[i]=weather_acquire(city);
            t->weather_sessions[i]=next[i];
            bfree(t->weather_cities[i]);t->weather_cities[i]=bstrdup(queries[i]);
        }
        t->weather_rotation=(struct weather_rotation){0};t->weather_value[0]=0;
        t->weather_status=automatic?WEATHER_WAITING:WEATHER_MANUAL;
    }
    t->weather_auto=automatic;t->weather_rotating=rotating;
    t->weather_interval=bounded(obs_data_get_int(s,"weather_interval"),5,300);if(weather_changed)t->weather_poll=0;
    if(weather_changed){t->weather_alpha=1;t->weather_stage=0;t->weather_pending[0]=0;}
    if(weather_changed&&automatic)select_weather(t,0,false,t->weather_value,sizeof(t->weather_value));
    for(unsigned i=0;i<WEATHER_CITIES;i++)bfree(queries[i]);
    bool ping_pong=obs_data_get_int(s,"now_motion")==1;
    uint32_t now_width=bounded(obs_data_get_int(s,"now_width"),80,7680),now_speed=bounded(obs_data_get_int(s,"now_speed"),10,200),now_pause=bounded(obs_data_get_int(s,"now_pause"),0,5000);
    if(t->now_ping_pong!=ping_pong||t->now_width!=now_width||t->now_speed!=now_speed||t->now_pause!=now_pause)t->now_time=0;
    t->now_ping_pong=ping_pong;t->now_width=now_width;t->now_speed=now_speed;t->now_pause=now_pause;
    t->map_enabled=obs_data_get_bool(s,"map_enabled");t->map_animated=obs_data_get_bool(s,"map_animated");
    t->map_speed=bounded(obs_data_get_int(s,"map_speed"),25,200);
    t->map_intensity=bounded(obs_data_get_int(s,"map_intensity"),0,200);
    if(!t->content[0])t->now_alpha=1;
    const char *event_keys[]={"twitch_follow","twitch_sub","twitch_cheer","twitch_gift","raid_enabled","twitch_daily_subs","twitch_daily_bits"};
    unsigned flags=0;for(unsigned i=0;i<7;i++)if(obs_data_get_bool(s,event_keys[i]))flags|=1u<<i;
    bool selection_changed=t->twitch_flags!=flags||t->raid_rotate!=obs_data_get_bool(s,"raid_rotate");
    t->twitch_flags=flags;t->raid_rotate=obs_data_get_bool(s,"raid_rotate");read_raid(t);t->raid_poll=0;
    const char *manual=obs_data_get_string(s,"upper_text"),*path=obs_data_get_string(s,"now_path");
    bfree(t->manual_upper);t->manual_upper=bstrdup(manual);
    long long requested_mode=obs_data_get_int(s,"upper_mode");int upper_mode=requested_mode==1||requested_mode==2?(int)requested_mode:0;
    if(upper_mode!=t->upper_mode || !t->now_path || strcmp(t->now_path,path)) {
        t->now_alpha=1;t->now_stage=0;t->now_pending[0]=0;
        np_release(t->now_session);t->now_session=NULL;
        bfree(t->now_path);t->now_path=bstrdup(path);t->now_value[0]=0;
        if(upper_mode==1 && *path)t->now_session=np_acquire(path);
        t->now_status=!upper_mode?NP_DISABLED:upper_mode==2?NP_WAITING:!*path?NP_WAITING:t->now_session?NP_WAITING:NP_BAD_PATH;
        t->now_wait=2;
    }
    t->upper_mode=upper_mode;t->now_fallback=obs_data_get_bool(s,"now_fallback");
    t->now_max_age=bounded(obs_data_get_int(s,"now_max_age"),3,300);t->now_poll=.25;
    t->info_right=obs_data_get_bool(s,"info_align_right");t->weather_width=bounded(obs_data_get_int(s,"weather_width"),80,2000);
    t->clock_enabled=obs_data_get_bool(s,"clock_enabled");
    t->clock_twelve=obs_data_get_int(s,"clock_format")==1;
    t->clock_seconds=obs_data_get_bool(s,"clock_seconds");
    if(t->clock_enabled) format_local_time(t,t->clock_value,sizeof(t->clock_value)); else t->clock_value[0]='\0';
    t->clock_poll=0;
    long long requested_motion=obs_data_get_int(s,"motion");
    int motion=requested_motion>=MOTION_STATIC && requested_motion<=MOTION_ALTERNATE?(int)requested_motion:MOTION_STATIC;
    bool mode_changed=t->motion!=motion;
    bool list_changed=selection_changed;
    t->message_count=0;
    for(int i=0;i<5;i++) {
        const char *value=obs_data_get_string(s,message_keys[i]);
        if(!t->messages[i] || strcmp(t->messages[i],value)) {
            bfree(t->messages[i]);t->messages[i]=bstrdup(value);list_changed=true;
        }
        if(nonblank(value)) t->valid_messages[t->message_count++]=i;
    }
    rebuild_news(t);
    uint32_t message_hold=bounded(obs_data_get_int(s,"message_hold"),500,30000);
    uint32_t message_fade=bounded(obs_data_get_int(s,"message_fade"),100,1000);
    if(mode_changed || list_changed || t->message_hold!=message_hold || t->message_fade!=message_fade) {
        t->message_index=0;t->alternate_time=0;t->block_time[1]=0;t->phase[1]=0;t->message_wait=2;
    }
    t->message_hold=message_hold;t->message_fade=message_fade;
    uint32_t transition=bounded(obs_data_get_int(s,"block_transition"),150,2000);
    uint32_t hold=bounded(obs_data_get_int(s,"block_hold"),500,20000);
    uint32_t pause=bounded(obs_data_get_int(s,"block_pause"),100,5000);
    if(t->motion!=motion) { t->phase[0]=t->phase[1]=0; t->block_time[0]=t->block_time[1]=0; }
    if(t->block_transition!=transition || t->block_hold!=hold || t->block_pause!=pause) t->block_time[0]=t->block_time[1]=0;
    t->block_transition=transition; t->block_hold=hold; t->block_pause=pause;
    t->motion=motion;
    t->scroll_speed=bounded(obs_data_get_int(s,"scroll_speed"),10,400);
    t->scroll_gap=bounded(obs_data_get_int(s,"scroll_gap"),16,600);
    t->width=bounded(obs_data_get_int(s,"width"),320,7680);
    bool preview=obs_data_get_bool(s,"hype_preview");if(preview!=t->hype_preview)t->hype_demo_end=0;
    t->hype_enabled=obs_data_get_bool(s,"hype_enabled");t->hype_preview=preview;t->hype_blink=obs_data_get_bool(s,"hype_blink");t->hype_blink_ms=bounded(obs_data_get_int(s,"hype_blink_ms"),200,4000);
    t->hype_width=bounded(obs_data_get_int(s,"hype_width"),0,7680);t->hype_height=bounded(obs_data_get_int(s,"hype_height"),16,200);hype_palette(t,s);
    t->upper_height=bounded(obs_data_get_int(s,"upper_height"),compact_style(t->style)?16:24,400);
    t->lower_height=bounded(obs_data_get_int(s,"lower_height"),compact_style(t->style)?16:24,400);
    t->header_bg=(uint32_t)obs_data_get_int(s,"header_bg");
    t->upper_bg=(uint32_t)obs_data_get_int(s,"upper_bg");
    t->lower_bg=(uint32_t)obs_data_get_int(s,"lower_bg");
    t->upper_color=(uint32_t)obs_data_get_int(s,"text_color");
    t->lower_color=(uint32_t)obs_data_get_int(s,"lower_text_color");
    t->reference_caption=false;
    if(t->style>=STYLE_PRIME&&t->style<=STYLE_DARK){
        obs_data_t *caption_font=obs_data_get_obj(s,"font");
        const char *caption=t->style==STYLE_GLASS||t->style==STYLE_SPORT||t->style==STYLE_MAGAZINE?"ON AIR":"LIVE";
        long long expected_size=(long long)roundf(reference_profiles[t->style].body_size*((float)t->width)/reference_profiles[t->style].width);
        uint32_t expected_color=t->style==STYLE_SPORT?0xff110b07:0xffffffff;
        t->reference_caption=!strcmp(obs_data_get_string(s,"header_text"),caption)&&!strcmp(obs_data_get_string(caption_font,"face"),"Arial")&&obs_data_get_int(caption_font,"size")==expected_size&&obs_data_get_int(caption_font,"flags")==0&&(uint32_t)obs_data_get_int(s,"header_color")==expected_color&&(t->header_bg>>24)==255;
        obs_data_release(caption_font);
    }
    for(int row=0;row<2;row++) {
        const char *content=row?active_message(t):t->now_stage&&t->content[0]?t->content[0]:upper_value(t);
        if(!t->content[row] || strcmp(t->content[row],content)) {
            if(!row)t->now_time=0;
            bfree(t->content[row]); t->content[row]=bstrdup(content);
            t->phase[row]=0;
            t->block_time[row]=0;
        }
    }
    for (int i=0;i<TEXT_COUNT;i++) {
        obs_data_t *text=obs_data_create();
        obs_data_t *input_font=obs_data_get_obj(s,"font");
        obs_data_t *font=obs_data_create();
        const char *face=obs_data_get_string(input_font,"face");
        obs_data_set_string(font,"face",i==MUSIC?"Segoe UI Symbol":*face?face:"Arial");
        long long font_size=input_font?obs_data_get_int(input_font,"size"):26;
        if(t->style!=STYLE_BASIC&&i==HEADER)font_size=(long long)(font_size*reference_profiles[t->style].header_scale);
        if(t->style!=STYLE_BASIC&&i==MUSIC)font_size=(long long)(font_size*1.3);
        if(t->style!=STYLE_BASIC&&!compact_style(t->style)&&i==LOWER)font_size=(long long)roundf(font_size*(t->style==STYLE_NEWS?.90f:.82f));
        if(t->style!=STYLE_BASIC&&t->style!=STYLE_NEWS&&i==UPPER_LABEL)font_size=(long long)roundf(font_size*.90f);
        if(t->style!=STYLE_BASIC&&(i==SUBTITLE||i==LOWER_LABEL))font_size=(long long)(font_size*1.1);
        if(i>=HYPE_BADGE)font_size=(long long)(t->hype_height*.72f);
        obs_data_set_int(font,"size",bounded(font_size,8,200));
        bool header_bold=t->style!=STYLE_WORLD&&t->style!=STYLE_BUSINESS&&t->style!=STYLE_CLEAN&&t->style!=STYLE_MAP;
        bool bold=i>=HYPE_BADGE||(t->style!=STYLE_BASIC&&((i==HEADER&&header_bold)||i==UPPER_LABEL||i==LOWER_LABEL||(i==CLOCK&&!compact_style(t->style))||((t->style==STYLE_NEWS||t->style==STYLE_BREAKING||t->style==STYLE_BUSINESS||t->style==STYLE_LIGHT)&&(i==LOWER||i==SUBTITLE))));
        obs_data_set_int(font,"flags",obs_data_get_int(input_font,"flags")|(bold?OBS_FONT_BOLD:0));
        obs_data_release(input_font);
        obs_data_set_obj(text,"font",font);
        obs_data_release(font);
        const char *value=i>=HYPE_BADGE?t->hype_text[i-HYPE_BADGE]:i==LOWER?active_message(t):i==UPPER?(t->now_stage&&t->content[0]?t->content[0]:upper_value(t)):i==CLOCK?t->clock_value:i==WEATHER&&t->weather_auto?t->weather_value:i==MUSIC?(t->style==STYLE_NEWS?"↻ ♫":"♫"):obs_data_get_string(s,keys[i]);
        obs_data_set_string(text,"text",value);
        t->has_text[i]=*value!='\0';
        const char *color_key=i==HEADER || i==UPPER_LABEL || i==LOWER_LABEL?"header_color":"text_color";
        if(i==LOWER || i==SUBTITLE) color_key="lower_text_color";
        if(t->style==STYLE_LIGHT&&i==UPPER_LABEL)color_key="text_color";
        uint32_t color=(uint32_t)obs_data_get_int(s,color_key);
        if(i==LOWER_LABEL&&t->style==STYLE_SPORTS)color=(uint32_t)obs_data_get_int(s,"upper_bg");
        if(i>=HYPE_BADGE)color=t->hype_fg;
        obs_data_set_int(text,"color",color & 0xffffff);
        obs_data_set_int(text,"opacity",(color>>24)*100/255);
        bool caption_edge=compact_style(t->style)&&t->style!=STYLE_ULTRA&&i==HEADER&&(color>>24)==255;
        obs_data_set_bool(text,"outline",caption_edge);
        if(caption_edge){obs_data_set_int(text,"outline_size",1);obs_data_set_int(text,"outline_color",color&0xffffff);obs_data_set_int(text,"outline_opacity",100);}
        obs_data_set_bool(text,"extents",false);
        obs_data_set_bool(text,"antialiasing",true);
        obs_data_set_int(text,"bk_opacity",0);
        obs_source_update(t->text[i],text);
        t->text_captured[i]=false; t->refresh[i]=2;
        obs_data_release(text);
    }
    measure_clock(t);
    prepare_compact_items(t,true);
    ReleaseSRWLockExclusive(&t->lock);
    finish_hype_anchor(t,anchor);
}
static void prepare_compact_items(struct ticker *t,bool force)
{
    if(t->style==STYLE_BASIC){t->compact_count=0;return;}
    obs_data_t *base=obs_source_get_settings(t->text[LOWER]);
    char *split=NULL;const char *normal[12];int normal_count=0;
    if(!compact_style(t->style)){
        split=bstrdup(t->joined_news?t->joined_news:"");char *cursor=split;
        while(*cursor&&normal_count<12){
            while(*cursor==' ')cursor++;normal[normal_count++]=cursor;
            char *separator=strstr(cursor,t->style==STYLE_NEWS?"•":" | ");
            if(!separator||normal_count==12)break;
            char *end=separator;while(end>cursor&&end[-1]==' ')end--;*end=0;
            cursor=separator+3;
        }
    }
    t->compact_count=compact_style(t->style)?t->event_count+t->message_count:normal_count;
    for(int i=0;i<t->compact_count;i++){
        const char *value=compact_style(t->style)?(i<t->event_count?t->event_text[i]:t->messages[t->valid_messages[i-t->event_count]]):normal[i];
        int kind=compact_style(t->style)&&i<t->event_count?t->event_kind[i]:-1;
        const char *symbols[]={"♥ ","★ ","◇ ","🎁 ","◁ "};
        if(compact_style(t->style)&&kind<0)for(int k=0;k<5;k++)if(!strncmp(value,symbols[k],strlen(symbols[k]))){kind=k;value+=strlen(symbols[k]);break;}
        t->compact_kind[i]=kind;
        obs_data_t *s=t->compact_items[i]?obs_source_get_settings(t->compact_items[i]):NULL;
        bool changed=!s||strcmp(obs_data_get_string(s,"text"),value);obs_data_release(s);
        if(changed||force){
            obs_data_t *item=obs_data_create();obs_data_apply(item,base);obs_data_set_string(item,"text",value);
            if(!t->compact_items[i])t->compact_items[i]=obs_source_create_private("text_gdiplus","Compact event text",item);
            else obs_source_update(t->compact_items[i],item);
            obs_data_release(item);
            t->compact_captured[i]=false;t->compact_refresh[i]=2;
        }
    }
    obs_data_release(base);
    bfree(split);
}
static void destroy(void *data)
{
    struct ticker *t=data;
    for(int i=0;i<TEXT_COUNT;i++) obs_source_release(t->text[i]);
    for(int i=0;i<12;i++)obs_source_release(t->compact_items[i]);
    obs_enter_graphics();
    for(int row=0;row<2;row++) { gs_texrender_destroy(t->clip[row]); bfree(t->content[row]); }
    for(int i=0;i<TEXT_COUNT;i++) gs_texrender_destroy(t->text_capture[i]);
    for(int i=0;i<12;i++)gs_texrender_destroy(t->compact_capture[i]);
    gs_texrender_destroy(t->hype_render);
    gs_effect_destroy(t->fade_effect);
    if(t->shared_graphics&&!--shared_graphics.users){
        gs_texture_destroy(shared_graphics.map);gs_texture_destroy(shared_graphics.design);gs_texture_destroy(shared_graphics.compact);
        shared_graphics.map=shared_graphics.design=shared_graphics.compact=NULL;
    }
    obs_leave_graphics();
    for(int i=0;i<5;i++) bfree(t->messages[i]);
    bfree(t->joined_news);
    np_release(t->now_session);bfree(t->now_path);bfree(t->manual_upper);
    for(unsigned i=0;i<WEATHER_CITIES;i++){weather_release(t->weather_sessions[i]);bfree(t->weather_cities[i]);}
    bfree(t);
}
struct initialization { struct ticker *ticker; obs_data_t *settings; bool ok; };
static void initialize(void *param)
{
    struct initialization *request=param;
    struct ticker *t=request->ticker;
    char *path=obs_module_file("effects/ticker-fade.effect"),*error=NULL;
    obs_enter_graphics();
    if(path) t->fade_effect=gs_effect_create_from_file(path,&error);
    if(!shared_graphics.users){
        char *map_path=obs_module_file("graphics/broadcast-reference.png");
        char *atlas_path=obs_module_file("graphics/broadcast-designs.png");
        char *compact_path=obs_module_file("graphics/compact-reference.png");
        if(map_path)shared_graphics.map=gs_texture_create_from_file(map_path);
        if(atlas_path)shared_graphics.design=gs_texture_create_from_file(atlas_path);
        if(compact_path)shared_graphics.compact=gs_texture_create_from_file(compact_path);
        bfree(map_path);bfree(atlas_path);bfree(compact_path);
    }
    shared_graphics.users++;t->shared_graphics=true;
    t->reference_map=shared_graphics.map;t->design_atlas=shared_graphics.design;t->compact_atlas=shared_graphics.compact;
    obs_leave_graphics();
    bfree(path);
    if(!t->fade_effect) {
        blog(LOG_ERROR,"[news-ticker-nexus] Cannot load fade effect: %s",error?error:"missing effect file");
        bfree(error);return;
    }
    bfree(error);
    for(int i=0;i<TEXT_COUNT;i++) {
        t->text[i]=obs_source_create_private("text_gdiplus_v3","Ticker text",NULL);
        if(!t->text[i]) {
            blog(LOG_ERROR,"[news-ticker-nexus] OBS Text (GDI+) is required");
            return;
        }
    }
    update(t,request->settings); request->ok=true;
}
static void *create(obs_data_t *s, obs_source_t *self)
{
    struct ticker *t=bzalloc(sizeof(*t));
    t->self=self; InitializeSRWLock(&t->lock);
    struct initialization request={.ticker=t,.settings=s};
    obs_queue_task(OBS_TASK_GRAPHICS,initialize,&request,true);
    if(!request.ok) { destroy(t); return NULL; }
    return t;
}
static uint32_t width(void *data)
{
    struct ticker *t=data; AcquireSRWLockShared(&t->lock);
    uint32_t n=t->width; ReleaseSRWLockShared(&t->lock); return n;
}
static uint32_t height(void *data)
{
    struct ticker *t=data; AcquireSRWLockShared(&t->lock);
    uint32_t n=(uint32_t)(t->upper_height+t->lower_height+(t->hype_enabled?t->hype_height-ticker_top_inset(t):0)); ReleaseSRWLockShared(&t->lock); return n;
}
static void rectangle(float x,float y,float w,float h,uint32_t rgba)
{
    struct vec4 c; float a=(rgba>>24)/255.f;
    vec4_set(&c,(rgba&255)/255.f*a,((rgba>>8)&255)/255.f*a,((rgba>>16)&255)/255.f*a,a);
    gs_effect_t *fx=obs_get_base_effect(OBS_EFFECT_SOLID);
    gs_effect_set_vec4(gs_effect_get_param_by_name(fx,"color"),&c);
    gs_matrix_push(); gs_matrix_translate3f(x,y,0);
    while(gs_effect_loop(fx,"Solid")) gs_draw_sprite(NULL,0,(uint32_t)w,(uint32_t)h);
    gs_matrix_pop();
}
static void composite_text(gs_texture_t *texture,float x,float y,uint32_t w,uint32_t h)
{
    gs_effect_t *fx=obs_get_base_effect(OBS_EFFECT_DEFAULT);
    gs_effect_set_texture(gs_effect_get_param_by_name(fx,"image"),texture);
    gs_blend_state_push();
    /* Text (GDI+) produces straight-alpha RGB. Premultiply it once while
     * compositing, with a separate ONE factor for the alpha channel. */
    gs_blend_function_separate(GS_BLEND_SRCALPHA,GS_BLEND_INVSRCALPHA,GS_BLEND_ONE,GS_BLEND_INVSRCALPHA);
    gs_matrix_push(); gs_matrix_translate3f(x,y,0);
    while(gs_effect_loop(fx,"Draw")) gs_draw_sprite(texture,0,w,h);
    gs_matrix_pop(); gs_blend_state_pop();
}
static void prepare_text(struct ticker *t,int index)
{
    obs_source_t *s=t->text[index];
    float w=(float)obs_source_get_width(s),h=(float)obs_source_get_height(s);
    if(w<=0||h<=0)return;
    if(!t->text_capture[index]) t->text_capture[index]=gs_texrender_create(GS_RGBA,GS_ZS_NONE);
    if(!t->text_captured[index]) {
        gs_texrender_reset(t->text_capture[index]);
        if(!gs_texrender_begin(t->text_capture[index],(uint32_t)w,(uint32_t)h)) return;
        struct vec4 clear; vec4_zero(&clear); gs_clear(GS_CLEAR_COLOR,&clear,0,0);
        gs_ortho(0,w,0,h,-100,100);
        gs_blend_state_push(); gs_blend_function(GS_BLEND_ONE,GS_BLEND_ZERO);
        obs_source_video_render(s);
        gs_blend_state_pop(); gs_texrender_end(t->text_capture[index]);
        if(index==HEADER||index==SUBTITLE||index==UPPER_LABEL||index==LOWER_LABEL||index>=HYPE_BADGE){
            t->caption_top[index]=0;t->caption_height[index]=h;
            gs_stagesurf_t *stage=gs_stagesurface_create((uint32_t)w,(uint32_t)h,GS_RGBA);
            if(stage){
                gs_stage_texture(stage,gs_texrender_get_texture(t->text_capture[index]));
                uint8_t *pixels;uint32_t stride;
                if(gs_stagesurface_map(stage,&pixels,&stride)){
                    uint32_t first=(uint32_t)h,last=0;
                    for(uint32_t row=0;row<(uint32_t)h;row++)for(uint32_t col=0;col<(uint32_t)w;col++)if(pixels[row*stride+col*4+3]>24){if(row<first)first=row;if(row>last)last=row;break;}
                    if(first<=last){t->caption_top[index]=(float)first;t->caption_height[index]=(float)(last-first+1);}
                    gs_stagesurface_unmap(stage);
                }
                gs_stagesurface_destroy(stage);
            }
        }
        t->text_captured[index]=true;
    }
}
static void draw_text(struct ticker *t,int index,float x,float y,float available,float band)
{
    obs_source_t *s=t->text[index];
    float w=(float)obs_source_get_width(s),h=(float)obs_source_get_height(s);
    if(w<=0||h<=0||available<=0)return;
    prepare_text(t,index);
    float ink_top=0;
    if(t->caption_height[index]>0){ink_top=t->caption_top[index];h=t->caption_height[index];}
    float padding=index>=HYPE_BADGE?4:t->style!=STYLE_BASIC?(index==HEADER?4:index==SUBTITLE?4:index==MUSIC?4:index==UPPER_LABEL||index==LOWER_LABEL||index==LOWER?6:12):12;
    float scale=fmaxf(0,band-fminf(padding,band*.25f))/h;
    if(scale>1) scale=1;
    float width_factor=index==HEADER?reference_header_width(t->style):(index==UPPER||index==LOWER)?reference_body_width(t->style):1;
    if(index!=UPPER&&w*scale*width_factor>available) scale=available/(w*width_factor);
    gs_matrix_push(); gs_matrix_translate3f(x,y+(band-h*scale)/2-ink_top*scale,0);
    gs_matrix_scale3f(scale*width_factor,scale,1);
    if(t->style==STYLE_NEWS&&(index==HEADER||index==SUBTITLE||index==UPPER_LABEL||index==LOWER_LABEL)){
        gs_effect_set_texture(gs_effect_get_param_by_name(t->fade_effect,"image"),gs_texrender_get_texture(t->text_capture[index]));
        gs_effect_set_float(gs_effect_get_param_by_name(t->fade_effect,"opacity"),.4f);
        gs_matrix_push();gs_matrix_translate3f(1,1,0);
        while(gs_effect_loop(t->fade_effect,"Shadow"))gs_draw_sprite(gs_texrender_get_texture(t->text_capture[index]),0,(uint32_t)w,obs_source_get_height(s));
        gs_matrix_pop();
    }
    if(index==HYPE_MESSAGE&&t->hype_blink){
        gs_effect_set_texture(gs_effect_get_param_by_name(t->fade_effect,"image"),gs_texrender_get_texture(t->text_capture[index]));
        gs_effect_set_float(gs_effect_get_param_by_name(t->fade_effect,"opacity"),.25f+.75f*(.5f+.5f*cosf((float)t->hype_clock*6.2831853f/(t->hype_blink_ms/1000.f))));
        while(gs_effect_loop(t->fade_effect,"TextFade"))gs_draw_sprite(gs_texrender_get_texture(t->text_capture[index]),0,(uint32_t)w,obs_source_get_height(s));
    }else if(index==WEATHER&&t->weather_auto&&t->weather_alpha<.999f){
        gs_effect_set_texture(gs_effect_get_param_by_name(t->fade_effect,"image"),gs_texrender_get_texture(t->text_capture[index]));
        gs_effect_set_float(gs_effect_get_param_by_name(t->fade_effect,"opacity"),t->weather_alpha);
        while(gs_effect_loop(t->fade_effect,"TextFade"))gs_draw_sprite(gs_texrender_get_texture(t->text_capture[index]),0,(uint32_t)w,obs_source_get_height(s));
    }else composite_text(gs_texrender_get_texture(t->text_capture[index]),0,0,(uint32_t)w,obs_source_get_height(s));
    gs_matrix_pop();
}
static float text_width(obs_source_t *s,float band)
{
    float w=(float)obs_source_get_width(s),h=(float)obs_source_get_height(s);
    return h>band-12?w*(band-12)/h:w;
}
static void tick(void *data,float seconds)
{
    struct ticker *t=data;
    if(!isfinite(seconds) || seconds<=0) return;
    AcquireSRWLockExclusive(&t->lock);
    if(style_has_map(t->style)&&t->map_enabled&&t->map_animated&&t->map_intensity&&obs_source_showing(t->self))t->map_time=fmod(t->map_time+seconds*t->map_speed/100.,80.);
    hype_tick(t,seconds);
    if(t->weather_wait)t->weather_wait--;
    if(obs_source_showing(t->self)){
        if(t->weather_stage==1){t->weather_alpha=fmaxf(0,t->weather_alpha-seconds/.25f);if(t->weather_alpha<=0){set_weather_text(t,t->weather_pending);t->weather_stage=2;}}
        else if(t->weather_stage==2){if(!t->weather_wait)t->weather_stage=3;}
        else if(t->weather_stage==3){t->weather_alpha=fminf(1,t->weather_alpha+seconds/.25f);if(t->weather_alpha>=1)t->weather_stage=0;}
    }
    if(t->weather_auto&&(t->weather_poll+=seconds)>=.25){
        char text[512];select_weather(t,t->weather_poll,obs_source_showing(t->self),text,sizeof(text));t->weather_poll=0;
        if(strcmp(text,t->weather_stage?t->weather_pending:t->weather_value)){
            snprintf(t->weather_pending,sizeof(t->weather_pending),"%s",text);
            if(!*text){t->weather_stage=0;t->weather_alpha=1;set_weather_text(t,"");}
            else if(*t->weather_value&&t->weather_stage!=2)t->weather_stage=1;
            else{set_weather_text(t,text);t->weather_alpha=0;t->weather_stage=2;}
        }
    }
    for(int i=0;i<TEXT_COUNT;i++) if(t->refresh[i]) {t->text_captured[i]=false;t->refresh[i]--;}
    for(int i=0;i<12;i++)if(t->compact_refresh[i]){t->compact_captured[i]=false;t->compact_refresh[i]--;}
    if(t->message_wait) t->message_wait--;
    if(t->now_wait)t->now_wait--;
    if(t->now_stage==1){
        t->now_alpha=fmaxf(0,t->now_alpha-seconds/.25f);
        if(t->now_alpha<=0){set_now_text(t,t->now_pending);t->now_stage=2;}
    }else if(t->now_stage==2){if(!t->now_wait)t->now_stage=3;}
    else if(t->now_stage==3){t->now_alpha=fminf(1,t->now_alpha+seconds/.25f);if(t->now_alpha>=1)t->now_stage=0;}
    t->raid_poll+=seconds;
    if(t->raid_poll>=.25){
        struct twitch_snapshot current;twitch_observer_read(&current);
        bool new_event=(t->twitch_flags&(1u<<NEXUS_TICKER_RAID))&&current.event_id[0]&&strcmp(current.event_id,t->twitch_seen.event_id);
        NexusTickerSnapshot feed;twitch_observer_read_feed(&feed);
        for(unsigned i=0;i<NEXUS_TICKER_COUNT;i++)
            if((t->twitch_flags&(1u<<i))&&feed.latest[i].present&&strcmp(feed.latest[i].id,t->feed_seen.latest[i].id))new_event=true;
        t->feed_seen=feed;
        t->twitch_seen=current;
        if(t->twitch_flags)refresh_raid(t,new_event);
        t->raid_poll=0;
    }
    if(t->clock_enabled) {
        t->clock_poll+=seconds;
        if(t->clock_poll>=.25) {refresh_clock(t);t->clock_poll=0;}
    }
    if(t->upper_mode) {
        t->now_poll+=seconds;
        if(t->now_poll>=.25){refresh_now_playing(t);t->now_poll=0;}
    }
    if(t->now_ping_pong&&!t->now_stage&&!t->now_wait&&obs_source_showing(t->self))t->now_time+=seconds;
    if(t->motion==MOTION_SCROLL && obs_source_showing(t->self)) {
        if(t->period[1]>0) t->phase[1]=fmod(t->phase[1]+t->scroll_speed*(double)seconds,t->period[1]);
    }
    if(t->motion==MOTION_BLOCKS && obs_source_showing(t->self)) {
        double cycle=(t->block_transition*2+t->block_hold+t->block_pause)/1000.;
        advance_messages(t,t->block_time[1],seconds,(t->block_transition*2+t->block_hold)/1000.,cycle);
        t->block_time[1]=fmod(t->block_time[1]+seconds,cycle);
    }
    if(t->motion==MOTION_ALTERNATE && obs_source_showing(t->self)) {
        double hidden=(t->message_fade*2+t->message_hold)/1000.,cycle=hidden+.1;
        advance_messages(t,t->alternate_time,seconds,hidden,cycle);
        t->alternate_time=fmod(t->alternate_time+seconds,cycle);
    }
    ReleaseSRWLockExclusive(&t->lock);
    /* Never rebuild a Qt properties view from background state changes: an editable-list modal may still own its widgets. */
}
static void moving_text(obs_source_t *s,float x,float band,float padding)
{
    float h=(float)obs_source_get_height(s);
    if(h<=0) return;
    float scale=h>band-padding?(band-padding)/h:1;
    gs_matrix_push(); gs_matrix_translate3f(x,(band-h*scale)/2,0);
    gs_matrix_scale3f(scale,scale,1); obs_source_video_render(s); gs_matrix_pop();
}
static void draw_block(struct ticker *t,int row,float x,float y,float available,float band);
static void draw_alternate(struct ticker *t,float x,float y,float available,float band)
{
    double opacity=block_reveal(t->alternate_time,t->message_fade/1000.,t->message_hold/1000.,.1);
    if(opacity<=0 || available<1 || band<1) return;
    if(!t->clip[1]) t->clip[1]=gs_texrender_create(GS_RGBA,GS_ZS_NONE);
    gs_texrender_reset(t->clip[1]);
    if(!gs_texrender_begin(t->clip[1],(uint32_t)available,(uint32_t)band)) return;
    struct vec4 clear;vec4_zero(&clear);gs_clear(GS_CLEAR_COLOR,&clear,0,0);
    gs_ortho(0,available,0,band,-100,100);
    draw_text(t,LOWER,0,0,available,band);
    gs_texrender_end(t->clip[1]);
    gs_effect_set_texture(gs_effect_get_param_by_name(t->fade_effect,"image"),gs_texrender_get_texture(t->clip[1]));
    gs_effect_set_float(gs_effect_get_param_by_name(t->fade_effect,"opacity"),(float)opacity);
    gs_matrix_push();gs_matrix_translate3f(x,y,0);
    while(gs_effect_loop(t->fade_effect,"Draw"))gs_draw_sprite(gs_texrender_get_texture(t->clip[1]),0,(uint32_t)available,(uint32_t)band);
    gs_matrix_pop();
}
static void draw_content(struct ticker *t,int row,float x,float y,float available,float band)
{
    if(!row && t->upper_mode && t->now_wait)return;
    if(!row){
        available=fminf(available,(float)t->now_width);
        if(available<1||band<1||!t->fade_effect)return;
        if(!t->clip[0])t->clip[0]=gs_texrender_create(GS_RGBA,GS_ZS_NONE);
        gs_texrender_reset(t->clip[0]);
        if(!gs_texrender_begin(t->clip[0],(uint32_t)available,(uint32_t)band))return;
        struct vec4 clear;vec4_zero(&clear);gs_clear(GS_CLEAR_COLOR,&clear,0,0);
        gs_ortho(0,available,0,band,-100,100);
        double overflow=fmax(0,text_width(t->text[UPPER],band)*reference_body_width(t->style)-available);
        float offset=t->now_ping_pong?(float)ping_pong_offset(t->now_time,overflow,t->now_speed,t->now_pause/1000.0):0;
        draw_text(t,UPPER,-offset,0,available,band);
        gs_texrender_end(t->clip[0]);
        gs_effect_set_texture(gs_effect_get_param_by_name(t->fade_effect,"image"),gs_texrender_get_texture(t->clip[0]));
        gs_effect_set_float(gs_effect_get_param_by_name(t->fade_effect,"opacity"),t->now_alpha);
        gs_matrix_push();gs_matrix_translate3f(x,y,0);
        while(gs_effect_loop(t->fade_effect,"Draw"))gs_draw_sprite(gs_texrender_get_texture(t->clip[0]),0,(uint32_t)available,(uint32_t)band);
        gs_matrix_pop();return;
    }
    if(row && t->message_wait) return;
    if(row && t->motion==MOTION_ALTERNATE) {draw_alternate(t,x,y,available,band);return;}
    obs_source_t *s=t->text[row?LOWER:UPPER];
    if(t->motion==MOTION_BLOCKS) {draw_block(t,row,x,y,available,band);return;}
    if(t->motion!=MOTION_SCROLL) { draw_text(t,row?LOWER:UPPER,x,y,available,band); return; }
    if(available<1 || band<1) return;
    uint32_t w=(uint32_t)available,h=(uint32_t)band;
    double text_w=text_width(s,t->style!=STYLE_BASIC?band+6:band);
    t->period[row]=fmax(text_w,(double)w)+t->scroll_gap;
    t->phase[row]=fmod(t->phase[row],t->period[row]);
    if(!t->clip[row]) t->clip[row]=gs_texrender_create(GS_RGBA,GS_ZS_NONE);
    gs_texrender_reset(t->clip[row]);
    if(!gs_texrender_begin(t->clip[row],w,h)) return;
    struct vec4 clear; vec4_zero(&clear);
    gs_clear(GS_CLEAR_COLOR,&clear,0,0);
    gs_ortho(0,(float)w,0,(float)h,-100,100);
    /* Off-screen clipping stays correct when OBS scales/rotates the source.
     * Text is fitted vertically only, so long messages stay readable. */
    moving_text(s,-(float)t->phase[row],band,t->style!=STYLE_BASIC?6:12);
    moving_text(s,(float)(t->period[row]-t->phase[row]),band,t->style!=STYLE_BASIC?6:12);
    gs_texrender_end(t->clip[row]);
    composite_text(gs_texrender_get_texture(t->clip[row]),x,y,w,h);
}
static uint32_t shade(uint32_t rgba,float brightness)
{
    uint32_t result=rgba&0xff000000;
    for(int shift=0;shift<24;shift+=8) {
        uint32_t component=(uint32_t)(((rgba>>shift)&255)*brightness);
        result|=(component>255?255:component)<<shift;
    }
    return result;
}
static void gradient(float x,float y,float w,float h,uint32_t color)
{
    /* Interpolate premultiplied vertex colors with OBS's built-in effect:
     * one draw per panel, without textures or per-pixel CPU work. */
    uint32_t endpoints[]={shade(color,1.16f),shade(color,.8f)};
    for(int i=0;i<2;i++) {
        uint32_t a=endpoints[i]>>24,result=endpoints[i]&0xff000000;
        for(int shift=0;shift<24;shift+=8) result|=((((endpoints[i]>>shift)&255)*a+127)/255)<<shift;
        endpoints[i]=result;
    }
    gs_effect_t *fx=obs_get_base_effect(OBS_EFFECT_SOLID);
    struct vec4 white; vec4_set(&white,1,1,1,1);
    gs_effect_set_vec4(gs_effect_get_param_by_name(fx,"color"),&white);
    while(gs_effect_loop(fx,"SolidColored")) {
        gs_render_start(true);
        gs_color(endpoints[0]); gs_vertex2f(x,y);
        gs_color(endpoints[0]); gs_vertex2f(x+w,y);
        gs_color(endpoints[1]); gs_vertex2f(x,y+h);
        gs_color(endpoints[1]); gs_vertex2f(x+w,y+h);
        gs_render_stop(GS_TRISTRIP);
    }
}
static void finished_panel(struct ticker *t,float x,float y,float w,float h,uint32_t color,int mode)
{
    if(w<1||h<1)return;
    float a=(color>>24)/255.f;struct vec4 c,size;
    vec4_set(&c,(color&255)/255.f*a,((color>>8)&255)/255.f*a,((color>>16)&255)/255.f*a,a);vec4_set(&size,w,h,0,0);
    gs_effect_set_vec4(gs_effect_get_param_by_name(t->fade_effect,"compact_bg"),&c);gs_effect_set_vec4(gs_effect_get_param_by_name(t->fade_effect,"compact_size"),&size);
    gs_effect_set_float(gs_effect_get_param_by_name(t->fade_effect,"finish_mode"),(float)mode);
    gs_matrix_push();gs_matrix_translate3f(x,y,0);while(gs_effect_loop(t->fade_effect,"Finish"))gs_draw_sprite(NULL,0,(uint32_t)w,(uint32_t)h);gs_matrix_pop();
}
static void draw_block(struct ticker *t,int row,float x,float y,float available,float band)
{
    if(available<1 || band<12) return;
    double reveal=block_reveal(t->block_time[row],t->block_transition/1000.,t->block_hold/1000.,t->block_pause/1000.);
    if(reveal<=0) return;
    int index=row?LOWER:UPPER;
    float card_width=text_width(t->text[index],band-6)*reference_body_width(t->style)+32;
    if(card_width>available) card_width=available;
    float offset=-card_width*(float)(1-reveal);
    if(!t->clip[row]) t->clip[row]=gs_texrender_create(GS_RGBA,GS_ZS_NONE);
    gs_texrender_reset(t->clip[row]);
    if(!gs_texrender_begin(t->clip[row],(uint32_t)available,(uint32_t)band)) return;
    struct vec4 clear; vec4_zero(&clear); gs_clear(GS_CLEAR_COLOR,&clear,0,0);
    gs_ortho(0,available,0,band,-100,100);
    uint32_t color=row?t->lower_bg:t->upper_bg;
    gradient(offset,3,card_width,band-6,shade(color,1.12f));
    rectangle(offset,3,3,band-6,t->header_bg);
    draw_text(t,index,offset+16,3,card_width-32,band-6);
    gs_texrender_end(t->clip[row]);
    /* Cards already contain premultiplied backgrounds and text. */
    gs_effect_t *fx=obs_get_base_effect(OBS_EFFECT_DEFAULT);
    gs_effect_set_texture(gs_effect_get_param_by_name(fx,"image"),gs_texrender_get_texture(t->clip[row]));
    gs_blend_state_push(); gs_blend_function(GS_BLEND_ONE,GS_BLEND_INVSRCALPHA);
    gs_matrix_push(); gs_matrix_translate3f(x,y,0);
    while(gs_effect_loop(fx,"Draw")) gs_draw_sprite(gs_texrender_get_texture(t->clip[row]),0,(uint32_t)available,(uint32_t)band);
    gs_matrix_pop(); gs_blend_state_pop();
}
static void round_icon(float x,float y,float radius,uint32_t rgba,bool sun)
{
    float a=(rgba>>24)/255.f;struct vec4 color;
    vec4_set(&color,(rgba&255)/255.f*a,((rgba>>8)&255)/255.f*a,((rgba>>16)&255)/255.f*a,a);
    gs_effect_t *fx=obs_get_base_effect(OBS_EFFECT_SOLID);gs_effect_set_vec4(gs_effect_get_param_by_name(fx,"color"),&color);
    while(gs_effect_loop(fx,"Solid")){
        gs_render_start(true);
        if(sun){
            for(int i=0;i<20;i++){
                float p=i*6.2831853f/20,q=(i+1)*6.2831853f/20;
                gs_vertex2f(x,y);gs_vertex2f(x+cosf(p)*radius*.57f,y+sinf(p)*radius*.57f);gs_vertex2f(x+cosf(q)*radius*.57f,y+sinf(q)*radius*.57f);
            }
            gs_render_stop(GS_TRIS);gs_render_start(true);
            for(int i=0;i<8;i++){float p=i*6.2831853f/8;gs_vertex2f(x+cosf(p)*radius*.8f,y+sinf(p)*radius*.8f);gs_vertex2f(x+cosf(p)*radius*1.15f,y+sinf(p)*radius*1.15f);}
            gs_render_stop(GS_LINES);
        }else{
            for(int i=0;i<=24;i++){float p=i*6.2831853f/24;gs_vertex2f(x+cosf(p)*radius,y+sinf(p)*radius);}
            gs_render_stop(GS_LINESTRIP);gs_render_start(true);
            gs_vertex2f(x,y-radius*.7f);gs_vertex2f(x,y);gs_vertex2f(x,y);gs_vertex2f(x+radius*.55f,y+radius*.35f);gs_render_stop(GS_LINES);
        }
    }
}
#include "reference-icons.h"
static float clock_text_slot(struct ticker *t,float band)
{return t->clock_layout_width*fminf(1,fmaxf(0,band-12)/fmaxf(1,t->clock_layout_height));}
static float draw_clock_at(struct ticker *t,float start,float band,float right)
{
    if(!t->clock_enabled || !t->has_text[CLOCK]) return right;
    float padding=t->style!=STYLE_BASIC?40:24;
    float clock_width=clock_text_slot(t,band)+padding;
    float maximum=(right-start)*(t->has_text[UPPER]?.5f:1.f);
    if(clock_width>maximum) clock_width=maximum;
    if(clock_width<28) return right;
    float x=right-clock_width;
    rectangle(x-(t->style!=STYLE_BASIC?25:8),8,1,band-16,shade(t->upper_bg,2));
    bool icon=t->style!=STYLE_BASIC&&clock_width>=56;
    if(icon&&!reference_icon(t,1,x-2,0,27,band,t->upper_color))round_icon(x+11,band*.5f,band*.18f,t->upper_color,false);
    float text_left=x+(icon?35:6),text_space=clock_width-(icon?40:12);
    float offset=fmaxf(0,(text_space-text_width(t->text[CLOCK],band))*.5f);
    draw_text(t,CLOCK,text_left+offset,0,fmaxf(0,text_space-offset),band);
    return x-16;
}
static float right_info_start(struct ticker *t,float start,float band,float right)
{
    float weather=t->has_text[WEATHER]?fminf((float)t->weather_width,fmaxf(0,(right-start)*.6f)):0;
    float clock=t->clock_enabled?clock_text_slot(t,band)+56:0;
    return fmaxf(start,right-weather-clock-(weather?20:0));
}
static float draw_right_info(struct ticker *t,float start,float band,float right)
{
    float weather=t->has_text[WEATHER]?fminf((float)t->weather_width,fmaxf(0,(right-start)*.6f)):0;
    float weather_right=right;
    if(t->clock_enabled)weather_right=draw_clock_at(t,start,band,right)-4;
    if(weather>0){
        float left=weather_right-weather,tw=text_width(t->text[WEATHER],band),icons=t->weather_auto?0:32;
        float tx=left+fmaxf(0,(weather-tw-icons)*.5f);rectangle(left-12,8,1,fmaxf(0,band-16),shade(t->upper_bg,2));
        if(icons&&!reference_icon(t,2,tx,0,26,band,t->upper_color))round_icon(tx+12,band*.5f,band*.19f,(t->upper_color&0xff000000)|0x0020bbff,true);
        draw_text(t,WEATHER,tx+icons,0,fmaxf(0,weather_right-tx-icons),band);
    }
    return right_info_start(t,start,band,right);
}
static float draw_clock(struct ticker *t,float start,float band)
{return draw_clock_at(t,start,band,(float)t->width-16);}
static void label_glow(struct ticker *t,float x,float width,float band)
{
    float alpha=(t->header_bg>>24)/255.f*.84f;
    uint32_t tint=t->style==STYLE_LIGHT?t->upper_color:t->header_bg;
    if(t->style==STYLE_LIGHT)alpha*=.22f;
    struct vec4 color;
    vec4_set(&color,(tint&255)/255.f*alpha,((tint>>8)&255)/255.f*alpha,((tint>>16)&255)/255.f*alpha,alpha);
    gs_effect_set_vec4(gs_effect_get_param_by_name(t->fade_effect,"glow_color"),&color);
    gs_matrix_push();gs_matrix_translate3f(x-16,0,0);
    while(gs_effect_loop(t->fade_effect,"Glow"))gs_draw_sprite(NULL,0,(uint32_t)(width+32),(uint32_t)band);
    gs_matrix_pop();
}
static float label_width(struct ticker *t,int index,float band)
{
    prepare_text(t,index);
    float h=(float)obs_source_get_height(t->text[index]);
    if(t->caption_height[index]>0)h=t->caption_height[index];
    float pad=index==HEADER||index==SUBTITLE||index>=HYPE_BADGE?4:6;
    return h>0?obs_source_get_width(t->text[index])*fminf(1,fmaxf(0,band-fminf(pad,band*.25f))/h)*(index==HEADER?reference_header_width(t->style):1):0;
}
static void centered_text(struct ticker *t,int index,float x,float y,float available,float band)
{
    float pad=fmaxf(0,(available-label_width(t,index,band))/2);
    draw_text(t,index,x+pad,y,available-pad,band);
}
static void render_news(struct ticker *t)
{
    gs_matrix_push();gs_matrix_translate3f(0,5,0);
    float w=(float)t->width,top=(float)t->upper_height-5,bottom=(float)t->lower_height-5;
    float total=top+bottom;
    float map_width=t->style==STYLE_NEWS&&t->map_enabled?fminf(total*(240.f/63.f),w*.22f):0;
    finished_panel(t,0,0,w,top,t->upper_bg,0);
    finished_panel(t,0,top,w,bottom,t->lower_bg,1);
    if(map_width>0){
        // Fade the light news strip into the map's dark panel.
        for(int i=0;i<24;i++){
            float a=(i+1)/24.f;
            uint32_t color=(t->upper_bg&0xffffff)|((uint32_t)((t->upper_bg>>24)*a)<<24);
            rectangle(w-map_width-24+i,top,1,bottom,color);
        }
        rectangle(w-map_width,0,map_width,total,t->upper_bg);
        if(t->reference_map){
            gs_effect_set_float(gs_effect_get_param_by_name(t->fade_effect,"map_clean_header"),0);
            gs_effect_set_texture(gs_effect_get_param_by_name(t->fade_effect,"image"),t->reference_map);
            struct vec4 uv;vec4_set(&uv,1115.f/1370,5.f/73,240.f/1370,63.f/73);
            gs_effect_set_vec4(gs_effect_get_param_by_name(t->fade_effect,"map_uv"),&uv);
            gs_effect_set_float(gs_effect_get_param_by_name(t->fade_effect,"map_time"),(float)t->map_time);
            gs_effect_set_float(gs_effect_get_param_by_name(t->fade_effect,"map_intensity"),t->map_intensity/100.f);
            gs_effect_set_float(gs_effect_get_param_by_name(t->fade_effect,"map_opacity"),(t->upper_bg>>24)/255.f);
            gs_matrix_push();gs_matrix_translate3f(w-map_width,0,0);
            while(gs_effect_loop(t->fade_effect,"Map"))gs_draw_sprite(NULL,0,(uint32_t)map_width,(uint32_t)total);
            gs_matrix_pop();
        }
    }
    float header=0;
    if(t->has_text[HEADER]) {
        header=label_width(t,HEADER,top);
        float subtitle=t->has_text[SUBTITLE]?label_width(t,SUBTITLE,bottom):0;
        if(subtitle>header) header=subtitle;
        header+=25;
        if(header<(top+bottom)*1.4f) header=(top+bottom)*1.4f;
        if(header>w*.28f) header=w*.28f;
        finished_panel(t,-3,2,header+12,top+bottom,(uint32_t)((t->header_bg>>24)*.33f)<<24,3);
        finished_panel(t,0,0,header,top,t->header_bg,2);
        rectangle(header,top,12,bottom,t->upper_bg);
        rectangle(1,0,header-2,1,shade(t->header_bg,1.5f));
        rectangle(0,top-2,header,2,shade(t->header_bg,.58f));
        centered_text(t,HEADER,10,0,header-20,top);
        if(t->has_text[SUBTITLE])centered_text(t,SUBTITLE,10,top,header-20,bottom);
        for(int i=0;i<7;i++){
            uint32_t a=(uint32_t)((t->header_bg>>24)*.45f*(7-i)/7);
            rectangle(header+i,0,1,total,a<<24);
        }
        if(t->style==STYLE_LIGHT){
            struct vec4 c;float a=(t->header_bg>>24)/255.f;
            vec4_set(&c,(t->header_bg&255)/255.f*a,((t->header_bg>>8)&255)/255.f*a,((t->header_bg>>16)&255)/255.f*a,a);
            gs_effect_t *fx=obs_get_base_effect(OBS_EFFECT_SOLID);
            gs_effect_set_vec4(gs_effect_get_param_by_name(fx,"color"),&c);
            while(gs_effect_loop(fx,"Solid")){
                gs_render_start(true);gs_vertex2f(header,0);gs_vertex2f(header+10,top*.5f);gs_vertex2f(header,top);gs_render_stop(GS_TRIS);
            }
        }
    }
    const int labels[]={UPPER_LABEL,LOWER_LABEL},contents[]={UPPER,LOWER};
    for(int row=0;row<2;row++) {
        float band=row?bottom:top,y=row?top:0,x=header+(row?12:22);
        if(!row&&(t->has_text[UPPER]||t->has_text[UPPER_LABEL])){
            float icons=fminf(54*w/1472.f,(w-header)*.12f);
            if(!reference_icon(t,0,x,0,icons,top,t->upper_color))draw_text(t,MUSIC,x,0,icons,top);x+=icons+10;
        }
        int label=labels[row],content=contents[row];
        if(t->has_text[label]) {
            float label_size=label_width(t,label,band)+(row?40:24);
            float maximum=(w-header-32)*.36f;
            if(label_size>maximum)label_size=maximum;
            if(row==0)label_glow(t,x,label_size,band);
            else{
                finished_panel(t,x,y,label_size,band,t->header_bg,2);
                rectangle(x,y+band-2,label_size,2,shade(t->header_bg,.48f));
                for(int i=0;i<4;i++)rectangle(x+label_size+i,y,1,band,((uint32_t)((t->header_bg>>24)*.3f*(4-i)/4))<<24);
            }
            centered_text(t,label,x+8,y,label_size-16,band);
            x+=label_size+(row?4:12);
        }
        float right=row?w-map_width-24:w-16;
        if(!row&&t->info_right){right=draw_right_info(t,x,top,w-16);}
        else if(!row){
            float weather=t->has_text[WEATHER]?fminf(text_width(t->text[WEATHER],top)+40,fmaxf(0,(w-x-32)*.4f)):0;
            float clock=t->clock_enabled?clock_text_slot(t,top)+40:0;
            float song=fminf((float)t->now_width,fmaxf(0,w-x-weather-clock-(weather>0?100:48)));
            if(t->clock_enabled)right=draw_clock_at(t,x,band,fminf(w-weather-16,x+song+25+clock));
            else right=x+song;
            if(weather>0){
                float wx=fminf(w-weather-8,x+song+clock+(clock>0?76:32));
                rectangle(wx-29,8,1,top-16,shade(t->upper_bg,2));
                draw_text(t,WEATHER,wx+(t->weather_auto?0:32),0,w-wx-(t->weather_auto?8:40),top);
                // Small sun mark, text is explicitly manual, never a fake forecast.
                if(!t->weather_auto&&!reference_icon(t,2,wx-7,0,30,top,t->upper_color))round_icon(wx+7,top*.5f,top*.19f,(t->upper_color&0xff000000)|0x0020bbff,true);
            }
        }
        if(t->has_text[content]){if(row)compact_stream(t,x,y,right-x,band);else draw_content(t,row,x,y,right-x,band);}
    }
    rectangle(0,0,w,1,shade(t->upper_bg,1.8f));
    rectangle(0,top,w,1,shade(t->lower_bg,.7f));
    rectangle(0,top+bottom-1,w,1,shade(t->lower_bg,.65f));
    if(map_width>0)rectangle(w-2,0,2,total,(t->upper_bg&0xff000000)|0x00be6504);
    gs_matrix_pop();
}
#include "broadcast-layouts.h"
#include "hype-render.h"
static void render(void *data,gs_effect_t *effect)
{
    UNUSED_PARAMETER(effect);
    struct ticker *t=data; AcquireSRWLockShared(&t->lock);
    gs_blend_state_push(); gs_blend_function(GS_BLEND_ONE,GS_BLEND_INVSRCALPHA);
    render_hype(t);
    gs_matrix_push();gs_matrix_translate3f(0,t->hype_enabled?t->hype_height-ticker_top_inset(t):0,0);
    if(t->style!=STYLE_BASIC) {
        if(t->style==STYLE_NEWS)render_news(t);else render_broadcast(t);
        gs_matrix_pop();gs_blend_state_pop(); ReleaseSRWLockShared(&t->lock); return;
    }
    float w=(float)t->width,top=(float)t->upper_height,bottom=(float)t->lower_height;
    float tw=(float)obs_source_get_width(t->text[0]),th=(float)obs_source_get_height(t->text[0]);
    float scale=th>bottom-12?(bottom-12)/th:1;
    float header=t->has_text[0] && tw>0?tw*scale+32:0;
    if(header>w*.4f) header=w*.4f;
    rectangle(0,0,w,top,t->upper_bg);
    rectangle(0,top,w,bottom,t->lower_bg);
    if(header>0) rectangle(0,top,header,bottom,t->header_bg);
    if(t->has_text[0]) draw_text(t,HEADER,16,top,header-32,bottom);
    float upper_right=draw_clock(t,16,top);
    if(t->has_text[1]) draw_content(t,0,16,0,upper_right-16,top);
    if(t->has_text[2]) draw_content(t,1,header+16,top,w-header-32,bottom);
    gs_matrix_pop();gs_blend_state_pop(); ReleaseSRWLockShared(&t->lock);
}
static void enumerate(void *data,obs_source_enum_proc_t callback,void *param)
{
    struct ticker *t=data;
    for(int i=0;i<TEXT_COUNT;i++) callback(t->self,t->text[i],param);
    for(int i=0;i<t->compact_count;i++)if(t->compact_items[i])callback(t->self,t->compact_items[i],param);
}
static bool restore_weather_capitals(obs_properties_t *p,obs_property_t *property,void *data)
{
    UNUSED_PARAMETER(p);UNUSED_PARAMETER(property);if(!data)return false;
    struct ticker *t=data;obs_data_t *settings=obs_source_get_settings(t->self);obs_data_array_t *list=default_weather_locations();
    obs_data_set_array(settings,"weather_locations",list);obs_data_array_release(list);obs_data_set_bool(settings,"weather_rotate",true);
    obs_source_update(t->self,settings);obs_data_release(settings);return true;
}
static bool weather_changed(obs_properties_t *p,obs_property_t *property,obs_data_t *s)
{
    while(obs_properties_get_parent(p))p=obs_properties_get_parent(p);
    UNUSED_PARAMETER(property);
    bool news=obs_data_get_int(s,"style")!=STYLE_BASIC,automatic=obs_data_get_int(s,"weather_mode")==1;
    obs_property_set_visible(obs_properties_get(p,"weather_mode"),news);
    obs_property_set_visible(obs_properties_get(p,"weather_text"),news&&!automatic);
    obs_property_set_visible(obs_properties_get(p,"weather_locations"),news&&automatic);
    obs_property_set_visible(obs_properties_get(p,"weather_capitals"),news&&automatic);
    obs_property_set_visible(obs_properties_get(p,"weather_rotate"),news&&automatic);
    bool rotating=obs_data_get_bool(s,"weather_rotate");

    obs_property_set_visible(obs_properties_get(p,"weather_interval"),news&&automatic&&rotating);
    obs_property_set_visible(obs_properties_get(p,"weather_status"),news&&automatic);
    obs_property_set_visible(obs_properties_get(p,"weather_info"),news&&automatic);
    return true;
}
struct style_editor_state { int selected; };
static void set_palette(obs_data_t *s);
static bool style_changed(obs_properties_t *p,obs_property_t *property,obs_data_t *s)
{
    while(obs_properties_get_parent(p))p=obs_properties_get_parent(p);
    UNUSED_PARAMETER(property);
    struct style_editor_state *editor=obs_properties_get_param(p);
    int selected=(int)obs_data_get_int(s,"style");
    // Track this editor, not the source: OBS also invokes callbacks on opening
    // properties and while rebuilding controls after a custom color edit.
    if(editor){
        if(editor->selected>=0&&editor->selected!=selected)set_palette(s);
        editor->selected=selected;
    }
    bool news=obs_data_get_int(s,"style")!=STYLE_BASIC;
    const char *keys[]={"header_subtitle","upper_label","lower_label","map_enabled","map_animated","map_speed","map_intensity","weather_text","apply_reference","reference_info"};
    for(size_t i=0;i<sizeof(keys)/sizeof(keys[0]);i++) obs_property_set_visible(obs_properties_get(p,keys[i]),news);
    const char *map_keys[]={"map_enabled","map_animated","map_speed","map_intensity"};
    for(size_t i=0;i<sizeof(map_keys)/sizeof(map_keys[0]);i++)obs_property_set_visible(obs_properties_get(p,map_keys[i]),style_has_map((int)obs_data_get_int(s,"style")));
    weather_changed(p,NULL,s);
    int style=(int)obs_data_get_int(s,"style");
    obs_property_set_visible(obs_properties_get(p,"section_map"),style_has_map(style));
    obs_property_set_visible(obs_properties_get(p,"section_weather"),news);
    obs_property_set_visible(obs_properties_get(p,"header_subtitle"),style==STYLE_NEWS);
    obs_property_set_visible(obs_properties_get(p,"upper_label"),news&&!compact_style(style));
    obs_property_set_visible(obs_properties_get(p,"lower_label"),style==STYLE_NEWS||style==STYLE_WORLD||style==STYLE_BREAKING||style==STYLE_BUSINESS||style==STYLE_SPORTS);
    return true;
}
static bool group_changed(obs_properties_t *p,obs_property_t *property,obs_data_t *s)
{
    while(obs_properties_get_parent(p))p=obs_properties_get_parent(p);
    UNUSED_PARAMETER(property);
    bool compact=obs_data_get_int(s,"style_group")==1;
    obs_property_t *list=obs_properties_get(p,"style");obs_property_list_clear(list);
    const int normal[]={STYLE_BASIC,STYLE_NEWS,STYLE_WORLD,STYLE_BREAKING,STYLE_BUSINESS,STYLE_SPORTS,STYLE_CLEAN,STYLE_MAP,STYLE_RED,STYLE_LIGHT};
    const char *keys[]={"StyleBasic","StyleNews","StyleWorld","StyleBreaking","StyleBusiness","StyleSports","StyleClean","StyleMap","StyleRed","StyleLight"};
    if(compact){
        for(int id=STYLE_PRIME;id<=STYLE_DARK;id++)obs_property_list_add_int(list,obs_module_text(broadcast_layouts[id].key),id);
        obs_property_list_add_int(list,obs_module_text("StyleUltra"),STYLE_ULTRA);
    }else for(size_t i=0;i<sizeof(normal)/sizeof(normal[0]);i++)obs_property_list_add_int(list,obs_module_text(keys[i]),normal[i]);
    int selected=(int)obs_data_get_int(s,"style");
    if(compact!=compact_style(selected))obs_data_set_int(s,"style",compact?STYLE_PRIME:STYLE_NEWS);
    return style_changed(p,NULL,s);
}
static bool motion_changed(obs_properties_t *p,obs_property_t *property,obs_data_t *s)
{
    while(obs_properties_get_parent(p))p=obs_properties_get_parent(p);
    UNUSED_PARAMETER(property);
    bool scroll=obs_data_get_int(s,"motion")==MOTION_SCROLL;
    obs_property_set_visible(obs_properties_get(p,"scroll_speed"),scroll);
    obs_property_set_visible(obs_properties_get(p,"scroll_gap"),scroll);
    bool blocks=obs_data_get_int(s,"motion")==MOTION_BLOCKS;
    const char *keys[]={"block_transition","block_hold","block_pause"};
    for(int i=0;i<3;i++) obs_property_set_visible(obs_properties_get(p,keys[i]),blocks);
    bool alternate=obs_data_get_int(s,"motion")==MOTION_ALTERNATE;
    obs_property_set_visible(obs_properties_get(p,"message_hold"),alternate);
    obs_property_set_visible(obs_properties_get(p,"message_fade"),alternate);
    return true;
}
static bool clock_changed(obs_properties_t *p,obs_property_t *property,obs_data_t *s)
{
    while(obs_properties_get_parent(p))p=obs_properties_get_parent(p);
    UNUSED_PARAMETER(property);bool enabled=obs_data_get_bool(s,"clock_enabled");
    obs_property_set_visible(obs_properties_get(p,"clock_format"),enabled);
    obs_property_set_visible(obs_properties_get(p,"clock_seconds"),enabled);
    return true;
}
static bool now_motion_changed(obs_properties_t *p,obs_property_t *property,obs_data_t *s)
{
    while(obs_properties_get_parent(p))p=obs_properties_get_parent(p);
    UNUSED_PARAMETER(property);bool moving=obs_data_get_int(s,"now_motion")==1;
    obs_property_set_visible(obs_properties_get(p,"now_speed"),moving);obs_property_set_visible(obs_properties_get(p,"now_pause"),moving);return true;
}
static bool upper_mode_changed(obs_properties_t *p,obs_property_t *property,obs_data_t *s)
{
    while(obs_properties_get_parent(p))p=obs_properties_get_parent(p);
    UNUSED_PARAMETER(property);bool external=obs_data_get_int(s,"upper_mode")!=0;
    const char *keys[]={"now_path","now_max_age","now_fallback","now_status"};
    for(int i=0;i<4;i++)obs_property_set_visible(obs_properties_get(p,keys[i]),external);
    obs_property_set_visible(obs_properties_get(p,"now_path"),obs_data_get_int(s,"upper_mode")==1);
    return true;
}
static void set_palette(obs_data_t *s)
{
    int style=(int)obs_data_get_int(s,"style");
    if(style>=STYLE_LIGHT&&style<=STYLE_DARK){
        const struct broadcast_layout *d=&broadcast_layouts[style];
        obs_data_set_int(s,"header_color",d->header_fg);obs_data_set_int(s,"text_color",d->upper_fg);
        obs_data_set_int(s,"lower_text_color",d->lower_fg);obs_data_set_int(s,"header_bg",d->header_bg);
        obs_data_set_int(s,"upper_bg",d->upper_bg);obs_data_set_int(s,"lower_bg",d->lower_bg);return;
    }
    bool news=style!=STYLE_BASIC,light=style==STYLE_LIGHT;
    obs_data_set_int(s,"header_color",light?0xffd64c05:0xffffffff);
    obs_data_set_int(s,"text_color",light?0xff281608:0xffffffff);
    obs_data_set_int(s,"lower_text_color",news?0xff21170e:0xffffffff);
    obs_data_set_int(s,"header_bg",light?0xfffdfbf8:news?0xff1010d5:0xff2424b8);
    obs_data_set_int(s,"upper_bg",light?0xfff4e5d2:news?0xff1b0c03:0xf021140b);
    obs_data_set_int(s,"lower_bg",light?0xfffcf5ed:news?0xffe5d9ce:0xf0332012);
}
static bool apply_palette(obs_properties_t *p,obs_property_t *property,void *data)
{
    UNUSED_PARAMETER(p); UNUSED_PARAMETER(property);
    struct ticker *t=data;
    if(!t) return false;
    obs_data_t *s=obs_source_get_settings(t->self);
    set_palette(s);
    obs_source_update(t->self,s); obs_data_release(s);
    return true;
}
static bool apply_reference_layout(obs_properties_t *p,obs_property_t *property,void *data)
{
    UNUSED_PARAMETER(p);UNUSED_PARAMETER(property);struct ticker *t=data;if(!t)return false;
    obs_data_t *s=obs_source_get_settings(t->self);
    int style=(int)obs_data_get_int(s,"style");if(style<STYLE_NEWS||style>STYLE_DARK)style=STYLE_NEWS;
    obs_data_set_int(s,"style",style);obs_data_set_int(s,"width",1370);
    const struct reference_profile *profile=&reference_profiles[style];float scale=1370.f/profile->width;
    obs_data_set_int(s,"upper_height",(long long)roundf(profile->top*scale)+5+(long long)roundf(profile->above*scale));
    obs_data_set_int(s,"lower_height",(long long)roundf(profile->bottom*scale)+5);obs_data_set_int(s,"now_width",350);
    obs_data_set_int(s,"motion",MOTION_SCROLL);obs_data_set_int(s,"scroll_speed",90);
    obs_data_set_bool(s,"clock_enabled",true);obs_data_set_bool(s,"clock_seconds",false);
    obs_data_set_bool(s,"map_enabled",true);obs_data_set_bool(s,"map_animated",true);
    if(compact_style(style)){
        uint32_t height=bounded((long long)roundf(profile->top*scale)+2,32,800);
        obs_data_set_int(s,"upper_height",height/2);obs_data_set_int(s,"lower_height",height-height/2);
        obs_data_set_int(s,"now_width",style==STYLE_ULTRA?180:300);
    }
    set_palette(s);
    const char *label=obs_data_get_string(s,"lower_label");bool standard=!strcmp(label,"LATEST");
    for(int id=STYLE_LIGHT;id<=STYLE_DARK;id++)if(!strcmp(label,broadcast_layouts[id].label))standard=true;
    if(standard)obs_data_set_string(s,"lower_label",style==STYLE_NEWS?"LATEST":broadcast_layouts[style].label);
    obs_data_t *font=obs_data_create();obs_data_set_string(font,"face","Arial");obs_data_set_int(font,"size",(long long)roundf(profile->body_size*scale));obs_data_set_int(font,"flags",0);
    obs_data_set_obj(s,"font",font);obs_data_release(font);obs_source_update(t->self,s);obs_data_release(s);
    return true;
}
static bool connect_twitch(obs_properties_t *p,obs_property_t *property,void *data)
{
    UNUSED_PARAMETER(p);UNUSED_PARAMETER(property);UNUSED_PARAMETER(data);
    if(!twitch_observer_authorize())return true;
    char url[2048];wchar_t wide[2048];
    if(twitch_observer_authorization_url(url,sizeof(url))&&
       MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,url,-1,wide,2048))
        ShellExecuteW(NULL,L"open",wide,NULL,NULL,SW_SHOWNORMAL);
    return true;
}
static bool disconnect_twitch(obs_properties_t *p,obs_property_t *property,void *data)
{
    UNUSED_PARAMETER(p);UNUSED_PARAMETER(property);UNUSED_PARAMETER(data);
    twitch_observer_disconnect();return true;
}
static bool refresh_status(obs_properties_t *p,obs_property_t *property,void *data)
{UNUSED_PARAMETER(p);UNUSED_PARAMETER(property);UNUSED_PARAMETER(data);return true;}
static bool hype_changed(obs_properties_t *p,obs_property_t *property,obs_data_t *settings)
{
 UNUSED_PARAMETER(property);while(obs_properties_get_parent(p))p=obs_properties_get_parent(p);
 bool enabled=obs_data_get_bool(settings,"hype_enabled"),custom=obs_data_get_bool(settings,"hype_custom_colors");
 const char *keys[]={"hype_width","hype_height","hype_preview","hype_blink","hype_blink_ms","hype_custom_colors","hype_info"};
 for(unsigned i=0;i<sizeof(keys)/sizeof(keys[0]);i++)obs_property_set_visible(obs_properties_get(p,keys[i]),enabled);
 const char *colors[]={"hype_color","hype_bg","hype_accent"};
 for(unsigned i=0;i<3;i++)obs_property_set_visible(obs_properties_get(p,colors[i]),enabled&&custom);return true;
}
static obs_properties_t *properties(void *data)
{
    obs_properties_t *p=obs_properties_create();
    struct style_editor_state *editor=bzalloc(sizeof(*editor));editor->selected=-1;
    obs_properties_set_param(p,editor,bfree);
    obs_properties_t *design_props=obs_properties_create();
    obs_properties_add_group(p,"section_design",obs_module_text("SectionDesign"),OBS_GROUP_NORMAL,design_props);
    obs_properties_t *header_props=obs_properties_create();
    obs_properties_add_group(p,"section_header",obs_module_text("SectionHeader"),OBS_GROUP_NORMAL,header_props);
    obs_properties_t *now_props=obs_properties_create();
    obs_properties_add_group(p,"section_now",obs_module_text("SectionNow"),OBS_GROUP_NORMAL,now_props);
    obs_properties_t *news_props=obs_properties_create();
    obs_properties_add_group(p,"section_news",obs_module_text("SectionNews"),OBS_GROUP_NORMAL,news_props);
    obs_properties_t *hype_props=obs_properties_create();
    obs_properties_add_group(p,"section_hype",obs_module_text("SectionHype"),OBS_GROUP_NORMAL,hype_props);
    obs_property_t *hype=obs_properties_add_bool(hype_props,"hype_enabled",obs_module_text("HypeEnabled"));obs_property_set_modified_callback(hype,hype_changed);
    obs_properties_add_int(hype_props,"hype_width",obs_module_text("HypeWidth"),0,7680,1);
    obs_properties_add_int(hype_props,"hype_height",obs_module_text("HypeHeight"),16,200,1);
    obs_properties_add_bool(hype_props,"hype_blink",obs_module_text("HypeBlink"));
    obs_properties_add_int(hype_props,"hype_blink_ms",obs_module_text("HypeBlinkSpeed"),200,4000,50);
    obs_properties_add_bool(hype_props,"hype_preview",obs_module_text("HypePreview"));
    obs_property_t *custom=obs_properties_add_bool(hype_props,"hype_custom_colors",obs_module_text("HypeCustom"));obs_property_set_modified_callback(custom,hype_changed);
    obs_properties_add_color_alpha(hype_props,"hype_color",obs_module_text("HypeColor"));
    obs_properties_add_color_alpha(hype_props,"hype_bg",obs_module_text("HypeBackground"));
    obs_properties_add_color_alpha(hype_props,"hype_accent",obs_module_text("HypeAccent"));
    obs_properties_add_text(hype_props,"hype_info",obs_module_text("HypeInfo"),OBS_TEXT_INFO);
    obs_properties_t *clock_props=obs_properties_create();
    obs_properties_add_group(p,"section_clock",obs_module_text("SectionClock"),OBS_GROUP_NORMAL,clock_props);
    obs_properties_t *weather_props=obs_properties_create();
    obs_properties_add_group(p,"section_weather",obs_module_text("SectionWeather"),OBS_GROUP_NORMAL,weather_props);
    obs_properties_t *map_props=obs_properties_create();
    obs_properties_add_group(p,"section_map",obs_module_text("SectionMap"),OBS_GROUP_NORMAL,map_props);
    obs_properties_t *appearance_props=obs_properties_create();
    obs_properties_add_group(p,"section_appearance",obs_module_text("SectionAppearance"),OBS_GROUP_NORMAL,appearance_props);
    obs_properties_t *twitch_props=obs_properties_create();
    obs_properties_add_group(p,"section_twitch",obs_module_text("SectionTwitch"),OBS_GROUP_NORMAL,twitch_props);

    obs_properties_add_text(p,"stage",obs_module_text("Stage"),OBS_TEXT_INFO);
    obs_property_t *group=obs_properties_add_list(design_props,"style_group",obs_module_text("StyleGroup"),OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(group,obs_module_text("GroupNormal"),0);obs_property_list_add_int(group,obs_module_text("GroupCompact"),1);
    obs_property_set_modified_callback(group,group_changed);
    obs_property_t *style=obs_properties_add_list(design_props,"style",obs_module_text("Style"),OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(style,obs_module_text("StyleBasic"),STYLE_BASIC);
    obs_property_list_add_int(style,obs_module_text("StyleNews"),STYLE_NEWS);
    obs_property_list_add_int(style,obs_module_text("StyleWorld"),STYLE_WORLD);
    obs_property_list_add_int(style,obs_module_text("StyleBreaking"),STYLE_BREAKING);
    obs_property_list_add_int(style,obs_module_text("StyleBusiness"),STYLE_BUSINESS);
    obs_property_list_add_int(style,obs_module_text("StyleSports"),STYLE_SPORTS);
    obs_property_list_add_int(style,obs_module_text("StyleClean"),STYLE_CLEAN);
    obs_property_list_add_int(style,obs_module_text("StyleMap"),STYLE_MAP);
    obs_property_list_add_int(style,obs_module_text("StyleRed"),STYLE_RED);
    obs_property_list_add_int(style,obs_module_text("StyleLight"),STYLE_LIGHT);
    obs_property_list_add_int(style,obs_module_text("StyleUltra"),STYLE_ULTRA);
    obs_property_set_modified_callback(style,style_changed);
    obs_properties_add_button2(design_props,"apply_palette",obs_module_text("ApplyPalette"),apply_palette,data);
    obs_properties_add_button2(design_props,"apply_reference",obs_module_text("ApplyReference"),apply_reference_layout,data);
    obs_properties_add_text(design_props,"reference_info",obs_module_text("ReferenceInfo"),OBS_TEXT_INFO);
    obs_property_t *motion=obs_properties_add_list(news_props,"motion",obs_module_text("Motion"),OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(motion,obs_module_text("MotionStatic"),MOTION_STATIC);
    obs_property_list_add_int(motion,obs_module_text("MotionScroll"),MOTION_SCROLL);
    obs_property_list_add_int(motion,obs_module_text("MotionBlocks"),MOTION_BLOCKS);
    obs_property_list_add_int(motion,obs_module_text("MotionAlternate"),MOTION_ALTERNATE);
    obs_property_set_modified_callback(motion,motion_changed);
    obs_properties_add_int_slider(news_props,"scroll_speed",obs_module_text("ScrollSpeed"),10,400,1);
    obs_properties_add_int(news_props,"scroll_gap",obs_module_text("ScrollGap"),16,600,1);
    obs_properties_add_int(news_props,"block_transition",obs_module_text("BlockTransition"),150,2000,10);
    obs_properties_add_int(news_props,"block_hold",obs_module_text("BlockHold"),500,20000,100);
    obs_properties_add_int(news_props,"block_pause",obs_module_text("BlockPause"),100,5000,50);
    obs_properties_add_int(news_props,"message_hold",obs_module_text("MessageHold"),500,30000,100);
    obs_properties_add_int(news_props,"message_fade",obs_module_text("MessageFade"),100,1000,10);
    obs_properties_add_int(design_props,"width",obs_module_text("Width"),320,7680,1);
    obs_properties_add_int(design_props,"upper_height",obs_module_text("UpperHeight"),16,400,1);
    obs_properties_add_int(design_props,"lower_height",obs_module_text("LowerHeight"),16,400,1);
    obs_property_t *clock=obs_properties_add_bool(clock_props,"clock_enabled",obs_module_text("ClockEnabled"));
    obs_property_set_modified_callback(clock,clock_changed);
    obs_property_t *format=obs_properties_add_list(clock_props,"clock_format",obs_module_text("ClockFormat"),OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(format,obs_module_text("Clock24"),0);
    obs_property_list_add_int(format,obs_module_text("Clock12"),1);
    obs_properties_add_bool(clock_props,"clock_seconds",obs_module_text("ClockSeconds"));
    obs_properties_add_text(header_props,"header_text",obs_module_text("Header"),OBS_TEXT_DEFAULT);
    obs_properties_add_text(header_props,"header_subtitle",obs_module_text("HeaderSubtitle"),OBS_TEXT_DEFAULT);
    obs_properties_add_text(now_props,"upper_label",obs_module_text("UpperLabel"),OBS_TEXT_DEFAULT);
    obs_properties_add_text(news_props,"lower_label",obs_module_text("LowerLabel"),OBS_TEXT_DEFAULT);
    obs_properties_add_color_alpha(header_props,"header_color",obs_module_text("HeaderColor"));
    obs_properties_add_color_alpha(header_props,"header_bg",obs_module_text("HeaderBackground"));
    obs_properties_add_font(appearance_props,"font",obs_module_text("Font"));
    obs_property_t *upper=obs_properties_add_list(now_props,"upper_mode",obs_module_text("UpperMode"),OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(upper,obs_module_text("UpperManual"),0);obs_property_list_add_int(upper,obs_module_text("UpperJson"),1);
    obs_property_list_add_int(upper,obs_module_text("UpperPlaying"),2);
    obs_property_set_modified_callback(upper,upper_mode_changed);
    obs_properties_add_path(now_props,"now_path",obs_module_text("NowPath"),OBS_PATH_FILE,"JSON (*.json)",NULL);
    obs_properties_add_int(now_props,"now_max_age",obs_module_text("NowMaxAge"),3,300,1);
    obs_properties_add_bool(now_props,"now_fallback",obs_module_text("NowFallback"));
    const char *status_keys[]={"NowDisabled","NowWaiting","NowPlaying","NowPaused","NowStopped","NowMissing","NowInvalid","NowStale","NowBadPath","NowIoError","NowFuture","NowProviderUnsupported","NowProviderAmbiguous"};
    enum np_status status=NP_DISABLED;
    if(data){struct ticker *t=data;AcquireSRWLockShared(&t->lock);status=t->now_status;ReleaseSRWLockShared(&t->lock);}
    obs_properties_add_text(now_props,"now_status",obs_module_text(status_keys[status]),OBS_TEXT_INFO);
    obs_properties_add_text(now_props,"upper_text",obs_module_text("Upper"),OBS_TEXT_DEFAULT);
    obs_properties_add_bool(design_props,"info_align_right",obs_module_text("InfoAlignRight"));
    obs_properties_add_int(weather_props,"weather_width",obs_module_text("WeatherWidth"),80,2000,10);
    obs_properties_add_int(now_props,"now_width",obs_module_text("NowWidth"),80,7680,1);
    obs_property_t *now_motion=obs_properties_add_list(now_props,"now_motion",obs_module_text("NowMotion"),OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(now_motion,obs_module_text("NowFixed"),0);obs_property_list_add_int(now_motion,obs_module_text("NowPingPong"),1);obs_property_set_modified_callback(now_motion,now_motion_changed);
    obs_properties_add_int(now_props,"now_speed",obs_module_text("NowSpeed"),10,200,1);
    obs_properties_add_int(now_props,"now_pause",obs_module_text("NowPause"),0,5000,100);
    obs_properties_add_bool(map_props,"map_enabled",obs_module_text("MapEnabled"));
    obs_properties_add_bool(map_props,"map_animated",obs_module_text("MapAnimated"));
    obs_properties_add_int_slider(map_props,"map_speed",obs_module_text("MapSpeed"),25,200,5);
    obs_properties_add_int_slider(map_props,"map_intensity",obs_module_text("MapIntensity"),0,200,5);
    obs_property_t *weather=obs_properties_add_list(weather_props,"weather_mode",obs_module_text("WeatherMode"),OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(weather,obs_module_text("WeatherManual"),0);
    obs_property_list_add_int(weather,obs_module_text("WeatherAuto"),1);
    obs_property_set_modified_callback(weather,weather_changed);
    obs_properties_add_editable_list(weather_props,"weather_locations",obs_module_text("WeatherLocations"),OBS_EDITABLE_LIST_TYPE_STRINGS,NULL,NULL);
    obs_properties_add_button2(weather_props,"weather_capitals",obs_module_text("WeatherCapitals"),restore_weather_capitals,data);
    obs_property_t *rotate=obs_properties_add_bool(weather_props,"weather_rotate",obs_module_text("WeatherRotate"));
    obs_property_set_modified_callback(rotate,weather_changed);
    obs_properties_add_int(weather_props,"weather_interval",obs_module_text("WeatherInterval"),5,300,1);
    obs_properties_add_text(weather_props,"weather_text",obs_module_text("WeatherText"),OBS_TEXT_DEFAULT);
    const char *weather_keys[]={"WeatherManual","WeatherWaiting","WeatherReady","WeatherLocation","WeatherNetwork","WeatherInvalid","WeatherStale"};
    enum weather_status ws=WEATHER_MANUAL;char weather_status_text[512];char resolved[512]="";
    if(data){struct ticker *t=data;AcquireSRWLockShared(&t->lock);ws=t->weather_status;snprintf(resolved,sizeof(resolved),"%s",t->weather_value);ReleaseSRWLockShared(&t->lock);}
    snprintf(weather_status_text,sizeof(weather_status_text),"%s%s%.350s",obs_module_text(weather_keys[ws]),*resolved?" — ":"",resolved);
    obs_properties_add_text(weather_props,"weather_status",weather_status_text,OBS_TEXT_INFO);
    obs_properties_add_text(weather_props,"weather_info",obs_module_text("WeatherInfo"),OBS_TEXT_INFO);
    obs_properties_add_text(news_props,"lower_text",obs_module_text("Lower"),OBS_TEXT_DEFAULT);
    obs_properties_add_bool(twitch_props,"raid_enabled",obs_module_text("RaidEnabled"));
    const char *event_keys[]={"twitch_follow","twitch_sub","twitch_cheer","twitch_gift","twitch_daily_subs","twitch_daily_bits"};
    const char *event_labels[]={"TwitchFollow","TwitchSub","TwitchCheer","TwitchGift","TwitchDailySubs","TwitchDailyBits"};
    for(unsigned i=0;i<6;i++)obs_properties_add_bool(twitch_props,event_keys[i],obs_module_text(event_labels[i]));
    obs_properties_add_text(twitch_props,"raid_info",obs_module_text("RaidInfo"),OBS_TEXT_INFO);
    const char *labels[]={"Message2","Message3","Message4","Message5"};
    for(int i=1;i<5;i++) obs_properties_add_text(news_props,message_keys[i],obs_module_text(labels[i-1]),OBS_TEXT_DEFAULT);
    obs_properties_add_text(news_props,"sequence_info",obs_module_text("SequenceInfo"),OBS_TEXT_INFO);
    struct twitch_snapshot twitch;twitch_observer_read(&twitch);
    const char *twitch_keys[]={"TwitchMissing","TwitchWaiting","TwitchReady","TwitchClosed"};
    obs_properties_add_text(twitch_props,"twitch_status",obs_module_text(twitch_keys[twitch.status]),OBS_TEXT_INFO);
    obs_property_t *connect=obs_properties_add_button2(twitch_props,"twitch_connect",obs_module_text("TwitchConnect"),connect_twitch,data);
    obs_property_set_enabled(connect,twitch.feed_version!=0);
    obs_properties_add_button2(twitch_props,"twitch_disconnect",obs_module_text("TwitchDisconnect"),disconnect_twitch,data);
    obs_properties_add_text(twitch_props,"twitch_shared_info",obs_module_text("TwitchSharedInfo"),OBS_TEXT_INFO);
    if(!twitch.feed_version)obs_properties_add_text(twitch_props,"twitch_upgrade_info",obs_module_text("TwitchUpgradeInfo"),OBS_TEXT_INFO);
    if(twitch.last_raid[0])obs_properties_add_text(twitch_props,"twitch_last_raid",twitch.last_raid,OBS_TEXT_INFO);
    obs_properties_add_color_alpha(appearance_props,"text_color",obs_module_text("TextColor"));
    obs_properties_add_color_alpha(appearance_props,"lower_text_color",obs_module_text("LowerTextColor"));
    obs_properties_add_color_alpha(appearance_props,"upper_bg",obs_module_text("UpperBackground"));
    obs_properties_add_color_alpha(appearance_props,"lower_bg",obs_module_text("LowerBackground"));
    obs_properties_add_button2(p,"refresh_status",obs_module_text("RefreshStatus"),refresh_status,data);
    obs_properties_add_text(p,"status_hint",obs_module_text("StatusHint"),OBS_TEXT_INFO);
    obs_properties_add_text(p,"credit","a plugin from <a href=\"https://discord.gg/dhcqZPDYh\">DjEmiMas</a>",OBS_TEXT_INFO);
    obs_data_t *settings=data?obs_source_get_settings(((struct ticker *)data)->self):obs_data_create();
    group_changed(p,NULL,settings);now_motion_changed(p,NULL,settings);hype_changed(p,NULL,settings);obs_data_release(settings);
    return p;
}
static struct obs_source_info info={
    .id="obs_news_ticker_nexus", .type=OBS_SOURCE_TYPE_INPUT,
    .output_flags=OBS_SOURCE_VIDEO|OBS_SOURCE_CUSTOM_DRAW,
    .get_name=name,.create=create,.destroy=destroy,.update=update,
    .get_defaults=defaults,.get_properties=properties,
    .get_width=width,.get_height=height,.video_render=render,
    .video_tick=tick,
    .enum_active_sources=enumerate,.enum_all_sources=enumerate,
    .icon_type=OBS_ICON_TYPE_TEXT,
};
bool obs_module_load(void)
{
    if(!np_start())return false;
    if(!weather_start()){np_stop();return false;}
    obs_register_source(&info);
    blog(LOG_INFO,"[news-ticker-nexus] " NEXUS_TICKER_RELEASE " loaded");
    return true;
}
static HMODULE owned_core;
static bool retained_core;
static void (*release_core)(void);
static void detach_twitch(void)
{
    twitch_observer_detach();
    if(retained_core){retained_core=false;release_core();}
}
static void attach_twitch(void)
{
    /* UI thread. Reuse the shared owner or retain the same runtime standalone. */
    if(retained_core)return;
    obs_module_t *overlay=obs_get_module("obs-twitch-overlays");
    if(overlay&&!os_dlsym(obs_get_module_lib(overlay),"nexus_overlay_uses_shared_runtime")){
        blog(LOG_WARNING,"[news-ticker-nexus] Legacy Overlay owns a private connection; shared runtime blocked");return;
    }
    HMODULE core=owned_core;
    if(!core&&GetModuleHandleExW(0,L"nexus-twitch-core.dll",&core))owned_core=core;
    if(!core){
        wchar_t path[32768];DWORD size=GetModuleFileNameW(NULL,path,32768);
        if(!size||size>=32768)return;
        wchar_t *slash=wcsrchr(path,L'\\');if(!slash)return;
        size_t prefix=(size_t)(slash-path)+1;
        if(prefix+wcslen(L"nexus-twitch-core.dll")>=32768)return;
        wcscpy(path+prefix,L"nexus-twitch-core.dll");
        core=LoadLibraryExW(path,NULL,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        owned_core=core;
    }
    if(!core)return;
    int (*get_info)(NexusTwitchCoreInfo *,uint32_t)=(void *)GetProcAddress(core,"nexus_twitch_core_info");
    int (*retain)(const char *)=(void *)GetProcAddress(core,"nexus_twitch_runtime_retain");
    release_core=(void *)GetProcAddress(core,"nexus_twitch_runtime_release");
    if(get_info&&retain&&release_core){
        NexusTwitchCoreInfo identity={0};unsigned major=0,minor=0,patch=0;
        if(!get_info(&identity,sizeof(identity))||!memchr(identity.version,0,sizeof(identity.version))||identity.abi!=NEXUS_TWITCH_ABI||
           sscanf(identity.version,"%u.%u.%u",&major,&minor,&patch)!=3||major!=1||minor<1){
            blog(LOG_WARNING,"[news-ticker-nexus] Incompatible shared Twitch Core; startup blocked");return;
        }
        void (*set_locale)(const char *)=(void *)GetProcAddress(core,"nexus_twitch_runtime_set_locale");
        if(set_locale)set_locale(obs_get_locale());
        char *path=obs_module_config_path("../nexus-twitch-core/twitch-session.bin");
        retained_core=path&&retain(path)!=0;bfree(path);
        if(!retained_core)return;
        blog(LOG_INFO,"[news-ticker-nexus] Twitch Core %s ABI 6 runtime retained",identity.version);
    }else return;
    NexusTwitchGetApi get_api=(NexusTwitchGetApi)(void *)GetProcAddress(core,"nexus_twitch_core_get_api");
    if(get_api&&twitch_observer_attach(get_api(NEXUS_TWITCH_ABI,sizeof(NexusTwitchApi)))){
        NexusTickerGetApi get_feed=(NexusTickerGetApi)(void *)GetProcAddress(core,"nexus_twitch_core_get_ticker_api");
        if(get_feed)twitch_observer_attach_feed(get_feed(NEXUS_TICKER_VERSION,sizeof(NexusTickerApi)));
        NexusHypeGetApi get_hype=(NexusHypeGetApi)(void *)GetProcAddress(core,"nexus_twitch_core_get_hype_api");
        if(get_hype)twitch_observer_attach_hype(get_hype(NEXUS_HYPE_VERSION,sizeof(NexusHypeApi)));
        NexusHypeGetApi get_preview=(NexusHypeGetApi)(void *)GetProcAddress(core,"nexus_twitch_core_get_hype_preview_api");
        if(get_preview)twitch_observer_attach_hype_preview(get_preview(NEXUS_HYPE_VERSION,sizeof(NexusHypeApi)));
        blog(LOG_INFO,"[news-ticker-nexus] Shared Twitch observer attached (ABI 6)");
    }else detach_twitch();
}
typedef void (*frontend_callback_fn)(obs_frontend_event_cb,void *);
static frontend_callback_fn remove_frontend_callback;
static bool frontend_registered;
static void frontend_event(enum obs_frontend_event event,void *unused)
{
    UNUSED_PARAMETER(unused);
    if(event==OBS_FRONTEND_EVENT_FINISHED_LOADING)attach_twitch();
    else if(event==OBS_FRONTEND_EVENT_EXIT)detach_twitch();
}
void obs_module_post_load(void)
{
    /* Module post-load callbacks run in reverse load order: an owner's runtime
     * may not exist yet. Wait until all owners and scenes finished loading.
     * Resolve frontend callbacks dynamically to keep headless libobs supported. */
    if(frontend_registered)return;
    HMODULE frontend=GetModuleHandleW(L"obs-frontend-api.dll");
    frontend_callback_fn add=frontend?(frontend_callback_fn)(void *)GetProcAddress(frontend,"obs_frontend_add_event_callback"):NULL;
    remove_frontend_callback=frontend?(frontend_callback_fn)(void *)GetProcAddress(frontend,"obs_frontend_remove_event_callback"):NULL;
    if(add&&remove_frontend_callback){add(frontend_event,NULL);frontend_registered=true;}
    else attach_twitch();
}
void obs_module_unload(void)
{
    if(frontend_registered){remove_frontend_callback(frontend_event,NULL);frontend_registered=false;}
    detach_twitch();if(owned_core){FreeLibrary(owned_core);owned_core=NULL;}weather_stop();np_stop();
}
