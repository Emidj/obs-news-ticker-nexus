// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "state/shared-state.hpp"
#include <functional>
#include <list>
#include <set>

namespace oto::execution {
using PresentationConsumer = std::uint64_t;
using PresentationTypes = std::array<bool,events::eventTypeCount>;
// One GUI-thread playback stage of the central execution pipeline. Jobs retain
// normalized events, never textures. All eligible instances share one turn;
// the next event waits for every participant's complete entry/hold/exit.
class PresentationQueue final {
public:
	PresentationQueue()=default;
	PresentationQueue(const PresentationQueue &)=delete;
	PresentationQueue &operator=(const PresentationQueue &)=delete;
	struct Job { state::LatestEvent entry; std::set<PresentationConsumer> consumers; };
	using Prepared = std::list<Job>;
	void setChanged(std::function<void()> changed){changed_=std::move(changed);}
	bool subscribe(PresentationConsumer &id,state::ChannelKey channel,PresentationTypes types,
		std::optional<state::ChannelKey> panelTest = {});
	void pause(PresentationConsumer id);
	void remove(PresentationConsumer &id);
	Prepared prepare(const std::vector<events::EventPtr> &,state::TextCache &) const;
	void publish(Prepared prepared) noexcept;
	std::optional<state::LatestEvent> next(PresentationConsumer id);
	void finish(PresentationConsumer id);
	void discardPanelTest(const state::ChannelKey &channel);
	std::size_t pending() const noexcept{return jobs_.size();}
	void clear() noexcept;
private:
	struct Subscription {state::ChannelKey channel;PresentationTypes types;bool visible=true;std::optional<state::ChannelKey> panelTest;};
	bool runnable(const Job &) const;void releasePaused();
	void prune();void notify() noexcept;
	std::map<PresentationConsumer,Subscription> subscriptions_;
	Prepared jobs_;Prepared::iterator active_=jobs_.end();PresentationConsumer sequence_=0;
	std::function<void()> changed_;
};
}
