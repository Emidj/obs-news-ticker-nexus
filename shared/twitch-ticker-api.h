/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdint.h>
/* Optional ABI 6 extension. UI thread only; all strings UTF-8, callback memory
 * borrowed. A feed binds to an existing base lease and dies with that lease.
 * Daily values are observed events in Windows local day, never full Twitch totals. */
enum { NEXUS_TICKER_VERSION=1, NEXUS_TICKER_UPDATE=1, NEXUS_TICKER_CLOSING=2 };
enum { NEXUS_TICKER_FOLLOW, NEXUS_TICKER_SUB, NEXUS_TICKER_CHEER,
       NEXUS_TICKER_GIFT, NEXUS_TICKER_RAID, NEXUS_TICKER_COUNT };
typedef struct NexusTickerEvent {
    uint32_t present,anonymous,tier,counts_toward_totals;
    int64_t occurred_ms;
    uint64_t amount;
    char id[256],actor[256],actor_id[128];
} NexusTickerEvent;
typedef struct NexusTickerSnapshot {
    uint32_t size,version,kind,active,authorized,local_day,saturated,reserved;
    uint64_t instance,daily_subs,daily_bits;
    char account[128];
    NexusTickerEvent latest[NEXUS_TICKER_COUNT];
} NexusTickerSnapshot;
typedef void (*NexusTickerCallback)(void *,const NexusTickerSnapshot *);
typedef struct NexusTickerApi {
    uint32_t size,version;
    int (*bind)(uint64_t lease,NexusTickerCallback callback,void *context);
    int (*snapshot)(uint64_t lease,NexusTickerSnapshot *out,uint32_t size);
    int (*authorize)(uint64_t lease); /* Explicit read/event authorization. */
} NexusTickerApi;
typedef const NexusTickerApi *(*NexusTickerGetApi)(uint32_t version,uint32_t size);
