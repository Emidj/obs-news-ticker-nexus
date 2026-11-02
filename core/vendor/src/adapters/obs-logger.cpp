// SPDX-License-Identifier: GPL-2.0-or-later
#include "obs-logger.hpp"
#include <util/base.h>
#include <algorithm>
namespace oto {
void ObsLogger::write(LogLevel level, std::string_view message) const noexcept
{
	int obsLevel = LOG_INFO;
	switch (level) {
	case LogLevel::Debug: obsLevel = LOG_DEBUG; break;
	case LogLevel::Info: obsLevel = LOG_INFO; break;
	case LogLevel::Warning: obsLevel = LOG_WARNING; break;
	case LogLevel::Error: obsLevel = LOG_ERROR; break;
	}
	// Fixed format and bounded payload; '%' in messages is never interpreted.
	const auto length = static_cast<int>(std::min(message.size(), std::size_t{4096}));
	blog(obsLevel, "[obs-twitch-overlays] %.*s", length, message.empty() ? "" : message.data());
}
} // namespace oto
