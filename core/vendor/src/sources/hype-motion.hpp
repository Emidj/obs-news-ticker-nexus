// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "hype-model.hpp"
namespace oto::sources {
struct HypeMotion {float phase=1,title=1,outline=1,bar=1,details=1,burst=0,titleScale=1,levelScale=1,gather=0,morph=0,leave=0,levelProgress=-1,levelFlash=0;bool finished=false;};
inline HypeMotion sampleHypeMotion(const HypeFrame &frame,visual::MotionClock::time_point now) {
	HypeMotion s;
	if(frame.appeared!=visual::MotionClock::time_point{})s.phase=float(std::clamp(std::chrono::duration<double,std::milli>(now-frame.appeared).count()/frame.settings.entranceMs,0.0,1.0));
	if(frame.view.ended){
		const double elapsed=(std::max)(0.0,std::chrono::duration<double,std::milli>(now-frame.ended).count());
		const float closing=float(elapsed/frame.settings.exitMs);
		s.gather=visual::smooth(closing/.55f);s.morph=visual::smooth((closing-.55f)/.45f);
		s.leave=visual::smooth(float((elapsed-frame.settings.exitMs-frame.settings.holdMs)/frame.settings.exitMs));
		// Freeze the entrance pose at End; never suddenly reveal missing layers.
		if(frame.appeared!=visual::MotionClock::time_point{})s.phase=float(std::clamp(std::chrono::duration<double,std::milli>(frame.ended-frame.appeared).count()/frame.settings.entranceMs,0.0,1.0));
	}
	const auto stage=[&](float a,float b){return visual::smooth((s.phase-a)/(b-a));};
	s.title=stage(0,.12f);s.titleScale=1+.65f*(1-stage(0,.28f));
	s.outline=stage(.22f,.54f);s.bar=stage(.50f,.78f);s.details=stage(.78f,1);
	s.levelScale=1+.6f*(1-s.bar);
	s.burst=(std::max)(0.0f,std::sin(s.phase*3.14159265f)*.9f);
	if(!frame.view.ended && frame.celebrated!=visual::MotionClock::time_point{}) {
		const auto age=std::chrono::duration<double,std::milli>(now-frame.celebrated).count()/2600;
		if(age>=0 && age<1){
			s.levelProgress=float(age);
			s.levelFlash=visual::smooth(s.levelProgress/.07f)*(1-visual::smooth((s.levelProgress-.25f)/.75f));
			s.burst=(std::max)(s.burst,s.levelFlash);
			const float impact=visual::smooth(s.levelProgress/.065f)*(1-visual::smooth((s.levelProgress-.10f)/.40f));
			s.levelScale+=.30f*impact;
		}
	}
	s.titleScale+=.65f*s.leave;s.title*=1-s.leave;s.finished=s.leave>=1;return s;
}
}
