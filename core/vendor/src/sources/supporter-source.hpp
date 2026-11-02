// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "supporter-model.hpp"
#include "supporter-frame.hpp"
#include "core/plugin-core.hpp"
#include "adapters/obs-visual-renderer.hpp"
#include "visual/asset-manager.hpp"
#include <QObject>
#include <QTimer>
#include <atomic>
#include <mutex>
namespace oto::sources {
struct BarInstance {
	std::mutex settingsMutex;
	BarSettings settings;
	std::atomic<int> width{1080}, height{96};
	std::atomic<bool> visible{false}, queued{false}, failed{false};
	BarFramePtr frame; // atomic_load/store only
	std::weak_ptr<const BarFrame> drawn; // OBS graphics context only; retains no hidden frames
	bool settled = false; // OBS graphics context only; skip clock/easing when idle
	bool registered = false; // GUI thread only
	std::string previewChannel; // GUI thread only, transient, never saved in OBS settings
};
class SupporterHub final : public QObject, public std::enable_shared_from_this<SupporterHub> {
public:
	SupporterHub(std::shared_ptr<PluginCore> core, std::shared_ptr<visual::VisualEngine> visual,
		std::shared_ptr<ObsVisualRenderer> renderer, std::shared_ptr<const presets::Registry> presets,
		std::shared_ptr<visual::AssetManager> assets);
	const presets::Registry &presets() const noexcept { return *presets_; }
	void request(const std::shared_ptr<BarInstance> &instance);
	void refresh(); // GUI thread; one notification for all sources after a batch
	bool preview(const std::shared_ptr<BarInstance> &instance, bool longText, bool end) noexcept; // GUI
	void remove(const std::shared_ptr<BarInstance> &instance); // any OBS thread
	void stop() noexcept; // GUI thread, before OBS graphics shutdown
	void draw(const std::shared_ptr<BarInstance> &instance) noexcept; // OBS graphics context
private:
	void prepare(const std::shared_ptr<BarInstance> &instance, const state::SnapshotPtr &snapshot, int today);
	void scheduleDay();
	std::shared_ptr<PluginCore> core_;
	std::shared_ptr<visual::VisualEngine> visual_;
	std::shared_ptr<ObsVisualRenderer> renderer_;
	std::shared_ptr<const presets::Registry> presets_;
	std::shared_ptr<visual::AssetManager> assets_;
	std::vector<std::weak_ptr<BarInstance>> instances_;
	QTimer midnight_;
	std::atomic<bool> running_{true};
	bool previewTransaction_ = false; // GUI: publish only final preview selection, not intermediate channel
};
void registerSupporterSource(const std::shared_ptr<SupporterHub> &hub);
}
