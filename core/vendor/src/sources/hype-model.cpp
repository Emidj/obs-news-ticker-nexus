#include "localization.hpp"
// SPDX-License-Identifier: GPL-2.0-or-later
#include "hype-model.hpp"
#include <cmath>
#include <QPainter>
#include <QLinearGradient>
#include <QLocale>
namespace oto::sources {
std::array<QString,2> hypeContributionLabels(const events::HypeContribution &c){
	std::array<QString,2> result;int line=0;
	const auto number=[](std::uint64_t n){return QLocale(nexus::localization::systemLanguage()).toString(qulonglong(n));};
	if(c.methods&1)result[line++]=i18n::qtext("Unit.Bits").arg(number(c.bits));
	if(c.methods&2)result[line++]=i18n::qtext("Unit.Subs").arg(c.subscriptions?number(*c.subscriptions):QStringLiteral("—"));
	if(!line)result[0]=QStringLiteral("—");
	return result;
}
HypeView hypeView(const state::SnapshotPtr &snapshot,const state::ChannelKey &key,std::chrono::system_clock::time_point now,int holdMs,int exitMs) {
	HypeView result;result.channel=key;result.preview=key.origin==events::EventOrigin::Preview;
	if(!snapshot)return result;const auto found=snapshot->channels.find(key);
	if(found==snapshot->channels.end() || !found->second->hypeTrain)return result;
	const auto &channel=*found->second;const auto &h=*channel.hypeTrain;
	result.trainId=h.trainId;result.level=h.level;result.total=h.total;result.goal=h.goal;result.progress=h.progress.value_or(h.total);
	result.updatedAt=channel.hypeUpdatedAt;result.expiresAt=h.expiresAt;result.ended=h.stage==events::HypeTrainStage::End;
	result.contributors=h.contributors;if(result.contributors.size()>3)result.contributors.resize(3);
	result.ratio=h.goal?double(std::clamp(static_cast<long double>(result.progress)/h.goal,0.0L,1.0L)):0;
	if(result.ended){result.visible=now<channel.hypeUpdatedAt+std::chrono::milliseconds(holdMs+2*exitMs);return result;}
	if(h.expiresAt==std::chrono::system_clock::time_point{})return result; // No invented deadline for old simulations.
	const auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(h.expiresAt-now).count();
	result.remaining=ms<=0?0:(ms+999)/1000;result.waiting=ms<=0;
	// Zero is not an End event. Keep a short grace period, then suspend an orphaned train.
	result.visible=now<h.expiresAt+std::chrono::seconds(15);return result;
}
double hypeFill(const HypeFrame &frame,visual::MotionClock::time_point now) {
	const auto ms=std::chrono::duration<double,std::milli>(now-frame.changed).count();
	const auto t=visual::smooth(float(std::clamp(ms/400.0,0.0,1.0)));
	return frame.fromRatio+(frame.view.ratio-frame.fromRatio)*t;
}
HypeFramePtr prepareHypeFrame(visual::VisualEngine &engine,const HypeSettings &settings,const HypeLabels &labels,const HypeView &view,const HypeFramePtr &before,visual::MotionClock::time_point now,bool animateEntrance,visual::ImagePtr artwork,visual::ImagePtr thankArt) {
 if(!view.visible)return {};
 if(before && before->settings==settings && before->view.trainId==view.trainId && before->view.channel==view.channel && before->view.level==view.level && before->view.progress==view.progress && before->view.goal==view.goal && before->view.total==view.total && before->view.remaining==view.remaining && before->view.ended==view.ended && before->view.waiting==view.waiting && before->view.contributors==view.contributors)return before;
 auto frame=std::make_shared<HypeFrame>();frame->settings=settings;frame->view=view;
 const bool same=before && before->view.trainId==view.trainId && before->view.channel==view.channel;
 const bool layoutSame=before && before->settings==settings,compatible=same && layoutSame;
 frame->clockStart=same?before->clockStart:now;
 frame->appeared=same?before->appeared:animateEntrance?now:visual::MotionClock::time_point{};
 frame->ended=view.ended?(same && before->view.ended?before->ended:now):visual::MotionClock::time_point{};
 const bool ratioChanged=compatible && (before->view.ratio!=view.ratio || before->view.level!=view.level);
 frame->changed=ratioChanged?now:compatible?before->changed:now;
 frame->fromRatio=ratioChanged?(before->view.level==view.level?hypeFill(*before,now):0):compatible?before->fromRatio:view.ratio;
 // Celebrate genuine increases in the same active train, never bootstrap,
 // reconnect, a backwards correction or the End packet itself.
 frame->celebrated=view.ended?visual::MotionClock::time_point{}:same && !before->view.ended && view.level>before->view.level?now:same?before->celebrated:visual::MotionClock::time_point{};
 const int w=settings.width,h=settings.height;
 const auto box=[&](QRect r){return QRect(2+qRound(r.x()*(w-4)/1000.0),2+qRound(r.y()*(h-4)/1000.0),std::max(1,qRound(r.width()*(w-4)/1000.0)),std::max(1,qRound(r.height()*(h-4)/1000.0))).intersected(QRect(2,2,w-4,h-4));};
 frame->bar=box(settings.style.barBox);frame->titleBox=box(settings.style.titleBox);frame->levelBox=box(settings.style.levelBox);frame->timerBox=box(settings.style.timerBox);frame->leaderBox=box(settings.style.leaderBox);
 frame->authored=bool(artwork) || (layoutSame && before->authored);
 const auto imageLayer=[&](QImage image){return engine.prepareRaster(std::move(image));};
 if(layoutSame){frame->base=before->base;frame->fill=before->fill;frame->title=before->title;frame->thanks=before->thanks;frame->labels=before->labels;}
 else {
  QImage body(w,h,QImage::Format_RGBA8888_Premultiplied),titleImage=body;body.fill(Qt::transparent);titleImage.fill(Qt::transparent);
  if(artwork){
   QPainter p(&body);p.setRenderHint(QPainter::SmoothPixmapTransform);p.drawImage(QRect(2,2,w-4,h-4),artwork->image);p.end();
   // Keep the authored lettering in its own plane. Transparent parts do not acquire a rectangle.
   QPainter titlePainter(&titleImage);titlePainter.drawImage(frame->titleBox,body,frame->titleBox);titlePainter.end();
   for(int y=frame->titleBox.top();y<=frame->titleBox.bottom();++y){auto *b=body.scanLine(y),*t=titleImage.scanLine(y);for(int x=frame->titleBox.left();x<=frame->titleBox.right();++x){auto *bc=b+x*4,*tc=t+x*4;const int a=bc[3];const int brightness=a?std::max({int(bc[0]),int(bc[1]),int(bc[2])})*255/a:0;for(int c=0;c<4;++c)(brightness>60?bc:tc)[c]=0;}}
   // Fade only genuinely dark interiors. Lighting, lettering and ornaments retain their alpha.
   for(int y=0;y<h;++y){auto *row=body.scanLine(y);for(int x=0;x<w;++x){auto *c=row+x*4;const int alpha=c[3];if(!alpha)continue;const int brightness=std::max({int(c[0]),int(c[1]),int(c[2])})*255/alpha;const float dark=std::clamp((64-brightness)/32.f,0.f,1.f);const float keep=1-dark*(1-settings.backgroundOpacity/100.f);for(int k=0;k<4;++k)c[k]=uchar(qRound(c[k]*keep));}}
   frame->base=imageLayer(body);
  } else {visual::Scene base{{w,h},{visual::CardNode{{2,2,w-4,h-4},QColor(9,14,22,settings.backgroundOpacity*255/100),settings.accent,9,0}}};frame->base=engine.prepare(base);}
  // One immutable fill texture: no painting, shaping or colour conversion on video frames.
  QImage fill(w,h,QImage::Format_RGBA8888_Premultiplied);fill.fill(Qt::transparent);QPainter paint(&fill);paint.setRenderHint(QPainter::Antialiasing);
  QLinearGradient gradient(frame->bar.topLeft(),frame->bar.topRight());
  if(settings.fillMode==1){gradient.setColorAt(0,QColor(40,238,99));gradient.setColorAt(.5,QColor(255,223,45));gradient.setColorAt(1,QColor(255,35,48));}
  else if(settings.fillMode==2){gradient.setColorAt(0,settings.accent);gradient.setColorAt(1,settings.style.fillEnd.isValid()?settings.style.fillEnd:settings.style.secondary);}
  else {gradient.setColorAt(0,settings.accent);gradient.setColorAt(1,settings.accent);}
  const auto b=frame->bar;const float dx=b.width()/20.f,slant=std::min(b.height()*.18f,dx*.32f);
  for(int i=0;i<20;++i){const float x=b.x()+i*dx;QPainterPath part;part.moveTo(x+slant,b.y()+1);part.lineTo(x+dx-1,b.y()+1);part.lineTo(x+dx-slant-1,b.bottom()-1);part.lineTo(x,b.bottom()-1);part.closeSubpath();paint.fillPath(part,gradient);paint.setPen(QColor(255,255,255,settings.fillMode==2?130:60));paint.drawLine(QPointF(x+slant+1,b.y()+2),QPointF(x+dx-2,b.y()+2));}
  paint.end();frame->fill=imageLayer(fill);
  const int titleSize=std::clamp(settings.labelSize?settings.labelSize+8:h/5,8,std::max(8,std::min(128,h/3)));
  const auto titleText=[&](QString value){visual::TextNode n{frame->titleBox,value};n.family=settings.labelSize?settings.labelFamily:settings.style.titleFamily;n.pixelSize=titleSize;n.minimumPixelSize=8;n.color=settings.labels;n.bold=true;n.overflow=visual::Overflow::AutoFit;n.horizontal=visual::Horizontal::Center;n.vertical=visual::Vertical::Center;return engine.prepare({{w,h},{n}});};
  frame->title=frame->authored && settings.artworkTitle?imageLayer(titleImage):titleText(labels.title);
  if(thankArt && settings.artworkTitle){QImage thanksImage(w,h,QImage::Format_RGBA8888_Premultiplied);thanksImage.fill(Qt::transparent);QPainter p(&thanksImage);p.setRenderHint(QPainter::SmoothPixmapTransform);p.drawImage(frame->titleBox,thankArt->image);p.end();frame->thanks=imageLayer(thanksImage);}else frame->thanks=titleText(labels.ended);
 }
 const int labelSize=std::clamp(settings.labelSize?settings.labelSize:h/10,8,std::max(8,h/7));
 const int numberSize=std::clamp(settings.numberSize?settings.numberSize:h/10,8,std::max(8,h/5));
 const auto text=[](QRect area,QString value,QString family,int size,QColor color,visual::Horizontal horizontal,bool bold=false){visual::TextNode n{area,value};n.family=family;n.pixelSize=size;n.minimumPixelSize=8;n.color=color;n.overflow=visual::Overflow::AutoFit;n.horizontal=horizontal;n.vertical=visual::Vertical::Center;n.bold=bold;return n;};
 const auto levelBox=frame->levelBox,timerBox=frame->timerBox,leaderBox=frame->leaderBox;
 const auto labelColor=[&](QColor authored){return settings.labelsFromStyle && authored.isValid()?authored:settings.labels;};
 const int header=std::max(12,h*16/100),rowH=std::max(14,(leaderBox.height()-header)/3);
 if(!layoutSame){visual::Scene captions{{w,h},{}};
  captions.nodes.emplace_back(text({timerBox.x()-w*4/100,timerBox.bottom()+1,timerBox.width()+w*4/100,h*15/100},labels.time,settings.labelFamily,std::max(8,labelSize-3),labelColor(settings.style.timeLabel),visual::Horizontal::Center));
  captions.nodes.emplace_back(text({leaderBox.x()+2,leaderBox.y()+1,leaderBox.width()-4,header},labels.contributors,settings.labelFamily,std::max(8,labelSize-3),labelColor(settings.style.headerColor),visual::Horizontal::Center));frame->labels=engine.prepare(captions);
 }
 visual::Scene level{{w,h},{}};
 level.nodes.emplace_back(text({levelBox.x(),levelBox.y(),levelBox.width(),header},labels.level,settings.labelFamily,labelSize,labelColor(settings.style.levelLabel),visual::Horizontal::Center));
 const auto levelColor=settings.numbersFromStyle?(settings.style.levelColor.isValid()?settings.style.levelColor:settings.accent):settings.numbers;
 level.nodes.emplace_back(text({levelBox.x(),levelBox.y()+header,levelBox.width(),levelBox.height()-header},QString::number(view.level),settings.numberFamily,std::min(128,settings.numberSize?numberSize*2:h*29/100),levelColor,visual::Horizontal::Center,true));frame->level=compatible && before->view.level==view.level?before->level:engine.prepare(level);
 const auto formatted=[](std::uint64_t n){return QLocale(nexus::localization::systemLanguage()).toString(qulonglong(n));};
 visual::Scene numbers{{w,h},{}};
 numbers.nodes.emplace_back(text({frame->bar.x(),frame->bar.bottom()+h*5/100,frame->bar.width(),h*17/100},formatted(view.progress)+" / "+formatted(view.goal),settings.numberFamily,numberSize,settings.numbers,visual::Horizontal::Center));
 numbers.nodes.emplace_back(text(box(settings.style.percentBox),QString::number(int(std::floor(view.ratio*100)))+"%",settings.numberFamily,std::min(128,settings.numberSize?numberSize+8:h*23/100),settings.numbersFromStyle?(settings.style.pattern==2?settings.style.secondary:settings.accent):settings.numbers,visual::Horizontal::Center,true));
 const int iconW=std::max(14,leaderBox.width()*22/100),amountW=std::max(24,leaderBox.width()*43/100);
 for(std::size_t i=0;i<view.contributors.size() && i<3;++i){const auto &c=view.contributors[i];const int y=leaderBox.y()+header+int(i)*rowH;
  const auto amounts=hypeContributionLabels(c);const int lines=amounts[1].isEmpty()?1:2;
  for(int line=0;line<lines;++line){const int top=y+line*rowH/lines,bottom=y+(line+1)*rowH/lines;
   numbers.nodes.emplace_back(text({leaderBox.right()-amountW-2,top,amountW,std::max(1,bottom-top)},amounts[line],settings.numberFamily,numberSize,settings.numbers,visual::Horizontal::Right));}
  if(!frame->authored)numbers.nodes.emplace_back(text({leaderBox.x()+2,y,iconW,rowH},i==0?QStringLiteral("★"):QStringLiteral("◇"),QStringLiteral("Segoe UI Symbol"),labelSize,settings.accent,visual::Horizontal::Center));
  frame->names[i].box={leaderBox.x()+iconW+5,y,std::max(1,leaderBox.width()-iconW-amountW-12),rowH};
  if(compatible && i<before->view.contributors.size() && c.user.displayName==before->view.contributors[i].user.displayName)frame->names[i].frame=before->names[i].frame;
  else {auto n=text({0,0,frame->names[i].box.width(),rowH},QString::fromStdString(c.user.displayName),settings.numberFamily,numberSize,settings.numbers,visual::Horizontal::Left);n.minimumPixelSize=std::max(8,numberSize*4/5);auto fitted=n;visual::TextLayoutEngine layout;n.pixelSize=layout.prepare(fitted).metrics.pixelSize;n.minimumPixelSize=8;n.overflow=visual::Overflow::Clip;frame->names[i].frame=engine.prepareTextStrip(n);}
  frame->names[i].distance=float(std::max(0,frame->names[i].frame->image.width()-frame->names[i].box.width()));
 }
 frame->numbers=compatible && before->view.progress==view.progress && before->view.goal==view.goal && before->view.contributors==view.contributors?before->numbers:engine.prepare(numbers);
 visual::Scene timer{{w,h},{}};const auto time=QStringLiteral("%1:%2").arg(view.remaining/60,2,10,QChar('0')).arg(view.remaining%60,2,10,QChar('0'));
 timer.nodes.emplace_back(text(timerBox,time,settings.numberFamily,std::min(128,settings.numberSize?numberSize:h*19/100),settings.numbers,visual::Horizontal::Center,true));frame->timer=compatible && (view.ended || before->view.remaining==view.remaining)?before->timer:engine.prepare(timer);
 return frame;
}
}
