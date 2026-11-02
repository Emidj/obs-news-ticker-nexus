// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace oto {
constexpr const char *hypeShader=R"(
uniform float4x4 ViewProj;
uniform float4 tint;
uniform float4 secondary;
uniform float4 canvas;
uniform float4 clock;
uniform float4 look;
uniform float4 ending;
uniform float4 layout_bar;
uniform float4 layout_title;
uniform float4 layout_level;
uniform float4 celebration;
struct Vertex {float4 pos:POSITION;float2 uv:TEXCOORD0;};
Vertex VSMain(Vertex input){Vertex o;o.pos=mul(float4(input.pos.xyz,1.0),ViewProj);o.uv=input.uv;return o;}
float hash(float n){return frac(sin(n*127.1)*43758.5453);}
float bolt(float x,float t){return sin(x*.045+t*2.0)*3.0+sin(x*.139-t*5.0)*2.1+sin(x*.39+t*8.0)*.95;}
float4 PSMain(Vertex input):TARGET {
 float2 p=input.uv*canvas.xy;
 float t=clock.x,phase=clock.y,burst=clock.z,pattern=look.x;
 float edgeFade=smoothstep(0.0,5.0,p.x)*smoothstep(0.0,5.0,p.y)*smoothstep(0.0,5.0,canvas.x-p.x)*smoothstep(0.0,5.0,canvas.y-p.y);
 float2 center=lerp(canvas.xy*.5,layout_title.xy,ending.x),halfSize=lerp(canvas.xy*.5-float2(15.0,17.0),float2(layout_title.z,6.0),ending.x);
 float2 q=abs(p-center)-halfSize;
 float d=max(q.x,q.y);
 // Bevel the four corners; the trace travels in both directions from the title.
 d=max(d,(abs(p.x-center.x)+abs(p.y-center.y)-halfSize.x-halfSize.y+12.0)*.7071);
 float outline=smoothstep(.22,.54,phase);
 float perimeter=atan2((p.y-center.y)/halfSize.y,(p.x-center.x)/halfSize.x);
 float reach=abs(perimeter+1.5707963);reach=min(reach,6.2831853-reach)/3.14159265;
 float traced=1.0-smoothstep(outline,outline+.035,reach);
 traced*=smoothstep(.22,.24,phase);
 float noise=bolt(p.x,t)+bolt(p.y,t*1.31);
 float jagged=d+noise*(pattern==7.0?.12:pattern==5.0?.6:1.2);
 float rim=exp(-abs(d)*1.35)*traced;
 float lightning=exp(-abs(jagged)*.8)*traced;
 float halo=exp(-abs(jagged)*.18)*traced;
 float pulse=.75+.25*sin(t*5.5);
 float front=exp(-abs(reach-outline)*120.0)*exp(-abs(d)*.09)*traced*3.5;
 float3 hue=lerp(tint.rgb,secondary.rgb,.5+.5*sin(p.x*.014-t*2.0));
 float alpha=rim*.70+lightning*(.35+burst*.9)+halo*(.20+burst*.45)+front;
 float white=rim*.45+front*.8;
 float inside=1.0-smoothstep(-2.0,1.0,d);
 float outsideBand=exp(-abs(d-8.0)*.11);
 float sweep=exp(-abs(p.x-canvas.x*frac(t*.24))* .025)*inside;
 if(pattern==1.0) { // Interrupted circuits and counter-running red/cyan scans.
  float dash=step(.25,frac((p.x+p.y*3.0)*.05-t*2.0));
  alpha*=.55+.45*dash;alpha+=sweep*.10*outline;
  hue=lerp(tint.rgb,secondary.rgb,step(.5,frac(p.y*.08+t*.7)));
 } else if(pattern==2.0) { // Phosphor interference: moving bands and horizontal tears.
  float scan=.5+.5*sin(p.y*2.9-t*10.0);
  float tear=exp(-abs(p.y-canvas.y*frac(t*.37))*.22);
  alpha=(alpha*(.5+.5*scan)+inside*tear*.25*burst)*outline;
  hue=lerp(tint.rgb,secondary.rgb,scan*.4);
 } else if(pattern==3.0) { // Two orbiting comets, starlight and plasma wakes.
  float orbit=atan2((p.y-center.y)/halfSize.y,(p.x-center.x)/halfSize.x);
  float comet=pow(.5+.5*cos(orbit-t*2.3),48.0)+pow(.5+.5*cos(orbit+t*1.7+2.0),48.0);
  alpha+=comet*exp(-abs(d-5.0)*.12)*outline*.9;
  hue=lerp(tint.rgb,secondary.rgb,.5+.5*cos(orbit+t));
 } else if(pattern==4.0) { // Crimson lightning, fractures, drifting fog.
  float flash=pow(max(0.0,sin(t*7.3+sin(t*3.7))),12.0);
  float fog=(.5+.5*sin(p.x*.027+sin(p.y*.053-t*2.0)*3.0+t))*outsideBand;
  alpha+=outline*(lightning*flash*2.0+fog*(.08+burst*.30));
  hue=lerp(secondary.rgb,tint.rgb,.5+.5*flash);
 } else if(pattern==5.0) { // Silk waves with bright pink undulations.
  float wave=sin(p.x*.025-t*3.0)*6.0;
  alpha+=exp(-abs(d-wave-5.0)*.23)*outline*(.12+burst*.6);
  hue=lerp(tint.rgb,secondary.rgb,.5+.5*sin(p.x*.022+t*1.8));
 } else if(pattern==6.0) { // Spray paint speckles and travelling marker strokes.
  float spray=step(.91,hash(floor(p.x*.4)+floor(p.y*.4)*113.0+floor(t*7.0)));
  alpha+=spray*outsideBand*outline*(.3+burst*.6);
  hue=lerp(tint.rgb,secondary.rgb,step(.45,hash(floor(p.x/40.0)+floor(p.y/24.0)*7.0)));
 } else if(pattern==7.0) { // Silver tracer and crisp crystalline light.
  alpha=rim*.65+halo*.10+front*.65+lightning*burst*.45;
  hue=lerp(secondary.rgb,tint.rgb,.5+.5*sin(t*2.0+p.x*.01));
 } else if(pattern==8.0) { // Amber neon with cyan current and neon streaks.
  float current=pow(.5+.5*sin(p.x*.035-t*14.0),14.0);
  alpha+=current*exp(-abs(d-4.0)*.24)*outline*(.30+burst);
  hue=lerp(tint.rgb,secondary.rgb,current);
 } else if(pattern==9.0) { // Holographic reconstruction, grid and scanning beams.
  float grid=max(step(.94,frac(p.x/18.0)),step(.94,frac(p.y/12.0)));
  alpha+=inside*grid*outline*.05+sweep*outline*.15;
  alpha*=.8+.2*sin(p.y*1.5+t*6.0);
 }
 // Current-level fill gets its own live energy head, separate from the outline.
 // Authored frames carry their own true silhouette. Do not paint a generic extra rectangle.
 if(canvas.z>0.5){alpha=0;white=0;}
 // Level-up energy is independent of the authored silhouette and entrance.
 // One bounded procedural pass supplies rings, an electrical sweep and local
 // impact. Reconnection/ordinary progress never arm this clock.
 if(look.w==0.0 && celebration.x>=0.0 && celebration.y>0.0) {
  float u=celebration.x,strength=celebration.y;
  float2 lp=p-layout_level.xy;
  float radius=max(12.0,layout_level.z);
  float radial=length(lp),angle=atan2(lp.y,lp.x);
  float ringRadius=radius*(.8+u*5.0);
  float ringDistance=abs(radial-ringRadius);
  float ring=exp(-ringDistance*.55)+exp(-ringDistance*.11)*.22;
  float secondRing=exp(-abs(radial-radius*(.7+u*3.3))*.7)*.6;
  float impact=exp(-radial/max(radius*.85,1.0))*(1.0-smoothstep(.12,.4,u));
  float2 cb=layout_bar.xy,sz=layout_bar.zw;
  float head=cb.x+sz.x*smoothstep(.03,.65,u);
  float vertical=p.y-(cb.y+sz.y*.5);
  float band=exp(-abs(vertical)*.14)*step(cb.x-10.0,p.x)*step(p.x,cb.x+sz.x+10.0);
  float trail=exp(-abs(p.x-head)*.075);
  float discharge=exp(-abs(vertical-bolt(p.x,u*9.0))*.85)*trail*2.2;
  float flare=ring+secondRing+impact*1.3+band*trail*.75+discharge;
  float3 charge=lerp(tint.rgb,secondary.rgb,.5+.5*sin(angle*2.0-u*12.0));
  if(pattern==1.0) { // Circuit fragments and a hexagonal shock front.
   float hex=radial*(.94+.06*cos(angle*6.0));
   flare=exp(-abs(hex-ringRadius)*.6)*(step(.28,frac(angle*3.8-u*3.0))+.2)+impact+discharge;
  } else if(pattern==2.0) { // Phosphor flash, scan tears and a compressed CRT wave.
   float scan=.45+.55*step(.45,frac(p.y*.45-u*18.0));
   flare=(ring*.6+impact*1.8+band*trail+discharge)*scan;
   flare+=exp(-abs(p.y-layout_level.y-sin(u*30.0)*radius)*.8)*trail*.7;
  } else if(pattern==3.0) { // Orbital comets and expanding plasma rings.
   float comet=pow(.5+.5*cos(angle-u*22.0),18.0);
   flare=ring*(.4+comet*2.0)+secondRing+impact+discharge*.7;
  } else if(pattern==4.0) { // Jagged red lightning radiating from the new level.
   float fracture=exp(-abs(radial-ringRadius-bolt(angle*50.0,u*8.0)*2.2)*.6);
   flare=fracture*1.6+impact+discharge*1.5;
  } else if(pattern==5.0) { // Pink heart shockwave and flowing silk ribbon.
   float heartRadius=ringRadius*(.88+.18*sin(angle)-.13*cos(angle*2.0));
   flare=exp(-abs(radial-heartRadius)*.5)+secondRing*.5+impact+band*trail;
   flare+=exp(-abs(vertical-sin(p.x*.035-u*15.0)*8.0)*.4)*trail;
  } else if(pattern==6.0) { // Expanding paint splat and a fresh spray stroke.
   float splat=ringRadius*(1.0+.12*sin(angle*9.0)+.08*sin(angle*17.0));
   float speck=step(.84,hash(floor(p.x*.25)+floor(p.y*.25)*113.0));
   flare=exp(-abs(radial-splat)*.4)+impact+band*trail*(.4+speck*2.0)+discharge*.6;
  } else if(pattern==7.0) { // Clean silver diamond and a thin luminous tracer.
   float diamond=abs(lp.x)+abs(lp.y);
   flare=exp(-abs(diamond-ringRadius)*.8)*.75+impact*.45+discharge*.55;
   charge=lerp(secondary.rgb,tint.rgb,.75);
  } else if(pattern==8.0) { // Amber neon ring with a cyan travelling current.
   flare=ring*.8+secondRing+impact+discharge*(.7+.5*sin(p.x*.1-u*30.0));
   charge=lerp(tint.rgb,secondary.rgb,trail*.8);
  } else if(pattern==9.0) { // Holographic radar arcs and reconstruction lattice.
   float sectors=.3+.7*step(.32,frac(angle*2.6-u*4.0));
   float grid=max(step(.9,frac(p.x/12.0)),step(.9,frac(p.y/10.0)));
   flare=ring*sectors+secondRing*.5+impact+band*trail*(.5+grid)+discharge*.5;
  }
  alpha+=flare*strength*(1.0-ending.x);
  white+=flare*strength*.38;
  hue=lerp(hue,charge,strength);
 }
 float2 bp=layout_bar.xy,bs=layout_bar.zw;
 float barInside=step(bp.x,p.x)*step(p.x,bp.x+bs.x*look.z)*step(bp.y,p.y)*step(p.y,bp.y+bs.y);
 float barHead=exp(-abs(p.x-(bp.x+bs.x*look.z))*.18)*barInside;
 float shimmer=pow(.5+.5*sin(p.x*.11-p.y*.05-t*8.0),7.0);
 alpha+=barInside*clock.w*(shimmer*.19+barHead*.85)*(1.0-ending.x);
 white+=barHead*clock.w*.65;
 alpha*=1.0-ending.y;
 // A fluid ribbon binds the titles during their morph, then rests below THANK YOU.
 float ribbon=exp(-abs(p.y-layout_title.y-sin(p.x*.028-t*6.0)*sin(ending.y*3.14159265)*5.0)*.32)*exp(-pow(abs((p.x-layout_title.x)/max(layout_title.z,1.0)),4.0));
 alpha+=ribbon*ending.x*(.25+sin(ending.y*3.14159265)*1.2);
 alpha*=1.0-ending.z;
 // Every particle is a small separate quad: never loop particles across the full meter.
 if(look.w>0.0) {
  float2 z=(input.uv-.5)*2.0;
  float dist=length(z);
  float spark=exp(-(abs(z.x)+abs(z.y))*9.0)+exp(-abs(z.x)*35.0-abs(z.y)*4.0)*.7+exp(-abs(z.y)*35.0-abs(z.x)*5.0)*.7;
  if(pattern==5.0){float2 h=float2(z.x,-z.y)*1.7;float a=h.x*h.x+h.y*h.y-1.0;spark=(1.0-smoothstep(-.04,.09,a*a*a-h.x*h.x*h.y*h.y*h.y))*step(length(h),1.5);}
  else if(pattern==6.0)spark=exp(-dist*5.0)*(1.0+.4*sin(atan2(z.y,z.x)*7.0+look.w));
  else if(pattern==1.0 || pattern==2.0 || pattern==9.0)spark=(1.0-smoothstep(.28,.45,max(abs(z.x),abs(z.y))));
  else if(pattern==3.0 || pattern==7.0)spark=(1.0-smoothstep(.55,.75,abs(z.x)*1.7+abs(z.y)));
  alpha=spark*look.y;white=spark*.7;edgeFade=1;
  hue=lerp(tint.rgb,secondary.rgb,hash(look.w));
 }
 alpha=saturate(alpha*edgeFade);float3 rgb=lerp(hue,float3(1.0,1.0,1.0),saturate(white));return float4(rgb*alpha,alpha);
}
technique Draw {pass {vertex_shader=VSMain(input);pixel_shader=PSMain(input);}}
)";
}
