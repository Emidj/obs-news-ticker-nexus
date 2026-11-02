// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "text-layout.hpp"
#include <QImage>
#include <list>

namespace oto::visual {
enum class RasterEncoding { Premultiplied, StraightSrgb };
struct Frame { QImage image; std::vector<TextMetrics> textMetrics; std::vector<ImagePtr> assets; RasterEncoding encoding=RasterEncoding::Premultiplied; };
using FramePtr = std::shared_ptr<const Frame>;
struct VisualStats { size_t entries = 0, bytes = 0; uint64_t renders = 0, hits = 0, evictions = 0; };
class VisualEngine final {
public:
	explicit VisualEngine(size_t maxEntries = 32, size_t maxBytes = 16 * 1024 * 1024);
	VisualEngine(const VisualEngine &) = delete;
	VisualEngine &operator=(const VisualEngine &) = delete;
	// UI thread only. Inactive consumers need not call prepare at all.
	FramePtr prepare(const Scene &scene);
	FramePtr prepareRaster(QImage image);
	// Explicit straight sRGB pixels (e.g. Windows GDI). Existing scenes remain premultiplied.
	FramePtr prepareSrgbRaster(QImage image); // Bounded GUI preparation; identical authored planes share one GPU identity.
	// Single-line immutable strip for a clipped viewport. Width expands only when
	// needed, <=2048 px; shaping/rasterization/cache happen only at preparation.
	FramePtr prepareTextStrip(const TextNode &node);
	void clear() noexcept;
	VisualStats stats() const noexcept { return stats_; }
private:
	FramePtr prepareImpl(const Scene &scene, bool textStrip);
	FramePtr prepareRasterImpl(QImage image,bool srgb);
	struct Entry { QByteArray key; FramePtr frame; size_t bytes; };
	TextLayoutEngine text_;
	std::list<Entry> cache_;
	size_t maxEntries_, maxBytes_;
	VisualStats stats_;
};
} // namespace oto::visual
