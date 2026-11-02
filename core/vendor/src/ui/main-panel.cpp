#include "twitch-core-diagnostics.hpp"
// SPDX-License-Identifier: GPL-2.0-or-later
#include "main-panel.hpp"
#include "twitch-panel.hpp"
#include "twitch/service.hpp"
#include "core/plugin-core.hpp"
#include "adapters/obs-logger.hpp"

#include <obs-module.h>
#include <obs-frontend-api.h>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QPushButton>
#include <QLabel>
#include <QPointer>
#include <QThread>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPixmap>
#include <QWidget>
#include <QScrollArea>

#include <memory>

namespace oto::ui {
namespace {
constexpr auto dockId = "obs-twitch-overlays.main-panel";
QPointer<QWidget> panel;
} // namespace

bool createMainPanel(const std::shared_ptr<PluginCore> &core,
	const std::shared_ptr<visual::VisualEngine> &visual, const std::shared_ptr<ObsVisualRenderer> &renderer, const std::shared_ptr<twitch::Service> &twitch)
{
	if (!core || !core->running() || !visual || !renderer) return false;
	if (panel)
		return true;
	if (!qApp || QThread::currentThread() != qApp->thread())
		return false;
	auto *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window());
	if (!mainWindow)
		return false;

	auto widget = std::make_unique<QWidget>(mainWindow);
	widget->setObjectName("otoMainPanel");
	widget->setMinimumSize(360, 560);
	widget->resize(400, 580);
	auto *outer = new QVBoxLayout(widget.get());
	outer->setContentsMargins(0,0,0,0);
	auto *scroll = new QScrollArea(widget.get());
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	outer->addWidget(scroll);
	auto *content = new QWidget(scroll);
	scroll->setWidget(content);
	auto *layout = new QVBoxLayout(content);
	layout->setSizeConstraint(QLayout::SetMinimumSize);
	const auto makeLabel = [&](const char *key, QVBoxLayout *target = nullptr) {
		auto *label = new QLabel(QString::fromUtf8(obs_module_text(key)), widget.get());
		label->setWordWrap(true);
		label->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Minimum);
		(target ? target : layout)->addWidget(label);
		return label;
	};
	auto *header = new QHBoxLayout;
	auto *heading = new QVBoxLayout;
	heading->setSpacing(4);
	header->addLayout(heading,1);
	header->setAlignment(heading,Qt::AlignTop);
	auto *status = makeLabel("Panel.Status",heading);
	auto font = status->font();
	font.setBold(true);
	status->setFont(font);
	makeLabel("Panel.Description",heading);
	auto *logo = new QLabel(content);
	logo->setObjectName("otoNexusLogo");
	logo->setAccessibleName(QStringLiteral("Nexus"));
	logo->setToolTip(QStringLiteral("Nexus"));
	logo->setFixedSize(44,48);
	logo->setAlignment(Qt::AlignRight | Qt::AlignTop);
	// Display the original mark and wordmark from the installer artwork. This
	// source rectangle omits the surrounding poster without redrawing the logo.
	const QPixmap branding(QStringLiteral(":/oto/installer/nexus-branding.png"));
	if(!branding.isNull())logo->setPixmap(branding.copy(QRect(branding.width()*23/100,
		branding.height()*23/100,branding.width()*55/100,branding.height()*40/100))
		.scaled(logo->size(),Qt::KeepAspectRatio,Qt::SmoothTransformation));
	header->addWidget(logo,0,Qt::AlignRight | Qt::AlignTop);
	layout->addLayout(header);
	layout->addWidget(createTwitchControls(widget.get(),twitch));
 NexusTwitchCoreInfo identity{};if(nexus_twitch_core_info(&identity,sizeof(identity))){auto *info=new QLabel(nexus::coreLabel(identity),content);info->setObjectName("twitchCoreIdentity");info->setWordWrap(true);info->setToolTip(nexus::corePath(identity));layout->addWidget(info); }
	auto *diagnostics = new QCheckBox(QString::fromUtf8(obs_module_text("Panel.Diagnostics")), widget.get());
	diagnostics->setObjectName("otoDiagnostics");
	diagnostics->setChecked(core->settings().diagnosticsEnabled);
	diagnostics->setEnabled(core->configurationWritable());
	layout->addWidget(diagnostics);
	auto *feedback = makeLabel(core->configurationWritable() ? "Panel.SettingsReady" : "Panel.SettingsProtected");
	QObject::connect(diagnostics, &QCheckBox::toggled, widget.get(),
			 [weak = std::weak_ptr<PluginCore>(core), diagnostics, feedback](bool enabled) {
		const auto owner = weak.lock();
		if (!owner) return;
		try {
			const auto result = owner->setDiagnosticsEnabled(enabled);
			if (result == SettingsUpdate::Applied || result == SettingsUpdate::Unchanged) {
				feedback->setText(QString::fromUtf8(obs_module_text("Panel.SettingsSaved")));
				return;
			}
		} catch (...) {
			owner->log(LogLevel::Error, "Unexpected error saving configuration");
		}
		const QSignalBlocker blocker(diagnostics);
		diagnostics->setChecked(owner->settings().diagnosticsEnabled);
		feedback->setText(QString::fromUtf8(obs_module_text("Panel.SettingsFailed")));
	});
	auto *simulation = new QGroupBox(QString::fromUtf8(obs_module_text("Panel.Simulation")), widget.get());
	auto *simulationLayout = new QVBoxLayout(simulation);
	auto *eventType = new QComboBox(simulation);
	eventType->setObjectName("otoSimulationType");
	eventType->setAccessibleName(QString::fromUtf8(obs_module_text("Panel.EventType")));
	for (const auto type : {events::EventType::Follow, events::EventType::Sub, events::EventType::Cheer,
			       events::EventType::Gift, events::EventType::Raid, events::EventType::Chat, events::EventType::HypeTrain, events::EventType::Shoutout})
		eventType->addItem(QString::fromUtf8(obs_module_text((QByteArray("Event.")+QByteArray(events::typeName(type)).replace(" ","")).constData())), static_cast<int>(type));
	simulationLayout->addWidget(eventType);
	auto *simulate = new QPushButton(QString::fromUtf8(obs_module_text("Panel.Simulate")), simulation);
	simulate->setObjectName("otoSimulateEvent");
	simulationLayout->addWidget(simulate);
	auto *resultLabel = new QLabel(QString::fromUtf8(obs_module_text("Panel.SimulationReady")), simulation);
	resultLabel->setObjectName("otoSimulationResult");
	resultLabel->setTextFormat(Qt::PlainText);
	resultLabel->setWordWrap(true);
	simulationLayout->addWidget(resultLabel);
	auto *finish = new QPushButton(QString::fromUtf8(obs_module_text("Panel.EndTest")), simulation);
	finish->setObjectName("otoEndPanelTest");simulationLayout->addWidget(finish);
	const auto refreshQueue = [weak = std::weak_ptr<PluginCore>(core), finish] {
		const auto owner=weak.lock();finish->setEnabled(owner && bool(owner->panelTestChannel()));
	};
	refreshQueue();
	QObject::connect(simulate, &QPushButton::clicked, widget.get(),
			 [weak = std::weak_ptr<PluginCore>(core), eventType, resultLabel, refreshQueue] {
		const auto owner = weak.lock();
		if (!owner) return;
		try {
			const auto type = static_cast<events::EventType>(eventType->currentData().toInt());
			const auto result = owner->simulatePanelEvent(type);
			refreshQueue();
			if (result) {
				const auto *name = events::typeName(events::typeOf(result.event->data().payload));
				const bool merged = result.queue.status == execution::EnqueueStatus::Coalesced;
				resultLabel->setText(QString::fromUtf8(obs_module_text(merged ? "Panel.SimulationCoalesced" : "Panel.SimulationAccepted"))
					.arg(QString::fromUtf8(obs_module_text((QByteArray("Event.")+QByteArray(name).replace(" ","")).constData()))));
				owner->log(LogLevel::Info, std::string(merged ? "Simulation coalesced: " : "Panel TEST delivered: ") + name);
				return;
			}
			if (result.queue.status == execution::EnqueueStatus::Full) {
				resultLabel->setText(QString::fromUtf8(obs_module_text("Panel.QueueFull")));
				return;
			}
		} catch (...) {
			owner->log(LogLevel::Error, "Simulation failed");
		}
		resultLabel->setText(QString::fromUtf8(obs_module_text("Panel.SimulationFailed")));
	});
	QObject::connect(finish, &QPushButton::clicked, widget.get(),
		[weak = std::weak_ptr<PluginCore>(core), resultLabel, refreshQueue] {
		if(const auto owner=weak.lock())try{owner->endPanelTest();refreshQueue();resultLabel->setText(QString::fromUtf8(obs_module_text("Panel.TestEnded")));}
		catch(...){owner->log(LogLevel::Error,"Panel TEST cleanup failed");}
	});
	layout->addWidget(simulation);
	// Full GPU/export probes run in development tools, never on the OBS UI thread.
	layout->addStretch();
	layout->addWidget(new QLabel(QStringLiteral("v" OTO_VERSION), widget.get()));

	if (!obs_frontend_add_dock_by_id(dockId, obs_module_text("Panel.Title"), widget.get()))
		return false;
	// OBS owns the dock and its child widget after successful registration.
	panel = widget.release();
	core->log(LogLevel::Info, "Main panel registered");
	return true;
}

void destroyMainPanel()
{
	if (!panel)
		return;
	obs_frontend_remove_dock(dockId);
	panel.clear();
	ObsLogger().write(LogLevel::Info, "Main panel removed");
}
} // namespace oto::ui
