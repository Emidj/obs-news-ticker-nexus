// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QString>
#include <QJsonObject>
namespace dst {
void setUiLanguage(QString language);
QString uiLanguage();
QString uiText(const QString &source);
const QJsonObject &uiCatalog();
}
