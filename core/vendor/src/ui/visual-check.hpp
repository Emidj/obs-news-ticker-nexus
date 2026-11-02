// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <memory>
#include "visual/asset-manager.hpp"
#include "events/event.hpp"
class QWidget;
namespace oto {
class PluginCore;
class ObsVisualRenderer;
namespace visual { class VisualEngine; }
namespace ui {
void showShoutoutCheck(QWidget *parent,const std::shared_ptr<PluginCore> &,const std::shared_ptr<visual::VisualEngine> &,const std::shared_ptr<ObsVisualRenderer> &,visual::ImagePtr portrait={});
void showVisualCheck(QWidget *parent, const std::shared_ptr<PluginCore> &core,
	const std::shared_ptr<visual::VisualEngine> &visual, const std::shared_ptr<ObsVisualRenderer> &renderer, visual::ImagePtr portrait={},events::User actor={});
}
}
