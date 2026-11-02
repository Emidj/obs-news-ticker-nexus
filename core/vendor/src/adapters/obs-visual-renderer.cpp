// SPDX-License-Identifier: GPL-2.0-or-later
#include "obs-visual-renderer.hpp"
#include "alert-shader.hpp"
#include "hype-shader.hpp"
#include "shoutout-shader.hpp"
#include <obs.h>
#include <graphics/vec4.h>
#include <cstring>
#include <stdexcept>
#include <cmath>

namespace oto {
namespace {
// Offset sampling keeps each moving value clipped to its own texture rectangle.
const std::string motionShader = std::string(R"(
uniform float4x4 ViewProj;
uniform texture2d image;
uniform float opacity;
uniform float straight_alpha;
uniform float offset_y;
uniform float offset_x;
uniform float viewport_ratio;
uniform float4 clip_uv;
uniform float4 material;
uniform float4 native_art;
uniform float4 source_uv;
uniform float4 cell_transform;
uniform float cell_padding;
uniform float4 cell_extent;
uniform float2 cell_shift;
uniform float4 cell_color;
uniform float4 cell_motion;
uniform float cell_filter;
uniform float4 cell_layer;
uniform float2 texture_texel;
sampler_state textureSampler { Filter = Linear; AddressU = Clamp; AddressV = Clamp; };
struct Vertex { float4 pos : POSITION; float2 uv : TEXCOORD0; };
Vertex VSMain(Vertex input) {
 Vertex output; output.pos = mul(float4(input.pos.xyz, 1.0), ViewProj); output.uv = input.uv; return output;
}
float nativeZone(float2 a,float2 center,float2 radius) {
 return 1.0-smoothstep(.70,1.0,length((a-center)/radius));
}
float nativePhase(float p,float start,float duration) {return saturate((p-start)/duration);}
float nativeSpring(float p) {return 1.0-exp(-7.0*p)*cos(11.0*p);}
float2 nativeWarp(float2 uv) {
 float2 a=(uv-native_art.xy)/native_art.zw;
 float p=material.z;
 if(p>=.999)return uv;
 float circle=nativeZone(a,float2(.17,.55),float2(.155,.29));
 float crown=nativeZone(a,float2(.34,.19),float2(.080,.11));
 float title=nativeZone(a,float2(.56,.31),float2(.255,.16));
 float panel=nativeZone(a,float2(.55,.61),float2(.24,.20));
 float face=nativeZone(a,float2(.89,.44),float2(.085,.18));
 float follow=nativeZone(a,float2(.79,.78),float2(.21,.15));
 float foreground=max(circle,max(crown,max(title,max(panel,max(face,follow)))));
 // Paint expands from its own centre with a brief fluid ripple.
 float paint=nativePhase(p,0.0,.30);
 a-= (a-float2(.48,.51))*.055*(1.0-paint)*(1.0-foreground);
 a+=float2(sin(a.y*37.0-p*22.0),cos(a.x*31.0+p*18.0))*.006*sin(paint*3.14159265)*(1.0-foreground);
 float c=nativePhase(p,.18,.22);
 a.x+=crown*sin(a.y*31.0+p*18.0)*.012*(1.0-c);
 a.y+=crown*.07*(1.0-nativeSpring(c));
 float hit=nativePhase(p,.30,.22);
 float punch=(1.0-nativeSpring(hit) )*.09;
 a+=(a-float2(.56,.31))*punch*title;
 a+=title*float2(sin(hit*42.0),cos(hit*37.0))*.008*sin(hit*3.14159265)*(1.0-hit);
 float smile=nativePhase(p,.64,.20);
 float tilt=.22*(1.0-nativeSpring(smile))*face;
 float2 f=a-float2(.89,.44);a+=float2(-f.y*.56,f.x*1.78)*tilt;
 float call=nativePhase(p,.73,.22);
 a.x-=follow*.025*(1.0-nativeSpring(call));
 return native_art.xy+a*native_art.zw;
}
float4 nativePaint(float4 color,float2 uv) {
 float2 a=(uv-native_art.xy)/native_art.zw;
 float p=material.z;
 float angle=atan2((a.y-.55)*.56,a.x-.17)+1.5707963;
 if(angle<0.0)angle+=6.2831853;
 float ring=nativePhase(p,.06,.25);
 float ringGate=1.0-smoothstep(ring*6.2831853,ring*6.2831853+.14,angle);
 if(ring>=.999)ringGate=1.0;
 float noise=.5+.25*sin(a.x*23.0+a.y*11.0)+.15*sin(a.y*31.0-a.x*5.0)+.10*sin(a.x*57.0+a.y*47.0);
 float paint=nativePhase(p,0.0,.27);
 float gate=smoothstep(noise*.68,noise*.68+.10,paint);
 float3 rgb=color.rgb/max(color.a,.00001);
 float white=step(.07,min(rgb.r,min(rgb.g,rgb.b)));
 float pink=step(.55,rgb.r)*step(.35,rgb.b)*(1.0-step(.40,rgb.g));
 float dark=1.0-step(.23,max(rgb.r,max(rgb.g,rgb.b)));
 // Classify actual ink instead of carving rectangular or elliptical holes.
 if(a.x<.305 && a.y>.29 && white>.5)gate=ringGate;
 if(a.x>.28 && a.x<.40 && a.y<.29 && pink>.5)gate=smoothstep(.18,.23,p);
 if(a.x>.305 && a.x<.805 && a.y<.435 && (white>.5 || dark>.5 || (rgb.r>.7 && rgb.g>.55)))gate=smoothstep(.28,.33,p);
 float brush=nativePhase(p,.40,.22);
 if(a.x>.33 && a.x<.795 && a.y>=.435 && a.y<.90 && white>.5){
  gate=1.0-smoothstep(brush,brush+.035,(a.x-.33)/.47+.025*sin(a.y*140.0));
  if(brush>=.999)gate=1.0;
 }
 if(a.x>.815 && a.y>.29 && a.y<.64 && white>.5)gate=smoothstep(.62,.67,p);
 if(a.x>.58 && a.y>(.85-.30*a.x))gate=smoothstep(.73,.80,p);
 if(a.x>.375 && a.x<.45 && a.y>.555 && a.y<.65 && dark>.5)gate=smoothstep(.57,.62,p);
 if(a.x>.375 && a.x<.45 && a.y>.65 && a.y<.76 && dark>.5)gate=smoothstep(.62,.67,p);
 if(p>=.999)gate=1.0;
 if(p<=.00001)gate=0.0;
 float finish=nativePhase(p,.87,.12);
 float light=exp(-abs(a.x-(finish*1.5-.2))*55.0)*sin(finish*3.14159265)*.32;
 if(p>=.999)light=.018*pow(max(0.0,sin(material.y*1.5707963+a.x*7.0)),12.0);
 color.rgb=lerp(color.rgb,float3(color.a,color.a,color.a),light);
 return color*gate;
}

)" ) + shoutoutMotionShader + R"(
float4 PSMain(Vertex input) : TARGET {
 if (input.uv.x < clip_uv.x || input.uv.y < clip_uv.y || input.uv.x > clip_uv.z || input.uv.y > clip_uv.w) return float4(0.0,0.0,0.0,0.0);
 float2 uv = float2(input.uv.x*viewport_ratio+offset_x,input.uv.y-offset_y);
 float2 q=(uv-0.5)*cell_padding*cell_extent.xy-cell_shift;
 if(abs(cell_motion.x)<0.00001 || abs(cell_motion.y)<0.00001)return float4(0.0,0.0,0.0,0.0);
 uv=float2(cell_transform.z*q.x+cell_transform.w*q.y,-cell_transform.w*q.x+cell_transform.z*q.y)*cell_transform.xy/cell_extent.zw/cell_motion.xy+0.5;
 if(cell_motion.w>0.5)uv.x=frac(uv.x+cell_motion.z);
 uv=(uv-cell_layer.xy)/cell_layer.zw;
 if(material.x>0.5 && material.w>27.5) {
  uv=soWarp(uv);
 } else if(material.x>0.5 && material.w>26.5) {
  uv=nativeWarp(uv);
 } else if(material.x>0.5 && material.w>5.5 && material.w<6.5) {
  // Continuous fluid refraction during the title exchange; no pixel discard.
  float strength=sin(material.z*3.14159265);
  uv.y=.20+(uv.y-.20)/(1.0-.55*strength);
  uv.x+=sin(uv.y*36.0+material.y*9.0)*.008*strength;
  uv.y+=sin(uv.x*24.0-material.y*7.0)*.014*strength;
 } else if(material.x>0.5 && material.w<5.5) {
  float t=material.y,kind=material.w;
  // Keep a transparent guard inside the quad during all local deformation.
  uv=(uv-0.5)/0.86+0.5;
  if(kind<0.5) {
   // Two heartbeats and a travelling flex through the lobes, not XY motion.
   float beat=pow(saturate(sin(t*5.8)),10.0)+0.45*pow(saturate(sin(t*5.8-0.9)),10.0);
   float2 d=uv-0.5;uv=0.5+d/(1.0+beat*0.09);
   uv.x+=sin(uv.y*8.0-t*5.8)*0.016*(1.0-saturate(abs(d.x)*2.0));
  } else if(kind<1.5) {
   // Crown tips bend independently while the base stays in place.
   uv.x+=sin(uv.x*18.0+t*4.0)*0.024*(1.0-smoothstep(0.22,0.66,uv.y));
  } else if(kind<3.5) {
   // Lift the lid away from the box, flex the bow and retain a real gap.
   float opening=0.035+0.040*(0.5+0.5*sin(t*3.5));
   if(uv.y<0.40){uv.y+=opening;uv.x+=sin(uv.x*13.0+t*6.0)*0.018;}
   else if(uv.y<0.40+opening)return float4(0.0,0.0,0.0,0.0);
   else uv.x+=sin(uv.y*9.0-t*3.5)*0.008;
  } else if(kind<4.5) {
   // Each crowd column rises independently, preserving the shared baseline.
   uv.y+=sin(uv.x*21.0+t*4.8)*0.031*(1.0-smoothstep(0.3,0.82,uv.y));
  } else {
   // Crystal surfaces refract in opposite directions on either facet.
   float side=uv.x<0.5?-1.0:1.0;
   uv.x+=side*sin(uv.y*6.0+t*3.2)*0.014;
   uv.y+=sin(uv.x*9.0-t*2.4)*0.011;
  }
 }
 if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return float4(0.0, 0.0, 0.0, 0.0);
 float2 sample_uv=source_uv.xy+uv*source_uv.zw;
 if(cell_layer.z!=1.0 || cell_layer.w!=1.0 || cell_extent.z>1.0 || cell_padding>1.0 || cell_transform.x<0.0 || cell_transform.y<0.0 || cell_transform.w!=0.0 || cell_shift.x!=0.0 || cell_shift.y!=0.0 || cell_motion.x!=1.0 || cell_motion.y!=1.0 || cell_motion.w>0.5)sample_uv=clamp(sample_uv,source_uv.xy+texture_texel*0.5,source_uv.xy+source_uv.zw-texture_texel*0.5);
 float4 color=image.Sample(textureSampler,sample_uv);
 if(cell_color.x>0.5 || cell_color.y>0.5){
  // Filter straight RGB, then restore premultiplication without changing alpha.
  float3 rgb=color.rgb/max(color.a,0.00001);
  if(cell_color.x>0.5){
   float3 sepia=float3(dot(rgb,float3(.393,.769,.189)),dot(rgb,float3(.349,.686,.168)),dot(rgb,float3(.272,.534,.131)));
   rgb=saturate(lerp(rgb,sepia,.5));float c=cell_color.z,s=cell_color.w;
   rgb=saturate(float3(dot(rgb,float3(.213+.787*c-.213*s,.715-.715*c-.715*s,.072-.072*c+.928*s)),
    dot(rgb,float3(.213-.213*c+.143*s,.715+.285*c+.140*s,.072-.072*c-.283*s)),
    dot(rgb,float3(.213-.213*c-.787*s,.715-.715*c+.715*s,.072+.928*c+.072*s))));
   float grey=dot(rgb,float3(.213,.715,.072));rgb=saturate(grey+(rgb-grey)*2.5);
  }else{
   float grey=dot(rgb,float3(.2126,.7152,.0722));float contrast=saturate((grey*.7-.5)*2.5+.5);rgb=float3(contrast,contrast,contrast);
 }color.rgb=rgb*color.a;
 }
 if(cell_filter>0.5){
  float3 rgb=color.rgb/max(color.a,0.00001);
  if(cell_filter<1.5){float grey=dot(rgb,float3(.2126,.7152,.0722));rgb=float3(grey,grey,grey);}
  else if(cell_filter<2.5)rgb=saturate(float3(dot(rgb,float3(.393,.769,.189)),dot(rgb,float3(.349,.686,.168)),dot(rgb,float3(.272,.534,.131))));
  else if(cell_filter<3.5){float c=cell_color.z,s=cell_color.w;rgb=saturate(float3(dot(rgb,float3(.213+.787*c-.213*s,.715-.715*c-.715*s,.072-.072*c+.928*s)),dot(rgb,float3(.213-.213*c+.143*s,.715+.285*c+.140*s,.072-.072*c-.283*s)),dot(rgb,float3(.213-.213*c-.787*s,.715-.715*c+.715*s,.072+.928*c+.072*s))));}
  else{rgb*=.2;rgb=saturate(float3(dot(rgb,float3(.393,.769,.189)),dot(rgb,float3(.349,.686,.168)),dot(rgb,float3(.272,.534,.131))));rgb=saturate((rgb*2.2-.5)*3+.5);float grey=dot(rgb,float3(.213,.715,.072));rgb=saturate(grey+(rgb-grey)*8);}
  color.rgb=rgb*color.a;
 }
 if(material.x>0.5 && material.w>27.5) {
  color=soInk(color,uv);
 } else if(material.x>0.5 && material.w>26.5) {
  color=nativePaint(color,uv);
 } else if(material.x>0.5) {
  float shine=exp(-abs(uv.x+uv.y*0.4-frac(material.y*0.55)*1.5)*42.0);
  if(material.w>5.5)shine*=sin(material.z*3.14159265);
  color.rgb=lerp(color.rgb,float3(color.a,color.a,color.a),shine*0.75);
  if(material.w>4.5 && material.w<5.5){float facet=0.86+0.14*sin(uv.x*12.0+uv.y*9.0+material.y*4.0);color.rgb*=facet;}
  if(material.w>5.5 && material.w<6.5)color.rgb=lerp(color.rgb,float3(color.a,color.a,color.a),sin(material.z*3.14159265)*.65);
  if(material.w>6.5 && material.w<16.5) {
   float kind=material.w-7.0,progress=material.z;
   float2 q=uv-float2(.43,.20);
   float angle=atan2(q.y*4.8,q.x),reach=abs(angle+1.5707963);reach=min(reach,6.2831853-reach)/3.14159265;
   float gate=1.0-smoothstep(progress,progress+.07,reach);
   float jitter=sin(uv.x*60.0+uv.y*40.0-material.y*19.0)*.015;
   if(kind==1.0)gate=step(floor(reach*18.0)/18.0,progress);
   else if(kind==2.0)gate=1.0-smoothstep(progress,progress+.035,uv.y+jitter);
   else if(kind==3.0)gate=1.0-smoothstep(progress,progress+.09,length(q*float2(1.7,1.0)));
   else if(kind==4.0)gate=1.0-smoothstep(progress,progress+.035,reach+jitter*4.0);
   else if(kind==5.0)gate=1.0-smoothstep(progress,progress+.08,length(q*float2(1.5,1.0))+.05*sin(uv.x*18.0));
   else if(kind==6.0)gate=1.0-smoothstep(progress,progress+.04,uv.x*.65+uv.y*.35+jitter*3.0);
   else if(kind==9.0)gate=step(floor(uv.y*22.0)/22.0,progress);
   gate*=smoothstep(0.0,.025,progress);
   if(progress>=.999)gate=1.0;
   float bright=max(color.r,max(color.g,color.b));float emissive=smoothstep(color.a*.15,color.a*.65,bright);
   float wave=.5+.5*sin(uv.x*33.0+uv.y*5.0-material.y*8.0);
   if(kind==2.0)wave=.5+.5*sin(uv.y*140.0-material.y*10.0);
   if(kind==4.0)wave=pow(max(0.0,sin(material.y*7.3+uv.x*6.0)),10.0);
   if(kind==5.0)wave=.5+.5*sin(uv.x*18.0-material.y*3.0);
   color.rgb=lerp(color.rgb,float3(color.a,color.a,color.a),emissive*wave*.20);
   color*=gate;
  } else if(material.w>16.5) {
   float kind=material.w-17.0;
   float shimmer=pow(.5+.5*sin(uv.x*78.0-uv.y*11.0-material.y*9.0),6.0);
   if(kind==2.0)shimmer=.5+.5*sin(uv.y*180.0-material.y*12.0);
   else if(kind==3.0)shimmer=pow(.5+.5*sin(uv.x*25.0+sin(uv.y*28.0-material.y*4.0)*2.0-material.y*5.0),5.0);
   else if(kind==4.0)shimmer=pow(max(0.0,sin(uv.x*25.0+material.y*7.3)),12.0);
   else if(kind==5.0)shimmer=.5+.5*sin(uv.x*19.0+sin(uv.y*10.0-material.y*4.0));
   else if(kind==6.0)shimmer=step(.74,frac(sin(floor(uv.x*100.0)*127.1+floor(material.y*8.0))*43758.5453));
   else if(kind==7.0)shimmer=exp(-abs(uv.x-(.18+.34*frac(material.y*.17))) * 90.0);
   else if(kind==9.0)shimmer=.5+.5*sin(uv.y*105.0+material.y*6.0);
   // Silver/white fills also need visible motion: travelling light over a
   // gentle darkened base, rather than blending white into identical white.
   color.rgb=lerp(color.rgb*.78,float3(color.a,color.a,color.a),shimmer*.55);
  }
 }
 return straight_alpha>0.5 ? float4(color.rgb,color.a*opacity) : color*opacity;
}
technique Draw { pass { vertex_shader = VSMain(input); pixel_shader = PSMain(input); } }
)";
// A single texture-free program for every preset. It runs only on changed cards.
// Pixel work is bounded to one card; at most twelve deterministic sparks.
constexpr const char *changeShader = R"(
uniform float4x4 ViewProj;
uniform float4 tint;
uniform float4 bounds;
uniform float4 timeline;
uniform float particle_count;
uniform float4 look;
uniform float4 clip_uv;
struct Vertex { float4 pos : POSITION; float2 uv : TEXCOORD0; };
Vertex VSMain(Vertex input) {
 Vertex output; output.pos = mul(float4(input.pos.xyz,1.0),ViewProj); output.uv = input.uv; return output;
}
float4 PSMain(Vertex input) : TARGET {
 if (input.uv.x < clip_uv.x || input.uv.y < clip_uv.y || input.uv.x > clip_uv.z || input.uv.y > clip_uv.w) return float4(0.0,0.0,0.0,0.0);
 float2 size = bounds.xy;
 float2 p = input.uv * (size + float2(bounds.z*2.0,bounds.w*2.0)) - bounds.zw;
 float radius = min(look.z,size.y*0.45);
 float2 q = abs(p-size*0.5) - (size*0.5-radius);
 float distance = length(max(q,0.0)) + min(max(q.x,q.y),0.0) - radius;
 float edge = exp(-abs(distance)*0.60);
 float halo = exp(-abs(distance)*0.17);
 float flash = exp(-pow((timeline.x-0.08)/0.065,2.0))*saturate(timeline.x/0.025);
 float beat = 0.88+0.12*sin(timeline.x*46.0);
 float inside = 1.0-smoothstep(0.0,2.0,distance);
 float2 iconCenter = float2(min(size.y*0.43,size.x*0.20),size.y*0.5);
 float iconHalo = exp(-length(p-iconCenter)*0.065);
 float shock = exp(-abs(distance+saturate(timeline.x/0.25)*9.0)*0.65)*flash;
 float electric = look.x < 1.5 || look.x > 4.5 && look.x < 5.5 ? pow(0.5+0.5*sin(p.x*0.30+p.y*0.60-timeline.x*70.0),6.0)*edge : 0.0;
 float alpha = (edge*0.95+halo*0.48+iconHalo*0.35*beat+inside*flash*0.35+shock*0.30+electric*0.30) * timeline.y;
 float core = (edge*0.60+iconHalo*flash*0.30+shock*0.50+electric*0.35)*timeline.y;
 float front = size.x * saturate((timeline.x-0.12)/0.40);
 float curve = size.x-20.0+pow(1.0-saturate(p.y/size.y),2.0)*20.0;
 float arc = (exp(-abs(p.x-curve)*0.70) + exp(-abs(p.x-curve)*0.12)*0.55 +
  exp(-abs(p.x-curve+11.0)*0.60)*0.35) * timeline.z;
 arc *= saturate((p.y+12.0)/6.0)*saturate((size.y+6.0-p.y)/6.0);
 if (look.x > 0.5 && look.x < 1.5) {
  float scan = floor(front/6.0)*6.0;
  arc = (exp(-abs(p.x-scan)*0.45) + exp(-abs(p.x-scan)*0.08)*0.45)*timeline.z*inside;
  alpha += (step(0.72,frac(p.x/9.0+timeline.x*4.0))*step(0.65,frac(p.y/7.0)))*timeline.y*inside*0.20;
 } else if (look.x > 1.5 && look.x < 2.5) {
  float scanY = size.y*saturate((timeline.x-0.12)/0.40);
  arc = (exp(-abs(p.y-scanY)*0.50)+exp(-abs(p.y-scanY)*0.10)*0.55)*timeline.z*inside;
  alpha += (0.5+0.5*cos(p.y*3.14159265))*timeline.y*inside*0.10;
 } else if (look.x > 2.5 && look.x < 3.5) {
  float angle = timeline.x*12.56637;
  float2 comet = size*0.5+float2(cos(angle)*size.x*0.46,sin(angle)*size.y*0.48);
  arc = exp(-length(p-comet)*0.14)*timeline.z*1.5;
  float2 tail = size*0.5+float2(cos(angle-0.30)*size.x*0.46,sin(angle-0.30)*size.y*0.48);
  arc += exp(-length(p-tail)*0.12)*timeline.z*0.60;
 } else if (look.x > 3.5 && look.x < 4.5) {
  float fog = 0.5+0.5*sin(p.x*0.065+sin(p.y*0.11-timeline.x*12.0)*2.0-timeline.x*10.0);
  arc = fog*exp(-abs(p.y-size.y*0.65)*0.04)*timeline.z*inside*0.45;
 } else if (look.x > 4.5 && look.x < 5.5) {
  float scanY = size.y*saturate((timeline.x-0.12)/0.40);
  arc = (exp(-abs(p.y-scanY)*0.45)+exp(-abs(p.y-scanY)*0.10)*0.40)*timeline.z*inside;
  float grid = max(step(0.88,frac(p.x/12.0)),step(0.88,frac(p.y/9.0)));
  alpha += grid*timeline.y*inside*0.22;
 } else if (look.x > 5.5 && look.x < 6.5) {
  arc = exp(-abs(p.x+p.y*0.65-front)*0.10)*timeline.z*inside*0.80;
 } else if (look.x > 6.5) {
  float column = floor(p.x/10.0);
  float roof = size.y*(0.25+0.35*(0.5+0.5*sin(column*2.2+timeline.x*18.0)));
  arc = step(roof,p.y)*step(frac(p.x/10.0),0.55)*inside*timeline.z*0.40;
  alpha += step(0.75,frac(p.x/8.0))*step(0.72,frac(p.y/7.0))*inside*timeline.y*0.18;
 }
 alpha += arc*0.85; core += arc;
 for (int i=0; i<12; i++) {
  if (float(i) < particle_count) {
   float seed = float(i)+1.0;
   float side = frac(seed*0.6180339);
   float phase = saturate(timeline.x/0.64);
   float2 origin = float2(4.0+frac(seed*0.381966)*max(size.x-8.0,1.0),side < 0.5 ? 2.0 : size.y-2.0);
   float2 drift = float2(sin(seed*2.7)*10.0,(side < 0.5 ? -1.0 : 1.0)*9.0);
   float2 signedDelta = p-(origin+drift*phase);
   float2 delta = abs(signedDelta);
   float sparkle = 0.75+0.25*sin(timeline.x*85.0+seed*2.0);
   float spark = (exp(-(delta.x+delta.y)*0.65) +
    exp(-delta.x*1.7-delta.y*0.30)*0.80 + exp(-delta.y*1.7-delta.x*0.40)*0.65)*timeline.w*sparkle;
   float2 trail = abs(p-(origin+drift*max(phase-0.16,0.0)));
   spark += exp(-trail.x*0.80-trail.y*0.55)*timeline.w*0.30;
   if (look.y > 0.5 && look.y < 1.5) {
    spark = (1.0-smoothstep(1.3,2.3,max(delta.x,delta.y)))*timeline.w*sparkle;
   } else if (look.y > 1.5 && look.y < 2.5) {
    float2 h = float2(signedDelta.x,-signedDelta.y)/3.7;
    float a = h.x*h.x+h.y*h.y-1.0;
    float heart = a*a*a-h.x*h.x*h.y*h.y*h.y;
    spark = (1.0-smoothstep(-0.05,0.12,heart))*step(length(h),1.6)*timeline.w;
   } else if (look.y > 2.5 && look.y < 3.5) {
    spark = exp(-length(signedDelta*float2(0.36,0.50)))*timeline.w;
   } else if (look.y > 4.5) {
    float shard = abs(signedDelta.x)*0.28+abs(signedDelta.y)*0.45;
    spark = (1.0-smoothstep(0.75,1.15,shard))*timeline.w*sparkle;
   } else if (look.y > 3.5) {
    spark = exp(-length(signedDelta)*0.15)*timeline.w*0.28;
   }
   alpha += spark*0.85; core += spark;
  }
 }
 alpha = saturate(alpha);
 float3 rgb = lerp(tint.rgb,float3(1.0,1.0,1.0),saturate(core));
 return float4(rgb*alpha,alpha);
}
technique Draw { pass { vertex_shader = VSMain(input); pixel_shader = PSMain(input); } }
)";
struct MatrixScope {
	MatrixScope(float x, float y) { gs_matrix_push(); gs_matrix_translate3f(x,y,0); }
	~MatrixScope() { gs_matrix_pop(); }
};
struct GraphicsLock {
	GraphicsLock() { obs_enter_graphics(); if (!gs_get_context()) { obs_leave_graphics(); throw std::runtime_error("OBS graphics unavailable"); } }
	~GraphicsLock() { obs_leave_graphics(); }
};
struct BlendScope {
	bool srgb = gs_framebuffer_srgb_enabled();
	BlendScope(bool straight=false,bool legacy=false,bool solid=false) {
		gs_blend_state_push();gs_enable_blending(true);
		if(straight&&!legacy)gs_blend_function_separate(GS_BLEND_SRCALPHA,GS_BLEND_INVSRCALPHA,GS_BLEND_ONE,GS_BLEND_INVSRCALPHA);
		else gs_blend_function(straight||solid?GS_BLEND_SRCALPHA:GS_BLEND_ONE,GS_BLEND_INVSRCALPHA);
		gs_enable_framebuffer_srgb(straight);
	}
	~BlendScope() { gs_enable_framebuffer_srgb(srgb); gs_blend_state_pop(); }
};
struct TargetScope {
	gs_texrender_t *target;
	TargetScope(gs_texrender_t *target, uint32_t width, uint32_t height) : target(target) {
		if (!gs_texrender_begin(target, width, height)) throw std::runtime_error("OBS render target unavailable");
	}
	~TargetScope() { gs_texrender_end(target); }
};
void validateFrame(const visual::FramePtr &frame)
{
	if (!frame || frame->image.isNull() || frame->image.format() != (frame->encoding==visual::RasterEncoding::StraightSrgb?QImage::Format_RGBA8888:QImage::Format_RGBA8888_Premultiplied) ||
		frame->image.width() > 2048 || frame->image.height() > 2048 || frame->image.bytesPerLine() != frame->image.width() * 4)
		throw std::invalid_argument("Invalid visual frame");
}
void setClip(gs_effect_t *effect,const QRectF &clip,float x,float y,float width,float height)
{
	vec4 uv;vec4_set(&uv,0,0,1,1);
	if(!clip.isNull()) {
		if(!clip.isValid() || !std::isfinite(clip.x()) || !std::isfinite(clip.y()) || !std::isfinite(clip.width()) || !std::isfinite(clip.height()))throw std::invalid_argument("Invalid draw clip");
		vec4_set(&uv,float((clip.left()-x)/width),float((clip.top()-y)/height),float((clip.right()-x)/width),float((clip.bottom()-y)/height));
	}
	gs_effect_set_vec4(gs_effect_get_param_by_name(effect,"clip_uv"),&uv);
}
}
ObsVisualRenderer::~ObsVisualRenderer() { clear(); }
void ObsVisualRenderer::makeSpace(size_t bytes,const GroupEntry *keep)
{
	if(bytes>textureBudgetBytes)throw std::length_error("GPU resource exceeds shared budget");
	while(stats_.bytes>textureBudgetBytes-bytes){
		if(!cache_.empty()){stats_.bytes-=cache_.back().bytes;cache_.pop_back();++stats_.evictions;stats_.entries=cache_.size();continue;}
		auto victim=groups_.end();
		for(auto it=groups_.begin();it!=groups_.end();++it)if(!it->busy && &*it!=keep)victim=it;
		if(victim==groups_.end())throw std::length_error("Active GPU groups exceed shared budget");
		stats_.bytes-=victim->bytes;groupStats_.bytes-=victim->bytes;groups_.erase(victim);
		++groupStats_.evictions;groupStats_.entries=groups_.size();
	}
}
void ObsVisualRenderer::composeGroup(uint64_t identity,QSize size,uint64_t revision,const std::function<void()> &paint,bool active)
{
	if(!active)return;
	if(!gs_get_context())throw std::logic_error("OBS graphics context required");
	if(size.width()<1||size.height()<1||size.width()>2048||size.height()>2048||!paint)throw std::invalid_argument("Invalid group recipe");
	auto found=groups_.end();for(auto it=groups_.begin();it!=groups_.end();++it)if(it->identity==identity){found=it;break;}
	if(found!=groups_.end()&&found->busy)throw std::logic_error("Recursive group recipe");
	if(found!=groups_.end()&&found->valid&&found->size==size&&found->revision==revision){groups_.splice(groups_.begin(),groups_,found);++groupStats_.hits;return;}
	if(found==groups_.end()){
		if(groups_.size()>=32){auto victim=groups_.end();for(auto it=groups_.begin();it!=groups_.end();++it)if(!it->busy)victim=it;
			if(victim==groups_.end())throw std::length_error("Too many active groups");
			stats_.bytes-=victim->bytes;groupStats_.bytes-=victim->bytes;groups_.erase(victim);++groupStats_.evictions;}
		groups_.push_front({identity,0,{},nullptr,0});found=groups_.begin();groupStats_.entries=groups_.size();
	}else groups_.splice(groups_.begin(),groups_,found);
	auto &slot=*found;slot.valid=false;slot.busy=true;
	struct BusyScope{GroupEntry &slot;~BusyScope(){slot.busy=false;}} busy{slot};
	const bool resized=!slot.target||slot.size!=size;
	const size_t bytes=size_t(size.width())*size_t(size.height())*4;
	std::unique_ptr<gs_texrender_t,TargetDestroy> replacement;
	if(resized){makeSpace(bytes,&slot);replacement.reset(gs_texrender_create(GS_RGBA,GS_ZS_NONE));if(!replacement)throw std::runtime_error("Group allocation failed");}
	struct Reservation{size_t &total;size_t bytes;Reservation(size_t &t,size_t b):total(t),bytes(b){total+=bytes;}~Reservation(){total-=bytes;}void commit(){bytes=0;}} reservation(stats_.bytes,resized?bytes:0);
	auto *target=replacement?replacement.get():slot.target.get();gs_texrender_reset(target);
	{
		TargetScope scope(target,uint32_t(size.width()),uint32_t(size.height()));
		vec4 empty;vec4_zero(&empty);gs_clear(GS_CLEAR_COLOR,&empty,0,0);gs_ortho(0,float(size.width()),0,float(size.height()),-100,100);gs_matrix_identity();
		++groupDepth_;struct DepthScope{unsigned &depth;~DepthScope(){--depth;}} depth{groupDepth_};paint();
	}
	if(resized){stats_.bytes-=slot.bytes;groupStats_.bytes-=slot.bytes;slot.target=std::move(replacement);slot.size=size;slot.bytes=bytes;groupStats_.bytes+=bytes;++groupStats_.allocations;reservation.commit();}
	slot.revision=revision;slot.valid=true;++groupStats_.renders;
}
void ObsVisualRenderer::drawGroup(uint64_t identity,float x,float y,float scale,float opacity,bool active)
{drawGroupImpl(identity,x,y,scale,opacity,active,false);}
void ObsVisualRenderer::drawGroupSrgb(uint64_t identity,float x,float y,float scale,float opacity,bool active)
{drawGroupImpl(identity,x,y,scale,opacity,active,true);}
void ObsVisualRenderer::drawGroupImpl(uint64_t identity,float x,float y,float scale,float opacity,bool active,bool srgbOutput)
{
	if(!active||opacity==0)return;
	if(!gs_get_context())throw std::logic_error("OBS graphics context required");
	if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(scale)||scale<=0||scale>4||!std::isfinite(opacity)||opacity<0||opacity>1)throw std::invalid_argument("Invalid group draw");
	auto found=groups_.end();for(auto it=groups_.begin();it!=groups_.end();++it)if(it->identity==identity){found=it;break;}
	if(found==groups_.end()||!found->valid||found->busy)throw std::logic_error("Group is not prepared");
	groups_.splice(groups_.begin(),groups_,found);const auto &slot=*found;
	if(!groupEffect_){
		static constexpr char shader[]=R"(
uniform float4x4 ViewProj;uniform texture2d image;uniform float opacity;
sampler_state groupSampler {Filter=Linear;AddressU=Clamp;AddressV=Clamp;};
struct Vertex {float4 pos:POSITION;float2 uv:TEXCOORD0;};
Vertex VS(Vertex v){Vertex o;o.pos=mul(float4(v.pos.xyz,1),ViewProj);o.uv=v.uv;return o;}
float4 PS(Vertex v):TARGET{return image.Sample(groupSampler,v.uv)*opacity;}
technique Draw {pass{vertex_shader=VS(v);pixel_shader=PS(v);}}
)";
		char *error=nullptr;groupEffect_=gs_effect_create(shader,"oto-group.effect",&error);if(error){blog(LOG_WARNING,"[obs-twitch-overlays] Group shader: %s",error);bfree(error);}if(!groupEffect_)throw std::runtime_error("Group shader unavailable");
	}
	if(srgbOutput&&groupDepth_)throw std::logic_error("sRGB output cannot enter an encoded group");
	BlendScope blend;if(srgbOutput)gs_enable_framebuffer_srgb(true);MatrixScope matrix(x,y);gs_matrix_scale3f(scale,scale,1);
	if(srgbOutput)gs_effect_set_texture_srgb(gs_effect_get_param_by_name(groupEffect_,"image"),gs_texrender_get_texture(slot.target.get()));else gs_effect_set_texture(gs_effect_get_param_by_name(groupEffect_,"image"),gs_texrender_get_texture(slot.target.get()));gs_effect_set_float(gs_effect_get_param_by_name(groupEffect_,"opacity"),opacity);
	while(gs_effect_loop(groupEffect_,"Draw"))gs_draw_sprite(gs_texrender_get_texture(slot.target.get()),0,uint32_t(slot.size.width()),uint32_t(slot.size.height()));
	++stats_.draws;++groupStats_.draws;
}
GroupStats ObsVisualRenderer::groupStats() const{GraphicsLock lock;return groupStats_;}
void ObsVisualRenderer::draw(const visual::FramePtr &frame, bool active, const DrawParams &params)
{
	if (!active) return;
	if (!std::isfinite(params.opacity) || params.opacity < 0 || params.opacity > 1 ||
		!std::isfinite(params.x) || !std::isfinite(params.y) || !std::isfinite(params.offsetY) ||
		!std::isfinite(params.offsetX) || params.offsetX<0 || params.offsetX>2048 ||
		!std::isfinite(params.viewportWidth) || params.viewportWidth<0 || params.viewportWidth>2048 ||
		!std::isfinite(params.scale) || params.scale<.0001f || params.scale>4 || !std::isfinite(params.angle) || std::abs(params.angle)>360 ||
		!std::isfinite(params.material) || params.material<0 || params.material>1 || !std::isfinite(params.time) || params.time<0 || params.time>80 || !std::isfinite(params.dissolve) || params.dissolve<0 || params.dissolve>1 ||
		!std::isfinite(params.materialVariant) || params.materialVariant<0 || params.materialVariant>37 ||
		(params.angle!=0 && !params.clip.isNull()))
		throw std::invalid_argument("Invalid draw parameters");
	if(params.materialVariant>=27 && (!QRectF(0,0,1,1).contains(params.nativeArt) || params.nativeArt.width()<.01 || params.nativeArt.height()<.01))throw std::invalid_argument("Invalid native artwork bounds");
	if(params.materialVariant>=28 && (!QRectF(0,0,1,1).contains(params.nativePortrait) || params.nativePortrait.width()<.01 || params.nativePortrait.height()<.01))throw std::invalid_argument("Invalid native portrait bounds");
	if (params.opacity == 0) return;
	if (!gs_get_context()) throw std::logic_error("OBS graphics context required");
	validateFrame(frame);
	const bool straight=frame->encoding==visual::RasterEncoding::StraightSrgb;
	if(straight&&groupDepth_)throw std::invalid_argument("Encoded sRGB groups require premultiplied inputs");
	if(frame->encoding!=visual::RasterEncoding::Premultiplied&&!straight)throw std::invalid_argument("Invalid raster encoding");
	if(straight&&(params.material!=0||params.dissolve!=0||!params.sourceRect.isNull()))throw std::invalid_argument("Straight sRGB raster cannot use material/cell effects");
	const bool region=!params.sourceRect.isNull();
	if(!std::isfinite(params.cellShiftX) || std::abs(params.cellShiftX)>2.f || !std::isfinite(params.cellShiftY) || std::abs(params.cellShiftY)>2.f || !std::isfinite(params.cellHue) || params.cellHue<0 || params.cellHue>360 || (!region && (params.cellShiftX!=0 || params.cellShiftY!=0 || params.cellHue!=0 || params.cellParty || params.cellCursed)))throw std::invalid_argument("Invalid emote filter/movement");
	if(!std::isfinite(params.cellAngle) || std::abs(params.cellAngle)>360 || !std::isfinite(params.cellPadding) || params.cellPadding<1 || params.cellPadding>4 || (!region && (params.cellFlipX || params.cellFlipY || params.cellAngle!=0 || params.cellPadding!=1)))throw std::invalid_argument("Invalid emote transform");
    if(!std::isfinite(params.cellExtentX) || params.cellExtentX<1 || params.cellExtentX>8 || !std::isfinite(params.cellExtentY) || params.cellExtentY<1 || params.cellExtentY>8 || !std::isfinite(params.cellAspect) || (params.cellAspect!=1 && params.cellAspect!=2 && params.cellAspect!=4) || (!region && (params.cellExtentX!=1 || params.cellExtentY!=1 || params.cellAspect!=1)))throw std::invalid_argument("Invalid emote extent");
	if(!std::isfinite(params.cellScaleX) || std::abs(params.cellScaleX)>1.2f || !std::isfinite(params.cellScaleY) || std::abs(params.cellScaleY)>1.2f || !std::isfinite(params.cellSlide) || params.cellSlide<0 || params.cellSlide>=1 || params.cellFilter<0 || params.cellFilter>4 || (!region && (params.cellScaleX!=1 || params.cellScaleY!=1 || params.cellSlide!=0 || params.cellFilter!=0)))throw std::invalid_argument("Invalid FFZ effect");
 const auto layer=params.cellLayer;
 if(!std::isfinite(layer.x()) || !std::isfinite(layer.y()) || !std::isfinite(layer.width()) || !std::isfinite(layer.height()) || layer.width()<.01 || layer.height()<.01 || !QRectF(0,0,1,1).contains(layer) || (!region && layer!=QRectF(0,0,1,1)))throw std::invalid_argument("Invalid accessory placement");
 if(region && (!params.sourceRect.isValid() || !frame->image.rect().contains(params.sourceRect) || params.viewportWidth!=0 || params.offsetX!=0 || params.offsetY!=0 || params.material!=0))throw std::invalid_argument("Invalid animation cell");
	const auto width=region?float(params.sourceRect.width()):params.viewportWidth?params.viewportWidth:float(frame->image.width());
	const auto height=region?float(params.sourceRect.height()):float(frame->image.height());
	if(width<1 || width>frame->image.width() || width!=std::floor(width) || params.offsetX+width>frame->image.width()+0.01f)
		throw std::invalid_argument("Invalid texture viewport");
	gs_texture_t *texture = nullptr;
	for (auto it = cache_.begin(); it != cache_.end(); ++it) {
		if (it->frame == frame) { texture = it->texture.get(); cache_.splice(cache_.begin(), cache_, it); ++stats_.hits; break; }
	}
	if (!texture) {
		makeSpace(size_t(frame->image.sizeInBytes()));
		const uint8_t *pixels = frame->image.constBits();
		Texture uploaded(gs_texture_create(frame->image.width(), frame->image.height(), GS_RGBA, 1, &pixels, 0), gs_texture_destroy);
		if (!uploaded) throw std::runtime_error("OBS texture upload failed");
		texture = uploaded.get();
		const auto bytes = size_t(frame->image.sizeInBytes());
		cache_.push_front({frame, std::move(uploaded), bytes});
		stats_.bytes += bytes; ++stats_.uploads;
		// Phase 13 measured the *combined* working set: the 1920x400 meter,
		// 1920x1080 chat, list, bar and alert/card exceed the old 32 MiB cap.
		// 64 MiB avoids repeated uploads of active planes. Allocation remains
		// on demand, shared by every source, with both entry and byte bounds.
		while (cache_.size() > 128) {
			stats_.bytes -= cache_.back().bytes; cache_.pop_back(); ++stats_.evictions;
		}
		stats_.entries = cache_.size();
	}
	auto *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	if (region || params.material!=0 || params.dissolve!=0 || params.opacity != 1 || params.offsetY != 0 || params.offsetX != 0 || width!=frame->image.width() || !params.clip.isNull()) {
		if (!motionEffect_) {
			char *error = nullptr;
			motionEffect_ = gs_effect_create(motionShader.c_str(),"oto-motion.effect",&error);
			if (error) { blog(LOG_WARNING,"[obs-twitch-overlays] Motion shader: %s",error); bfree(error); }
			if (!motionEffect_) throw std::runtime_error("Motion shader unavailable");
		}
		effect = motionEffect_;
		gs_effect_set_float(gs_effect_get_param_by_name(effect,"opacity"),params.opacity);
		gs_effect_set_float(gs_effect_get_param_by_name(effect,"straight_alpha"),straight?1.f:0.f);
		gs_effect_set_float(gs_effect_get_param_by_name(effect,"offset_y"),params.offsetY / frame->image.height());
		gs_effect_set_float(gs_effect_get_param_by_name(effect,"offset_x"),params.offsetX / frame->image.width());
		gs_effect_set_float(gs_effect_get_param_by_name(effect,"viewport_ratio"),region?1.0f:width / frame->image.width());
		setClip(effect,params.clip,params.x,params.y,width*params.scale*params.cellExtentX,height*params.scale*params.cellExtentY);
		vec4 source;vec4_set(&source,0,0,1,1);
		if(region)vec4_set(&source,float(params.sourceRect.x())/frame->image.width(),float(params.sourceRect.y())/frame->image.height(),width/frame->image.width(),height/frame->image.height());
		gs_effect_set_vec4(gs_effect_get_param_by_name(effect,"source_uv"),&source);
		const float radians=params.cellAngle*3.14159265f/180;vec4 transform;vec4_set(&transform,params.cellFlipX?-1.f:1.f,params.cellFlipY?-1.f:1.f,std::cos(radians),std::sin(radians));
		gs_effect_set_vec4(gs_effect_get_param_by_name(effect,"cell_transform"),&transform);gs_effect_set_float(gs_effect_get_param_by_name(effect,"cell_padding"),params.cellPadding);
        vec4 extent;vec4_set(&extent,params.cellExtentX,params.cellExtentY,params.cellAspect,1.f);gs_effect_set_vec4(gs_effect_get_param_by_name(effect,"cell_extent"),&extent);
		vec2 shift;vec2_set(&shift,params.cellShiftX,params.cellShiftY);gs_effect_set_vec2(gs_effect_get_param_by_name(effect,"cell_shift"),&shift);
		const float hue=params.cellHue*3.14159265f/180;vec4 filter;vec4_set(&filter,params.cellParty?1.f:0.f,params.cellCursed?1.f:0.f,std::cos(hue),std::sin(hue));gs_effect_set_vec4(gs_effect_get_param_by_name(effect,"cell_color"),&filter);
		vec4 motion;vec4_set(&motion,params.cellScaleX,params.cellScaleY,params.cellSlide,params.cellSlide!=0?1.f:0.f);gs_effect_set_vec4(gs_effect_get_param_by_name(effect,"cell_motion"),&motion);gs_effect_set_float(gs_effect_get_param_by_name(effect,"cell_filter"),float(params.cellFilter));
 vec4 layer;vec4_set(&layer,float(params.cellLayer.x()),float(params.cellLayer.y()),float(params.cellLayer.width()),float(params.cellLayer.height()));gs_effect_set_vec4(gs_effect_get_param_by_name(effect,"cell_layer"),&layer);
 vec2 texel;vec2_set(&texel,1.f/frame->image.width(),1.f/frame->image.height());gs_effect_set_vec2(gs_effect_get_param_by_name(effect,"texture_texel"),&texel);
		vec4 art;vec4_set(&art,float(params.nativeArt.x()),float(params.nativeArt.y()),float(params.nativeArt.width()),float(params.nativeArt.height()));gs_effect_set_vec4(gs_effect_get_param_by_name(effect,"native_art"),&art);
		vec4 aperture;vec4_set(&aperture,float(params.nativePortrait.x()),float(params.nativePortrait.y()),float(params.nativePortrait.width()),float(params.nativePortrait.height()));gs_effect_set_vec4(gs_effect_get_param_by_name(effect,"native_portrait"),&aperture);
		vec4 material;vec4_set(&material,params.material,params.time,params.dissolve,params.materialVariant);gs_effect_set_vec4(gs_effect_get_param_by_name(effect,"material"),&material);
	}
	if (!effect) throw std::runtime_error("OBS base effect unavailable");
	auto *image = gs_effect_get_param_by_name(effect, "image");
	if (!image) throw std::runtime_error("OBS image parameter unavailable");
	BlendScope state(straight,params.legacyAlpha);
	MatrixScope matrix(params.x,params.y);
	if(params.angle!=0)gs_matrix_rotaa4f(0,0,1,params.angle*3.14159265f/180);
	if(params.scale!=1 || params.cellExtentX!=1 || params.cellExtentY!=1)gs_matrix_scale3f(params.scale*params.cellExtentX,params.scale*params.cellExtentY,1);
	if(straight)gs_effect_set_texture_srgb(image,texture);else gs_effect_set_texture(image,texture);
	while (gs_effect_loop(effect, "Draw")) gs_draw_sprite(texture, 0, uint32_t(width), uint32_t(height));
	++stats_.draws;
}
void ObsVisualRenderer::drawSolidRect(const QRectF &box,const QColor &color,bool active)
{
	if(!active)return;
	if(!box.isValid()||!std::isfinite(box.x())||!std::isfinite(box.y())||!std::isfinite(box.width())||!std::isfinite(box.height())||box.width()>2048||box.height()>2048||box.width()!=std::floor(box.width())||box.height()!=std::floor(box.height())||!color.isValid())throw std::invalid_argument("Invalid authored solid quad");
	if(!gs_get_context())throw std::logic_error("OBS graphics context required");
	auto *effect=obs_get_base_effect(OBS_EFFECT_SOLID);if(!effect)throw std::runtime_error("OBS solid effect unavailable");
	vec4 tint;vec4_set(&tint,float(color.redF()),float(color.greenF()),float(color.blueF()),float(color.alphaF()));
	gs_effect_set_vec4(gs_effect_get_param_by_name(effect,"color"),&tint);
	BlendScope blend(false,false,true);MatrixScope matrix(float(box.x()),float(box.y()));
	while(gs_effect_loop(effect,"Solid"))gs_draw_sprite(nullptr,0,uint32_t(box.width()),uint32_t(box.height()));
	++stats_.draws;
}
QImage ObsVisualRenderer::renderOffscreen(const visual::FramePtr &frame, float opacity, float offsetY)
{
	validateFrame(frame);
	return renderOffscreen(frame->image.size(),[&] { draw(frame,true,{0,0,opacity,offsetY}); });
}
void ObsVisualRenderer::drawAlertEnergy(float seconds,const QColor &color,float portrait,float burst,float effects,float exit,int variant,int particles,int pattern,float title,float offsetX,bool glassOnly)
{
	if(portrait==0 && burst==0 && effects==0)return;
	if(!gs_get_context())throw std::logic_error("OBS graphics context required");
	if(!std::isfinite(seconds) || seconds<0 || seconds>80 || !color.isValid() || variant<0 || variant>5 || particles<0 || particles>12 || pattern<0 || pattern>9 || (glassOnly && pattern!=2) || !std::isfinite(offsetX) || std::abs(offsetX)>132.001f)throw std::invalid_argument("Invalid alert energy");
	for(float value:{portrait,burst,effects,exit,title})if(!std::isfinite(value) || value<0 || value>1)throw std::invalid_argument("Invalid alert timeline");
	if(!alertEffect_){char *error=nullptr;alertEffect_=gs_effect_create(alertShader,"oto-alert-energy.effect",&error);if(error){blog(LOG_WARNING,"[obs-twitch-overlays] Alert energy shader: %s",error);bfree(error);}if(!alertEffect_)throw std::runtime_error("Alert energy shader unavailable");}
	vec4 tint;vec4_set(&tint,float(color.redF()),float(color.greenF()),float(color.blueF()),1);gs_effect_set_vec4(gs_effect_get_param_by_name(alertEffect_,"tint"),&tint);
	BlendScope blend;
	const auto pass=[&](float mode,float x,float y,float w,float h,float opacity,float seed){
		if(opacity<=0)return;vec4 clock,look;vec4_set(&clock,seconds,opacity,effects,pattern>0?title:exit);vec4_set(&look,mode,float(variant),seed,float(pattern));
		gs_effect_set_vec4(gs_effect_get_param_by_name(alertEffect_,"clock"),&clock);gs_effect_set_vec4(gs_effect_get_param_by_name(alertEffect_,"look"),&look);
		MatrixScope matrix(x,y);while(gs_effect_loop(alertEffect_,"Draw"))gs_draw_sprite(nullptr,0,uint32_t(w),uint32_t(h));++stats_.draws;
	};
	if(glassOnly){pass(3,378,193,244,244,portrait,0);return;}
	pass(0,30,40,940,780,pattern>=2?portrait:burst,0);pass(1,270,85,460,460,portrait,0);
	if(pattern==1){pass(3,140+offsetX,440,720,190,title,0);pass(4,275+offsetX,414,310,115,title,0);}
	// Each procedural particle has a small quad, independent age and geometry.
	// Avoid evaluating all particles for every pixel of the 1000-square canvas.
	for(int n=0;n<particles*2;++n){
		const float seed=float(n+1),age=std::fmod(seconds*(variant==3?1.1f:.7f)+seed*.6180339f,1.0f),angle=seed*2.39996f+seconds*.22f;
		const float radius=180+age*220+exit*55,size=22+seed*1.1f;
		float x=500+std::cos(angle)*radius,y=430+std::sin(angle)*radius*.80f;
		if(variant==1 || variant==3){x=150+std::fmod(seed*83,700.0f);y=155+age*580;}
		if(variant==4){x=160+std::fmod(seed*113,680.0f);y=760-age*390;}
		// Entry/exit both sample the same procedural particle paths.
		const float life=std::sin(age*3.14159265f),alpha=burst*life*(.30f+effects*.65f)*(1-visual::smooth((exit-.94f)/.06f));
		const float extent=size;
		pass(2,x-extent*.5f,y-extent*.5f,extent,extent,alpha,seed);
	}
}
void ObsVisualRenderer::drawHypeEnergy(QSize size,const QColor &primary,const QColor &secondary,float seconds,float phase,float burst,float bar,float ratio,int pattern,int particles,float gather,float morph,float leave,QRect barBounds,QRect titleBounds,bool authored,QRect levelBounds,float levelProgress,float levelFlash)
{
	if(phase<=0)return;
	if(!gs_get_context())throw std::logic_error("OBS graphics context required");
	if(size.width()<420 || size.width()>1920 || size.height()<120 || size.height()>400 || !primary.isValid() || !secondary.isValid() || pattern<0 || pattern>9 || particles<0 || particles>24 || !std::isfinite(seconds) || seconds<0 || seconds>3600)throw std::invalid_argument("Invalid Hype energy");
	for(float v:{phase,burst,bar,ratio,gather,morph,leave})if(!std::isfinite(v) || v<0 || v>1)throw std::invalid_argument("Invalid Hype energy timeline");
	if(!std::isfinite(levelProgress) || levelProgress< -1 || levelProgress>1 || !std::isfinite(levelFlash) || levelFlash<0 || levelFlash>1)throw std::invalid_argument("Invalid Hype level celebration");
	if(!hypeEffect_){char *error=nullptr;hypeEffect_=gs_effect_create(hypeShader,"oto-hype-energy.effect",&error);if(error){blog(LOG_WARNING,"[obs-twitch-overlays] Hype shader: %s",error);bfree(error);}if(!hypeEffect_)throw std::runtime_error("Hype energy shader unavailable");}
	vec4 a,b,c,clock;vec4_set(&a,float(primary.redF()),float(primary.greenF()),float(primary.blueF()),1);vec4_set(&b,float(secondary.redF()),float(secondary.greenF()),float(secondary.blueF()),1);vec4_set(&c,float(size.width()),float(size.height()),authored?1.f:0.f,0);vec4_set(&clock,seconds,phase,burst,bar);
	gs_effect_set_vec4(gs_effect_get_param_by_name(hypeEffect_,"tint"),&a);gs_effect_set_vec4(gs_effect_get_param_by_name(hypeEffect_,"secondary"),&b);gs_effect_set_vec4(gs_effect_get_param_by_name(hypeEffect_,"canvas"),&c);gs_effect_set_vec4(gs_effect_get_param_by_name(hypeEffect_,"clock"),&clock);
	vec4 ending;vec4_set(&ending,gather,morph,leave,0);gs_effect_set_vec4(gs_effect_get_param_by_name(hypeEffect_,"ending"),&ending);
	if(barBounds.isNull())barBounds=QRect(size.width()*14/100,size.height()*43/100,size.width()*34/100,size.height()*23/100);
	if(titleBounds.isNull())titleBounds=QRect(size.width()*20/100,0,size.width()*47/100,size.height()*29/100);
	if(levelBounds.isNull())levelBounds=QRect(size.width()*3/100,size.height()*20/100,size.width()*10/100,size.height()*64/100);
	vec4 layout;vec4_set(&layout,float(barBounds.x()),float(barBounds.y()),float(barBounds.width()),float(barBounds.height()));gs_effect_set_vec4(gs_effect_get_param_by_name(hypeEffect_,"layout_bar"),&layout);
	vec4_set(&layout,float(titleBounds.center().x()),float(titleBounds.bottom()+6),float(titleBounds.width())*.6f,float(titleBounds.center().y()));gs_effect_set_vec4(gs_effect_get_param_by_name(hypeEffect_,"layout_title"),&layout);
	vec4_set(&layout,float(levelBounds.center().x()),float(levelBounds.center().y()),float(levelBounds.width())*.5f,float(levelBounds.height())*.5f);gs_effect_set_vec4(gs_effect_get_param_by_name(hypeEffect_,"layout_level"),&layout);
	vec4_set(&layout,levelProgress,levelFlash,0,0);gs_effect_set_vec4(gs_effect_get_param_by_name(hypeEffect_,"celebration"),&layout);
	BlendScope blend;
	const auto pass=[&](float x,float y,float w,float h,float opacity,float seed){vec4 look;vec4_set(&look,float(pattern),opacity,ratio,seed);gs_effect_set_vec4(gs_effect_get_param_by_name(hypeEffect_,"look"),&look);MatrixScope matrix(x,y);while(gs_effect_loop(hypeEffect_,"Draw"))gs_draw_sprite(nullptr,0,uint32_t(w),uint32_t(h));++stats_.draws;};
	pass(0,0,float(size.width()),float(size.height()),1,0);
	// Small quads with deterministic independent age, trajectory and life. Entire
	// sprite stays within the source; no textures or Qt work per animation frame.
	for(int i=0;i<particles;++i){const float seed=float(i+1),age=std::fmod(seconds*.6f+seed*.618034f,1.0f),extent=10+std::fmod(seed*7,15.0f),dir=i%2?-1.0f:1.0f;
		float x=20+std::fmod(seed*83+age*(35+burst*90),float(size.width()-40));
		float y=(i%2?size.height()-18.0f:18.0f)+dir*(std::sin(age*3.14159265f)*10+burst*age*8);
		x+=(titleBounds.center().x()-x)*gather;y+=(titleBounds.bottom()+6-y)*gather;
		x=std::clamp(x,extent*.5f+2,size.width()-extent*.5f-2);y=std::clamp(y,extent*.5f+2,size.height()-extent*.5f-2);
		float alpha=visual::smooth(phase/.18f)*std::sin(age*3.14159265f)*(.28f+burst*.72f)*(1-morph)*(1-leave);
		if(levelProgress>=0 && levelFlash>0){
			// A synchronized burst replaces ambient paths, reusing the same bounded
			// particle count. It launches from the level and travels along the meter.
			const float launch=visual::smooth(std::clamp(levelProgress*2.4f-seed*.012f,0.f,1.f));
			const float destination=levelBounds.center().x()+35+std::fmod(seed*71,float(size.width()-80));
			const float launchX=levelBounds.center().x()+(destination-levelBounds.center().x())*launch;
			const float launchY=levelBounds.center().y()+dir*(20+std::fmod(seed*13,float(size.height())*.34f))*std::sin(launch*1.5707963f);
			x+=(launchX-x)*levelFlash;y+=(launchY-y)*levelFlash;
			x=std::clamp(x,extent*.5f+2,size.width()-extent*.5f-2);y=std::clamp(y,extent*.5f+2,size.height()-extent*.5f-2);
			alpha+=(.55f+.45f*std::sin(launch*3.14159265f)-alpha)*levelFlash;
		}
		pass(x-extent*.5f,y-extent*.5f,extent,extent,alpha,seed);
	}
}
void ObsVisualRenderer::drawChange(const QRect &card, const QColor &color, const visual::ChangeSample &sample,
	int glowPercent, int particles, bool sweep, int horizontalMargin, int verticalMargin, ChangeLook look, const QRectF &clip)
{
	if(sample.finished || (sample.glow == 0 && sample.sweep == 0 && sample.particles == 0)) return;
	if(!gs_get_context())throw std::logic_error("OBS graphics context required");
	if(card.width()<1 || card.height()<1 || card.width()>1920 || card.height()>240 ||
		!color.isValid() || glowPercent<0 || glowPercent>100 || particles<0 || particles>12 || horizontalMargin<0 || horizontalMargin>6 || verticalMargin<0 || verticalMargin>24 || look.pattern<0 || look.pattern>7 || look.particleShape<0 || look.particleShape>5 || look.radius<0 || look.radius>24 ||
		!std::isfinite(sample.progress) || sample.progress<0 || sample.progress>1 ||
		!std::isfinite(sample.glow) || sample.glow<0 || sample.glow>1 ||
		!std::isfinite(sample.sweep) || sample.sweep<0 || sample.sweep>1 ||
		!std::isfinite(sample.particles) || sample.particles<0 || sample.particles>1)throw std::invalid_argument("Invalid change effect");
	if(glowPercent==0 && particles==0 && !sweep)return;
	if(!changeEffect_) {
		char *error=nullptr;changeEffect_=gs_effect_create(changeShader,"oto-change.effect",&error);
		if(error){blog(LOG_WARNING,"[obs-twitch-overlays] Change shader: %s",error);bfree(error);}
		if(!changeEffect_)throw std::runtime_error("Change shader unavailable");
	}
	vec4 tint,bounds,timeline,profile;
	vec4_set(&tint,float(color.redF()),float(color.greenF()),float(color.blueF()),1);
	vec4_set(&bounds,float(card.width()),float(card.height()),float(horizontalMargin),float(verticalMargin));
	vec4_set(&timeline,sample.progress,sample.glow*glowPercent/100.0f,sweep?sample.sweep:0,sample.particles);
	vec4_set(&profile,float(look.pattern),float(look.particleShape),float(look.radius),0);
	gs_effect_set_vec4(gs_effect_get_param_by_name(changeEffect_,"tint"),&tint);
	gs_effect_set_vec4(gs_effect_get_param_by_name(changeEffect_,"bounds"),&bounds);
	gs_effect_set_vec4(gs_effect_get_param_by_name(changeEffect_,"timeline"),&timeline);
	gs_effect_set_float(gs_effect_get_param_by_name(changeEffect_,"particle_count"),float(particles));
	gs_effect_set_vec4(gs_effect_get_param_by_name(changeEffect_,"look"),&profile);
	setClip(changeEffect_,clip,float(card.x()-horizontalMargin),float(card.y()-verticalMargin),float(card.width()+horizontalMargin*2),float(card.height()+verticalMargin*2));
	BlendScope state;MatrixScope matrix(float(card.x()-horizontalMargin),float(card.y()-verticalMargin));
	while(gs_effect_loop(changeEffect_,"Draw"))gs_draw_sprite(nullptr,0,uint32_t(card.width()+horizontalMargin*2),uint32_t(card.height()+verticalMargin*2));
	++stats_.draws;
}
QImage ObsVisualRenderer::renderOffscreen(QSize size, const std::function<void()> &drawScene)
{
	if(size.width()<1 || size.height()<1 || size.width()>2048 || size.height()>2048)throw std::invalid_argument("Invalid probe size");
	QImage output(size, QImage::Format_RGBA8888_Premultiplied);
	if (output.isNull()) throw std::bad_alloc();
	GraphicsLock lock;
	std::unique_ptr<gs_texrender_t, decltype(&gs_texrender_destroy)> target(gs_texrender_create(GS_RGBA, GS_ZS_NONE), gs_texrender_destroy);
	std::unique_ptr<gs_stagesurf_t, decltype(&gs_stagesurface_destroy)> stage(gs_stagesurface_create(output.width(), output.height(), GS_RGBA), gs_stagesurface_destroy);
	if (!target || !stage) throw std::runtime_error("OBS offscreen resources unavailable");
	{
		TargetScope scope(target.get(), output.width(), output.height());
		vec4 transparent; vec4_zero(&transparent); gs_clear(GS_CLEAR_COLOR, &transparent, 0, 0);
		gs_ortho(0, float(output.width()), 0, float(output.height()), -100, 100);
		drawScene();
	}
	gs_stage_texture(stage.get(), gs_texrender_get_texture(target.get()));
	uint8_t *data = nullptr; uint32_t stride = 0;
	if (!gs_stagesurface_map(stage.get(), &data, &stride)) throw std::runtime_error("OBS readback failed");
	if (stride < uint32_t(output.bytesPerLine())) { gs_stagesurface_unmap(stage.get()); throw std::runtime_error("OBS readback stride invalid"); }
	for (int y = 0; y < output.height(); ++y) std::memcpy(output.scanLine(y), data + size_t(y) * stride, size_t(output.bytesPerLine()));
	gs_stagesurface_unmap(stage.get());
	return output;
}
GpuStats ObsVisualRenderer::stats() const { GraphicsLock lock; return stats_; }
void ObsVisualRenderer::clear() noexcept
{
	// Module shutdown calls this during FRONTEND_EVENT_EXIT, before graphics dies.
	obs_enter_graphics(); cache_.clear(); stats_ = {};
	groups_.clear();groupStats_={};groupDepth_=0;
	if(groupEffect_){gs_effect_destroy(groupEffect_);groupEffect_=nullptr;}
	if (motionEffect_) { gs_effect_destroy(motionEffect_); motionEffect_ = nullptr; }
	if (changeEffect_) { gs_effect_destroy(changeEffect_); changeEffect_ = nullptr; }
	if (alertEffect_) { gs_effect_destroy(alertEffect_); alertEffect_ = nullptr; }
	if (hypeEffect_) { gs_effect_destroy(hypeEffect_); hypeEffect_ = nullptr; }
	obs_leave_graphics();
}
} // namespace oto

