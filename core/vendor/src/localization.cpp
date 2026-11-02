// SPDX-License-Identifier: GPL-2.0-or-later
#include "localization.hpp"
#include <QResource>
static void initializeLanguages(){Q_INIT_RESOURCE(oto_languages);}
namespace oto::i18n {
const nexus::localization::Catalog &catalog(){
    static const auto initialized=[] {initializeLanguages();return true;}();(void)initialized;
    static const nexus::localization::Catalog value(QStringLiteral(":/nexus/data/locale"));return value;
}
}
