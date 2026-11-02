// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "sources/chat-model.hpp"
#include "obs-visual-renderer.hpp"
#include <cmath>
namespace oto {
inline void drawChatFrame(ObsVisualRenderer &renderer,const sources::ChatFrame &frame,visual::MotionClock::time_point now,QPointF offset={}){
	const QRectF sourceClip(offset,QSizeF(frame.settings.width,frame.settings.height));
	if(frame.border){DrawParams border;border.x=float(offset.x());border.y=float(offset.y());border.clip=sourceClip;renderer.draw(frame.border,true,border);}
	const QRectF clip=QRectF(frame.content).translated(offset).intersected(sourceClip);
	const int elapsed=int(std::chrono::duration_cast<std::chrono::milliseconds>(now-frame.mediaClock).count()%86400000);
	for(const auto &row:frame.rows){
		const auto pose=sources::sampleChatRow(row,frame.settings.entryMs,frame.settings.layout,now);const auto pos=pose.position+offset;
		if(!QRectF(pos,row.graphic->image.size()).intersects(clip))continue;
		DrawParams params;params.x=float(pos.x());params.y=float(pos.y());params.opacity=pose.opacity;params.clip=clip;renderer.draw(row.graphic,true,params);
		for(const auto &badge:row.badges){if(!badge.media)continue;const auto point=pos+badge.box.topLeft();if(!QRectF(point,badge.box.size()).intersects(clip))continue;
			DrawParams icon;icon.x=float(point.x());icon.y=float(point.y());icon.opacity=pose.opacity;icon.clip=clip;icon.scale=float(badge.box.width()/badge.media->cell);icon.sourceRect=badge.media->frame(0);renderer.draw(badge.media->atlas,true,icon);
		}
		const auto origin=pos+row.bodyBox.topLeft();params.scale=row.bodyScale;
		params.clip=clip.intersected(row.bodyBox.translated(pos));if(params.clip.isEmpty())continue;
		for(const auto &page:row.bodyPages){const auto point=origin+page.position*row.bodyScale;if(!QRectF(point,QSizeF(page.graphic->image.size())*row.bodyScale).intersects(params.clip))continue;
			params.x=float(point.x());params.y=float(point.y());renderer.draw(page.graphic,true,params);}
		for(const auto &emote:row.emotes){
			const auto point=origin+emote.box.topLeft()*row.bodyScale;if(!emote.media || !QRectF(point,emote.box.size()*row.bodyScale).intersects(params.clip))continue;
			const auto effect=chat::sampleEmote(emote.effects,frame.settings.animatedEmotes,elapsed);
			params.x=float(point.x());params.y=float(point.y());params.scale=float(emote.box.width()/emote.media->cell)*row.bodyScale;
			params.sourceRect=emote.media->frame(frame.settings.animatedEmotes?elapsed:0);params.cellFlipX=(emote.effects & chat::FlipX)!=0;params.cellFlipY=(emote.effects & chat::FlipY)!=0;
			params.cellAngle=effect.angle;params.cellPadding=effect.padding;params.cellShiftX=effect.shiftX;params.cellShiftY=effect.shiftY;
			params.cellExtentX=effect.extentX;params.cellExtentY=effect.extentY;params.cellAspect=effect.aspect;
            if(emote.effects & (chat::Wide|chat::Grow)){params.scale=float(emote.box.height()/effect.extentY/emote.media->cell)*row.bodyScale;params.cellExtentX=float(emote.box.width()/emote.box.height())*effect.extentY;}
            params.cellScaleX=effect.scaleX;params.cellScaleY=effect.scaleY;params.cellSlide=effect.slide;params.cellFilter=effect.filter;
            params.cellLayer=emote.layerRect;params.cellParty=effect.party;params.cellCursed=effect.cursed;params.cellHue=effect.hue;renderer.draw(emote.media->atlas,true,params);
		}
	}
}
}
