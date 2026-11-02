// SPDX-License-Identifier: GPL-2.0-or-later
#include "daily-totals.hpp"
#include <ctime>
#include <stdexcept>
namespace oto::state {
int localDay(std::chrono::system_clock::time_point time)
{
	const auto raw = std::chrono::system_clock::to_time_t(time);
	std::tm local{};
#ifdef _WIN32
	if (localtime_s(&local, &raw)) throw std::runtime_error("Local calendar unavailable");
#else
	if (!localtime_r(&raw, &local)) throw std::runtime_error("Local calendar unavailable");
#endif
	return (local.tm_year + 1900) * 10000 + (local.tm_mon + 1) * 100 + local.tm_mday;
}
}
