/* SPDX-License-Identifier: GPL-2.0-or-later
 * Icon-only rectangles from unchanged reference sheets; no text samples.
 */
struct reference_icon_box {int x,y,w,h;};
static const struct reference_icon_box reference_icon_boxes[][3]={
    [STYLE_NEWS]={{182,76,54,24},{802,74,29,28},{942,74,31,30}},
    [STYLE_WORLD]={{555,171,27,29},{1035,172,29,29},{1188,171,35,29}},
    [STYLE_BREAKING]={{501,268,29,30},{948,267,29,30},{1088,267,32,31}},
    [STYLE_BUSINESS]={{514,359,30,30},{1053,359,29,30},{1195,358,32,31}},
    [STYLE_SPORTS]={{501,450,30,30},{1026,449,31,31},{1173,449,35,32}},
    [STYLE_CLEAN]={{499,548,30,30},{1088,548,31,31},{1234,548,34,32}},
    [STYLE_MAP]={{513,645,30,30},{1005,644,32,32},{1150,645,34,29}},
    [STYLE_RED]={{500,741,30,30},{1031,742,28,28},{1188,741,32,31}},
    [STYLE_LIGHT]={{509,837,29,29},{1080,837,30,30},{1211,837,34,31}},
    [STYLE_ULTRA]={{141,977,20,23},{355,977,22,23},{454,976,25,25}},
    [STYLE_PRIME]={{397,139,34,32},{713,139,33,32},{813,139,36,33}},
    [STYLE_GLASS]={{420,267,34,33},{726,267,33,33},{830,268,36,31}},
    [STYLE_NEON]={{420,392,34,34},{717,394,34,34},{824,395,38,32}},
    [STYLE_SPORT]={{431,527,34,34},{698,527,34,34},{811,525,36,36}},
    [STYLE_MAGAZINE]={{426,665,34,35},{700,665,35,36},{810,667,38,33}},
    [STYLE_DARK]={{389,799,33,35},{626,800,33,34},{734,797,36,37}}
};
static const struct reference_icon_box compact_event_boxes[][4]={
    [STYLE_PRIME]={{960,138,32,32},{1082,138,32,32},{1207,136,36,34},{1334,137,38,34}},
    [STYLE_GLASS]={{976,267,32,34},{1096,267,34,34},{1220,264,36,37},{1336,266,38,36}},
    [STYLE_NEON]={{970,394,34,34},{1096,394,35,34},{1219,390,36,38},{1334,393,39,36}},
    [STYLE_SPORT]={{963,529,34,35},{1094,529,35,35},{1218,525,37,38},{1338,527,39,37}},
    [STYLE_MAGAZINE]={{967,668,34,35},{1083,667,36,36},{1218,665,36,39},{1337,668,39,36}},
    [STYLE_DARK]={{936,800,35,36},{1076,801,34,34},{1212,798,36,37},{1338,800,39,37}}
};
/* Ultra artwork is on the normal sheet; category order is independent of
 * runtime event order. FOLLOW, SUB, CHEER, GIFTED, RAID. */
static const struct reference_icon_box ultra_event_boxes[]={
    {588,978,21,23},{931,978,22,23},{1310,978,23,23},{760,978,22,23},{1103,978,23,23}
};
struct reference_text_anchor {int clock,weather;};
static const struct reference_text_anchor reference_text_anchors[]={
    [STYLE_NEWS]={842,983},[STYLE_WORLD]={1078,1233},[STYLE_BREAKING]={990,1128},
    [STYLE_BUSINESS]={1093,1240},[STYLE_SPORTS]={1067,1214},[STYLE_CLEAN]={1128,1275},
    [STYLE_MAP]={1046,1192},[STYLE_RED]={1076,1230},[STYLE_LIGHT]={1119,1257},[STYLE_ULTRA]={380,486},
    [STYLE_PRIME]={755,855},[STYLE_GLASS]={769,879},[STYLE_NEON]={757,873},
    [STYLE_SPORT]={738,850},[STYLE_MAGAZINE]={744,860},[STYLE_DARK]={667,777}
};
static bool draw_icon_sample(struct ticker *t,gs_texture_t *atlas,struct reference_icon_box box,float x,float y,float w,float h,uint32_t rgba,int mode)
{
    if(!atlas||box.w<1||box.h<1||w<1||h<1)return false;
    float scale=fminf(w/box.w,h/box.h),dw=box.w*scale,dh=box.h*scale;
    struct vec4 uv,c;vec4_set(&uv,box.x/1536.f,box.y/1024.f,box.w/1536.f,box.h/1024.f);
    float a=(rgba>>24)/255.f;vec4_set(&c,(rgba&255)/255.f*a,((rgba>>8)&255)/255.f*a,((rgba>>16)&255)/255.f*a,a);
    gs_effect_set_texture(gs_effect_get_param_by_name(t->fade_effect,"image"),atlas);
    gs_effect_set_vec4(gs_effect_get_param_by_name(t->fade_effect,"map_uv"),&uv);
    gs_effect_set_vec4(gs_effect_get_param_by_name(t->fade_effect,"icon_color"),&c);
    gs_effect_set_float(gs_effect_get_param_by_name(t->fade_effect,"icon_mode"),(float)mode);
    gs_matrix_push();gs_matrix_translate3f(x+(w-dw)*.5f,y+(h-dh)*.5f,0);
    while(gs_effect_loop(t->fade_effect,"ReferenceIcon"))gs_draw_sprite(NULL,0,(uint32_t)fmaxf(1,dw),(uint32_t)fmaxf(1,dh));
    gs_matrix_pop();return true;
}
static bool reference_icon(struct ticker *t,int kind,float x,float y,float w,float h,uint32_t color)
{
    bool compact=t->style>=STYLE_PRIME,light=t->style==STYLE_LIGHT||t->style==STYLE_SPORT;
    return draw_icon_sample(t,compact?t->compact_atlas:t->design_atlas,reference_icon_boxes[t->style][kind],x,y,w,h,color,kind==2?(light?3:4):(light?1:0));
}
