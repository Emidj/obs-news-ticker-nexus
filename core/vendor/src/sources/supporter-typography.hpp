// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "supporter-model.hpp"
#include "font-properties.hpp"
namespace oto::sources {
inline void readSupporterTypography(BarSettings &next,obs_data_t *settings)
{
	next.valueFamily=fonts::overrideFamily(settings,fonts::names);
	next.valuePixelSize=fonts::overridePixelSize(settings,fonts::names);
	next.labelFamily=fonts::overrideFamily(settings,fonts::labels);
	next.labelPixelSize=fonts::overridePixelSize(settings,fonts::labels);
}
}
