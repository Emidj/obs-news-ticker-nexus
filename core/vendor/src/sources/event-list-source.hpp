// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "event-list-model.hpp"
#include "core/plugin-core.hpp"
#include "adapters/obs-visual-renderer.hpp"
#include "visual/asset-manager.hpp"
#include <QObject>
#include <atomic>
#include <mutex>
namespace oto::sources {
struct ListInstance {
	std::mutex mutex;
	ListSettings settings;
	std::atomic<int> width{600},height{600};
	std::atomic<bool> visible{false},queued{false},failed{false};
	ListFramePtr frame;
	std::weak_ptr<const ListFrame> drawn; // Graphics context only.
	bool settled=false,registered=false;
	std::string previewChannel; // GUI only, never serialized.
};
class EventListHub final:public QObject,public std::enable_shared_from_this<EventListHub> {
public:
	EventListHub(std::shared_ptr<PluginCore> core,std::shared_ptr<visual::VisualEngine> visual,std::shared_ptr<ObsVisualRenderer> renderer,std::shared_ptr<const presets::Registry> presets,std::shared_ptr<visual::AssetManager> assets);
	const presets::Style &style(const QString &id) const noexcept {const auto *definition=presets_->find(id);return definition && definition->eventListStyle ? *definition->eventListStyle : *presets_->resolve(QStringLiteral("minimal")).eventListStyle;}
	const presets::Registry &presets() const noexcept {return *presets_;}
	void request(const std::shared_ptr<ListInstance> &instance);
	void refresh();
	bool preview(const std::shared_ptr<ListInstance> &instance,bool longText,bool end) noexcept;
	void remove(const std::shared_ptr<ListInstance> &instance);
	void draw(const std::shared_ptr<ListInstance> &instance) noexcept;
	void stop() noexcept;
private:
	void prepare(const std::shared_ptr<ListInstance> &instance,const state::SnapshotPtr &snapshot,bool demoEntrance=false);
	std::shared_ptr<PluginCore> core_;
	std::shared_ptr<visual::VisualEngine> visual_;
	std::shared_ptr<ObsVisualRenderer> renderer_;
	std::shared_ptr<const presets::Registry> presets_;
	std::shared_ptr<visual::AssetManager> assets_;
	std::vector<std::weak_ptr<ListInstance>> instances_;
	ListLabels labels_;
	std::atomic<bool> running_{true};
	bool previewTransaction_=false;
};
void registerEventListSource(const std::shared_ptr<EventListHub> &hub);
}
