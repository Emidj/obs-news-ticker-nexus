// SPDX-License-Identifier: MIT
#pragma once
#include <obs.h>
#include <chrono>
#include "playing/core/playing-core.hpp"

// Production, read-only OBS procedure. No diagnostic exports or Core mutations.
namespace nexus_now_playing {
inline std::string bounded_utf8(const std::string &value,std::size_t limit) {
    if(value.size()<=limit)return value;
    std::size_t end=limit;
    while(end&&(static_cast<unsigned char>(value[end])&0xc0)==0x80)--end;
    return value.substr(0,end);
}
template<class Source> void publish(void *opaque,calldata_t *args) noexcept {
    obs_data_t *json=obs_data_create();
    if(!json){calldata_set_string(args,"json","");return;}
    try {
        const auto frame=static_cast<Source*>(opaque)->state->consumer->publication.read();
        const auto snapshot=frame?frame->snapshot:playing::core::SnapshotPtr{};
        const char *state="waiting";
        if(snapshot){
            if(!snapshot->running)state="stopped";
            else if(snapshot->playback==playing::core::Playback::Playing&&snapshot->track)state="playing";
            else if(snapshot->playback==playing::core::Playback::Paused)state="paused";
        }
        obs_data_set_string(json,"schema","nexus.now-playing.v1");
        obs_data_set_string(json,"state",state);
        obs_data_set_double(json,"updated_at",std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count());
        if(snapshot&&snapshot->track&&snapshot->running&&snapshot->playback==playing::core::Playback::Playing){
            const auto artist=bounded_utf8(snapshot->track->artist,256),title=bounded_utf8(snapshot->track->title,512);
            obs_data_set_string(json,"artist",artist.c_str());
            obs_data_set_string(json,"title",title.c_str());
            obs_data_set_bool(json,"truncated",artist.size()!=snapshot->track->artist.size()||title.size()!=snapshot->track->title.size());
        }
        calldata_set_string(args,"json",obs_data_get_json(json));
    }catch(...){calldata_set_string(args,"json","");}
    obs_data_release(json);
}
template<class Source> void attach(obs_source_t *source,Source *instance) {
    proc_handler_add(obs_source_get_proc_handler(source),"void nexus_now_playing_v1(out string json)",&publish<Source>,instance);
}
}
