// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <cmath>
#include <array>
namespace oto::chat {
enum EmoteEffect : unsigned { FlipX=1,FlipY=2,Spin=4,TurnLeft=8,TurnRight=16,Party=32,Shake=64,Cursed=128,Wide=256,
 Grow=512,Slide=1024,Appear=2048,Leave=4096,Greyscale=8192,Sepia=16384,Rainbow=32768,HyperRed=65536,HyperShake=131072,Jam=262144,Bounce=524288,NoSpace=1048576 };
inline unsigned ffzEffects(unsigned flags){return (flags&2?FlipX:0u)|(flags&4?FlipY:0u)|(flags&8?Grow:0u)|(flags&16?Slide:0u)|(flags&32?Appear:0u)|(flags&64?Leave:0u)|(flags&128?Spin:0u)|(flags&256?TurnRight:0u)|(flags&512?Greyscale:0u)|(flags&1024?Sepia:0u)|(flags&2048?Rainbow:0u)|(flags&4096?HyperRed:0u)|(flags&8192?HyperShake:0u)|(flags&16384?Cursed:0u)|(flags&32768?Jam:0u)|(flags&65536?Bounce:0u)|(flags&131072?NoSpace:0u);}
inline double emotePadding(unsigned effects){return (effects & (Spin|Jam)?std::sqrt(2.):1.)+(effects & Shake?4./28:0.)+(effects & HyperShake?6./28:0.)+(effects & Jam?8./28:0.)+(effects & (Appear|Leave)?36./28:0.)+(effects & Bounce?.4f:0.);}
// Fixed envelopes keep animated wide rectangles inside the inline layout.
inline std::array<double,2> emoteExtent(unsigned effects){
 if(!(effects & (Wide|Grow))){const auto p=emotePadding(effects);return {p,p};}
 const double aspect=effects & Wide?4.:2.;const double guard=emotePadding(effects)-(effects & (Spin|Jam)?std::sqrt(2.):1.);
 if(effects & (Spin|Jam)){const double radius=std::hypot(aspect,1.);return {radius+guard,radius+guard};}
 return effects & (TurnLeft|TurnRight)?std::array<double,2>{1.+guard,aspect+guard}:std::array<double,2>{aspect+guard,1.+guard};
}
struct EmoteSample {float angle=0,shiftX=0,shiftY=0,hue=0,padding=1;bool party=false,cursed=false;float extentX=1,extentY=1,aspect=1,scaleX=1,scaleY=1,slide=0;int filter=0;};
inline EmoteSample sampleEmote(unsigned effects,bool animated,int elapsed){
 const int t=std::max(0,elapsed);EmoteSample s;s.padding=float(emotePadding(effects));
 if(effects & (Wide|Grow)){const auto extent=emoteExtent(effects);s.padding=1;s.extentX=float(extent[0]);s.extentY=float(extent[1]);s.aspect=effects & Wide?4:2;}
 s.angle=animated && (effects & Spin)?float(t%1500)*360/1500:0;
 s.angle+=(effects & TurnRight)?90:((effects & TurnLeft)?-90:0);if(s.angle>360)s.angle-=360;
 s.party=(effects & Party)!=0;s.cursed=(effects & Cursed)!=0;
 s.hue=s.party && animated?float(t%1500)*360/1500:0;
 if(animated && (effects & Shake)){
  // BetterTTV's ten step-start positions: CSS pixels relative to a 28px emote.
  constexpr int x[]={0,2,1,-2,0,2,-1,-2,2,-1},y[]={1,0,-2,1,-1,2,-1,2,1,-2};
  const int i=(t%500)/50;s.shiftX=float(x[i])/28;s.shiftY=float(y[i])/28;
 }
 s.filter=effects & HyperRed?4:effects & Sepia?2:effects & Greyscale?1:0;
 if(effects & Rainbow){s.filter=3;s.hue=animated?float(t%2000)*360/2000:0;}
 if(animated && (effects & Slide))s.slide=float(t%1500)/1500;
 const auto lerp=[](float a,float b,float u){return a+(b-a)*u;};
 if(animated && (effects & HyperShake)){constexpr float x[]={1,-1,-3,3,1,-1,-3,3,-1,1,1},y[]={1,-2,0,2,-1,2,1,1,-1,2,-2};const float p=float(t%100)/10;const int i=int(p);s.shiftX+=lerp(x[i],x[i+1],p-i)/28;s.shiftY+=lerp(y[i],y[i+1],p-i)/28;}
 if(animated && (effects & Jam)){constexpr float x[]={-2,-1.5f,1,3,3,2,1,-.5f,-1.25f,-1.75f,-2},y[]={-2,-2,-1.5f,2.5f,4,4,4,3,1,-.5f,-2},a[]={-6,-8,-8,-6,-2,3,3,2,0,-2,-5};const float p=float(t%600)/60;const int i=int(p);s.shiftX+=lerp(x[i],x[i+1],p-i)/28;s.shiftY+=lerp(y[i],y[i+1],p-i)/28;s.angle+=lerp(a[i],a[i+1],p-i);}
 if(animated && (effects & Bounce)){constexpr float p[]={0,10,20,25,25.001f,30,40,50,60,70,75,75.001f,80,90,100},x[]={.8f,.9f,1,1.2f,-1.2f,-1,-.9f,-.8f,-.9f,-1,-1.2f,1.2f,1,.9f,.8f},y[]={1,.8f,.4f,.3f,.3f,.4f,.8f,1,.8f,.4f,.3f,.3f,.4f,.8f,1};const float pct=float(t%500)/5;int i=0;while(i<13 && pct>=p[i+1])++i;const float u=(pct-p[i])/(p[i+1]-p[i]);s.scaleX=lerp(x[i],x[i+1],u);s.scaleY=lerp(y[i],y[i+1],u);s.shiftY+=(1-s.scaleY)/2;}
 if(animated && (effects & (Appear|Leave))){
  constexpr float ap[][4]={{0,-18,0,0},{19.99f,-18,0,0},{20,-18,.1f,0},{25,-16,.2f,.6f},{30,-14,.3f,-4},{35,-12,.4f,.6f},{40,-10,.5f,-4},{45,-8,.6f,2},{50,-6,.7f,-3},{55,-4,.8f,2},{60,-2,.9f,-3},{65,0,1,0},{100,0,1,0}};
  constexpr float lp[][5]={{0,0,1,1,0},{39.99f,0,1,1,0},{40,0,-.9f,.9f,-3},{45,-2,-.8f,.8f,2},{50,-4,-.7f,.7f,-3},{55,-6,-.6f,.6f,2},{60,-8,-.5f,.5f,-4},{65,-10,-.4f,.4f,.6f},{70,-12,-.3f,.3f,-4},{75,-14,-.2f,.2f,.6f},{80,-16,-.1f,.1f,0},{85,-18,-.01f,0,0},{100,-18,0,0,0}};
  const bool both=(effects & (Appear|Leave))==(Appear|Leave),leaving=both?(t%6000>=3000):bool(effects & Leave);const float pct=float(t%3000)/30;int i=0;while(i<11 && pct>=(leaving?lp[i+1][0]:ap[i+1][0]))++i;
  const float u=(pct-(leaving?lp[i][0]:ap[i][0]))/((leaving?lp[i+1][0]:ap[i+1][0])-(leaving?lp[i][0]:ap[i][0]));
  s.shiftX+=lerp(leaving?lp[i][1]:ap[i][1],leaving?lp[i+1][1]:ap[i+1][1],u)/28;
  s.scaleX*=lerp(leaving?lp[i][2]:ap[i][2],leaving?lp[i+1][2]:ap[i+1][2],u);s.scaleY*=lerp(leaving?lp[i][3]:ap[i][2],leaving?lp[i+1][3]:ap[i+1][2],u);
  s.shiftY+=lerp(leaving?lp[i][4]:ap[i][3],leaving?lp[i+1][4]:ap[i+1][3],u)/28;
 }
 s.angle=std::remainder(s.angle,360.f);return s;
}
}
