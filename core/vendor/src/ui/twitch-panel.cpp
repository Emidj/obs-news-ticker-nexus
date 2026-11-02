// SPDX-License-Identifier: GPL-2.0-or-later
#include "twitch-panel.hpp"
#include "authorization-launch.hpp"
#include "twitch/service.hpp"
#include <QDialog>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QDesktopServices>
#include <QUrl>
#include <QPointer>
#include <QCheckBox>
#include <obs-module.h>
namespace oto::ui {
namespace {QString text(const char *key){return QString::fromUtf8(obs_module_text(key));}}
QWidget *createTwitchControls(QWidget *parent,const std::shared_ptr<twitch::Service> &service){
	auto *group=new QGroupBox(text("Twitch.Title"),parent);auto *layout=new QVBoxLayout(group);
	auto *authorization=new QLabel(group);authorization->setObjectName("otoTwitchAuthorization");authorization->setTextFormat(Qt::PlainText);layout->addWidget(authorization);
	auto *status=new QLabel(group);status->setTextFormat(Qt::PlainText);status->setWordWrap(true);layout->addWidget(status);
	auto *connect=new QPushButton(text("Twitch.Connect"),group);connect->setObjectName("otoTwitchConnect");layout->addWidget(connect);
	auto *configure=new QPushButton(text("Twitch.Settings"),group);layout->addWidget(configure);
	auto *dialog=new QDialog(group);dialog->setWindowTitle(text("Twitch.Title"));dialog->setMinimumWidth(430);auto *form=new QVBoxLayout(dialog);
	auto *description=new QLabel(text("Twitch.Help"),dialog);description->setWordWrap(true);form->addWidget(description);
	auto *advanced=new QCheckBox(text("Twitch.Advanced"),dialog);form->addWidget(advanced);
	auto *clientLabel=new QLabel(text("Twitch.Client"),dialog);form->addWidget(clientLabel);auto *client=new QLineEdit(service->client(),dialog);client->setMaxLength(128);client->setObjectName("otoTwitchClient");form->addWidget(client);
	clientLabel->hide();client->hide();
	QObject::connect(advanced,&QCheckBox::toggled,dialog,[clientLabel,client](bool shown){clientLabel->setVisible(shown);client->setVisible(shown);});
	auto *connectButton=new QPushButton(text("Twitch.Connect"),dialog);form->addWidget(connectButton);
	auto *detail=new QLabel(dialog);detail->setTextFormat(Qt::PlainText);detail->setWordWrap(true);form->addWidget(detail);
	auto *code=new QLineEdit(dialog);code->setReadOnly(true);code->setAccessibleName(text("Twitch.Code"));form->addWidget(code);
	auto *authorize=new QPushButton(text("Twitch.Authorize"),dialog);form->addWidget(authorize);
	auto *renew=new QPushButton(text("Twitch.Renew"),dialog);form->addWidget(renew);
	auto *chatStatus=new QLabel(dialog);chatStatus->setTextFormat(Qt::PlainText);chatStatus->setWordWrap(true);form->addWidget(chatStatus);
	auto *chatAuthorize=new QPushButton(text("Shoutout.AuthorizeChat"),dialog);form->addWidget(chatAuthorize);
	auto *disconnect=new QPushButton(text("Twitch.Disconnect"),dialog);form->addWidget(disconnect);
	auto *retry=new QPushButton(text("Twitch.Retry"),dialog);form->addWidget(retry);
	auto *close=new QPushButton(text("Twitch.Close"),dialog);form->addWidget(close);
	const auto weak=std::weak_ptr<twitch::Service>(service);
	const auto launch=std::make_shared<AuthorizationLaunch>();
	auto refresh=[weak,safe=QPointer<QWidget>(group),status,detail,code,authorize,disconnect,connectButton,client,retry,renew,chatStatus,chatAuthorize,connect,launch,dialog,authorization]{
		const auto owner=weak.lock();if(!owner || !safe)return;status->setText(owner->status());detail->setText(owner->status());code->setText(owner->code());
		authorization->setText(QStringLiteral("● ")+text(owner->authorized()?"Twitch.Authorized":"Twitch.Unauthorized"));
		authorization->setStyleSheet(owner->authorized()?"color: #55d887; font-weight: 600;":"color: #ff6b6b; font-weight: 600;");
		authorization->setAccessibleName(text(owner->authorized()?"Twitch.Authorized":"Twitch.Unauthorized"));
		code->setVisible(!owner->code().isEmpty());authorize->setEnabled(!owner->verificationUrl().isEmpty());disconnect->setEnabled(owner->active());
		connectButton->setEnabled(!owner->active());client->setEnabled(!owner->active());
		retry->setVisible(owner->retainedBatch());
		renew->setEnabled(owner->canRenewAuthorization());
		chatStatus->setText(owner->shoutoutChatStatus());chatAuthorize->setEnabled(owner->canRenewAuthorization() && !owner->canSendChat());
		connect->setEnabled(!owner->active() || !owner->verificationUrl().isEmpty());
		if(const auto url=launch->update(owner->active(),owner->authorized(),owner->verificationUrl()))
			if(!QDesktopServices::openUrl(*url)){dialog->show();dialog->raise();}
	};
	service->setChanged(refresh);refresh();
	QObject::connect(configure,&QPushButton::clicked,group,[dialog]{dialog->show();dialog->raise();dialog->activateWindow();});
	QObject::connect(connect,&QPushButton::clicked,group,[weak,launch,refresh]{if(const auto owner=weak.lock()){
		launch->arm();if(!owner->verificationUrl().isEmpty())refresh();else owner->connectAccount({},twitch::AuthorizationFlow::Browser);
	}});
	QObject::connect(close,&QPushButton::clicked,dialog,&QDialog::hide);
	QObject::connect(connectButton,&QPushButton::clicked,dialog,[weak,client,advanced,launch]{if(const auto owner=weak.lock()){launch->arm();owner->connectAccount(advanced->isChecked()?client->text():owner->client(),twitch::AuthorizationFlow::Browser);}});
	QObject::connect(disconnect,&QPushButton::clicked,dialog,[weak]{if(const auto owner=weak.lock())owner->disconnectAccount();});
	QObject::connect(retry,&QPushButton::clicked,dialog,[weak]{if(const auto owner=weak.lock())owner->retryBatch();});
	QObject::connect(renew,&QPushButton::clicked,dialog,[weak,launch]{if(const auto owner=weak.lock()){launch->arm();owner->renewAuthorization();}});
	QObject::connect(chatAuthorize,&QPushButton::clicked,dialog,[weak,launch]{if(const auto owner=weak.lock()){launch->arm();owner->authorizeChatMessages(twitch::AuthorizationFlow::Browser);}});
	QObject::connect(authorize,&QPushButton::clicked,dialog,[weak]{if(const auto owner=weak.lock()){const QUrl url(owner->verificationUrl());if(AuthorizationLaunch::allowed(url))QDesktopServices::openUrl(url);}});
	return group;
}
}
