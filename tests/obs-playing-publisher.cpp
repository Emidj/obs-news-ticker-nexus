// SPDX-License-Identifier: MIT
#include "../integrations/obs-playing/nexus-now-playing-publisher.hpp"
#include <cstdio>
#include <cstring>
using namespace playing;
struct Frame {core::SnapshotPtr snapshot;};
struct Publication {std::shared_ptr<const Frame> current;auto read()const{return current;}};
struct Consumer {Publication publication;};struct State {std::shared_ptr<Consumer> consumer=std::make_shared<Consumer>();};struct Source {std::shared_ptr<State> state=std::make_shared<State>();};
static int checks,failures;
static void check(const char *name,bool ok){std::printf("%s %s\n",ok?"PASS":"FAIL",name);++checks;failures+=!ok;}
int main(){
 Source source;calldata_t args;calldata_init(&args);
 auto read=[&](){nexus_now_playing::publish<Source>(&source,&args);return obs_data_create_from_json(calldata_string(&args,"json"));};
 auto data=read();check("empty publication waits",!std::strcmp(obs_data_get_string(data,"state"),"waiting"));obs_data_release(data);
 auto snapshot=std::make_shared<core::Snapshot>();snapshot->running=true;snapshot->playback=core::Playback::Playing;auto track=std::make_shared<core::TrackState>();track->artist="DJ";track->title="Current track";snapshot->track=track;source.state->consumer->publication.current=std::make_shared<Frame>(Frame{snapshot});
 data=read();check("real Snapshot/TrackState fields serialize",!std::strcmp(obs_data_get_string(data,"state"),"playing")&&!std::strcmp(obs_data_get_string(data,"title"),"Current track"));check("versioned schema and UTC timestamp supplied",!std::strcmp(obs_data_get_string(data,"schema"),"nexus.now-playing.v1")&&obs_data_get_double(data,"updated_at")>0);obs_data_release(data);
 track->title="changed";check("calldata owns the serialized response",std::strstr(calldata_string(&args,"json"),"Current track")!=nullptr);
 snapshot->playback=core::Playback::Paused;data=read();check("pause cannot report active title",!std::strcmp(obs_data_get_string(data,"state"),"paused")&&!obs_data_has_user_value(data,"title"));obs_data_release(data);
 snapshot->running=false;data=read();check("stopped Core clears active metadata",!std::strcmp(obs_data_get_string(data,"state"),"stopped")&&!obs_data_has_user_value(data,"title"));obs_data_release(data);
 snapshot->running=true;snapshot->playback=core::Playback::Playing;snapshot->track.reset();data=read();check("playing without verified track does not invent one",!std::strcmp(obs_data_get_string(data,"state"),"waiting"));obs_data_release(data);
 snapshot->track=track;track->artist=std::string(255,'a')+u8"é";track->title=std::string(700,'b');data=read();check("long metadata is bounded without splitting UTF-8",std::strlen(obs_data_get_string(data,"artist"))==255&&std::strlen(obs_data_get_string(data,"title"))==512&&obs_data_get_bool(data,"truncated"));obs_data_release(data);
 calldata_free(&args);std::printf("RESULT checks=%d failures=%d\n",checks,failures);return failures?1:0;
}
