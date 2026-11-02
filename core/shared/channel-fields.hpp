// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QString>
#include <QStringList>
#include <QRegularExpression>
#include <QJsonObject>
#include <QJsonArray>
namespace dst {
struct ChannelFields {
 QString title,gameId,category;QStringList tags;
 bool operator==(const ChannelFields &other)const{return title==other.title&&gameId==other.gameId&&tags==other.tags;}
 QJsonObject payload()const{QJsonArray array;for(const auto &tag:tags)array.append(tag);return {{"title",title},{"game_id",gameId},{"tags",array}};}
 QString error()const{
  if(title.trimmed().isEmpty()||title.toUcs4().size()>140)return QString::fromUtf8("El título debe tener entre 1 y 140 caracteres.");
  static const QRegularExpression id("^[0-9]{1,32}$"),tagPattern(QStringLiteral("^[\\p{L}\\p{N}]{1,25}$"));
  if(!id.match(gameId).hasMatch())return QString::fromUtf8("Selecciona una categoría válida de Twitch.");
  if(tags.size()>10)return QString::fromUtf8("Se permiten hasta 10 tags.");
  QStringList seen;for(const auto &tag:tags){if(!tagPattern.match(tag).hasMatch())return QString::fromUtf8("Cada tag debe tener de 1 a 25 letras o números, sin espacios.");if(seen.contains(tag.toCaseFolded()))return QString::fromUtf8("No repitas tags.");seen.append(tag.toCaseFolded());}
  return {};
 }
};
}
