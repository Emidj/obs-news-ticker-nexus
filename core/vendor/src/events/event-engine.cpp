// SPDX-License-Identifier: GPL-2.0-or-later
#include "event-engine.hpp"
#include <string_view>
#include <type_traits>
#include <utility>
#include <cctype>

namespace oto::events {
namespace {
void trim(std::string &text)
{
	const auto first = text.find_first_not_of(" \t\r\n");
	if (first == std::string::npos) { text.clear(); return; }
	const auto last = text.find_last_not_of(" \t\r\n");
	text = text.substr(first, last - first + 1);
}
bool identifier(std::string &value)
{
	if (value.size() > 128) return false;
	trim(value);
	if (value.empty()) return false;
	for (const unsigned char ch : value) {
		if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
		      (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' || ch == ':')) return false;
	}
	return true;
}
bool eventIdentifier(std::string &value, EventOrigin origin)
{
	if (origin != EventOrigin::Twitch) return identifier(value);
	// EventSub message IDs are opaque, not user/channel identifiers or UUIDs.
	// Preserve them byte-for-byte for deduplication, including Base64 + / =.
	if (value.empty() || value.size() > 128) return false;
	for (const unsigned char ch : value) if (ch < 0x21 || ch > 0x7e) return false;
	return true;
}
// Strict UTF-8: reject overlong forms, surrogate code points, truncation and NUL.
bool utf8(std::string_view text, bool multiline = false)
{
	for (std::size_t i = 0; i < text.size();) {
		const auto first = static_cast<unsigned char>(text[i++]);
		if (first < 0x80) {
			if (first < 0x20 && !(multiline && (first == '\n' || first == '\t'))) return false;
			if (first == 0x7f) return false;
			continue;
		}
		unsigned remaining = 0;
		std::uint32_t codepoint = 0;
		std::uint32_t minimum = 0;
		if (first >= 0xc2 && first <= 0xdf) { remaining = 1; codepoint = first & 0x1f; minimum = 0x80; }
		else if (first >= 0xe0 && first <= 0xef) { remaining = 2; codepoint = first & 0x0f; minimum = 0x800; }
		else if (first >= 0xf0 && first <= 0xf4) { remaining = 3; codepoint = first & 0x07; minimum = 0x10000; }
		else return false;
		if (i + remaining > text.size()) return false;
		while (remaining--) {
			const auto next = static_cast<unsigned char>(text[i++]);
			if ((next & 0xc0) != 0x80) return false;
			codepoint = (codepoint << 6) | (next & 0x3f);
		}
		if (codepoint < minimum || codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff)) return false;
	}
	return true;
}
bool user(User &value)
{
	if (!identifier(value.id) || value.displayName.size() > 256) return false;
	trim(value.displayName);
	if (value.displayName.empty()) value.displayName = value.id;
	return utf8(value.displayName);
}
bool optionalUser(std::optional<User> &value) { return !value || user(*value); }
bool tier(SubscriptionTier value, bool gift = false)
{
	return value == SubscriptionTier::Tier1 || value == SubscriptionTier::Tier2 ||
	       value == SubscriptionTier::Tier3 || (!gift && value == SubscriptionTier::Prime);
}
bool normalize(FollowEvent &event) { return user(event.follower); }
bool normalize(SubEvent &event) { return user(event.subscriber) && tier(event.tier) && event.totalMonths > 0; }
bool normalize(CheerEvent &event) { return optionalUser(event.user) && event.bits > 0; }
bool normalize(GiftEvent &event) { return optionalUser(event.gifter) && event.count > 0 && tier(event.tier, true); }
bool normalize(RaidEvent &event) { return user(event.raider) && event.viewers > 0; }
bool normalize(ChatEvent &event)
{
	if(!event.messageId.empty() && !eventIdentifier(event.messageId,EventOrigin::Twitch))return false;
	if(event.fragments.size()>128)return false;
	if(event.badges.size()>32)return false;
	for(const auto &badge:event.badges){
		const auto safe=[](const std::string &id){if(id.empty() || id.size()>64)return false;for(const unsigned char c:id)if(!((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_' || c=='-'))return false;return true;};
		if(!safe(badge.setId) || !safe(badge.version) || badge.info.size()>128 || !utf8(badge.info,true))return false;
	}
	std::string joined;
	for(auto &fragment:event.fragments){
		if(fragment.text.size()>4096 || !utf8(fragment.text,true))return false;
		if(!fragment.emoteId.empty() && !identifier(fragment.emoteId))return false;
		if(!fragment.cheerPrefix.empty()){
			if(!fragment.emoteId.empty() || fragment.cheerPrefix.size()>64 || !fragment.cheerBits || fragment.cheerBits>1000000000 || !fragment.cheerTier || fragment.cheerTier>fragment.cheerBits)return false;
			for(const unsigned char c:fragment.cheerPrefix)if(!std::isalnum(c) && c!='_')return false;
		}else if(fragment.cheerBits || fragment.cheerTier)return false;
		joined+=fragment.text;if(joined.size()>4096)return false;
	}
	if(!event.fragments.empty() && joined!=event.message)return false;
	if(event.role<ChatRole::Viewer || event.role>ChatRole::Vip)return false;
	if(!event.color.empty()){
		if(event.color.size()!=7 || event.color.front()!='#')return false;
		for(std::size_t i=1;i<event.color.size();++i)if(!std::isxdigit(static_cast<unsigned char>(event.color[i])))return false;
	}
	return user(event.author) && event.message.size() <= 4096 &&
	       event.message.find_first_not_of(" \t\r\n") != std::string::npos && utf8(event.message, true);
}
bool normalize(ChatModerationEvent &event)
{
	switch(event.action){
	case ChatModerationAction::DeleteMessage:return event.userId.empty() && eventIdentifier(event.messageId,EventOrigin::Twitch);
	case ChatModerationAction::ClearUser:return event.messageId.empty() && identifier(event.userId);
	case ChatModerationAction::ClearAll:return event.messageId.empty() && event.userId.empty();
	}return false;
}
bool normalize(ShoutoutEvent &event)
{
	if (!user(event.target) || event.login.empty() || event.login.size() > 25 || event.category.size()>256 || !utf8(event.category)) return false;
	for (const unsigned char ch : event.login)
		if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_')) return false;
	return true;
}
bool normalize(HypeTrainEvent &event)
{
	if((!event.trainId.empty() && !eventIdentifier(event.trainId,EventOrigin::Twitch)) || event.contributors.size()>16)return false;
	for(auto &entry:event.contributors)if(!user(entry.user))return false;
	return event.level > 0 && (event.goal > 0 || event.stage == HypeTrainStage::End) && (event.stage == HypeTrainStage::Begin ||
	       event.stage == HypeTrainStage::Progress || event.stage == HypeTrainStage::End);
}
} // namespace

EventType typeOf(const EventPayload &payload)
{
	return std::visit([](const auto &value) {
		using T = std::decay_t<decltype(value)>;
		if constexpr (std::is_same_v<T, FollowEvent>) return EventType::Follow;
		else if constexpr (std::is_same_v<T, SubEvent>) return EventType::Sub;
		else if constexpr (std::is_same_v<T, CheerEvent>) return EventType::Cheer;
		else if constexpr (std::is_same_v<T, GiftEvent>) return EventType::Gift;
		else if constexpr (std::is_same_v<T, RaidEvent>) return EventType::Raid;
		else if constexpr (std::is_same_v<T, ChatEvent>) return EventType::Chat;
		else if constexpr (std::is_same_v<T, HypeTrainEvent>) return EventType::HypeTrain;
		else if constexpr (std::is_same_v<T, ShoutoutEvent>) return EventType::Shoutout;
		else return EventType::ChatModeration;
	}, payload);
}
const char *typeName(EventType type) noexcept
{
	switch (type) {
	case EventType::Follow: return "Follow";
	case EventType::Sub: return "Sub";
	case EventType::Cheer: return "Cheer";
	case EventType::Gift: return "Gift";
	case EventType::Raid: return "Raid";
	case EventType::Chat: return "Chat";
	case EventType::HypeTrain: return "Hype Train";
	case EventType::Shoutout: return "Shoutout";
	case EventType::ChatModeration: return "Chat moderation";
	}
	return "Unknown";
}
ProcessResult EventEngine::process(EventDraft draft) const
{
	if (!running_) return {nullptr, EventError::Stopped};
	if (!eventIdentifier(draft.id, draft.origin) || !identifier(draft.channelId)) return {nullptr, EventError::InvalidId};
	if (draft.occurredAt.time_since_epoch().count() <= 0) return {nullptr, EventError::InvalidTime};
	if (draft.origin != EventOrigin::Simulation && draft.origin != EventOrigin::Preview && draft.origin != EventOrigin::Twitch) return {nullptr, EventError::InvalidOrigin};
	if (draft.payload.valueless_by_exception() || !std::visit([](auto &value) { return normalize(value); }, draft.payload))
		return {nullptr, EventError::InvalidPayload};
	return {EventPtr(new Event(std::move(draft))), EventError::None};
}
} // namespace oto::events
