// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "obs-visual-renderer.hpp"
#include "sources/supporter-frame.hpp"
namespace oto {
// Production and the manual GPU probe use the same immutable plan and timeline.
inline bool drawSupporterFrame(ObsVisualRenderer &renderer, const sources::BarFrame &frame,
	visual::MotionClock::time_point now, bool settled = false)
{
	if(frame.base)renderer.draw(frame.base);
	bool finished=true;
	for(size_t i=0;i<frame.layers.size();++i) {
		const auto &layer=frame.layers[i];if(!layer.frame)continue;
		const bool animated=!settled && layer.started!=visual::MotionClock::time_point{};
		const auto elapsed=animated?std::chrono::duration<double,std::milli>(now-layer.started).count():0;
		const auto scroll=layer.scrollDistance?visual::samplePingPong(std::chrono::duration<double,std::milli>(now-layer.scrollStarted).count(),layer.scrollDistance):0;
		if(frame.settings.motion==visual::Motion::StyleChange && animated) {
			const auto sample=visual::sampleChange(elapsed,frame.settings.durationMs);
			const auto &style=frame.settings.style;
			const auto &accent=style.changeColors[i].isValid()?style.changeColors[i]:style.titleColors[i].isValid()?style.titleColors[i]:style.cardEdge;
			const int marginY=std::clamp(std::min(layer.card.y(),frame.settings.height-layer.card.y()-layer.card.height()),0,24);
			if(!sample.finished)renderer.drawChange(layer.card,accent,sample,style.changeGlow,style.changeParticles,style.changeSweep,std::min(6,style.gap/2),marginY,{style.changePattern,style.changeParticleShape,style.cardRadius});
			if(!sample.finished && layer.previousValue)renderer.draw(layer.previousValue,true,{float(layer.x),float(layer.y),sample.oldOpacity,sample.oldOffsetY,layer.previousOffsetX,float(layer.viewportWidth)});
			renderer.draw(layer.frame,true,{float(layer.x),float(layer.y),sample.newOpacity,sample.newOffsetY,scroll,float(layer.viewportWidth)});
			finished=finished && sample.finished;
		} else {
			const auto sample=animated?visual::sampleMotion(frame.settings.motion,elapsed,frame.settings.durationMs):visual::MotionSample{};
			renderer.draw(layer.frame,true,{float(layer.x),float(layer.y),sample.opacity,sample.offsetY,scroll,float(layer.viewportWidth)});
			finished=finished && sample.finished;
		}
	}
	return finished;
}
}
