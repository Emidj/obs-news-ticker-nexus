/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdint.h>
/* Optional ABI 6 extension, UI thread only. Shares the base lease and channel.
 * The snapshot contains only live Twitch state, never simulations or previews. */
enum {NEXUS_HYPE_VERSION=1};
typedef struct NexusHypeSnapshot {
 uint32_t size,version,active,authorized,present,level;
 uint64_t instance,progress,goal;
 int64_t started_ms,expires_ms;
 char account[128],train_id[256];
} NexusHypeSnapshot;
typedef void (*NexusHypeCallback)(void *,const NexusHypeSnapshot *);
typedef struct NexusHypeApi {
 uint32_t size,version;
 int (*bind)(uint64_t lease,NexusHypeCallback callback,void *context);
 int (*snapshot)(uint64_t lease,NexusHypeSnapshot *out,uint32_t size);
} NexusHypeApi;
typedef const NexusHypeApi *(*NexusHypeGetApi)(uint32_t version,uint32_t size);

/* Core 1.3.1 also exports nexus_twitch_core_get_hype_preview_api with this
 * shape. It contains only the explicitly selected Overlays panel TEST, never
 * live events; authorized stays zero. End TEST or release clears that stream. */
