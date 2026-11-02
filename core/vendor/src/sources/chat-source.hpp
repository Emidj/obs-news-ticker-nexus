// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "chat-model.hpp"
#include "core/plugin-core.hpp"
#include "adapters/obs-visual-renderer.hpp"
#include "visual/asset-manager.hpp"
#include <QObject>
#include <atomic>
#include <mutex>
namespace oto::sources {
struct ChatInstance {
	std::mutex mutex;ChatSettings settings;std::atomic<int> width{600},height{700};
	std::atomic<bool> visible{false},queued{false},failed{false};ChatFramePtr frame;
	bool registered=false;std::string previewChannel;std::uint64_t cleared=0,generation=0;state::ChannelKey channel{events::EventOrigin::Simulation,""};
};
class ChatHub final:public QObject,public std::enable_shared_from_this<ChatHub> {
public:
	ChatHub(std::shared_ptr<PluginCore>,std::shared_ptr<visual::VisualEngine>,std::shared_ptr<ObsVisualRenderer>,std::shared_ptr<chat::EmoteCache>,std::shared_ptr<const presets::Registry> = {},std::shared_ptr<visual::AssetManager> = {});
	presets::ChatStyle style(const QString &id)const{return presets_?presets_->resolve(id).chatStyle.value_or(presets::ChatStyle{}):presets::ChatStyle{};}
	const presets::Registry *registry()const{return presets_.get();}
	void request(const std::shared_ptr<ChatInstance> &);void refresh();
	bool refreshEmotes(){return running_ && media_->requestCatalogRefresh();}
	bool preview(const std::shared_ptr<ChatInstance> &,bool longText,bool end,bool append=false,ChatPreviewEffects effects=ChatPreviewEffects::None)noexcept;
	bool moderatePreview(const std::shared_ptr<ChatInstance> &,events::ChatModerationAction)noexcept;
	void clear(const std::shared_ptr<ChatInstance> &);void remove(const std::shared_ptr<ChatInstance> &);
	void draw(const std::shared_ptr<ChatInstance> &)noexcept;void stop()noexcept;
private:
	void prepare(const std::shared_ptr<ChatInstance> &,const state::SnapshotPtr &,bool entrance=false);
	std::shared_ptr<PluginCore> core_;std::shared_ptr<visual::VisualEngine> visual_;std::shared_ptr<ObsVisualRenderer> renderer_;std::shared_ptr<chat::EmoteCache> media_;
	std::shared_ptr<const presets::Registry> presets_;std::shared_ptr<visual::AssetManager> assets_;
	std::vector<std::weak_ptr<ChatInstance>> instances_;std::atomic<bool> running_{true};bool transaction_=false;
};
void registerChatSource(const std::shared_ptr<ChatHub> &);
}
