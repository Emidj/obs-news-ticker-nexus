// SPDX-License-Identifier: GPL-2.0-or-later
#include "asset-manager.hpp"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QBuffer>
#include <QImageReader>
#include <QGuiApplication>
#include <QThread>
#include <QRegularExpression>
#include <stdexcept>
namespace oto::visual {
AssetManager::AssetManager(QString root,size_t maxEntries,size_t maxBytes):
	root_(QDir(root).absolutePath()),maxEntries_(maxEntries),maxBytes_(maxBytes) {
	if(root.isEmpty())throw std::invalid_argument("Asset root is required");
}
bool AssetManager::validReference(const QString &reference) {
	static const QRegularExpression path("^[a-zA-Z0-9_-]+(?:/[a-zA-Z0-9_-]+)*\\.png$");
	return reference.size()<=128 && path.match(reference).hasMatch();
}
ImagePtr AssetManager::load(const QString &reference) {
	return loadRegion(reference,{});
}
ImagePtr AssetManager::loadRegion(const QString &reference,const QRect &region) {
	if(!qobject_cast<QGuiApplication *>(QCoreApplication::instance()) || QThread::currentThread()!=qApp->thread())throw std::logic_error("Asset loading requires GUI thread");
	if(!validReference(reference))throw std::invalid_argument("Invalid asset reference");
	const auto root=QFileInfo(root_).canonicalFilePath();
	const auto filename=QFileInfo(QDir(root_).filePath(reference)).canonicalFilePath();
	const auto relative=QDir(root).relativeFilePath(filename);
	if(root.isEmpty() || filename.isEmpty() || QDir::isAbsolutePath(relative) || relative==".." || relative.startsWith("../"))throw std::runtime_error("Asset outside package or unavailable");
	if(!region.isNull() && (!region.isValid() || !QRect(0,0,1000,1000).contains(region)))throw std::invalid_argument("Invalid authored atlas region");
	const auto key=region.isNull()?reference:reference+QString("@%1,%2,%3,%4").arg(region.x()).arg(region.y()).arg(region.width()).arg(region.height());
	return loadPath(filename,key,false,region);
}
ImagePtr AssetManager::loadPath(const QString &filename,const QString &key,bool userImage,QRect region) {
	if(!qobject_cast<QGuiApplication *>(QCoreApplication::instance()) || QThread::currentThread()!=qApp->thread())throw std::logic_error("Asset loading requires GUI thread");
	for(auto it=cache_.begin();it!=cache_.end();++it)if(it->reference==key) {
		auto image=it->image;cache_.splice(cache_.begin(),cache_,it);++stats_.hits;return image;
	}
	QFile file(filename);
	if(!file.open(QIODevice::ReadOnly) || file.size()>4*1024*1024)throw std::runtime_error("Asset file unavailable or too large");
	auto bytes=file.read(4*1024*1024+1);if(bytes.size()>4*1024*1024)throw std::runtime_error("Asset file too large");
	return decodeImage(key,std::move(bytes),userImage,region);
}
ImagePtr AssetManager::profileImage(const QString &userId) {
	if(!qobject_cast<QGuiApplication *>(QCoreApplication::instance()) || QThread::currentThread()!=qApp->thread())throw std::logic_error("Profile cache requires GUI thread");
	for(auto it=cache_.begin();it!=cache_.end();++it)if(it->reference=="profile:"+userId){auto image=it->image;cache_.splice(cache_.begin(),cache_,it);++stats_.hits;return image;}return {};
}
ImagePtr AssetManager::ingestProfileImage(const QString &userId,const QByteArray &bytes) {
	static const QRegularExpression id("^[0-9]{1,128}$");if(!id.match(userId).hasMatch() || bytes.size()>1024*1024)throw std::invalid_argument("Invalid profile image");
	return decodeImage("profile:"+userId,bytes,true);
}
ImagePtr AssetManager::decodeImage(const QString &key,QByteArray bytes,bool userImage,QRect region) {
	if(!qobject_cast<QGuiApplication *>(QCoreApplication::instance()) || QThread::currentThread()!=qApp->thread())throw std::logic_error("Image decoding requires GUI thread");
	QBuffer buffer(&bytes);buffer.open(QIODevice::ReadOnly);QImageReader reader(&buffer,userImage?QByteArray{}:QByteArray("png"));reader.setAutoDetectImageFormat(userImage);reader.setAutoTransform(userImage);
	const auto size=reader.size();
	// Wide authored atlases are accepted, but resident textures stay <=1024.
	// Bound both encoded input and temporary decoding before reading pixels.
	const bool squareAtlas=!userImage && size.width()==size.height() && size.height()<=2048;
	if(size.width()<1 || size.height()<1 || size.width()>4096 || size.height()>(userImage?4096:squareAtlas?2048:1024) || qint64(size.width())*size.height()>4*1024*1024)throw std::runtime_error("Asset dimensions exceed limit");
	const auto residentSize=size.scaled(1024,1024,Qt::KeepAspectRatio);
	const auto decodedSize=size.width()>1024 || size.height()>1024 ? residentSize : size;
	if(region.isNull())reader.setScaledSize(decodedSize);
	auto decoded=reader.read();
	if(!region.isNull() && !decoded.isNull()){
		const QRect selected(qRound(region.x()*size.width()/1000.0),qRound(region.y()*size.height()/1000.0),qRound(region.width()*size.width()/1000.0),qRound(region.height()*size.height()/1000.0));
		if(!decoded.rect().contains(selected))throw std::runtime_error("Atlas region exceeds decoded image");
		decoded=decoded.copy(selected);if(decoded.width()>1024 || decoded.height()>1024)decoded=decoded.scaled(1024,1024,Qt::KeepAspectRatio,Qt::SmoothTransformation);
	}
	if(decoded.isNull() || (!userImage && region.isNull() && decoded.size()!=decodedSize) || decoded.width()>1024 || decoded.height()>1024)throw std::runtime_error("Invalid image asset");
	const auto image=std::make_shared<const ImageData>(ImageData{decoded.convertToFormat(QImage::Format_RGBA8888_Premultiplied)});
	if(image->image.isNull())throw std::bad_alloc();
	++stats_.loads;const auto resident=size_t(image->image.sizeInBytes());
	if(maxEntries_ && resident<=maxBytes_) {
		cache_.push_front({key,image,resident});stats_.bytes+=resident;
		while(cache_.size()>maxEntries_ || stats_.bytes>maxBytes_) {stats_.bytes-=cache_.back().bytes;cache_.pop_back();++stats_.evictions;}
		stats_.entries=cache_.size();
	}
	return image;
}
void AssetManager::clear() noexcept {cache_.clear();stats_={};}
}
