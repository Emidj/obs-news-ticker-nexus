// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <chrono>
#include <cstdint>
namespace oto::state {
struct DailyTotals {
	int day = 0; // YYYYMMDD in the system local timezone.
	std::uint64_t bits = 0, subs = 0;
	bool saturated = false;
	bool operator==(const DailyTotals &other) const noexcept
	{ return day == other.day && bits == other.bits && subs == other.subs && saturated == other.saturated; }
};
int localDay(std::chrono::system_clock::time_point time);
}
