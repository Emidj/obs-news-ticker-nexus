// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "obs-visual-renderer.hpp"
#include "sources/event-list-model.hpp"
namespace oto {
inline bool drawEventListFrame(ObsVisualRenderer &renderer,const sources::ListFrame &frame,visual::MotionClock::time_point now,bool settled=false)
{
	bool finished=true;
	const auto shiftMs=sources::listShiftDuration(frame.settings.durationMs);
	const QRectF clip(0,0,frame.settings.width,frame.settings.height());
	if(frame.emptyState)renderer.draw(frame.emptyState,true,{0,12,1,0,0,0,clip});
	if(!settled)for(const auto &row:frame.exiting) {
		const auto elapsed=std::chrono::duration<double,std::milli>(now-row.exitStarted).count();
		if(elapsed>=shiftMs)continue;
		const float opacity=1-visual::smooth(float(elapsed/shiftMs)),y=sources::listRowY(row,now,frame.settings.durationMs);
		renderer.draw(row.base,true,{0,y,opacity,0,0,0,clip});
		const float scroll=row.scrollDistance?visual::samplePingPong(std::chrono::duration<double,std::milli>(now-row.scrollStarted).count(),row.scrollDistance):0;
		renderer.draw(row.name,true,{float(row.nameX),y,opacity,0,scroll,float(row.nameWidth),clip});
		finished=false;
	}
	for(std::size_t i=0;i<frame.rows.size();++i) {
		// Preserve the existing appearance; directional entries pass in front of older rows.
		const auto &row=frame.rows[frame.settings.entrance==sources::ListEntrance::Appearance?i:frame.rows.size()-1-i];
		const auto sample=!settled && row.entered!=visual::MotionClock::time_point{}?sources::sampleListEntry(std::chrono::duration<double,std::milli>(now-row.entered).count(),frame.settings.entrance,frame.settings.width,frame.settings.rowHeight,frame.settings.durationMs):sources::ListEntrySample{};
		const float y=(settled?row.y:sources::listRowY(row,now,frame.settings.durationMs))+sample.offsetY;
		renderer.draw(row.base,true,{sample.offsetX,y,sample.card,0,0,0,clip});
		if(!sample.finished) {
			visual::ChangeSample effect;effect.glow=sample.glow;effect.particles=sample.particles;effect.sweep=sample.sweep;effect.progress=sample.progress;effect.finished=false;
			const auto &style=frame.settings.style;
			renderer.drawChange(QRect(12+qRound(sample.offsetX),qRound(y)+4,frame.settings.width-24,frame.settings.rowHeight-8),row.accent,effect,style.changeGlow,style.changeParticles,style.changeSweep,4,4,{style.changePattern,style.changeParticleShape,style.cardRadius},clip);
		}
		const float scroll=row.scrollDistance?visual::samplePingPong(std::chrono::duration<double,std::milli>(now-row.scrollStarted).count(),row.scrollDistance):0;
		renderer.draw(row.name,true,{row.nameX+sample.offsetX,y,sample.text,0,scroll,float(row.nameWidth),clip});
		finished=finished && sample.finished && (settled || row.moved==visual::MotionClock::time_point{} || std::chrono::duration<double,std::milli>(now-row.moved).count()>=shiftMs);
	}return finished;
}
}
