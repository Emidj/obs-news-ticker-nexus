// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QUrl>
#include <optional>

namespace oto::ui {
// Only a user's Connect/renew click arms browser launch. Startup restoration and
// polling never reopen a browser. Consume before opening (even if opening fails).
class AuthorizationLaunch final {
public:
	void arm() noexcept { armed_=true; }
	static bool allowed(const QUrl &url) {
		return url.isValid() && url.scheme()=="https" && url.userInfo().isEmpty() && !url.hasFragment() &&
			(url.port()==-1 || url.port()==443) &&
			((url.host()=="www.twitch.tv" && url.path()=="/activate") ||
			 (url.host()=="id.twitch.tv" && url.path()=="/oauth2/authorize"));
	}
	std::optional<QUrl> update(bool active,bool authenticated,const QString &verification) {
		if(!armed_)return {};
		if(!active || authenticated){armed_=false;return {};}
		if(verification.isEmpty())return {};
		armed_=false;
		const QUrl url(verification);
		if(allowed(url))return url;
		return {};
	}
private:
	bool armed_=false;
};
}
