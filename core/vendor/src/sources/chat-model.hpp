// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "state/shared-state.hpp"
#include "visual/visual-engine.hpp"
#include "visual/motion.hpp"
#include <tuple>
#include "chat/emote-cache.hpp"
#include "presets/preset-registry.hpp"
namespace oto::sources {
enum class ChatLayout { Vertical, Horizontal };
struct ChatSettings {
	QString presetId="minimal";presets::ChatStyle style;
	int width=600,height=700,messages=8,entryMs=350,backgroundOpacity=85;
	ChatLayout layout=ChatLayout::Vertical;bool showCommands=true,twitchColors=true,thirdParty=true,animatedEmotes=true,showFrame=true;
	QString nameFamily="Segoe UI",messageFamily="Segoe UI";int nameSize=22,messageSize=22;
	QColor nameColor{100,215,245},messageColor{235,244,255};
	bool operator==(const ChatSettings &o)const{return presetId==o.presetId && style==o.style && std::tie(width,height,messages,entryMs,backgroundOpacity,layout,showCommands,twitchColors,thirdParty,animatedEmotes,showFrame,nameFamily,messageFamily,nameSize,messageSize,nameColor,messageColor)==std::tie(o.width,o.height,o.messages,o.entryMs,o.backgroundOpacity,o.layout,o.showCommands,o.twitchColors,o.thirdParty,o.animatedEmotes,o.showFrame,o.nameFamily,o.messageFamily,o.nameSize,o.messageSize,o.nameColor,o.messageColor);}
};
struct ChatView {state::ChannelKey channel{events::EventOrigin::Simulation,""};std::uint64_t generation=0;std::vector<state::LatestEvent> messages;state::ChatModerationPtr moderation;};
ChatView chatView(const state::SnapshotPtr &,const state::ChannelKey &,const ChatSettings &,std::uint64_t afterDelivery=0);
struct ChatRow {
	struct InlineEmote {QRectF box;chat::EmotePtr media;QString url,text;bool layer=false;unsigned effects=0;QRectF layerRect{0,0,1,1};};
	struct BodyPage {QPointF position;visual::FramePtr graphic;};
	struct Badge {QRectF box;chat::EmotePtr media;QString url;};
	events::EventPtr event;visual::FramePtr graphic;QPointF position,from;
	std::vector<InlineEmote> emotes;
	std::vector<Badge> badges;
	std::vector<BodyPage> bodyPages;QRectF bodyBox;QSize bodySize;
	visual::MotionClock::time_point moved{},entered{};
	float bodyScale=1;int bodyPixelSize=22,lines=0;bool retiring=false;
};
struct ChatFrame {ChatSettings settings;ChatView view;std::vector<ChatRow> rows;visual::ImagePtr artwork;visual::FramePtr border;QRect content;std::uint64_t mediaRevision=0;visual::MotionClock::time_point mediaClock{};};
using ChatFramePtr=std::shared_ptr<const ChatFrame>;
struct ChatPose {QPointF position;float opacity=1;bool finished=true;};
ChatPose sampleChatRow(const ChatRow &,int entryMs,ChatLayout,visual::MotionClock::time_point);
ChatFramePtr prepareChatFrame(visual::VisualEngine &,const ChatSettings &,const ChatView &,const ChatFramePtr &,visual::MotionClock::time_point,bool preview=false,chat::EmoteCache *media=nullptr,visual::ImagePtr artwork={});
}
