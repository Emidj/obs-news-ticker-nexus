// SPDX-License-Identifier: GPL-2.0-or-later
#include "preset-registry.hpp"
#include "visual/asset-manager.hpp"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <set>
#include <stdexcept>
#include <cstring>
namespace oto::presets {
namespace {
QString string(const QJsonObject &object,const char *key,int maximum) {
	const auto value=object.value(key);
	if(!value.isString() || value.toString().isEmpty() || value.toString().size()>maximum)
		throw std::invalid_argument("Invalid preset string");
	const auto result=value.toString();
	for(const auto ch:result)if(ch.unicode()<32 || ch.unicode()==127)throw std::invalid_argument("Invalid preset text");
	return result;
}
int integer(const QJsonObject &object,const char *key,int fallback,int minimum,int maximum) {
	if(!object.contains(key))return fallback;
	const auto value=object.value(key);const auto n=value.toDouble(-1);
	if(!value.isDouble() || n<minimum || n>maximum || n!=int(n))throw std::invalid_argument("Invalid preset number");
	return int(n);
}
QColor color(const QJsonObject &object,const char *key,const QColor &fallback) {
	if(!object.contains(key))return fallback;
	const auto value=string(object,key,9);
	static const QRegularExpression hex("^#[0-9a-fA-F]{8}$");
	if(!hex.match(value).hasMatch())throw std::invalid_argument("Invalid preset color");
	return QColor(value); // #AARRGGBB
}
QRect crop(const QJsonObject &object,const char *key,const QRect &fallback) {
	if(!object.contains(key))return fallback;
	const auto value=object.value(key);
	if(!value.isArray() || value.toArray().size()!=4)throw std::invalid_argument("Invalid asset crop");
	const auto a=value.toArray();int v[4];
	for(int i=0;i<4;++i) {const auto n=a[i].toDouble(-1);if(!a[i].isDouble() || n<0 || n>1000 || n!=int(n))throw std::invalid_argument("Invalid asset crop");v[i]=int(n);}
	if(v[2]<1 || v[3]<1 || v[0]+v[2]>1000 || v[1]+v[3]>1000)throw std::invalid_argument("Asset crop outside image");
	return {v[0],v[1],v[2],v[3]};
}
}
Registry Registry::fromFile(const QString &filename) {
	QFile file(filename);
	if(!file.open(QIODevice::ReadOnly) || file.size()>64*1024)throw std::runtime_error("Preset registry unavailable");
	return fromJson(file.read(64*1024+1));
}
Registry Registry::fromJson(const QByteArray &bytes) {
	if(bytes.size()>64*1024)throw std::invalid_argument("Preset registry too large");
	QJsonParseError error;const auto doc=QJsonDocument::fromJson(bytes,&error);
	if(error.error!=QJsonParseError::NoError || !doc.isObject())throw std::invalid_argument("Invalid preset registry");
	const auto object=doc.object();const auto version=object.value("schemaVersion");
	if(!version.isDouble() || version.toDouble()!=1 || !object.value("presets").isArray())throw std::invalid_argument("Unsupported preset registry");
	const auto entries=object.value("presets").toArray();
	if(entries.isEmpty() || entries.size()>64)throw std::invalid_argument("Preset count exceeded");
	Registry result;std::set<QString> ids;bool minimal=false;
	static const QRegularExpression id("^[a-z][a-z0-9-]{0,47}$");
	for(const auto &entry:entries) {
		if(!entry.isObject())throw std::invalid_argument("Invalid preset definition");
		const auto item=entry.toObject();Definition definition{string(item,"id",48),string(item,"name",80),{}};
		if(!id.match(definition.id).hasMatch() || !ids.insert(definition.id).second || !item.value("style").isObject())throw std::invalid_argument("Invalid or duplicate preset ID");
		for(const auto *layout:{"style","eventListStyle"}) {
		if(!item.contains(layout))continue;
		if(!item.value(layout).isObject())throw std::invalid_argument("Invalid preset layout style");
		const bool list=std::strcmp(layout,"eventListStyle")==0;
		if(list)definition.eventListStyle.emplace();
		const auto style=item.value(layout).toObject();auto &out=list?*definition.eventListStyle:definition.style;
		if(style.contains("family"))out.family=string(style,"family",128);
		out.background=color(style,"background",out.background);out.title=color(style,"title",out.title);out.value=color(style,"value",out.value);
		out.titleSize=integer(style,"titleSize",out.titleSize,8,22);out.valueSize=integer(style,"valueSize",out.valueSize,12,40);
		out.padding=integer(style,"padding",out.padding,6,24);out.gap=integer(style,"gap",out.gap,0,12);
		out.changeGlow=integer(style,"changeGlow",out.changeGlow,0,100);
		out.changeParticles=integer(style,"changeParticles",out.changeParticles,0,12);
		out.changePattern=integer(style,"changePattern",out.changePattern,0,7);out.changeParticleShape=integer(style,"changeParticleShape",out.changeParticleShape,0,5);
		if(style.contains("changeSweep")) {if(!style.value("changeSweep").isBool())throw std::invalid_argument("Invalid change sweep");out.changeSweep=style.value("changeSweep").toBool();}
		if(style.contains("cards")) {if(!style.value("cards").isBool())throw std::invalid_argument("Invalid card layout");out.cards=style.value("cards").toBool();}
		out.backgroundCrop=crop(style,"backgroundCrop",out.backgroundCrop);out.iconCrop=crop(style,"iconCrop",out.iconCrop);
		out.backgroundEdge=integer(style,"backgroundEdge",0,0,400);
		out.backgroundBands=integer(style,"backgroundBands",1,1,5);
		out.nameStart=integer(style,"nameStart",0,0,400);
		out.badgeBox=crop(style,"badgeBox",out.badgeBox);
		out.backgroundBox=crop(style,"backgroundBox",out.backgroundBox);
		if(style.contains("badgeAccent")) {if(!style.value("badgeAccent").isBool())throw std::invalid_argument("Invalid badge accent");out.badgeAccent=style.value("badgeAccent").toBool();}
		if(style.contains("rowCrops")) {
			if(!style.value("rowCrops").isArray() || style.value("rowCrops").toArray().size()!=5)throw std::invalid_argument("Invalid row crop atlas");
			const auto crops=style.value("rowCrops").toArray();for(int n=0;n<5;++n)out.rowCrops[n]=crop(QJsonObject{{"crop",crops[n]}},"crop",{});
		}
		out.cardBackground=color(style,"cardBackground",out.cardBackground);out.cardEdge=color(style,"cardEdge",out.cardEdge);
		out.cardRadius=integer(style,"cardRadius",out.cardRadius,0,24);out.cardGlow=integer(style,"cardGlow",out.cardGlow,0,8);
		out.cardInset=integer(style,"cardInset",out.cardInset,10,120);out.cardTop=integer(style,"cardTop",out.cardTop,5,40);out.cardHeight=integer(style,"cardHeight",out.cardHeight,30,80);
		if(out.cardTop+out.cardHeight>95)throw std::invalid_argument("Cards outside bar");
		if(style.contains("cardEdges")) {
			if(!style.value("cardEdges").isArray() || style.value("cardEdges").toArray().size()!=6)throw std::invalid_argument("Invalid card edge palette");
			const auto palette=style.value("cardEdges").toArray();for(int n=0;n<6;++n)out.cardEdges[n]=color(QJsonObject{{"color",palette[n]}},"color",out.cardEdge);
		}
		if(style.contains("titleColors")) {
			if(!style.value("titleColors").isArray() || style.value("titleColors").toArray().size()!=6)throw std::invalid_argument("Invalid title palette");
			const auto palette=style.value("titleColors").toArray();for(int n=0;n<6;++n)out.titleColors[n]=color(QJsonObject{{"color",palette[n]}},"color",out.title);
		}
		if(style.contains("changeColors")) {
			if(!style.value("changeColors").isArray() || style.value("changeColors").toArray().size()!=6)throw std::invalid_argument("Invalid change palette");
			const auto palette=style.value("changeColors").toArray();for(int n=0;n<6;++n)out.changeColors[n]=color(QJsonObject{{"color",palette[n]}},"color",out.cardEdge);
		}
		if(style.contains("iconImage")) {out.iconImage=string(style,"iconImage",128);if(!visual::AssetManager::validReference(out.iconImage))throw std::invalid_argument("Invalid icon reference");}
		if(style.contains("backgroundImage")) {
			out.backgroundImage=string(style,"backgroundImage",128);
			if(!visual::AssetManager::validReference(out.backgroundImage))throw std::invalid_argument("Invalid asset reference");
		}
		}
		if(item.contains("chatStyle")) {
			if(!item.value("chatStyle").isObject())throw std::invalid_argument("Invalid chat style");
			definition.chatStyle.emplace();auto &out=*definition.chatStyle;const auto style=item.value("chatStyle").toObject();
			if(style.contains("atlas")){out.atlas=string(style,"atlas",128);if(!visual::AssetManager::validReference(out.atlas))throw std::invalid_argument("Invalid chat artwork");}
			if(style.contains("family"))out.family=string(style,"family",128);
			out.crop=crop(style,"crop",out.crop);
			out.name=color(style,"name",out.name);out.message=color(style,"message",out.message);out.cell=color(style,"cell",out.cell);out.edge=color(style,"edge",out.edge);
			out.accent=color(style,"accent",out.accent);
			out.insetX=integer(style,"insetX",0,0,64);out.insetY=integer(style,"insetY",0,0,32);out.cap=integer(style,"cap",120,10,400);
			out.capY=integer(style,"capY",0,0,400);out.corner=integer(style,"corner",0,0,128);
			out.padding=integer(style,"padding",12,4,16);out.gap=integer(style,"gap",6,0,16);
			out.radius=integer(style,"radius",9,0,24);out.glow=integer(style,"glow",0,0,8);out.bevel=integer(style,"bevel",0,0,16);
			if(style.contains("scanlines")){if(!style.value("scanlines").isBool())throw std::invalid_argument("Invalid chat scanlines");out.scanlines=style.value("scanlines").toBool();}
			if(style.contains("rowCards")){if(!style.value("rowCards").isBool())throw std::invalid_argument("Invalid chat row cards");out.rowCards=style.value("rowCards").toBool();}
			if(style.contains("circuit")){if(!style.value("circuit").isBool())throw std::invalid_argument("Invalid chat circuit");out.circuit=style.value("circuit").toBool();}
			if(style.contains("preserveCornerAspect")){if(!style.value("preserveCornerAspect").isBool())throw std::invalid_argument("Invalid chat corner aspect");out.preserveCornerAspect=style.value("preserveCornerAspect").toBool();}
			if(style.contains("fractured")){if(!style.value("fractured").isBool())throw std::invalid_argument("Invalid chat fractures");out.fractured=style.value("fractured").toBool();}
			if(style.contains("fillBehindRows")){if(!style.value("fillBehindRows").isBool())throw std::invalid_argument("Invalid chat row backdrop");out.fillBehindRows=style.value("fillBehindRows").toBool();}
			if(style.contains("painted")){if(!style.value("painted").isBool())throw std::invalid_argument("Invalid chat painted edges");out.painted=style.value("painted").toBool();}
			if(style.contains("riveted")){if(!style.value("riveted").isBool())throw std::invalid_argument("Invalid chat riveted rails");out.riveted=style.value("riveted").toBool();}
			if(style.contains("holographic")){if(!style.value("holographic").isBool())throw std::invalid_argument("Invalid chat holographic glass");out.holographic=style.value("holographic").toBool();}
			if(!out.rowCards && out.atlas.isEmpty())throw std::invalid_argument("Chat panel artwork required");
		}
		if(item.contains("alertStyle")) {
			if(!item.value("alertStyle").isObject())throw std::invalid_argument("Invalid alert style");
			definition.alertStyle.emplace();auto &out=*definition.alertStyle;const auto style=item.value("alertStyle").toObject();
			if(style.contains("atlas")){out.atlas=string(style,"atlas",128);if(!visual::AssetManager::validReference(out.atlas))throw std::invalid_argument("Invalid alert atlas");}
			for(const auto &entry:{std::pair{"burstImage",&out.burstImage},std::pair{"ringImage",&out.ringImage}})if(style.contains(entry.first)){*entry.second=string(style,entry.first,128);if(!visual::AssetManager::validReference(*entry.second))throw std::invalid_argument("Invalid alert layer reference");}
			out.family=string(style,"family",128);out.particles=integer(style,"particles",12,0,12);
			out.energyPattern=integer(style,"energyPattern",0,0,9);
			out.headlineColor=color(style,"headlineColor",out.headlineColor);out.captionColor=color(style,"captionColor",out.captionColor);
			out.burstCrop=crop(style,"burstCrop",out.burstCrop);out.ringCrop=crop(style,"ringCrop",out.ringCrop);
			if(style.contains("iconAnimations")) {
				const auto clips=style.value("iconAnimations").toArray();if(clips.size()!=6)throw std::invalid_argument("Invalid alert animation list");
				for(int n=0;n<6;++n){out.iconAnimations[n]=clips[n].toString();if(!visual::AssetManager::validReference(out.iconAnimations[n]))throw std::invalid_argument("Invalid alert animation reference");}
			}
			for(const auto *key:{"colors","headlines","captions"})if(!style.value(key).isArray() || style.value(key).toArray().size()!=6)throw std::invalid_argument("Invalid alert palette/captions");
			if(style.contains("iconCrops") && (!style.value("iconCrops").isArray() || style.value("iconCrops").toArray().size()!=6))throw std::invalid_argument("Invalid alert crops");
			for(int n=0;n<6;++n){out.colors[n]=color(QJsonObject{{"color",style.value("colors").toArray()[n]}},"color",Qt::white);out.headlines[n]=string(QJsonObject{{"text",style.value("headlines").toArray()[n]}},"text",32);out.captions[n]=string(QJsonObject{{"text",style.value("captions").toArray()[n]}},"text",32);
				if(style.contains("iconCrops"))out.iconCrops[n]=crop(QJsonObject{{"crop",style.value("iconCrops").toArray()[n]}},"crop",{});
				if(out.iconAnimations[n].isEmpty() && (out.atlas.isEmpty() || out.iconCrops[n].isEmpty()))throw std::invalid_argument("Alert icon resource missing");}
		}
		if(item.contains("shoutoutStyle")) {
			if(!item["shoutoutStyle"].isObject())throw std::invalid_argument("Invalid shoutout style");
			definition.shoutoutStyle.emplace();auto &out=*definition.shoutoutStyle;const auto style=item["shoutoutStyle"].toObject();
			out.motionPattern=integer(style,"motionPattern",-1,-1,9);
			out.atlas=string(style,"atlas",128);if(!visual::AssetManager::validReference(out.atlas))throw std::invalid_argument("Invalid shoutout artwork");
			out.family=string(style,"family",128);out.name=color(style,"name",out.name);out.details=color(style,"details",out.details);
			for(const auto &[key,box]:{std::pair{"assetBox",&out.assetBox},std::pair{"portraitBox",&out.portraitBox},std::pair{"nameBox",&out.nameBox},std::pair{"categoryBox",&out.categoryBox},std::pair{"followersBox",&out.followersBox}}){*box=crop(style,key,*box);if(box->right()>=800 || box->bottom()>=800)throw std::invalid_argument("Shoutout layout outside 800 square");}
			for(const auto &[key,value]:{std::pair{"circular",&out.circular},std::pair{"hexagonal",&out.hexagonal}})if(style.contains(key)){if(!style[key].isBool())throw std::invalid_argument("Invalid shoutout portrait shape");*value=style[key].toBool();}
		}
		if(item.contains("hypeStyle")) {
			if(!item["hypeStyle"].isObject())throw std::invalid_argument("Invalid Hype style");
			definition.hypeStyle.emplace();auto &out=*definition.hypeStyle;const auto style=item["hypeStyle"].toObject();
			out.family=string(style,"family",128);out.titleFamily=string(style,"titleFamily",128);
			out.accent=color(style,"accent",out.accent);out.secondary=color(style,"secondary",out.secondary);
			out.labels=color(style,"labels",out.labels);out.numbers=color(style,"numbers",out.numbers);
			out.pattern=integer(style,"pattern",0,0,9);out.particles=integer(style,"particles",18,0,24);
			for(const auto &[key,value]:{std::pair{"fillEnd",&out.fillEnd},std::pair{"levelColor",&out.levelColor},std::pair{"levelLabel",&out.levelLabel},std::pair{"headerColor",&out.headerColor},std::pair{"timeLabel",&out.timeLabel}})*value=color(style,key,*value);
			for(const auto &[key,value]:{std::pair{"atlas",&out.atlas},std::pair{"thanksAtlas",&out.thanksAtlas}})if(style.contains(key)){*value=string(style,key,128);if(!visual::AssetManager::validReference(*value))throw std::invalid_argument("Invalid Hype artwork");}
			for(const auto &[key,box]:{std::pair{"atlasCrop",&out.atlasCrop},std::pair{"thanksCrop",&out.thanksCrop},std::pair{"titleBox",&out.titleBox},std::pair{"levelBox",&out.levelBox},std::pair{"barBox",&out.barBox},std::pair{"timerBox",&out.timerBox},std::pair{"leaderBox",&out.leaderBox},std::pair{"percentBox",&out.percentBox}})*box=crop(style,key,*box);
		}
		if(definition.id=="minimal") {result.minimal_=result.definitions_.size();minimal=true;}
		result.definitions_.push_back(std::move(definition));
	}
	if(!minimal)throw std::invalid_argument("Minimal fallback missing");
	return result;
}
const Definition *Registry::find(const QString &id) const noexcept {
	for(const auto &item:definitions_)if(item.id==id)return &item;return nullptr;
}
const Definition &Registry::resolve(const QString &id) const noexcept {
	if(const auto *item=find(id))return *item;return definitions_[minimal_];
}
}
