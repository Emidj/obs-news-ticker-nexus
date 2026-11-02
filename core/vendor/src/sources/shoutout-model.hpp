// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "alert-model.hpp"
namespace oto::sources {
struct ShoutoutSettings {
	AlertTimes times{2000,6000,700};
	QString nameFamily,labelFamily;
	QString preset="neon-dj";
	QColor nameColor{Qt::white},detailsColor{Qt::white};
	bool inheritNameColor=true,inheritDetailsColor=true;
};
struct ShoutoutSample {float opacity=0,portrait=0,data=0,dataScale=1;bool finished=true;};
ShoutoutSample sampleShoutout(double elapsed,AlertTimes times) noexcept;
class ShoutoutCursor {
public:
	bool synchronize(const state::SnapshotPtr &,const state::ChannelKey &,bool preview=false);
	const state::LatestEvent *next(const state::SnapshotPtr &) noexcept;
	void reset() noexcept {initialized_=false;}
private:
	state::ChannelKey channel_{events::EventOrigin::Simulation,{}};
	std::uint64_t generation_=0,delivery_=0;bool initialized_=false;
};
struct ShoutoutFrame {
	state::LatestEvent event;
	visual::ImagePtr profile;
	ShoutoutSettings settings;
	visual::FramePtr base,name,portrait,details;
	presets::ShoutoutStyle style;
	visual::MotionClock::time_point started;
	float scrollDistance=0;
	int namePixelSize=0;
};
using ShoutoutFramePtr=std::shared_ptr<const ShoutoutFrame>;
ShoutoutFramePtr prepareShoutoutFrame(visual::VisualEngine &,const ShoutoutSettings &,const presets::ShoutoutStyle &,
	const state::LatestEvent &,visual::ImagePtr,visual::MotionClock::time_point,visual::ImagePtr artwork={});
}
