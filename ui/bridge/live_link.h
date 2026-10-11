#pragma once
#include <functional>

#include <QHash>
#include <QObject>
#include <QString>
#include <nlohmann/json.hpp>

class QTcpServer;
class QTcpSocket;

namespace jad {

// The running app listens on the loopback interface so an AI assistant (the MCP server) can look at and drive the open project.
// One JSON object per line in each direction: {"id": 1, "token": "...", "op": "state", ...} -> {"id": 1, "ok": true, ...} or {"id": 1, "ok": false, "error": "..."}.
// The port and the token are written to `<app data>/live-link.json` while the link is on; nothing but this machine can connect, and a request without the
// token is refused. What the requests mean is decided by the handler (ProjectController::liveHandle).
class LiveLink : public QObject {
    Q_OBJECT
public:
    using Reply = std::function<void(const nlohmann::json&)>;
    using Handler = std::function<void(const nlohmann::json& request, Reply reply)>;

    explicit LiveLink(Handler handler, QObject* parent = nullptr);
    ~LiveLink() override;

    bool start();  // false if no port could be opened
    void stop();
    bool running() const { return server_ != nullptr; }
    quint16 port() const;
    const QString& token() const { return token_; }

    static QString infoFilePath();  // where the port and the token are written

private:
    void accept();
    void readFrom(QTcpSocket* socket);
    void handleLine(QTcpSocket* socket, const QByteArray& line);

    Handler handler_;
    QTcpServer* server_ = nullptr;
    QString token_;
    QHash<QTcpSocket*, QByteArray> buffers_;
};

}  // namespace jad
