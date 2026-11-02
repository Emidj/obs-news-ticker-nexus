// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QColor>
#include <QString>
#include <QByteArray>
#include <vector>
#include <tuple>
#include <QRect>
#include <array>
#include <optional>
namespace oto::presets {
struct Style {
	QString family = QStringLiteral("Segoe UI");
	QColor background{19,23,31,230}, title{162,175,195}, value{Qt::white};
	int titleSize = 14, valueSize = 24, padding = 12, gap = 6;
	QString backgroundImage;
	bool cards = false;
	QString iconImage;
	QRect backgroundCrop{0,0,1000,1000}, iconCrop{0,0,1000,1000}; // Thousandths of asset size.
	int backgroundEdge = 0;
	int backgroundBands = 1, nameStart = 0;
	QRect badgeBox{160,200,140,600}; // Thousandths of row canvas for decorated lists.
	QRect backgroundBox{130,130,820,740}; // Dark interior; preserves perimeter and icons.
	bool badgeAccent=false; // Plain illuminated labels on dark industrial cards.
	std::array<QRect,5> rowCrops{}; // Optional authored atlas crops, Follow/Sub/Bits/Gift/Raid.
	QColor cardBackground{10,9,24,225},cardEdge{112,65,155,180};
	int cardInset=75,cardTop=27,cardHeight=47;
	int cardRadius=5,cardGlow=0;
	std::array<QColor,6> cardEdges{};
	std::array<QColor,6> titleColors{};
	std::array<QColor,6> changeColors{};
	int changeGlow = 70, changeParticles = 8;
	bool changeSweep = true;
	int changePattern=0,changeParticleShape=0;
	bool operator==(const Style &other) const {
		return std::tie(family,background,title,value,titleSize,valueSize,padding,gap,backgroundImage,cards,iconImage,backgroundCrop,iconCrop,backgroundEdge,backgroundBands,nameStart,badgeBox,backgroundBox,badgeAccent,rowCrops,cardBackground,cardEdge,cardInset,cardTop,cardHeight,cardEdges,titleColors,changeGlow,changeParticles,changeSweep,changeColors,cardRadius,cardGlow,changePattern,changeParticleShape) ==
			std::tie(other.family,other.background,other.title,other.value,other.titleSize,other.valueSize,other.padding,other.gap,other.backgroundImage,other.cards,other.iconImage,other.backgroundCrop,other.iconCrop,other.backgroundEdge,other.backgroundBands,other.nameStart,other.badgeBox,other.backgroundBox,other.badgeAccent,other.rowCrops,other.cardBackground,other.cardEdge,other.cardInset,other.cardTop,other.cardHeight,other.cardEdges,other.titleColors,other.changeGlow,other.changeParticles,other.changeSweep,other.changeColors,other.cardRadius,other.cardGlow,other.changePattern,other.changeParticleShape);
	}
	bool operator!=(const Style &other) const { return !(*this==other); }
};
struct AlertStyle {
	QString family=QStringLiteral("Segoe UI Black"),atlas,burstImage,ringImage;
	std::array<QColor,6> colors{};
	std::array<QString,6> headlines{},captions{};
	std::array<QRect,6> iconCrops{};
	std::array<QString,6> iconAnimations{}; // Optional 8x6, 128 px cells, 24 fps sheets.
	QRect burstCrop{0,0,500,500},ringCrop{500,0,500,500};
	int particles=12;
	int energyPattern=0; // 0 plasma; 1 angular/circuits; 2 CRT cabinet/phosphor; 3 orbital machinery; 4 fractured horror; 5 luminous love; 6 marker paint/splashes; 7 minimal silver lines; 8 urban neon; 9 holographic projection.
	QColor headlineColor,captionColor; // Invalid => accent/white respectively.
};
struct ShoutoutStyle {
	int motionPattern=-1; // Optional native artwork motion; shared shader, 0..9.
	QString atlas,family=QStringLiteral("Segoe UI");
	QColor name{Qt::white},details{Qt::white};
	QRect assetBox{20,200,760,400},portraitBox{80,300,200,200},nameBox{330,355,330,48},categoryBox{355,407,300,30},followersBox{355,442,300,30};
	bool circular=true,hexagonal=false;
};
struct HypeStyle {
	QString family="Segoe UI",titleFamily="Segoe UI Black";
	QString atlas,thanksAtlas;
	QRect atlasCrop{0,0,1000,1000},thanksCrop{0,0,1000,1000};
	// Thousandths of the source; authored ornaments and live data share one layout.
	QRect titleBox{240,0,380,390},levelBox{60,345,85,480},barBox{180,535,330,170},timerBox{625,420,110,430},leaderBox{765,200,205,665};
	QRect percentBox{520,440,85,290};
	QColor accent{0,205,235},secondary{255,0,210},labels{185,203,218},numbers{Qt::white};
	QColor fillEnd,levelColor,levelLabel{Qt::white},headerColor,timeLabel;
	int pattern=0,particles=18;
	bool operator==(const HypeStyle &o)const{return std::tie(family,titleFamily,atlas,thanksAtlas,atlasCrop,thanksCrop,titleBox,levelBox,barBox,timerBox,leaderBox,percentBox,accent,secondary,labels,numbers,fillEnd,levelColor,levelLabel,headerColor,timeLabel,pattern,particles)==std::tie(o.family,o.titleFamily,o.atlas,o.thanksAtlas,o.atlasCrop,o.thanksCrop,o.titleBox,o.levelBox,o.barBox,o.timerBox,o.leaderBox,o.percentBox,o.accent,o.secondary,o.labels,o.numbers,o.fillEnd,o.levelColor,o.levelLabel,o.headerColor,o.timeLabel,o.pattern,o.particles);}
};
struct ChatStyle {
	QString atlas,family="Segoe UI";QRect crop{0,0,1000,1000};
	QColor name{100,215,245},message{235,244,255},cell{6,12,21},edge{166,204,230,175},accent{255,40,85};
	int insetX=0,insetY=0,cap=120,capY=0,corner=0,radius=9,glow=0,bevel=0,padding=12,gap=6;
	bool scanlines=false,rowCards=true,circuit=false,preserveCornerAspect=false,fractured=false,fillBehindRows=true,painted=false,riveted=false,holographic=false;
	bool operator==(const ChatStyle &o)const{return std::tie(atlas,family,crop,name,message,cell,edge,accent,insetX,insetY,cap,capY,corner,radius,glow,bevel,padding,gap,scanlines,rowCards,circuit,preserveCornerAspect,fractured,fillBehindRows,painted,riveted,holographic)==std::tie(o.atlas,o.family,o.crop,o.name,o.message,o.cell,o.edge,o.accent,o.insetX,o.insetY,o.cap,o.capY,o.corner,o.radius,o.glow,o.bevel,o.padding,o.gap,o.scanlines,o.rowCards,o.circuit,o.preserveCornerAspect,o.fractured,o.fillBehindRows,o.painted,o.riveted,o.holographic);}
};
struct Definition { QString id, name; Style style; std::optional<Style> eventListStyle; std::optional<AlertStyle> alertStyle; std::optional<ShoutoutStyle> shoutoutStyle; std::optional<HypeStyle> hypeStyle; std::optional<ChatStyle> chatStyle; };
// Immutable metadata only: constructing a registry never loads images or fonts.
class Registry final {
public:
	Registry(const Registry &) = default;
	Registry(Registry &&) = default;
	static Registry fromJson(const QByteArray &bytes);
	static Registry fromFile(const QString &filename);
	const std::vector<Definition> &entries() const noexcept { return definitions_; }
	const Definition *find(const QString &id) const noexcept;
	const Definition &resolve(const QString &id) const noexcept; // Unknown IDs use Minimal.
private:
	Registry() = default;
	std::vector<Definition> definitions_;
	size_t minimal_ = 0;
};
}
