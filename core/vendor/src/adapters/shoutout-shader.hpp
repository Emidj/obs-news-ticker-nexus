// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace oto {
// Shared motion shader extension. Patterns are preset data, never separate
// renderers. Coordinates are local to the unmodified artwork/aperture.
constexpr const char *shoutoutMotionShader = R"(
uniform float4 native_portrait;
float soPulse(float p,float begin,float duration) {
 float u=nativePhase(p,begin,duration);return sin(u*3.14159265)*(1.0-u);
}
float2 soRotate(float2 v,float angle) {
 float s=sin(angle),c=cos(angle);return float2(v.x*c-v.y*s,v.x*s+v.y*c);
}
float2 soTitleCenter(float k) {
 if(k==0.0)return float2(.57,.26);
 if(k==1.0)return float2(.61,.17);
 if(k==2.0)return float2(.62,.29);
 if(k==3.0)return float2(.55,.26);
 if(k==4.0)return float2(.51,.27);
 if(k==5.0)return float2(.62,.20);
 if(k==7.0)return float2(.61,.28);
 if(k==8.0)return float2(.65,.28);
 return float2(.59,.17);
}
float2 soCallCenter(float k) {
 if(k==2.0)return float2(.79,.64);
 if(k==3.0)return float2(.81,.74);
 if(k==4.0)return float2(.82,.59);
 if(k==5.0)return float2(.83,.58);
 if(k==7.0)return float2(.88,.65);
 if(k==9.0)return float2(.59,.76);
 return float2(.60,.82);
}
float2 soWarp(float2 uv) {
 float2 a=(uv-native_art.xy)/native_art.zw;
 float p=material.z,k=material.w-28.0;
 if(p>=.999)return uv;
 float2 centre=native_portrait.xy+native_portrait.zw*.5;
 float2 q=a-centre;
 float rim=nativeZone(a,centre,native_portrait.zw*.74);
 float title=nativeZone(a,soTitleCenter(k),float2(.30,.19));
 float call=nativeZone(a,soCallCenter(k),float2(.23,.18));
 float settle=nativePhase(p,0.0,.94);
 float energy=sin(settle*3.14159265)*(1.0-settle);
 float hit=nativePhase(p,.32,.33);
 float elastic=1.0-nativeSpring(hit);
 // Each design deforms continuously. Soft influence fields do not cut pieces.
 if(k==0.0) {
  a+=float2(sin(a.y*26.0-p*24.0),cos(a.x*19.0+p*20.0))*.026*energy*(1.0-rim);
  float turn=.42*(1.0-nativeSpring(nativePhase(p,.03,.37)))*rim;
  a+=soRotate(q,turn)-q;
  a+=(a-soTitleCenter(k))*.18*elastic*title;
 } else if(k==1.0) {
  float turn=-.65*(1.0-nativeSpring(nativePhase(p,.02,.42)))*rim;
  a+=soRotate(q,turn)-q;
  a.x+=(a.x<.45?-1.0:1.0)*.07*(1.0-nativeSpring(nativePhase(p,.10,.43)))*(1.0-rim);
  a.y+=title*.06*elastic;
 } else if(k==2.0) {
  // Analog sync drift decays smoothly; no random frame-to-frame jumps.
  a.x+=(sin(a.y*75.0+p*30.0)*.020+sin(a.y*199.0-p*54.0)*.012)*energy;
  a.y+=sin(a.x*17.0-p*12.0)*.015*energy;
  a.x+=title*sin(hit*38.0)*.020*(1.0-hit);
 } else if(k==3.0) {
  // Neon horizon flexes, palm leaves sway, sunset compresses then rebounds.
  float palms=(1.0-smoothstep(.08,.22,a.x))+smoothstep(.84,.97,a.x);
  a.x+=palms*sin(a.y*10.0-p*16.0)*.034*energy;
  a.y+=sin(a.x*15.0-p*12.0)*.026*energy;
  a+=(a-soTitleCenter(k))*title*.16*elastic;
 } else if(k==4.0) {
  // Three diagonal plates seat with a controlled impact, following the art.
  float plate=sin(a.x*9.0+a.y*12.0);
  a+=float2(.10,-.065)*plate*(1.0-nativeSpring(nativePhase(p,.03,.52)));
  a+=title*float2(sin(hit*43.0),sin(hit*37.0))*.014*soPulse(p,.34,.34);
 } else if(k==5.0) {
  // Elastic glass and a gentle lens ripple centred on the authored aperture.
  float radial=length(q/native_portrait.zw);
  a+=q*sin(radial*19.0-p*21.0)*.12*energy*rim;
  a.y+=sin(a.x*8.0-p*9.0)*.025*energy*(1.0-rim);
  a+=(a-soCallCenter(k))*.13*elastic*call;
 } else if(k==7.0) {
  // Sleek ribbons flex along their own diagonal then settle precisely.
  a.y+=sin(a.x*3.14159265)*.055*(1.0-nativeSpring(nativePhase(p,.04,.56)));
  a.x+=title*.035*elastic;
  a.x-=call*.045*(1.0-nativeSpring(nativePhase(p,.66,.28)));
 } else if(k==8.0) {
  // Industrial casing compresses as lamps ignite; title plate locks in.
  a.y+=(a.y-.54)*.11*(1.0-nativeSpring(nativePhase(p,.04,.49)));
  a.x+=sin(a.y*16.0)*.015*energy*(1.0-title);
  a.y+=title*.08*elastic;
  a.x+=call*.025*sin(hit*24.0)*(1.0-hit);
 } else {
  // A coherent holographic field curls into place instead of sliced strips.
  float radius=length(q);
  float turn=.35*(1.0-nativeSpring(nativePhase(p,.01,.42)))*rim;
  a+=soRotate(q,turn)-q;
  a.x+=sin(a.y*23.0-p*20.0)*.022*energy;
  a.y+=sin(radius*31.0-p*20.0)*.027*energy*(1.0-rim);
  a.y+=title*.045*elastic;
 }
 // The callout gets a distinct final elastic beat, not a moving PNG plane.
 a+=(a-soCallCenter(k))*.055*(1.0-nativeSpring(nativePhase(p,.65,.30)))*call;
 return native_art.xy+a*native_art.zw;
}
float4 soInk(float4 color,float2 uv) {
 float2 a=(uv-native_art.xy)/native_art.zw;
 float p=material.z,k=material.w-28.0;
 float2 centre=native_portrait.xy+native_portrait.zw*.5;
 float2 q=(a-centre)/native_portrait.zw;
 float radial=length(q),angle=atan2(q.y,q.x)+3.14159265;
 float rim=nativeZone(a,centre,native_portrait.zw*.77);
 float title=nativeZone(a,soTitleCenter(k),float2(.29,.18));
 float call=nativeZone(a,soCallCenter(k),float2(.21,.17));
 float3 rgb=color.rgb/max(color.a,.00001);
 float emissive=smoothstep(.20,.72,max(rgb.r,max(rgb.g,rgb.b)));
 float first=nativePhase(p,.0,.42),ring=nativePhase(p,.05,.39);
 float field=length((a-centre)*float2(.9,1.15));
 float gate=1.0-smoothstep(first*.98,first*.98+.09,field);
 float edge=exp(-abs(field-first*.98)*43.0)*sin(first*3.14159265);
 if(k==0.0) {
  float sweep=angle/6.2831853;
  float ringGate=1.0-smoothstep(ring,ring+.10,sweep);
  gate=lerp(gate,ringGate,rim*emissive);
  edge+=pow(max(0.0,sin(a.x*18.0+a.y*13.0-p*34.0)),8.0)*soPulse(p,.0,.72);
 } else if(k==1.0) {
  float circuit=abs(a.x-centre.x)+abs(a.y-centre.y)*.55;
  gate=1.0-smoothstep(first*1.05,first*1.05+.045,circuit);
  edge=exp(-abs(circuit-first*1.05)*64.0)*sin(first*3.14159265);
 } else if(k==2.0) {
  float tube=abs(a.y-.52)+.07*sin(a.x*7.0);
  gate=1.0-smoothstep(first*.62,first*.62+.025,tube);
  edge=exp(-abs(tube-first*.62)*85.0)*sin(first*3.14159265);
  color.rgb*=1.0-.18*soPulse(p,.08,.62)*(.5+.5*sin(a.y*190.0-p*35.0));
 } else if(k==3.0) {
  float horizon=abs(a.y-.60)+.12*sin(a.x*7.0-p*8.0)*(1.0-first);
  gate=1.0-smoothstep(first*.85,first*.85+.065,horizon);
  edge=exp(-abs(horizon-first*.85)*56.0)*sin(first*3.14159265);
 } else if(k==4.0) {
  float slash=abs(a.x*.58+a.y*.42-.47);
  gate=1.0-smoothstep(first*.62,first*.62+.025,slash);
  edge=exp(-abs(slash-first*.62)*72.0)*sin(first*3.14159265);
 } else if(k==5.0) {
  float wave=field+.035*sin(a.x*16.0-p*12.0);
  gate=1.0-smoothstep(first*1.10,first*1.10+.09,wave);
  edge=exp(-abs(wave-first*1.10)*47.0)*sin(first*3.14159265);
 } else if(k==7.0) {
  float ribbon=a.x*.85+a.y*.15;
  gate=1.0-smoothstep(first*1.22-.10,first*1.22+.025,ribbon);
  edge=exp(-abs(ribbon-(first*1.22-.08))*60.0)*sin(first*3.14159265);
 } else if(k==8.0) {
  float ignition=abs(a.x-.50)*.95+abs(a.y-.51)*.25;
  gate=1.0-smoothstep(first*.80,first*.80+.065,ignition);
  edge=exp(-abs(ignition-first*.80)*73.0)*sin(first*3.14159265);
 } else {
  float field2=field+.025*sin(a.y*21.0-p*20.0);
  gate=1.0-smoothstep(first*1.05,first*1.05+.040,field2);
  edge=exp(-abs(field2-first*1.05)*80.0)*sin(first*3.14159265);
 }
 if(first>=.999)gate=1.0;
 // Actual luminous ink and soft influence fields delay title/CTA organically.
 gate*=lerp(1.0,smoothstep(.30,.50,p),title*emissive);
 gate*=lerp(1.0,smoothstep(.65,.82,p),call*emissive);
 if(p>=.999)gate=1.0;
 if(p<=.00001)gate=0.0;
 float glint=.0;
 if(p<.999)glint=emissive*edge*.50;
 else glint=emissive*.018*pow(max(0.0,sin(material.y*1.5707963+a.x*7.0+a.y*3.0)),12.0);
 color.rgb=lerp(color.rgb,float3(color.a,color.a,color.a),saturate(glint));
 return color*gate;
}
)";
}
