// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "core/daily-store.hpp"
#include <QString>
#include <QByteArray>
#include <optional>
namespace oto {
class JsonDailyStore final : public IDailyStore {
public:
	explicit JsonDailyStore(QString filename) : filename_(std::move(filename)) {}
	state::DailyBook load() override;
	bool save(const state::DailyBook &book) override;
	state::RestoredLatestBook loadLatest()override{return latest_;}
	state::RestoredActivityBook loadActivity()override{return activity_;}
	bool saveState(const state::DailyBook &book,const state::LatestBook &latest)override;
	bool saveState(const state::DailyBook &book,const state::LatestBook &latest,const state::ActivityBook &activity)override;
private:
	QString filename_;
	std::optional<QByteArray> original_;
	bool writable_ = false;
	state::RestoredLatestBook latest_;
	state::RestoredActivityBook activity_;
};
}
