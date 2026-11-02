// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QObject>
#include <QByteArray>
#include <QTimer>
#include <QSocketNotifier>
#include <curl/curl.h>
#include <functional>
#include <map>
#include <memory>
namespace oto::twitch {
struct HttpReply { long status=0; QByteArray body; bool networkError=false; };
enum class SocketCloseCause { Peer, Connect, Read, Write, InvalidFrame, ReceiveLimit };
struct SocketClose { SocketCloseCause cause=SocketCloseCause::Connect; int peerCode=0; CURLcode networkCode=CURLE_OK; };
// One GUI-thread network owner. Native socket notifications, no polling in idle.
class Transport final:public QObject {
public:
	using Id=std::uint64_t;
	explicit Transport(QObject *parent=nullptr);
	~Transport() override;
	Id request(const QByteArray &url,const QByteArray &body,const QList<QByteArray> &headers,std::function<void(HttpReply)> done,size_t maxBytes=128*1024,const QByteArray &method={});
	Id websocket(const QByteArray &url,std::function<void()> opened,std::function<void(QByteArray)> message,std::function<void(SocketClose)> closed);
	bool sendText(Id,const QByteArray &); // Bounded, masked RFC6455 text frame.
	void cancel(Id id);
	void stop();
private:
	struct Task {
		Id id=0; CURL *easy=nullptr; curl_slist *headers=nullptr; QByteArray url,body,received,frame,raw,outgoing,accept;
		size_t maxBytes=128*1024;
		bool ws=false,connected=false,handshaken=false,fragmented=false,continuation=false; std::unique_ptr<QSocketNotifier> reader,writer;
		SocketClose close;
		std::function<void(HttpReply)> done; std::function<void()> opened; std::function<void(SocketClose)> closed; std::function<void(QByteArray)> message;
		~Task();
	};
	struct Watch { QSocketNotifier *read=nullptr,*write=nullptr; };
	static size_t receive(char *,size_t,size_t,void *);
	static int socketCallback(CURL *,curl_socket_t,int,void *,void *);
	static int timerCallback(CURLM *,long,void *);
	void watch(curl_socket_t socket,int action);
	void perform(curl_socket_t socket,int action);
	void readWebsocket(Id id);
	void writeWebsocket(Id id);
	int decodeFrame(Task &task);
	void pong(Task &task,const QByteArray &payload);
	void failWebsocket(Id id);
	Task &create(const QByteArray &url);
	CURLM *multi_=nullptr; QTimer timer_; Id next_=1;std::uint64_t epoch_=0;
	std::map<Id,std::unique_ptr<Task>> tasks_; std::map<curl_socket_t,Watch> watches_;
};
}
