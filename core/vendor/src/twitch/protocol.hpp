// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "events/event.hpp"
#include <QJsonObject>
#include <QJsonArray>
#include <QStringList>
namespace oto::twitch {
// Exact !so command only, with the permission supplied by Twitch chat metadata.
QString shoutoutTarget(const events::ChatEvent &chat);
QString shoutoutMessage(const QString &login);
QStringList requiredScopes();
QJsonArray subscriptions(const QString &user, const QString &session);
std::optional<events::HypeTrainEvent> hypeTrain(const QJsonObject &, events::HypeTrainStage);
// Bounded JSON adapter only. Validation/normalization and deduplication belong to Core.
std::optional<events::EventDraft> notification(const QJsonObject &message, const QString &account);
}
