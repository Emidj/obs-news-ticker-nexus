#include "localization.hpp"
// SPDX-License-Identifier: GPL-2.0-or-later
#include "shoutout-model.hpp"
#include <QLocale>
#include <QPainter>
#include <stdexcept>
namespace oto::sources {
ShoutoutSample sampleShoutout(double elapsed,AlertTimes times) noexcept
{
	if(!std::isfinite(elapsed) || elapsed<0 || times.entry<100 || times.entry>10000 || times.hold<0 || times.hold>60000 || times.exit<100 || times.exit>10000 || elapsed>=times.total())return {};
	const float progress=float(elapsed<times.entry?elapsed/times.entry:elapsed<times.entry+times.hold?1:(times.total()-elapsed)/times.exit);
	const float portrait=visual::smooth(std::clamp(progress/0.65f,0.f,1.f));const float data=visual::smooth(std::clamp((progress-0.15f)/0.85f,0.f,1.f));
	const float opacity=elapsed<times.entry+times.hold?1:visual::smooth(progress);
	return {opacity,portrait,data,1+0.35f*(1-data),false};
}
bool ShoutoutCursor::synchronize(const state::SnapshotPtr &snapshot,const state::ChannelKey &channel,bool preview)
{
	if(initialized_ && snapshot && generation_==snapshot->generation && channel_==channel)return false;
	channel_=channel;generation_=snapshot?snapshot->generation:0;delivery_=0;initialized_=bool(snapshot);
	if(snapshot && !preview){const auto found=snapshot->channels.find(channel);if(found!=snapshot->channels.end())delivery_=found->second->delivered[state::slot(events::EventType::Shoutout)];}return true;
}
const state::LatestEvent *ShoutoutCursor::next(const state::SnapshotPtr &snapshot) noexcept
{
	if(!initialized_ || !snapshot || snapshot->generation!=generation_)return nullptr;const auto found=snapshot->channels.find(channel_);if(found==snapshot->channels.end())return nullptr;
	for(const auto &entry:found->second->shoutouts)if(entry.delivery>delivery_){delivery_=entry.delivery;return &entry;}return nullptr;
}
ShoutoutFramePtr prepareShoutoutFrame(visual::VisualEngine &engine,const ShoutoutSettings &settings,const presets::ShoutoutStyle &style,
	const state::LatestEvent &entry,visual::ImagePtr profile,visual::MotionClock::time_point started,visual::ImagePtr artwork)
{
	if(!entry.event || !entry.displayName || events::typeOf(entry.event->data().payload)!=events::EventType::Shoutout)throw std::invalid_argument("Invalid shoutout card");
	auto frame=std::make_shared<ShoutoutFrame>();frame->settings=settings;frame->style=style;frame->event=entry;frame->profile=std::move(profile);frame->started=started;
	const auto names=settings.nameFamily.isEmpty()?style.family:settings.nameFamily;const auto labels=settings.labelFamily.isEmpty()?style.family:settings.labelFamily;
	const auto nameColor=settings.inheritNameColor?style.name:settings.nameColor;const auto detailsColor=settings.inheritDetailsColor?style.details:settings.detailsColor;
	visual::Scene scene{{800,800},{}};if(artwork)scene.nodes.push_back(visual::ImageNode{style.assetBox,std::move(artwork)});else scene.nodes.push_back(visual::CardNode{{20,265,760,270},QColor(10,10,25),QColor(130,80,230),18,4});frame->base=engine.prepare(scene);
	const auto size=style.portraitBox.size();visual::Scene portrait{size,{}};
	if(frame->profile){auto photo=std::make_shared<visual::ImageData>();const auto &image=frame->profile->image;const int side=std::min(image.width(),image.height());
		photo->image=QImage(size,QImage::Format_RGBA8888_Premultiplied);photo->image.fill(Qt::transparent);QPainter painter(&photo->image);painter.setRenderHint(QPainter::Antialiasing);painter.setRenderHint(QPainter::SmoothPixmapTransform);
		QPainterPath clip;if(style.circular)clip.addEllipse(QRectF(QPointF{},QSizeF(size)));else if(style.hexagonal){const float w=size.width(),h=size.height();clip.moveTo(w*.25,0);clip.lineTo(w*.75,0);clip.lineTo(w,h*.5);clip.lineTo(w*.75,h);clip.lineTo(w*.25,h);clip.lineTo(0,h*.5);clip.closeSubpath();}else clip.addRoundedRect(QRectF(QPointF{},QSizeF(size)),6,6);
		painter.setClipPath(clip);painter.drawImage(QRect(QPoint{},size),image,QRect((image.width()-side)/2,(image.height()-side)/2,side,side));painter.end();portrait.nodes.push_back(visual::ImageNode{QRect(QPoint{},size),photo});
	}else{visual::TextNode initial;initial.box={QPoint{},size};initial.text=QString::fromUtf8(entry.displayName->c_str()).left(1).toUpper();initial.family=names;initial.color=nameColor;initial.pixelSize=80;initial.horizontal=visual::Horizontal::Center;initial.vertical=visual::Vertical::Center;portrait.nodes.push_back(initial);}
	frame->portrait=engine.prepare(portrait);
	visual::TextNode name;name.box={QPoint{},style.nameBox.size()};name.text=QString::fromUtf8(entry.displayName->c_str());name.family=names;name.pixelSize=std::min(38,style.nameBox.height()-4);name.minimumPixelSize=std::min(24,name.pixelSize);name.bold=true;name.color=nameColor;name.horizontal=visual::Horizontal::Left;name.vertical=visual::Vertical::Center;name.overflow=visual::Overflow::Clip;
	visual::TextLayoutEngine layout;auto fitted=name;fitted.minimumPixelSize=std::min(24,name.pixelSize);fitted.overflow=visual::Overflow::AutoFit;name.pixelSize=layout.prepare(fitted).metrics.pixelSize;
	frame->namePixelSize=name.pixelSize;frame->name=engine.prepareTextStrip(name);frame->scrollDistance=std::max(0,frame->name->image.width()-style.nameBox.width());
	visual::Scene details{{800,800},{}};const auto &data=std::get<events::ShoutoutEvent>(entry.event->data().payload);
	visual::TextNode text=name;text.family=labels;text.pixelSize=std::min(24,style.categoryBox.height()-2);text.bold=false;text.overflow=visual::Overflow::AutoFit;text.minimumPixelSize=12;text.color=detailsColor;text.box=style.categoryBox;text.text=data.category.empty()?i18n::qtext("Shoutout.CategoryUnavailable"):QString::fromStdString(data.category);details.nodes.push_back(text);
	text.box=style.followersBox;text.text=data.followers?i18n::qtext("Shoutout.Followers").arg(QLocale(nexus::localization::systemLanguage()).toString(qulonglong(*data.followers))):i18n::qtext("Shoutout.FollowersUnavailable");details.nodes.push_back(text);frame->details=engine.prepare(details);return frame;
}
}
