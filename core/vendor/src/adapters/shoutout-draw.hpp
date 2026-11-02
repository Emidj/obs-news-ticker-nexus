// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "obs-visual-renderer.hpp"
#include "sources/shoutout-model.hpp"
namespace oto {
inline float shoutoutArtworkPhase(double elapsed,sources::AlertTimes times) noexcept
{
 if(times.entry<=0 || times.exit<=0 || !std::isfinite(elapsed))return 0;
 return float(std::clamp(elapsed<times.entry?elapsed/times.entry:elapsed<times.entry+times.hold?1.0:(times.total()-elapsed)/times.exit,0.0,1.0));
}
inline void drawNativeShoutoutArtwork(ObsVisualRenderer &renderer,const visual::FramePtr &base,const presets::ShoutoutStyle &style,double elapsed,sources::AlertTimes times)
{
 const auto sample=sources::sampleShoutout(elapsed,times);if(sample.finished)return;
 DrawParams p;p.clip=QRectF(style.assetBox).adjusted(0,0,-.5,-.5);p.material=1;p.materialVariant=style.motionPattern==6?27:28+style.motionPattern;
 p.time=float(std::fmod((std::max)(0.0,elapsed)/1000.0,80.0));p.dissolve=shoutoutArtworkPhase(elapsed,times);
 p.nativeArt={style.assetBox.x()/800.0,style.assetBox.y()/800.0,style.assetBox.width()/800.0,style.assetBox.height()/800.0};
 p.nativePortrait={double(style.portraitBox.x()-style.assetBox.x())/style.assetBox.width(),double(style.portraitBox.y()-style.assetBox.y())/style.assetBox.height(),double(style.portraitBox.width())/style.assetBox.width(),double(style.portraitBox.height())/style.assetBox.height()};
 renderer.draw(base,true,p);
}
inline void drawShoutoutFrame(ObsVisualRenderer &renderer,const sources::ShoutoutFrame &frame,visual::MotionClock::time_point now)
{
	const auto elapsed=std::chrono::duration<double,std::milli>(now-frame.started).count();auto sample=sources::sampleShoutout(elapsed,frame.settings.times);if(sample.finished)return;
	const bool native=frame.style.motionPattern>=0 && frame.style.motionPattern<=9;
 if(native){const float phase=shoutoutArtworkPhase(elapsed,frame.settings.times);sample.opacity=1;sample.portrait=visual::smooth((phase-.22f)/.24f);sample.data=visual::smooth((phase-.60f)/.18f);sample.dataScale=1+.10f*(1-sample.data);}
 DrawParams body;body.opacity=sample.opacity;body.clip={0,0,800,800};
	DrawParams portrait=body;portrait.scale=native?1+.06f*(1-sample.portrait):1;portrait.x=frame.style.portraitBox.x()+frame.style.portraitBox.width()*(1-portrait.scale)*.5f;portrait.y=frame.style.portraitBox.y()+frame.style.portraitBox.height()*(1-portrait.scale)*.5f;portrait.opacity*=sample.portrait;portrait.clip=native?QRectF(frame.style.portraitBox):QRectF(frame.style.portraitBox.x(),frame.style.portraitBox.y(),frame.style.portraitBox.width()*sample.portrait,frame.style.portraitBox.height());renderer.draw(frame.portrait,true,portrait);
	if(native)drawNativeShoutoutArtwork(renderer,frame.base,frame.style,elapsed,frame.settings.times);else renderer.draw(frame.base,true,body); // Authored aperture covers the photo's edges and keeps its rim intact.
	const auto pivot=frame.style.nameBox.united(frame.style.followersBox).center();DrawParams data=body;data.clip={};data.scale=sample.dataScale;data.x=pivot.x()*(1-data.scale);data.y=pivot.y()*(1-data.scale);data.opacity*=sample.data;renderer.draw(frame.details,true,data);
	DrawParams name=data;name.x+=frame.style.nameBox.x()*name.scale;name.y+=frame.style.nameBox.y()*name.scale;name.offsetX=visual::samplePingPong(elapsed,frame.scrollDistance);name.viewportWidth=frame.style.nameBox.width();renderer.draw(frame.name,true,name);
}
}


