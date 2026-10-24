#pragma once
#include <functional>

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>

class QTcpServer;
class QTcpSocket;
class QUdpSocket;

namespace jad {

// Control of the Live Loops grid from a phone or a tablet, in the place of Logic Remote. Two ways in, both off until the owner turns them on:
//  - a web page: any browser on the same network opens http://<this computer>:<port>/?k=<token> and gets the grid with a button per cell and scene (no app to install);
//    the token is part of the address and a request without it is refused;
//  - Open Sound Control over UDP, for the free OSC apps (Open Stage Control, TouchOSC and others): /jad/cell/<row>/<column>, /jad/scene/<n> (1-based, a value above 0 presses,
//    0 lets go), /jad/stopall, /jad/trigger, /jad/performance.
// What the requests mean is decided by the callbacks (ProjectController).
class RemoteServer : public QObject {
    Q_OBJECT
public:
    using StateFn = std::function<QByteArray()>;                                    // the grid as JSON
    using CommandFn = std::function<void(const QString& command, const QStringList& args)>;  // cell [track, scene, 1|0], cellAt [row, column, 1|0], scene [n, 1|0], stopAll, trigger, performance

    RemoteServer(StateFn state, CommandFn command, QObject* parent = nullptr);
    ~RemoteServer() override;

    bool start(quint16 httpPort = 8765, quint16 oscPort = 8000);  // false if the web port could not be opened
    void stop();
    bool running() const { return http_ != nullptr; }
    quint16 httpPort() const;
    quint16 oscPort() const;
    const QString& token() const { return token_; }
    QStringList addresses() const;  // the web addresses to type in the browser of the phone

    static void parseOsc(const QByteArray& packet, const CommandFn& command);  // also used by the tests

private:
    void accept();
    void readFrom(QTcpSocket* socket);
    void answer(QTcpSocket* socket, const QByteArray& request);
    void readOsc();

    StateFn state_;
    CommandFn command_;
    QTcpServer* http_ = nullptr;
    QUdpSocket* osc_ = nullptr;
    QString token_;
    QHash<QTcpSocket*, QByteArray> buffers_;
};

}  // namespace jad
