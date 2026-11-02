// SPDX-License-Identifier: GPL-2.0-or-later
#include "supporter-frame.hpp"
#include <algorithm>
#include <stdexcept>
namespace oto::sources {
namespace {
QRect cropPixels(const QRect &crop,const visual::ImagePtr &image) {
	const auto size=image->image.size();return {crop.x()*size.width()/1000,crop.y()*size.height()/1000,std::max(1,crop.width()*size.width()/1000),std::max(1,crop.height()*size.height()/1000)};
}
}
BarFramePtr prepareSupporterFrame(visual::VisualEngine &engine, const BarSettings &settings,
	const BarLabels &labels, const std::array<QString,6> &values, const BarFramePtr &previous,
	visual::MotionClock::time_point now, visual::ImagePtr background, visual::ImagePtr icons, std::optional<BarContext> context)
{
	if (settings.width < 600 || settings.width > 1920 || settings.height < 80 || settings.height > 240)
		throw std::invalid_argument("Invalid bar dimensions");
	if ((settings.motion != visual::Motion::None && settings.motion != visual::Motion::Fade && settings.motion != visual::Motion::SlideFade && settings.motion != visual::Motion::StyleChange) ||
		settings.durationMs < 100 || settings.durationMs > 5000) throw std::invalid_argument("Invalid bar motion");
	const auto labelLayout=supporterLabelLayout(settings);
	const bool languageChanged=previous && previous->settings.language!=settings.language;
	const bool labelGeometryChanged=previous && supporterLabelLayout(previous->settings).height!=labelLayout.height;
	const bool layout = !previous || labelGeometryChanged || previous->settings.width != settings.width ||
		previous->settings.height != settings.height || previous->settings.fields != settings.fields ||
		previous->labels.titles != labels.titles || previous->settings.style != settings.style || previous->background != background || previous->icons != icons;
	const bool fontChanged=previous && (previous->settings.valueFamily!=settings.valueFamily || previous->settings.valuePixelSize!=settings.valuePixelSize);
	const bool labelFontChanged=previous && (previous->settings.labelFamily!=settings.labelFamily || previous->settings.labelPixelSize!=settings.labelPixelSize);
	const bool baseChanged=layout || labelFontChanged;
	const bool motionChanged = previous && (previous->settings.motion != settings.motion || previous->settings.durationMs != settings.durationMs);
	// TEST changes labels, but it still demonstrates actual value changes. Geometry,
	// preset changes, showing a source and restoring a scene must never replay events.
	const bool compatible = previous && previous->settings.width == settings.width && previous->settings.height == settings.height &&
		previous->settings.fields == settings.fields && previous->settings.style == settings.style && !motionChanged && !fontChanged && !labelFontChanged && !languageChanged;
	const bool sourceChanged=context && previous && (!previous->context || previous->context->generation!=context->generation || !(previous->context->channel==context->channel));
	std::array<bool,6> fresh;fresh.fill(compatible);
	if(context){
		fresh.fill(false);
		if(compatible && previous->context && previous->context->generation==context->generation){
			if(context->preview)fresh.fill(true); // Explicit TEST still demonstrates changes.
			else if(!sourceChanged){
				for(std::size_t i=0;i<4;++i)fresh[i]=context->latestRevision[i]>previous->context->latestRevision[i];
				fresh[4]=context->delivered[2]>previous->context->delivered[2];
				fresh[5]=context->delivered[1]>previous->context->delivered[1] || context->delivered[3]>previous->context->delivered[3];
			}
		}
	}
	const bool contextChanged=context.has_value()!=(previous && previous->context.has_value()) || (context && previous && previous->context && !(*context==*previous->context));
	bool changed = baseChanged || motionChanged || fontChanged || contextChanged || languageChanged;
	for (size_t i=0;i<6;++i) if (settings.fields[i] && previous && previous->values[i] != values[i]) changed = true;
	if (!changed) return previous;
	auto result = previous ? std::make_shared<BarFrame>(*previous) : std::make_shared<BarFrame>();
	result->settings = settings; result->labels = labels; result->values = values;result->background=std::move(background);result->icons=std::move(icons);
	result->context=std::move(context);
	const int count = int(std::count(settings.fields.begin(),settings.fields.end(),true));
	if (!count) { result->base.reset(); result->layers = {};result->background.reset();result->icons.reset();result->hasScrolling=false; return result; }
	const auto &style=settings.style;
	// Leave enough room for one or six cards even at the narrowest supported size.
	const int padding=style.cards ? std::min(settings.width/5,std::max(style.padding,settings.height*style.cardInset/100)) : style.padding;
	const int cell = (settings.width - padding*2 - style.gap*(count-1))/count;
	const int cardTop=settings.height*style.cardTop/100,cardHeight=settings.height*style.cardHeight/100;
	const int iconSize=style.cards ? std::min(cardHeight*4/5,cell/3) : 0;
	const int textOffset=style.cards ? iconSize+4 : 0;
	const int textWidth=std::max(1,cell-textOffset-(style.cards?6:0));
	const int titleHeight=labelLayout.height;
	const int valueY=style.cards ? cardTop+titleHeight : (settings.labelPixelSize?12+titleHeight+2:36);
	const int valueHeight=style.cards ? cardHeight-titleHeight : settings.height-valueY-8;
	visual::Scene base{QSize(settings.width,settings.height),{}};
	if (baseChanged) {
		if(!style.cards)base.nodes.push_back(visual::RectangleNode{QRect(0,0,settings.width,settings.height),style.background});
		if(result->background)base.nodes.push_back(visual::ImageNode{QRect(0,0,settings.width,settings.height),result->background,cropPixels(style.backgroundCrop,result->background),style.backgroundEdge*result->background->image.width()/1000});
	}
	int column=0;
	for (size_t i=0;i<6;++i) {
		if (!settings.fields[i]) { result->layers[i] = {}; continue; }
		const int x = padding + column++ * (cell+style.gap);
		if (baseChanged) {
			if(style.cards) {
				base.nodes.push_back(visual::CardNode{QRect(x,cardTop,cell,cardHeight),style.cardBackground,style.cardEdges[i].isValid()?style.cardEdges[i]:style.cardEdge,style.cardRadius,style.cardGlow});
				if(result->icons) {const auto atlas=cropPixels(style.iconCrop,result->icons);const int left=atlas.x()+int(i)*atlas.width()/6,right=atlas.x()+int(i+1)*atlas.width()/6;
					base.nodes.push_back(visual::ImageNode{QRect(x+2,cardTop+(cardHeight-iconSize)/2,iconSize,iconSize),result->icons,QRect(left,atlas.y(),right-left,atlas.height())});}
			}
			visual::TextNode title; title.box=QRect(x+textOffset,(style.cards?cardTop:12)+supporterLabelTopInset,textWidth,titleHeight-supporterLabelTopInset); title.text=labels.titles[i]; title.pixelSize=labelLayout.pixelSize;
			title.minimumPixelSize=std::min(12,style.titleSize);
			title.minimumPixelSize=std::min(title.minimumPixelSize,title.pixelSize);title.bold=style.cards;
			title.family=settings.labelFamily.isEmpty()?style.family:settings.labelFamily;title.color=style.titleColors[i].isValid()?style.titleColors[i]:style.title;
			// Longer translations fit the inherited style; explicit user sizes remain exact.
			title.overflow=settings.labelPixelSize?visual::Overflow::Ellipsis:visual::Overflow::AutoFit;
			if(!settings.labelPixelSize)title.minimumPixelSize=std::min(8,title.pixelSize);base.nodes.push_back(title);
		}
		if (layout || (fontChanged && i<4) || previous->values[i] != values[i]) {
			visual::TextNode value; value.box=QRect(0,0,textWidth,valueHeight); value.text=values[i];
			value.pixelSize=supporterValuePixelSize(settings,valueHeight,i<4);value.minimumPixelSize=std::min(12,value.pixelSize);value.family=i<4 && !settings.valueFamily.isEmpty()?settings.valueFamily:style.family;value.color=style.value; value.overflow=i<4?visual::Overflow::Clip:visual::Overflow::Ellipsis; value.vertical=visual::Vertical::Center;value.bold=style.cards;
			const bool liveChange = fresh[i] && previous->values[i] != values[i];
			const bool animatedChange = settings.motion == visual::Motion::StyleChange && liveChange;
			result->layers[i] = {i<4?engine.prepareTextStrip(value):engine.prepare({value.box.size(),{value}}),x+textOffset,valueY,
				settings.motion != visual::Motion::None && liveChange ? now : visual::MotionClock::time_point{},
				animatedChange ? previous->layers[i].frame : visual::FramePtr{},
				QRect(x,style.cards?cardTop:12,cell,style.cards?cardHeight:settings.height-24)};
			auto &layer=result->layers[i];layer.viewportWidth=textWidth;layer.scrollDistance=std::max(0,layer.frame->image.width()-textWidth);
			if(layer.scrollDistance)layer.scrollStarted=now+std::chrono::milliseconds(layer.started==visual::MotionClock::time_point{}?0:settings.durationMs);
			if(animatedChange && previous->layers[i].scrollDistance)layer.previousOffsetX=visual::samplePingPong(std::chrono::duration<double,std::milli>(now-previous->layers[i].scrollStarted).count(),previous->layers[i].scrollDistance);
		} else if (motionChanged || labelFontChanged || fontChanged || sourceChanged || languageChanged) { result->layers[i].started = {}; result->layers[i].previousValue.reset();result->layers[i].previousOffsetX=0;if(result->layers[i].scrollDistance)result->layers[i].scrollStarted=now; }
	}
	if (baseChanged) result->base=engine.prepare(base);
	result->hasScrolling=std::any_of(result->layers.begin(),result->layers.end(),[](const auto &layer){return layer.frame && layer.scrollDistance>0;});
	return result;
}
}
