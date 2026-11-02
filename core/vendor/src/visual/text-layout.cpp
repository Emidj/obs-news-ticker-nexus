// SPDX-License-Identifier: GPL-2.0-or-later
#include "text-layout.hpp"
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QThread>
#include <QGlyphRun>
#include <algorithm>
#include <stdexcept>

namespace oto::visual {
namespace {
LaidOutText shape(const TextNode &node, const QString &text, int pixels)
{
	QFont font(node.family); font.setPixelSize(pixels); font.setBold(node.bold);font.setItalic(node.italic);
	LaidOutText result;
	result.displayedText = text;
	if (node.overflow == Overflow::Ellipsis) {
		result.displayedText = QFontMetricsF(font).elidedText(text, Qt::ElideRight, node.box.width());
		result.metrics.elided = result.displayedText != text;
	}
	auto layout = std::make_shared<QTextLayout>(result.displayedText, font);
	QTextOption option;
	option.setWrapMode(node.overflow == Overflow::Wrap ? QTextOption::WrapAtWordBoundaryOrAnywhere : QTextOption::NoWrap);
	option.setAlignment(node.horizontal == Horizontal::Left ? Qt::AlignLeft :
		(node.horizontal == Horizontal::Center ? Qt::AlignHCenter : Qt::AlignRight));
	option.setTextDirection(Qt::LayoutDirectionAuto);
	layout->setTextOption(option);
	layout->beginLayout();
	qreal height = 0, width = 0;
	for (;;) {
		auto line = layout->createLine();
		if (!line.isValid()) break;
		line.setLineWidth(node.box.width());
		line.setPosition(QPointF(0, height));
		height += line.height();
		width = std::max(width, line.naturalTextWidth());
	}
	layout->endLayout();
	result.metrics.pixelSize = pixels;
	result.metrics.lines = layout->lineCount();
	result.metrics.width = width; result.metrics.height = height;
	result.metrics.overflow = width > node.box.width() || height > node.box.height();
	const auto freeHeight = std::max(qreal(0), node.box.height() - height);
	result.verticalOffset = node.vertical == Vertical::Center ? freeHeight / 2 :
		(node.vertical == Vertical::Bottom ? freeHeight : 0);
	if(node.vertical==Vertical::InkCenter){
		QRectF ink;
		for(const auto &run:layout->glyphRuns()){
			const auto glyphs=run.glyphIndexes();const auto positions=run.positions();
			for(qsizetype i=0;i<glyphs.size();++i){auto bounds=run.rawFont().pathForGlyph(glyphs[i]).boundingRect();if(!bounds.isEmpty())ink=ink.united(bounds.translated(positions[i]));}
		}
		result.verticalOffset=ink.isEmpty()?freeHeight/2:(node.box.height()-ink.height())/2-ink.top();
	}
	result.layout = std::move(layout);
	return result;
}
}
void TextLayoutEngine::validate(const TextNode &node)
{
	if (node.box.width() < 1 || node.box.height() < 1 || node.box.width() > 4096 || node.box.height() > 4096 ||
		node.box.x() < -4096 || node.box.x() > 4096 || node.box.y() < -4096 || node.box.y() > 4096 ||
		node.text.size() > 4096 || node.family.isEmpty() || node.family.size() > 128 ||
		node.pixelSize < 8 || node.pixelSize > 128 || node.minimumPixelSize < 8 || node.minimumPixelSize > node.pixelSize ||
		!node.color.isValid() || !node.outline.isValid() || !node.gradientBottom.isValid() || node.outlineWidth<0 || node.outlineWidth>16 || node.overflow < Overflow::Clip || node.overflow > Overflow::AutoFit ||
		node.horizontal < Horizontal::Left || node.horizontal > Horizontal::Right ||
		node.vertical < Vertical::Top || node.vertical > Vertical::InkCenter)
		throw std::invalid_argument("Invalid text specification");
}
LaidOutText TextLayoutEngine::prepare(const TextNode &node) const
{
	if (!qobject_cast<QGuiApplication *>(QCoreApplication::instance()) || QThread::currentThread() != qApp->thread())
		throw std::logic_error("Text preparation requires the GUI thread");
	validate(node);
	auto text = node.text;
	text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
	text.replace(QChar('\r'), QChar('\n'));
	text.replace(QChar(0x2029), QChar('\n'));
	text.replace(QChar(0x2028), QChar('\n'));
	text.replace(QChar('\n'), node.overflow == Overflow::Wrap ? QChar(0x2028) : QChar(' '));
	auto result = shape(node, text, node.pixelSize);
	if (node.overflow != Overflow::AutoFit || !result.metrics.overflow) return result;
	// Bounded search in integer pixel sizes; minimum-size overflow is clipped.
	int low = node.minimumPixelSize, high = node.pixelSize - 1;
	result = shape(node, text, low);
	while (low <= high) {
		const auto middle = low + (high - low) / 2;
		auto trial = shape(node, text, middle);
		if (!trial.metrics.overflow) { result = std::move(trial); low = middle + 1; }
		else high = middle - 1;
	}
	return result;
}
} // namespace oto::visual
