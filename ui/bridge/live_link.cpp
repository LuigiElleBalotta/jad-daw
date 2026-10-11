#include "live_link.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QPointer>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>

namespace jad {

namespace {
constexpr int kMaxLine = 8 * 1024 * 1024;  // a request is never this big

QString newToken() {
    QByteArray bytes(24, 0);
    for (char& c : bytes) c = static_cast<char>(QRandomGenerator::system()->bounded(256));
    return QString::fromLatin1(bytes.toHex());
}
}  // namespace

LiveLink::LiveLink(Handler handler, QObject* parent) : QObject(parent), handler_(std::move(handler)) {}

LiveLink::~LiveLink() { stop(); }

QString LiveLink::infoFilePath() {
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/live-link.json");
}

quint16 LiveLink::port() const { return server_ ? server_->serverPort() : 0; }

bool LiveLink::start() {
    if (server_) return true;
    auto* server = new QTcpServer(this);
    if (!server->listen(QHostAddress::LocalHost, 0)) {
        delete server;
        return false;
    }
    server_ = server;
    token_ = newToken();
    connect(server_, &QTcpServer::newConnection, this, &LiveLink::accept);
    const QString path = infoFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    nlohmann::json info = {{"port", static_cast<int>(server_->serverPort())}, {"token", token_.toStdString()}, {"pid", QCoreApplication::applicationPid()}};
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QByteArray::fromStdString(info.dump(1)));
        file.commit();
    }
    return true;
}

void LiveLink::stop() {
    if (!server_) return;
    for (QTcpSocket* s : buffers_.keys()) {
        s->disconnect(this);
        s->close();
        s->deleteLater();
    }
    buffers_.clear();
    server_->close();
    server_->deleteLater();
    server_ = nullptr;
    QFile::remove(infoFilePath());
}

void LiveLink::accept() {
    while (server_ && server_->hasPendingConnections()) {
        QTcpSocket* socket = server_->nextPendingConnection();
        buffers_.insert(socket, {});
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { readFrom(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
            buffers_.remove(socket);
            socket->deleteLater();
        });
    }
}

void LiveLink::readFrom(QTcpSocket* socket) {
    QByteArray& buffer = buffers_[socket];
    buffer += socket->readAll();
    if (buffer.size() > kMaxLine && !buffer.contains('\n')) {
        socket->close();
        return;
    }
    int newline;
    while ((newline = buffer.indexOf('\n')) >= 0) {
        const QByteArray line = buffer.left(newline).trimmed();
        buffer.remove(0, newline + 1);
        if (!line.isEmpty()) handleLine(socket, line);
        if (!buffers_.contains(socket)) return;
    }
}

void LiveLink::handleLine(QTcpSocket* socket, const QByteArray& line) {
    QPointer<QTcpSocket> target(socket);
    auto send = [target](nlohmann::json response) {
        if (!target || target->state() != QAbstractSocket::ConnectedState) return;
        target->write(QByteArray::fromStdString(response.dump() + "\n"));
    };
    nlohmann::json request = nlohmann::json::parse(line.toStdString(), nullptr, false);
    if (request.is_discarded() || !request.is_object()) {
        send({{"ok", false}, {"error", "the request is not a JSON object"}});
        return;
    }
    const nlohmann::json id = request.value("id", nlohmann::json());
    auto respond = [send, id](nlohmann::json body) {
        if (!id.is_null()) body["id"] = id;
        send(std::move(body));
    };
    if (request.value("token", std::string()) != token_.toStdString()) {
        respond({{"ok", false}, {"error", "bad token"}});
        return;
    }
    handler_(request, [respond](const nlohmann::json& body) { respond(body); });
}

}  // namespace jad
