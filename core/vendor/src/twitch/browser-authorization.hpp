// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QStringList>
#include <functional>
#include <memory>
#include <set>

namespace oto::twitch {
// A temporary loopback receiver for the browser's OAuth fragment. It is not a
// hosted service and exists only during an explicit authorization attempt.
class BrowserAuthorization final : public QObject {
public:
	explicit BrowserAuthorization(QObject *parent=nullptr);
	~BrowserAuthorization() override { stop(); }
	QUrl start(const QString &client,const QStringList &scopes,
		std::function<void(QString)> completed,quint16 port=3000,int timeoutMs=300000);
	void stop();
	QUrl redirectUri() const { return redirect_; }
	bool active() const { return server_.isListening(); }
private:
	void accept();
	void read(QTcpSocket *socket,const std::shared_ptr<QByteArray> &buffer);
	void reply(QTcpSocket *socket,int status,const QByteArray &body,const QByteArray &type="text/plain; charset=utf-8");
	void finish(QString token,QTcpSocket *accepted=nullptr);
	QTcpServer server_;QTimer deadline_;QUrl redirect_;
	QString state_,completionPath_,nonce_;std::function<void(QString)> completed_;
	std::set<QTcpSocket *> sockets_;
	std::uint64_t generation_=0;
};
}
