// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "supporter-model.hpp"
#include "visual/visual-engine.hpp"
#include "visual/motion.hpp"
#include <optional>
namespace oto::sources {
struct BarLayer {
	visual::FramePtr frame;
	int x = 0, y = 36;
	visual::MotionClock::time_point started{};
	visual::FramePtr previousValue; // At most one outgoing texture; rapid updates replace it.
	QRect card;
	int viewportWidth = 0, scrollDistance = 0;
	visual::MotionClock::time_point scrollStarted{};
	float previousOffsetX = 0;
};
struct BarFrame {
	BarSettings settings;
	BarLabels labels;
	std::array<QString,6> values;
	visual::FramePtr base;
	std::array<BarLayer,6> layers;
	visual::ImagePtr background;
	visual::ImagePtr icons;
	bool hasScrolling = false;
	std::optional<BarContext> context;
};
using BarFramePtr = std::shared_ptr<const BarFrame>;
// GUI-only selective preparation. Returns previous unchanged when no visible data changed.
BarFramePtr prepareSupporterFrame(visual::VisualEngine &engine, const BarSettings &settings,
	const BarLabels &labels, const std::array<QString,6> &values, const BarFramePtr &previous,
	visual::MotionClock::time_point now, visual::ImagePtr background = {}, visual::ImagePtr icons = {}, std::optional<BarContext> context = {});
}
