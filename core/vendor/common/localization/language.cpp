// SPDX-License-Identifier: GPL-2.0-or-later
#include "language.hpp"
#include <QDir>
#include <QLocale>
#include <QSettings>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace nexus::localization {
namespace {
QString applicationLanguage;
QString prefix(QString value){return value.trimmed().replace('-','_').section('_',0,0).toLower();}
}
QString languageFor(const QString &locale){
    const auto p=prefix(locale);for(const auto &item:languages)if(p==prefix(QString::fromLatin1(item.code)))return QString::fromLatin1(item.code);
    return QStringLiteral("en-US");
}
QString selection(const QString &value){
    const auto p=prefix(value);for(const auto &item:languages)if(p==prefix(QString::fromLatin1(item.code)))return QString::fromLatin1(item.code);
    return QStringLiteral("auto");
}
QString systemLanguage(){
    if(!applicationLanguage.isEmpty())return applicationLanguage;
    static const auto language=[] {
#ifdef _WIN32
        wchar_t name[LOCALE_NAME_MAX_LENGTH]{};
        if(LCIDToLocaleName(MAKELCID(GetUserDefaultUILanguage(),SORT_DEFAULT),name,LOCALE_NAME_MAX_LENGTH,0))
            return languageFor(QString::fromWCharArray(name));
#endif
        return languageFor(QLocale::system().name());
    }();return language;
}
void setApplicationLanguage(const QString &locale){applicationLanguage=languageFor(locale);}
Catalog::Catalog(const QString &directory){
    for(const auto &language:languages){
        const auto code=QString::fromLatin1(language.code);QSettings settings(QDir(directory).filePath(code+".ini"),QSettings::IniFormat);
        auto &catalog=catalogs_[code];for(const auto &key:settings.allKeys())catalog.emplace(key.toUtf8(),settings.value(key).toString().toUtf8());
    }
}
const std::map<QByteArray,QByteArray> &Catalog::entries(const QString &language)const{
    const auto effective=language.isEmpty() || selection(language)=="auto"?systemLanguage():languageFor(language);
    return catalogs_.at(effective);
}
const char *Catalog::text(const char *key,const QString &language)const{
    const auto &catalog=entries(language);const auto found=catalog.find(key);if(found!=catalog.end())return found->second.constData();
    const auto &english=catalogs_.at(QStringLiteral("en-US"));const auto fallback=english.find(key);return fallback==english.end()?key:fallback->second.constData();
}
bool Catalog::contains(const char *key,const QString &language)const{return entries(language).count(key) || catalogs_.at(QStringLiteral("en-US")).count(key);}
}
