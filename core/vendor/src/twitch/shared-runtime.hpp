// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "service.hpp"
#include "twitch-core-runtime.h"
namespace oto::twitch {
NEXUS_CORE_EXPORT std::shared_ptr<Service> retainRuntime(std::shared_ptr<PluginCore> core,QString session);
}
