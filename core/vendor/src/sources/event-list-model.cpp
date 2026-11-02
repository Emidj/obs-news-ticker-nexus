// SPDX-License-Identifier: GPL-2.0-or-later
#include "event-list-model.hpp"
#include <QTransform>
#include <QFontMetricsF>
#include <algorithm>
#include <limits>
#include <stdexcept>
namespace oto::sources {
namespace {
visual::TextNode text(QRect box, QString value, int size, const QString &family, QColor color=QColor(222,240,255), bool bold=false)
{
	// Fit height only: names retain their horizontal ping-pong viewport. All
	// three text columns use the full cell height and the same vertical centre.
	QFont font(family);font.setBold(bold);font.setPixelSize(size);
	while(size>8 && QFontMetricsF(font).height()>box.height()){font.setPixelSize(--size);}
	visual::TextNode node;node.box=box;node.text=std::move(value);node.pixelSize=size;node.minimumPixelSize=8;node.family=family;
	 node.color=color;node.bold=bold;node.overflow=visual::Overflow::Ellipsis;node.vertical=visual::Vertical::InkCenter;return node;
}
QPainterPath icon(events::EventType type)
{
	QPainterPath p;
	switch(type) {
	case events::EventType::Follow:
		p.moveTo(16,29);p.cubicTo(10,24,2,18,2,10);p.cubicTo(2,1,12,1,16,8);p.cubicTo(20,1,30,1,30,10);p.cubicTo(30,18,22,24,16,29);p.closeSubpath();break;
	case events::EventType::Sub:
		p.moveTo(3,8);p.lineTo(9,15);p.lineTo(16,4);p.lineTo(23,15);p.lineTo(29,8);p.lineTo(26,26);p.lineTo(6,26);p.closeSubpath();p.moveTo(6,22);p.lineTo(26,22);break;
	case events::EventType::Cheer:
		p.moveTo(16,2);p.lineTo(29,16);p.lineTo(16,30);p.lineTo(3,16);p.closeSubpath();p.moveTo(16,2);p.lineTo(12,16);p.lineTo(16,30);p.moveTo(3,16);p.lineTo(29,16);break;
	case events::EventType::Gift:
		p.addRect(5,14,22,15);p.addRect(3,10,26,5);p.moveTo(16,10);p.lineTo(16,29);p.moveTo(16,10);p.cubicTo(0,10,4,-3,12,4);p.lineTo(16,10);p.cubicTo(32,10,28,-3,20,4);p.closeSubpath();break;
	case events::EventType::Raid:
		p.addEllipse(12,3,8,8);p.addEllipse(2,6,7,7);p.addEllipse(23,6,7,7);
		p.moveTo(8,28);p.lineTo(9,20);p.cubicTo(9,12,23,12,23,20);p.lineTo(24,28);p.closeSubpath();
		p.moveTo(7,27);p.lineTo(1,27);p.lineTo(1,20);p.cubicTo(1,15,5,13,8,16);p.moveTo(25,27);p.lineTo(31,27);p.lineTo(31,20);p.cubicTo(31,15,27,13,24,16);break;
	default:break;
	}return p;
}
}
ListView eventListView(const state::SnapshotPtr &snapshot,const ListSettings &settings,const ListLabels &labels,const state::ChannelKey &channel,bool previewTail)
{
	ListView result;result.channel=channel;if(!snapshot)return result;result.generation=snapshot->generation;
	const auto found=snapshot->channels.find(channel);if(found==snapshot->channels.end())return result;
	for(const auto &entry:found->second->activity) {
		const auto type=events::typeOf(entry.event->data().payload);const auto index=state::slot(type);
		if(index>=settings.types.size() || !settings.types[index])continue;
		ListItem row;row.event=entry.event;row.historical=entry.delivery==0;row.name=entry.displayName?QString::fromUtf8(entry.displayName->c_str()):labels.anonymous;
		if(channel.origin==events::EventOrigin::Preview)row.name.prepend(QStringLiteral("TEST · "));
		switch(type) {
		case events::EventType::Follow:row.badge=labels.badges[0];break;
		case events::EventType::Sub:{row.badge=labels.badges[1];const auto &sub=std::get<events::SubEvent>(row.event->data().payload);row.detail=sub.countsTowardTotals?labels.subscription:labels.months.arg(sub.totalMonths);break;}
		case events::EventType::Cheer:row.badge=labels.badges[2];row.detail=labels.bits.arg(qulonglong(std::get<events::CheerEvent>(row.event->data().payload).bits));break;
		case events::EventType::Gift:row.badge=labels.badges[3];row.detail=labels.gifts.arg(qulonglong(std::get<events::GiftEvent>(row.event->data().payload).count));break;
		case events::EventType::Raid:row.badge=labels.badges[4];row.detail=labels.viewers.arg(qulonglong(std::get<events::RaidEvent>(row.event->data().payload).viewers));break;
		default:break;
		}
		result.items.push_back(std::move(row));if(result.items.size()>=std::size_t(settings.rows+(previewTail?1:0)))break;
	}return result;
}
float listRowY(const ListRow &row,visual::MotionClock::time_point now,int durationMs) noexcept
{
	if(row.moved==visual::MotionClock::time_point{})return row.y;
	const auto t=std::chrono::duration<double,std::milli>(now-row.moved).count()/listShiftDuration(durationMs);
	return row.fromY+(row.y-row.fromY)*visual::smooth(float(t));
}
ListEntrySample sampleListEntry(double ms,ListEntrance entrance,int width,int rowHeight,int durationMs) noexcept
{
	if(!std::isfinite(ms) || durationMs<=0 || ms>=durationMs)return {};
	// Keep the established choreography; scale every phase by the total duration.
	const float t=float(std::max(0.0,ms)*6000/durationMs);
	const float card=visual::smooth(t/500),letters=visual::smooth((t-500)/500);
	const float glow=t<200?visual::smooth(t/200):t<1000?1.0f:t<4000?.28f+.05f*std::sin(t*.008f):.28f*(1-visual::smooth((t-4000)/2000));
	ListEntrySample result{card,letters,-10*(1-card),glow,t<1000?visual::smooth(t/100)*(1-visual::smooth((t-500)/500)):0,t<1000?std::sin(std::clamp((t-200)/800,0.0f,1.0f)*3.14159265f):0,false};
	result.progress=t/6000;
	if(entrance==ListEntrance::Left || entrance==ListEntrance::Right)
		result.offsetX=(entrance==ListEntrance::Left?-1:1)*float(std::clamp(width,420,1200)+4)*(1-card);
	else if(entrance==ListEntrance::Top){result.offsetX=0;result.offsetY=-float(std::clamp(rowHeight,48,96)+12)*(1-card);}
	return result;
}
ListFramePtr prepareEventListFrame(visual::VisualEngine &engine,const ListSettings &settings,const ListLabels &labels,const ListView &view,const ListFramePtr &previous,visual::MotionClock::time_point now,bool demoEntrance,visual::ImagePtr background)
{
	if(settings.width<420 || settings.width>1200 || settings.rows<1 || settings.rows>10 || settings.rowHeight<48 || settings.rowHeight>96 || settings.durationMs<1000 || settings.durationMs>12000 || int(settings.entrance)<0 || int(settings.entrance)>3 || settings.family.isEmpty() || settings.family.size()>128 || view.items.size()>std::size_t(settings.rows+(demoEntrance?1:0)))throw std::invalid_argument("Invalid Event List settings");
	if(settings.backgroundOpacity<0 || settings.backgroundOpacity>100)throw std::invalid_argument("Invalid Event List background opacity");
	if((settings.namePixelSize && (settings.namePixelSize<8 || settings.namePixelSize>128)) || (settings.labelPixelSize && (settings.labelPixelSize<8 || settings.labelPixelSize>128)))throw std::invalid_argument("Invalid Event List font size");
	if(!view.items.empty() && !settings.style.backgroundImage.isEmpty() && !background)throw std::invalid_argument("Event List artwork missing");
	if(demoEntrance && !view.items.empty()) {
		auto prior=view;prior.items.erase(prior.items.begin());
		const auto seed=prepareEventListFrame(engine,settings,labels,prior,{},now,false,background);
		auto incoming=view;if(incoming.items.size()>std::size_t(settings.rows))incoming.items.resize(settings.rows);
		return prepareEventListFrame(engine,settings,labels,incoming,seed,now,false,std::move(background));
	}
	const bool imported=previous && std::any_of(view.items.begin(),view.items.end(),[&](const auto &item){return item.historical && std::none_of(previous->view.items.begin(),previous->view.items.end(),[&](const auto &old){return old.event==item.event;});});
	const bool compatible=!imported && previous && previous->settings==settings && previous->labels==labels && previous->view.generation==view.generation && previous->view.channel==view.channel && (previous->background==background || previous->view.items.empty());
	bool same=compatible && previous->view.items.size()==view.items.size();
	if(same)for(std::size_t i=0;i<view.items.size();++i)if(previous->view.items[i].event!=view.items[i].event){same=false;break;}
	if(same && !demoEntrance)return previous;
	auto next=std::make_shared<ListFrame>();next->settings=settings;next->labels=labels;next->view=view;
	next->background=std::move(background);
	const auto &style=settings.style;
	if(view.items.empty()){
		auto fill=style.cardBackground;fill.setAlpha(fill.alpha()*settings.backgroundOpacity/100);
		auto notice=text(QRect(24,0,settings.width-48,settings.rowHeight),labels.empty,16,settings.family,style.value);notice.horizontal=visual::Horizontal::Center;
		next->emptyState=engine.prepare({QSize(settings.width,settings.rowHeight),{visual::CardNode{QRect(12,4,settings.width-24,settings.rowHeight-8),fill,style.cardEdge,style.cardRadius,style.cardGlow},notice}});
	}
	const auto &labelFamily=settings.labelFamily.isEmpty()?style.family:settings.labelFamily;
	const int size=settings.namePixelSize?settings.namePixelSize:std::clamp(settings.rowHeight*style.valueSize/64,14,24),iconSize=settings.rowHeight*58/100;
	const int badgeSize=settings.labelPixelSize?settings.labelPixelSize:std::clamp(style.titleSize,12,std::clamp(settings.rowHeight*style.valueSize/64,14,24));
	const int detailSize=settings.labelPixelSize?settings.labelPixelSize:std::max(12,std::clamp(settings.rowHeight*style.valueSize/64,14,24)-2);
	const int nameX=next->background?settings.width*style.nameStart/1000:20+iconSize+76;
	for(std::size_t i=0;i<view.items.size();++i) {
		const auto &item=view.items[i];const ListRow *old=nullptr;
		// Reserve the rightmost column only when there is a quantity; Follow uses the free space.
		const int detailWidth=item.detail.isEmpty()?0:settings.width/4;
		const int detailX=settings.width-28-detailWidth;
		const int nameWidth=detailX-nameX-(detailWidth?12:0);
		if(nameWidth<1)throw std::invalid_argument("Event List name viewport empty");
		if(compatible)for(const auto &row:previous->rows)if(row.item.event==item.event){old=&row;break;}
		ListRow row;if(old)row=*old;row.item=item;row.y=float(12+i*(settings.rowHeight+8));row.nameX=nameX;row.nameWidth=nameWidth;
		if(!old) {
			const auto type=events::typeOf(item.event->data().payload);
			const auto index=state::slot(type);if(index>=5)throw std::invalid_argument("Unsupported Event List row type");row.accent=style.titleColors[index].isValid()?style.titleColors[index]:style.title;
			visual::Scene base{QSize(settings.width,settings.rowHeight),{}};
			if(next->background) {
				const auto &image=next->background->image;const int band=int(index)%style.backgroundBands;
				const int top=image.height()*band/style.backgroundBands,bottom=image.height()*(band+1)/style.backgroundBands;
				const auto &crop=style.rowCrops[index];const auto source=crop.isValid()?QRect(image.width()*crop.x()/1000,image.height()*crop.y()/1000,std::max(1,image.width()*crop.width()/1000),std::max(1,image.height()*crop.height()/1000)):QRect(0,top,image.width(),bottom-top);
				const auto &fill=style.backgroundBox;
				base.nodes.push_back(visual::ImageNode{QRect(0,0,settings.width,settings.rowHeight),next->background,source,0,QRect(settings.width*fill.x()/1000,settings.rowHeight*fill.y()/1000,settings.width*fill.width()/1000,settings.rowHeight*fill.height()/1000),settings.backgroundOpacity});
				const auto &b=style.badgeBox;const int badgeHeight=settings.rowHeight*b.height()/1000;auto badge=text(QRect(settings.width*b.x()/1000,(settings.rowHeight-badgeHeight)/2,settings.width*b.width()/1000,badgeHeight),item.badge,badgeSize,labelFamily,settings.inheritLabelColor?(style.badgeAccent?row.accent:QColor(8,5,20)):settings.labelColor,true);badge.horizontal=visual::Horizontal::Center;base.nodes.push_back(badge);
			} else {
				auto fill=style.cardBackground;fill.setAlpha(fill.alpha()*settings.backgroundOpacity/100);
				base.nodes.push_back(visual::CardNode{QRect(12,4,settings.width-24,settings.rowHeight-8),fill,style.cardEdge,style.cardRadius,style.cardGlow});
				QTransform transform;transform.translate(18,(settings.rowHeight-iconSize)/2);transform.scale(double(iconSize)/32,double(iconSize)/32);
				base.nodes.push_back(visual::PathNode{transform.map(icon(type)),row.accent,QColor(Qt::transparent),1.7f});
				base.nodes.push_back(text(QRect(24+iconSize,0,68,settings.rowHeight),item.badge,badgeSize,labelFamily,settings.inheritLabelColor?row.accent:settings.labelColor,true));
			}
			if(detailWidth) {
				auto detail=text(QRect(detailX,0,detailWidth,settings.rowHeight),item.detail,detailSize,labelFamily,settings.inheritLabelColor?row.accent:settings.labelColor,next->background!=nullptr);
				detail.horizontal=visual::Horizontal::Right;base.nodes.push_back(std::move(detail));
			}
			row.base=engine.prepare(base);
			auto name=text(QRect(0,0,nameWidth,settings.rowHeight),item.name,size,settings.family,settings.inheritNameColor?style.value:settings.nameColor,true);name.overflow=visual::Overflow::Clip;
			row.name=engine.prepareTextStrip(name);row.scrollDistance=std::max(0,row.name->image.width()-nameWidth);
			const bool enter=settings.animate && ((compatible && view.items.size()>0) || (demoEntrance && i==0));
			row.entered=enter?now:visual::MotionClock::time_point{};row.scrollStarted=now+(enter?std::chrono::duration_cast<visual::MotionClock::duration>(std::chrono::duration<double,std::milli>(settings.durationMs/6.0)):visual::MotionClock::duration{});row.fromY=row.y;
		} else if(old->y!=row.y) {row.fromY=listRowY(*old,now,settings.durationMs);row.moved=settings.animate?now:visual::MotionClock::time_point{};}
		next->hasScrolling=next->hasScrolling || row.scrollDistance>0;next->rows.push_back(std::move(row));
	}
	if(compatible && settings.animate) {
		const auto retained=[&](const ListRow &row){return std::any_of(next->rows.begin(),next->rows.end(),[&](const ListRow &current){return current.item.event==row.item.event;});};
		const auto incoming=std::count_if(next->rows.begin(),next->rows.end(),[&](const ListRow &row){return row.entered==now;});
		if(incoming) {
			const auto retainExit=[&](const ListRow &old){
				if(next->exiting.size()>=std::size_t(settings.rows))return;
				auto row=old;row.fromY=listRowY(old,now,settings.durationMs);row.y=old.y+float(incoming*(settings.rowHeight+8));row.moved=now;row.entered={};if(row.exitStarted==visual::MotionClock::time_point{})row.exitStarted=now;
				next->hasScrolling=next->hasScrolling || row.scrollDistance>0;next->exiting.push_back(std::move(row));
			};
			for(const auto &row:previous->rows)if(!retained(row))retainExit(row);
			for(const auto &row:previous->exiting)if(std::chrono::duration<double,std::milli>(now-row.exitStarted).count()<listShiftDuration(settings.durationMs))retainExit(row);
		} else next->exiting=previous->exiting;
	}
	return next;
}
}
