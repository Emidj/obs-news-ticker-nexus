// SPDX-License-Identifier: GPL-2.0-or-later
#include "transport.hpp"
#include <curl/websockets.h>
#include <QMetaObject>
#include <QUrl>
#include <QRandomGenerator>
#include <QCryptographicHash>
#include <QStringConverter>
#include <stdexcept>
#include <vector>
namespace oto::twitch {
Transport::Task::~Task(){if(easy)curl_easy_cleanup(easy);if(headers)curl_slist_free_all(headers);}
Transport::Transport(QObject *parent):QObject(parent){
	if(curl_global_init(CURL_GLOBAL_DEFAULT)!=CURLE_OK)throw std::runtime_error("Network initialization failed");
	multi_=curl_multi_init();if(!multi_){curl_global_cleanup();throw std::runtime_error("Network initialization failed");}
	curl_multi_setopt(multi_,CURLMOPT_SOCKETFUNCTION,&Transport::socketCallback);curl_multi_setopt(multi_,CURLMOPT_SOCKETDATA,this);
	curl_multi_setopt(multi_,CURLMOPT_TIMERFUNCTION,&Transport::timerCallback);curl_multi_setopt(multi_,CURLMOPT_TIMERDATA,this);
	timer_.setSingleShot(true);connect(&timer_,&QTimer::timeout,this,[this]{perform(CURL_SOCKET_TIMEOUT,0);});
}
Transport::~Transport(){stop();curl_multi_cleanup(multi_);curl_global_cleanup();}
Transport::Task &Transport::create(const QByteArray &url){
	if(tasks_.size()>=6)throw std::runtime_error("Network request capacity reached");
	auto task=std::make_unique<Task>();task->id=next_++;task->url=url;task->easy=curl_easy_init();
	if(!task->easy)throw std::runtime_error("Network request initialization failed");
	// URLs are chosen by Service. Redirects stay disabled, credentials never leave an endpoint.
	curl_easy_setopt(task->easy,CURLOPT_URL,task->url.constData());curl_easy_setopt(task->easy,CURLOPT_NOSIGNAL,1L);
	curl_easy_setopt(task->easy,CURLOPT_CONNECTTIMEOUT_MS,15000L);curl_easy_setopt(task->easy,CURLOPT_TIMEOUT_MS,20000L);
	curl_easy_setopt(task->easy,CURLOPT_WRITEFUNCTION,&Transport::receive);curl_easy_setopt(task->easy,CURLOPT_WRITEDATA,task.get());
	curl_easy_setopt(task->easy,CURLOPT_PRIVATE,task.get());
	auto &result=*task;tasks_.emplace(task->id,std::move(task));return result;
}
Transport::Id Transport::request(const QByteArray &url,const QByteArray &body,const QList<QByteArray> &headers,std::function<void(HttpReply)> done,size_t maxBytes,const QByteArray &method){
	if(!method.isEmpty()&&method!="PATCH")throw std::invalid_argument("Unsupported HTTP method");
	if(!maxBytes || maxBytes>2*1024*1024)throw std::invalid_argument("Invalid HTTP receive limit");
	auto &t=create(url);t.done=std::move(done);t.body=body;t.maxBytes=maxBytes;
	for(const auto &header:headers)t.headers=curl_slist_append(t.headers,header.constData());
	curl_easy_setopt(t.easy,CURLOPT_HTTPHEADER,t.headers);
	if(!body.isNull()){curl_easy_setopt(t.easy,CURLOPT_POSTFIELDS,t.body.constData());curl_easy_setopt(t.easy,CURLOPT_POSTFIELDSIZE,long(t.body.size()));}
	if(!method.isEmpty())curl_easy_setopt(t.easy,CURLOPT_CUSTOMREQUEST,method.constData());
	if(curl_multi_add_handle(multi_,t.easy)!=CURLM_OK){const auto id=t.id;cancel(id);throw std::runtime_error("Network request failed");}
	return t.id;
}
Transport::Id Transport::websocket(const QByteArray &url,std::function<void()> opened,std::function<void(QByteArray)> message,std::function<void(SocketClose)> closed){
	QUrl endpoint=QUrl::fromEncoded(url);if(endpoint.scheme()!="ws" && endpoint.scheme()!="wss")throw std::runtime_error("Invalid WebSocket endpoint");
	endpoint.setScheme(endpoint.scheme()=="wss"?"https":"http");
	auto &t=create(endpoint.toEncoded());t.ws=true;t.opened=std::move(opened);t.message=std::move(message);t.closed=std::move(closed);
	// OBS's curl 8.12 does not expose FIN reliably. Own the bounded RFC 6455 framing;
	// curl still owns TLS/certificate validation, proxies and the native connection.
	curl_easy_setopt(t.easy,CURLOPT_CONNECT_ONLY,1L);
	// This is a persistent socket, not a 20-second HTTP transfer. Schannel still
	// checks the transfer deadline in curl_easy_send(), including later Pongs.
	// Connect remains bounded by CONNECTTIMEOUT; Service owns receive deadlines.
	curl_easy_setopt(t.easy,CURLOPT_TIMEOUT_MS,0L);
	if(curl_multi_add_handle(multi_,t.easy)!=CURLM_OK){const auto id=t.id;cancel(id);throw std::runtime_error("WebSocket initialization failed");}return t.id;
}
size_t Transport::receive(char *data,size_t size,size_t count,void *opaque){
	auto &task=*static_cast<Task *>(opaque);const auto n=size*count;
	if(n>task.maxBytes || size_t(task.received.size())+n>task.maxBytes)return 0;
	task.received.append(data,qsizetype(n));return n;
}
int Transport::socketCallback(CURL *,curl_socket_t socket,int action,void *opaque,void *){static_cast<Transport *>(opaque)->watch(socket,action);return 0;}
int Transport::timerCallback(CURLM *,long timeout,void *opaque){auto &self=*static_cast<Transport *>(opaque);if(timeout<0)self.timer_.stop();else self.timer_.start(int(timeout));return 0;}
void Transport::watch(curl_socket_t socket,int action){
	const auto old=watches_.find(socket);
	if(old!=watches_.end()){
		if(old->second.read){old->second.read->setEnabled(false);old->second.read->deleteLater();}
		if(old->second.write){old->second.write->setEnabled(false);old->second.write->deleteLater();}
		watches_.erase(old);
	}
	if(action==CURL_POLL_REMOVE)return;
	Watch w;
	if(action==CURL_POLL_IN || action==CURL_POLL_INOUT){
		w.read=new QSocketNotifier(qintptr(socket),QSocketNotifier::Read,this);
		connect(w.read,&QSocketNotifier::activated,this,[this,socket]{perform(socket,CURL_CSELECT_IN);});
	}
	if(action==CURL_POLL_OUT || action==CURL_POLL_INOUT){
		w.write=new QSocketNotifier(qintptr(socket),QSocketNotifier::Write,this);
		connect(w.write,&QSocketNotifier::activated,this,[this,socket]{perform(socket,CURL_CSELECT_OUT);});
	}
	watches_.emplace(socket,w);
}
void Transport::perform(curl_socket_t socket,int action){
	int running=0;curl_multi_socket_action(multi_,socket,action,&running);
	int left=0;
	while(auto *msg=curl_multi_info_read(multi_,&left)){
		if(msg->msg!=CURLMSG_DONE)continue;
		Task *task=nullptr;curl_easy_getinfo(msg->easy_handle,CURLINFO_PRIVATE,&task);if(!task)continue;
		const auto id=task->id;
		if(task->ws && msg->data.result==CURLE_OK){
			curl_socket_t active=CURL_SOCKET_BAD;curl_easy_getinfo(task->easy,CURLINFO_ACTIVESOCKET,&active);
			if(active==CURL_SOCKET_BAD){failWebsocket(id);continue;}
			task->connected=true;task->reader=std::make_unique<QSocketNotifier>(qintptr(active),QSocketNotifier::Read,this);
			task->writer=std::make_unique<QSocketNotifier>(qintptr(active),QSocketNotifier::Write,this);
			connect(task->reader.get(),&QSocketNotifier::activated,this,[this,id]{readWebsocket(id);});
			connect(task->writer.get(),&QSocketNotifier::activated,this,[this,id]{writeWebsocket(id);});
			QByteArray nonce;for(int i=0;i<4;++i){const auto n=QRandomGenerator::system()->generate();nonce.append(reinterpret_cast<const char *>(&n),4);}const auto key=nonce.toBase64();
			task->accept=QCryptographicHash::hash(key+"258EAFA5-E914-47DA-95CA-C5AB0DC85B11",QCryptographicHash::Sha1).toBase64();
			const auto endpoint=QUrl::fromEncoded(task->url);auto path=endpoint.path(QUrl::FullyEncoded).toUtf8();if(path.isEmpty())path="/";if(endpoint.hasQuery())path+='?'+endpoint.query(QUrl::FullyEncoded).toUtf8();
			auto host=endpoint.host().toUtf8();if(endpoint.port()!=-1)host+=':'+QByteArray::number(endpoint.port());
			task->outgoing="GET "+path+" HTTP/1.1\r\nHost: "+host+"\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: "+key+"\r\nSec-WebSocket-Version: 13\r\n\r\n";
			QTimer::singleShot(0,this,[this,id]{writeWebsocket(id);readWebsocket(id);});
		}else if(task->ws){task->close={SocketCloseCause::Connect,0,msg->data.result};failWebsocket(id);}
		else {
			HttpReply reply;reply.networkError=msg->data.result!=CURLE_OK;curl_easy_getinfo(task->easy,CURLINFO_RESPONSE_CODE,&reply.status);reply.body=std::move(task->received);
			auto done=std::move(task->done);cancel(id);
			const auto epoch=epoch_;QTimer::singleShot(0,this,[this,epoch,done=std::move(done),reply=std::move(reply)]()mutable{if(epoch==epoch_ && done)done(std::move(reply));});
		}
	}
}
void Transport::readWebsocket(Id id){
	const auto found=tasks_.find(id);if(found==tasks_.end() || !found->second->connected)return;
	auto &t=*found->second;t.continuation=false;
	for(int turn=0;turn<32;++turn){
		t.close={SocketCloseCause::InvalidFrame};
		const int decoded=decodeFrame(t);if(decoded<0){failWebsocket(id);return;}if(decoded>0)continue;
		char buffer[4096];size_t received=0;const auto rc=curl_easy_recv(t.easy,buffer,sizeof(buffer),&received);
		if(rc==CURLE_AGAIN)return;
		if(rc!=CURLE_OK || !received){t.close={SocketCloseCause::Read,0,rc};failWebsocket(id);return;}
		if(t.raw.size()+received>144*1024){t.close={SocketCloseCause::ReceiveLimit};failWebsocket(id);return;}t.raw.append(buffer,qsizetype(received));
	}
	if(!t.continuation){t.continuation=true;QTimer::singleShot(0,this,[this,id]{readWebsocket(id);});}
}
void Transport::writeWebsocket(Id id){
	const auto found=tasks_.find(id);if(found==tasks_.end())return;auto &t=*found->second;
	while(!t.outgoing.isEmpty()){
		size_t sent=0;const auto rc=curl_easy_send(t.easy,t.outgoing.constData(),size_t(t.outgoing.size()),&sent);
		if(rc==CURLE_AGAIN){t.writer->setEnabled(true);return;}
		if(rc!=CURLE_OK || !sent){t.close={SocketCloseCause::Write,0,rc};failWebsocket(id);return;}t.outgoing.remove(0,qsizetype(sent));
	}
	t.writer->setEnabled(false);
}
bool Transport::sendText(Id id,const QByteArray &payload){
	const auto found=tasks_.find(id);if(found==tasks_.end() || !found->second->handshaken || payload.size()>2048)return false;
	auto &t=*found->second;if(t.outgoing.size()+payload.size()+8>4096)return false;
	QStringDecoder decoder(QStringDecoder::Utf8);decoder.decode(payload);if(decoder.hasError())return false;
	QByteArray packet;packet+=char(0x81);if(payload.size()<126)packet+=char(0x80|payload.size());else{packet+=char(0xfe);packet+=char(payload.size()>>8);packet+=char(payload.size());}
	const auto mask=QRandomGenerator::system()->generate();const auto *key=reinterpret_cast<const char *>(&mask);packet.append(key,4);
	for(qsizetype i=0;i<payload.size();++i)packet+=char(payload[i]^key[i%4]);t.outgoing+=packet;t.writer->setEnabled(true);return true;
}
void Transport::pong(Task &t,const QByteArray &payload){
	if(t.outgoing.size()+payload.size()+6>4096)throw std::runtime_error("WebSocket control queue full");
	QByteArray packet;packet+=char(0x8a);packet+=char(0x80|payload.size());const auto mask=QRandomGenerator::system()->generate();
	const auto *key=reinterpret_cast<const char *>(&mask);packet.append(key,4);for(qsizetype i=0;i<payload.size();++i)packet+=char(payload[i]^key[i%4]);
	t.outgoing+=packet;t.writer->setEnabled(true);
}
int Transport::decodeFrame(Task &t){
	if(!t.handshaken){
		const auto end=t.raw.indexOf("\r\n\r\n");if(end<0)return t.raw.size()>16*1024?-1:0;
		if(end>16*1024)return -1;
		const auto lines=t.raw.left(end).split('\n');if(lines.isEmpty() || !lines[0].trimmed().startsWith("HTTP/1.1 101 "))return -1;
		QByteArray accept,upgrade,connection;
		for(int i=1;i<lines.size();++i){const auto line=lines[i].trimmed();const auto colon=line.indexOf(':');if(colon<=0)return -1;
			const auto key=line.left(colon).toLower(),value=line.mid(colon+1).trimmed();
			if(key=="sec-websocket-accept"){if(!accept.isEmpty())return -1;accept=value;}
			else if(key=="upgrade")upgrade=value.toLower();else if(key=="connection")connection+=value.toLower()+',';
			else if(key=="sec-websocket-extensions" || key=="sec-websocket-protocol")return -1;
		}
		bool connectionUpgrade=false;for(const auto &v:connection.split(','))connectionUpgrade=connectionUpgrade || v.trimmed()=="upgrade";
		if(accept!=t.accept || upgrade!="websocket" || !connectionUpgrade)return -1;
		t.handshaken=true;t.raw.remove(0,end+4);const auto id=t.id;
		QTimer::singleShot(0,this,[this,id]{const auto found=tasks_.find(id);if(found!=tasks_.end() && found->second->opened)found->second->opened();});return 1;
	}
	if(t.raw.size()<2)return 0;
	const auto first=static_cast<unsigned char>(t.raw[0]),second=static_cast<unsigned char>(t.raw[1]);
	const bool fin=first&128;const int opcode=first&15;quint64 length=second&127;int header=2;
	if(first&0x70 || second&0x80)return -1;
	if(length==126){if(t.raw.size()<4)return 0;length=(quint64(static_cast<unsigned char>(t.raw[2]))<<8)|static_cast<unsigned char>(t.raw[3]);header=4;if(length<126)return -1;}
	else if(length==127){if(t.raw.size()<10)return 0;length=0;for(int i=2;i<10;++i)length=(length<<8)|static_cast<unsigned char>(t.raw[i]);header=10;if(length<65536)return -1;}
	if(length>128*1024 || (opcode>=8 && (!fin || length>125)))return -1;
	if(t.raw.size()<qint64(length)+header)return 0;
	const auto payload=t.raw.mid(header,qsizetype(length));t.raw.remove(0,header+qsizetype(length));
	if(opcode==8){
		if(payload.size()==1)return -1;
		t.close={SocketCloseCause::Peer};
		if(payload.size()>=2)t.close.peerCode=(static_cast<unsigned char>(payload[0])<<8)|static_cast<unsigned char>(payload[1]);
		return -1;
	}
	if(opcode==9){try{pong(t,payload);}catch(...){return -1;}return 1;}
	if(opcode==10)return 1;
	if(opcode!=0 && opcode!=1)return -1;
	if((opcode==0 && !t.fragmented) || (opcode==1 && t.fragmented) || t.frame.size()+payload.size()>128*1024)return -1;
	t.frame+=payload;t.fragmented=!fin;
	if(fin){
		auto data=std::move(t.frame);t.frame.clear();QStringDecoder decoder(QStringDecoder::Utf8);const QString utf8=decoder.decode(data);if(decoder.hasError())return -1;
		const auto id=t.id;QTimer::singleShot(0,this,[this,id,data=std::move(data)]()mutable{const auto found=tasks_.find(id);if(found!=tasks_.end() && found->second->message)found->second->message(std::move(data));});
	}return 1;
}
void Transport::failWebsocket(Id id){
	const auto found=tasks_.find(id);if(found==tasks_.end())return;
	const auto code=found->second->close;auto closed=std::move(found->second->closed);cancel(id);
	const auto epoch=epoch_;QTimer::singleShot(0,this,[this,epoch,code,closed=std::move(closed)]{if(epoch==epoch_ && closed)closed(code);});
}
void Transport::cancel(Id id){const auto found=tasks_.find(id);if(found==tasks_.end())return;curl_multi_remove_handle(multi_,found->second->easy);tasks_.erase(found);}
void Transport::stop(){++epoch_;while(!tasks_.empty())cancel(tasks_.begin()->first);timer_.stop();for(auto &[s,w]:watches_){delete w.read;delete w.write;}watches_.clear();}
}
