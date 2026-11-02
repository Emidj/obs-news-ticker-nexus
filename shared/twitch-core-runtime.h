/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "twitch-core-api.h"
#ifdef NEXUS_CORE_BUILD
#define NEXUS_CORE_EXPORT __declspec(dllexport)
#else
#define NEXUS_CORE_EXPORT __declspec(dllimport)
#endif
#ifdef __cplusplus
extern "C" {
#endif
typedef struct NexusTwitchCoreInfo {
 uint32_t size,abi;
 char version[32];
 /* Windows UTF-16 path: inspect the loaded DLL, not a proposed package. */
 uint16_t module_path[32768];
} NexusTwitchCoreInfo;
NEXUS_CORE_EXPORT int nexus_twitch_core_info(NexusTwitchCoreInfo *out,uint32_t size);
/* UI thread. A successful retain must be paired with release. No implicit auth. */
NEXUS_CORE_EXPORT int nexus_twitch_runtime_retain(const char *utf8_session_path);
NEXUS_CORE_EXPORT void nexus_twitch_runtime_release(void);
NEXUS_CORE_EXPORT int nexus_twitch_runtime_active(void);
NEXUS_CORE_EXPORT void nexus_twitch_runtime_set_locale(const char *utf8_locale);
NEXUS_CORE_EXPORT const NexusTwitchApi *nexus_twitch_core_get_api(uint32_t abi,uint32_t size);
#ifdef __cplusplus
}
#endif
