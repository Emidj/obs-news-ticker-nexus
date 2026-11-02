// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "alert-model.hpp"
#include "core/plugin-core.hpp"
#include "adapters/obs-visual-renderer.hpp"
#include "twitch/service.hpp"
#include <atomic>
#include <mutex>
#include <obs.h>
namespace oto::sources {
struct AlertInstance {
	std::mutex mutex;AlertSettings settings;
	std::atomic<bool> visible{false},queued{false},preferences{true},failed{false};
	std::atomic<std::int64_t> completion{0};AlertFramePtr frame;
	AlertCursor cursor;std::optional<state::LatestEvent> waiting;
	execution::PresentationConsumer presentation=0; // GUI-thread ownership only.
	bool registered=false;std::string previewChannel;
	QString testUser,previewNotice;std::atomic<std::uint64_t> previewNonce{0};bool resolving=false;
	std::shared_ptr<obs_weak_source_t> source;
};
class AlertHub final:public QObject,public std::enable_shared_from_this<AlertHub> {
public:
	AlertHub(std::shared_ptr<PluginCore>,std::shared_ptr<visual::VisualEngine>,std::shared_ptr<ObsVisualRenderer>,std::shared_ptr<const presets::Registry>,std::shared_ptr<visual::AssetManager>,std::shared_ptr<twitch::Service>);
	void request(const std::shared_ptr<AlertInstance> &);void refresh();
	void draw(const std::shared_ptr<AlertInstance> &)noexcept;void tick(const std::shared_ptr<AlertInstance> &)noexcept;
	bool preview(const std::shared_ptr<AlertInstance> &,int variant,bool longText,bool end)noexcept;
	void remove(const std::shared_ptr<AlertInstance> &);void stop()noexcept;
	const presets::Registry &presets()const noexcept{return *presets_;}
private:
	void prepare(const std::shared_ptr<AlertInstance> &,const state::SnapshotPtr &);
	void startPreview(const std::shared_ptr<AlertInstance> &,int,bool,events::User);
	std::shared_ptr<PluginCore> core_;std::shared_ptr<visual::VisualEngine> visual_;std::shared_ptr<ObsVisualRenderer> renderer_;
	std::shared_ptr<const presets::Registry> presets_;std::shared_ptr<visual::AssetManager> assets_;std::shared_ptr<twitch::Service> twitch_;
	std::vector<std::weak_ptr<AlertInstance>> instances_;std::atomic<bool> running_{true};bool previewTransaction_=false;
};
void registerAlertSource(const std::shared_ptr<AlertHub> &);
}
