// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "events/event.hpp"
#include "twitch/transport.hpp"
#include "visual/visual-engine.hpp"
#include "emote-effects.hpp"
#include "cheermotes.hpp"
#include <QJsonDocument>
#include <deque>
#include <list>
#include <map>
#include <set>
namespace oto::chat {
class PersonalCatalog;
struct Emote { visual::FramePtr atlas; std::vector<int> ends; int columns=1,cell=56; int duration()const{return ends.empty()?0:ends.back();} QRect frame(int elapsed)const; };
using EmotePtr=std::shared_ptr<const Emote>;
struct Token {QString text,url;bool zeroWidth=false;int layerOf=-1;unsigned effects=0;bool modifier=false,prefix=false;QRectF layerRect{0,0,1,1};QColor color;};
struct CatalogEntry {QString name,url;int priority=0;bool zeroWidth=false;unsigned effects=0;bool modifier=false,prefix=false;QRectF layerRect{0,0,1,1};bool operator==(const CatalogEntry &o)const{return name==o.name && url==o.url && priority==o.priority && zeroWidth==o.zeroWidth && effects==o.effects && modifier==o.modifier && prefix==o.prefix && layerRect==o.layerRect;}};
// Resolve layers before layout, including while image downloads are pending.
std::vector<Token> composeTokens(const std::vector<Token> &);
struct BadgeEntry {QString setId,version,url;};
std::optional<std::vector<BadgeEntry>> parseBadgeCatalog(const QByteArray &);
bool publicBadgeUrl(const QString &);
std::optional<std::vector<CatalogEntry>> parseCatalogSnapshot(int provider,const QByteArray &,bool channel);
std::vector<CatalogEntry> parseCatalog(int provider,const QByteArray &,bool channel);
bool publicEmoteUrl(const QString &);
EmotePtr decodeEmote(visual::VisualEngine &,const QByteArray &);
class EmoteCache final:public QObject {
public:
	using Fetch=std::function<void(QString,std::function<void(twitch::HttpReply)>)>;
	using Clock=std::function<std::chrono::steady_clock::time_point()>;
	explicit EmoteCache(std::shared_ptr<visual::VisualEngine>,Fetch fetch={},Clock clock=std::chrono::steady_clock::now);
	~EmoteCache()override;
	void ensureAuthor(const QString &);
	void enablePersonalFeed();
	bool ingestPersonalBTTV(const QByteArray &);
	void setChanged(std::function<void()> f){changed_=std::move(f);}
	void ensureChannel(const QString &channel);
	void setCatalogActive(bool);
	void refreshCatalogs();
	bool requestCatalogRefresh();
	void ensureBadges(const QString &channel);
	void ensureCheermotes(const QString &channel);
	bool ingestBadges(const QString &channel,const QByteArray &); // Empty channel = global.
	std::vector<QString> badgeUrls(const std::vector<events::ChatBadge> &)const;
	EmotePtr getBadge(const QString &url);
	std::vector<Token> tokens(const events::ChatEvent &,bool thirdParty)const;
	EmotePtr get(const QString &url); // GUI only; one request per asset, bounded cache.
	void stop();
	std::uint64_t revision()const{return revision_;}
	size_t bytes()const{return bytes_;}
	// Same bounded insertion path used by fixtures and downloads.
	bool ingest(const QString &url,const QByteArray &);
private:
	struct Provider {std::map<QString,CatalogEntry> entries;bool pending=false;unsigned failures=0;std::chrono::steady_clock::time_point retry{},expires{};};
	struct Catalog {std::array<Provider,3> providers;std::map<QString,CatalogEntry> entries;std::uint64_t epoch=0;};
	struct Entry {QString url;EmotePtr image;std::chrono::steady_clock::time_point retry;};
	struct Request {QString url,channel;int provider=-1;std::uint64_t epoch=0;};
	struct Badges {std::map<std::pair<QString,QString>,QString> entries;bool pending=false;std::chrono::steady_clock::time_point retry{},expires{};};
	EmotePtr getResource(const QString &url);
	bool enqueue(Request);void pump();void catalog(const Request &,const twitch::HttpReply &);void scheduleCatalogs();void discardCatalogRequests(bool all);void notify();
	std::shared_ptr<visual::VisualEngine> visual_;Fetch fetch_;Clock clock_;QTimer catalogTimer_;bool catalogActive_=true;std::uint64_t catalogEpoch_=0;std::optional<std::chrono::steady_clock::time_point> lastManual_;
	std::map<QString,Catalog> catalogs_;std::list<Entry> cache_;std::deque<Request> pending_;std::set<QString> requested_;
	QString channel_;int busy_=0;bool running_=true;size_t bytes_=0;std::uint64_t revision_=0;
	QString badgeChannel_;std::map<QString,Badges> badges_;QTimer badgeRetry_;
	std::function<void()> changed_;
	std::unique_ptr<PersonalCatalog> personal_;
	std::vector<CheerTier> cheers_;QString cheerChannel_;std::uint64_t cheerEpoch_=0;bool cheerPending_=false;std::chrono::steady_clock::time_point cheerExpires_{},cheerRetryDue_{};QTimer cheerRetry_;
};
}
