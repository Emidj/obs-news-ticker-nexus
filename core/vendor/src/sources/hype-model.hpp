// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "state/shared-state.hpp"
#include "visual/visual-engine.hpp"
#include "presets/preset-registry.hpp"
#include "visual/motion.hpp"
#include <tuple>
namespace oto::sources {
struct HypeSettings {
	int width=900,height=180,backgroundOpacity=85,entranceMs=2500,exitMs=1500,holdMs=4000;
	QString presetId="neon-dj";presets::HypeStyle style;
	QString numberFamily="Segoe UI",labelFamily="Segoe UI";
	int numberSize=0,labelSize=0;
	int fillMode=2; // 0 fixed, 1 green/yellow/red gradient, 2 live style energy.
	bool artworkTitle=true,numbersFromStyle=true,labelsFromStyle=true;
	QColor accent{0,205,235},numbers{Qt::white},labels{185,203,218};
	bool operator==(const HypeSettings &o)const{return style==o.style && std::tie(presetId,width,height,backgroundOpacity,entranceMs,exitMs,holdMs,numberFamily,labelFamily,numberSize,labelSize,fillMode,artworkTitle,numbersFromStyle,labelsFromStyle,accent,numbers,labels)==std::tie(o.presetId,o.width,o.height,o.backgroundOpacity,o.entranceMs,o.exitMs,o.holdMs,o.numberFamily,o.labelFamily,o.numberSize,o.labelSize,o.fillMode,o.artworkTitle,o.numbersFromStyle,o.labelsFromStyle,o.accent,o.numbers,o.labels);}
};
struct HypeLabels {QString title="HYPE TRAIN",level="LEVEL",ended="THANK YOU",waiting="AWAITING TWITCH",time="TIME LEFT",contributors="TOP CONTRIBUTORS";};
struct HypeView {
	bool visible=false,ended=false,waiting=false,preview=false;
	std::string trainId;state::ChannelKey channel{events::EventOrigin::Simulation,""};
	std::uint32_t level=1;std::uint64_t progress=0,goal=0,total=0;
	std::int64_t remaining=0;std::chrono::system_clock::time_point updatedAt{},expiresAt{};
	double ratio=0;
	std::vector<events::HypeContribution> contributors;
};
HypeView hypeView(const state::SnapshotPtr &,const state::ChannelKey &,std::chrono::system_clock::time_point now,int holdMs=4000,int exitMs=1500);
struct HypeFrame {
	HypeSettings settings;HypeView view;
	visual::FramePtr base,fill,title,thanks,labels,level,numbers,timer;
	struct Name{visual::FramePtr frame;QRect box;float distance=0;};std::array<Name,3> names;
	QRect bar,titleBox,levelBox,timerBox,leaderBox;bool authored=false;
	visual::MotionClock::time_point changed{},appeared{},ended{},celebrated{},clockStart{};
	double fromRatio=0;
};
using HypeFramePtr=std::shared_ptr<const HypeFrame>;
std::array<QString,2> hypeContributionLabels(const events::HypeContribution &);
double hypeFill(const HypeFrame &,visual::MotionClock::time_point);
HypeFramePtr prepareHypeFrame(visual::VisualEngine &,const HypeSettings &,const HypeLabels &,const HypeView &,const HypeFramePtr &,visual::MotionClock::time_point,bool animateEntrance=false,visual::ImagePtr artwork={},visual::ImagePtr thanks={});
}
