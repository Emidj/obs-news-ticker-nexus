// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "../common/localization/language.hpp"
namespace oto::i18n {
const nexus::localization::Catalog &catalog();
inline const char *text(const char *key,const QString &language={}){return catalog().text(key,language);}
inline QString qtext(const char *key,const QString &language={}){return QString::fromUtf8(text(key,language));}
}
