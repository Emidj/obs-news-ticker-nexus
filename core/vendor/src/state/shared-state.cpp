// SPDX-License-Identifier: GPL-2.0-or-later
#include "shared-state.hpp"
#include <atomic>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <set>

namespace oto::state {
bool chatRemoved(const ChatModerationState &moderation,const events::Event &event)
{
	const auto &data=event.data();const auto *chat=std::get_if<events::ChatEvent>(&data.payload);if(!chat)return false;
	if(moderation.clearedAt!=std::chrono::system_clock::time_point{} && data.occurredAt<=moderation.clearedAt)return true;
	const auto &id=chat->messageId.empty()?data.id:chat->messageId;
	for(const auto &mark:moderation.messages)if(mark.id==id)return true;
	for(const auto &mark:moderation.users)if(mark.id==chat->author.id && data.occurredAt<=mark.at)return true;
	return false;
}
namespace {
void add(std::uint64_t &target, std::uint64_t amount, bool &saturated) noexcept
{
	if (amount > std::numeric_limits<std::uint64_t>::max() - target) {
		target = std::numeric_limits<std::uint64_t>::max(); saturated = true;
	} else target += amount;
}
const events::User *actor(const events::EventPayload &payload)
{
	return std::visit([](const auto &item) -> const events::User * {
		using T = std::decay_t<decltype(item)>;
		if constexpr (std::is_same_v<T, events::FollowEvent>) return &item.follower;
		else if constexpr (std::is_same_v<T, events::SubEvent>) return &item.subscriber;
		else if constexpr (std::is_same_v<T, events::CheerEvent>) return item.user ? &*item.user : nullptr;
		else if constexpr (std::is_same_v<T, events::GiftEvent>) return item.gifter ? &*item.gifter : nullptr;
		else if constexpr (std::is_same_v<T, events::RaidEvent>) return &item.raider;
		else if constexpr (std::is_same_v<T, events::ChatEvent>) return &item.author;
		else if constexpr (std::is_same_v<T, events::ShoutoutEvent>) return &item.target;
		else return nullptr;
	}, payload);
}
}
SharedState::SharedState(std::size_t maxChannels) : maxChannels_(maxChannels)
{ if (!maxChannels) throw std::invalid_argument("State capacity must be positive"); }
SnapshotPtr SharedState::snapshot() const noexcept { return std::atomic_load(&current_); }
void SharedState::publish(SnapshotPtr prepared) noexcept { std::atomic_store(&current_, std::move(prepared)); }
DailyBook dailyBook(const SnapshotPtr &snapshot)
{
	DailyBook result;
	if (snapshot) for (const auto &[key, channel] : snapshot->channels)
		if (key.origin != events::EventOrigin::Preview) result.emplace(key, channel->daily);
	return result;
}
LatestBook latestBook(const SnapshotPtr &snapshot)
{
	LatestBook result;
	if(snapshot)for(const auto &[key,channel]:snapshot->channels)if(key.origin!=events::EventOrigin::Preview){
		std::array<events::EventPtr,4> latest;for(std::size_t i=0;i<latest.size();++i)latest[i]=channel->latest[i].event;
		result.emplace(key,std::move(latest));
	}
	return result;
}
ActivityBook activityBook(const SnapshotPtr &snapshot)
{
	ActivityBook result;
	if(snapshot)for(const auto &[key,channel]:snapshot->channels)if(key.origin!=events::EventOrigin::Preview){
		auto &entries=result[key];for(const auto &item:channel->activity)entries.push_back(item.event);
	}
	return result;
}
void SharedState::start(const DailyBook &restored, const LatestBook &latest, TextCache *cache, const ActivityBook &activity)
{
	if (snapshot()) return;
	if (generation_ == std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("State generation exhausted");
	auto initial = std::make_shared<Snapshot>();
	if (restored.size() > maxChannels_) throw std::length_error("Restored daily state exceeds capacity");
	for (const auto &[key, daily] : restored) {
		auto channel = std::make_shared<ChannelState>(); channel->daily = daily;
		const auto found=latest.find(key);
		if(found!=latest.end())for(std::size_t i=0;i<found->second.size();++i)if(const auto &event=found->second[i]){
			const auto *user=actor(event->data().payload);if(user && !cache)throw std::logic_error("Restored names require shared text cache");
			channel->latest[i]={event,user?cache->intern(user->displayName):nullptr,0};
		}
		std::vector<events::EventPtr> entries;
		const auto saved=activity.find(key);
		if(saved!=activity.end())entries=saved->second;
		else if(found!=latest.end())for(const auto &event:found->second)if(event)entries.push_back(event);
		if(entries.size()>ChannelState::activityCapacity)throw std::length_error("Restored activity exceeds capacity");
		std::set<std::string> ids;
		for(const auto &event:entries){
			if(!event || event->data().origin!=key.origin || event->data().channelId!=key.channelId || slot(events::typeOf(event->data().payload))>4 || !ids.insert(event->data().id).second)throw std::runtime_error("Invalid restored activity");
			const auto *user=actor(event->data().payload);if(user && !cache)throw std::logic_error("Restored activity requires shared cache");
			channel->activity.push_back({event,user?cache->intern(user->displayName):nullptr,0,0});
		}
		std::stable_sort(channel->activity.begin(),channel->activity.end(),[](const auto &a,const auto &b){return a.event->data().occurredAt>b.event->data().occurredAt;});
		// Repair missing/stale last fields from actual saved activity, without replay.
		for(const auto &item:channel->activity){auto &last=channel->latest[slot(events::typeOf(item.event->data().payload))];if(!last.event || item.event->data().occurredAt>last.event->data().occurredAt)last=item;}
		initial->channels.emplace(key, std::move(channel));
	}
	initial->generation = generation_ + 1;
	publish(initial);
	++generation_;
}
SnapshotPtr SharedState::prepareHistory(const ChannelKey &key,const std::vector<events::EventPtr> &events,TextCache &cache) const
{
	const auto before=snapshot();if(!before)throw std::logic_error("Shared state stopped");
	if(key.origin!=events::EventOrigin::Twitch || events.size()>ChannelState::activityCapacity)throw std::invalid_argument("Invalid historical channel");
	const auto old=before->channels.find(key);
	if(old==before->channels.end() && before->channels.size()>=maxChannels_)throw std::length_error("History channel capacity exceeded");
	auto channel=old==before->channels.end()?std::make_shared<ChannelState>():std::make_shared<ChannelState>(*old->second);
	if(old==before->channels.end())channel->daily={localDay(std::chrono::system_clock::now())};
	for(const auto &event:events){
		if(!event || event->data().origin!=key.origin || event->data().channelId!=key.channelId || events::typeOf(event->data().payload)!=events::EventType::Follow)throw std::invalid_argument("Invalid follow history");
		const auto &draft=event->data();const auto &follower=std::get<events::FollowEvent>(draft.payload).follower;
		const auto duplicate=std::any_of(channel->activity.begin(),channel->activity.end(),[&](const auto &item){
			if(item.event->data().id==draft.id)return true;
			const auto *follow=std::get_if<events::FollowEvent>(&item.event->data().payload);
			return follow && follow->follower.id==follower.id && std::chrono::duration_cast<std::chrono::seconds>(item.event->data().occurredAt.time_since_epoch())==std::chrono::duration_cast<std::chrono::seconds>(draft.occurredAt.time_since_epoch());
		});
		if(duplicate)continue;
		channel->activity.push_back({event,cache.intern(follower.displayName),0,0});
	}
	std::stable_sort(channel->activity.begin(),channel->activity.end(),[](const auto &a,const auto &b){return a.event->data().occurredAt>b.event->data().occurredAt;});
	// Backfill may replace older historical rows, but never pending live alerts.
	while(channel->activity.size()>ChannelState::activityCapacity){const auto oldest=std::find_if(channel->activity.rbegin(),channel->activity.rend(),[](const auto &item){return item.delivery==0;});channel->activity.erase(std::next(oldest).base());}
	if(old!=before->channels.end() && channel->activity.size()==old->second->activity.size() && std::equal(channel->activity.begin(),channel->activity.end(),old->second->activity.begin(),[](const auto &a,const auto &b){return a.event==b.event;}))return before;
	for(const auto &item:channel->activity){const auto i=slot(events::typeOf(item.event->data().payload));auto &latest=channel->latest[i];if(!latest.event || item.event->data().occurredAt>latest.event->data().occurredAt)latest=item;}
	if(before->revision==std::numeric_limits<std::uint64_t>::max())throw std::overflow_error("State revision exhausted");
	auto next=std::make_shared<Snapshot>(*before);++next->revision;next->channels[key]=std::move(channel);return next;
}
void SharedState::stop() noexcept { publish(nullptr); }
SnapshotPtr SharedState::prepare(const std::vector<events::EventPtr> &events, TextCache &cache, std::chrono::system_clock::time_point now) const
{
	const auto before = snapshot();
	if (!before) throw std::logic_error("Shared state stopped");
	if (events.empty()) return before;
	if (before->revision == std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("State revision exhausted");
	auto next = std::make_shared<Snapshot>(*before);
	++next->revision;
	std::map<ChannelKey, std::shared_ptr<ChannelState>> changed;
	std::set<ChannelKey> hypeCountsChanged;
	const auto today = localDay(now);
	for (const auto &event : events) {
		if (!event) throw std::invalid_argument("Null state event");
		const auto &data = event->data();
		const ChannelKey key{data.origin, data.channelId};
		auto entry = changed.find(key);
		if (entry == changed.end()) {
			const auto old = next->channels.find(key);
			if (old == next->channels.end() && next->channels.size() >= maxChannels_)
				throw std::length_error("Shared state channel capacity reached");
			auto channel = old == next->channels.end() ? std::make_shared<ChannelState>() : std::make_shared<ChannelState>(*old->second);
			next->channels[key] = channel;
			entry = changed.emplace(key, std::move(channel)).first;
		}
		auto &channel = *entry->second;
		const auto rememberSub=[&](const events::User &actor,std::uint64_t count,events::SubscriptionTier tier){
			const std::uint64_t weight=tier==events::SubscriptionTier::Tier3?2500:tier==events::SubscriptionTier::Tier2?1000:500;
			if(!count || count>std::numeric_limits<std::uint64_t>::max()/weight)return;
			auto ledger=channel.hypeSubscriptions?std::make_shared<std::vector<ChannelState::HypeSubscription>>(*channel.hypeSubscriptions):std::make_shared<std::vector<ChannelState::HypeSubscription>>();
			const auto cutoff=channel.hypeTrain?channel.hypeTrain->startedAt:now-std::chrono::minutes(10);
			ledger->erase(std::remove_if(ledger->begin(),ledger->end(),[&](const auto &s){return s.occurredAt<cutoff;}),ledger->end());
			ledger->push_back({actor.id,count,count*weight,data.occurredAt});
			if(ledger->size()>ChannelState::hypeSubscriptionCapacity)ledger->erase(ledger->begin());
			channel.hypeSubscriptions=std::move(ledger);hypeCountsChanged.insert(key);
		};
		if(const auto *sub=std::get_if<events::SubEvent>(&data.payload);sub && sub->countsTowardTotals)rememberSub(sub->subscriber,1,sub->tier);
		if(const auto *gift=std::get_if<events::GiftEvent>(&data.payload);gift && gift->gifter)rememberSub(*gift->gifter,gift->count,gift->tier);
		if (channel.daily.day != today) channel.daily = {today};
		if (localDay(data.occurredAt) == today) {
			if (const auto *cheer = std::get_if<events::CheerEvent>(&data.payload)) add(channel.daily.bits, cheer->bits, channel.daily.saturated);
			if (const auto *sub = std::get_if<events::SubEvent>(&data.payload); sub && sub->countsTowardTotals) add(channel.daily.subs, 1, channel.daily.saturated);
			if (const auto *gift = std::get_if<events::GiftEvent>(&data.payload)) add(channel.daily.subs, gift->count, channel.daily.saturated);
		}
		channel.revision = next->revision;
		const auto index = slot(events::typeOf(data.payload));
		add(channel.delivered[index], 1, channel.saturated);
		if (const auto *cheer = std::get_if<events::CheerEvent>(&data.payload)) add(channel.sessionBits, cheer->bits, channel.saturated);
		if (const auto *sub = std::get_if<events::SubEvent>(&data.payload); sub && sub->countsTowardTotals) add(channel.sessionSubs, 1, channel.saturated);
		if (const auto *gift = std::get_if<events::GiftEvent>(&data.payload)) {
			add(channel.sessionSubs, gift->count, channel.saturated);
			add(channel.sessionGifts, gift->count, channel.saturated);
		}
		auto &latest = channel.latest[index];
		const auto *user = actor(data.payload);
		if(const auto *action=std::get_if<events::ChatModerationEvent>(&data.payload)){
			auto moderation=channel.chatModeration?std::make_shared<ChatModerationState>(*channel.chatModeration):std::make_shared<ChatModerationState>();
			const auto mark=[&](auto &marks,const std::string &id){
				const auto found=std::find_if(marks.begin(),marks.end(),[&](const auto &m){return m.id==id;});
				if(found==marks.end())marks.push_back({id,data.occurredAt});else found->at=std::max(found->at,data.occurredAt);
				std::stable_sort(marks.begin(),marks.end(),[](const auto &a,const auto &b){return a.at<b.at;});
				if(marks.size()>ChatModerationState::capacity)marks.erase(marks.begin());
			};
			if(action->action==events::ChatModerationAction::ClearAll)moderation->clearedAt=std::max(moderation->clearedAt,data.occurredAt);
			else if(action->action==events::ChatModerationAction::ClearUser)mark(moderation->users,action->userId);
			else mark(moderation->messages,action->messageId);
			channel.chatModeration=moderation;
			channel.chat.erase(std::remove_if(channel.chat.begin(),channel.chat.end(),[&](const auto &item){return chatRemoved(*moderation,*item.event);}),channel.chat.end());
			// Do not retain a removed message in the latest-chat slot either.
			auto &lastChat=channel.latest[slot(events::EventType::Chat)];
			if(lastChat.event && chatRemoved(*moderation,*lastChat.event))lastChat=channel.chat.empty()?LatestEvent{}:channel.chat.back();
		}
		// Moderation can be delivered ahead of queued or delayed chat messages.
		if(index==slot(events::EventType::Chat) && channel.chatModeration && chatRemoved(*channel.chatModeration,*event))continue;
		if(const auto *hype=std::get_if<events::HypeTrainEvent>(&data.payload)) {
			const auto &previous=channel.hypeTrain;
			const bool same=previous && previous->trainId==hype->trainId;
			const bool closed=same && previous->stage==events::HypeTrainStage::End && hype->stage!=events::HypeTrainStage::End;
			if(!closed && (!previous || data.occurredAt>=channel.hypeUpdatedAt)) {
				auto nextHype=*hype;
				if(same && hype->stage==events::HypeTrainStage::End) {
					nextHype.goal=previous->goal;nextHype.progress=previous->progress;
					nextHype.expiresAt=previous->expiresAt;
				}
				channel.hypeTrain=std::move(nextHype);channel.hypeUpdatedAt=data.occurredAt;
				hypeCountsChanged.insert(key);
			}
		}
		const bool activity = index <= slot(events::EventType::Raid);
		const auto name = user && (activity || !latest.event || data.occurredAt >= latest.event->data().occurredAt)
			? cache.intern(user->displayName) : nullptr;
		if(index==slot(events::EventType::Chat)){
			const auto position=std::upper_bound(channel.chat.begin(),channel.chat.end(),data.occurredAt,[](auto when,const auto &item){return when<item.event->data().occurredAt;});
			channel.chat.insert(position,{event,cache.intern(user->displayName),next->revision,channel.delivered[index]});
			if(channel.chat.size()>ChannelState::chatCapacity)channel.chat.erase(channel.chat.begin());
		}
		if (index == slot(events::EventType::Shoutout)) {
			channel.shoutouts.push_back({event,cache.intern(user->displayName),next->revision,channel.delivered[index]});
			if (channel.shoutouts.size() > ChannelState::shoutoutCapacity) channel.shoutouts.erase(channel.shoutouts.begin());
		}
		if (activity) {
			if(channel.activityDelivery==std::numeric_limits<std::uint64_t>::max())throw std::overflow_error("Activity delivery exhausted");
			const auto delivery=++channel.activityDelivery;
			const auto position = std::lower_bound(channel.activity.begin(), channel.activity.end(), data.occurredAt,
				[](const LatestEvent &item, auto when) { return item.event->data().occurredAt > when; });
			channel.activity.insert(position, {event, name, next->revision, delivery});
			// Keep the most recently delivered activity, still sorted by timestamp
			// for Event List. A late timestamp must not discard its own fresh alert.
			if(channel.activity.size()>ChannelState::activityCapacity)channel.activity.erase(std::min_element(channel.activity.begin(),channel.activity.end(),[](const auto &a,const auto &b){return a.delivery<b.delivery || (a.delivery==b.delivery && a.event->data().occurredAt<b.event->data().occurredAt);}));
		}
		// Queue priority is not chronology. Late events count but never regress latest data.
		if (!latest.event || data.occurredAt >= latest.event->data().occurredAt) {
			latest = {event, name, next->revision};
		}
	}
	for(const auto &key:hypeCountsChanged){
		auto &channel=*changed.at(key);if(!channel.hypeTrain || key.origin==events::EventOrigin::Preview)continue;
		auto &hype=*channel.hypeTrain;
		for(auto &c:hype.contributors){
			c.subscriptions.reset();if(!(c.methods&2) || !channel.hypeSubscriptions)continue;
			std::uint64_t count=0,points=0;bool overflow=false;
			for(const auto &s:*channel.hypeSubscriptions){
				if(s.userId!=c.user.id || s.occurredAt<hype.startedAt || (hype.stage==events::HypeTrainStage::End && s.occurredAt>channel.hypeUpdatedAt))continue;
				if(s.count>std::numeric_limits<std::uint64_t>::max()-count || s.points>std::numeric_limits<std::uint64_t>::max()-points){overflow=true;break;}
				count+=s.count;points+=s.points;
			}
			// Missing/late events or bootstrap midway through a train remain
			// explicitly unknown, rather than treating all tiers as Tier 1.
			if(!overflow && count && points==c.subscriptionPoints)c.subscriptions=count;
		}
	}
	return next;
}
}
