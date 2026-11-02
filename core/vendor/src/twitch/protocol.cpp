// SPDX-License-Identifier: GPL-2.0-or-later
#include "protocol.hpp"
#include <QDateTime>
#include <QRegularExpression>
#include <limits>
#include <stdexcept>
#include <algorithm>
#include <cmath>
namespace oto::twitch {
QString shoutoutTarget(const events::ChatEvent &chat)
{
	if (!chat.canShoutout) return {};
	static const QRegularExpression command(QStringLiteral("^!so\\s+@([a-z0-9_]{1,25})\\s*$"),QRegularExpression::CaseInsensitiveOption);
	const auto match=command.match(QString::fromStdString(chat.message).trimmed());
	return match.hasMatch()?match.captured(1).toLower():QString{};
}
QString shoutoutMessage(const QString &login)
{
	static const QRegularExpression valid(QStringLiteral("^[a-z0-9_]{1,25}$"));
	if(!valid.match(login).hasMatch())return {};
	return QStringLiteral("🌟 Go check out @%1! Show them some love and drop a follow: https://www.twitch.tv/%1").arg(login);
}
namespace {
QString string(const QJsonObject &o, const char *key) {
	if (!o.value(key).isString()) throw std::runtime_error("Missing Twitch string");
	return o.value(key).toString();
}
std::uint64_t number(const QJsonObject &o, const char *key) {
	const auto v=o.value(key); const auto n=v.toDouble(-1);
	if (!v.isDouble() || n<0 || n>9007199254740991.0 || n!=double(std::uint64_t(n))) throw std::runtime_error("Invalid Twitch integer");
	return std::uint64_t(n);
}
bool boolean(const QJsonObject &o, const char *key) {
	if (!o.value(key).isBool()) throw std::runtime_error("Missing Twitch boolean");
	return o.value(key).toBool();
}
events::User user(const QJsonObject &o, const char *id="user_id", const char *name="user_name") {
	return {string(o,id).toStdString(),string(o,name).toStdString()};
}
events::SubscriptionTier tier(const QJsonObject &o) {
	const auto t=string(o,"tier");
	if(t=="1000")return events::SubscriptionTier::Tier1;
	if(t=="2000")return events::SubscriptionTier::Tier2;
	if(t=="3000")return events::SubscriptionTier::Tier3;
	throw std::runtime_error("Unknown Twitch tier");
}
}
std::optional<events::HypeTrainEvent> hypeTrain(const QJsonObject &e,events::HypeTrainStage stage) {
	try {
		const auto timestamp=[](QString value) {
			static const QRegularExpression iso("^\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2}(?:\\.\\d{1,9})?Z$");
			if(!iso.match(value).hasMatch())throw std::runtime_error("Invalid Hype timestamp");
			const auto dot=value.indexOf('.');if(dot>=0)value=value.left(dot)+"."+value.mid(dot+1,value.size()-dot-2).left(3).leftJustified(3,'0')+"Z";
			const auto time=QDateTime::fromString(value,Qt::ISODateWithMs);if(!time.isValid() || time.toMSecsSinceEpoch()<=0)throw std::runtime_error("Invalid Hype time");
			return std::chrono::system_clock::time_point(std::chrono::milliseconds(time.toMSecsSinceEpoch()));
		};
		const auto level=number(e,"level");const auto id=string(e,"id");
		if(!level || level>std::numeric_limits<std::uint32_t>::max() || id.isEmpty() || id.size()>128)return {};
		events::HypeTrainEvent result{stage,std::uint32_t(level),number(e,"total"),stage==events::HypeTrainStage::End?0:number(e,"goal")};
		result.trainId=id.toStdString();result.startedAt=timestamp(string(e,"started_at"));
		if(stage!=events::HypeTrainStage::End) {
			result.progress=number(e,"progress");result.expiresAt=timestamp(string(e,"expires_at"));
			if(!result.goal || result.expiresAt<=result.startedAt)return {};
		}else if(timestamp(string(e,"ended_at"))<result.startedAt)return {};
		const auto contributions=e.value("top_contributions");
		if(!contributions.isArray() || contributions.toArray().size()>16)return {};
		for(const auto &entry:contributions.toArray()) {
			if(!entry.isObject())return {};const auto row=entry.toObject();const auto actor=user(row);const auto points=number(row,"total");
			const auto method=string(row,"type");const std::uint8_t methods=method=="bits"?1:method=="subscription"?2:method=="other"?4:0;if(!methods)return {};
			auto found=std::find_if(result.contributors.begin(),result.contributors.end(),[&](const auto &c){return c.user.id==actor.id;});
			if(found==result.contributors.end()){result.contributors.push_back({actor,0,0});found=std::prev(result.contributors.end());}
			if(points>std::numeric_limits<std::uint64_t>::max()-found->points)return {};
			found->points+=points;found->methods|=methods;
			if(methods==1)found->bits+=points;
			if(methods==2)found->subscriptionPoints+=points;
		}
		std::stable_sort(result.contributors.begin(),result.contributors.end(),[](const auto &a,const auto &b){return a.points>b.points;});
		return result;
	}catch(...){return {};}
}
QStringList requiredScopes() { return {"user:read:chat","moderator:read:followers","channel:read:subscriptions","bits:read","channel:read:hype_train"}; }
QJsonArray subscriptions(const QString &account,const QString &session) {
	QJsonArray result;
	for(const auto &type : {"channel.chat.message","channel.follow","channel.subscribe","channel.subscription.gift","channel.subscription.message",
		"channel.cheer","channel.raid","channel.hype_train.begin","channel.hype_train.progress","channel.hype_train.end",
		"channel.chat.message_delete","channel.chat.clear_user_messages","channel.chat.clear"}) {
		const QString t=QString::fromLatin1(type);
		QJsonObject condition{{"broadcaster_user_id",account}};
		if(t.startsWith("channel.chat.")) condition.insert("user_id",account);
		if(t=="channel.follow") condition.insert("moderator_user_id",account);
		if(t=="channel.raid") condition={{"to_broadcaster_user_id",account}};
		result.append(QJsonObject{{"type",t},{"version",t=="channel.follow" || t.startsWith("channel.hype_train.") ? "2":"1"},
			{"condition",condition},{"transport",QJsonObject{{"method","websocket"},{"session_id",session}}}});
	}
	return result;
}
std::optional<events::EventDraft> notification(const QJsonObject &message,const QString &account) {
	try {
		const auto metadata=message.value("metadata").toObject();
		if(string(metadata,"message_type")!="notification") return {};
		const auto payload=message.value("payload").toObject();
		const auto subscription=payload.value("subscription").toObject();
		const auto type=string(subscription,"type");
		const auto expectedVersion=(type=="channel.follow" || type.startsWith("channel.hype_train."))?"2":"1";
		if(string(subscription,"version")!=expectedVersion) return {};
		const auto e=payload.value("event").toObject();
		if(string(e,type=="channel.raid"?"to_broadcaster_user_id":"broadcaster_user_id")!=account) return {};
		auto timestamp=string(metadata,"message_timestamp");
		static const QRegularExpression iso(QStringLiteral("^\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2}(?:\\.\\d{1,9})?Z$"));
		if(!iso.match(timestamp).hasMatch()) return {};
		const auto dot=timestamp.indexOf('.');
		if(dot>=0)timestamp=timestamp.left(dot)+"."+timestamp.mid(dot+1,timestamp.size()-dot-2).left(3).leftJustified(3,'0')+"Z";
		const auto time=QDateTime::fromString(timestamp,Qt::ISODateWithMs);
		if(!time.isValid())return {};
		events::EventDraft draft{string(metadata,"message_id").toStdString(),account.toStdString(),
			std::chrono::system_clock::time_point(std::chrono::milliseconds(time.toMSecsSinceEpoch())),events::EventOrigin::Twitch,{}};
		if(type=="channel.follow") draft.payload=events::FollowEvent{user(e)};
		else if(type=="channel.subscribe") draft.payload=events::SubEvent{user(e),tier(e),1,!boolean(e,"is_gift")};
		else if(type=="channel.subscription.message") {
			const auto months=number(e,"cumulative_months");
			if(months>std::numeric_limits<std::uint32_t>::max())return {};
			// A shared resub message is not a new subscription purchase.
			draft.payload=events::SubEvent{user(e),tier(e),std::uint32_t(months),false};
		}
		else if(type=="channel.subscription.gift") draft.payload=events::GiftEvent{boolean(e,"is_anonymous")?std::optional<events::User>{}:user(e),number(e,"total"),tier(e)};
		else if(type=="channel.cheer") draft.payload=events::CheerEvent{boolean(e,"is_anonymous")?std::optional<events::User>{}:user(e),number(e,"bits")};
		else if(type=="channel.raid") {
   auto login=e.value("from_broadcaster_user_login").toString().toLower();
   static const QRegularExpression validLogin("^[a-z0-9_]{1,25}$");if(!validLogin.match(login).hasMatch())login.clear();
   draft.payload=events::RaidEvent{user(e,"from_broadcaster_user_id","from_broadcaster_user_name"),number(e,"viewers"),login.toStdString()};
  }
		else if(type=="channel.chat.message_delete")draft.payload=events::ChatModerationEvent{events::ChatModerationAction::DeleteMessage,string(e,"message_id").toStdString(),{}};
		else if(type=="channel.chat.clear_user_messages")draft.payload=events::ChatModerationEvent{events::ChatModerationAction::ClearUser,{},string(e,"target_user_id").toStdString()};
		else if(type=="channel.chat.clear")draft.payload=events::ChatModerationEvent{};
		else if(type=="channel.chat.message") {
			const auto author=user(e,"chatter_user_id","chatter_user_name");
			const auto source=e.value("source_broadcaster_user_id").toString();
			bool permitted=author.id==account.toStdString();auto role=permitted?events::ChatRole::Streamer:events::ChatRole::Viewer;
			for(const auto &badge:e.value("badges").toArray()) {
				const auto id=badge.toObject().value("set_id").toString();
				if(id=="moderator" || id=="lead_moderator"){permitted=true;if(role!=events::ChatRole::Streamer)role=events::ChatRole::Moderator;}
				else if(id=="vip" && role!=events::ChatRole::Streamer && role!=events::ChatRole::Moderator)role=events::ChatRole::Vip;
				else if(id=="subscriber" && role==events::ChatRole::Viewer)role=events::ChatRole::Subscriber;
			}
			// Badges in shared chat may belong to the other broadcaster's room.
			if(!source.isEmpty() && source!=account)permitted=false;
			const auto nameColor=e.value("color").toString();
			const auto safeColor=QRegularExpression(QStringLiteral("^#[0-9a-fA-F]{6}$")).match(nameColor).hasMatch()?nameColor:QString{};
			draft.payload=events::ChatEvent{author,string(e.value("message").toObject(),"text").toStdString(),permitted,safeColor.toStdString(),role};
			auto &chat=std::get<events::ChatEvent>(draft.payload);
			chat.messageId=e.value("message_id").toString().toStdString();
			// Keep the receiving room's badges in Twitch order. source_badges
			// belong to a different room in Shared Chat and must not replace these.
			static const QRegularExpression badgeId("^[A-Za-z0-9_-]{1,64}$");
			for(const auto &value:e.value("badges").toArray()){
				const auto badge=value.toObject();const auto set=badge.value("set_id").toString(),version=badge.value("id").toString(),info=badge.value("info").toString();
				if(chat.badges.size()<32 && badgeId.match(set).hasMatch() && badgeId.match(version).hasMatch() && info.size()<=128 && !info.contains(QChar::Null))chat.badges.push_back({set.toStdString(),version.toStdString(),info.toStdString()});
			}
			const auto fragments=e.value("message").toObject().value("fragments").toArray();QString joined;
			if(fragments.size()<=128)for(const auto &value:fragments){
				const auto fragment=value.toObject();const auto text=fragment.value("text").toString();joined+=text;
				const auto emote=fragment.value("emote").toObject();const auto id=fragment.value("type").toString()=="emote"?emote.value("id").toString():QString{};
				static const QRegularExpression validId("^[A-Za-z0-9_-]{1,128}$");
				chat.fragments.push_back({text.toStdString(),validId.match(id).hasMatch()?id.toStdString():std::string{},emote.value("format").toArray().contains("animated")});
				if(fragment.value("type").toString()=="cheermote"){
					const auto cheer=fragment.value("cheermote").toObject();const auto prefix=cheer.value("prefix").toString();const auto bits=cheer.value("bits").toDouble(),tier=cheer.value("tier").toDouble();static const QRegularExpression validPrefix("^[A-Za-z][A-Za-z0-9_]{0,63}$");
					if(validPrefix.match(prefix).hasMatch() && bits>=1 && bits<=1000000000 && bits==std::floor(bits) && tier>=1 && tier<=bits && tier==std::floor(tier)){auto &f=chat.fragments.back();f.cheerPrefix=prefix.toStdString();f.cheerBits=std::uint64_t(bits);f.cheerTier=std::uint64_t(tier);}
				}
			}
			if(joined.toStdString()!=chat.message)chat.fragments.clear();
		}
		else if(type=="channel.hype_train.begin" || type=="channel.hype_train.progress" || type=="channel.hype_train.end") {
			const auto stage=type.endsWith(".begin")?events::HypeTrainStage::Begin:type.endsWith(".progress")?events::HypeTrainStage::Progress:events::HypeTrainStage::End;
			const auto hype=hypeTrain(e,stage);if(!hype)return {};draft.payload=*hype;
		}
		else return {};
		return draft;
	} catch(...) { return {}; }
}
}
