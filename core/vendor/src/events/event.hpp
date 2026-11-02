// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <utility>
#include <vector>

namespace oto::events {
enum class EventType { Follow, Sub, Cheer, Gift, Raid, Chat, HypeTrain, Shoutout, ChatModeration };
enum class EventOrigin { Simulation, Preview, Twitch };
enum class SubscriptionTier { Tier1, Tier2, Tier3, Prime };
enum class HypeTrainStage { Begin, Progress, End };

struct User {
	std::string id;
	std::string displayName;
};
struct FollowEvent { User follower; };
struct SubEvent {
	User subscriber;
	SubscriptionTier tier = SubscriptionTier::Tier1;
	std::uint32_t totalMonths = 1;
	bool countsTowardTotals = true;
};
struct CheerEvent { std::optional<User> user; std::uint64_t bits = 0; };
struct GiftEvent {
	std::optional<User> gifter;
	std::uint64_t count = 0;
	SubscriptionTier tier = SubscriptionTier::Tier1;
};
struct RaidEvent { User raider; std::uint64_t viewers = 0; std::string login; };
enum class ChatRole { Viewer, Streamer, Moderator, Subscriber, Vip };
struct ChatFragment { std::string text,emoteId; bool animated=false;std::string cheerPrefix;std::uint64_t cheerBits=0,cheerTier=0; };
struct ChatBadge { std::string setId,version,info; };
struct ChatEvent { User author; std::string message; bool canShoutout = false; std::string color; ChatRole role=ChatRole::Viewer; std::vector<ChatFragment> fragments; std::vector<ChatBadge> badges; std::string messageId; };
enum class ChatModerationAction { DeleteMessage, ClearUser, ClearAll };
struct ChatModerationEvent { ChatModerationAction action=ChatModerationAction::ClearAll; std::string messageId,userId; };
struct ShoutoutEvent { User target; std::string login; std::string category; std::optional<std::uint64_t> followers; };
struct HypeContribution {
	User user;std::uint64_t points=0;
	std::uint8_t methods=0; // 1 Bits, 2 subscription (including gifts), 4 other; may combine.
	std::uint64_t bits=0,subscriptionPoints=0;
	// Counts come from actual subscription/gift events, never points / 500.
	std::optional<std::uint64_t> subscriptions;
	bool operator==(const HypeContribution &o)const{return user.id==o.user.id && user.displayName==o.user.displayName && points==o.points && methods==o.methods && bits==o.bits && subscriptionPoints==o.subscriptionPoints && subscriptions==o.subscriptions;}
};
struct HypeTrainEvent {
	HypeTrainStage stage = HypeTrainStage::Begin;
	std::uint32_t level = 1;
	std::uint64_t total = 0;
	std::uint64_t goal = 0;
	std::string trainId;
	std::optional<std::uint64_t> progress;
	std::chrono::system_clock::time_point startedAt{}, expiresAt{};
	std::vector<HypeContribution> contributors;
};
using EventPayload = std::variant<FollowEvent, SubEvent, CheerEvent, GiftEvent, RaidEvent, ChatEvent, HypeTrainEvent, ShoutoutEvent, ChatModerationEvent>;
inline constexpr std::size_t eventTypeCount=std::variant_size_v<EventPayload>;

// Input owned by value: normalization can move it without retaining producer memory.
struct EventDraft {
	std::string id;
	std::string channelId;
	std::chrono::system_clock::time_point occurredAt;
	EventOrigin origin = EventOrigin::Simulation;
	EventPayload payload;
};

class EventEngine;
class Event final {
public:
	const EventDraft &data() const noexcept { return data_; }
private:
	friend class EventEngine;
	explicit Event(EventDraft draft) : data_(std::move(draft)) {}
	EventDraft data_;
};
using EventPtr = std::shared_ptr<const Event>;

EventType typeOf(const EventPayload &payload);
const char *typeName(EventType type) noexcept;
} // namespace oto::events
