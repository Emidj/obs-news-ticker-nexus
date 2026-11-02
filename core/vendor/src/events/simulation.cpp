// SPDX-License-Identifier: GPL-2.0-or-later
#include "simulation.hpp"
#include <stdexcept>

namespace oto::events {
EventDraft makeSimulation(EventType type, std::uint64_t sequence, std::chrono::system_clock::time_point when)
{
	if (sequence == 0) throw std::invalid_argument("Simulation sequence must be positive");
	EventDraft draft;
	draft.id = "sim-" + std::to_string(when.time_since_epoch().count()) + "-" + std::to_string(sequence);
	draft.channelId = "sim-channel";
	draft.occurredAt = when;
	draft.origin = EventOrigin::Simulation;
	const User viewer{"sim-viewer", "DemoViewer"};
	switch (type) {
	case EventType::Follow: draft.payload = FollowEvent{viewer}; break;
	case EventType::Sub: draft.payload = SubEvent{viewer, SubscriptionTier::Tier1, 3}; break;
	case EventType::Cheer: draft.payload = CheerEvent{viewer, 100}; break;
	case EventType::Gift: draft.payload = GiftEvent{viewer, 5, SubscriptionTier::Tier1}; break;
	case EventType::Raid: draft.payload = RaidEvent{viewer, 25}; break;
	case EventType::Chat: draft.payload = ChatEvent{viewer, "Mensaje de prueba: ¡hola! 🎵"}; break;
	case EventType::HypeTrain: draft.payload = HypeTrainEvent{HypeTrainStage::Progress, 2, 750, 1000}; break;
	case EventType::Shoutout: draft.payload = ShoutoutEvent{viewer, "demoviewer"}; break;
	case EventType::ChatModeration: draft.payload = ChatModerationEvent{}; break;
	default: throw std::invalid_argument("Unknown simulation type");
	}
	return draft;
}
} // namespace oto::events
