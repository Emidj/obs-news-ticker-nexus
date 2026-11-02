// SPDX-License-Identifier: GPL-2.0-or-later
#include "session-store.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dpapi.h>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
namespace oto::twitch {
namespace {constexpr auto magic="OTO-TWITCH-DPAPI-1\n";constexpr int limit=64*1024;}
SessionLoad SessionStore::load(Session &session) const {
	if(filename_.isEmpty())return SessionLoad::Unavailable;
	if(!QFileInfo::exists(filename_))return SessionLoad::Missing;
	QFile file(filename_);if(!file.open(QIODevice::ReadOnly)||file.size()>limit)return SessionLoad::Unavailable;
	auto bytes=file.readAll();if(file.error()!=QFileDevice::NoError||!bytes.startsWith(magic))return SessionLoad::Unavailable;
	bytes.remove(0,int(strlen(magic)));DATA_BLOB in{DWORD(bytes.size()),reinterpret_cast<BYTE *>(bytes.data())},out{};
	if(!CryptUnprotectData(&in,nullptr,nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&out))return SessionLoad::Unavailable;
	QByteArray plain(reinterpret_cast<const char *>(out.pbData),int(out.cbData));SecureZeroMemory(out.pbData,out.cbData);LocalFree(out.pbData);
	const auto doc=QJsonDocument::fromJson(plain);plain.fill('\0');const auto o=doc.object();
	if(!doc.isObject()||o.value("version").toDouble()!=1||!o.value("enabled").isBool())return SessionLoad::Unavailable;
	for(const auto *key:{"client","access","refresh"}){const auto s=o.value(key).toString();if(!o.value(key).isString()||s.size()>4096||s.contains('\r')||s.contains('\n'))return SessionLoad::Unavailable;}
	session={o.value("client").toString(),o.value("access").toString(),o.value("refresh").toString(),o.value("enabled").toBool()};return SessionLoad::Loaded;
}
bool SessionStore::save(const Session &session) const {
	if(filename_.isEmpty())return false;
	auto bytes=QJsonDocument(QJsonObject{{"version",1},{"client",session.client},{"access",session.access},{"refresh",session.refresh},{"enabled",session.enabled}}).toJson(QJsonDocument::Compact);
	if(bytes.size()>limit/2)return false;
	DATA_BLOB in{DWORD(bytes.size()),reinterpret_cast<BYTE *>(bytes.data())},out{};
	const bool ok=CryptProtectData(&in,L"OBS Twitch Overlays",nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&out)!=FALSE;bytes.fill('\0');if(!ok)return false;
	QByteArray encrypted(magic);encrypted.append(reinterpret_cast<const char *>(out.pbData),int(out.cbData));LocalFree(out.pbData);
	if(!QDir().mkpath(QFileInfo(filename_).absolutePath()))return false;
	QSaveFile file(filename_);file.setDirectWriteFallback(false);
	return file.open(QIODevice::WriteOnly)&&file.write(encrypted)==encrypted.size()&&file.commit();
}
}
