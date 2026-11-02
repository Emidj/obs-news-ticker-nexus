// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "core/services.hpp"
namespace oto {
class ObsLogger final : public ILogger {
public:
	void write(LogLevel level, std::string_view message) const noexcept override;
};
} // namespace oto
