// SPDX-License-Identifier: GPL-2.0-or-later
#include "execution-queue.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>

namespace oto::execution {
std::size_t ExecutionQueue::cancelFor(events::EventOrigin origin,const std::string &channel) noexcept
{
	std::size_t removed=0;
	for(auto it=pending_.begin();it!=pending_.end();) {
		const auto &event=it->delivery.event->data();
		if(event.origin==origin && event.channelId==channel){it=pending_.erase(it);++removed;}else ++it;
	}
	if(removed)++revision_;
	return removed;
}
namespace {
std::string identity(const events::Event &event)
{
	const auto &data = event.data();
	// Length prefix avoids ambiguous channel/id boundaries, including ':' in IDs.
	return std::to_string(static_cast<int>(data.origin)) + ":" + std::to_string(data.channelId.size()) +
	       ":" + data.channelId + data.id;
}
constexpr std::array<Priority, 7> schedule{Priority::High, Priority::High, Priority::High,
	Priority::High, Priority::Normal, Priority::Normal, Priority::Low};
}
ExecutionQueue::ExecutionQueue(QueueLimits limits) : limits_(limits)
{
	if (!limits.pending || !limits.recentIds || !limits.batch || limits.dedupWindow <= Clock::duration::zero())
		throw std::invalid_argument("Queue limits must be positive");
}
Priority ExecutionQueue::priorityFor(const events::Event &event)
{
	switch (events::typeOf(event.data().payload)) {
	case events::EventType::Raid:
	case events::EventType::Sub:
	case events::EventType::Gift: return Priority::High;
	case events::EventType::ChatModeration: return Priority::High;
	case events::EventType::Follow:
	case events::EventType::Cheer: return Priority::Normal;
	case events::EventType::Shoutout: return Priority::Normal;
	case events::EventType::Chat:
	case events::EventType::HypeTrain: return Priority::Low;
	}
	return Priority::Low;
}
void ExecutionQueue::expire(Clock::time_point now)
{
	while (!seenOrder_.empty() && now - seenOrder_.front().at >= limits_.dedupWindow) {
		seen_.erase(seenOrder_.front().key);
		seenOrder_.pop_front();
	}
}
void ExecutionQueue::remember(const std::string &key, Clock::time_point now)
{
	// Allocate first; failures leave pending jobs and dedup history unchanged.
	seenOrder_.push_back({key, now});
	try { seen_.emplace(key, now); }
	catch (...) { seenOrder_.pop_back(); throw; }
	while (seenOrder_.size() > limits_.recentIds) {
		seen_.erase(seenOrder_.front().key);
		seenOrder_.pop_front();
	}
}
EnqueueResult ExecutionQueue::enqueue(events::EventPtr event, Clock::time_point now)
{
	if (!running_) return {EnqueueStatus::Stopped};
	if (!event) return {EnqueueStatus::InvalidEvent};
	now = std::max(now, lastNow_);
	lastNow_ = now;
	expire(now);
	auto key = identity(*event);
	if (seen_.find(key) != seen_.end()) return {EnqueueStatus::Duplicate};
	// Pending IDs remain protected even after TTL expiration or history eviction.
	for (const auto &job : pending_)
		if (job.key == key) return {EnqueueStatus::Duplicate, job.delivery.jobId};
	const auto &data = event->data();
	const auto *progress = std::get_if<events::HypeTrainEvent>(&data.payload);
	if (progress && progress->stage == events::HypeTrainStage::Progress) {
		for (auto it = pending_.rbegin(); it != pending_.rend(); ++it) {
			const auto &previous = it->delivery.event->data();
			if (previous.channelId != data.channelId || previous.origin != data.origin) continue;
			const auto *old = std::get_if<events::HypeTrainEvent>(&previous.payload);
			if (!old) continue;
			// Do not combine across Begin/End boundaries. All Hype events share a priority.
			if (old->stage != events::HypeTrainStage::Progress || old->trainId != progress->trainId) break;
			remember(key, now);
			if (data.occurredAt >= previous.occurredAt) {
				it->delivery.event = std::move(event);
				it->key.swap(key);
			}
			++revision_;
			return {EnqueueStatus::Coalesced, it->delivery.jobId};
		}
	}
	if (pending_.size() >= limits_.pending) return {EnqueueStatus::Full};
	if (nextId_ == std::numeric_limits<JobId>::max()) throw std::overflow_error("Queue job IDs exhausted");
	const auto priority = priorityFor(*event);
	pending_.push_back({{nextId_, priority, std::move(event)}, std::move(key)});
	try { remember(pending_.back().key, now); }
	catch (...) { pending_.pop_back(); throw; }
	++revision_;
	return {EnqueueStatus::Enqueued, nextId_++};
}
std::vector<Delivery> ExecutionQueue::takeBatch(std::size_t limit)
{
	auto batch = prepareBatch(limit);
	if (!commitBatch(batch)) return {};
	return std::move(batch.items_);
}
PreparedBatch ExecutionQueue::prepareBatch(std::size_t limit) const
{ return prepare(limit, nullptr, nullptr); }
PreparedBatch ExecutionQueue::prepareBatchFor(events::EventOrigin origin, const std::string &channel, std::size_t limit) const
{ return prepare(limit, &origin, &channel); }
PreparedBatch ExecutionQueue::prepare(std::size_t limit, const events::EventOrigin *origin, const std::string *channel) const
{
	PreparedBatch batch;
	batch.owner_ = this;
	batch.revision_ = revision_;
	batch.nextTurn_ = turn_;
	if (!running_) return batch;
	const auto eligible = [origin, channel](const auto &job) {
		const auto &data = job.delivery.event->data();
		return !origin || (data.origin == *origin && data.channelId == *channel);
	};
	limit = std::min({limit, limits_.batch, std::size_t(std::count_if(pending_.begin(), pending_.end(), eligible))});
	batch.items_.reserve(limit);
	std::array<std::list<Pending>::const_iterator, 3> cursors{pending_.begin(), pending_.begin(), pending_.begin()};
	while (batch.items_.size() < limit) {
		const auto priority = schedule[batch.nextTurn_];
		batch.nextTurn_ = (batch.nextTurn_ + 1) % schedule.size();
		auto &cursor = cursors[static_cast<std::size_t>(priority)];
		cursor = std::find_if(cursor, pending_.end(), [priority, &eligible](const auto &job) {
			return job.delivery.priority == priority && eligible(job);
		});
		if (cursor == pending_.end()) continue;
		batch.items_.push_back(cursor->delivery);
		++cursor;
	}
	return batch;
}
bool ExecutionQueue::commitBatch(const PreparedBatch &batch) noexcept
{
	if (!running_ || batch.owner_ != this || batch.revision_ != revision_) return false;
	if (batch.items_.empty()) return true;
	for (const auto &item : batch.items_) {
		const auto found = std::find_if(pending_.begin(), pending_.end(), [&](const auto &job) {
			return job.delivery.jobId == item.jobId;
		});
		pending_.erase(found); // Revision check guarantees this exact job still exists.
	}
	turn_ = batch.nextTurn_;
	++revision_;
	return true;
}
bool ExecutionQueue::cancel(JobId jobId) noexcept
{
	const auto found = std::find_if(pending_.begin(), pending_.end(), [jobId](const auto &job) {
		return job.delivery.jobId == jobId;
	});
	if (found == pending_.end()) return false;
	pending_.erase(found);
	++revision_;
	return true;
}
std::size_t ExecutionQueue::cancelAll() noexcept
{
	const auto count = pending_.size();
	pending_.clear();
	if (count) ++revision_;
	return count;
}
void ExecutionQueue::stop() noexcept
{
	++revision_;
	running_ = false;
	cancelAll();
	seen_.clear();
	seenOrder_.clear();
	turn_ = 0;
	lastNow_ = {};
}
} // namespace oto::execution
