// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "events/event.hpp"
#include "text-cache.hpp"
#include "daily-totals.hpp"
#include <array>
#include <map>
#include <vector>

namespace oto::state {
struct ChannelKey {
	events::EventOrigin origin;
	std::string channelId;
	bool operator<(const ChannelKey &other) const noexcept
	{ return origin < other.origin || (origin == other.origin && channelId < other.channelId); }
	bool operator==(const ChannelKey &other) const noexcept { return origin == other.origin && channelId == other.channelId; }
};
struct LatestEvent {
	events::EventPtr event;
	TextPtr displayName; // Null for anonymous actors and Hype Train; UI localizes anonymity.
	std::uint64_t revision = 0;
	// Delivery order, independent of timestamps and batch revision. Alert sources
	// keep only a cursor into this existing bounded shared activity, never a queue.
	std::uint64_t delivery = 0;
};
struct ChatModerationState {
	struct Mark {std::string id;std::chrono::system_clock::time_point at;};
	static constexpr std::size_t capacity=256;
	std::vector<Mark> messages,users;
	std::chrono::system_clock::time_point clearedAt{};
};
using ChatModerationPtr=std::shared_ptr<const ChatModerationState>;
bool chatRemoved(const ChatModerationState &,const events::Event &);
struct ChannelState {
	struct HypeSubscription {std::string userId;std::uint64_t count=0,points=0;std::chrono::system_clock::time_point occurredAt{};};
	// Shared session ledger independent of the 64-row display history. Copied
	// only on subscription events, bounded, never persisted or queried by a source.
	static constexpr std::size_t hypeSubscriptionCapacity=2048;
	std::shared_ptr<const std::vector<HypeSubscription>> hypeSubscriptions;
	// Bounded persisted activity, newest first. Chat/Hype Train must not evict it.
	static constexpr std::size_t activityCapacity = 64;
	static constexpr std::size_t chatCapacity = 64;
	std::vector<LatestEvent> chat; // Session-only; no historical chat API or replay.
	ChatModerationPtr chatModeration; // Session-only, shared by every chat source.
	std::vector<LatestEvent> activity;
	std::uint64_t activityDelivery = 0;
	std::uint64_t revision = 0;
	std::array<LatestEvent, events::eventTypeCount> latest;
	std::array<std::uint64_t, events::eventTypeCount> delivered{};
	// Effective train snapshot. End retains the last known level goal/progress;
	// it must never be resurrected by a delayed Begin/Progress of the same train.
	std::optional<events::HypeTrainEvent> hypeTrain;
	std::chrono::system_clock::time_point hypeUpdatedAt{};
	// Transient bounded presentations, shared by all cards. Never persisted as
	// supporter activity and never restored/replayed after OBS starts.
	static constexpr std::size_t shoutoutCapacity = 16;
	std::vector<LatestEvent> shoutouts;
	std::uint64_t sessionBits = 0, sessionSubs = 0, sessionGifts = 0;
	bool saturated = false;
	DailyTotals daily;
};
struct Snapshot {
	std::uint64_t generation = 0;
	std::uint64_t revision = 0;
	std::map<ChannelKey, std::shared_ptr<const ChannelState>> channels;
};
using SnapshotPtr = std::shared_ptr<const Snapshot>;
using DailyBook = std::map<ChannelKey, DailyTotals>;
using LatestBook = std::map<ChannelKey, std::array<events::EventPtr, 4>>;
using RestoredLatestBook = std::map<ChannelKey, std::array<std::optional<events::EventDraft>, 4>>;
using ActivityBook = std::map<ChannelKey, std::vector<events::EventPtr>>;
using RestoredActivityBook = std::map<ChannelKey, std::vector<events::EventDraft>>;
DailyBook dailyBook(const SnapshotPtr &snapshot);
LatestBook latestBook(const SnapshotPtr &snapshot);
ActivityBook activityBook(const SnapshotPtr &snapshot);
constexpr std::size_t slot(events::EventType type) noexcept { return static_cast<std::size_t>(type); }

// One UI-thread writer; atomic immutable snapshot reads from any thread.
class SharedState final {
public:
	explicit SharedState(std::size_t maxChannels = 16);
	SharedState(const SharedState &) = delete;
	SharedState &operator=(const SharedState &) = delete;
	void start(const DailyBook &restored = {}, const LatestBook &latest = {}, TextCache *cache = nullptr, const ActivityBook &activity = {});
	// Quiet historical import: no delivery, counters or alert replay.
	SnapshotPtr prepareHistory(const ChannelKey &key, const std::vector<events::EventPtr> &events, TextCache &cache) const;
	void stop() noexcept;
	SnapshotPtr snapshot() const noexcept;
	// Builds privately, no publication. Cache may gain reusable entries on failure.
	SnapshotPtr prepare(const std::vector<events::EventPtr> &events, TextCache &cache,
		std::chrono::system_clock::time_point now = std::chrono::system_clock::now()) const;
	// Core calls only after its queue transaction commits; no allocating operations.
	void publish(SnapshotPtr prepared) noexcept;
private:
	std::size_t maxChannels_;
	std::uint64_t generation_ = 0;
	SnapshotPtr current_;
};
}
