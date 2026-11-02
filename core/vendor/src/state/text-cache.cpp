// SPDX-License-Identifier: GPL-2.0-or-later
#include "text-cache.hpp"
#include <limits>
#include <stdexcept>

namespace oto::state {
namespace { void count(std::size_t &value) { if (value != std::numeric_limits<std::size_t>::max()) ++value; } }
TextCache::TextCache(std::size_t entries, std::size_t bytes) : maxEntries_(entries), maxBytes_(bytes)
{
	if (!entries || !bytes) throw std::invalid_argument("Cache limits must be positive");
}
TextPtr TextCache::intern(const std::string &text)
{
	const auto found = index_.find(text);
	if (found != index_.end()) {
		lru_.splice(lru_.begin(), lru_, found->second);
		count(stats_.hits);
		return found->second->text;
	}
	auto result = std::make_shared<const std::string>(text);
	if (text.size() > maxBytes_) { count(stats_.misses); return result; }
	// Allocate before eviction, preserving existing entries if allocation fails.
	lru_.push_front({result});
	try { index_.emplace(text, lru_.begin()); }
	catch (...) { lru_.pop_front(); throw; }
	while (index_.size() > maxEntries_ || stats_.bytes > maxBytes_ - text.size()) {
		const auto &old = *lru_.back().text;
		stats_.bytes -= old.size();
		index_.erase(old);
		lru_.pop_back();
		count(stats_.evictions);
	}
	stats_.bytes += text.size();
	stats_.entries = index_.size();
	count(stats_.misses);
	return result;
}
bool TextCache::invalidate(const std::string &text) noexcept
{
	const auto found = index_.find(text);
	if (found == index_.end()) return false;
	stats_.bytes -= found->first.size();
	lru_.erase(found->second);
	index_.erase(found);
	stats_.entries = index_.size();
	return true;
}
void TextCache::clear() noexcept { index_.clear(); lru_.clear(); stats_ = {}; }
}
