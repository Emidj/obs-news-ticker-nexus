// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <memory>
class QWidget;
namespace oto::twitch {class Service;}
namespace oto::ui {QWidget *createTwitchControls(QWidget *parent,const std::shared_ptr<twitch::Service> &service);}
