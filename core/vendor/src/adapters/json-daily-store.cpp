// SPDX-License-Identifier: GPL-2.0-or-later
#include "json-daily-store.hpp"
#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <stdexcept>
#include <limits>
#include <type_traits>
#include <set>
namespace oto {
namespace {
// 16 channels × 64 bounded activity records, in addition to daily/last fields.
constexpr qint64 limit = 2 * 1024 * 1024;
bool validDay(int day) { return QDate(day / 10000, day / 100 % 100, day % 100).isValid(); }
std::string string(const QJsonObject &o,const char *key){if(!o.value(key).isString())throw std::runtime_error("Invalid saved activity string");return o.value(key).toString().toStdString();}
std::uint64_t quantity(const QJsonObject &o,const char *key){bool ok=false;auto n=o.value(key).toString().toULongLong(&ok);if(!ok)throw std::runtime_error("Invalid saved activity quantity");return n;}
events::SubscriptionTier savedTier(const QJsonObject &o){const int t=o.value("tier").toInt(-1);if(t<0 || t>3 || o.value("tier").toDouble()!=t)throw std::runtime_error("Invalid saved tier");return static_cast<events::SubscriptionTier>(t);}
events::EventDraft decodeLatest(const QJsonObject &o,const state::ChannelKey &key,std::size_t index){
	bool timeOk=false;const auto ms=o.value("timeMs").toString().toLongLong(&timeOk);if(!timeOk || ms<=0 || ms>std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::duration::max()).count())throw std::runtime_error("Invalid saved activity timestamp");
	events::EventDraft draft{string(o,"id"),key.channelId,std::chrono::system_clock::time_point(std::chrono::milliseconds(ms)),key.origin,{}};
	std::optional<events::User> user;const auto actor=o.value("user");if(actor.isObject()){const auto u=actor.toObject();user=events::User{string(u,"id"),string(u,"name")};}else if(!actor.isNull())throw std::runtime_error("Invalid saved activity user");
	if((index<2 || index==4) && !user)throw std::runtime_error("Missing saved activity user");
	if(index==0)draft.payload=events::FollowEvent{*user};
	else if(index==1){const auto months=quantity(o,"months");if(months>std::numeric_limits<std::uint32_t>::max() || !o.value("counts").isBool())throw std::runtime_error("Invalid saved subscription");draft.payload=events::SubEvent{*user,savedTier(o),std::uint32_t(months),o.value("counts").toBool()};}
	else if(index==2)draft.payload=events::CheerEvent{user,quantity(o,"bits")};
	else if(index==3)draft.payload=events::GiftEvent{user,quantity(o,"count"),savedTier(o)};
	else if(index==4)draft.payload=events::RaidEvent{*user,quantity(o,"viewers")};
	else throw std::runtime_error("Invalid saved activity type");
	return draft;
}
QJsonObject encodeLatest(const events::EventPtr &event){
	const auto &draft=event->data();QJsonObject result{{"id",QString::fromStdString(draft.id)},{"timeMs",QString::number(std::chrono::duration_cast<std::chrono::milliseconds>(draft.occurredAt.time_since_epoch()).count())}};
	std::visit([&](const auto &value){using T=std::decay_t<decltype(value)>;const events::User *user=nullptr;
		if constexpr(std::is_same_v<T,events::FollowEvent>)user=&value.follower;
		else if constexpr(std::is_same_v<T,events::SubEvent>){user=&value.subscriber;result.insert("tier",int(value.tier));result.insert("months",QString::number(value.totalMonths));result.insert("counts",value.countsTowardTotals);}
		else if constexpr(std::is_same_v<T,events::CheerEvent>){user=value.user?&*value.user:nullptr;result.insert("bits",QString::number(qulonglong(value.bits)));}
		else if constexpr(std::is_same_v<T,events::GiftEvent>){user=value.gifter?&*value.gifter:nullptr;result.insert("count",QString::number(qulonglong(value.count)));result.insert("tier",int(value.tier));}
		else if constexpr(std::is_same_v<T,events::RaidEvent>){user=&value.raider;result.insert("viewers",QString::number(qulonglong(value.viewers)));}
		result.insert("user",user?QJsonValue(QJsonObject{{"id",QString::fromStdString(user->id)},{"name",QString::fromStdString(user->displayName)}}):QJsonValue(QJsonValue::Null));
	},draft.payload);return result;
}
}
state::DailyBook JsonDailyStore::load()
{
	writable_ = false; original_.reset();latest_.clear();activity_.clear();
	if (filename_.isEmpty()) throw std::runtime_error("Daily totals path unavailable");
	if (!QFileInfo::exists(filename_)) { writable_ = true; return {}; }
	QFile file(filename_);
	if (!file.open(QIODevice::ReadOnly) || file.size() > limit) throw std::runtime_error("Daily totals file unavailable or too large");
	const auto bytes = file.read(limit + 1);
	if (file.error() != QFileDevice::NoError || bytes.size() > limit) throw std::runtime_error("Daily totals read failed");
	const auto document = QJsonDocument::fromJson(bytes);
	const auto object = document.object();
	const auto version=object.value("schemaVersion").toInt(-1);
	if (!document.isObject() || (version!=1 && version!=2 && version!=3) || object.value("schemaVersion").toDouble() != version ||
		object.value("calendar").toString() != QStringLiteral("system-local") || !object.value("channels").isArray())
		throw std::runtime_error("Unsupported daily totals document; file preserved");
	const auto entries = object.value("channels").toArray();
	if (entries.size() > 16) throw std::runtime_error("Daily totals capacity exceeded");
	state::DailyBook book;
	state::RestoredLatestBook latest;
	state::RestoredActivityBook history;
	for (const auto &value : entries) {
		const auto item = value.toObject();
		const auto channel = item.value("channel").toString();
		const auto day = item.value("day").toInt();
		bool bitsOk = false, subsOk = false;
		const auto bits = item.value("bits").toString().toULongLong(&bitsOk);
		const auto subs = item.value("subs").toString().toULongLong(&subsOk);
		const auto origin = item.value("origin").toString();
		if (!value.isObject() || (origin != QStringLiteral("simulation") && origin != QStringLiteral("twitch")) || channel.isEmpty() ||
			channel.toUtf8().size() > 128 || !validDay(day) || item.value("day").toDouble() != day || !bitsOk || !subsOk || !item.value("saturated").isBool())
			throw std::runtime_error("Invalid daily totals entry; file preserved");
		if (!book.emplace(state::ChannelKey{origin == QStringLiteral("twitch") ? events::EventOrigin::Twitch : events::EventOrigin::Simulation, channel.toStdString()},
			state::DailyTotals{day, bits, subs, item.value("saturated").toBool()}).second)
			throw std::runtime_error("Duplicate daily channel; file preserved");
		if(version>=2){const auto values=item.value("latest");if(!values.isArray() || values.toArray().size()!=4)throw std::runtime_error("Invalid saved activity list; file preserved");
			const state::ChannelKey key{origin=="twitch"?events::EventOrigin::Twitch:events::EventOrigin::Simulation,channel.toStdString()};const auto array=values.toArray();
			for(int i=0;i<4;++i){if(array[i].isNull())continue;if(!array[i].isObject())throw std::runtime_error("Invalid saved activity event; file preserved");latest[key][i]=decodeLatest(array[i].toObject(),key,std::size_t(i));}
		}
		if(version==3){
			const auto values=item.value("activity");if(!values.isArray() || values.toArray().size()>int(state::ChannelState::activityCapacity))throw std::runtime_error("Invalid saved history capacity; file preserved");
			const state::ChannelKey key{origin=="twitch"?events::EventOrigin::Twitch:events::EventOrigin::Simulation,channel.toStdString()};auto &saved=history[key];std::set<std::string> ids;
			for(const auto &value:values.toArray()){
				const auto row=value.toObject();const int type=row.value("type").toInt(-1);
				if(!value.isObject() || type<0 || type>4 || row.value("type").toDouble()!=type)throw std::runtime_error("Invalid saved history type; file preserved");
				auto draft=decodeLatest(row,key,std::size_t(type));if(!ids.insert(draft.id).second)throw std::runtime_error("Duplicate saved history event; file preserved");saved.push_back(std::move(draft));
			}
		}
	}
	original_ = bytes;latest_=std::move(latest);activity_=std::move(history); writable_ = true; return book;
}
bool JsonDailyStore::save(const state::DailyBook &book)
{return saveState(book,{});}
bool JsonDailyStore::saveState(const state::DailyBook &book,const state::LatestBook &latest)
{return saveState(book,latest,{});}
bool JsonDailyStore::saveState(const state::DailyBook &book,const state::LatestBook &latest,const state::ActivityBook &history)
{
	if (!writable_ || book.size() > 16) return false;
	for(const auto &[key,values]:latest)if(book.find(key)==book.end())return false;
	for(const auto &[key,values]:history)if(book.find(key)==book.end() || values.size()>state::ChannelState::activityCapacity)return false;
	if (QFileInfo::exists(filename_)) {
		QFile current(filename_);
		if (!original_ || !current.open(QIODevice::ReadOnly) || current.size() > limit) return false;
		const auto bytes = current.read(limit + 1);
		if (current.error() != QFileDevice::NoError || bytes != *original_) return false;
	} else if (original_) return false;
	QJsonArray entries;
	for (const auto &[key, daily] : book) {
		if ((key.origin != events::EventOrigin::Simulation && key.origin != events::EventOrigin::Twitch) || key.channelId.empty() || key.channelId.size() > 128 || !validDay(daily.day)) return false;
		QJsonArray activity;const auto found=latest.find(key);for(std::size_t i=0;i<4;++i){const auto event=found==latest.end()?events::EventPtr{}:found->second[i];if(event){const auto &draft=event->data();if(draft.origin!=key.origin || draft.channelId!=key.channelId || draft.payload.index()!=i)return false;activity.append(encodeLatest(event));}else activity.append(QJsonValue(QJsonValue::Null));}
		QJsonArray saved;const auto entriesFound=history.find(key);std::set<std::string> ids;
		if(entriesFound!=history.end())for(const auto &event:entriesFound->second){
			if(!event || event->data().origin!=key.origin || event->data().channelId!=key.channelId || event->data().payload.index()>4 || !ids.insert(event->data().id).second)return false;
			auto row=encodeLatest(event);row.insert("type",int(event->data().payload.index()));saved.append(row);
		}
		// The two-argument compatibility API still preserves its last fields as rows.
		else for(int i=0;i<activity.size();++i)if(activity[i].isObject()){auto row=activity[i].toObject();row.insert("type",i);saved.append(row);}
		entries.append(QJsonObject{{"origin", key.origin == events::EventOrigin::Twitch ? "twitch" : "simulation"}, {"channel", QString::fromStdString(key.channelId)}, {"day", daily.day},
			{"bits", QString::number(qulonglong(daily.bits))}, {"subs", QString::number(qulonglong(daily.subs))}, {"saturated", daily.saturated},{"latest",activity},{"activity",saved}});
	}
	const auto bytes = QJsonDocument(QJsonObject{{"schemaVersion", 3}, {"calendar", "system-local"}, {"channels", entries}}).toJson();
	if (bytes.size() > limit || !QDir().mkpath(QFileInfo(filename_).absolutePath())) return false;
	QSaveFile file(filename_); file.setDirectWriteFallback(false);
	if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) return false;
	original_ = bytes; return true;
}
}
