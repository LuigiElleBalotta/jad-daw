#include "bridge/remote_server.h"

#include <QHostAddress>
#include <QNetworkInterface>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>
#include <QtEndian>

namespace jad {
namespace {

// The page of the phone: the grid, one button per cell and scene, refreshed twice a second.
const char* const kPage = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no">
<title>Live Loops</title>
<style>
body{margin:0;background:#1b1b1d;color:#eee;font:15px -apple-system,Segoe UI,sans-serif;-webkit-user-select:none;user-select:none}
header{display:flex;gap:8px;padding:8px;position:sticky;top:0;background:#1b1b1d}
button{font:inherit;color:#eee;background:#34343a;border:1px solid #55555c;border-radius:6px;padding:10px 12px}
#grid{padding:0 8px 16px;overflow-x:auto}
.row{display:flex;gap:6px;margin-bottom:6px;align-items:stretch}
.name{min-width:84px;max-width:84px;font-size:13px;padding:6px 4px;overflow:hidden;color:#bbb}
.cell{min-width:84px;height:54px;padding:2px;font-size:12px;overflow:hidden}
.empty{background:#26262a;border-color:#333;color:#555}
.stopped{background:#3a4a5c}.queued{background:#8a6a1c;border-color:#e0b040}.playing,.stopping{background:#1f7a3a;border-color:#5fe08a}
.recording,.recordQueued{background:#8a1f1f;border-color:#ff6060}
.scene{min-width:84px;background:#444}
</style></head><body>
<header><button id="trigger">Trigger</button><button id="stop">Stop all</button><button id="perf">Performance</button></header>
<div id="grid"></div>
<script>
const k=new URLSearchParams(location.search).get('k')||'';
const send=(c,a)=>fetch('/cmd?k='+k+'&c='+c+'&a='+encodeURIComponent(a.join(',')));
document.getElementById('trigger').onclick=()=>send('trigger',[]);
document.getElementById('stop').onclick=()=>send('stopAll',[]);
document.getElementById('perf').onclick=()=>send('performance',[]);
function press(el,fn,up){el.addEventListener('pointerdown',e=>{e.preventDefault();fn(1)});el.addEventListener('pointerup',()=>fn(0));el.addEventListener('pointerleave',()=>{})}
let last='';
async function refresh(){
  try{
    const r=await fetch('/state?k='+k);const g=await r.json();
    const sig=JSON.stringify(g);if(sig===last)return;last=sig;
    const root=document.getElementById('grid');root.innerHTML='';
    g.tracks.forEach(t=>{
      const row=document.createElement('div');row.className='row';
      const n=document.createElement('div');n.className='name';n.textContent=t.name;row.appendChild(n);
      for(let s=0;s<g.scenes;s++){
        const c=g.cells.find(x=>x.trackId===t.id&&x.scene===s);
        const b=document.createElement('button');
        b.className='cell '+(c?c.state:'empty');b.textContent=c?(c.name||t.name):'';
        if(c)press(b,on=>send('cell',[t.id,s,on]));
        row.appendChild(b);
      }
      root.appendChild(row);
    });
    const row=document.createElement('div');row.className='row';
    const sp=document.createElement('div');sp.className='name';row.appendChild(sp);
    for(let s=0;s<g.scenes;s++){
      const b=document.createElement('button');b.className='cell scene';
      b.textContent='▶ '+((g.sceneNames[s])||('Scene '+(s+1)));
      press(b,on=>send('scene',[s,on]));row.appendChild(b);
    }
    root.appendChild(row);
  }catch(e){}
}
setInterval(refresh,500);refresh();
</script></body></html>)HTML";

QByteArray response(int code, const char* reason, const char* type, const QByteArray& body) {
    QByteArray r = "HTTP/1.1 " + QByteArray::number(code) + " " + reason + "\r\nContent-Type: " + type + "\r\nContent-Length: " + QByteArray::number(body.size())
                   + "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
    return r + body;
}

// One OSC message: the address, the type tags, then the arguments (big-endian 32-bit integers and floats).
void oscMessage(const QByteArray& data, const RemoteServer::CommandFn& command) {
    const auto padded = [](int n) { return (n + 4) & ~3; };  // a string and its zeros, up to a multiple of four
    const int end = data.indexOf('\0');
    if (end <= 0) return;
    const QString address = QString::fromLatin1(data.left(end));
    int pos = padded(end);
    double value = 1.0;
    if (pos < data.size() && data[pos] == ',') {
        const int tagEnd = data.indexOf('\0', pos);
        if (tagEnd < 0) return;
        const QByteArray tags = data.mid(pos + 1, tagEnd - pos - 1);
        pos += padded(tagEnd - pos);
        if (!tags.isEmpty() && pos + 4 <= data.size()) {
            const quint32 raw = qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(data.constData() + pos));
            if (tags[0] == 'f') {
                float f;
                memcpy(&f, &raw, 4);
                value = f;
            } else if (tags[0] == 'i') {
                value = static_cast<qint32>(raw);
            }
        } else if (!tags.isEmpty() && tags[0] == 'F') {
            value = 0.0;
        }
    }
    const QStringList parts = address.split('/', Qt::SkipEmptyParts);
    if (parts.size() < 2 || parts[0] != QLatin1String("jad")) return;
    const QString on = value > 0.0 ? QStringLiteral("1") : QStringLiteral("0");
    if (parts[1] == QLatin1String("cell") && parts.size() == 4) command(QStringLiteral("cellAt"), {parts[2], parts[3], on});
    else if (parts[1] == QLatin1String("scene") && parts.size() == 3) command(QStringLiteral("scene"), {QString::number(parts[2].toInt() - 1), on});
    else if (on == QLatin1String("1") && parts[1] == QLatin1String("stopall")) command(QStringLiteral("stopAll"), {});
    else if (on == QLatin1String("1") && parts[1] == QLatin1String("trigger")) command(QStringLiteral("trigger"), {});
    else if (on == QLatin1String("1") && parts[1] == QLatin1String("performance")) command(QStringLiteral("performance"), {});
}

}  // namespace

RemoteServer::RemoteServer(StateFn state, CommandFn command, QObject* parent) : QObject(parent), state_(std::move(state)), command_(std::move(command)) {}

RemoteServer::~RemoteServer() { stop(); }

bool RemoteServer::start(quint16 httpPort, quint16 oscPort) {
    stop();
    http_ = new QTcpServer(this);
    if (!http_->listen(QHostAddress::Any, httpPort) && !http_->listen(QHostAddress::Any, 0)) {
        delete http_;
        http_ = nullptr;
        return false;
    }
    token_ = QUuid::createUuid().toString(QUuid::Id128).left(12);
    connect(http_, &QTcpServer::newConnection, this, &RemoteServer::accept);
    osc_ = new QUdpSocket(this);
    if (oscPort == 0 || !osc_->bind(QHostAddress::Any, oscPort, QUdpSocket::ShareAddress)) {
        delete osc_;
        osc_ = nullptr;  // the web page still works
    } else {
        connect(osc_, &QUdpSocket::readyRead, this, &RemoteServer::readOsc);
    }
    return true;
}

void RemoteServer::stop() {
    for (QTcpSocket* s : buffers_.keys()) s->deleteLater();
    buffers_.clear();
    delete http_;
    http_ = nullptr;
    delete osc_;
    osc_ = nullptr;
}

quint16 RemoteServer::httpPort() const { return http_ ? http_->serverPort() : 0; }
quint16 RemoteServer::oscPort() const { return osc_ ? osc_->localPort() : 0; }

QStringList RemoteServer::addresses() const {
    QStringList out;
    if (!http_) return out;
    for (const QHostAddress& a : QNetworkInterface::allAddresses())
        if (a.protocol() == QAbstractSocket::IPv4Protocol && !a.isLoopback()) out << QStringLiteral("http://%1:%2/?k=%3").arg(a.toString()).arg(httpPort()).arg(token_);
    if (out.isEmpty()) out << QStringLiteral("http://localhost:%1/?k=%2").arg(httpPort()).arg(token_);
    return out;
}

void RemoteServer::accept() {
    while (QTcpSocket* s = http_->nextPendingConnection()) {
        buffers_.insert(s, {});
        connect(s, &QTcpSocket::readyRead, this, [this, s] { readFrom(s); });
        connect(s, &QTcpSocket::disconnected, this, [this, s] {
            buffers_.remove(s);
            s->deleteLater();
        });
    }
}

void RemoteServer::readFrom(QTcpSocket* socket) {
    QByteArray& buf = buffers_[socket];
    buf += socket->readAll();
    if (buf.size() > 16384) {
        socket->disconnectFromHost();
        return;
    }
    if (!buf.contains("\r\n\r\n")) return;
    answer(socket, buf);
    buf.clear();
}

void RemoteServer::answer(QTcpSocket* socket, const QByteArray& request) {
    const QList<QByteArray> line = request.left(request.indexOf("\r\n")).split(' ');
    QByteArray reply;
    if (line.size() < 2 || line[0] != "GET") {
        reply = response(405, "Method Not Allowed", "text/plain", "GET only");
    } else {
        const QUrl url = QUrl::fromEncoded(line[1]);
        const QUrlQuery query(url);
        if (query.queryItemValue("k") != token_) {
            reply = response(403, "Forbidden", "text/plain", "Open the address that Live Loops shows (it ends with ?k=...)");
        } else if (url.path() == QLatin1String("/")) {
            reply = response(200, "OK", "text/html; charset=utf-8", QByteArray(kPage));
        } else if (url.path() == QLatin1String("/state")) {
            reply = response(200, "OK", "application/json", state_());
        } else if (url.path() == QLatin1String("/cmd")) {
            const QStringList args = query.queryItemValue("a", QUrl::FullyDecoded).split(',', Qt::SkipEmptyParts);
            command_(query.queryItemValue("c"), args);
            reply = response(200, "OK", "text/plain", "ok");
        } else {
            reply = response(404, "Not Found", "text/plain", "not found");
        }
    }
    socket->write(reply);
    socket->disconnectFromHost();
}

void RemoteServer::parseOsc(const QByteArray& packet, const CommandFn& command) {
    if (packet.startsWith("#bundle")) {  // a bundle: 8 bytes of name, 8 of time, then size-prefixed elements
        int pos = 16;
        while (pos + 4 <= packet.size()) {
            const int size = static_cast<int>(qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(packet.constData() + pos)));
            pos += 4;
            if (size <= 0 || pos + size > packet.size()) return;
            parseOsc(packet.mid(pos, size), command);
            pos += size;
        }
        return;
    }
    oscMessage(packet, command);
}

void RemoteServer::readOsc() {
    while (osc_ && osc_->hasPendingDatagrams()) {
        QByteArray packet;
        packet.resize(static_cast<int>(osc_->pendingDatagramSize()));
        osc_->readDatagram(packet.data(), packet.size());
        parseOsc(packet, command_);
    }
}

}  // namespace jad
