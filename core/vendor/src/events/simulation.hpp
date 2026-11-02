// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "event.hpp"

namespace oto::events {
// Pure fixture generator; never contacts a service or invents real user data.
EventDraft makeSimulation(EventType type, std::uint64_t sequence,
			  std::chrono::system_clock::time_point when);
} // namespace oto::events
