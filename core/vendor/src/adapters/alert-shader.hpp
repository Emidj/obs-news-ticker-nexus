// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace oto {
// One shared procedural program. Plasma, electricity and particle geometry are
// evaluated in the GPU, not sampled from an animated or translated bitmap.
constexpr const char *alertShader=R"(
uniform float4x4 ViewProj;
uniform float4 tint;
uniform float4 clock;
uniform float4 look;
struct Vertex { float4 pos : POSITION; float2 uv : TEXCOORD0; };
Vertex VSMain(Vertex input) { Vertex o; o.pos=mul(float4(input.pos.xyz,1.0),ViewProj);o.uv=input.uv;return o; }
float hash(float2 p) { return frac(sin(dot(p,float2(127.1,311.7)))*43758.5453); }
float noise(float2 p) {
 float2 a=floor(p),f=frac(p);f=f*f*(3.0-2.0*f);
 return lerp(lerp(hash(a),hash(a+float2(1.0,0.0)),f.x),lerp(hash(a+float2(0.0,1.0)),hash(a+float2(1.0,1.0)),f.x),f.y);
}
float cloud(float2 p) { return noise(p)*0.57+noise(p*2.13+3.1)*0.28+noise(p*4.37-7.0)*0.15; }
float roundBox(float2 p,float2 halfSize,float radius) {
 float2 q=abs(p)-halfSize+radius;return length(max(q,float2(0.0,0.0)))+min(max(q.x,q.y),0.0)-radius;
}
float wireDistance(float2 p,float2 a,float2 b) {
 float2 v=b-a;return length(p-a-v*saturate(dot(p-a,v)/max(dot(v,v),0.0001)));
}
float4 PSMain(Vertex input) : TARGET {
 float2 uv=input.uv;
 float edge=smoothstep(0.0,0.065,min(min(uv.x,1.0-uv.x),min(uv.y,1.0-uv.y)));
 if(edge<=0.0)return float4(0.0,0.0,0.0,0.0);
 float t=clock.x,alpha=0.0,hot=0.0,hue=0.0;
 float whitePanel=0.0;

 if(look.w>8.5) {
  // Holographic projection: open depth rings, travelling scans and lattice shards.
  float3 cyan=float3(0.03,0.84,1.0),violet=float3(0.59,0.15,1.0);
  if(look.x<0.5) {
   float2 p=(uv-0.5)*float2(940.0,780.0);
   float3 rgb=float3(0.0,0.0,0.0);float coverage=0.0;
   float2 head=p-float2(0.0,-105.0);
   for(int j=0;j<3;j++) {
    float fj=float(j),phase=t*(0.45+fj*0.13)+fj*2.1;
    float2 q=head*float2(1.0,1.18+fj*0.24);
    float a=atan2(q.y,q.x),r=length(q);
    float radius=204.0+fj*27.0+4.0*sin(a*3.0-phase);
    float gaps=smoothstep(0.18,0.40,0.5+0.5*cos(a*(4.0+fj)-phase));
    float reveal=1.0-smoothstep(clock.w*1.15,clock.w*1.15+0.05,frac((a+3.14159)/6.28318+fj*0.17));
    float wire=exp(-abs(r-radius)*0.75)*gaps*reveal;
    float glow=exp(-abs(r-radius)*0.10)*gaps*reveal;
    float comet=pow(0.5+0.5*cos(a-phase*2.0),30.0);
    float3 color=lerp(cyan,violet,0.5+0.5*sin(a+fj));
    rgb+=color*(wire*1.25+glow*0.15)+float3(0.78,1.0,1.0)*glow*comet*0.7;
    coverage+=wire+glow*(0.14+comet*0.20);
   }
   for(int side=0;side<2;side++) {
    float sx=side==0?-1.0:1.0;
    for(int j=0;j<6;j++) {
     float fj=float(j),phase=t*0.8+fj*1.3+float(side)*2.2;
     float2 center=float2(sx*(247.0+27.0*sin(phase)),-230.0+fj*83.0+8.0*cos(phase));
     float2 q=p-center;float a=phase*0.32;
     float2 v=float2(q.x*cos(a)-q.y*sin(a),q.x*sin(a)+q.y*cos(a));
     float diamond=abs(v.x)+abs(v.y*1.5);
     float shape=exp(-abs(diamond-(13.0+5.0*sin(phase)))*0.80);
     float facet=exp(-abs(v.x)*0.9)*exp(-abs(v.y)*0.05);
     float glow=exp(-abs(diamond-17.0)*0.16);
     float reveal=smoothstep(fj/9.0,fj/9.0+0.24,clock.w);
     float3 color=lerp(cyan,violet,0.5+0.5*sin(phase));
     rgb+=reveal*color*(shape+glow*0.13+facet*0.40);coverage+=reveal*(shape+glow*0.14+facet*0.18);
    }
   }
   // Scanning beams have narrow, changing geometry, never a stretching panel.
   for(int j=0;j<3;j++) {
    float fj=float(j),scanY=-240.0+520.0*(0.5+0.5*sin(t*0.7+fj*2.1));
    float span=exp(-pow(p.x/320.0,4.0));
    float beam=exp(-abs(p.y-scanY)*0.64)*span;
    float trail=exp(-abs(p.y-scanY)*0.10)*span*0.10;
    float gaps=0.4+0.6*pow(0.5+0.5*cos(p.x*0.022+t*2.0+fj),3.0);
    rgb+=clock.w*lerp(cyan,violet,fj/2.0)*(beam+trail)*gaps;
    coverage+=clock.w*(beam+trail)*gaps*0.55;
   }
   float2 base=p-float2(0.0,282.0);
   float r=length(base*float2(1.0,3.8));
   float projector=exp(-abs(r-205.0-5.0*sin(t*1.1+p.x*0.018))*0.40);
   rgb+=clock.w*cyan*projector*0.75;coverage+=clock.w*projector*0.5;
   alpha=saturate(coverage)*clock.y*edge;
   return float4(rgb*clock.y*edge,alpha);
  } else if(look.x<1.5) {
   float2 p=(uv-0.5)*460.0;float r=length(p),a=atan2(p.y,p.x);
   float arc=smoothstep(0.18,0.42,0.5+0.5*cos(a*6.0-t*0.75));
   float inner=exp(-abs(r-148.0)*0.85)*arc;
   float outer=exp(-abs(r-162.0)*0.80)*smoothstep(0.1,0.38,0.5+0.5*sin(a*3.0+t*0.55));
   float glow=exp(-abs(r-150.0)*0.095)*arc;
   float pulse=pow(0.5+0.5*cos(a-t*1.3),36.0);
   float3 color=lerp(cyan,violet,0.5+0.5*sin(a));
   alpha=saturate(inner+outer*0.7+glow*0.16)*clock.y*edge;
   return float4((color*(inner+glow*0.16)+violet*outer+float3(0.8,1.0,1.0)*pulse*glow)*clock.y*edge,alpha);
  } else {
   float2 p=(uv-0.5)*2.0;float a=t*.65+look.z;
   float2 q=float2(p.x*cos(a)-p.y*sin(a),p.x*sin(a)+p.y*cos(a));
   float wire=exp(-abs(abs(q.x)+abs(q.y)-.45)*35.0);
   float core=exp(-length(q*float2(14.0,20.0)));
   alpha=saturate(wire+core*.45)*clock.y*edge;
   return float4(lerp(cyan,violet,0.5+0.5*sin(a))*alpha,alpha);
  }
 }
)" R"(
 else if(look.w>7.5) {

  // Urban City: hollow bronze machinery, hinged corner plates, glass lamps
  // and live reflections. The scene background is never part of the alert.
  if(look.x<0.5) {
   // City neon: open bent tubes and an elliptical pool of reflected light.
   // Reveal the light along each tube; never stretch a rectangular frame.
   float2 p=(uv-0.5)*float2(940.0,780.0);
   float coverage=0.0;float3 rgb=float3(0.0,0.0,0.0);
   for(int side=0;side<2;side++) {
    float signX=side==0?-1.0:1.0;
    float2 q=float2(p.x*signX,p.y);
    float phase=t*1.10+float(side)*2.8;
    for(int j=0;j<4;j++) {
     float fj=float(j),y=-238.0+fj*124.0;
     float2 a=float2(260.0+38.0*sin(fj*1.7),y);
     float2 b=float2(260.0+38.0*sin((fj+1.0)*1.7),y+118.0);
     float d=wireDistance(q,a,b);
     float along=saturate((q.y-y)/118.0);
     float reveal=1.0-smoothstep(clock.w*1.18,clock.w*1.18+0.07,(fj+along)/4.0);
     float runner=pow(0.5+0.5*cos(along*3.14159+fj*1.6-phase),16.0);
     float tube=exp(-d*0.85),glow=exp(-d*0.095),coat=exp(-abs(d-4.4)*1.1);
     float3 neon=side==0?float3(1.0,0.42,0.035):float3(0.03,0.75,1.0);
     float pulse=0.62+0.30*runner+0.08*sin(phase+fj);
     rgb+=reveal*(coat*float3(0.045,0.035,0.027)+neon*(tube*1.4+glow*0.32)*pulse+float3(1.0,0.88,0.65)*tube*runner);
     coverage+=reveal*(tube*0.9+glow*0.24+coat*0.35);
     float cap=exp(-length(q-a)*0.45)*reveal;
     rgb+=cap*neon;coverage+=cap;
    }
   }
   float2 head=p-float2(0.0,-115.0);
   float a=atan2(head.y*1.22,head.x),r=length(head*float2(1.0,1.22));
   float radius=208.0+9.0*sin(a*3.0-t*0.9);
   float split=smoothstep(0.10,0.34,0.5+0.5*cos(a*3.0-t*0.45));
   float rim=exp(-abs(r-radius)*0.70)*split;
   float aura=exp(-abs(r-radius)*0.10)*split;
   float comet=pow(0.5+0.5*cos(a-t*1.1),34.0);
   float3 neon=lerp(float3(1.0,0.42,0.035),float3(0.03,0.78,1.0),0.5+0.5*sin(a));
   rgb+=clock.w*neon*(rim*0.95+aura*0.18+comet*aura);
   coverage+=clock.w*(rim*0.9+aura*0.16);
   // Reflections ripple below the name, as on a wet city street.
   float2 floorP=float2(p.x,(p.y-267.0)*3.7);
   float floorR=length(floorP);
   float wave=exp(-abs(floorR-232.0-8.0*sin(p.x*0.030-t*1.6))*0.24);
   float ripple=wave*(0.55+0.45*sin(p.x*0.026-t*2.0));
   rgb+=clock.w*ripple*lerp(float3(1.0,0.40,0.02),float3(0.03,0.65,1.0),smoothstep(-200.0,200.0,p.x))*0.75;
   coverage+=clock.w*ripple*0.42;
   alpha=saturate(coverage)*clock.y*edge;
   return float4(rgb*clock.y*edge,alpha);
  } else if(look.x<1.5) {
   float2 p=(uv-0.5)*460.0;float r=length(p),a=atan2(p.y,p.x);
   float arc=smoothstep(0.06,0.30,0.5+0.5*cos(a*3.0-t*0.30));
   float tube=exp(-abs(r-146.0)*0.85)*arc;
   float second=exp(-abs(r-158.0)*0.70)*smoothstep(0.15,0.45,0.5+0.5*sin(a*2.0+t*0.40));
   float aura=exp(-abs(r-149.0)*0.095)*arc;
   float pulse=pow(0.5+0.5*cos(a-t*1.25),32.0);
   float3 neon=lerp(float3(1.0,0.50,0.045),float3(0.02,0.80,1.0),0.5+0.5*sin(a));
   float3 rgb=neon*(tube*1.25+aura*0.14)+float3(0.04,0.55,1.0)*second*0.8+float3(1.0,0.90,0.70)*aura*pulse;
   alpha=saturate(tube+second*0.8+aura*0.18)*clock.y*edge;
   return float4(rgb*clock.y*edge,alpha);
  } else {
   float2 p=(uv-0.5)*2.0;float angle=t*0.45+look.z;
   float2 q=float2(p.x*cos(angle)-p.y*sin(angle),p.x*sin(angle)+p.y*cos(angle));
   float ember=exp(-length(q*float2(10.0,17.0)));
   float spark=exp(-abs(q.x)*36.0-abs(q.y)*9.0)*(0.5+0.5*sin(angle*1.5));
   alpha=saturate(ember+spark)*clock.y*edge;
   return float4(lerp(float3(0.94,0.34,0.025),float3(1.0,0.88,0.47),saturate(spark+ember))*alpha,alpha);
  }
 }
)" R"(
 else if(look.w>6.5) {
  // Minimal: open silver contours and independently travelling reflections.
  // Empty space stays transparent; no solid backdrop behind the composition.
  if(look.x<0.5) {
   float2 p=(uv-0.5)*float2(940.0,780.0);
   float2 head=p-float2(0.0,-115.0);
   float r=length(head),a=atan2(head.y,head.x);
   float coverage=0.0,shine=0.0;
   for(int j=0;j<2;j++) {
    float phase=t*(0.54+float(j)*0.12)+float(j)*3.14159;
    float radius=164.0+float(j)*19.0+2.0*sin(a*2.0-phase);
    float sweep=0.5+0.5*cos(a-phase);
    float arc=exp(-abs(r-radius)*0.88)*smoothstep(0.36,0.63,sweep)*clock.w;
    float glint=exp(-abs(r-radius)*0.28)*pow(sweep,32.0)*clock.w;
    coverage=max(coverage,arc*0.60+glint*0.32);shine+=glint;
   }
   // Fine horizontal rules unfold from the centre below the title. Only a
   // short reflection travels along them; their interior is never filled.
   float width=look.y<1.5?245.0:(look.y<2.5?120.0:205.0);
   float halfWidth=width*clock.w;
   float rule=exp(-abs(p.y-168.0)*1.05)*(1.0-smoothstep(halfWidth-14.0,halfWidth,abs(p.x)));
   float spot=exp(-pow((p.x-sin(t*0.9)*width*0.7)/27.0,2.0));
   coverage=max(coverage,rule*(0.34+spot*0.60));shine+=rule*spot;
   // Two small floating ticks bracket the portrait without closing a frame.
   for(int j=0;j<2;j++) {
    float side=j==0?-1.0:1.0;
    float2 q=p-float2(side*(210.0+5.0*sin(t*0.7)), -115.0);
    float tick=exp(-abs(q.x)*0.85)*(1.0-smoothstep(14.0,18.0,abs(q.y-4.0*sin(t+float(j)))))*clock.w;
    coverage=max(coverage,tick*0.52);
   }
   alpha=saturate(coverage)*clock.y*edge;
   float3 rgb=lerp(float3(0.56,0.70,0.80),float3(0.94,0.98,1.0),saturate(shine));
   return float4(rgb*alpha,alpha);
  } else if(look.x<1.5) {
   float2 p=(uv-0.5)*460.0;float r=length(p),a=atan2(p.y,p.x);
   float ring=exp(-abs(r-132.0)*0.85);
   float sweep=pow(0.5+0.5*cos(a-t*0.85),28.0);
   float inner=exp(-abs(r-129.0)*1.2)*sweep;
   float glow=exp(-abs(r-132.0)*0.23)*sweep*0.26;
   alpha=saturate(ring*0.65+inner+glow)*clock.y*edge;
   return float4(lerp(float3(0.55,0.70,0.82),float3(1.0,1.0,1.0),sweep)*alpha,alpha);
  } else {
   float2 p=(uv-0.5)*2.0;float phase=t*0.8+look.z;
   float2 q=p-float2(0.08*cos(phase),0.08*sin(phase));
   float radius=0.32+0.04*sin(phase);
   float dot=exp(-length(q)*15.0);
   float circle=exp(-abs(length(q)-radius)*30.0)*(0.5+0.5*cos(phase));
   alpha=saturate(dot+circle*0.35)*clock.y*edge;
   return float4(float3(0.83,0.94,1.0)*alpha,alpha);
  }
 }
)" R"(
 else if(look.w>5.5) {
  // Graffiti: separate live brush paths and spray trails, with open space
  // between them. No filled backdrop or animated bitmap.
  if(look.x<0.5) {
   float2 p=(uv-0.5)*float2(940.0,780.0);
   float3 yellow=float3(1.0,0.84,0.045),pink=float3(0.98,0.035,0.62),cyan=float3(0.02,0.79,0.88);
   float coverage=0.0;float3 rgb=float3(0.0,0.0,0.0);
   float2 head=p-float2(0.0,-115.0);
   float a=atan2(head.y,head.x),r=length(head);
   // Three calligraphic crescents change curvature, width and swept length.
   // Their gaps travel around the portrait; the area between paths is empty.
   for(int j=0;j<3;j++) {
    float seed=float(j),phase=t*(0.95+seed*0.17)+seed*2.09;
    float sweep=0.5+0.5*cos(a-phase);
    float radius=158.0+seed*16.0+10.0*sin(a*3.0-phase*1.3);
    float width=(4.0+seed*1.4+4.0*sweep)*clock.w;
    float grain=noise(head*0.18+float2(phase*3.0,-phase));
    float brush=(1.0-smoothstep(width,width+1.6,abs(r-radius)))*smoothstep(0.24,0.60,sweep);
    brush*=0.58+0.42*grain;
    float3 col=j==0?yellow:(j==1?pink:cyan);
    rgb=lerp(rgb,col,brush);coverage=max(coverage,brush);
   }
   // Fast diagonal paint flicks bend and extend from independent origins.
   for(int j=0;j<4;j++) {
    float seed=float(j),phase=t*1.6+seed*1.71;
    float slope=j%2==0?0.31:-0.25;
    float pathY=-116.0+seed*19.0+slope*p.x+11.0*sin(p.x*0.013-phase);
    float width=2.2+2.3*(0.5+0.5*sin(phase));
    float extent=(214.0+30.0*sin(phase))*clock.w;
    float streak=(1.0-smoothstep(width,width+1.2,abs(p.y-pathY)))*(1.0-smoothstep(extent-18.0,extent,abs(p.x)));
    streak*=smoothstep(135.0,151.0,r);
    float3 col=j%2==0?pink:cyan;
    rgb=lerp(rgb,col,streak);coverage=max(coverage,streak);
   }
   // Small droplets spray along curved paths, changing shape as they travel.
   for(int j=0;j<10;j++) {
    float seed=float(j+1),phase=t*1.4+seed*2.39996;
    float angle=seed*2.39996+0.28*sin(phase);
    float radius=188.0+24.0*sin(phase)+float(j%3)*15.0;
    float2 centre=float2(cos(angle)*radius,sin(angle)*radius)-float2(0.0,115.0);
    float2 v=p-centre;
    float size=(3.0+float(j%3)*2.0)*(0.70+0.30*sin(phase*1.3));
    float irregular=1.0+0.22*sin(atan2(v.y,v.x)*5.0+phase);
    float droplet=(1.0-smoothstep(size,size+1.2,length(v)/irregular))*clock.w;
    float3 col=j%3==0?yellow:(j%3==1?pink:cyan);
    rgb=lerp(rgb,col,droplet);coverage=max(coverage,droplet);
   }
   // Only narrow ink strokes support the text, never a solid lower panel.
   for(int j=0;j<3;j++) {
    if(j==2 && look.y<0.5)continue;
    float centreY=j==0?40.0:(j==1?105.0:277.0);
    float titleWidth=look.y<0.5?210.0:(look.y<1.5?300.0:(look.y<2.5?95.0:(look.y<4.5?220.0:155.0)));
    float halfWidth=j==0?102.0:(j==1?titleWidth:136.0);
    float halfHeight=j==0?16.0:(j==1?28.0:16.0);
    float wave=3.0*sin(p.x*0.033-t*1.8+float(j));
    float y=p.y-centreY+p.x*0.022-wave;
    float tail=halfWidth*clock.w-abs(p.x)-5.0*sin(y*0.63+t*1.6);
    float ink=smoothstep(-1.0,1.0,tail)*(1.0-smoothstep(halfHeight-2.0,halfHeight+2.0,abs(y)))*clock.w;
    rgb=lerp(rgb,float3(0.012,0.010,0.015),ink);coverage=max(coverage,ink);
    // Paint runs along the lower edge as a travelling brush, not a rigid rail.
    float nibX=sin(t*1.5+float(j)*2.1)*halfWidth*0.72;
    float brush=(1.0-smoothstep(1.4,3.0,abs(y-halfHeight-3.0)))*exp(-pow((p.x-nibX)/65.0,2.0))*clock.w;
    float3 col=j==1?yellow:pink;
    rgb=lerp(rgb,col,brush);coverage=max(coverage,brush);
   }
   alpha=saturate(coverage)*clock.y*edge;
   return float4(rgb*clock.y*edge,alpha);
  } else if(look.x<1.5) {
   float2 p=(uv-0.5)*460.0;float r=length(p),a=atan2(p.y,p.x);
   float radius=141.0+3.0*sin(a*7.0-t*1.2);
   float black=exp(-abs(r-radius)*0.36),marker=exp(-abs(r-radius)*0.72);
   float slash=pow(0.5+0.5*cos(a*6.0-t*2.0),16.0)*exp(-abs(r-154.0)*0.65);
   float scribble=exp(-abs(r-166.0-10.0*sin(a*3.0-t*1.5))*0.65)*(0.5+0.5*cos(a-t*1.3));
   float dots=pow(0.5+0.5*cos(a*11.0-t*1.8),28.0)*exp(-abs(r-179.0)*0.85);
   float3 rgb=tint.rgb*marker+float3(1.0,0.04,0.62)*(scribble*0.7+dots*0.95)+float3(0.04,0.85,0.93)*slash;
   alpha=saturate(black+slash+scribble+dots)*clock.y*edge;
   return float4(rgb*clock.y*edge,alpha);
  } else {
   float2 p=(uv-0.5)*2.0;float a=t*0.4+look.z,cs=cos(a),sn=sin(a);
   float2 q=float2(p.x*cs-p.y*sn,p.x*sn+p.y*cs);
   float r=length(q),angle=atan2(q.y,q.x);
   float silhouette=1.0-smoothstep(-0.025,0.025,r-0.44-0.08*sin(angle*7.0+look.z));
   float slash=exp(-abs(q.y+q.x*0.32)*19.0)*step(abs(q.x),0.60);
   float ink=pow(saturate(0.5+0.5*sin(q.x*24.0+q.y*18.0+t)),8.0);
   float3 color=look.z<8.5?tint.rgb:(look.z<16.5?float3(0.99,0.025,0.61):float3(0.02,0.85,0.92));
   alpha=saturate(silhouette+slash*0.75)*clock.y*edge;
   return float4(color*(1.0-ink*0.75)*alpha,alpha);
  }
 }
)" R"(
 else if(look.w>4.5) {
  // Love: bevelled violet glass, independently flowing ribbons, heart
  // geometry and travelling glints. Reverse the shared clock from the held pose.
  if(look.x<0.5) {
   // Love: flowing rose-silk ribbons, open pearl waves and floating hearts.
   float2 p=(uv-0.5)*float2(940.0,780.0);
   float2 head=p-float2(0.0,-115.0);
   float a=atan2(head.y*1.12,head.x),r=length(head*float2(1.0,1.12));
   float coverage=0.0;float3 rgb=float3(0.0,0.0,0.0);
   for(int j=0;j<3;j++) {
    float fj=float(j),phase=t*(0.68+fj*0.16)+fj*2.1;
    float radius=202.0+fj*20.0+18.0*sin(a*2.0-phase)+7.0*sin(a*5.0+phase*0.8);
    float d=r-radius;
    float width=3.5+3.0*(0.5+0.5*sin(a*3.0+phase));
    float silk=exp(-pow(d/width,2.0));
    float glow=exp(-abs(d)*0.10);
    float aperture=smoothstep(0.05,0.50,0.5+0.5*cos(a+phase*0.35));
    float pearl=exp(-abs(d+width)*0.80)*pow(0.5+0.5*cos(a-phase),10.0);
    float3 rose=lerp(float3(1.0,0.075,0.47),float3(0.88,0.37,1.0),0.5+0.5*sin(a+fj));
    rgb+=clock.w*aperture*(rose*(silk*0.82+glow*0.17)+float3(1.0,0.82,0.94)*pearl);
    coverage+=clock.w*aperture*(silk*0.80+glow*0.18+pearl*0.50);
   }
   // Two soft, twisting waves flow underneath the heading and name.
   for(int j=0;j<2;j++) {
    float fj=float(j),phase=t*(1.0+fj*0.20)+fj*2.6;
    float path=134.0+fj*114.0+27.0*sin(p.x*0.010-phase)+8.0*sin(p.x*0.025+phase);
    float width=4.0+3.0*(0.5+0.5*cos(p.x*0.015-phase));
    float d=p.y-path;
    float silk=exp(-pow(d/width,2.0)),glow=exp(-abs(d)*0.14);
    float reveal=(1.0-smoothstep(270.0,342.0,abs(p.x)))*clock.w;
    float glint=pow(0.5+0.5*cos(p.x*0.010-phase*1.2),14.0);
    rgb+=reveal*(float3(1.0,0.20,0.57)*(silk*0.7+glow*0.15)+float3(1.0,0.83,0.95)*glint*silk);
    coverage+=reveal*(silk*0.65+glow*0.15);
   }
   for(int j=0;j<6;j++) {
    float fj=float(j),phase=t*0.8+fj*1.17,side=j<3?-1.0:1.0;
    float2 centre=float2(side*(279.0+18.0*sin(phase)),-180.0+float(j%3)*185.0+23.0*sin(phase*1.3));
    float2 h=(p-centre)/(12.0+float(j%3)*3.5);h.y=-h.y;
    float cs=cos(0.18*sin(phase)),sn=sin(0.18*sin(phase));h=float2(h.x*cs-h.y*sn,h.x*sn+h.y*cs);
    h/=1.0+0.10*pow(saturate(sin(t*2.5+fj)),6.0);
    float v=dot(h,h)-1.0,field=v*v*v-h.x*h.x*h.y*h.y*h.y;
    float shape=1.0-smoothstep(-0.07,0.07,field),pearl=exp(-abs(field)*9.0)*step(length(h),1.6);
    rgb+=clock.w*(shape*float3(0.72,0.09,0.35)+pearl*float3(1.0,0.65,0.86));
    coverage+=clock.w*(shape*0.62+pearl*0.6);
   }
   alpha=saturate(coverage)*clock.y*edge;
   return float4(rgb*clock.y*edge,alpha);
  } else if(look.x<1.5) {
   float2 p=(uv-0.5)*460.0;float r=length(p),a=atan2(p.y,p.x);
   float ring=exp(-abs(r-139.0)*0.60),pearl=exp(-abs(r-151.0)*0.85);
   float halo=exp(-abs(r-141.0)*0.075);
   float ribbon=exp(-abs(r-169.0-8.0*sin(a*3.0-t*0.65))*0.55);
   float strands=0.45+0.55*pow(0.5+0.5*cos(a*6.0+t*0.85),4.0);
   float comet=pow(0.5+0.5*cos(a-t*0.8),40.0);
   float gems=pow(0.5+0.5*cos(a*12.0),28.0)*exp(-abs(r-158.0)*0.80);
   float3 rgb=tint.rgb*(ring*1.1+halo*0.16+ribbon*strands*0.70);
   rgb+=float3(0.55,0.26,1.0)*pearl*0.8+float3(1.0,0.76,0.96)*(gems*0.8+comet*halo*0.85);
   alpha=saturate(ring+pearl+halo*0.30+ribbon*strands*0.8+gems)*clock.y*edge;
   return float4(rgb*clock.y*edge,alpha);
  } else {
   float2 p=(uv-0.5)*2.0;float a=t*0.55+look.z,cs=cos(a),sn=sin(a);
   float2 q=float2(p.x*cs-p.y*sn,p.x*sn+p.y*cs);
   float2 h=q*2.0;h.y=-h.y;float v=dot(h,h)-1.0;
   float field=v*v*v-h.x*h.x*h.y*h.y*h.y;
   float heart=1.0-smoothstep(-0.05,0.05,field);
   float sparkle=exp(-abs(q.x)*27.0)*exp(-abs(q.y)*4.5)+exp(-abs(q.y)*27.0)*exp(-abs(q.x)*4.5);
   float blend=0.5+0.5*sin(look.z*2.1);
   float3 rgb=lerp(tint.rgb,float3(0.76,0.49,1.0),blend)+sparkle*0.6;
   alpha=saturate(heart*0.65+sparkle*0.65)*clock.y*edge;
   return float4(rgb*alpha,alpha);
  }
 }
)" R"(
 else if(look.w>3.5) {
  // Fractured dark armour: independent seams, drifting smoke, ember flow and
  // thorn geometry. The common reversible timeline drives each component.
  if(look.x<0.5) {
   // Horror: branched lightning in drifting storm clouds, no armour panel.
   float2 p=(uv-0.5)*float2(940.0,780.0);
   float2 head=p-float2(0.0,-115.0);
   float r=length(head*float2(1.0,0.96));
   float turbulence=cloud(head*0.021+float2(t*0.12,-t*0.23));
   float smoke=exp(-pow((r-216.0)/79.0,2.0))*pow(saturate((turbulence-0.25)*1.7),2.0);
   float lower=exp(-pow(p.x/265.0,2.0)-pow((p.y-238.0)/47.0,2.0))*cloud(p*0.033+float2(-t*0.16,t*0.35));
   float coverage=(smoke*0.42+lower*0.27)*clock.w;
   float3 rgb=(smoke*float3(0.19,0.018,0.039)+lower*float3(0.17,0.020,0.053))*clock.w;
   for(int side=0;side<2;side++) {
    float signX=side==0?-1.0:1.0,fs=float(side);
    float phase=t*2.2+fs*2.5;
    // Smooth discharge pulses preserve the reversal at arbitrary exit times.
    float discharge=0.16+0.84*pow(0.5+0.5*sin(phase),10.0);
    float reveal=1.0-smoothstep(clock.w*1.20,clock.w*1.20+0.12,(p.y+266.0)/540.0);
    float2 q=float2(p.x*signX,p.y);
    for(int j=0;j<7;j++) {
     float fj=float(j),y=-258.0+fj*73.0;
     float x=258.0+38.0*sin(fj*2.3+fs)+10.0*sin(t*1.7+fj*1.1);
     float nx=258.0+38.0*sin((fj+1.0)*2.3+fs)+10.0*sin(t*1.7+(fj+1.0)*1.1);
     float2 a=float2(x,y),b=float2(nx,y+73.0);
     float d=wireDistance(q,a,b);
     float core=exp(-d*1.5),halo=exp(-d*0.13);
     rgb+=reveal*discharge*(core*float3(1.0,0.78,0.80)+halo*float3(0.90,0.035,0.12)*0.62);
     coverage+=reveal*discharge*(core+halo*0.42);
     if(j==1 || j==3 || j==5) {
      float2 tip=b+float2(57.0+9.0*sin(phase+fj),-31.0);
      float branch=wireDistance(q,b,tip);
      float branchCore=exp(-branch*1.8),branchGlow=exp(-branch*0.18);
      rgb+=reveal*discharge*(branchCore*float3(1.0,0.72,0.78)+branchGlow*float3(0.85,0.025,0.16)*0.37);
      coverage+=reveal*discharge*(branchCore*0.85+branchGlow*0.27);
     }
    }
   }
   float flash=pow(0.5+0.5*sin(t*2.2),12.0)*smoke*0.35*clock.w;
   rgb+=flash*float3(0.50,0.035,0.09);coverage+=flash*0.2;
   alpha=saturate(coverage)*clock.y*edge;
   return float4(rgb*clock.y*edge,alpha);
  } else if(look.x<1.5) {
   float2 p=(uv-0.5)*460.0;float r=length(p),a=atan2(p.y,p.x);
   float hex=max(abs(p.x)*0.8660254+abs(p.y)*0.5,abs(p.y));
   float band=1.0-smoothstep(9.0,11.0,abs(hex-145.0));
   float rough=cloud(p*0.12);
   float rim=exp(-abs(hex-154.0)*0.72);
   float inner=exp(-abs(r-125.0)*0.74);
   float vein=exp(-abs(sin(a*7.0+noise(float2(r*0.035,5.0))*4.0))*29.0)*band;
   float thorns=pow(saturate(0.5+0.5*cos(a*12.0)),12.0);
   float spikes=1.0-smoothstep(2.0,4.0,abs(r-(168.0+thorns*18.0)));
   spikes*=smoothstep(0.65,0.93,thorns);
   float glint=pow(0.5+0.5*cos(a-t*0.85),48.0);
   float halo=exp(-abs(r-159.0)*0.12);
   float3 rgb=band*(float3(0.11,0.045,0.056)+rough*0.09);
   rgb+=tint.rgb*(rim*0.85+inner*0.45+vein*(0.3+0.25*sin(t*2.0+a*2.0))+spikes*0.9+halo*0.12+glint*halo*1.1);
   alpha=saturate(band+rim+inner+spikes+halo*0.24)*clock.y*edge;
   return float4(rgb*clock.y*edge,alpha);
  } else {
   float2 p=(uv-0.5)*2.0;
   float a=t*0.9+look.z,cs=cos(a),sn=sin(a);
   float2 q=float2(p.x*cs-p.y*sn,p.x*sn+p.y*cs);
   float shard=1.0-smoothstep(0.19,0.26,abs(q.x)*1.8+abs(q.y)*0.45);
   float coal=exp(-length(p)*5.0);
   float flicker=0.65+0.35*sin(t*3.0+look.z);
   alpha=saturate(shard*0.9+coal*0.3)*clock.y*edge;
   float3 rgb=lerp(tint.rgb,float3(1.0,0.73,0.50),coal*0.7)*flicker;
   return float4(rgb*alpha,alpha);
  }
 }
)" R"(
 else if(look.w>2.5) {
  // Orbital machinery: layered armour, running ion rails and rotating
  // segmented rings. All geometry evolves inside this shared GPU program.
  if(look.x<0.5) {
   float2 p=(uv-0.5)*float2(940.0,780.0);
   float width=238.0+78.0*clock.w;
   float d=max(max(abs(p.x)-width,abs(p.y)-304.0),abs(p.x)+abs(p.y)-width-237.0);
   float body=1.0-smoothstep(-1.0,1.0,d);
   float seam=exp(-abs(d+17.0)*0.26),rim=exp(-abs(d)*0.33);
   float inner=roundBox(p-float2(0.0,135.0),float2(279.0,112.0),10.0);
   float panel=1.0-smoothstep(-1.0,1.0,inner);
   float grid=exp(-abs(sin(p.x*0.025))*35.0)*exp(-abs(sin(p.y*0.025))*35.0);
   float brushed=0.5+0.5*sin(p.y*0.81+noise(p*0.13)*0.4);
   float bevel=exp(-abs(d+7.0)*0.24)+exp(-abs(d+28.0)*0.30)*0.6;
   float plate=step(-43.0,d)*body;
   float3 rgb=float3(0.012,0.055,0.071)+grid*float3(0.01,0.035,0.045);
   rgb+=plate*(float3(0.027,0.093,0.117)+brushed*float3(0.015,0.030,0.037));
   rgb+=bevel*float3(0.13,0.32,0.38)*(0.65+0.35*saturate(-p.y/300.0));
   rgb+=seam*float3(0.08,0.23,0.28)+rim*tint.rgb*0.50;
   rgb=lerp(rgb,float3(0.008,0.026,0.041),panel);
   rgb+=exp(-abs(inner)*0.33)*tint.rgb*(0.23+clock.w*0.28);
   // Four armour insets and moving luminous packets along side rails.
   float rail=exp(-abs(abs(p.x)-299.0)*0.75)*step(abs(p.y),245.0);
   float packet=pow(0.5+0.5*cos(p.y*0.028-t*1.6),8.0);
   rgb+=rail*tint.rgb*(0.25+packet*0.9);
   float ledBand=exp(-abs(abs(p.x)-290.0)*0.6)*step(120.0,abs(p.y))*step(abs(p.y),242.0);
   float ledCell=smoothstep(0.2,0.3,frac(p.y/13.0))*smoothstep(0.95,0.85,frac(p.y/13.0));
   rgb+=ledBand*ledCell*tint.rgb*(0.10+0.6*packet);
   float2 hinge=float2(abs(p.x)-301.0,abs(p.y+100.0)-139.0);
   float hingeD=roundBox(hinge,float2(21.0,37.0),5.0);
   float hingeBody=1.0-smoothstep(-1.0,1.0,hingeD);
   float hingeRim=exp(-abs(hingeD)*0.60);
   rgb=lerp(rgb,float3(0.05,0.13,0.17)+brushed*0.035,hingeBody);
   rgb+=hingeRim*float3(0.12,0.40,0.47);
   rgb+=exp(-abs(hinge.x)*0.8)*step(abs(hinge.y),22.0)*tint.rgb*(0.4+packet*0.35);
   float grate=step(0.48,frac(p.y/10.0))*step(258.0,abs(p.x))*step(abs(p.x),282.0)*step(abs(p.y),112.0);
   rgb=lerp(rgb,float3(0.018,0.115,0.145),grate);
   float2 screw=abs(p)-float2(274.0,272.0);
   float bolt=1.0-smoothstep(4.0,7.0,length(screw));
   rgb=lerp(rgb,float3(0.16,0.29,0.33),bolt);rgb*=1.0-bolt*exp(-abs(screw.y)*2.0)*0.5;
   float jet=exp(-abs(abs(p.x)-323.0)*0.09)*exp(-abs(p.y-65.0)*0.026)*(0.25+0.75*cloud(p*0.03+float2(t,-t*2.0)));
   float halo=exp(-max(d,0.0)*0.095)*(1.0-body)*0.22;
   rgb+=tint.rgb*jet*(0.4+clock.z);
   alpha=saturate(body+halo+jet*clock.z)*clock.y*edge;
   return float4(rgb*alpha,alpha);
  } else if(look.x<1.5) {
   float2 p=(uv-0.5)*460.0;float r=length(p),a=atan2(p.y,p.x);
   float teeth=0.5+0.5*cos(a*24.0-t*0.35);
   float gearD=abs(r-166.0)-(9.0+teeth*5.0);
   float metal=1.0-smoothstep(-0.8,1.0,gearD);
   float segments=smoothstep(0.18,0.28,0.5+0.5*cos(a*8.0+t*0.65));
   float outer=exp(-abs(r-191.0)*0.70)*segments;
   float rim=exp(-abs(r-127.0)*0.62),ring=exp(-abs(r-150.0)*0.52);
   float spark=pow(0.5+0.5*cos(a-t*0.9),34.0);
   float glow=exp(-abs(r-143.0)*0.085);
   float bevel=exp(-abs(gearD+2.0)*0.45);
   float3 rgb=metal*(float3(0.055,0.14,0.18)+float3(0.06,0.12,0.14)*(0.5-0.5*p.y/190.0));
   rgb+=bevel*float3(0.15,0.32,0.38);
   float slot=exp(-abs(r-166.0)*0.55)*pow(0.5+0.5*cos(a*12.0+t*0.6),14.0);
   rgb+=slot*tint.rgb*(0.6+0.3*sin(t*2.0+a*3.0));
   rgb+=tint.rgb*(rim*0.9+ring*0.6+outer*0.9+glow*0.17+spark*ring);
   alpha=saturate(metal*0.95+rim+ring+outer+glow*0.30)*clock.y*edge;
   return float4(rgb*clock.y*edge,alpha);
  } else {
   float2 p=(uv-0.5)*2.0;
   float a=t*0.6+look.z,cs=cos(a),sn=sin(a);
   float2 q=float2(p.x*cs-p.y*sn,p.x*sn+p.y*cs);
   float star=exp(-abs(q.x)*30.0)*exp(-abs(q.y)*4.5)+exp(-abs(q.y)*30.0)*exp(-abs(q.x)*4.5);
   float jewel=1.0-smoothstep(0.25,0.31,abs(q.x)+abs(q.y));
   alpha=saturate(star*0.9+jewel*0.45)*clock.y;hot=star;hue=0.2;
  }
 } else if(look.w>1.5) {
  // Machined analogue TV. Cabinet, vents, screws, tuning knobs and glass are
  // separate evolving geometry in this same program, never a moved TV PNG.
  if(look.x<0.5) {
   // Retro CRT: free phosphor echoes, RGB signal tears and rolling noise.
   // There is no cabinet or large glass rectangle around the composition.
   float2 p=(uv-0.5)*float2(940.0,780.0);
   float2 head=p-float2(0.0,-115.0);
   float coverage=0.0;float3 rgb=float3(0.0,0.0,0.0);
   float scan=0.5+0.5*cos(p.y*1.55-t*7.0);
   float2 bend=head; bend.x+=13.0*sin(head.y*0.029-t*2.0)*pow(0.5+0.5*sin(t*1.6),6.0);
   float r=length(bend*float2(1.0,1.17)),a=atan2(bend.y*1.17,bend.x);
   for(int j=0;j<3;j++) {
    float fj=float(j),radius=184.0+fj*21.0;
    float phosphorLine=exp(-abs(r-radius-4.0*sin(a*6.0+t*2.0))*0.55);
    float echo=exp(-abs(r-radius)*0.12);
    float aperture=smoothstep(0.05,0.44,0.5+0.5*sin(a*3.0+t*0.7+fj));
    float3 phosphor=j==0?float3(0.12,0.95,0.70):j==1?float3(1.0,0.36,0.06):float3(0.42,0.30,1.0);
    rgb+=clock.w*aperture*phosphor*(phosphorLine*(0.7+scan*0.3)+echo*0.15);
    coverage+=clock.w*aperture*(phosphorLine*0.85+echo*0.10);
   }
   // Independently curling scan strips drift through the signal. RGB fringes
   // split sideways without moving the profile picture or readable text.
   for(int j=0;j<7;j++) {
    float fj=float(j),phase=t*(0.85+fj*0.055)+fj*1.9;
    float y=-260.0+fj*84.0+17.0*sin(phase);
    float centre=42.0*sin(phase*1.3);
    float width=178.0+100.0*(0.5+0.5*sin(phase+fj));
    float bendY=y+4.0*sin(p.x*0.026-phase);
    float reveal=(1.0-smoothstep(width-16.0,width+12.0,abs(p.x-centre)))*clock.w;
    float gate=0.24+0.76*pow(0.5+0.5*sin(phase*2.1),4.0);
    float core=exp(-abs(p.y-bendY)*0.8);
    float red=exp(-abs(p.y-bendY-3.5)*0.7);
    float cyan=exp(-abs(p.y-bendY+3.5)*0.7);
    float fragment=0.35+0.65*pow(0.5+0.5*cos(p.x*0.052+phase*3.0),6.0);
    rgb+=reveal*gate*fragment*(core*float3(0.55,1.0,0.80)+red*float3(0.90,0.07,0.12)+cyan*float3(0.02,0.45,1.0));
    coverage+=reveal*gate*fragment*(core*0.65+red*0.28+cyan*0.28);
    float staticNoise=noise(float2(floor(p.x/4.0),fj*9.0)+float2(t*5.0,0.0));
    float snow=exp(-abs(p.y-bendY)*0.12)*pow(staticNoise,6.0)*reveal*gate*0.18;
    rgb+=snow*float3(0.48,0.70,0.58);coverage+=snow;
   }
   alpha=saturate(coverage)*clock.y*edge;
   return float4(rgb*clock.y*edge,alpha);
  } else if(look.x<1.5) {
   float2 p=(uv-0.5)*460.0;
   float d=roundBox(p,float2(128.0,128.0),10.0);
   float rim=exp(-abs(d-3.0)*0.65),glow=exp(-abs(d-7.0)*0.15);
   float sweep=exp(-abs(p.y-sin(t*1.8)*123.0)*0.20);
   alpha=(rim*0.8+glow*(0.16+sweep*0.25))*clock.y;
   hot=rim;hue=0.7;
  } else if(look.x<2.5) {
   float2 p=(uv-0.5)*2.0;float2 pixel=floor(p*4.0)/4.0;
   float square=1.0-smoothstep(0.35,0.50,max(abs(p.x),abs(p.y)));
   float cell=step(0.15,frac(uv.x*6.0))*step(0.15,frac(uv.y*6.0));
   alpha=square*cell*(0.35+0.5*sin(t*3.0+look.z+pixel.x*7.0)*sin(t*3.0+look.z+pixel.x*7.0))*clock.y;
   hot=square*0.3;hue=0.9;
  } else {
   // Foreground glass over the actual cached profile picture: fine scanlines,
   // a slowly travelling roll and curved reflection. No CPU work per frame.
   float2 p=(uv-0.5)*2.0;
   float scan=(0.5+0.5*sin(uv.y*382.0-t*5.0))*0.11;
   float roll=exp(-abs(p.y-sin(t*0.9)*1.2)*19.0)*0.12;
   float reflection=exp(-abs(p.y+p.x*0.35+0.72)*13.0)*0.10;
   // Turn the real picture on from a horizontal electron beam, and retract
   // this exact aperture on exit. The timeline starts from the held state.
   float aperture=0.01+clock.w*1.15;
   float closed=smoothstep(aperture-0.025,aperture+0.025,abs(p.y));
   float beam=exp(-abs(abs(p.y)-aperture)*90.0)*(1.0-clock.w)*0.75;
   alpha=saturate(closed+scan+roll+reflection+beam)*clock.y*edge;
   float3 rgb=lerp(float3(0.025,0.014,0.040),tint.rgb,saturate(roll*4.0+reflection*5.0+beam));
   return float4(rgb*alpha,alpha);
  }
 } else if(look.w>0.5) {
  // Angular profile: independently unfolding blades, moving circuit traces
  // and segmented arcs. No plasma bitmap or translated full composition.
  float2 p=(uv-0.5)*2.0;
  if(look.x<0.5) {
   float2 q=float2(p.x,p.y+p.x*0.17);
   float opening=0.24+0.76*clock.w;
   float d=max(abs(q.x)/opening-0.72,abs(q.y)-0.57);
   d=max(d,abs(q.x)+abs(q.y)*0.35-0.85);
   float panel=1.0-smoothstep(-0.008,0.004,d);
   float rim=exp(-abs(d)*145.0);
   float cut=1.0-smoothstep(0.045,0.085,abs(q.y+q.x*0.14-0.05));
   float2 rail=float2(abs(q.x),q.y);
   float railWire=exp(-abs(rail.y+rail.x*0.32-0.27)*180.0);
   float tracer=pow(saturate(1.0-abs(frac(rail.x*0.85-t*0.40)-0.5)*2.0),16.0);
   float scan=exp(-abs(q.y-(frac(t*0.32)*1.30-0.65))*180.0)*panel;
   float bars=step(0.78,frac(uv.y*38.0))*exp(-abs(d-0.026)*80.0);
   float slash=exp(-abs(q.y+q.x*0.38-0.1*sin(t*2.0))*150.0)*exp(-abs(q.x)*2.0)*clock.z;
   alpha=(panel*0.87*(1.0-cut)+rim*0.96+railWire*tracer+scan*0.24+bars*0.35+slash)*clock.y;
   hot=rim+railWire*tracer+slash;hue=0.75;
   // The black polygon is part of the reference badge, not a scene backdrop.
   float3 panelRgb=float3(0.045,0.025,0.085);
   float3 accent=lerp(float3(0.19,0.83,1.0),tint.rgb,0.75);
   float lit=saturate(rim+railWire*tracer+bars*0.6+scan+slash);
   alpha=saturate(alpha)*edge;
   return float4(lerp(panelRgb,accent,lit)*alpha,alpha);
  } else if(look.x<1.5) {
   float2 q=p*230.0;float r=length(q),a=atan2(q.y,q.x);
   float wire=exp(-abs(r-150.0)*1.0);
   float sector=step(0.18,frac((a+3.14159265)*1.90986+t*0.10));
   float arc=exp(-abs(r-162.0)*0.8)*sector;
   float sweep=pow(0.5+0.5*cos(a-t*2.3),38.0);
   float hex=max(abs(q.x)*0.866+abs(q.y)*0.5,abs(q.y));
   float corners=exp(-abs(hex-184.0)*0.7)*step(0.60,abs(sin(a*3.0)));
   float glow=exp(-abs(r-153.0)*0.15);
   alpha=(wire*0.85+arc*0.75+corners*0.8+glow*(0.24+sweep))*clock.y;
   hot=wire*0.5+sweep+corners;hue=0.8;
  } else if(look.x<2.5) {
   float a=t*1.25+look.z,cs=cos(a),sn=sin(a);
   float2 q=float2(p.x*cs-p.y*sn,p.x*sn+p.y*cs);
   float d=max(abs(q.x)*0.866-q.y*0.5,q.y)-0.39;
   d=max(d,-q.y-0.62);
   float facet=0.5+0.5*sin(q.x*8.0-t*4.0+look.z);
   alpha=((1.0-smoothstep(-0.04,0.04,d))*(0.35+0.5*facet)+exp(-abs(d)*22.0)*0.55)*clock.y;
   hot=exp(-abs(d)*24.0);hue=0.9;
  } else {
   // Two white diagonal bands with bevel, violet underside and a travelling
   // highlight. Their reveal retracts symmetrically on the reversed timeline.
   float2 q=float2(p.x,p.y+p.x*(look.x<3.5?0.466:0.334));
   float opening=0.08+0.92*clock.y;
   float d=max(abs(q.x)/opening-0.90,abs(q.y)-0.57);
   d=max(d,abs(q.x)+q.y*0.13-0.89);
   float body=1.0-smoothstep(-0.012,0.012,d);
   float under=exp(-abs(d-0.08)*28.0);
   float sheen=exp(-abs(q.x-(frac(t*0.30)*2.8-1.4))*28.0);
   alpha=(body+under*0.48)*clock.y;whitePanel=body;
   float3 paper=lerp(float3(0.87,0.89,0.95),float3(1.0,1.0,1.0),saturate(0.6-q.y*0.2+sheen));
   alpha=saturate(alpha)*edge;
   return float4(lerp(tint.rgb,paper,whitePanel)*alpha,alpha);
  }
 } else if(look.x<0.5) {
  float2 p=(uv-0.5)*2.0;
  float2 jet=float2(p.x,p.y*1.14+0.09);
  float2 q=jet;
  float r=length(q),angle=atan2(q.y,q.x);
  float2 flow=float2(t*0.48,-t*0.32);
  float n=cloud(q*5.5+flow);
  float warp=cloud(q*8.0-float2(t*0.27,t*0.56));
  float wave=sin(angle*7.0+t*2.8+warp*5.0);
  float radius=0.49+0.065*wave+0.14*(n-0.5);
  float band=exp(-abs(r-radius)*12.0);
  float filaments=pow(saturate(1.0-abs(n-0.50)*7.5),3.0);
  float wisps=pow(saturate(1.0-abs(warp-0.47)*8.0),4.0);
  float smoke=band*(0.07+filaments*0.60+wisps*0.24);
  float electric=exp(-abs(r-radius-0.038*sin(angle*27.0+t*9.0+warp*6.0))*135.0);
  electric*=pow(0.5+0.5*sin(angle*11.0-t*4.0),2.0);
  float rays=pow(saturate(sin(angle*43.0+warp*4.0-t*1.7)),18.0)*exp(-abs(r-0.62)*16.0);
  float slash=exp(-abs(q.y+q.x*0.28+0.03*sin(q.x*32.0-t*13.0))*130.0)*exp(-abs(q.x)*2.0);
  float halo=exp(-abs(q.y+q.x*0.28)*26.0)*exp(-abs(q.x)*2.0);
  float shock=exp(-abs(r-(0.20+frac(t*0.73)*0.73))*95.0)*(1.0-frac(t*0.73));
  alpha=(smoke+electric*0.9+rays*0.72+(slash*1.15+halo*0.24+shock*0.60)*clock.z)*clock.y;
  alpha*=1.0-smoothstep(1.0,1.15,r);
  hot=electric+rays+slash*clock.z;
  hue=0.5+0.5*sin(angle*2.0+warp*6.0-t*1.1);
 } else if(look.x<1.5) {
  float2 p=(uv-0.5)*460.0;
  float contract=1.0;
  float r=length(p),angle=atan2(p.y,p.x);
  float jag=sin(angle*31.0-t*12.0)*1.8+sin(angle*67.0+t*19.0)*1.1+noise(float2(angle*13.0,t*5.0))*2.8;
  float d=abs(r-(151.0+jag)*contract);
  float wire=exp(-d*1.25),glow=exp(-d*0.13);
  float wire2=exp(-abs(r-(162.0+3.0*sin(angle*9.0+t*4.0))*contract)*0.9);
  float comet=pow(0.5+0.5*cos(angle-t*3.7),40.0)+pow(0.5+0.5*cos(angle+t*2.9+2.1),55.0);
  float corona=exp(-abs(r-(169.0+jag*3.0)*contract)*0.24)*pow(saturate(sin(angle*53.0+t*8.0)),8.0);
  alpha=(wire*0.96+glow*0.42+wire2*0.35+comet*glow*1.8+corona*0.45)*clock.y;
  alpha*=smoothstep(126.0*contract,137.0*contract,r);
  hot=wire*0.65+comet;hue=0.5+0.5*sin(angle*1.7+t*0.7);
 } else {
  float2 p=(uv-0.5)*2.0;float r=length(p);
  float a=clock.x*1.7+look.z,cs=cos(a),sn=sin(a);
  float2 q=float2(p.x*cs-p.y*sn,p.x*sn+p.y*cs);
  float shape=0.0;
  if(look.y<0.5) {
   // Signed heart silhouette: its facets and shine evolve independently.
   float2 h=float2(q.x,-q.y)*1.65;float k=dot(h,h)-0.8;
   shape=1.0-smoothstep(-0.07,0.09,k*k*k-h.x*h.x*h.y*h.y*h.y);
   shape*=1.0-smoothstep(0.76,0.90,r);
  } else if(look.y<1.5) {
   float roof=-0.23-0.34*abs(sin(q.x*5.4));
   shape=(1.0-smoothstep(0.57,0.65,abs(q.x)))*smoothstep(roof-0.06,roof,q.y)*(1.0-smoothstep(0.42,0.50,q.y));
  } else if(look.y<3.5) {
   shape=1.0-smoothstep(0.46,0.54,max(abs(q.x),abs(q.y)));
   float ribbon=exp(-abs(q.x)*28.0)+exp(-abs(q.y+0.18)*28.0);
   hot=ribbon*0.65;
  } else if(look.y<4.5) {
   float head=1.0-smoothstep(0.18,0.23,length(q-float2(0.0,-0.32)));
   shape=max(head,(1.0-smoothstep(0.39,0.46,length(float2(q.x,q.y*0.75)-float2(0.0,0.2))))*step(-0.12,q.y));
  } else {
   shape=1.0-smoothstep(0.64,0.74,abs(q.x)+abs(q.y)*0.75);
  }
  float facet=0.5+0.5*sin(q.x*7.0+q.y*5.0-t*5.0);
  float glint=exp(-abs(q.x+q.y*0.45-sin(t*3.0+look.z)*0.6)*32.0);
  float spark=exp(-abs(p.x)*30.0-abs(p.y)*4.5)+exp(-abs(p.y)*30.0-abs(p.x)*4.5);
  alpha=(shape*(0.50+facet*0.45)+spark*0.42)*clock.y;
  hot+=glint*shape+spark*0.7;hue=0.5+0.5*sin(look.z+t+q.x*3.0);
 }
 alpha=saturate(alpha)*edge;
 float3 blue=float3(0.03,0.67,1.0),pink=float3(0.95,0.02,1.0);
 float3 rgb=lerp(lerp(blue,pink,hue),tint.rgb,0.25);
 if(look.w>1.5)rgb=lerp(float3(0.94,0.60,0.28),tint.rgb,0.85);
 else if(look.w>0.5)rgb=lerp(float3(0.15,0.82,1.0),tint.rgb,0.82);
 rgb=lerp(rgb,float3(1.0,0.92,1.0),saturate(hot)*0.78);
 return float4(rgb*alpha,alpha);
}
technique Draw { pass { vertex_shader=VSMain(input);pixel_shader=PSMain(input); } }
)";
}
