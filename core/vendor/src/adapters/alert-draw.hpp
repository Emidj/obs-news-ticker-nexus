// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "obs-visual-renderer.hpp"
#include "sources/alert-model.hpp"
namespace oto {
// Already prepared layers only; no timers, layout, images, files or network.
inline bool drawAlertFrame(ObsVisualRenderer &renderer,const sources::AlertFrame &frame,visual::MotionClock::time_point now)
{
	const auto elapsed=std::chrono::duration<double,std::milli>(now-frame.started).count();
	const auto sample=sources::sampleAlert(elapsed,frame.settings.times);if(sample.finished)return true;
	const double motionMs=sample.visualMs;
	const float t=float(motionMs*.001);
	const auto draw=[&](const visual::FramePtr &layer,float cx,float cy,float opacity,float scale=1,float angle=0,float material=0){
		if(!layer || opacity<=0)return;const float radians=angle*3.14159265f/180;
		float w=layer->image.width()*scale*.5f,h=layer->image.height()*scale*.5f;
		float ex=std::abs(w*std::cos(radians))+std::abs(h*std::sin(radians)),ey=std::abs(w*std::sin(radians))+std::abs(h*std::cos(radians));
		// Fit the entire rotated sprite including authored glow into the fixed
		// square. This places sprites safely; it does not crop their graphics.
		const float fit=(std::min)({1.0f,498.0f/ex,498.0f/ey});scale*=fit;w*=fit;h*=fit;ex*=fit;ey*=fit;
		cx=std::clamp(cx,ex+2,998-ex);cy=std::clamp(cy,ey+2,998-ey);
		DrawParams p;p.x=cx-w*std::cos(radians)+h*std::sin(radians);p.y=cy-w*std::sin(radians)-h*std::cos(radians);p.opacity=std::clamp(opacity,0.0f,1.0f);p.scale=scale;p.angle=angle;p.material=material;p.time=t;p.materialVariant=float(frame.variant);renderer.draw(layer,true,p);
	};
	renderer.drawAlertEnergy(t,frame.accent,sample.portrait,sample.burst,sample.effects,0,frame.variant,frame.particles,frame.energyPattern,sample.title,sample.offsetX*.6f);
	// Entry and its reverse use identical centres, shapes and authored layers.
	draw(frame.portrait,500,315,sample.portrait,.84f);
	if(frame.energyPattern==2)renderer.drawAlertEnergy(t,frame.accent,sample.portrait,sample.burst,sample.effects,0,frame.variant,0,2,sample.title,0,true);
	if(frame.animatedEmblem){DrawParams icon;icon.x=620;icon.y=245;icon.scale=200.0f/128;icon.opacity=sample.portrait*sample.burst;icon.time=t;icon.sourceRect=sources::alertIconCell(motionMs);renderer.draw(frame.emblem,true,icon);}
	else draw(frame.emblem,720,345,sample.portrait*sample.burst);
	draw(frame.caption,(frame.energyPattern>=2?500:430)+sample.offsetX*.6f,470,sample.title,sample.scale,frame.energyPattern>=2?0:-7);
	draw(frame.heading,500+sample.offsetX*.6f,535,sample.title,sample.scale,frame.energyPattern>=2?0:-7);
	draw(frame.badge,500,630,sample.name);draw(frame.detail,500,707,sample.name);
	if(sample.name>0){const float scroll=frame.scrollDistance?visual::samplePingPong((std::max)(0.0,motionMs-frame.settings.times.entry),frame.scrollDistance):0;DrawParams p;p.x=500-frame.nameViewport*.5f;p.y=599;p.opacity=sample.name;p.offsetX=scroll;p.viewportWidth=float(frame.nameViewport);p.time=t;renderer.draw(frame.name,true,p);}
	return false;
}
}
