// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrl>
#include <QColor>
#include <cmath>
#include <optional>
#include <vector>
namespace oto::chat {
struct CheerTier {QString prefix,url;std::uint64_t minimum=0;QColor color;};
inline bool publicCheerUrl(const QString &value){
 const QUrl u(value);if(value.size()>512 || !u.isValid() || u.scheme()!="https" || !u.userInfo().isEmpty() || u.hasFragment() || u.hasQuery() || u.port()!=-1)return false;
 return (u.host()=="d3aqoihi2n8ty8.cloudfront.net" && u.path().startsWith("/actions/")) || (u.host()=="static-cdn.jtvnw.net" && u.path().startsWith("/bits/"));
}
inline std::optional<std::vector<CheerTier>> parseCheermotes(const QByteArray &bytes){
 if(bytes.size()>2*1024*1024)return {};const auto root=QJsonDocument::fromJson(bytes).object();if(!root.value("data").isArray())return {};std::vector<CheerTier> result;static const QRegularExpression prefix("^[A-Za-z][A-Za-z0-9_]{0,63}$"),hex("^#[0-9A-Fa-f]{6}$");
 for(const auto &v:root.value("data").toArray()){const auto e=v.toObject();const auto code=e.value("prefix").toString();if(!prefix.match(code).hasMatch())continue;
  for(const auto &tier:e.value("tiers").toArray()){const auto t=tier.toObject();const auto minimum=t.value("min_bits").toDouble();if(minimum<1 || minimum>1000000000 || minimum!=std::floor(minimum))continue;const auto dark=t.value("images").toObject().value("dark").toObject();QString url;
   for(const auto type:{"animated","static"})for(const auto scale:{"2","1"})if(url.isEmpty()){const auto candidate=dark.value(type).toObject().value(scale).toString();if(publicCheerUrl(candidate))url=candidate;}
   const auto color=t.value("color").toString();if(!url.isEmpty() && result.size()<2048)result.push_back({code.toLower(),url,std::uint64_t(minimum),hex.match(color).hasMatch()?QColor(color):QColor{}});
  }
 }return result;
}
}
