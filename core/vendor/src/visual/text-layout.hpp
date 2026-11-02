// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "scene.hpp"
#include <QTextLayout>
#include <memory>

namespace oto::visual {
struct TextMetrics {
	int pixelSize = 0;
	int lines = 0;
	qreal width = 0, height = 0;
	bool overflow = false, elided = false;
};
struct LaidOutText {
	std::shared_ptr<const QTextLayout> layout;
	TextMetrics metrics;
	QString displayedText;
	qreal verticalOffset = 0;
};
class TextLayoutEngine final {
public:
	// QGuiApplication/UI thread only. Output can be retained, never mutated.
	LaidOutText prepare(const TextNode &node) const;
	static void validate(const TextNode &node);
};
} // namespace oto::visual
