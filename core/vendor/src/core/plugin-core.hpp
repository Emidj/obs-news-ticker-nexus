// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "config-store.hpp"
#include "daily-store.hpp"
#include <functional>
#include "events/event-engine.hpp"
#include "execution/execution-queue.hpp"
#include "execution/presentation-queue.hpp"

#include <atomic>
#include <memory>

namespace oto {
enum class SettingsUpdate { Applied, Unchanged, NotRunning, ReadOnly, SaveFailed };
enum class ChatPreviewEffects { None,FFZMirrors,BTTVTransforms,BTTVMood,BTTVWide,FFZCursed,FFZGrow,Cheermotes };
struct SubmissionResult {
	events::EventPtr event;
	events::EventError error = events::EventError::None;
	execution::EnqueueResult queue;
	explicit operator bool() const noexcept { return event && bool(queue); }
};
struct BatchUpdate { std::size_t processed = 0; state::SnapshotPtr snapshot; std::vector<events::EventPtr> delivered; };

class PluginCore final : public IPluginServices {
public:
	PluginCore(std::unique_ptr<IConfigStore> config, std::unique_ptr<ILogger> logger,
		std::unique_ptr<IDailyStore> dailyStore = {});
	// One central UI consumer notification, after publication. Never called per frame.
	void setStateChanged(std::function<void()> callback) { stateChanged_ = std::move(callback); }
	~PluginCore() override;
	PluginCore(const PluginCore &) = delete;
	PluginCore &operator=(const PluginCore &) = delete;

	// Lifecycle and configuration writes run on the OBS UI thread only.
	void start();
	void stop() noexcept;
	SettingsUpdate setDiagnosticsEnabled(bool enabled);
	bool configurationWritable() const noexcept;
	// UI thread only. Normalize once, then submit to the single central queue.
	SubmissionResult processEvent(events::EventDraft draft);
	SubmissionResult simulateEvent(events::EventType type);
	// Shared, ephemeral panel TEST. Live routing and accepted presentations stay intact.
	SubmissionResult simulatePanelEvent(events::EventType type);
	void endPanelTest();
	const std::optional<state::ChannelKey> &panelTestChannel() const noexcept { return panelTestChannel_; }
	const state::ChannelKey &displayChannel() const noexcept { return panelTestChannel_ ? *panelTestChannel_ : selectedChannel_; }
	// Authorized channel history, normalized once; never submitted as live alerts.
	std::size_t restoreChannelHistory(const state::ChannelKey &channel, std::vector<events::EventDraft> drafts);
	BatchUpdate processPendingEvents(std::size_t limit = 32);
	BatchUpdate processPendingEventsFor(events::EventOrigin origin, const std::string &channel, std::size_t limit = 32);
	void selectChannel(state::ChannelKey channel);
	const state::ChannelKey &selectedChannel() const noexcept { return selectedChannel_; }
	// GUI only. Four bar/eight list events through the central queue/reducer, ephemeral channel.
	std::string createSourcePreview(bool longText, bool activityList = false);
	std::string createAlertPreview(events::EventType type, bool pack, bool longText,events::User actor={});
	std::string createShoutoutPreview(bool longText,events::User actor={},std::string login="demoviewer",std::string category={},std::optional<std::uint64_t> followers={});
	void releaseSourcePreview(const std::string &channel);
	std::string createHypePreview(bool longText=false);
	void updateHypePreview(const std::string &channel, events::HypeTrainStage stage, bool levelUp=false, bool longText=false);
	std::string createChatPreview(bool longText=false,ChatPreviewEffects effects=ChatPreviewEffects::None);
	void appendChatPreview(const std::string &channel,bool longText=false,ChatPreviewEffects effects=ChatPreviewEffects::None);
	void moderateChatPreview(const std::string &channel,events::ChatModerationAction);
	state::CacheStats cacheStats() const noexcept;
	bool cancelEvent(execution::JobId id) noexcept;
	std::size_t cancelPendingEvents() noexcept;
	std::size_t cancelPendingEventsFor(events::EventOrigin origin,const std::string &channel) noexcept;
	std::size_t pendingEventCount() const noexcept;
	// GUI-thread only; one playback turn shared by Animated Alerts and Shoutout.
	execution::PresentationQueue &presentations() noexcept {return presentationQueue_;}

	// Consumers may call these from any thread while holding a shared handle.
	bool running() const noexcept override;
	Settings settings() const noexcept override;
	state::SnapshotPtr sharedSnapshot() const noexcept override;
	void log(LogLevel level, std::string_view message) const noexcept override;

private:
	BatchUpdate applyBatch(const execution::PreparedBatch &batch);
	static constexpr unsigned runningBit = 1;
	static constexpr unsigned diagnosticsBit = 2;
	std::unique_ptr<IConfigStore> config_;
	std::unique_ptr<ILogger> logger_;
	std::unique_ptr<IDailyStore> dailyStore_;
	std::function<void()> stateChanged_;
	state::ChannelKey selectedChannel_{events::EventOrigin::Simulation,"sim-channel"};
	std::optional<state::ChannelKey> panelTestChannel_;
	std::atomic<unsigned> state_{0};
	bool configurationWritable_ = false;
	events::EventEngine eventEngine_;
	execution::ExecutionQueue executionQueue_;
	execution::PresentationQueue presentationQueue_;
	state::SharedState sharedState_;
	state::TextCache textCache_;
	std::uint64_t simulationSequence_ = 0;
	std::uint64_t previewSequence_ = 0;
};
} // namespace oto
