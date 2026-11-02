/* SPDX-License-Identifier: GPL-2.0-or-later
 * Bounds and ink heights measured on the two supplied 1536x1024 sheets.
 * Reference presets preserve aspect ratio; user dimensions remain editable.
 */
#pragma once
struct reference_profile {
    int x,y,width,top,bottom,above;
    float body_size,header_scale;
};
static const struct reference_profile reference_profiles[]={
    [STYLE_NEWS]={32,67,1472,42,27,0,20,1.76f},
    [STYLE_LIGHT]={332,828,1172,46,27,0,20,1.76f},
    [STYLE_WORLD]={422,164,1082,44,28,0,20,1.20f},
    [STYLE_BREAKING]={292,261,1212,41,28,0,20,1.84f},
    [STYLE_BUSINESS]={292,350,1212,47,23,0,20,1.52f},
    [STYLE_SPORTS]={292,443,1212,44,30,0,20,1.68f},
    [STYLE_CLEAN]={297,541,1207,43,30,0,20,1.60f},
    [STYLE_MAP]={332,635,1172,45,29,11,20,1.60f},
    [STYLE_RED]={272,733,1232,46,31,0,20,1.60f},
    [STYLE_ULTRA]={32,973,1472,32,0,0,17,1.40f},
    [STYLE_PRIME]={231,119,1288,70,0,0,20,1.70f},
    [STYLE_GLASS]={231,247,1288,70,0,0,20,1.80f},
    [STYLE_NEON]={231,373,1288,70,0,0,20,1.90f},
    [STYLE_SPORT]={229,509,1292,69,0,0,20,1.95f},
    [STYLE_MAGAZINE]={225,646,1295,72,0,0,20,1.67f},
    [STYLE_DARK]={227,785,1294,64,0,0,20,1.82f}
};
static inline float reference_header_width(int style)
{
    const float widths[]={.76f,.81f,.87f,.80f,.76f,.75f};
    return style>=STYLE_PRIME&&style<=STYLE_DARK?widths[style-STYLE_PRIME]:1;
}
static inline float reference_body_width(int style)
{return style>=STYLE_PRIME&&style<=STYLE_DARK?.87f:1;}
static inline float reference_top_inset(int style,float width,float upper)
{
    float extra=style==STYLE_MAP?reference_profiles[style].above*width/reference_profiles[style].width:0;
    return fminf(5+roundf(extra),fmaxf(5,upper-16));
}
