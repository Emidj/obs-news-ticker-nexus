// SPDX-License-Identifier: GPL-2.0-or-later
#include "browser-authorization.hpp"
#include "localization.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QUrlQuery>
#include <QRegularExpression>
#include <map>
#include <memory>
#include <algorithm>

namespace oto::twitch {
namespace {
QString randomSecret(){QByteArray bytes;for(int i=0;i<8;++i){const auto word=QRandomGenerator::system()->generate();bytes.append(reinterpret_cast<const char *>(&word),sizeof(word));}return QString::fromLatin1(bytes.toHex());}
}
BrowserAuthorization::BrowserAuthorization(QObject *parent):QObject(parent){
	deadline_.setSingleShot(true);
	connect(&server_,&QTcpServer::newConnection,this,[this]{accept();});
	connect(&deadline_,&QTimer::timeout,this,[this]{finish({});});
}
QUrl BrowserAuthorization::start(const QString &client,const QStringList &scopes,std::function<void(QString)> completed,quint16 port,int timeoutMs){
	stop();if(!server_.listen(QHostAddress::LocalHost,port))return {};
	redirect_=QUrl(QStringLiteral("http://localhost:%1").arg(server_.serverPort()));
	state_=randomSecret();completionPath_="/complete/"+randomSecret();nonce_=randomSecret();completed_=std::move(completed);
	QUrl result("https://id.twitch.tv/oauth2/authorize");QUrlQuery query;
	query.addQueryItem("client_id",client);query.addQueryItem("response_type","token");
	query.addQueryItem("redirect_uri",redirect_.toString());query.addQueryItem("scope",scopes.join(' '));query.addQueryItem("state",state_);
	result.setQuery(query);deadline_.start(std::max(1,timeoutMs));return result;
}
void BrowserAuthorization::stop(){
	++generation_;
	deadline_.stop();server_.close();completed_={};state_.clear();completionPath_.clear();nonce_.clear();redirect_={};
	const auto sockets=std::move(sockets_);sockets_.clear();for(auto *socket:sockets){socket->disconnect(this);socket->abort();socket->deleteLater();}
}
void BrowserAuthorization::finish(QString token,QTcpSocket *accepted){
	// Let the accepted response flush before retiring that connection. Stop all
	// other work before dispatching, so late/replayed replies cannot win.
	if(accepted)sockets_.erase(accepted);
	auto callback=std::move(completed_);stop();
	const auto generation=generation_;
	if(callback)QTimer::singleShot(0,this,[this,generation,callback=std::move(callback),token=std::move(token)]{if(generation==generation_)callback(token);});
}
void BrowserAuthorization::accept(){
	while(server_.hasPendingConnections()){
		auto *socket=server_.nextPendingConnection();socket->setParent(this);
		if(!socket->peerAddress().isLoopback() || sockets_.size()>=8){socket->abort();socket->deleteLater();continue;}
		socket->setReadBufferSize(32769);sockets_.insert(socket);auto buffer=std::make_shared<QByteArray>();
		connect(socket,&QTcpSocket::disconnected,this,[this,socket]{sockets_.erase(socket);socket->deleteLater();});
		connect(socket,&QTcpSocket::readyRead,this,[this,socket,buffer]{read(socket,buffer);});
		QTimer::singleShot(10000,socket,[socket]{socket->abort();socket->deleteLater();});
	}
}
void BrowserAuthorization::reply(QTcpSocket *socket,int status,const QByteArray &body,const QByteArray &type){
	socket->setProperty("answered",true);
	const auto policy="default-src 'none'; script-src 'nonce-"+nonce_.toUtf8()+"'; connect-src 'self'; base-uri 'none'; frame-ancestors 'none'";
	socket->write("HTTP/1.1 "+QByteArray::number(status)+" Reply\r\nContent-Type: "+type+"\r\nContent-Length: "+QByteArray::number(body.size())+
		"\r\nCache-Control: no-store\r\nReferrer-Policy: no-referrer\r\nX-Content-Type-Options: nosniff\r\nContent-Security-Policy: "+policy+"\r\nConnection: close\r\n\r\n"+body);
	socket->disconnectFromHost();
}
void BrowserAuthorization::read(QTcpSocket *socket,const std::shared_ptr<QByteArray> &buffer){
	if(socket->property("answered").toBool())return;
	*buffer+=socket->readAll();if(buffer->size()>32768){reply(socket,413,"Request too large");return;}
	const auto headerEnd=buffer->indexOf("\r\n\r\n");if(headerEnd<0){if(buffer->size()>16384)reply(socket,413,"Headers too large");return;}
	if(headerEnd>16384){reply(socket,413,"Headers too large");return;}
	const auto lines=buffer->left(headerEnd).split('\n');const auto request=lines.value(0).trimmed().split(' ');
	if(request.size()!=3 || request[2]!="HTTP/1.1"){reply(socket,400,"Invalid request");return;}
	std::map<QByteArray,QByteArray> headers;
	for(int i=1;i<lines.size();++i){const auto line=lines[i].trimmed();const auto colon=line.indexOf(':');if(colon<=0 || !headers.emplace(line.left(colon).toLower(),line.mid(colon+1).trimmed()).second){reply(socket,400,"Invalid headers");return;}}
	const auto host="localhost:"+QByteArray::number(server_.serverPort());
	if(headers["host"]!=host || headers.count("transfer-encoding") || (headers.count("origin") && headers["origin"]!=redirect_.toEncoded())){reply(socket,403,"Invalid origin");return;}
	bool validLength=true;const auto size=headers.count("content-length")?headers["content-length"].toLongLong(&validLength):0;
	if(!validLength || size<0 || size>16384){reply(socket,413,"Invalid length");return;}
	if(buffer->size()<headerEnd+4+size)return;
	if(buffer->size()!=headerEnd+4+size){reply(socket,400,"Invalid body");return;}
	if(request[0]=="GET" && request[1]=="/" && size==0){
		// The token stays in the browser fragment, is removed from history, and is
		// posted locally. There are no external scripts, assets or token-bearing URLs.

        const auto js=[](const char *key){
            auto json=QJsonDocument(QJsonArray{i18n::qtext(key)}).toJson(QJsonDocument::Compact);
            return json.mid(1,json.size()-2).replace("<","\\u003c");
        };
        const auto html=QByteArray("<!doctype html><html lang='")+nexus::localization::systemLanguage().left(2).toUtf8()+
            "'><meta charset='utf-8'><title>OBS Twitch Overlays NEXUS</title><body><h1>OBS Twitch Overlays NEXUS</h1><p id='result'>"+
            i18n::qtext("Auth.Checking").toHtmlEscaped().toUtf8()+"</p><script nonce='"+nonce_.toUtf8()+"'>"
            "const p=new URLSearchParams(location.hash.slice(1));history.replaceState(null,'','/');"
            "if(!p.get('state')){document.getElementById('result').textContent="+js("Auth.Return")+";}else{"
            "fetch('"+completionPath_.toUtf8()+"',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({state:p.get('state'),access_token:p.get('access_token'),token_type:p.get('token_type'),error:p.get('error')})})"
            ".then(r=>{document.getElementById('result').textContent=r.ok?"+js("Auth.Received")+":"+js("Auth.Failed")+";})"
            ".catch(()=>{document.getElementById('result').textContent="+js("Auth.Return")+";});}</script></body></html>";
		reply(socket,200,html,"text/html; charset=utf-8");return;
	}
	if(request[0]!="POST" || request[1]!=completionPath_.toUtf8()){reply(socket,404,"Not found");return;}
	if(headers["content-type"]!="application/json"){reply(socket,415,"JSON required");return;}
	const auto document=QJsonDocument::fromJson(buffer->mid(headerEnd+4));const auto data=document.object();
	if(!document.isObject() || data["state"].toString()!=state_){reply(socket,403,"Invalid state");return;}
	const auto token=data["access_token"].toString();static const QRegularExpression validToken("^[A-Za-z0-9._~-]{1,4096}$");
	if(!data["error"].toString().isEmpty()){reply(socket,200,"Authorization declined");finish({},socket);return;}
	if(data["token_type"].toString().compare("bearer",Qt::CaseInsensitive)!=0 || !validToken.match(token).hasMatch()){reply(socket,400,"Invalid authorization");return;}
	reply(socket,200,"Authorization received");finish(token,socket);
}
}
