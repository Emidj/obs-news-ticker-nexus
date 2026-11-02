/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "state/shared-state.hpp"
#include "twitch-ticker-api.h"
#include "twitch-hype-api.h"
namespace oto::twitch {
NexusHypeSnapshot hypePreviewFeed(const state::SnapshotPtr &,const std::optional<state::ChannelKey> &panel,uint64_t instance);
NexusHypeSnapshot hypeFeed(const state::SnapshotPtr &,const std::string &account,bool active,bool authorized,uint64_t instance);
NexusTickerSnapshot tickerFeed(const state::SnapshotPtr &,const std::string &account,
                              bool active,bool authorized,uint64_t instance);
}
