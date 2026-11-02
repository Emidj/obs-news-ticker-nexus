// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "obs-visual-renderer.hpp"
#include "sources/hype-motion.hpp"
namespace oto {
inline void drawHypeFrame(ObsVisualRenderer &renderer,const sources::HypeFrame &f,visual::MotionClock::time_point now) {
	const auto s=sources::sampleHypeMotion(f,now);if(s.finished)return;
	const int w=f.settings.width,h=f.settings.height;
	const float targetX=float(f.titleBox.center().x()),targetY=float(f.titleBox.bottom()+6),titleY=float(f.titleBox.center().y()),contraction=1-.92f*s.gather;
	const double elapsed=(std::max)(0.0,std::chrono::duration<double,std::milli>(now-f.clockStart).count());
	const float t=float(std::fmod(elapsed/1000,3600.0));
	const float content=1-visual::smooth((s.gather-.65f)/.35f);
	const float titleShift=(h*.5f-titleY)*(s.leave>0?s.leave:1-visual::smooth(s.phase/.28f));
	const auto layer=[&](const visual::FramePtr &frame,float visible,float scale,float cx,float cy,float shift=0,bool gather=true){
		if(!frame || visible<=0 || (gather && content<=0))return;
		if(gather)scale*=contraction;
		DrawParams p;p.opacity=visible*(gather?content:1);p.scale=scale;
		p.x=cx*(1-scale)+shift+(gather?(targetX-cx)*s.gather:0);p.y=cy*(1-scale)+(gather?(targetY-cy)*s.gather:titleShift);p.clip=QRectF(2,2,w-4,h-4);renderer.draw(frame,true,p);
	};
	// Trace the real authored silhouette, then gather its four regions independently.
	if(f.authored && s.outline>0 && content>0){
		const int left=f.bar.x()-5,right=f.leaderBox.x()-5,time=f.timerBox.x()-w*5/100;
		for(const auto &region:{QRect(2,2,left-2,h-4),QRect(left,2,time-left,h-4),QRect(time,2,right-time,h-4),QRect(right,2,w-2-right,h-4)}){
			const auto c=region.center();DrawParams p;p.opacity=content;p.scale=contraction;p.x=c.x()*(1-p.scale)+(targetX-c.x())*s.gather;p.y=c.y()*(1-p.scale)+(targetY-c.y())*s.gather;
			p.clip=QRectF(p.x+region.x()*p.scale,p.y+region.y()*p.scale,region.width()*p.scale,region.height()*p.scale).intersected(QRectF(2,2,w-4,h-4));
			if(!p.clip.isValid())continue;p.material=1;p.materialVariant=float(7+f.settings.style.pattern);p.time=std::fmod(t,80.f);p.dissolve=s.outline;renderer.draw(f.base,true,p);
		}
	}else if(!f.authored)layer(f.base,visual::smooth((s.phase-.40f)/.18f),1,w*.5f,h*.5f);
	const float ratio=float(sources::hypeFill(f,now))*s.bar;
	if(s.bar>0 && ratio>0 && content>0){
		DrawParams p;p.scale=contraction;p.opacity=s.bar*content;
		const QPointF center=f.bar.center();p.x=float(center.x())*(1-p.scale)+(targetX-float(center.x()))*s.gather;p.y=float(center.y())*(1-p.scale)+(targetY-float(center.y()))*s.gather;
		p.clip=QRectF(p.x+f.bar.x()*p.scale,p.y+f.bar.y()*p.scale,f.bar.width()*ratio*p.scale,f.bar.height()*p.scale);
		if(f.settings.fillMode==2){p.material=1;p.materialVariant=float(17+f.settings.style.pattern);p.time=std::fmod(t,80.f);p.dissolve=s.bar;}renderer.draw(f.fill,true,p);
	}
	renderer.drawHypeEnergy({w,h},f.settings.accent,f.settings.style.secondary,t,s.phase,s.burst,f.settings.fillMode==2?s.bar:0,ratio,f.settings.style.pattern,f.settings.style.particles,s.gather,s.morph,s.leave,f.bar,f.titleBox,f.authored,f.levelBox,s.levelProgress,s.levelFlash);
	// Title is a separate zoom impact, not a fade of the entire meter.
	if(s.morph>0 && s.morph<1){
		for(int i=0;i<2;++i){DrawParams p;p.opacity=s.title*(i?s.morph:1-s.morph);p.material=1;p.materialVariant=6;p.time=std::fmod(t,80.0f);p.dissolve=s.morph;p.scale=s.titleScale;p.x=targetX*(1-p.scale);p.y=titleY*(1-p.scale);p.clip=QRectF(2,2,w-4,h-4);renderer.draw(i?f.thanks:f.title,true,p);}
	}else layer(s.morph>=1?f.thanks:f.title,s.title,s.titleScale,targetX,titleY,0,false);
	layer(f.level,s.bar,s.levelScale,float(f.levelBox.center().x()),float(f.levelBox.center().y()));
	const float shift=30*(1-s.details);
	layer(f.labels,s.details,1,w*.76f,h*.60f);layer(f.numbers,s.details,1,float(f.bar.center().x()),float(f.bar.center().y()),shift);layer(f.timer,s.details,1,float(f.timerBox.center().x()),float(f.timerBox.center().y()),shift);
	for(const auto &name:f.names)if(name.frame && s.details>0 && content>0){
		DrawParams p;p.scale=contraction;p.x=name.box.x()*(1-s.gather)+(targetX-name.box.width()*p.scale*.5f)*s.gather+shift;p.y=name.box.y()*(1-s.gather)+(targetY-name.box.height()*p.scale*.5f)*s.gather;p.opacity=s.details*content;p.viewportWidth=name.box.width();
		p.offsetX=name.distance?visual::samplePingPong(elapsed,name.distance):0;p.clip=QRectF(p.x,p.y,name.box.width()*p.scale,name.box.height()*p.scale);renderer.draw(name.frame,true,p);
	}
}
}
