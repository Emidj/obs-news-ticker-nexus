// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "shoutout-model.hpp"
#include "core/plugin-core.hpp"
#include "adapters/obs-visual-renderer.hpp"
#include "twitch/service.hpp"
#include <mutex>
#include <obs.h>
namespace oto::sources {
struct ShoutoutInstance {
	std::mutex mutex;ShoutoutSettings settings;QString testUser,notice;
	std::atomic<bool> visible{false},queued{false},preferences{true},failed{false},reset{false};
	std::atomic<std::int64_t> completion{0};std::atomic<std::uint64_t> nonce{0};
	ShoutoutFramePtr frame;ShoutoutCursor cursor;std::optional<state::LatestEvent> waiting;
	execution::PresentationConsumer presentation=0; // GUI-thread ownership only.
	bool registered=false,resolving=false;std::string previewChannel;
	std::shared_ptr<obs_weak_source_t> source;
};
class ShoutoutHub final:public QObject,public std::enable_shared_from_this<ShoutoutHub> {
public:
	ShoutoutHub(std::shared_ptr<PluginCore>,std::shared_ptr<visual::VisualEngine>,std::shared_ptr<ObsVisualRenderer>,std::shared_ptr<const presets::Registry>,std::shared_ptr<visual::AssetManager>,std::shared_ptr<twitch::Service>);
	const presets::Registry &presets()const{return *presets_;}
	void request(const std::shared_ptr<ShoutoutInstance> &);void refresh();
	void draw(const std::shared_ptr<ShoutoutInstance> &)noexcept;void tick(const std::shared_ptr<ShoutoutInstance> &)noexcept;
	bool preview(const std::shared_ptr<ShoutoutInstance> &,bool longText,bool end)noexcept;
	void remove(const std::shared_ptr<ShoutoutInstance> &);void stop()noexcept;
private:
	void prepare(const std::shared_ptr<ShoutoutInstance> &,const state::SnapshotPtr &);
	void startPreview(const std::shared_ptr<ShoutoutInstance> &,bool,events::ShoutoutEvent);
	std::shared_ptr<PluginCore> core_;std::shared_ptr<visual::VisualEngine> visual_;std::shared_ptr<ObsVisualRenderer> renderer_;
	std::shared_ptr<const presets::Registry> presets_;std::shared_ptr<twitch::Service> twitch_;
	std::shared_ptr<visual::AssetManager> assets_;
	std::vector<std::weak_ptr<ShoutoutInstance>> instances_;std::atomic<bool> running_{true};bool previewTransaction_=false;
};
void registerShoutoutSource(const std::shared_ptr<ShoutoutHub> &);
}
