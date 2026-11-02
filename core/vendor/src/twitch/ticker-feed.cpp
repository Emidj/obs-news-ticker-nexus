/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "ticker-feed.hpp"
#include <cstring>
#include <algorithm>
namespace oto::twitch {
template<size_t N>static void copy(char (&out)[N],const std::string &text)
{std::memcpy(out,text.data(),std::min(N-1,text.size()));}
NexusHypeSnapshot hypePreviewFeed(const state::SnapshotPtr &state,const std::optional<state::ChannelKey> &panel,uint64_t instance)
{
 NexusHypeSnapshot out{};out.size=sizeof(out);out.version=NEXUS_HYPE_VERSION;out.instance=instance;
 if(!state||!panel||panel->origin!=events::EventOrigin::Preview||panel->channelId.rfind("panel-test-",0)!=0)return out;
 auto found=state->channels.find(*panel);if(found==state->channels.end()||!found->second->hypeTrain)return out;
 const auto &h=*found->second->hypeTrain;
 if(h.stage==events::HypeTrainStage::End||!h.progress||!h.goal||!h.level||h.expiresAt<=std::chrono::system_clock::now())return out;
 out.active=out.present=1;out.level=h.level;out.progress=*h.progress;out.goal=h.goal;copy(out.account,panel->channelId);copy(out.train_id,h.trainId);
 out.started_ms=std::chrono::duration_cast<std::chrono::milliseconds>(h.startedAt.time_since_epoch()).count();
 out.expires_ms=std::chrono::duration_cast<std::chrono::milliseconds>(h.expiresAt.time_since_epoch()).count();return out;
}
NexusHypeSnapshot hypeFeed(const state::SnapshotPtr &state,const std::string &account,bool active,bool authorized,uint64_t instance)
{
 NexusHypeSnapshot out{};out.size=sizeof(out);out.version=NEXUS_HYPE_VERSION;out.instance=instance;out.active=active;out.authorized=authorized;
 if(!active||!authorized||account.empty()||!state)return out;
 copy(out.account,account);
 auto found=state->channels.find({events::EventOrigin::Twitch,account});
 if(found==state->channels.end()||!found->second->hypeTrain)return out;
 const auto &h=*found->second->hypeTrain;
 if(h.stage==events::HypeTrainStage::End||!h.progress||!h.goal||!h.level||h.expiresAt<=std::chrono::system_clock::now())return out;
 out.present=1;out.level=h.level;out.progress=*h.progress;out.goal=h.goal;copy(out.train_id,h.trainId);
 out.started_ms=std::chrono::duration_cast<std::chrono::milliseconds>(h.startedAt.time_since_epoch()).count();
 out.expires_ms=std::chrono::duration_cast<std::chrono::milliseconds>(h.expiresAt.time_since_epoch()).count();return out;
}
NexusTickerSnapshot tickerFeed(const state::SnapshotPtr &state,const std::string &account,
                              bool active,bool authorized,uint64_t instance)
{
    NexusTickerSnapshot out{};out.size=sizeof(out);out.version=NEXUS_TICKER_VERSION;
    out.kind=NEXUS_TICKER_UPDATE;out.instance=instance;out.active=active;out.authorized=authorized;
    out.local_day=oto::state::localDay(std::chrono::system_clock::now());
    if(!active||!authorized||account.empty()||!state)return out;
    copy(out.account,account);
    auto found=state->channels.find({events::EventOrigin::Twitch,account});
    if(found==state->channels.end())return out;
    const auto &channel=*found->second;
    if(channel.daily.day==int(out.local_day)){
        out.daily_subs=channel.daily.subs;out.daily_bits=channel.daily.bits;out.saturated=channel.daily.saturated;
    }
    for(unsigned i=0;i<NEXUS_TICKER_COUNT;i++){
        const auto &latest=channel.latest[i];if(!latest.event)continue;
        const auto &data=latest.event->data();
        if(data.origin!=events::EventOrigin::Twitch||data.channelId!=account)continue;
        auto &row=out.latest[i];row.present=1;
        copy(row.id,data.id);row.occurred_ms=std::chrono::duration_cast<std::chrono::milliseconds>(data.occurredAt.time_since_epoch()).count();
        const events::User *actor=nullptr;
        if(const auto *e=std::get_if<events::FollowEvent>(&data.payload))actor=&e->follower;
        else if(const auto *e=std::get_if<events::SubEvent>(&data.payload)){
            actor=&e->subscriber;row.amount=e->totalMonths;row.counts_toward_totals=e->countsTowardTotals;
            row.tier=e->tier==events::SubscriptionTier::Tier3?3000:e->tier==events::SubscriptionTier::Tier2?2000:1000;
        }else if(const auto *e=std::get_if<events::CheerEvent>(&data.payload)){
            actor=e->user?&*e->user:nullptr;row.amount=e->bits;
        }else if(const auto *e=std::get_if<events::GiftEvent>(&data.payload)){
            actor=e->gifter?&*e->gifter:nullptr;row.amount=e->count;
            row.tier=e->tier==events::SubscriptionTier::Tier3?3000:e->tier==events::SubscriptionTier::Tier2?2000:1000;
        }else if(const auto *e=std::get_if<events::RaidEvent>(&data.payload)){
            actor=&e->raider;row.amount=e->viewers;
        }
        if(actor){copy(row.actor,actor->displayName);copy(row.actor_id,actor->id);}
        else row.anonymous=1;
    }
    return out;
}
}
