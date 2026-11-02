// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <string_view>
#include "state/shared-state.hpp"

namespace oto {
enum class LogLevel { Debug, Info, Warning, Error };

struct Settings {
	bool diagnosticsEnabled = false;
};

class ILogger {
public:
	virtual ~ILogger() = default;
	// Sinks must be thread-safe. Messages must never contain credentials.
	virtual void write(LogLevel level, std::string_view message) const noexcept = 0;
};

// Internal C++ contract, not a public binary SDK. Consumers receive the same
// shared service handle through injection; no per-source service construction.
class IPluginServices {
public:
	virtual ~IPluginServices() = default;
	virtual bool running() const noexcept = 0;
	virtual Settings settings() const noexcept = 0;
	virtual state::SnapshotPtr sharedSnapshot() const noexcept = 0;
	virtual void log(LogLevel level, std::string_view message) const noexcept = 0;
};
} // namespace oto
