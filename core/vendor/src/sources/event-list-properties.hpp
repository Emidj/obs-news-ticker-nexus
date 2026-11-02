// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "event-list-model.hpp"
#include "font-properties.hpp"
namespace oto::sources {
inline void readListTypography(ListSettings &next,obs_data_t *settings)
{
	const auto family=fonts::overrideFamily(settings,fonts::names);
	if(!family.isEmpty())next.family=family;
	next.labelFamily=fonts::overrideFamily(settings,fonts::labels);
	next.namePixelSize=fonts::overridePixelSize(settings,fonts::names);
	next.labelPixelSize=fonts::overridePixelSize(settings,fonts::labels);
	next.inheritNameColor=obs_data_get_bool(settings,"use_style_name_color");
	next.inheritLabelColor=obs_data_get_bool(settings,"use_style_label_color");
	const auto color=[&](const char *key){const auto value=uint32_t(obs_data_get_int(settings,key));return QColor(value&255,(value>>8)&255,(value>>16)&255);};
	next.nameColor=color("name_color");next.labelColor=color("label_color");
}
inline bool listColorInheritanceModified(obs_properties_t *properties,obs_property_t *,obs_data_t *settings) noexcept
{
	for(const auto &[color,inherit]:{std::pair{"name_color","use_style_name_color"},std::pair{"label_color","use_style_label_color"}})
		if(auto *property=obs_properties_get(properties,color))obs_property_set_enabled(property,!obs_data_get_bool(settings,inherit));
	return true;
}
}
