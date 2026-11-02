#include "localization.hpp"
// SPDX-License-Identifier: GPL-2.0-or-later
#include "alert-model.hpp"
#include <stdexcept>
namespace oto::sources {
namespace {
QString localizedAlertText(const QString &value){
 static const std::pair<const char *,const char *> names[]={
 {"NEW","New"},{"GIFTED","Gifted"},{"WELCOME","Welcome"},{"FOLLOWER","Follower"},{"SUBSCRIPTION","Subscription"},{"SUB","Sub"},{"SUBS PACK","Pack"},{"RAID","Raid"},{"RAIDED","Raided"},{"RAIDED IN","Raid"},{"IN","In"},{"CHEER","Cheer"}};
 for(const auto &name:names)if(value==QString::fromLatin1(name.first))return i18n::qtext((QByteArray("Alert.Text")+name.second).constData());
 return value;
}
}

AlertSample sampleAlert(double elapsedMs,AlertTimes times) noexcept
{
	if(!std::isfinite(elapsedMs) || times.entry<100 || times.entry>10000 || times.hold<0 || times.hold>60000 || times.exit<100 || times.exit>10000 || elapsedMs<0 || elapsedMs>=times.total())return {};
	AlertSample out;out.finished=false;out.visualMs=elapsedMs;
	if(elapsedMs<times.entry){
		const float t=float(elapsedMs/times.entry);
		out.portrait=visual::smooth(t/.24f);out.burst=visual::smooth((t-.14f)/.40f);out.title=visual::smooth((t-.45f)/.34f);out.name=visual::smooth((t-.72f)/.28f);
		out.effects=std::max(0.0f,std::sin(std::min(1.0f,t/.95f)*3.14159265f));out.scale=.60f+.40f*visual::smooth(t/.45f)+.045f*std::sin(t*3.14159265f);out.offsetX=-220*(1-visual::smooth(t/.48f));
	}else if(elapsedMs<times.entry+times.hold){
		const float beat=float(elapsedMs-times.entry)*.005f;
		out.portrait=out.title=out.name=1;out.burst=.88f+.07f*std::sin(beat);out.effects=.24f+.08f*std::sin(beat);out.scale=1+.006f*std::sin(beat*.7f);
	}else{
		const float t=float((elapsedMs-times.entry-times.hold)/times.exit);out.exitProgress=t;
		// Exact entrance choreography in reverse, retimed to the exit duration.
		const double reversed=std::clamp((times.total()-elapsedMs)*times.entry/times.exit,0.0,double(times.entry)-.000001);
		out=sampleAlert(reversed,times);out.exitProgress=t;
		// Reverse from the displayed hold frame, without resetting shader/clip phase.
		out.visualMs=times.entry+times.hold-(elapsedMs-times.entry-times.hold)*times.entry/times.exit;
		if(times.hold>0){const float beat=times.hold*.005f,blend=visual::smooth(t/.12f);
			out.burst=(.88f+.07f*std::sin(beat))*(1-blend)+out.burst*blend;
			out.effects=(.24f+.08f*std::sin(beat))*(1-blend)+out.effects*blend;
			out.scale=(1+.006f*std::sin(beat*.7f))*(1-blend)+out.scale*blend;
		}
	}return out;
}
int alertVariant(const events::EventPayload &payload) noexcept
{
	switch(events::typeOf(payload)){
	case events::EventType::Follow:return 0;
	case events::EventType::Sub:return 1;
	case events::EventType::Gift:return std::get<events::GiftEvent>(payload).count>1?3:2;
	case events::EventType::Raid:return 4;
	case events::EventType::Cheer:return 5;
	default:return -1;
	}
}
bool AlertCursor::synchronize(const state::SnapshotPtr &snapshot,const state::ChannelKey &channel,bool history)
{
	if(initialized_ && snapshot && generation_==snapshot->generation && channel_==channel)return false;
	channel_=channel;generation_=snapshot?snapshot->generation:0;delivery_=0;initialized_=bool(snapshot);if(!history)skip(snapshot);return true;
}
void AlertCursor::skip(const state::SnapshotPtr &snapshot) noexcept
{
	if(!snapshot)return;const auto found=snapshot->channels.find(channel_);if(found!=snapshot->channels.end())delivery_=found->second->activityDelivery;
}
const state::LatestEvent *AlertCursor::next(const state::SnapshotPtr &snapshot,const std::array<bool,5> &types) noexcept
{
	if(!initialized_ || !snapshot || generation_!=snapshot->generation)return nullptr;
	const auto found=snapshot->channels.find(channel_);if(found==snapshot->channels.end())return nullptr;
	const state::LatestEvent *selected=nullptr;
	for(const auto &entry:found->second->activity){const auto slot=state::slot(events::typeOf(entry.event->data().payload));if(entry.delivery>delivery_ && slot<types.size() && types[slot] && (!selected || entry.delivery<selected->delivery))selected=&entry;}
	if(selected)delivery_=selected->delivery;else skip(snapshot);return selected;
}
namespace {
visual::TextNode text(QSize size,QString value,int pixels,const QString &family,QColor color,bool decorative=false)
{
	visual::TextNode node;node.box=QRect(QPoint(12,8),size-QSize(24,16));node.text=std::move(value);node.family=family;node.pixelSize=pixels;node.minimumPixelSize=24;
	 node.bold=true;node.color=color;node.overflow=visual::Overflow::AutoFit;node.vertical=visual::Vertical::Center;node.horizontal=visual::Horizontal::Center;
	if(decorative){node.italic=true;node.outline=QColor(7,3,22);node.outlineWidth=10;node.gradientBottom=color.darker(140);}return node;
}
visual::FramePtr crop(visual::VisualEngine &engine,const visual::ImagePtr &atlas,QRect crop,QSize size)
{
	const auto imageSize=atlas->image.size();QRect pixels(qRound(crop.x()*imageSize.width()/1000.0),qRound(crop.y()*imageSize.height()/1000.0),qRound(crop.width()*imageSize.width()/1000.0),qRound(crop.height()*imageSize.height()/1000.0));
	return engine.prepare({size,{visual::ImageNode{QRect(QPoint(0,0),size),atlas,pixels}}});
}
}
QRect alertIconCell(double elapsedMs) noexcept
{
	if(!std::isfinite(elapsedMs) || elapsedMs<0)return QRect(0,0,128,128);
	const int index=int(std::floor(std::fmod(elapsedMs,2000.0)*.024));return QRect((index%8)*128,(index/8)*128,128,128);
}
AlertFramePtr prepareAlertFrame(visual::VisualEngine &engine,const AlertSettings &settings,const presets::AlertStyle &style,const state::LatestEvent &event,visual::ImagePtr atlas,visual::ImagePtr portrait,visual::MotionClock::time_point started,visual::ImagePtr iconClip)
{
	const int variant=event.event?alertVariant(event.event->data().payload):-1;
	if(variant<0 || (!atlas && !iconClip) || style.energyPattern<0 || style.energyPattern>9 || settings.times.entry<100 || settings.times.entry>10000 || settings.times.hold<0 || settings.times.hold>60000 || settings.times.exit<100 || settings.times.exit>10000)throw std::invalid_argument("Invalid alert frame");
	auto out=std::make_shared<AlertFrame>();out->event=event.event;out->displayName=event.displayName;out->settings=settings;out->started=started;out->variant=variant;out->accent=style.colors[variant];out->particles=style.particles;out->energyPattern=style.energyPattern;
	if(iconClip){
		if(iconClip->image.size()!=QSize(1024,768))throw std::invalid_argument("Invalid alert animation dimensions");
		out->emblem=engine.prepare({{1024,768},{visual::ImageNode{QRect(0,0,1024,768),iconClip,{}}}});out->animatedEmblem=true;
	}else out->emblem=crop(engine,atlas,style.iconCrops[variant],{200,200});
	visual::Scene image{{290,290},{}};
	out->profileImage=portrait;
	if(portrait){const auto &img=portrait->image;const int edge=std::min(img.width(),img.height());image.nodes.push_back(visual::ImageNode{QRect(0,0,290,290),portrait,QRect((img.width()-edge)/2,(img.height()-edge)/2,edge,edge),0,{},100,style.energyPattern!=2});}
	else{QPainterPath disc;if(style.energyPattern==2)disc.addRoundedRect(4,4,282,282,8,8);else disc.addEllipse(4,4,282,282);image.nodes.push_back(visual::PathNode{disc,style.energyPattern==8?QColor(209,138,42,200):style.energyPattern==7?QColor(180,210,233,180):style.energyPattern==6?QColor(255,222,24,210):style.energyPattern==5?QColor(255,100,220,160):style.energyPattern==4?QColor(255,52,73,160):QColor(63,202,255,160),style.energyPattern==8?QColor(20,15,10,235):style.energyPattern==7?QColor(6,12,21,225):style.energyPattern==6?QColor(8,8,9,245):style.energyPattern==5?QColor(34,6,46,225):style.energyPattern==4?QColor(22,6,12,225):QColor(10,6,31,225),2});QPainterPath avatar;avatar.addEllipse(104,63,82,82);avatar.moveTo(57,250);avatar.cubicTo(55,139,235,139,233,250);avatar.closeSubpath();image.nodes.push_back(visual::PathNode{avatar,out->accent.lighter(145),out->accent.darker(240),3});}
	out->portrait=engine.prepare(image);
	const auto labels=settings.labelFamily.isEmpty()?style.family:settings.labelFamily;
	const auto names=settings.nameFamily.isEmpty()?style.family:settings.nameFamily;
	auto caption=text({600,80},localizedAlertText(style.captions[variant]),44,labels,style.captionColor.isValid()?style.captionColor:QColor(Qt::white),true);
	auto heading=text({660,130},localizedAlertText(style.headlines[variant]),88,labels,style.headlineColor.isValid()?style.headlineColor:out->accent,true);
	if(style.energyPattern==1){caption.outlineWidth=heading.outlineWidth=0;caption.gradientBottom=heading.gradientBottom=Qt::transparent;}
	if(style.energyPattern==9){caption=text({600,80},localizedAlertText(style.captions[variant]),34,labels,style.captionColor);heading=text({660,130},localizedAlertText(style.headlines[variant]),70,labels,style.headlineColor);}
	if(style.energyPattern==8){caption=text({600,80},localizedAlertText(style.captions[variant]),36,labels,style.captionColor);heading=text({660,130},localizedAlertText(style.headlines[variant]),72,labels,style.headlineColor);}
	if(style.energyPattern==7){caption=text({600,80},localizedAlertText(style.captions[variant]),30,labels,style.captionColor);heading=text({660,130},localizedAlertText(style.headlines[variant]),64,labels,style.headlineColor);}
	if(style.energyPattern==6){caption=text({600,80},localizedAlertText(style.captions[variant]),38,labels,Qt::white,true);heading=text({660,130},localizedAlertText(style.headlines[variant]),78,labels,out->accent,true);}
	if(style.energyPattern==5){caption=text({600,80},localizedAlertText(style.captions[variant]),36,labels,Qt::white);heading=text({660,130},localizedAlertText(style.headlines[variant]),70,labels,QColor(255,221,249));}
	if(style.energyPattern==4){caption=text({600,80},localizedAlertText(style.captions[variant]),36,labels,Qt::white);heading=text({660,130},localizedAlertText(style.headlines[variant]),70,labels,out->accent);}
	if(style.energyPattern==3){caption=text({600,80},localizedAlertText(style.captions[variant]),36,labels,Qt::white);heading=text({660,130},localizedAlertText(style.headlines[variant]),70,labels,out->accent);}
	if(style.energyPattern==2){caption=text({540,80},localizedAlertText(style.captions[variant]),36,labels,Qt::white);heading=text({540,110},localizedAlertText(style.headlines[variant]),64,labels,out->accent);}
	out->caption=engine.prepare({style.energyPattern==2?QSize(540,80):QSize(600,80),{caption}});out->heading=engine.prepare({style.energyPattern==2?QSize(540,110):QSize(660,130),{heading}});
	QString name=event.displayName?QString::fromUtf8(event.displayName->c_str()):i18n::qtext("Bar.Anonymous");
	if(event.event->data().origin==events::EventOrigin::Preview)name.prepend(QStringLiteral("TEST \u00b7 "));
	auto nameNode=text({440,62},name,36,names,Qt::white);nameNode.box=QRect(0,0,440,62);nameNode.horizontal=visual::Horizontal::Left;nameNode.overflow=visual::Overflow::Clip;
	out->name=engine.prepareTextStrip(nameNode);
	const auto measured=out->name->textMetrics.front().width;
	out->nameViewport=std::clamp(int(std::ceil(measured))+4,152,440);
	out->scrollDistance=measured>out->nameViewport?out->name->image.width()-out->nameViewport:0;
	if(!out->scrollDistance){nameNode.box.setWidth(out->nameViewport);nameNode.horizontal=visual::Horizontal::Center;out->name=engine.prepare({{out->nameViewport,62},{nameNode}});}
	QString detail;
	if(variant==1){const auto &sub=std::get<events::SubEvent>(event.event->data().payload);detail=sub.totalMonths>1?i18n::qtext("List.MonthsDetail").toUpper().arg(qulonglong(sub.totalMonths)):i18n::qtext("List.SubDetail").toUpper();}
	if(variant==2 || variant==3)detail=QStringLiteral("\u00d7%1").arg(qulonglong(std::get<events::GiftEvent>(event.event->data().payload).count));
	if(variant==4)detail=i18n::qtext("List.RaidDetail").toUpper().arg(qulonglong(std::get<events::RaidEvent>(event.event->data().payload).viewers));
	if(variant==5)detail=i18n::qtext("List.BitsDetail").toUpper().arg(qulonglong(std::get<events::CheerEvent>(event.event->data().payload).bits));
	const int plateWidth=out->nameViewport+40;
	visual::Scene badge{{plateWidth,86},{visual::CardNode{QRect(4,4,plateWidth-8,78),QColor(8,4,23,240),out->accent,16,3}}};
	if(style.energyPattern==1){QPainterPath plate;plate.moveTo(20,7);plate.lineTo(plateWidth-7,7);plate.lineTo(plateWidth-20,79);plate.lineTo(7,79);plate.closeSubpath();badge.nodes={visual::PathNode{plate,out->accent,QColor(11,7,21,245),3}};}
	if(style.energyPattern==2)badge.nodes={visual::CardNode{QRect(4,4,plateWidth-8,78),QColor(12,10,14,245),QColor(151,109,73),4,2}};
	if(style.energyPattern==3)badge.nodes={visual::CardNode{QRect(4,4,plateWidth-8,78),QColor(3,20,29,245),out->accent,6,3}};
	if(style.energyPattern==4){QPainterPath plate;plate.moveTo(18,5);plate.lineTo(plateWidth-6,5);plate.lineTo(plateWidth-18,81);plate.lineTo(6,81);plate.closeSubpath();badge.nodes={visual::PathNode{plate,out->accent,QColor(21,5,9,245),3}};}
	if(style.energyPattern==5)badge.nodes={visual::CardNode{QRect(4,4,plateWidth-8,78),QColor(32,6,49,245),out->accent,14,3}};
	if(style.energyPattern==6){QPainterPath plate;plate.moveTo(9,8);plate.lineTo(plateWidth-6,4);plate.lineTo(plateWidth-9,78);plate.lineTo(5,82);plate.closeSubpath();badge.nodes={visual::PathNode{plate,QColor(255,222,24),QColor(8,8,9,250),3}};}
	if(style.energyPattern==7)badge.nodes={visual::CardNode{QRect(4,4,plateWidth-8,78),QColor(6,12,21,220),QColor(166,204,230,210),10,1}};
	if(style.energyPattern==8){QPainterPath plate;plate.moveTo(14,5);plate.lineTo(plateWidth-14,5);plate.lineTo(plateWidth-5,15);plate.lineTo(plateWidth-5,71);plate.lineTo(plateWidth-14,81);plate.lineTo(14,81);plate.lineTo(5,71);plate.lineTo(5,15);plate.closeSubpath();badge.nodes={visual::PathNode{plate,QColor(180,120,52),QColor(15,13,12,240),2}};}
	if(style.energyPattern==9){QPainterPath plate;plate.moveTo(16,5);plate.lineTo(plateWidth-6,5);plate.lineTo(plateWidth-6,67);plate.lineTo(plateWidth-20,81);plate.lineTo(6,81);plate.lineTo(6,19);plate.closeSubpath();badge.nodes={visual::PathNode{plate,QColor(80,225,255,220),QColor(6,17,39,180),2}};}
	out->badge=engine.prepare(badge);
	if(!detail.isEmpty())out->detail=engine.prepare({{460,76},{text({460,76},detail,style.energyPattern==7?30:36,labels,out->accent,style.energyPattern<2 || style.energyPattern==6)}});return out;
}
}
