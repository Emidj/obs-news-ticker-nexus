// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "events/event.hpp"
#include <chrono>
#include <deque>
#include <list>
#include <unordered_map>
#include <vector>

namespace oto::execution {
using JobId = std::uint64_t;
using Clock = std::chrono::steady_clock;
enum class Priority { High, Normal, Low };
enum class EnqueueStatus { Enqueued, Coalesced, Duplicate, Full, Stopped, InvalidEvent };
struct EnqueueResult {
	EnqueueStatus status = EnqueueStatus::Stopped;
	JobId jobId = 0;
	explicit operator bool() const noexcept
	{ return status == EnqueueStatus::Enqueued || status == EnqueueStatus::Coalesced; }
};
struct Delivery {
	JobId jobId;
	Priority priority;
	events::EventPtr event;
};
struct QueueLimits {
	std::size_t pending = 256;
	std::size_t recentIds = 2048;
	std::size_t batch = 32;
	Clock::duration dedupWindow = std::chrono::minutes(10);
};
class ExecutionQueue;
class PreparedBatch final {
public:
	const std::vector<Delivery> &items() const noexcept { return items_; }
private:
	friend class ExecutionQueue;
	const ExecutionQueue *owner_ = nullptr;
	std::uint64_t revision_ = 0;
	std::size_t nextTurn_ = 0;
	std::vector<Delivery> items_;
};

// UI-thread only. No worker, polling, callbacks or ownership of consumers.
class ExecutionQueue final {
public:
	explicit ExecutionQueue(QueueLimits limits = {});
	ExecutionQueue(const ExecutionQueue &) = delete;
	ExecutionQueue &operator=(const ExecutionQueue &) = delete;
	void start() noexcept { if (!running_) { running_ = true; ++revision_; } }
	void stop() noexcept;
	EnqueueResult enqueue(events::EventPtr event, Clock::time_point now = Clock::now());
	std::vector<Delivery> takeBatch(std::size_t limit = 32);
	PreparedBatch prepareBatch(std::size_t limit = 32) const;
	// Same queue/scheduler; scoped manual previews must not drain unrelated pending events.
	PreparedBatch prepareBatchFor(events::EventOrigin origin, const std::string &channel, std::size_t limit = 32) const;
	bool commitBatch(const PreparedBatch &batch) noexcept;
	bool cancel(JobId jobId) noexcept;
	std::size_t cancelAll() noexcept;
	std::size_t cancelFor(events::EventOrigin origin,const std::string &channel) noexcept;
	std::size_t size() const noexcept { return pending_.size(); }
	std::size_t recentIdCount() const noexcept { return seen_.size(); }
	static Priority priorityFor(const events::Event &event);
private:
	PreparedBatch prepare(std::size_t limit, const events::EventOrigin *origin, const std::string *channel) const;
	struct Pending { Delivery delivery; std::string key; };
	struct Seen { std::string key; Clock::time_point at; };
	void expire(Clock::time_point now);
	void remember(const std::string &key, Clock::time_point now);
	QueueLimits limits_;
	bool running_ = false;
	JobId nextId_ = 1; // Never reused across stop/start, so stale cancellation is harmless.
	std::size_t turn_ = 0;
	std::uint64_t revision_ = 0;
	Clock::time_point lastNow_{};
	std::list<Pending> pending_;
	std::deque<Seen> seenOrder_;
	std::unordered_map<std::string, Clock::time_point> seen_;
};
} // namespace oto::execution
