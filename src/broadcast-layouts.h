/* SPDX-License-Identifier: GPL-2.0-or-later
 * Coordinates measured from the supplied 1536x1024 broadcast sheet.
 * Native panels, editable text and a shared motion engine; no baked-in titles.
 */
struct broadcast_layout {
    float width,header_x,header_w,upper_x,song_x,clock_x,weather_x;
    float lower_panel,lower_label_x,lower_label_w,lower_x,lower_end;
    uint32_t header_bg,upper_bg,lower_bg,header_fg,upper_fg,lower_fg;
    const char *key,*label;
};
static const struct broadcast_layout broadcast_layouts[]={
    [STYLE_LIGHT]={1172,0,164,176,340,752,897,156,0,0,168,1172,0xfffdfbf8,0xfff4e5d2,0xfffcf5ed,0xffd64c05,0xff281608,0xff21170e,"StyleLight",""},
    [STYLE_WORLD]={1082,0,122,138,281,625,760,0,0,122,134,1060,0xff321405,0xff301104,0xff431704,0xffffffff,0xffffffff,0xffffffff,"StyleWorld","STREAM NEWS"},
    [STYLE_BREAKING]={1212,22,174,212,395,680,800,174,174,130,308,1175,0xff1010bd,0xff090707,0xffe4d9d2,0xffffffff,0xffffffff,0xff17100a,"StyleBreaking","BREAKING"},
    [STYLE_BUSINESS]={1212,0,190,220,400,760,900,160,160,140,310,1070,0xff57321a,0xff301a0b,0xffdbcec1,0xffffffff,0xffffffff,0xff17100a,"StyleBusiness","STREAM STATS"},
    [STYLE_SPORTS]={1212,38,154,215,380,740,890,38,38,100,158,1200,0xffdf5505,0xff190b03,0xff190b03,0xffffffff,0xffffffff,0xffffffff,"StyleSports","LIVE"},
    [STYLE_CLEAN]={1207,22,156,200,375,800,950,24,0,0,38,1207,0xff211a17,0xee160e08,0xee211a15,0xfff2e7e2,0xffffffff,0xffffffff,"StyleClean",""},
    [STYLE_MAP]={1172,0,160,180,355,680,820,110,0,0,125,1000,0xffa7570c,0xff291203,0xff381503,0xffffffff,0xffffffff,0xffffffff,"StyleMap",""},
    [STYLE_RED]={1232,59,143,225,400,800,940,52,0,0,166,1200,0xff100e10,0xff050303,0xff0c0907,0xffffffff,0xffffffff,0xffffffff,"StyleRed",""},
    [STYLE_ULTRA]={1472,0,90,110,136,328,434,0,0,0,560,1456,0xff470921,0xff100704,0xff100704,0xffffffff,0xffffffff,0xffffffff,"StyleUltra",""},
    [STYLE_PRIME]={1288,42,109,175,206,494,595,0,0,0,725,1272,0xff0707b5,0xff170c05,0xff170c05,0xffffffff,0xffffffff,0xffffffff,"StylePrime",""},
    [STYLE_GLASS]={1288,0,178,195,230,510,610,0,0,0,735,1272,0xffff8e00,0xff261308,0xff261308,0xffffffff,0xffffffff,0xffffffff,"StyleGlass",""},
    [STYLE_NEON]={1288,0,178,195,228,510,610,0,0,0,735,1272,0xffe500c5,0xff180805,0xff180805,0xffffffff,0xffffffff,0xffffffff,"StyleNeon",""},
    [STYLE_SPORT]={1288,34,128,195,244,480,595,0,0,0,725,1272,0xfff8f6f2,0xfff8eee3,0xfff8eee3,0xff110b07,0xff110b07,0xff110b07,"StyleSport",""},
    [STYLE_MAGAZINE]={1288,18,160,195,244,510,610,0,0,0,735,1272,0xfff00091,0xff160710,0xff160710,0xffffffff,0xffffffff,0xffffffff,"StyleMagazine",""},
    [STYLE_DARK]={1288,22,128,165,202,400,505,0,0,0,715,1272,0xff242019,0xff100c07,0xff100c07,0xffffffff,0xffffffff,0xffffffff,"StyleDark",""}
};
static bool style_has_map(int style)
{return style==STYLE_NEWS||style==STYLE_WORLD||style==STYLE_MAP||style==STYLE_SPORTS;}
static uint32_t alpha_color(uint32_t color,float gain)
{return (color&0xffffff)|((uint32_t)((color>>24)*gain)<<24);}
static void slanted_panel(float x,float y,float w,float h,float cut,uint32_t color)
{
    if(w<=0||h<=0)return;
    struct vec4 white;vec4_set(&white,1,1,1,1);
    gs_effect_t *fx=obs_get_base_effect(OBS_EFFECT_SOLID);
    gs_effect_set_vec4(gs_effect_get_param_by_name(fx,"color"),&white);
    uint32_t colors[]={shade(color,1.12f),shade(color,.72f)};
    for(int i=0;i<2;i++){
        uint32_t a=colors[i]>>24,c=colors[i]&0xff000000;
        for(int shift=0;shift<24;shift+=8)c|=((((colors[i]>>shift)&255)*a+127)/255)<<shift;
        colors[i]=c;
    }
    while(gs_effect_loop(fx,"SolidColored")){
        gs_render_start(true);
        gs_color(colors[0]);gs_vertex2f(x+cut,y);
        gs_color(colors[0]);gs_vertex2f(x+w,y);
        gs_color(colors[1]);gs_vertex2f(x,y+h);
        gs_color(colors[1]);gs_vertex2f(x+w-cut,y+h);
        gs_render_stop(GS_TRISTRIP);
    }
}
static void chevron_tip(float x,float y,float h,float w,uint32_t color)
{
    float a=(color>>24)/255.f;struct vec4 c;
    vec4_set(&c,(color&255)/255.f*a,((color>>8)&255)/255.f*a,((color>>16)&255)/255.f*a,a);
    gs_effect_t *fx=obs_get_base_effect(OBS_EFFECT_SOLID);
    gs_effect_set_vec4(gs_effect_get_param_by_name(fx,"color"),&c);
    while(gs_effect_loop(fx,"Solid")){
        gs_render_start(true);gs_vertex2f(x,y);gs_vertex2f(x+w,y+h*.5f);gs_vertex2f(x,y+h);gs_render_stop(GS_TRIS);
    }
}
static void finished_trapezoid_panel(struct ticker *t,float x,float y,float w,float h,float cut,float right_cut,uint32_t color,int mode)
{
    if(w<1||h<1)return;struct vec4 c,size;float a=(color>>24)/255.f;
    vec4_set(&c,(color&255)/255.f*a,((color>>8)&255)/255.f*a,((color>>16)&255)/255.f*a,a);vec4_set(&size,w,h,0,0);
    gs_effect_set_vec4(gs_effect_get_param_by_name(t->fade_effect,"compact_bg"),&c);gs_effect_set_vec4(gs_effect_get_param_by_name(t->fade_effect,"compact_size"),&size);
    gs_effect_set_float(gs_effect_get_param_by_name(t->fade_effect,"finish_mode"),(float)mode);
    while(gs_effect_loop(t->fade_effect,"Finish")){
        gs_render_start(true);
        gs_texcoord(0,0,0);gs_vertex2f(x+cut,y);gs_texcoord(1,0,0);gs_vertex2f(x+w,y);
        gs_texcoord(0,1,0);gs_vertex2f(x,y+h);gs_texcoord(1,1,0);gs_vertex2f(x+w-right_cut,y+h);
        gs_render_stop(GS_TRISTRIP);
    }
}
static void finished_slanted_panel(struct ticker *t,float x,float y,float w,float h,float cut,uint32_t color,int mode)
{finished_trapezoid_panel(t,x,y,w,h,cut,cut,color,mode);}
static void chevron_band(float x,float y,float h,float run,float thickness,uint32_t color)
{
    struct vec4 c;float a=(color>>24)/255.f;vec4_set(&c,(color&255)/255.f*a,((color>>8)&255)/255.f*a,((color>>16)&255)/255.f*a,a);
    gs_effect_t *fx=obs_get_base_effect(OBS_EFFECT_SOLID);gs_effect_set_vec4(gs_effect_get_param_by_name(fx,"color"),&c);
    while(gs_effect_loop(fx,"Solid")){
        gs_render_start(true);
        gs_vertex2f(x,y);gs_vertex2f(x+thickness,y);gs_vertex2f(x+run+thickness,y+h*.5f);
        gs_vertex2f(x,y);gs_vertex2f(x+run+thickness,y+h*.5f);gs_vertex2f(x+run,y+h*.5f);
        gs_vertex2f(x+run,y+h*.5f);gs_vertex2f(x+run+thickness,y+h*.5f);gs_vertex2f(x+thickness,y+h);
        gs_vertex2f(x+run,y+h*.5f);gs_vertex2f(x+thickness,y+h);gs_vertex2f(x,y+h);
        gs_render_stop(GS_TRIS);
    }
}
static void atlas_map_shape(struct ticker *t,float x,float y,float w,float h,float u,float v,float uw,float vh,float cut)
{
    if(!t->design_atlas||w<=0||h<=0)return;
    struct vec4 uv;vec4_set(&uv,u/1536,v/1024,uw/1536,vh/1024);
    gs_effect_set_vec4(gs_effect_get_param_by_name(t->fade_effect,"map_uv"),&uv);
    gs_effect_set_float(gs_effect_get_param_by_name(t->fade_effect,"map_clean_header"),u==332&&v==635?1:0);
    gs_effect_set_texture(gs_effect_get_param_by_name(t->fade_effect,"image"),t->design_atlas);
    gs_effect_set_float(gs_effect_get_param_by_name(t->fade_effect,"map_time"),(float)t->map_time);
    gs_effect_set_float(gs_effect_get_param_by_name(t->fade_effect,"map_intensity"),t->map_intensity/100.f);
    gs_effect_set_float(gs_effect_get_param_by_name(t->fade_effect,"map_opacity"),(t->upper_bg>>24)/255.f);
    gs_matrix_push();gs_matrix_translate3f(x,y,0);
    while(gs_effect_loop(t->fade_effect,"Map")){
        if(cut<=0)gs_draw_sprite(NULL,0,(uint32_t)w,(uint32_t)h);
        else{
            const float points[][2]={{0,0},{w-cut,0},{0,h},{w-cut,0},{w-cut,h},{0,h},{w-cut,0},{w,h*.5f},{w-cut,h}};
            gs_render_start(true);for(int i=0;i<9;i++){gs_texcoord(points[i][0]/w,points[i][1]/h,0);gs_vertex2f(points[i][0],points[i][1]);}gs_render_stop(GS_TRIS);
        }
    }
    gs_matrix_pop();
}
static void atlas_map(struct ticker *t,float x,float y,float w,float h,float u,float v,float uw,float vh)
{atlas_map_shape(t,x,y,w,h,u,v,uw,vh,0);}
static void filled_circle(float x,float y,float r,uint32_t color)
{
    float a=(color>>24)/255.f;struct vec4 c;vec4_set(&c,(color&255)/255.f*a,((color>>8)&255)/255.f*a,((color>>16)&255)/255.f*a,a);
    gs_effect_t *fx=obs_get_base_effect(OBS_EFFECT_SOLID);gs_effect_set_vec4(gs_effect_get_param_by_name(fx,"color"),&c);
    while(gs_effect_loop(fx,"Solid")){
        gs_render_start(true);for(int i=0;i<20;i++){float p=i*6.2831853f/20,q=(i+1)*6.2831853f/20;gs_vertex2f(x,y);gs_vertex2f(x+r*cosf(p),y+r*sinf(p));gs_vertex2f(x+r*cosf(q),y+r*sinf(q));}gs_render_stop(GS_TRIS);
    }
}
static void music_mark(float x,float top,uint32_t color)
{
    float h=fminf(26,top*.67f),y=(top-h)*.5f,r=h*.16f;
    filled_circle(x+5,y+h-r,r,color);filled_circle(x+18,y+h-r*1.8f,r,color);
    rectangle(x+7,y+3,2,h-r-3,color);rectangle(x+20,y,2,h-r*1.8f,color);
    rectangle(x+7,y+2,15,3,color);
}
static void broadcast_clock_weather(struct ticker *t,float clock_x,float weather_x,float top,float w)
{
    if(t->info_right){const struct broadcast_layout *layout=&broadcast_layouts[t->style];draw_right_info(t,layout->song_x*(t->width)/layout->width,top,w);return;}
    float scale=(t->width)/(float)reference_profiles[t->style].width;
    float time_text=(reference_text_anchors[t->style].clock-reference_profiles[t->style].x-2)*scale;
    float weather_text=(reference_text_anchors[t->style].weather-reference_profiles[t->style].x-2)*scale;
    if(t->clock_enabled&&t->has_text[CLOCK]){
        rectangle(clock_x-16,top*.22f,1,top*.56f,alpha_color(t->upper_color,.4f));
        float icon_width=reference_icon_boxes[t->style][1].w*(t->width)/reference_profiles[t->style].width;
        if(!reference_icon(t,1,clock_x-4,0,icon_width,top,t->upper_color))round_icon(clock_x+10,top*.5f,fminf(11,top*.19f),t->upper_color,false);
        draw_text(t,CLOCK,time_text,0,fmaxf(0,weather_x-time_text-8*scale),top);
    }
    if(t->has_text[WEATHER]){
        rectangle(weather_x-16,top*.22f,1,top*.56f,alpha_color(t->upper_color,.4f));
        bool cloud=t->style==STYLE_WORLD||t->style==STYLE_MAP||t->style==STYLE_SPORTS||t->style==STYLE_GLASS||t->style==STYLE_NEON||t->style==STYLE_MAGAZINE;
        float icon_width=reference_icon_boxes[t->style][2].w*(t->width)/reference_profiles[t->style].width;
        if(!t->weather_auto&&!reference_icon(t,2,weather_x-4,0,icon_width,top,t->upper_color)){
            if(!cloud||t->style==STYLE_SPORTS)round_icon(weather_x+10,top*.5f,fminf(14,top*.23f),(t->upper_color&0xff000000)|0x000acbff,true);
            if(cloud){float cy=top*.5f;filled_circle(weather_x+3,cy+3,5,t->upper_color);filled_circle(weather_x+10,cy-1,7,t->upper_color);filled_circle(weather_x+17,cy+3,5,t->upper_color);rectangle(weather_x+3,cy+2,14,6,t->upper_color);}
        }
        if(t->weather_auto)weather_text=weather_x-4;
        draw_text(t,WEATHER,weather_text,0,fmaxf(0,w-weather_text-6*scale),top);
    }
}
static void rounded_panel(struct ticker *t,float x,float y,float w,float h,uint32_t color)
{
    if(w<1||h<1)return;struct vec4 size,c;vec4_set(&size,w,h,0,0);float a=(color>>24)/255.f;
    vec4_set(&c,(color&255)/255.f*a,((color>>8)&255)/255.f*a,((color>>16)&255)/255.f*a,a);
    gs_effect_set_vec4(gs_effect_get_param_by_name(t->fade_effect,"compact_size"),&size);gs_effect_set_vec4(gs_effect_get_param_by_name(t->fade_effect,"compact_bg"),&c);
    gs_matrix_push();gs_matrix_translate3f(x,y,0);while(gs_effect_loop(t->fade_effect,"Panel"))gs_draw_sprite(NULL,0,(uint32_t)w,(uint32_t)h);gs_matrix_pop();
}
static void event_icon(float x,float y,float r,int kind,uint32_t opacity,int style)
{
    const uint32_t colors[]={0x003334ff,0x00ff9700,0x00ff0084,0x00e600ef,0x006300ff,0x00ff9700,0x00ff0084};
    uint32_t color=(opacity&0xff000000)|colors[kind%7];
    if(kind==0&&(style==STYLE_NEON||style==STYLE_MAGAZINE||style==STYLE_DARK))color=(opacity&0xff000000)|0x00df00f0;
    if(kind==1&&(style==STYLE_GLASS||style==STYLE_MAGAZINE||style==STYLE_DARK))color=(opacity&0xff000000)|0x00ff273b;
    struct vec4 c;float a=(color>>24)/255.f;vec4_set(&c,(color&255)/255.f*a,((color>>8)&255)/255.f*a,((color>>16)&255)/255.f*a,a);
    gs_effect_t *fx=obs_get_base_effect(OBS_EFFECT_SOLID);gs_effect_set_vec4(gs_effect_get_param_by_name(fx,"color"),&c);
    if(kind==3){
        rectangle(x-r,y-r*.35f,r*.85f,r*1.25f,color);rectangle(x+r*.15f,y-r*.35f,r*.85f,r*1.25f,color);
        rectangle(x-r*1.15f,y-r*.6f,r, r*.3f,color);rectangle(x+r*.15f,y-r*.6f,r,r*.3f,color);
    }
    gs_effect_set_vec4(gs_effect_get_param_by_name(fx,"color"),&c);
    while(gs_effect_loop(fx,"Solid")){
        gs_render_start(true);
        if(kind==0){
            for(int i=0;i<32;i++){
                float p=i*6.2831853f/32,q=(i+1)*6.2831853f/32;
                float px=powf(sinf(p),3),py=(13*cosf(p)-5*cosf(2*p)-2*cosf(3*p)-cosf(4*p))/16;
                float qx=powf(sinf(q),3),qy=(13*cosf(q)-5*cosf(2*q)-2*cosf(3*q)-cosf(4*q))/16;
                gs_vertex2f(x,y);gs_vertex2f(x+px*r,y-py*r);gs_vertex2f(x+qx*r,y-qy*r);
            }
        }else if(kind==1||kind==5){
            for(int i=0;i<10;i++){float p=i*6.2831853f/10-1.5707963f,q=(i+1)*6.2831853f/10-1.5707963f;float a1=i%2?.43f:1,a2=i%2?1:.43f;
                gs_vertex2f(x,y);gs_vertex2f(x+cosf(p)*r*a1,y+sinf(p)*r*a1);gs_vertex2f(x+cosf(q)*r*a2,y+sinf(q)*r*a2);}
        }else if(kind==3){
            gs_vertex2f(x,y-r*.55f);gs_vertex2f(x-r*.8f,y-r*1.1f);gs_vertex2f(x-r*.8f,y-r*.55f);
            gs_vertex2f(x,y-r*.55f);gs_vertex2f(x+r*.8f,y-r*1.1f);gs_vertex2f(x+r*.8f,y-r*.55f);
        }else if(kind==4){
            gs_vertex2f(x-r,y-r*.4f);gs_vertex2f(x+r,y-r);gs_vertex2f(x+r,y+r);
            gs_vertex2f(x-r,y-r*.4f);gs_vertex2f(x+r,y+r);gs_vertex2f(x-r,y+r*.4f);
            gs_vertex2f(x-r*.3f,y);gs_vertex2f(x+r*.2f,y);gs_vertex2f(x+r*.2f,y+r*1.25f);
        }else{
            gs_vertex2f(x,y-r);gs_vertex2f(x+r,y);gs_vertex2f(x,y+r);
            gs_vertex2f(x,y-r);gs_vertex2f(x,y+r);gs_vertex2f(x-r,y);
        }
        gs_render_stop(GS_TRIS);
    }
}
static float compact_item_width(struct ticker *t,int i,float band)
{
    obs_source_t *s=t->compact_items[i];if(!s)return 0;
    float h=(float)obs_source_get_height(s),scale=h>0?fminf(1,(band-6)/h):1;
    return obs_source_get_width(s)*scale*reference_body_width(t->style)+(t->compact_kind[i]>=0?(t->style==STYLE_ULTRA?19:30):0)+20;
}
static void compact_stream_copy(struct ticker *t,float x,float band)
{
    for(int i=0;i<t->compact_count;i++){
        obs_source_t *s=t->compact_items[i];if(!s)continue;
        float w=(float)obs_source_get_width(s),h=(float)obs_source_get_height(s);if(w<1||h<1)continue;
        float scale=fminf(1,(band-6)/h);
        if(t->compact_kind[i]>=0){
            int kind=t->compact_kind[i],index=kind==0?0:kind==1||kind==5?1:kind==3?2:3;
            bool drawn=false;
            if(t->style==STYLE_ULTRA&&kind<5)drawn=draw_icon_sample(t,t->design_atlas,ultra_event_boxes[kind],x-1,0,17,band,t->lower_color,2);
            if(t->style>=STYLE_PRIME&&kind!=2&&kind!=6)drawn=draw_icon_sample(t,t->compact_atlas,compact_event_boxes[t->style][index],x-5,0,34,band,t->lower_color,t->style==STYLE_SPORT?3:2);
            if(!drawn)event_icon(x+(t->style==STYLE_ULTRA?7:11),band*.5f,t->style==STYLE_ULTRA?6.5f:11,kind,t->lower_color,t->style);
            x+=t->style==STYLE_ULTRA?19:30;
        }
        if(!t->compact_capture[i])t->compact_capture[i]=gs_texrender_create(GS_RGBA,GS_ZS_NONE);
        if(!t->compact_captured[i]){
            gs_texrender_reset(t->compact_capture[i]);
            if(gs_texrender_begin(t->compact_capture[i],(uint32_t)w,(uint32_t)h)){
                struct vec4 clear;vec4_zero(&clear);gs_clear(GS_CLEAR_COLOR,&clear,0,0);gs_ortho(0,w,0,h,-100,100);
                gs_blend_state_push();gs_blend_function(GS_BLEND_ONE,GS_BLEND_ZERO);obs_source_video_render(s);gs_blend_state_pop();gs_texrender_end(t->compact_capture[i]);t->compact_captured[i]=true;
            }
        }
        if(t->compact_captured[i])composite_text(gs_texrender_get_texture(t->compact_capture[i]),x,(band-h*scale)/2,(uint32_t)(w*scale*reference_body_width(t->style)),(uint32_t)(h*scale));
        x+=w*scale*reference_body_width(t->style)+10;
        if(i<t->compact_count-1){
            if(t->style==STYLE_NEWS)filled_circle(x,band*.5f,3,(t->lower_color&0xff000000)|(t->header_bg&0xffffff));
            else rectangle(x,band*.28f,t->style==STYLE_BREAKING?2:1,band*.44f,t->style==STYLE_BREAKING?(t->lower_color&0xff000000)|(t->header_bg&0xffffff):alpha_color(t->lower_color,.55f));
        }
        x+=10;
    }
}
static void compact_stream(struct ticker *t,float x,float y,float available,float band)
{
    if(t->motion==MOTION_BLOCKS){draw_content(t,1,x,y,available,band);return;}
    if(available<1||band<1||t->message_wait)return;
    float total=0;for(int i=0;i<t->compact_count;i++)total+=compact_item_width(t,i,band);
    t->period[1]=fmaxf(total,available)+t->scroll_gap;
    if(!t->clip[1])t->clip[1]=gs_texrender_create(GS_RGBA,GS_ZS_NONE);gs_texrender_reset(t->clip[1]);
    if(!gs_texrender_begin(t->clip[1],(uint32_t)available,(uint32_t)band))return;
    struct vec4 clear;vec4_zero(&clear);gs_clear(GS_CLEAR_COLOR,&clear,0,0);gs_ortho(0,available,0,band,-100,100);
    float offset=t->motion==MOTION_SCROLL?-(float)fmod(t->phase[1],t->period[1]):0;
    compact_stream_copy(t,offset,band);if(t->motion==MOTION_SCROLL)compact_stream_copy(t,offset+(float)t->period[1],band);
    gs_texrender_end(t->clip[1]);
    float opacity=t->motion==MOTION_ALTERNATE?(float)block_reveal(t->alternate_time,t->message_fade/1000.,t->message_hold/1000.,.1):1;
    gs_effect_set_texture(gs_effect_get_param_by_name(t->fade_effect,"image"),gs_texrender_get_texture(t->clip[1]));gs_effect_set_float(gs_effect_get_param_by_name(t->fade_effect,"opacity"),opacity);
    gs_matrix_push();gs_matrix_translate3f(x,y,0);while(gs_effect_loop(t->fade_effect,"Draw"))gs_draw_sprite(NULL,0,(uint32_t)available,(uint32_t)band);gs_matrix_pop();
}
static void render_broadcast(struct ticker *t)
{
    const struct broadcast_layout *d=&broadcast_layouts[t->style];
    bool ultra=compact_style(t->style);
    float inset=ultra?1:reference_top_inset(t->style,(float)t->width,(float)t->upper_height),w=(float)t->width;
    float top=ultra?t->upper_height+t->lower_height-2:t->upper_height-inset,bottom=ultra?0:t->lower_height-5;
    float sx=w/d->width,hx=d->header_x*sx,hw=d->header_w*sx;
    float lp=d->lower_panel*sx,lower_end=d->lower_end*sx;
    bool artwork_caption=t->style>=STYLE_PRIME&&t->style<=STYLE_DARK&&t->reference_caption&&t->compact_atlas&&(t->header_bg&0xffffff)==(d->header_bg&0xffffff);
    gs_matrix_push();gs_matrix_translate3f(0,inset,0);
    if(t->style>STYLE_ULTRA){
        struct vec4 size,colors,head;vec4_set(&size,w,top,0,0);
        uint32_t bg=t->upper_bg,hc=t->header_bg;float a=(bg>>24)/255.f,ha=(hc>>24)/255.f;
        vec4_set(&colors,(bg&255)/255.f*a,((bg>>8)&255)/255.f*a,((bg>>16)&255)/255.f*a,a);
        vec4_set(&head,(hc&255)/255.f*ha,((hc>>8)&255)/255.f*ha,((hc>>16)&255)/255.f*ha,ha);
        gs_effect_set_vec4(gs_effect_get_param_by_name(t->fade_effect,"compact_size"),&size);
        gs_effect_set_vec4(gs_effect_get_param_by_name(t->fade_effect,"compact_bg"),&colors);
        gs_effect_set_vec4(gs_effect_get_param_by_name(t->fade_effect,"compact_header"),&head);
        gs_effect_set_float(gs_effect_get_param_by_name(t->fade_effect,"compact_mode"),(float)(t->style-STYLE_PRIME));
        gs_effect_set_float(gs_effect_get_param_by_name(t->fade_effect,"compact_head_width"),(d->header_x+d->header_w)*sx);
        gs_effect_set_float(gs_effect_get_param_by_name(t->fade_effect,"compact_head_left"),d->header_x*sx);
        gs_effect_set_float(gs_effect_get_param_by_name(t->fade_effect,"compact_reference_header"),t->compact_atlas&&(t->header_bg&0xffffff)==(d->header_bg&0xffffff)?1:0);
        gs_effect_set_float(gs_effect_get_param_by_name(t->fade_effect,"compact_reference_caption"),artwork_caption?1:0);
        gs_effect_set_float(gs_effect_get_param_by_name(t->fade_effect,"compact_reference_tail"),t->compact_atlas&&(t->upper_bg&0xffffff)==(d->upper_bg&0xffffff)?1:0);
        const struct reference_profile *profile=&reference_profiles[t->style];struct vec4 frame_uv;
        vec4_set(&frame_uv,profile->x/1536.f,profile->y/1024.f,profile->width/1536.f,profile->top/1024.f);
        gs_effect_set_texture(gs_effect_get_param_by_name(t->fade_effect,"image"),t->compact_atlas);
        gs_effect_set_vec4(gs_effect_get_param_by_name(t->fade_effect,"map_uv"),&frame_uv);
        while(gs_effect_loop(t->fade_effect,"Compact"))gs_draw_sprite(NULL,0,(uint32_t)w,(uint32_t)top);
    }else if(t->style==STYLE_WORLD){
        finished_panel(t,0,0,w,top+bottom,t->upper_bg,0);
        if(t->map_enabled)atlas_map(t,w-180*sx,0,180*sx,top+bottom,1300,67,200,69);
    }else if(t->style==STYLE_BREAKING||t->style==STYLE_RED){
        finished_slanted_panel(t,0,0,w,top+bottom,24*sx,t->upper_bg,0);
        if(t->style==STYLE_BREAKING)finished_slanted_panel(t,w-245*sx,0,245*sx,top+bottom,28*sx,t->header_bg,4);
    }else{
        float start=t->style==STYLE_CLEAN?22*sx:t->style==STYLE_SPORTS?38*sx:0;
        if(t->style==STYLE_LIGHT)finished_panel(t,start,0,w-start,top+bottom,t->upper_bg,8);else finished_panel(t,start,0,w-start,t->style==STYLE_MAP?top+bottom:top,t->upper_bg,0);
    }
    if(!ultra){
        if(t->style==STYLE_BREAKING||t->style==STYLE_BUSINESS||t->style==STYLE_SPORTS||t->style==STYLE_MAP||t->style==STYLE_RED)
            finished_slanted_panel(t,lp,top,lower_end-lp,bottom,10*sx,t->lower_bg,t->style==STYLE_BREAKING||t->style==STYLE_BUSINESS?1:0);
        else if(t->style==STYLE_LIGHT||t->style==STYLE_CLEAN)rounded_panel(t,lp,top,lower_end-lp,bottom,t->lower_bg);
        else finished_panel(t,lp,top,lower_end-lp,bottom,t->lower_bg,0);
    }
    if(t->style==STYLE_RED)finished_slanted_panel(t,w-245*sx,0,245*sx,top+bottom,28*sx,0xff1010a5,7);
    if(!ultra&&t->style!=STYLE_LIGHT)finished_panel(t,hx-6,2,hw+22,top+9,(uint32_t)((t->header_bg>>24)*.4f)<<24,3);
    if(t->style>STYLE_ULTRA){/* Header is part of the same procedural plate. */}
    else if(t->style==STYLE_LIGHT){
        rounded_panel(t,hx,0,hw-12*sx,top,t->header_bg);chevron_tip(hx+hw-12*sx-1,0,top,12*sx,t->header_bg);
    }else if(t->style==STYLE_BREAKING){
        finished_slanted_panel(t,0,0,198*sx,top+bottom*.55f,23*sx,t->header_bg,6);
    }else if(t->style==STYLE_MAP){
        finished_panel(t,hx,0,hw-12*sx,top,t->header_bg,2);chevron_tip(hx+hw-12*sx-1,0,top,12*sx,t->header_bg);
    }else if(t->style==STYLE_SPORTS){
        finished_slanted_panel(t,hx,0,hw,top,18*sx,t->header_bg,6);
    }else finished_panel(t,hx,0,hw,top,t->header_bg,t->style==STYLE_WORLD||t->style==STYLE_CLEAN||t->style==STYLE_RED?0:2);
    if(t->style==STYLE_BUSINESS){
        chevron_band(hx+hw-11*sx,0,top,16*sx,8*sx,shade(t->header_bg,1.8f));
        chevron_band(hx+hw-22*sx,0,top,16*sx,9*sx,0xffe9d4c3);
        for(int i=0;i<7;i++){float bh=(.22f+i*.09f)*(top+bottom);rectangle(w-(112-i*17)*sx,top+bottom-bh,11*sx,bh,alpha_color(0xffbb8b56,.4f+i*.06f));}
    }
    if(t->style==STYLE_BREAKING){
        slanted_panel(590*sx,0,25*sx,top,14*sx,t->header_bg);
        slanted_panel(609*sx,0,21*sx,top,14*sx,t->header_bg);
        slanted_panel(w-48*sx,0,35*sx,top+bottom,29*sx,t->header_bg);
        slanted_panel(w-35*sx,0,35*sx,top+bottom,29*sx,shade(t->header_bg,.60f));
    }
    if(t->style==STYLE_SPORTS){
        finished_slanted_panel(t,0,0,41*sx,top+bottom,30*sx,0xfff3eee5,9);
        finished_slanted_panel(t,14*sx,0,40*sx,top+bottom,30*sx,0xffaa4a07,9);
        if(t->map_enabled)atlas_map(t,w-155*sx,0,155*sx,top+bottom,1345,443,159,73);
    }
    if(t->style==STYLE_CLEAN){
        rectangle(hx+hw-4*sx,0,4*sx,top,0xffb62c37);
        slanted_panel(0,top,20*sx,bottom,9*sx,0xffbe3537);
    }
    if(t->style==STYLE_RED){
        for(int i=0;i<2;i++)slanted_panel(i*11*sx,0,29*sx,top+bottom,23*sx,alpha_color(0xff1010c2,1-i*.25f));
        slanted_panel(22*sx,top,16*sx,bottom,10*sx,0xff1010b5);
        rectangle(hx+hw-4*sx,0,4*sx,top,0xff1010b5);
        rectangle(hx,top-1,hw,1,0xff1010b5);
    }
    if(t->style==STYLE_MAP&&t->map_enabled){
        // Blue-only extraction discards the sheet's white lettering; the
        // header uses its own solid geography, not the dotted right map.
        atlas_map_shape(t,hx,0,hw,top,332,635,160,45,12*sx);
        atlas_map(t,w-176*sx,5-inset,176*sx,top+bottom+inset-5,1328,624,176,85);
    }
    if(t->style==STYLE_MAP)slanted_panel(lp-sx,top,11*sx,bottom,10*sx,0xffb86d59);
    if(t->style==STYLE_ULTRA){
        rectangle(0,0,w,1,0xffc47b6e);rectangle(0,top-1,w,1,0xffc47b6e);
        rectangle(hw-3*sx,0,3*sx,top,0xffee0070);
    }
    float header_offset=t->style>STYLE_ULTRA?top*.175f-4:0;
    if(t->style==STYLE_GLASS||t->style==STYLE_NEON)header_offset+=10*sx;
    if(t->has_text[HEADER]&&!artwork_caption){
        const float dx[]={-4,-3,-13,0,3,-4},dy[]={3,1,2,0,2,1};
        int badge=t->style-STYLE_PRIME;
        float tx=badge>=0&&badge<6?dx[badge]*sx:0,ty=badge>=0&&badge<6?dy[badge]*top/70:0;
        centered_text(t,HEADER,hx+7*sx+header_offset+tx,ty,hw-(t->style==STYLE_SPORT?10:18)*sx,t->style==STYLE_BREAKING?top+bottom*.35f:top);
    }
    float music=(reference_icon_boxes[t->style][0].x-reference_profiles[t->style].x)*sx,label=d->upper_x*sx+37*sx,song=d->song_x*sx;
    float note_width=reference_icon_boxes[t->style][0].w*sx;
    if(!reference_icon(t,0,music,0,note_width,top,t->upper_color))music_mark(music,top,t->upper_color);
    if(!ultra&&t->has_text[UPPER_LABEL])draw_text(t,UPPER_LABEL,label,0,fmaxf(0,song-label-12*sx),top);
    float clock=(reference_icon_boxes[t->style][1].x-reference_profiles[t->style].x+4)*sx;
    float weather=(reference_icon_boxes[t->style][2].x-reference_profiles[t->style].x+4)*sx;
    float news_start=d->lower_x*sx;
    float song_end=t->clock_enabled?clock-32*sx:t->has_text[WEATHER]?weather-32*sx:(ultra?d->lower_x*sx-20*sx:w-16);
    if(t->info_right)song_end=right_info_start(t,song,top,ultra?d->lower_x*sx-8*sx:w-10*sx);
    if(ultra){
        float info=(t->has_text[WEATHER]?t->weather_width+20.f:0)+(t->clock_enabled?clock_text_slot(t,top)+56:0);
        float available=fmaxf(0,w-song-info-120*sx);
        song_end=song+fminf((float)t->now_width,available);
        news_start=song_end+info+24*sx;
    }
    if(t->has_text[UPPER])draw_content(t,0,song,0,fmaxf(0,song_end-song),top);
    if(ultra){
        draw_right_info(t,song,top,news_start-8*sx);
        // Keep scrolling content inside the painted diagonal end caps.
        float news_end=w-(t->style==STYLE_DARK?78:t->style==STYLE_SPORT?45:12)*sx;
        if(t->has_text[LOWER])compact_stream(t,news_start,0,fmaxf(0,news_end-news_start),top);
    }else{
        broadcast_clock_weather(t,clock,weather,top,w-10*sx);
        if(d->lower_label_w>0){
            uint32_t color=t->style==STYLE_SPORTS?0xff00dfff:t->style==STYLE_WORLD||t->style==STYLE_BUSINESS?0xffe55400:t->header_bg;
            if(t->style==STYLE_SPORTS)finished_trapezoid_panel(t,d->lower_label_x*sx,top,d->lower_label_w*sx,bottom,10*sx,0,color,2);
            else finished_panel(t,d->lower_label_x*sx,top,d->lower_label_w*sx,bottom,color,2);
            if(t->has_text[LOWER_LABEL])centered_text(t,LOWER_LABEL,(d->lower_label_x+4)*sx,top,(d->lower_label_w-8)*sx,bottom);
        }
        if(t->has_text[LOWER])compact_stream(t,d->lower_x*sx,top,fmaxf(0,(d->lower_end-d->lower_x)*sx-6),bottom);
    }
    if(t->style<=STYLE_ULTRA)rectangle(0,0,w,1,alpha_color(t->upper_color,.18f));
    gs_matrix_pop();
}
