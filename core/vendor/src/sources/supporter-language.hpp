// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "supporter-model.hpp"
#include "localization.hpp"
#include <obs.h>
namespace oto::sources {
inline void readSupporterLanguage(BarSettings &settings,obs_data_t *data){settings.language=nexus::localization::selection(QString::fromUtf8(obs_data_get_string(data,"language")));}
inline BarLabels supporterLabels(const QString &language){
    constexpr const char *keys[]={"Bar.LastFollow","Bar.LastSub","Bar.LastCheer","Bar.LastGift","Bar.DailyBits","Bar.DailySubs"};
    BarLabels labels;for(size_t i=0;i<6;++i)labels.titles[i]=i18n::qtext(keys[i],language);
    labels.empty=i18n::qtext("Bar.Empty",language);labels.anonymous=i18n::qtext("Bar.Anonymous",language);return labels;
}
}
