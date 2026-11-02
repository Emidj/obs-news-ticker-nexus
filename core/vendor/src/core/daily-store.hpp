// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "state/shared-state.hpp"
namespace oto {
class IDailyStore {
public:
	virtual ~IDailyStore() = default;
	// UI thread only; invalid/unreadable files must be preserved, with save disabled.
	virtual state::DailyBook load() = 0;
	virtual bool save(const state::DailyBook &book) = 0;
	virtual state::RestoredLatestBook loadLatest(){return {};}
	virtual state::RestoredActivityBook loadActivity(){return {};}
	virtual bool saveState(const state::DailyBook &book,const state::LatestBook &){return save(book);}
	virtual bool saveState(const state::DailyBook &book,const state::LatestBook &latest,const state::ActivityBook &){return saveState(book,latest);}
};
}
