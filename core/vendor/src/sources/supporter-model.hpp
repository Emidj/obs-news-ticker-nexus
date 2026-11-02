// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "state/shared-state.hpp"
#include "visual/scene.hpp"
#include "visual/motion.hpp"
#include "presets/preset-registry.hpp"
#include <array>
namespace oto::sources {
struct BarSettings {
	QString language=QStringLiteral("auto");
	int width = 1080, height = 96;
	std::array<bool,6> fields{true,true,true,true,true,true};
	visual::Motion motion = visual::Motion::Fade;
	int durationMs = 250;
	QString presetId = QStringLiteral("minimal");
	presets::Style style;
	QString valueFamily; // Empty inherits the preset; independent per source.
	int valuePixelSize=0; // Names only, like valueFamily; zero inherits the preset.
	QString labelFamily; // Titles only; independent from names and numeric values.
	int labelPixelSize=0; // Zero inherits the style; custom size fits inside the card.
};
struct BarLabels {
	std::array<QString,6> titles;
	QString empty, anonymous;
};
// Shared delivery metadata distinguishes live events from restored text.
struct BarContext {
	std::uint64_t generation=0;
	state::ChannelKey channel{events::EventOrigin::Simulation,"sim-channel"};
	std::array<std::uint64_t,4> latestRevision{},delivered{};
	bool preview=false;
	bool operator==(const BarContext &b) const {return generation==b.generation && channel==b.channel && latestRevision==b.latestRevision && delivered==b.delivered && preview==b.preview;}
};
BarContext supporterContext(const state::SnapshotPtr &snapshot,const std::string &previewChannel={},
	const state::ChannelKey &channel={events::EventOrigin::Simulation,"sim-channel"});
std::array<QString,6> supporterValues(const state::SnapshotPtr &snapshot, int today, const BarLabels &labels,
	const std::string &previewChannel = {}, const state::ChannelKey &channel = {events::EventOrigin::Simulation,"sim-channel"});
visual::Scene supporterScene(const BarSettings &settings, const BarLabels &labels, const std::array<QString,6> &values);
struct BarLabelLayout { int pixelSize, height; };
BarLabelLayout supporterLabelLayout(const BarSettings &settings);
inline constexpr int supporterLabelTopInset=2;
int supporterValuePixelSize(const BarSettings &settings,int height,bool name);
}
