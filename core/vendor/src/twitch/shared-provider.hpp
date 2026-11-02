// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "twitch-core-runtime.h"
#include "service.hpp"
#include "twitch-core-api.h"
namespace oto::twitch {
NEXUS_CORE_EXPORT void publishSharedService(std::shared_ptr<Service> service);
NEXUS_CORE_EXPORT void unpublishSharedService();
}
extern "C" NEXUS_CORE_EXPORT const NexusTwitchApi *nexus_twitch_core_get_api(uint32_t abi,uint32_t size);
