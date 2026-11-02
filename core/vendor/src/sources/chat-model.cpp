// SPDX-License-Identifier: GPL-2.0-or-later
#include "chat-model.hpp"
#include <QFontMetricsF>
#include <cmath>
#include <QTextDocument>
#include <QTextCursor>
#include <QTextImageFormat>
#include <QTextBlock>
#include <QAbstractTextDocumentLayout>
#include <QPainter>
namespace oto::sources {
ChatView chatView(const state::SnapshotPtr &snapshot,const state::ChannelKey &channel,const ChatSettings &settings,std::uint64_t afterDelivery){
	ChatView view;view.channel=channel;if(!snapshot)return view;view.generation=snapshot->generation;
	const auto found=snapshot->channels.find(channel);if(found==snapshot->channels.end())return view;
	view.moderation=found->second->chatModeration;
	for(const auto &item:found->second->chat){const auto &c=std::get<events::ChatEvent>(item.event->data().payload);
		if(item.delivery<=afterDelivery || (!settings.showCommands && QString::fromStdString(c.message).trimmed().startsWith('!')))continue;view.messages.push_back(item);}
	if(view.messages.size()>std::size_t(settings.messages))view.messages.erase(view.messages.begin(),view.messages.end()-settings.messages);return view;
}
ChatPose sampleChatRow(const ChatRow &row,int entryMs,ChatLayout layout,visual::MotionClock::time_point now){
	ChatPose pose;pose.position=row.position;if(!entryMs)return pose;
	const auto progress=[&](auto start){return start==visual::MotionClock::time_point{}?1.f:std::clamp(float(std::chrono::duration<double,std::milli>(now-start).count()/entryMs),0.f,1.f);};
	const auto move=progress(row.moved),entry=progress(row.entered);pose.position=row.from+(row.position-row.from)*visual::smooth(move);
	pose.opacity=visual::smooth(entry);if(entry<1)pose.position+=layout==ChatLayout::Vertical?QPointF(0,18*(1-visual::smooth(entry))):QPointF(32*(1-visual::smooth(entry)),0);
	pose.finished=move>=1 && entry>=1;return pose;
}
namespace {
// Shared glass/phosphor surface is rasterized once, never over live text/emotes.
// SourceAtop preserves the selected panel alpha, including a fully clear panel.
void paintSharedPanel(QPainter &painter,const ChatSettings &settings){
	const auto &style=settings.style;
	const QRect interior(style.insetX,style.insetY,settings.width-2*style.insetX,settings.height-2*style.insetY);
	auto fill=style.cell;fill.setAlpha(settings.backgroundOpacity*255/100);
	if(!style.scanlines){painter.fillRect(interior,fill);return;}
	painter.save();painter.setRenderHint(QPainter::Antialiasing);
	QPainterPath glass;glass.addRoundedRect(interior,style.radius,style.radius);
	painter.fillPath(glass,fill);painter.setClipPath(glass);
	painter.setCompositionMode(QPainter::CompositionMode_SourceAtop);
	for(int y=interior.top()+4;y<interior.bottom();y+=4)
		painter.fillRect(QRect(interior.left(),y,interior.width(),1),QColor(30,125,90,24));
	painter.restore();
}
// Two cached vector paths, shared by circuit-style panels and message cells.
// All details stay inside their canvas, including compact wrapped messages.
std::array<QPainterPath,2> circuitTraces(int width,int height,bool panel){
	std::array<QPainterPath,2> paths;auto &cyan=paths[0];auto &accent=paths[1];
	const qreal w=width,h=height,b=std::min(10.,h/4.),x=panel?3.:2.,y=panel?3.:2.;
	cyan.moveTo(x,b+y);cyan.lineTo(x,y+b/2);cyan.lineTo(x+b/2,y);cyan.lineTo(w*.21,y);
	cyan.moveTo(w*.27,y);cyan.lineTo(w*.53,y);cyan.lineTo(w*.53+3,y+2);cyan.lineTo(w*.71,y+2);
	cyan.moveTo(w-b-x,y);cyan.lineTo(w-x,b+y);cyan.lineTo(w-x,h-b-y);cyan.lineTo(w-b-x,h-y);cyan.lineTo(w*.76,h-y);
	cyan.moveTo(w*.64,h-y);cyan.lineTo(w*.32,h-y);cyan.lineTo(w*.32-3,h-y-2);cyan.lineTo(w*.18,h-y-2);
	accent.moveTo(x+2,b+y+3);accent.lineTo(x+2,h-b-y);accent.lineTo(x+b,h-y-1);accent.lineTo(w*.15,h-y-1);
	accent.moveTo(w*.74,y);accent.lineTo(w-b-x,y);accent.lineTo(w-x-2,b+y);
	for(int i=0;i<3;++i){const qreal at=w*.36+i*7;cyan.moveTo(at,y+3);cyan.lineTo(at+4,y+3);accent.moveTo(w*.82+i*7,h-y-3);accent.lineTo(w*.82+i*7+4,h-y-3);}
	if(panel){
		for(int i=0;i<4;++i){const qreal at=h*(.18+i*.18);cyan.moveTo(w-x,at);cyan.lineTo(w-x-3,at+3);cyan.lineTo(w-x-3,at+9);accent.moveTo(x,at+4);accent.lineTo(x+3,at+7);accent.lineTo(x+3,at+12);}
	}else{
		cyan.moveTo(w-x-4,h/2-3);cyan.lineTo(w-x-2,h/2);cyan.lineTo(w-x-4,h/2+3);
		accent.moveTo(x+1,b+y+4);accent.lineTo(x+4,b+y+1);
	}
	return paths;
}
// Static ember fissures hug the rails, leaving the message interior clean.
// Paths scale with the cell and are prepared through the shared scene cache.
std::array<QPainterPath,2> fracturedCell(int width,int height){
	std::array<QPainterPath,2> paths;auto &shape=paths[0];auto &cracks=paths[1];
	const qreal w=width,h=height,b=std::min(9.,h/4.);
	shape.moveTo(b,2);shape.lineTo(w*.24,2);shape.lineTo(w*.24+4,4);shape.lineTo(w*.24+8,2);
	shape.lineTo(w-b,2);shape.lineTo(w-3,b);shape.lineTo(w-2,h*.44);shape.lineTo(w-5,h*.51);shape.lineTo(w-2,h*.57);
	shape.lineTo(w-3,h-b);shape.lineTo(w-b,h-3);shape.lineTo(w*.69,h-2);shape.lineTo(w*.69-4,h-4);shape.lineTo(w*.69-8,h-2);
	shape.lineTo(b,h-3);shape.lineTo(3,h-b);shape.lineTo(2,h*.56);shape.lineTo(5,h*.5);shape.lineTo(2,h*.42);shape.lineTo(3,b);shape.closeSubpath();
	for(int i=0;i<6;++i){
		const qreal x=b+8+(w-2*b-30)*(i+.35)/6;
		const qreal top=i%2?2.:h-3,inside=i%2?1.:-1.;
		cracks.moveTo(x-9,top);cracks.lineTo(x-3,top+inside);cracks.lineTo(x,top+inside*4);cracks.lineTo(x+3,top+inside*2);cracks.lineTo(x+10,top);
		cracks.moveTo(x,top+inside*4);cracks.lineTo(x-2,top+inside*6);
	}
	cracks.moveTo(3,b);cracks.lineTo(7,b+3);cracks.lineTo(4,b+7);
	cracks.moveTo(w-3,h-b);cracks.lineTo(w-7,h-b-3);cracks.lineTo(w-4,h-b-7);
	return paths;
}
// Brush chips and spray dots are cached with each cell. Keep the paint in a
// 6 px edge band so wrapped text and inline emotes have an unobstructed interior.
std::array<QPainterPath,2> paintedEdges(int width,int height){
	std::array<QPainterPath,2> paths;
	for(int i=0;i<8;++i){
		auto &paint=paths[i%2];const qreal x=8+(width-28)*(i+.2)/8;
		const qreal length=std::min(qreal(14+i%4*3),(width-20)/16.);
		paint.moveTo(x,2);paint.lineTo(x+length,1.5);paint.lineTo(x+length-3,3.5);paint.lineTo(x+length/2,4.5);paint.lineTo(x+2,3);paint.closeSubpath();
		paint.moveTo(width-x,height-2);paint.lineTo(width-x-length,height-1.5);paint.lineTo(width-x-length+3,height-3.5);paint.lineTo(width-x-length/2,height-4.5);paint.lineTo(width-x-2,height-3);paint.closeSubpath();
		paint.addEllipse(QRectF(x+length+2,2+i%2,1.5,1.5));
		paint.addEllipse(QRectF(width-x-length-3.5,height-4-i%2,1.5,1.5));
	}
	for(int i=0;i<4;++i){auto &paint=paths[i%2];const qreal y=7+(height-18)*(i+.2)/4;
		paint.addEllipse(QRectF(2,y,2+i%2,2));paint.addEllipse(QRectF(width-5,y,2+i%2,2));}
	return paths;
}
// Recessed rails and small slotted fasteners stay in the outer 6 px band.
// These two paths are prepared with the cell, never during animation frames.
std::array<QPainterPath,2> rivetedRails(int width,int height){
	std::array<QPainterPath,2> paths;auto &rails=paths[0];auto &fasteners=paths[1];
	for(const qreal y:{4.,height-4.}){
		rails.moveTo(10,y);rails.lineTo(width-10,y);
		for(const qreal x:{5.5,width-5.5}){
			fasteners.addEllipse(QRectF(x-1.25,y-1.25,2.5,2.5));
			fasteners.moveTo(x-.7,y+.7);fasteners.lineTo(x+.7,y-.7);
		}
	}
	return paths;
}
// Cyan glass highlights and magenta projection strips are cached with the
// cell. Keep the interior untouched, even when the fill is fully transparent.
std::array<QPainterPath,2> holographicGlass(int width,int height){
	std::array<QPainterPath,2> paths;auto &glass=paths[0];auto &projection=paths[1];
	glass.moveTo(10,3);glass.lineTo(width*.43,3);
	glass.moveTo(width*.54,3);glass.lineTo(width-11,3);
	glass.moveTo(3,10);glass.lineTo(3,height-10);
	glass.moveTo(11,height-3);glass.lineTo(width*.34,height-3);
	projection.moveTo(width*.48,height-3);projection.lineTo(width-10,height-3);
	projection.moveTo(width-4,9);projection.lineTo(width-4,height-9);
	for(int i=0;i<4;++i){const qreal x=width*.68+i*4;
		glass.moveTo(x,3);glass.lineTo(x+1,5);
		projection.moveTo(width-x,height-3);projection.lineTo(width-x-1,height-5);
	}
	return paths;
}
visual::FramePtr prepareBorder(visual::VisualEngine &engine,const ChatSettings &settings,const visual::ImagePtr &artwork){
	if(!settings.showFrame){
		if(settings.style.rowCards)return {};
		QImage image(settings.width,settings.height,QImage::Format_RGBA8888_Premultiplied);image.fill(Qt::transparent);
		{QPainter painter(&image);paintSharedPanel(painter,settings);}
		return engine.prepareRaster(std::move(image));
	}
	if(settings.style.circuit){
		QImage image(settings.width,settings.height,QImage::Format_RGBA8888_Premultiplied);image.fill(Qt::transparent);
		const auto paths=circuitTraces(settings.width,settings.height,true);
		{QPainter painter(&image);painter.setRenderHint(QPainter::Antialiasing);auto edge=settings.style.edge;edge.setAlpha(190);painter.setPen(QPen(edge,1));painter.drawPath(paths[0]);painter.setPen(QPen(settings.style.accent,1.5));painter.drawPath(paths[1]);}
		return engine.prepareRaster(std::move(image));
	}
	if(!artwork)return {};
	const auto &source=artwork->image;const auto &style=settings.style;
	QImage image(settings.width,settings.height,QImage::Format_RGBA8888_Premultiplied);image.fill(Qt::transparent);
	// Nine slices preserve authored corners in both layouts. Transparent panel
	// artwork overlays one adjustable fill, without darkening it per message.
	const int sx=std::clamp(source.width()*style.cap/1000,1,(source.width()-1)/2),sy=std::clamp(style.capY?source.height()*style.capY/1000:source.height()/3,1,(source.height()-1)/2);
	int dx=std::min(settings.width/3,style.corner?style.corner:std::max(style.insetX,settings.layout==ChatLayout::Horizontal?60:32));
	int dy=std::min(settings.height/3,style.corner?style.corner:std::max(style.insetY,20));
	if(style.preserveCornerAspect){
		const double scale=std::min(double(dx)/sx,double(dy)/sy);
		dx=std::max(1,qRound(sx*scale));dy=std::max(1,qRound(sy*scale));
	}
	const int xs[]={0,sx,source.width()-sx,source.width()},ys[]={0,sy,source.height()-sy,source.height()};
	const int xd[]={0,dx,settings.width-dx,settings.width},yd[]={0,dy,settings.height-dy,settings.height};
	const QRect interior(style.insetX,style.insetY,settings.width-2*style.insetX,settings.height-2*style.insetY);
	{QPainter painter(&image);painter.setRenderHint(QPainter::SmoothPixmapTransform);
		if(!style.rowCards)paintSharedPanel(painter,settings);
		for(int y=0;y<3;++y)for(int x=0;x<3;++x){const QRect target(xd[x],yd[y],xd[x+1]-xd[x],yd[y+1]-yd[y]);
			if(x==1 && y==1){if(style.rowCards && !style.fractured && style.fillBehindRows)painter.fillRect(target,style.cell);}
			else painter.drawImage(target,source,QRect(xs[x],ys[y],xs[x+1]-xs[x],ys[y+1]-ys[y]));}}
	// Opacity attenuates only dark interior pixels; bright authored ornaments
	// keep their original alpha. Live text and emotes are separate planes.
	if(style.rowCards && settings.backgroundOpacity<100)for(int y=interior.top();y<=interior.bottom();++y){auto *line=image.scanLine(y);for(int x=interior.left();x<=interior.right();++x){auto *p=line+x*4;if(!p[3])continue;
		const float dark=std::clamp((160-std::max({int(p[0]),int(p[1]),int(p[2])})*255/int(p[3]))/64.f,0.f,1.f);
		const float feather=std::clamp(std::min({x-interior.left(),interior.right()-x,y-interior.top(),interior.bottom()-y})/2.f,0.f,1.f);
		const float keep=1-dark*feather*(1-settings.backgroundOpacity/100.f);for(int c=0;c<4;++c)p[c]=uchar(qRound(p[c]*keep));}}
	return engine.prepareRaster(std::move(image));
}
ChatRow prepareRow(visual::VisualEngine &engine,const ChatSettings &settings,const state::LatestEvent &item,chat::EmoteCache *media){
	ChatRow row;row.event=item.event;const auto &c=std::get<events::ChatEvent>(item.event->data().payload);
	const int maximum=settings.height-2*settings.style.insetY-8;
	const int pad=std::min(settings.style.padding,maximum/4),badge=std::min(24,maximum-pad*2),gap=8;
	const int count=int(c.badges.size());
	QFont bodyFont(settings.messageFamily);bodyFont.setPixelSize(settings.messageSize);
	QFont nameFont(settings.nameFamily);nameFont.setPixelSize(settings.nameSize);nameFont.setBold(true);
	int side=std::clamp(int(settings.messageSize*1.3),10,96);
	const auto tokens=media?media->tokens(c,settings.thirdParty):std::vector<chat::Token>{{QString::fromStdString(c.message),{}}};
	double textWidth=0;for(const auto &token:tokens)if(token.layerOf<0)textWidth+=token.url.isEmpty()?QFontMetricsF(bodyFont).horizontalAdvance(token.text):std::ceil(side*chat::emoteExtent(token.effects)[0]);
	const int natural=pad*2+count*(badge+2)+gap*2+int(QFontMetricsF(nameFont).horizontalAdvance(QString::fromStdString(c.author.displayName)))+int(std::ceil(textWidth));
	const int availableWidth=settings.width-2*settings.style.insetX-8;
	const int width=settings.layout==ChatLayout::Vertical?availableWidth:std::min(availableWidth,std::max(natural,240));
	// Reserve a stable band while badge media loads. Many simultaneous badges
	// shrink together, leaving room for the complete name and message body.
	const bool vertical=settings.layout==ChatLayout::Vertical;
	// A fixed author band keeps every message at the same left edge. Badge
	// counts and long names fit inside that band without pushing the body.
	const int authorEnd=pad+width/3;
	const double badgeWidth=count?std::min(double(count*(badge+2)-2),double(vertical?width/9:width/3)):0;
	const double badgeSide=count?std::max(1.,(badgeWidth-2*(count-1))/count):0;
	const int nameX=pad+(count?int(std::ceil(badgeWidth))+gap:0);
	const int nameWidth=vertical?std::max(24,authorEnd-nameX):std::max(24,std::min(width/3-pad,int(std::ceil(QFontMetricsF(nameFont).horizontalAdvance(QString::fromStdString(c.author.displayName))))+2));
	visual::TextNode name{{nameX,pad,nameWidth,128},QString::fromStdString(c.author.displayName)};
	name.family=settings.nameFamily;name.pixelSize=settings.nameSize;name.minimumPixelSize=8;name.bold=true;name.color=settings.twitchColors && !c.color.empty()?QColor(QString::fromStdString(c.color)):settings.nameColor;name.overflow=visual::Overflow::AutoFit;
	const int lineH=std::min(maximum-pad*2,std::max({badge,settings.nameSize+5,settings.messageSize+5}));name.box.setHeight(lineH);name.vertical=visual::Vertical::Center;
	const int bodyX=vertical?authorEnd+gap:name.box.right()+gap;
	const int bodyWidth=std::max(20,width-bodyX-pad);
	QTextDocument document;document.setDocumentMargin(0);document.setDefaultFont(bodyFont);document.setTextWidth(bodyWidth);
	QTextOption option;option.setAlignment(Qt::AlignLeft);option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);document.setDefaultTextOption(option);
	QTextCharFormat textFormat;textFormat.setForeground(settings.messageColor);
	QImage blank(1,1,QImage::Format_RGBA8888_Premultiplied);blank.fill(Qt::transparent);document.addResource(QTextDocument::ImageResource,QUrl("oto://emote"),blank);
	struct Positioned {int index;chat::Token token;chat::EmotePtr asset;QSizeF size;};std::vector<Positioned> positions;
	const auto layout=[&](int pixels){
		bodyFont.setPixelSize(pixels);side=std::clamp(int(pixels*1.3),10,96);
		document.clear();document.setDefaultFont(bodyFont);document.addResource(QTextDocument::ImageResource,QUrl("oto://emote"),blank);positions.clear();QTextCursor cursor(&document);
		std::vector<int> indices;std::vector<chat::EmotePtr> assets;std::vector<QSizeF> sizes;
		for(const auto &token:tokens){
			const auto extent=chat::emoteExtent(token.effects);sizes.push_back({std::ceil(side*extent[0]),std::ceil(side*extent[1])});indices.push_back(cursor.position());assets.push_back(token.url.isEmpty()?chat::EmotePtr{}:media->get(token.url));
			if(token.url.isEmpty()){auto format=textFormat;if(token.color.isValid())format.setForeground(token.color);cursor.insertText(token.text,format);continue;}
			if(token.layerOf>=0){positions.push_back({indices[size_t(token.layerOf)],token,assets[size_t(token.layerOf)]?assets.back():chat::EmotePtr{},sizes[size_t(token.layerOf)]});continue;}
			positions.push_back({cursor.position(),token,assets.back(),sizes.back()});
			QTextImageFormat image;image.setName("oto://emote");image.setWidth(sizes.back().width());image.setHeight(sizes.back().height());cursor.insertImage(image);
		}
		return document.size();
	};
	int pixels=settings.messageSize;auto docSize=layout(pixels);
	// Grow the cell first. Only reduce the body when wrapping still exceeds the
	// configured source height. Keep every fragment, including the last emote.
	if(docSize.height()>maximum-pad*2){int lo=8,hi=pixels;while(lo<hi){const int mid=(lo+hi+1)/2;const auto size=layout(mid);if(size.height()<=maximum-pad*2)lo=mid;else hi=mid-1;}pixels=lo;docSize=layout(pixels);}
	row.bodyPixelSize=pixels;
	row.bodySize={std::max(bodyWidth,int(std::ceil(docSize.width()))),int(std::ceil(docSize.height()))};
	// Extreme messages on a very small canvas can still exceed the area at 8px.
	// Fit the complete prepared body uniformly rather than silently omit data.
	row.bodyScale=float(std::min({1.,double(maximum-pad*2)/row.bodySize.height(),double(bodyWidth)/row.bodySize.width()}));
	for(auto block=document.begin();block.isValid();block=block.next())row.lines+=block.layout()->lineCount();
	const int visibleHeight=std::min(maximum-pad*2,int(std::ceil(row.bodySize.height()*row.bodyScale)));
	const int height=std::min(maximum,pad*2+std::max(lineH,visibleHeight));
	row.bodyBox={double(bodyX),double(pad),double(bodyWidth),double(visibleHeight)};
	for(const auto &p:positions){const auto block=document.findBlock(p.index);const auto line=block.layout()->lineForTextPosition(p.index-block.position());if(!line.isValid())continue;
		const auto rect=document.documentLayout()->blockBoundingRect(block);
		const QRectF box(line.cursorToX(p.index-block.position()),rect.top()+line.y()+line.ascent()-p.size.height(),p.size.width(),p.size.height());
		row.emotes.push_back({box,p.asset,p.token.url,p.token.text,p.token.layerOf>=0,p.token.effects,p.token.layerRect});
	}
	auto fill=settings.style.cell;fill.setAlpha(settings.backgroundOpacity*255/100);
	visual::Scene scene{{width,height},{}};
	if(settings.style.rowCards){
		if(settings.style.fractured){const auto paths=fracturedCell(width,height);scene.nodes.emplace_back(visual::PathNode{paths[0],settings.style.edge.darker(170),fill,1.5f});auto halo=settings.style.edge;halo.setAlpha(60);scene.nodes.emplace_back(visual::PathNode{paths[1],halo,Qt::transparent,3.f});scene.nodes.emplace_back(visual::PathNode{paths[1],settings.style.edge,Qt::transparent,1.f});}
		else if(settings.style.bevel){const auto b=settings.style.bevel;QPainterPath shape;shape.moveTo(b,2);shape.lineTo(width-b,2);shape.lineTo(width-2,b);shape.lineTo(width-2,height-b);shape.lineTo(width-b,height-2);shape.lineTo(b,height-2);shape.lineTo(2,height-b);shape.lineTo(2,b);shape.closeSubpath();scene.nodes.emplace_back(visual::PathNode{shape,settings.style.edge,fill,1.f});}
		else scene.nodes.emplace_back(visual::CardNode{{1,1,width-2,height-2},fill,settings.style.edge,settings.style.radius,settings.style.glow});
		if(settings.style.circuit){const auto paths=circuitTraces(width,height,false);auto edge=settings.style.edge;edge.setAlpha(230);scene.nodes.emplace_back(visual::PathNode{paths[0],edge,Qt::transparent,1.f});scene.nodes.emplace_back(visual::PathNode{paths[1],settings.style.accent,Qt::transparent,1.5f});}
		if(settings.style.painted){const auto paths=paintedEdges(width,height);scene.nodes.emplace_back(visual::PathNode{paths[0],Qt::transparent,settings.style.edge,.5f});scene.nodes.emplace_back(visual::PathNode{paths[1],Qt::transparent,settings.style.accent,.5f});}
		if(settings.style.riveted){const auto paths=rivetedRails(width,height);scene.nodes.emplace_back(visual::PathNode{paths[0],settings.style.edge.darker(210),Qt::transparent,1.f});scene.nodes.emplace_back(visual::PathNode{paths[1],settings.style.accent,Qt::transparent,.7f});}
		if(settings.style.holographic){const auto paths=holographicGlass(width,height);auto highlight=settings.style.edge.lighter(150);highlight.setAlpha(190);auto halo=settings.style.accent;halo.setAlpha(48);scene.nodes.emplace_back(visual::PathNode{paths[0],highlight,Qt::transparent,.8f});scene.nodes.emplace_back(visual::PathNode{paths[1],halo,Qt::transparent,3.f});scene.nodes.emplace_back(visual::PathNode{paths[1],settings.style.accent,Qt::transparent,1.f});}
	}
	// CRT phosphor lines are static beneath text and emotes, prepared with the cell.
	if(settings.style.rowCards && settings.style.scanlines)for(int y=4;y<height-2 && scene.nodes.size()<48;y+=4)scene.nodes.emplace_back(visual::RectangleNode{{4,y,width-8,1},QColor(30,125,90,28*settings.backgroundOpacity/100)});
	const auto badgeUrls=media?media->badgeUrls(c.badges):std::vector<QString>(c.badges.size());
	for(int i=0;i<count;++i){const auto &url=badgeUrls[size_t(i)];row.badges.push_back({{pad+i*(badgeSide+2),pad+(lineH-badgeSide)/2,badgeSide,badgeSide},media?media->getBadge(url):chat::EmotePtr{},url});}
	scene.nodes.emplace_back(name);row.graphic=engine.prepare(scene);
	// Tile the complete body. Each immutable texture
	// remains within the shared renderer's 2048px limit. Video only draws tiles
	// intersecting the body viewport; blank emote-only tiles need no texture.
	constexpr int page=1024;
	for(int y=0;y<row.bodySize.height();y+=page)for(int x=0;x<row.bodySize.width();x+=page){
		const QRect tile(x,y,std::min(page,row.bodySize.width()-x),std::min(page,row.bodySize.height()-y));
		QImage image(tile.size(),QImage::Format_RGBA8888_Premultiplied);image.fill(Qt::transparent);
		{QPainter painter(&image);painter.translate(-x,-y);document.drawContents(&painter,tile);
			for(const auto &emote:row.emotes)if(!emote.media && !emote.layer && emote.box.intersects(tile)){painter.save();painter.setClipRect(emote.box,Qt::IntersectClip);painter.setPen(settings.messageColor);auto fallback=bodyFont;fallback.setPixelSize(9);painter.setFont(fallback);painter.drawText(emote.box,Qt::AlignCenter,emote.text);painter.restore();}
		}
		bool painted=false;for(int iy=0;iy<image.height() && !painted;++iy)for(int ix=0;ix<image.width();++ix)if(image.constScanLine(iy)[ix*4+3]){painted=true;break;}
		if(painted)row.bodyPages.push_back({tile.topLeft(),engine.prepareRaster(std::move(image))});
	}
	return row;
}
}
ChatFramePtr prepareChatFrame(visual::VisualEngine &engine,const ChatSettings &settings,const ChatView &view,const ChatFramePtr &before,visual::MotionClock::time_point now,bool preview,chat::EmoteCache *media,visual::ImagePtr artwork){
	if(settings.width<320 || settings.width>1920 || settings.height<100 || settings.height>1080 || settings.style.insetX<0 || settings.style.insetX>64 || settings.style.insetY<0 || settings.style.insetY>32)throw std::invalid_argument("Invalid Chat geometry");
	if(settings.showFrame && !settings.style.atlas.isEmpty() && !artwork)throw std::invalid_argument("Chat artwork missing");
	const bool compatible=before && before->settings==settings && before->view.channel==view.channel && before->view.generation==view.generation;
	const auto revision=media?media->revision():0;
	if(compatible && before->view.moderation==view.moderation && before->mediaRevision==revision && before->view.messages.size()==view.messages.size() && std::equal(view.messages.begin(),view.messages.end(),before->view.messages.begin(),[](const auto &a,const auto &b){return a.event==b.event;}))return before;
	auto frame=std::make_shared<ChatFrame>();frame->settings=settings;frame->view=view;frame->mediaRevision=revision;frame->mediaClock=compatible?before->mediaClock:now;
	frame->artwork=std::move(artwork);frame->border=compatible?before->border:prepareBorder(engine,settings,frame->artwork);
	frame->content={settings.style.insetX,settings.style.insetY,settings.width-2*settings.style.insetX,settings.height-2*settings.style.insetY};
	int extent=4;const int available=settings.layout==ChatLayout::Vertical?frame->content.height():frame->content.width();
	for(auto it=view.messages.rbegin();it!=view.messages.rend();++it){
		const ChatRow *previous=nullptr;if(compatible)for(const auto &r:before->rows)if(r.event==it->event){previous=&r;break;}
		auto row=previous && before->mediaRevision==revision?*previous:prepareRow(engine,settings,*it,media);
		if(previous){row.position=previous->position;row.from=previous->from;row.moved=previous->moved;row.entered=previous->entered;}
		const int span=settings.layout==ChatLayout::Vertical?row.graphic->image.height():row.graphic->image.width();
		if(settings.layout==ChatLayout::Vertical && extent+span>available-4 && !frame->rows.empty())break;
		const auto position=settings.layout==ChatLayout::Vertical?QPointF(4,available-extent-span)+frame->content.topLeft():QPointF(available-extent-span,(frame->content.height()-row.graphic->image.height())/2)+frame->content.topLeft();
		if(previous){const auto current=sampleChatRow(*previous,settings.entryMs,settings.layout,now).position;row.position=position;if(previous->position!=position){row.from=current;row.moved=now;}}
		else{row.position=row.from=position;const bool fresh=preview || (compatible && (!before->view.messages.empty()?it->delivery>before->view.messages.back().delivery:true));if(fresh && settings.entryMs){row.moved=now;row.entered=now;}}
		frame->rows.push_back(std::move(row));extent+=span+settings.style.gap;
	}
	std::reverse(frame->rows.begin(),frame->rows.end());
	// Retiring messages also finish their horizontal trip across the edge,
	// including when the configured history count removes them from the view.
	if(compatible && !view.messages.empty() && settings.layout==ChatLayout::Horizontal)for(const auto &old:before->rows){
		// Deleted content disappears immediately, even if its card was already
		// retiring outside the bounded history. Normal departures stay animated.
		if(view.moderation && state::chatRemoved(*view.moderation,*old.event))continue;
		if(std::any_of(frame->rows.begin(),frame->rows.end(),[&](const auto &r){return r.event==old.event;}))continue;
		const auto current=sampleChatRow(old,settings.entryMs,settings.layout,now).position;
		if(current.x()+old.graphic->image.width()<=0)continue;
		auto leaving=old;if(!old.retiring){leaving.from=current;leaving.position={-double(old.graphic->image.width())-6,current.y()};leaving.moved=now;leaving.retiring=true;}
		frame->rows.insert(frame->rows.begin(),std::move(leaving));
	}
	if(frame->rows.size()>64)frame->rows.erase(frame->rows.begin(),frame->rows.end()-64);
	return frame;
}
}
