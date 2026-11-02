// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <obs-data.h>
#include <obs-properties.h>
#include <QString>
#include <algorithm>

namespace oto::sources::fonts {
struct Role { const char *font; const char *inherit; };
inline constexpr Role names{"name_font","use_style_font"};
inline constexpr Role labels{"label_font","use_style_label_font"};

// Empty means inherit. Sources may opt into the saved size separately.
inline QString overrideFamily(obs_data_t *settings, Role role)
{
	if (obs_data_get_bool(settings,role.inherit)) return {};
	auto *font=obs_data_get_obj(settings,role.font);
	auto family=QString::fromUtf8(obs_data_get_string(font,"face")).left(128);
	obs_data_release(font);
	for (const auto ch:family) if (ch.unicode()<32 || ch.unicode()==127) return {};
	return family;
}
inline int overridePixelSize(obs_data_t *settings, Role role)
{
	if (obs_data_get_bool(settings,role.inherit)) return 0;
	auto *font=obs_data_get_obj(settings,role.font);
	const auto points=obs_data_get_int(font,"size");
	obs_data_release(font);
	// OBS stores QFontDialog point sizes. Use a fixed 96-DPI canvas, independent
	// of the monitor's scaling, and the shared text engine's 8..128 pixel bounds.
	if(points<=0)return 0;
	return int(std::clamp((std::clamp(points,1LL,96LL)*4+1)/3,8LL,128LL));
}
inline void defaults(obs_data_t *settings, const char *family)
{
	for (const auto role:{names,labels}) {
		obs_data_set_default_bool(settings,role.inherit,true);
		auto *font=obs_data_create();
		obs_data_set_string(font,"face",family);obs_data_set_int(font,"size",12);
		obs_data_set_default_obj(settings,role.font,font);obs_data_release(font);
	}
}
inline bool inheritanceModified(obs_properties_t *properties, obs_property_t *, obs_data_t *settings) noexcept
{
	for (const auto role:{names,labels})
		if (auto *font=obs_properties_get(properties,role.font))
			obs_property_set_enabled(font,!obs_data_get_bool(settings,role.inherit));
	return true;
}
inline void addRoleProperties(obs_properties_t *properties, Role role, const char *title,
	const char *inheritTitle, const char *help)
{
	auto *font=obs_properties_add_font(properties,role.font,title);
	obs_property_set_long_description(font,help);
	auto *inherit=obs_properties_add_bool(properties,role.inherit,inheritTitle);
	obs_property_set_modified_callback(inherit,inheritanceModified);
}
inline void addProperties(obs_properties_t *properties, const char *namesTitle,
	const char *labelsTitle, const char *inheritTitle, const char *help)
{
	for (const auto role:{names,labels}) {
		addRoleProperties(properties,role,role.font==names.font?namesTitle:labelsTitle,inheritTitle,help);
	}
}
}
