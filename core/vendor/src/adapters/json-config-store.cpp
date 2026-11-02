// SPDX-License-Identifier: GPL-2.0-or-later
#include "json-config-store.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <utility>

namespace oto {
namespace { constexpr qint64 maxConfigBytes = 64 * 1024; }
JsonConfigStore::JsonConfigStore(QString filename) : filename_(std::move(filename)) {}

ConfigLoadResult JsonConfigStore::load()
{
	writable_ = false;
	original_.reset();
	document_ = {};
	if (filename_.isEmpty()) return {ConfigStatus::Unavailable, {}};
	if (!QFileInfo::exists(filename_)) {
		writable_ = true;
		return {ConfigStatus::Missing, {}};
	}
	QFile file(filename_);
	if (!file.open(QIODevice::ReadOnly)) return {ConfigStatus::Unavailable, {}};
	if (file.size() > maxConfigBytes) return {ConfigStatus::Invalid, {}};
	const auto bytes = file.read(maxConfigBytes + 1);
	if (file.error() != QFileDevice::NoError) return {ConfigStatus::Unavailable, {}};
	if (bytes.size() > maxConfigBytes) return {ConfigStatus::Invalid, {}};
	QJsonParseError error;
	const auto parsed = QJsonDocument::fromJson(bytes, &error);
	if (error.error != QJsonParseError::NoError || !parsed.isObject()) return {ConfigStatus::Invalid, {}};
	const auto object = parsed.object();
	const auto version = object.value(QStringLiteral("schemaVersion"));
	if (!version.isDouble()) return {ConfigStatus::Invalid, {}};
	if (version.toDouble() > 1) return {ConfigStatus::UnsupportedVersion, {}};
	const auto diagnostics = object.value(QStringLiteral("diagnosticsEnabled"));
	if (version.toDouble() != 1 || !diagnostics.isBool()) return {ConfigStatus::Invalid, {}};
	document_ = object;
	original_ = bytes;
	writable_ = true;
	return {ConfigStatus::Loaded, {diagnostics.toBool()}};
}

bool JsonConfigStore::save(Settings settings)
{
	if (!writable_ || filename_.isEmpty()) return false;
	// Reject external edits since load instead of silently replacing them.
	if (QFileInfo::exists(filename_)) {
		QFile current(filename_);
		if (!original_ || !current.open(QIODevice::ReadOnly) || current.size() > maxConfigBytes) return false;
		const auto bytes = current.read(maxConfigBytes + 1);
		if (current.error() != QFileDevice::NoError || bytes != *original_) return false;
	} else if (original_) {
		return false;
	}
	if (!QDir().mkpath(QFileInfo(filename_).absolutePath())) return false;
	auto updated = document_;
	updated.insert(QStringLiteral("schemaVersion"), 1);
	updated.insert(QStringLiteral("diagnosticsEnabled"), settings.diagnosticsEnabled);
	const auto bytes = QJsonDocument(updated).toJson();
	QSaveFile file(filename_);
	file.setDirectWriteFallback(false);
	if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) return false;
	document_ = updated;
	original_ = bytes;
	return true;
}
} // namespace oto
