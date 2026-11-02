// SPDX-License-Identifier: GPL-2.0-or-later
#include "visual-engine.hpp"
#include <QDataStream>
#include <QGuiApplication>
#include <QPainter>
#include <QLinearGradient>
#include <QThread>
#include <QGlyphRun>
#include <QRawFont>
#include <QCryptographicHash>
#include <stdexcept>
#include <optional>
#include <cmath>

namespace oto::visual {
namespace {
QByteArray keyFor(const Scene &scene)
{
	if (scene.size.width() < 1 || scene.size.height() < 1 || scene.size.width() > 2048 || scene.size.height() > 2048 || scene.nodes.size() > 64)
		throw std::invalid_argument("Scene exceeds visual limits");
	qsizetype textUnits = 0;
	for (const auto &node : scene.nodes) {
		if (const auto *text = std::get_if<TextNode>(&node)) { TextLayoutEngine::validate(*text); textUnits += text->text.size(); }
		else if (const auto *image = std::get_if<ImageNode>(&node)) {
			if (!image->image || image->image->image.isNull() || image->image->image.width()>1024 || image->image->image.height()>1024 ||
				image->image->image.format()!=QImage::Format_RGBA8888_Premultiplied || image->box.width()<1 || image->box.height()<1 ||
				image->box.width()>4096 || image->box.height()>4096 || image->box.x() < -4096 || image->box.x()>4096 || image->box.y() < -4096 || image->box.y()>4096)
				throw std::invalid_argument("Invalid image node");
			const auto source=image->source.isNull()?image->image->image.rect():image->source;
			if(source.width()<1 || source.height()<1 || !image->image->image.rect().contains(source) || image->edge<0 || image->edge*2>=source.width())throw std::invalid_argument("Invalid image crop or end caps");
			if(image->darkOpacity<0 || image->darkOpacity>100 || (!image->darkBox.isNull() && !image->box.contains(image->darkBox)))throw std::invalid_argument("Invalid image dark fill");
			if(image->darkOpacity!=100 && (image->darkBox.isNull() || image->edge))throw std::invalid_argument("Dark fill requires a bounded simple image");
		}
		else if(const auto *card=std::get_if<CardNode>(&node)) {
			if(!card->color.isValid() || !card->edge.isValid() || card->radius<0 || card->radius>128 || card->glow<0 || card->glow>8 || card->box.width()<1 || card->box.height()<1 || card->box.width()>4096 || card->box.height()>4096 || card->box.x() < -4096 || card->box.x()>4096 || card->box.y() < -4096 || card->box.y()>4096)throw std::invalid_argument("Invalid card node");
		}
		else if (const auto *path=std::get_if<PathNode>(&node)) {
			if (!path->stroke.isValid() || !path->fill.isValid() || !std::isfinite(path->width) || path->width<.5f || path->width>16 || path->path.elementCount()>256)
				throw std::invalid_argument("Invalid vector path");
			for(int i=0;i<path->path.elementCount();++i) { const auto p=path->path.elementAt(i); if(!std::isfinite(p.x) || !std::isfinite(p.y) || std::abs(p.x)>4096 || std::abs(p.y)>4096)throw std::invalid_argument("Invalid vector coordinate"); }
		}
		else {
			const auto &rect = std::get<RectangleNode>(node);
			if (!rect.color.isValid() || rect.box.width() < 1 || rect.box.height() < 1 || rect.box.width() > 4096 || rect.box.height() > 4096 ||
				rect.box.x() < -4096 || rect.box.x() > 4096 || rect.box.y() < -4096 || rect.box.y() > 4096)
				throw std::invalid_argument("Invalid rectangle");
		}
	}
	if (textUnits > 16384) throw std::invalid_argument("Scene text budget exceeded");
	QByteArray key;
	QDataStream stream(&key, QIODevice::WriteOnly); stream.setVersion(QDataStream::Qt_6_0);
	stream << scene.size;
	for (const auto &node : scene.nodes) {
		stream << qint32(node.index());
		if (const auto *text = std::get_if<TextNode>(&node))
			stream << text->box << text->text << text->family << text->pixelSize << text->minimumPixelSize << text->color
				<< qint32(text->overflow) << qint32(text->horizontal) << qint32(text->vertical) << text->bold << text->italic << text->outline << text->gradientBottom << text->outlineWidth;
		else if (const auto *image = std::get_if<ImageNode>(&node))
			stream << image->box << quint64(reinterpret_cast<quintptr>(image->image.get())) << image->image->image.cacheKey() << image->source << image->edge << image->darkBox << image->darkOpacity << image->ellipse;
		else if(const auto *card=std::get_if<CardNode>(&node)) stream << card->box << card->color << card->edge << card->radius << card->glow;
		else if(const auto *path=std::get_if<PathNode>(&node)) stream << path->path << path->stroke << path->fill << path->width;
		else { const auto &rect = std::get<RectangleNode>(node); stream << rect.box << rect.color; }
	}
	if (stream.status() != QDataStream::Ok) throw std::runtime_error("Visual key serialization failed");
	return key;
}
}
VisualEngine::VisualEngine(size_t maxEntries, size_t maxBytes) : maxEntries_(maxEntries), maxBytes_(maxBytes) {}
FramePtr VisualEngine::prepare(const Scene &scene)
{
	return prepareImpl(scene,false);
}
FramePtr VisualEngine::prepareRaster(QImage image) {return prepareRasterImpl(std::move(image),false);}
FramePtr VisualEngine::prepareSrgbRaster(QImage image) {return prepareRasterImpl(std::move(image),true);}
FramePtr VisualEngine::prepareRasterImpl(QImage image,bool srgb)
{
	if(!qobject_cast<QGuiApplication *>(QCoreApplication::instance()) || QThread::currentThread()!=qApp->thread())throw std::logic_error("Raster preparation requires GUI thread");
	if(image.isNull() || image.width()>2048 || image.height()>2048 || image.format()!=(srgb?QImage::Format_RGBA8888:QImage::Format_RGBA8888_Premultiplied) || image.bytesPerLine()!=image.width()*4)throw std::invalid_argument("Invalid prepared raster");
	QByteArray key("authored-raster-v2");QDataStream stream(&key,QIODevice::Append);stream<<image.size()<<int(image.format());
	key.append(QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char *>(image.constBits()),image.sizeInBytes()),QCryptographicHash::Sha256));
	for(auto it=cache_.begin();it!=cache_.end();++it)if(it->key==key){auto frame=it->frame;cache_.splice(cache_.begin(),cache_,it);++stats_.hits;return frame;}
	auto frame=std::make_shared<Frame>();frame->image=std::move(image);frame->encoding=srgb?RasterEncoding::StraightSrgb:RasterEncoding::Premultiplied;++stats_.renders;
	const auto bytes=size_t(frame->image.sizeInBytes())+size_t(key.size());
	if(maxEntries_ && bytes<=maxBytes_){cache_.push_front({std::move(key),frame,bytes});stats_.bytes+=bytes;while(cache_.size()>maxEntries_ || stats_.bytes>maxBytes_){stats_.bytes-=cache_.back().bytes;cache_.pop_back();++stats_.evictions;}stats_.entries=cache_.size();}
	return frame;
}
FramePtr VisualEngine::prepareTextStrip(const TextNode &node)
{
	if(node.box.topLeft()!=QPoint(0,0) || node.overflow!=Overflow::Clip || node.horizontal!=Horizontal::Left)
		throw std::invalid_argument("Text strip requires an origin-aligned single line");
	return prepareImpl({node.box.size(),{node}},true);
}
FramePtr VisualEngine::prepareImpl(const Scene &scene, bool textStrip)
{
	if (!qobject_cast<QGuiApplication *>(QCoreApplication::instance()) || QThread::currentThread() != qApp->thread())
		throw std::logic_error("Visual preparation requires the GUI thread");
	auto key = keyFor(scene);
	key.append(textStrip ? '\1' : '\0');
	for (auto it = cache_.begin(); it != cache_.end(); ++it) {
		if (it->key == key) {
			auto frame = it->frame; cache_.splice(cache_.begin(), cache_, it); ++stats_.hits; return frame;
		}
	}
	auto frame = std::make_shared<Frame>();
	QSize outputSize=scene.size;
	std::optional<LaidOutText> strip;
	if(textStrip) {
		auto node=std::get<TextNode>(scene.nodes.front());node.box.setWidth(2048);
		strip=text_.prepare(node);
		// Preserve the established texture budget even for pathological input.
		if(strip->metrics.width>2046) {node.overflow=Overflow::Ellipsis;strip=text_.prepare(node);}
		if(strip->metrics.width>scene.size.width())outputSize.setWidth(std::min(2048,int(std::ceil(strip->metrics.width))+2));
		strip->metrics.overflow=strip->metrics.width>scene.size.width() || strip->metrics.height>scene.size.height();
	}
	frame->image = QImage(outputSize, QImage::Format_RGBA8888_Premultiplied);
	if (frame->image.isNull()) throw std::bad_alloc();
	frame->image.fill(Qt::transparent);
	QPainter painter(&frame->image);
	if (!painter.isActive()) throw std::runtime_error("Raster painter unavailable");
	painter.setRenderHint(QPainter::TextAntialiasing);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.setRenderHint(QPainter::SmoothPixmapTransform);
	for (const auto &node : scene.nodes) {
		if (const auto *text = std::get_if<TextNode>(&node)) {
			const auto prepared = strip ? *strip : text_.prepare(*text);
			painter.save(); painter.setClipRect(strip ? frame->image.rect() : text->box); painter.setPen(text->color);
			const auto origin=text->box.topLeft()+QPointF(0,prepared.verticalOffset);
			if(text->outlineWidth || text->gradientBottom.alpha()) {
				QPainterPath glyphs;
				for(const auto &run:prepared.layout->glyphRuns()) {
					const auto indexes=run.glyphIndexes();const auto positions=run.positions();
					for(qsizetype i=0;i<indexes.size();++i){auto path=run.rawFont().pathForGlyph(indexes[i]);path.translate(origin+positions[i]);glyphs.addPath(path);}
				}
				if(text->outlineWidth)painter.strokePath(glyphs,QPen(text->outline,text->outlineWidth,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
				QLinearGradient gradient(text->box.topLeft(),text->box.bottomLeft());gradient.setColorAt(0,text->color);gradient.setColorAt(1,text->gradientBottom.alpha()?text->gradientBottom:text->color);
				painter.fillPath(glyphs,gradient);
			} else prepared.layout->draw(&painter,origin);
			painter.restore(); frame->textMetrics.push_back(prepared.metrics);
		} else if (const auto *image = std::get_if<ImageNode>(&node)) {
			painter.save();if(image->ellipse){QPainterPath mask;mask.addEllipse(image->box);painter.setClipPath(mask,Qt::IntersectClip);}
			const auto source=image->source.isNull()?image->image->image.rect():image->source;
			if(image->darkOpacity!=100) {
				// One bounded preparation on the GUI thread, cached with the scene.
				// Work on a temporary row; never mutate the shared atlas or fade text.
				QImage row(image->box.size(),QImage::Format_RGBA8888_Premultiplied);row.fill(Qt::transparent);
				{QPainter crop(&row);crop.setRenderHint(QPainter::SmoothPixmapTransform);crop.drawImage(row.rect(),image->image->image,source);}
				const auto region=image->darkBox.translated(-image->box.topLeft());
				for(int y=region.top();y<=region.bottom();++y) {
					auto *line=row.scanLine(y);
					for(int x=region.left();x<=region.right();++x) {
						auto *p=line+x*4;const int a=p[3];if(!a)continue;
						const int brightness=std::max({int(p[0]),int(p[1]),int(p[2])})*255/a;
						const float dark=std::clamp((160-brightness)/64.0f,0.0f,1.0f);
						const int distance=std::min({x-region.left(),region.right()-x,y-region.top(),region.bottom()-y});
						const float feather=std::clamp(distance/2.0f,0.0f,1.0f);
						const float keep=1-dark*feather*(1-image->darkOpacity/100.0f);
						for(int c=0;c<4;++c)p[c]=uchar(qRound(p[c]*keep));
					}
				}
				painter.drawImage(image->box.topLeft(),row);
			}
			else if(!image->edge)painter.drawImage(image->box,image->image->image,source);
			else {
				const auto &b=image->box;const int e=image->edge;
				const int destinationEdge=std::min(std::max(1,b.width()/8),std::max(1,qRound(double(e)*b.height()/source.height())));
				painter.drawImage(QRect(b.x(),b.y(),destinationEdge,b.height()),image->image->image,QRect(source.x(),source.y(),e,source.height()));
				painter.drawImage(QRect(b.x()+destinationEdge,b.y(),b.width()-destinationEdge*2,b.height()),image->image->image,QRect(source.x()+e,source.y(),source.width()-e*2,source.height()));
				painter.drawImage(QRect(b.right()-destinationEdge+1,b.y(),destinationEdge,b.height()),image->image->image,QRect(source.right()-e+1,source.y(),e,source.height()));
			}
			frame->assets.push_back(image->image);
			painter.restore();
		} else if(const auto *card=std::get_if<CardNode>(&node)) {
			QLinearGradient gradient(card->box.topLeft(),card->box.bottomLeft());gradient.setColorAt(0,card->color.lighter(160));gradient.setColorAt(.45,card->color);gradient.setColorAt(1,card->color.darker(150));
			painter.save();
			for(int glow=card->glow;glow>0;--glow){QColor tint=card->edge;tint.setAlpha(std::min(70,card->edge.alpha())/(glow+1));painter.setPen(QPen(tint,2));painter.setBrush(Qt::NoBrush);painter.drawRoundedRect(QRectF(card->box).adjusted(-glow,-glow,glow,glow),card->radius+glow,card->radius+glow);}
			painter.setPen(QPen(card->edge,1));painter.setBrush(gradient);
			painter.drawRoundedRect(QRectF(card->box).adjusted(.5,.5,-.5,-.5),card->radius,card->radius);painter.restore();
		} else if(const auto *path=std::get_if<PathNode>(&node)) {
			painter.save();painter.setPen(QPen(path->stroke,path->width,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));painter.setBrush(path->fill);painter.drawPath(path->path);painter.restore();
		} else { const auto &rect = std::get<RectangleNode>(node); painter.fillRect(rect.box, rect.color); }
	}
	painter.end(); ++stats_.renders;
	const auto bytes = size_t(frame->image.sizeInBytes()) + size_t(key.size());
	if (maxEntries_ && bytes <= maxBytes_) {
		cache_.push_front({std::move(key), frame, bytes}); stats_.bytes += bytes;
		while (cache_.size() > maxEntries_ || stats_.bytes > maxBytes_) {
			stats_.bytes -= cache_.back().bytes; cache_.pop_back(); ++stats_.evictions;
		}
		stats_.entries = cache_.size();
	}
	return frame;
}
void VisualEngine::clear() noexcept { cache_.clear(); stats_ = {}; }
} // namespace oto::visual

