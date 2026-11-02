/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Test-only provider. Never packaged or copied to portable OBS. */
#include "../shared/twitch-core-api.h"
#define NEXUS_CORE_BUILD
#include "../shared/twitch-core-runtime.h"
#include "../shared/twitch-ticker-api.h"
#include "../shared/twitch-hype-api.h"
#include <string.h>
static NexusHypeCallback preview_receiver;static void *preview_context;
static int bind_preview(uint64_t lease,NexusHypeCallback callback,void *ctx){if(lease!=1)return 0;preview_receiver=callback;preview_context=ctx;return 1;}
static int preview_snapshot(uint64_t lease,NexusHypeSnapshot *out,uint32_t size){(void)lease;(void)out;(void)size;return 0;}
__declspec(dllexport) const NexusHypeApi *nexus_twitch_core_get_hype_preview_api(uint32_t version,uint32_t size){static const NexusHypeApi api={sizeof(api),1,bind_preview,preview_snapshot};return version==1&&size<=sizeof(api)?&api:0;}
__declspec(dllexport) void fixture_emit_hype_preview(const NexusHypeSnapshot *message){if(preview_receiver)preview_receiver(preview_context,message);}
static NexusHypeCallback hype_receiver;static void *hype_context;
static int bind_hype(uint64_t lease,NexusHypeCallback callback,void *ctx){if(lease!=1)return 0;hype_receiver=callback;hype_context=ctx;return 1;}
static int hype_snapshot(uint64_t lease,NexusHypeSnapshot *out,uint32_t size){(void)lease;(void)out;(void)size;return 0;}
__declspec(dllexport) const NexusHypeApi *nexus_twitch_core_get_hype_api(uint32_t version,uint32_t size){static const NexusHypeApi api={sizeof(api),1,bind_hype,hype_snapshot};return version==1&&size<=sizeof(api)?&api:0;}
__declspec(dllexport) void fixture_emit_hype(const NexusHypeSnapshot *message){if(hype_receiver)hype_receiver(hype_context,message);}
static NexusTwitchCallback receiver;
static void *context;
static NexusTickerCallback ticker_receiver;
static void *ticker_context;
static int bind_ticker(uint64_t lease,NexusTickerCallback callback,void *ctx)
{if(lease!=1)return 0;ticker_receiver=callback;ticker_context=ctx;return 1;}
static int ticker_snapshot(uint64_t lease,NexusTickerSnapshot *out,uint32_t size)
{(void)lease;(void)out;(void)size;return 0;}
static int ticker_authorize(uint64_t lease){return lease==1;}
__declspec(dllexport) const NexusTickerApi *nexus_twitch_core_get_ticker_api(uint32_t version,uint32_t size)
{static const NexusTickerApi api={sizeof(api),1,bind_ticker,ticker_snapshot,ticker_authorize};return version==1&&size>=sizeof(api)?&api:0;}
__declspec(dllexport) void fixture_emit_feed(const NexusTickerSnapshot *message)
{if(ticker_receiver)ticker_receiver(ticker_context,message);}
int nexus_twitch_core_info(NexusTwitchCoreInfo *out,uint32_t size)
{if(!out||size<sizeof(*out))return 0;memset(out,0,sizeof(*out));out->abi=6;strcpy(out->version,"1.2.0");return 1;}
int nexus_twitch_runtime_retain(const char *path){return path&&*path;}
void nexus_twitch_runtime_release(void){}
static uint64_t acquire(NexusTwitchCallback callback,void *ctx)
{receiver=callback;context=ctx;return 1;}
static void release(uint64_t lease){(void)lease;receiver=0;context=0;ticker_receiver=0;ticker_context=0;hype_receiver=0;hype_context=0;preview_receiver=0;preview_context=0;}
static int snapshot(uint64_t lease,NexusTwitchMessage *out,uint32_t size)
{
    if(lease!=1||size<sizeof(*out))return 0;
    memset(out,0,sizeof(*out));out->size=sizeof(*out);out->kind=NEXUS_STATE;
    out->active=1;out->authorized=1;out->instance=1;strcpy(out->account,"fixture-channel");return 1;
}
__declspec(dllexport) const NexusTwitchApi *nexus_twitch_core_get_api(uint32_t abi,uint32_t size)
{
    static const NexusTwitchApi api={.size=sizeof(api),.abi=NEXUS_TWITCH_ABI,
        .acquire=acquire,.release=release,.snapshot=snapshot};
    return abi==NEXUS_TWITCH_ABI&&size<=sizeof(api)?&api:0;
}
__declspec(dllexport) void fixture_emit(const NexusTwitchMessage *message)
{if(receiver)receiver(context,message);}
