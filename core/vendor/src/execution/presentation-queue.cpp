// SPDX-License-Identifier: GPL-2.0-or-later
#include "presentation-queue.hpp"
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace oto::execution {
namespace {
const events::User *actor(const events::EventPayload &payload){
	return std::visit([](const auto &item)->const events::User *{
		using T=std::decay_t<decltype(item)>;
		if constexpr(std::is_same_v<T,events::FollowEvent>)return &item.follower;
		else if constexpr(std::is_same_v<T,events::SubEvent>)return &item.subscriber;
		else if constexpr(std::is_same_v<T,events::CheerEvent>)return item.user?&*item.user:nullptr;
		else if constexpr(std::is_same_v<T,events::GiftEvent>)return item.gifter?&*item.gifter:nullptr;
		else if constexpr(std::is_same_v<T,events::RaidEvent>)return &item.raider;
		else if constexpr(std::is_same_v<T,events::ShoutoutEvent>)return &item.target;
		else return nullptr;
	},payload);
}
}
bool PresentationQueue::subscribe(PresentationConsumer &id,state::ChannelKey channel,PresentationTypes types,std::optional<state::ChannelKey> panelTest){
	if(panelTest && panelTest->origin!=events::EventOrigin::Preview)throw std::invalid_argument("Panel TEST must be ephemeral");
	const auto found=subscriptions_.find(id);
	if(found!=subscriptions_.end() && found->second.channel==channel && found->second.types==types && found->second.panelTest==panelTest){const bool resumed=!found->second.visible;found->second.visible=true;if(resumed)notify();return resumed;}
	if(!id){if(sequence_==std::numeric_limits<PresentationConsumer>::max())throw std::overflow_error("Presentation consumers exhausted");id=++sequence_;}
	for(auto &job:jobs_){const state::ChannelKey key{job.entry.event->data().origin,job.entry.event->data().channelId};if((!(key==channel) && !(panelTest && key==*panelTest)) || !types[state::slot(events::typeOf(job.entry.event->data().payload))])job.consumers.erase(id);}
	subscriptions_[id]={std::move(channel),types,true,std::move(panelTest)};prune();return true;
}
void PresentationQueue::pause(PresentationConsumer id){
	const auto found=subscriptions_.find(id);if(found==subscriptions_.end() || !found->second.visible)return;
	found->second.visible=false;releasePaused();notify();
}
void PresentationQueue::remove(PresentationConsumer &id){
	if(!id)return;subscriptions_.erase(id);for(auto &job:jobs_)job.consumers.erase(id);id=0;prune();
}
PresentationQueue::Prepared PresentationQueue::prepare(const std::vector<events::EventPtr> &events,state::TextCache &cache) const{
	Prepared result;
	for(const auto &event:events){
		Job job;job.entry.event=event;
		const state::ChannelKey channel{event->data().origin,event->data().channelId};
		const auto type=state::slot(events::typeOf(event->data().payload));
		for(const auto &[id,subscription]:subscriptions_)if(subscription.visible && subscription.types[type] &&
			((channel.origin!=events::EventOrigin::Preview && subscription.channel==channel) ||
			(subscription.panelTest && *subscription.panelTest==channel)))job.consumers.insert(id);
		if(job.consumers.empty())continue;
		if(const auto *user=actor(event->data().payload))job.entry.displayName=cache.intern(user->displayName);
		result.push_back(std::move(job));
	}
	return result;
}
void PresentationQueue::publish(Prepared prepared) noexcept{jobs_.splice(jobs_.end(),prepared);}
bool PresentationQueue::runnable(const Job &job) const{
	for(const auto id:job.consumers){const auto found=subscriptions_.find(id);if(found!=subscriptions_.end() && found->second.visible)return true;}return false;
}
void PresentationQueue::releasePaused(){if(active_!=jobs_.end() && !runnable(*active_))active_=jobs_.end();}
std::optional<state::LatestEvent> PresentationQueue::next(PresentationConsumer id){
	const auto subscription=subscriptions_.find(id);if(subscription==subscriptions_.end() || !subscription->second.visible)return {};
	if(active_==jobs_.end())for(auto it=jobs_.begin();it!=jobs_.end();++it)if(runnable(*it)){active_=it;break;}
	if(active_==jobs_.end() || !active_->consumers.count(id))return {};
	return active_->entry;
}
void PresentationQueue::finish(PresentationConsumer id){
	if(active_==jobs_.end() || !active_->consumers.erase(id))return;
	if(active_->consumers.empty()){jobs_.erase(active_);active_=jobs_.end();notify();}
	else if(!runnable(*active_)){active_=jobs_.end();notify();}
}
void PresentationQueue::prune(){
	bool changed=false;
	for(auto it=jobs_.begin();it!=jobs_.end();)if(it->consumers.empty()){if(it==active_)active_=jobs_.end();it=jobs_.erase(it);changed=true;}else ++it;
	releasePaused();
	if(changed)notify();
}
void PresentationQueue::discardPanelTest(const state::ChannelKey &channel){
	if(channel.origin!=events::EventOrigin::Preview)throw std::invalid_argument("Only panel TEST may be discarded");
	for(auto &job:jobs_)if(job.entry.event->data().origin==channel.origin && job.entry.event->data().channelId==channel.channelId)job.consumers.clear();
	prune();
}
void PresentationQueue::notify() noexcept{if(changed_)try{changed_();}catch(...){} }
void PresentationQueue::clear() noexcept{jobs_.clear();active_=jobs_.end();subscriptions_.clear();changed_={};}
}
