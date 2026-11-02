// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QColor>
#include <QRect>
#include <QString>
#include <QSize>
#include <QImage>
#include <QPainterPath>
#include <memory>
#include <variant>
#include <vector>

namespace oto::visual {
enum class Overflow { Clip, Ellipsis, Wrap, AutoFit };
enum class Horizontal { Left, Center, Right };
enum class Vertical { Top, Center, Bottom, InkCenter };
struct TextNode {
	QRect box;
	QString text;
	QString family = QStringLiteral("Segoe UI");
	int pixelSize = 24;
	int minimumPixelSize = 12;
	QColor color = Qt::white;
	Overflow overflow = Overflow::Clip;
	Horizontal horizontal = Horizontal::Left;
	Vertical vertical = Vertical::Top;
	bool bold = false;
	bool italic = false;
	QColor outline{Qt::transparent}, gradientBottom{Qt::transparent};
	int outlineWidth = 0;
};
struct RectangleNode { QRect box; QColor color; };
struct CardNode { QRect box; QColor color, edge; int radius = 6, glow = 0; };
struct ImageData { QImage image; };
using ImagePtr = std::shared_ptr<const ImageData>;
// Optional pixel crop and horizontal end caps. End caps retain their aspect
// ratio while only the middle stretches; useful for any ornamental UI frame.
struct ImageNode { QRect box; ImagePtr image; QRect source; int edge = 0; QRect darkBox; int darkOpacity = 100; bool ellipse = false; };
struct PathNode { QPainterPath path; QColor stroke, fill{Qt::transparent}; float width = 1; };
using Node = std::variant<RectangleNode, TextNode, ImageNode, CardNode, PathNode>;
struct Scene { QSize size; std::vector<Node> nodes; };
// Logical units are output pixels, independent of desktop scaling. No event,
// source, Twitch, preset, clock or animation concepts cross this module boundary.
} // namespace oto::visual
