#pragma once
#include <QNetworkAccessManager>
#include <QObject>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include "update_logic.h"

class QFile;
class QNetworkReply;

namespace jad {

// Looks for a newer release on GitHub and installs it over the running copy.
//  - Windows: downloads the setup (*-setup.exe, Inno Setup), starts it silently and quits; the setup replaces the files of the installed copy and starts the program again.
//  - macOS: downloads the app archive, unpacks it, and a small script waits for this process to end, swaps the bundle and opens it.
//  - Anywhere else: the release page is opened.
// The answer of GitHub is checked against the size and the SHA-256 digest of the asset when it gives them.
class Updater : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)  // idle, checking, upToDate, available, downloading, ready, installing, error
    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY releaseChanged)
    Q_PROPERTY(QString notes READ notes NOTIFY releaseChanged)
    Q_PROPERTY(QUrl releaseUrl READ releaseUrl NOTIFY releaseChanged)
    Q_PROPERTY(bool canInstall READ canInstall NOTIFY releaseChanged)  // the release has a file for this platform
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)  // 0..1 while downloading
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(bool autoCheck READ autoCheck WRITE setAutoCheck NOTIFY autoCheckChanged)  // look at start-up, at most once a day
    Q_PROPERTY(bool includePrereleases READ includePrereleases WRITE setIncludePrereleases NOTIFY includePrereleasesChanged)
    Q_PROPERTY(QUrl feedUrl READ feedUrl WRITE setFeedUrl NOTIFY feedUrlChanged)  // the list of releases (tests point it at a local server)
    Q_PROPERTY(bool launchInstaller READ launchInstaller WRITE setLaunchInstaller)  // false: stop at "ready" (tests)
public:
    explicit Updater(QObject* parent = nullptr);

    QString state() const { return state_; }
    QString currentVersion() const;
    QString latestVersion() const { return release_.tag; }
    QString notes() const { return release_.notes; }
    QUrl releaseUrl() const { return release_.page; }
    bool canInstall() const;
    double progress() const { return progress_; }
    QString error() const { return error_; }
    bool autoCheck() const;
    void setAutoCheck(bool on);
    bool includePrereleases() const;
    void setIncludePrereleases(bool on);
    QUrl feedUrl() const { return feed_; }
    void setFeedUrl(const QUrl& url);
    bool launchInstaller() const { return launch_; }
    void setLaunchInstaller(bool on) { launch_ = on; }
    QString downloadedFile() const { return file_; }

    Q_INVOKABLE void check();           // asks GitHub; ends in upToDate, available or error
    Q_INVOKABLE void checkOnStartup();  // check() when autoCheck is on, the last look is a day old and this is not a development build; says nothing when there is nothing new
    Q_INVOKABLE void install();         // downloads the asset of the found release and installs it
    Q_INVOKABLE void skipThisVersion(); // the start-up check stays quiet about this release
    Q_INVOKABLE void openReleasePage() const;
    Q_INVOKABLE void cancel();

signals:
    void stateChanged();
    void releaseChanged();
    void progressChanged();
    void autoCheckChanged();
    void includePrereleasesChanged();
    void feedUrlChanged();
    void updateFound();  // a new version was found by the start-up check: the program shows the dialog

private:
    void setState(const QString& state, const QString& error = {});
    void onFeed(QNetworkReply* reply);
    void onDownloaded(QNetworkReply* reply);
    void finish();
    bool startWindows();
    bool startMac();

    QNetworkAccessManager net_;
    QUrl feed_;
    QString state_ = QStringLiteral("idle");
    QString error_;
    update::Release release_;
    update::Asset asset_;
    double progress_ = 0;
    bool silent_ = false;
    bool launch_ = true;
    bool cancelled_ = false;
    QString file_;
    QNetworkReply* reply_ = nullptr;
    QFile* out_ = nullptr;
};

}  // namespace jad
