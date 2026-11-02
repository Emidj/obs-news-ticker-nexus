// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "event.hpp"

namespace oto::events {
enum class EventError { None, Stopped, InvalidId, InvalidTime, InvalidOrigin, InvalidPayload };
struct ProcessResult {
	EventPtr event;
	EventError error = EventError::None;
	explicit operator bool() const noexcept { return event != nullptr; }
};

// One instance owned by PluginCore. All methods run on the OBS UI thread.
// No dispatch, buffering, subscribers, history or automatic background work.
class EventEngine final {
public:
	void start() noexcept { running_ = true; }
	void stop() noexcept { running_ = false; }
	ProcessResult process(EventDraft draft) const;
private:
	bool running_ = false;
};
} // namespace oto::events
