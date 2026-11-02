// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "state/shared-state.hpp"
#include "visual/visual-engine.hpp"
#include "visual/motion.hpp"
#include "presets/preset-registry.hpp"
#include <array>
namespace oto::sources {
enum class ListEntrance { Appearance, Left, Right, Top };
struct ListSettings {
	int width=600, rows=8, rowHeight=64;
	int durationMs=6000;
	int backgroundOpacity=100; // Dark fill only; inherited artwork/text stays intact.
	std::array<bool,5> types{true,true,true,true,true};
	bool animate=true;
	ListEntrance entrance=ListEntrance::Appearance;
	QString family=QStringLiteral("Segoe UI");
	QString labelFamily; // Empty inherits the preset; event badges only.
	int namePixelSize=0,labelPixelSize=0; // Zero inherits the preset.
	QColor nameColor{Qt::white},labelColor{Qt::white};
	bool inheritNameColor=true,inheritLabelColor=true;
	QString presetId=QStringLiteral("minimal");
	presets::Style style;
	ListSettings() {style.title=QColor(222,239,255);style.valueSize=18;style.titleSize=15;style.cardBackground=QColor(6,12,21,239);style.cardEdge=QColor(166,204,230,210);style.cardRadius=9;style.cardGlow=2;style.changeGlow=35;style.changeParticles=2;style.changePattern=6;}
	int height() const noexcept { return 24+rows*(rowHeight+8); }
	bool operator==(const ListSettings &b) const { return width==b.width && rows==b.rows && rowHeight==b.rowHeight && durationMs==b.durationMs && backgroundOpacity==b.backgroundOpacity && types==b.types && animate==b.animate && entrance==b.entrance && family==b.family && labelFamily==b.labelFamily && namePixelSize==b.namePixelSize && labelPixelSize==b.labelPixelSize && nameColor==b.nameColor && labelColor==b.labelColor && inheritNameColor==b.inheritNameColor && inheritLabelColor==b.inheritLabelColor && presetId==b.presetId && style==b.style; }
};
struct ListLabels {
	QString anonymous, bits, gifts, viewers, subscription, months;
	QString empty=QStringLiteral("No saved events for these filters");
	std::array<QString,5> badges{"FOLLOW","SUB","BITS","GIFTED","RAID"};
	bool operator==(const ListLabels &b) const { return anonymous==b.anonymous && bits==b.bits && gifts==b.gifts && viewers==b.viewers && subscription==b.subscription && months==b.months && empty==b.empty && badges==b.badges; }
};
struct ListItem {
	events::EventPtr event;
	QString name, badge, detail;
	bool historical=false;
};
struct ListView {
	std::uint64_t generation=0;
	state::ChannelKey channel{events::EventOrigin::Simulation,"sim-channel"};
	std::vector<ListItem> items;
};
ListView eventListView(const state::SnapshotPtr &snapshot, const ListSettings &settings, const ListLabels &labels,
	const state::ChannelKey &channel, bool previewTail=false);
struct ListRow {
	ListItem item;
	visual::FramePtr base, name;
	QColor accent;
	int nameX=0, nameWidth=0, scrollDistance=0;
	float y=0, fromY=0;
	visual::MotionClock::time_point moved{}, entered{}, scrollStarted{}, exitStarted{};
};
struct ListFrame {
	ListSettings settings;
	ListLabels labels;
	ListView view;
	std::vector<ListRow> rows;
	std::vector<ListRow> exiting; // Bounded retained cards, drawn only during the scaled exit.
	bool hasScrolling=false;
	visual::ImagePtr background;
	visual::FramePtr emptyState;
};
using ListFramePtr=std::shared_ptr<const ListFrame>;
inline double listShiftDuration(int durationMs) noexcept { return durationMs/12.0; }
float listRowY(const ListRow &row, visual::MotionClock::time_point now,int durationMs=6000) noexcept;
// TEST seeds a quiet full list before inserting its newest event. Other refreshes are quiet.
ListFramePtr prepareEventListFrame(visual::VisualEngine &engine, const ListSettings &settings, const ListLabels &labels,
	const ListView &view, const ListFramePtr &previous, visual::MotionClock::time_point now, bool demoEntrance=false, visual::ImagePtr background={});
struct ListEntrySample { float card=1, text=1, offsetX=0, glow=0, particles=0, sweep=0; bool finished=true; float offsetY=0, progress=1; };
ListEntrySample sampleListEntry(double elapsedMs,ListEntrance entrance=ListEntrance::Appearance,int width=600,int rowHeight=64,int durationMs=6000) noexcept;
}
