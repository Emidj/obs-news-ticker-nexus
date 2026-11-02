// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "state/shared-state.hpp"
#include "visual/visual-engine.hpp"
#include "visual/motion.hpp"
#include "presets/preset-registry.hpp"
namespace oto::sources {
struct AlertTimes { int entry=1500,hold=3000,exit=1500; int total() const noexcept{return entry+hold+exit;} };
struct AlertSettings {
	AlertTimes times;
	std::array<bool,5> types{true,true,true,true,true};
	QString presetId=QStringLiteral("neon-dj"),nameFamily,labelFamily;
};
struct AlertSample {
	float portrait=0,burst=0,title=0,name=0,effects=0,scale=1,offsetX=0,exitProgress=0;
	double visualMs=0; // Continuous forward clock, reversed from the held pose.
	bool finished=true;
};
AlertSample sampleAlert(double elapsedMs,AlertTimes times) noexcept;
int alertVariant(const events::EventPayload &payload) noexcept;
// A position in the one shared bounded activity. No per-source backlog/queue.
class AlertCursor {
public:
	bool synchronize(const state::SnapshotPtr &snapshot,const state::ChannelKey &channel,bool history=false);
	void reset() noexcept {initialized_=false;}
	void skip(const state::SnapshotPtr &snapshot) noexcept;
	const state::LatestEvent *next(const state::SnapshotPtr &snapshot,const std::array<bool,5> &types) noexcept;
	std::uint64_t delivery() const noexcept{return delivery_;}
private:
	state::ChannelKey channel_{events::EventOrigin::Simulation,{}};
	std::uint64_t generation_=0,delivery_=0;bool initialized_=false;
};
struct AlertFrame {
	events::EventPtr event;
	state::TextPtr displayName;
	visual::ImagePtr profileImage;
	AlertSettings settings;
	visual::MotionClock::time_point started;
	visual::FramePtr portrait,emblem,heading,caption,name,badge,detail;
	float scrollDistance=0;
	int nameViewport=440; // Centred at x=500; measured once when preparing.
	QColor accent;
	int variant=0,particles=12,energyPattern=0;
	bool animatedEmblem=false;
};
using AlertFramePtr=std::shared_ptr<const AlertFrame>;
// The clip is loaded once on the GUI thread, then sampled on the GPU.
QRect alertIconCell(double elapsedMs) noexcept;
AlertFramePtr prepareAlertFrame(visual::VisualEngine &engine,const AlertSettings &settings,const presets::AlertStyle &style,const state::LatestEvent &event,visual::ImagePtr atlas,visual::ImagePtr portrait,visual::MotionClock::time_point started,visual::ImagePtr iconClip={});
}
