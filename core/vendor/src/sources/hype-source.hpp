// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "hype-model.hpp"
#include "core/plugin-core.hpp"
#include "adapters/obs-visual-renderer.hpp"
#include "twitch/service.hpp"
#include "visual/asset-manager.hpp"
#include <mutex>
namespace oto::sources {
struct HypeInstance {
	std::mutex mutex;HypeSettings settings;
	std::atomic<int> width{900},height{180};
	std::atomic<bool> visible{false},queued{false},failed{false};
	HypeFramePtr frame;bool registered=false;std::string previewChannel;
	bool initialized=false;std::string observedTrain;state::ChannelKey observedChannel{events::EventOrigin::Simulation,""};std::uint64_t generation=0;
};
class HypeHub final:public QObject,public std::enable_shared_from_this<HypeHub> {
public:
	HypeHub(std::shared_ptr<PluginCore>,std::shared_ptr<visual::VisualEngine>,std::shared_ptr<ObsVisualRenderer>,std::shared_ptr<twitch::Service>,std::shared_ptr<const presets::Registry> = {},std::shared_ptr<visual::AssetManager> = {});
	presets::HypeStyle style(const QString &id)const;
	const std::shared_ptr<const presets::Registry> &presets()const{return presets_;}
	void request(const std::shared_ptr<HypeInstance> &);void refresh();void remove(const std::shared_ptr<HypeInstance> &);
	bool preview(const std::shared_ptr<HypeInstance> &,int action)noexcept;
	void draw(const std::shared_ptr<HypeInstance> &)noexcept;void stop()noexcept;
	bool timerActive()const{return timer_.isActive();}
private:
	void prepare(const std::shared_ptr<HypeInstance> &,const state::SnapshotPtr &,bool entrance=false);
	std::shared_ptr<PluginCore> core_;std::shared_ptr<visual::VisualEngine> visual_;std::shared_ptr<ObsVisualRenderer> renderer_;std::shared_ptr<twitch::Service> twitch_;
	std::vector<std::weak_ptr<HypeInstance>> instances_;HypeLabels labels_;QTimer timer_;
	std::atomic<bool> running_{true};bool previewTransaction_=false;
	std::shared_ptr<const presets::Registry> presets_;
	std::shared_ptr<visual::AssetManager> assets_;
};
void registerHypeSource(const std::shared_ptr<HypeHub> &);
}
