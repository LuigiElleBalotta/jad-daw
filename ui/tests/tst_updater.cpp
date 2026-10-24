#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>

#include "bridge/update_logic.h"
#include "bridge/updater.h"

using namespace jad;
using namespace jad::update;

namespace {

// A GitHub in a few lines: the list of releases at /releases and the file at /file.
class FakeGitHub : public QObject {
public:
    FakeGitHub() {
        server.listen(QHostAddress::LocalHost);
        connect(&server, &QTcpServer::newConnection, this, [this] {
            QTcpSocket* s = server.nextPendingConnection();
            connect(s, &QTcpSocket::readyRead, s, [this, s] {
                const QByteArray request = s->readAll();
                const QByteArray path = request.split(' ').value(1);
                QByteArray body, type = "application/json";
                if (path.startsWith("/releases")) body = releases();
                else if (path.startsWith("/file")) { body = payload; type = "application/octet-stream"; }
                const QByteArray head = (body.isEmpty() ? "HTTP/1.1 404 Not Found\r\n" : "HTTP/1.1 200 OK\r\n") + QByteArray("Content-Type: ") + type +
                                        "\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n";
                s->write(head + body);
                s->disconnectFromHost();
            });
        });
    }
    QByteArray releases() const {
        QJsonArray assets;
        QJsonObject a;
        a["name"] = assetName;
        a["browser_download_url"] = QString("http://127.0.0.1:%1/file").arg(server.serverPort());
        a["size"] = payload.size();
        a["digest"] = "sha256:" + digest;
        assets.append(a);
        QJsonObject r;
        r["tag_name"] = tag;
        r["name"] = "JAD Daw " + tag;
        r["body"] = "What is new";
        r["html_url"] = "https://example.invalid/release";
        r["prerelease"] = true;
        r["assets"] = assets;
        return QJsonDocument(QJsonArray{r}).toJson();
    }
    QTcpServer server;
    QByteArray payload = QByteArray(300000, 'x');
    QString digest = QString::fromLatin1(QCryptographicHash::hash(QByteArray(300000, 'x'), QCryptographicHash::Sha256).toHex());
    QString tag = "0.0.9";
    QString assetName;
};

// JSON written with apostrophes for the quotes
QByteArray json(const char* text) { return QByteArray(text).replace('\'', '"'); }

QString nameForThisSystem() {
    switch (currentPlatform()) {
    case Platform::Windows: return "jad-daw-0.0.9-windows-x64-setup.exe";
    case Platform::MacArm: return "jad-daw-0.0.9-macos-arm64.zip";
    case Platform::MacIntel: return "jad-daw-0.0.9-macos-x64.zip";
    default: return {};
    }
}

}  // namespace

class UpdaterTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName("JAD-updater-test");
        QCoreApplication::setApplicationName("JAD Daw updater test");
        QCoreApplication::setApplicationVersion("0.0.1");
        QSettings().clear();
    }
    void init() { QSettings().clear(); }

    void versionsAreOrdered() {
        QVERIFY(compareVersions(parseVersion("0.0.2"), parseVersion("0.0.10")) < 0);
        QVERIFY(compareVersions(parseVersion("v1.2.0"), parseVersion("1.1.9")) > 0);
        QVERIFY(compareVersions(parseVersion("1.0.0-1"), parseVersion("1.0.0")) < 0);
        QVERIFY(compareVersions(parseVersion("1.0.0-2"), parseVersion("1.0.0-10")) < 0);
        QVERIFY(compareVersions(parseVersion("1.0.0-beta"), parseVersion("1.0.0-1")) > 0);
        QCOMPARE(compareVersions(parseVersion("0.0.2"), parseVersion("0.0.2")), 0);
        QVERIFY(!parseVersion("latest").valid);
        QVERIFY(!parseVersion("0.0.0-dev").pre.isEmpty());
    }

    void newestSkipsDraftsAndMaybePrereleases() {
        const QByteArray feed = json(
            "[{'tag_name':'1.1.0','draft':true,'prerelease':false},"
            "{'tag_name':'1.0.1','prerelease':true},"
            "{'tag_name':'1.0.0','prerelease':false},"
            "{'tag_name':'nonsense','prerelease':false}]");
        const auto all = parseReleases(feed);
        QCOMPARE(all.size(), 4);
        QCOMPARE(newest(all, true)->tag, QString("1.0.1"));
        QCOMPARE(newest(all, false)->tag, QString("1.0.0"));
        QVERIFY(!newest({}, true));
        QCOMPARE(parseReleases(json("{'tag_name':'2.0.0'}")).size(), 1);  // /releases/latest answers with one object
    }

    void theAssetOfThePlatformIsPicked() {
        const auto r = parseReleases(json(
            "[{'tag_name':'0.0.9','assets':["
            "{'name':'jad-daw-0.0.9-windows-x64.zip','browser_download_url':'https://x/a.zip','size':1},"
            "{'name':'jad-daw-0.0.9-windows-x64-setup.exe','browser_download_url':'https://x/b.exe','size':2,'digest':'sha256:ABCD'},"
            "{'name':'jad-daw-0.0.9-macos-arm64.dmg','browser_download_url':'https://x/c.dmg','size':3},"
            "{'name':'jad-daw-0.0.9-macos-arm64.zip','browser_download_url':'https://x/d.zip','size':4}]}]"));
        QCOMPARE(assetFor(r[0], Platform::Windows).name, QString("jad-daw-0.0.9-windows-x64-setup.exe"));
        QCOMPARE(assetFor(r[0], Platform::Windows).sha256, QString("abcd"));
        QCOMPARE(assetFor(r[0], Platform::MacArm).name, QString("jad-daw-0.0.9-macos-arm64.zip"));
        QVERIFY(assetFor(r[0], Platform::MacIntel).name.isEmpty());
        QVERIFY(assetFor(r[0], Platform::Other).name.isEmpty());
    }

    void aNewerReleaseIsFoundAndDownloaded() {
        FakeGitHub gh;
        gh.assetName = nameForThisSystem();
        if (gh.assetName.isEmpty()) QSKIP("no installer is built for this system");
        Updater u;
        u.setFeedUrl(QString("http://127.0.0.1:%1/releases").arg(gh.server.serverPort()));
        u.setLaunchInstaller(false);
        QSignalSpy states(&u, &Updater::stateChanged);
        u.check();
        QTRY_COMPARE(u.state(), QString("available"));
        QCOMPARE(u.latestVersion(), QString("0.0.9"));
        QCOMPARE(u.notes(), QString("What is new"));
        QVERIFY(u.canInstall());

        u.install();
        QTRY_COMPARE_WITH_TIMEOUT(u.state(), QString("ready"), 15000);
        QCOMPARE(u.progress(), 1.0);
        QFile f(u.downloadedFile());
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), gh.payload);
        QFile::remove(u.downloadedFile());
    }

    void aDownloadThatDoesNotMatchItsChecksumIsRefused() {
        FakeGitHub gh;
        gh.assetName = nameForThisSystem();
        if (gh.assetName.isEmpty()) QSKIP("no installer is built for this system");
        gh.digest = QString(64, '0');
        Updater u;
        u.setFeedUrl(QString("http://127.0.0.1:%1/releases").arg(gh.server.serverPort()));
        u.setLaunchInstaller(false);
        u.check();
        QTRY_COMPARE(u.state(), QString("available"));
        u.install();
        QTRY_COMPARE_WITH_TIMEOUT(u.state(), QString("error"), 15000);
        QVERIFY(u.error().contains("checksum"));
        QVERIFY(!QFile::exists(u.downloadedFile()));
    }

    void theSameOrAnOlderReleaseIsUpToDate() {
        FakeGitHub gh;
        gh.assetName = nameForThisSystem();
        gh.tag = "0.0.1";
        Updater u;
        u.setFeedUrl(QString("http://127.0.0.1:%1/releases").arg(gh.server.serverPort()));
        u.check();
        QTRY_COMPARE(u.state(), QString("upToDate"));
    }

    void anUnreachableGitHubIsAnErrorForTheMenuAndSilentAtStart() {
        Updater u;
        u.setFeedUrl(QString("http://127.0.0.1:1/releases"));  // nothing listens there
        u.check();
        QTRY_COMPARE(u.state(), QString("error"));
        QVERIFY(!u.error().isEmpty());

        QSettings().clear();
        Updater v;
        v.setFeedUrl(QString("http://127.0.0.1:1/releases"));
        v.checkOnStartup();
        QTRY_COMPARE_WITH_TIMEOUT(v.state(), QString("idle"), 5000);
    }

    void theStartUpCheckKeepsToItsRules() {
        FakeGitHub gh;
        gh.assetName = nameForThisSystem();
        {
            Updater u;
            u.setFeedUrl(QString("http://127.0.0.1:%1/releases").arg(gh.server.serverPort()));
            QSignalSpy found(&u, &Updater::updateFound);
            u.checkOnStartup();
            QTRY_COMPARE(u.state(), QString("available"));
            QCOMPARE(found.count(), 1);
            u.skipThisVersion();
        }
        {   // looked a minute ago: not again for a day
            Updater u;
            u.setFeedUrl(QString("http://127.0.0.1:%1/releases").arg(gh.server.serverPort()));
            u.checkOnStartup();
            QCOMPARE(u.state(), QString("idle"));
        }
        QSettings().remove("update/lastCheck");
        {   // the skipped version stays quiet
            Updater u;
            u.setFeedUrl(QString("http://127.0.0.1:%1/releases").arg(gh.server.serverPort()));
            QSignalSpy found(&u, &Updater::updateFound);
            u.checkOnStartup();
            QTRY_COMPARE_WITH_TIMEOUT(u.latestVersion(), QString("0.0.9"), 5000);
            QTRY_COMPARE(u.state(), QString("idle"));
            QCOMPARE(found.count(), 0);
        }
        QSettings().remove("update/lastCheck");
        {   // switched off
            Updater u;
            u.setAutoCheck(false);
            u.checkOnStartup();
            QCOMPARE(u.state(), QString("idle"));
        }
    }
};

QTEST_MAIN(UpdaterTests)
#include "tst_updater.moc"
