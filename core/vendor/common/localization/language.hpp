// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QByteArray>
#include <QString>
#include <map>
namespace nexus::localization {
struct Language { const char *code; const char *name; };
inline constexpr Language languages[]={{"en-US","English"},{"es-ES","Español"},{"it-IT","Italiano"},{"fr-FR","Français"},{"de-DE","Deutsch"}};
// Locale variants (es-MX, en-GB, etc.) share their language. Unknown OS locales
// resolve to English; invalid saved overrides resolve to automatic selection.
QString languageFor(const QString &locale);
QString selection(const QString &value);
QString systemLanguage();
void setApplicationLanguage(const QString &locale);
class Catalog final {
public:
    explicit Catalog(const QString &directory);
    const char *text(const char *key,const QString &language={}) const;
    bool contains(const char *key,const QString &language={}) const;
    const std::map<QByteArray,QByteArray> &entries(const QString &language) const;
private:
    std::map<QString,std::map<QByteArray,QByteArray>> catalogs_;
};
}
