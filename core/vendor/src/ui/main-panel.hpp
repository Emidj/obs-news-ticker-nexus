// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <memory>
namespace oto { class PluginCore; class ObsVisualRenderer; namespace visual { class VisualEngine; } namespace twitch { class Service; } }

namespace oto::ui {
// Called only by the module lifecycle on the OBS UI thread.
bool createMainPanel(const std::shared_ptr<PluginCore> &core,
	const std::shared_ptr<visual::VisualEngine> &visual, const std::shared_ptr<ObsVisualRenderer> &renderer, const std::shared_ptr<twitch::Service> &twitch);
void destroyMainPanel();
} // namespace oto::ui
