/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* UI thread only. No Qt/STL objects, credentials or ownership cross this ABI.
 * Message memory is borrowed only for the synchronous callback duration.
 * Release a lease before unloading its client. Closing invalidates all leases.
 * Pending reads are cancelled by disconnect/release/provider shutdown; callers
 * must discard them on state loss. For events, account identifies that event's
 * channel, which callers must compare against their active channel.
 * A lease observes the existing owner; acquiring/releasing never connects/stops it.
 */
enum { NEXUS_TWITCH_ABI=6, NEXUS_STATE=1, NEXUS_RAID=2,
 NEXUS_HYPE_BEGIN=3, NEXUS_HYPE_END=4, NEXUS_CLOSING=5 };
typedef struct NexusTwitchMessage {
 uint32_t size,kind,active,authorized;
 uint64_t instance;
 char status[512],account[128],login[128],event_id[256],actor[128];
 uint32_t can_manage;
 char verification[2048],code[128];
 char actor_id[128]; /* actor is the validated raid login, not its display name. */
 char train_id[256];
 int64_t train_started_ms;
} NexusTwitchMessage;
typedef void (*NexusTwitchCallback)(void *context,const NexusTwitchMessage *message);
typedef void (*NexusTwitchReply)(void *context,int http_status,const char *utf8_json,uint32_t bytes);
typedef struct NexusTwitchApi {
 uint32_t size,abi;
 uint64_t (*acquire)(NexusTwitchCallback callback,void *context);
 void (*release)(uint64_t lease);
 int (*snapshot)(uint64_t lease,NexusTwitchMessage *out,uint32_t size);
 /* Explicit authorization only. No UI is opened by the provider. */
 int (*authorize)(uint64_t lease);
 int (*channel_info)(uint64_t lease,NexusTwitchReply callback,void *context);
 int (*disconnect)(uint64_t lease);
 int (*search_categories)(uint64_t lease,const char *query,uint32_t bytes,NexusTwitchReply callback,void *context);
 int (*update_channel)(uint64_t lease,const char *json,uint32_t bytes,NexusTwitchReply callback,void *context);
 int (*update_title)(uint64_t lease,const char *title,uint32_t bytes,NexusTwitchReply callback,void *context);
 /* Compare a fresh channel read with expected before PATCH. HTTP 409 = conflict.
  * Twitch offers no atomic compare-and-swap; concurrent external edits can race. */
 int (*guarded_update)(uint64_t lease,const char *json,uint32_t bytes,NexusTwitchReply callback,void *context);
} NexusTwitchApi;
typedef const NexusTwitchApi *(*NexusTwitchGetApi)(uint32_t abi,uint32_t size);
#ifdef __cplusplus
}
#endif
