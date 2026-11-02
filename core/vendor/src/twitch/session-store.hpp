// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QString>
namespace oto::twitch {
struct Session { QString client,access,refresh; bool enabled=false; };
enum class SessionLoad { Missing,Loaded,Unavailable };
class SessionStore final {
public:
	explicit SessionStore(QString filename):filename_(std::move(filename)){}
	SessionLoad load(Session &session) const;
	bool save(const Session &session) const;
private:QString filename_;
};
}
