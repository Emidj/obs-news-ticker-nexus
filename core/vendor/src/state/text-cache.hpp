// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <list>
#include <memory>
#include <string>
#include <unordered_map>

namespace oto::state {
using TextPtr = std::shared_ptr<const std::string>;
struct CacheStats { std::size_t entries = 0, bytes = 0, hits = 0, misses = 0, evictions = 0; };
// CPU text interning, UI-thread mutations only. Returned immutable handles can be shared.
class TextCache final {
public:
	explicit TextCache(std::size_t entries = 256, std::size_t bytes = 64 * 1024);
	TextCache(const TextCache &) = delete;
	TextCache &operator=(const TextCache &) = delete;
	TextPtr intern(const std::string &text);
	bool invalidate(const std::string &text) noexcept;
	void clear() noexcept;
	CacheStats stats() const noexcept { return stats_; }
private:
	struct Entry { TextPtr text; };
	std::size_t maxEntries_, maxBytes_;
	CacheStats stats_;
	std::list<Entry> lru_;
	std::unordered_map<std::string, std::list<Entry>::iterator> index_;
};
}
