// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "scene.hpp"
#include <list>
namespace oto::visual {
struct AssetStats { size_t entries=0,bytes=0; uint64_t loads=0,hits=0,evictions=0; };
class AssetManager final {
public:
	explicit AssetManager(QString root,size_t maxEntries=8,size_t maxBytes=8*1024*1024);
	AssetManager(const AssetManager &) = delete;
	AssetManager &operator=(const AssetManager &) = delete;
	static bool validReference(const QString &reference);
	ImagePtr load(const QString &reference); // GUI only, PNG, no polling/hot reload.
	ImagePtr loadRegion(const QString &reference,const QRect &thousandths); // Cache only the selected authored atlas cell.
	ImagePtr profileImage(const QString &userId);
	ImagePtr ingestProfileImage(const QString &userId,const QByteArray &bytes);
	void clear() noexcept;
	AssetStats stats() const noexcept { return stats_; }
private:
	ImagePtr loadPath(const QString &filename,const QString &key,bool userImage,QRect region={});
	ImagePtr decodeImage(const QString &key,QByteArray bytes,bool userImage,QRect region={});
	struct Entry { QString reference; ImagePtr image; size_t bytes; };
	QString root_;size_t maxEntries_,maxBytes_;
	std::list<Entry> cache_;AssetStats stats_;
};
}
