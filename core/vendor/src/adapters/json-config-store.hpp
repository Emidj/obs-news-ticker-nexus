// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "core/config-store.hpp"
#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <optional>

namespace oto {
// UI-thread-only adapter. Invalid/newer documents are never overwritten.
class JsonConfigStore final : public IConfigStore {
public:
	explicit JsonConfigStore(QString filename);
	ConfigLoadResult load() override;
	bool save(Settings settings) override;
private:
	QString filename_;
	QJsonObject document_;
	std::optional<QByteArray> original_;
	bool writable_ = false;
};
} // namespace oto
