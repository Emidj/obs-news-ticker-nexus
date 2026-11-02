/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "../shared/twitch-core-api.h"
#include "../shared/twitch-ticker-api.h"
#include "../shared/twitch-hype-api.h"
#include <stdbool.h>
enum twitch_status { TWITCH_MISSING, TWITCH_WAITING, TWITCH_READY, TWITCH_CLOSED };
struct twitch_snapshot {
    enum twitch_status status;
    uint32_t feed_version;
    char account[128], last_raid[128], event_id[256];
};
/* Attach/detach and all provider callbacks: OBS UI thread only.
 * Read is safe on any thread. Never authorizes, connects or changes the channel. */
bool twitch_observer_attach(const NexusTwitchApi *api);
void twitch_observer_detach(void);
void twitch_observer_read(struct twitch_snapshot *out);
bool twitch_observer_attach_feed(const NexusTickerApi *api);
void twitch_observer_read_feed(NexusTickerSnapshot *out);
bool twitch_observer_authorize(void);
bool twitch_observer_authorization_url(char *out,uint32_t size);
bool twitch_observer_disconnect(void);

bool twitch_observer_attach_hype(const NexusHypeApi *api);
void twitch_observer_read_hype(NexusHypeSnapshot *out);

bool twitch_observer_attach_hype_preview(const NexusHypeApi *api);
void twitch_observer_read_hype_mode(NexusHypeSnapshot *out,bool *preview);
