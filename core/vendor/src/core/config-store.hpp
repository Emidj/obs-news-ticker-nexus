// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "services.hpp"

namespace oto {
enum class ConfigStatus { Missing, Loaded, Invalid, UnsupportedVersion, Unavailable };

struct ConfigLoadResult {
	ConfigStatus status = ConfigStatus::Missing;
	Settings settings;
};

class IConfigStore {
public:
	virtual ~IConfigStore() = default;
	virtual ConfigLoadResult load() = 0;
	virtual bool save(Settings settings) = 0;
};
} // namespace oto
