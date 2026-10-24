#include "updater.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>

namespace jad {

namespace {
constexpr const char* kFeed = "https://api.github.com/repos/LuigiElleBalotta/jad-daw/releases?per_page=15";
}

Updater::Updater(QObject* parent) : QObject(parent) {
    const QByteArray env = qgetenv("JAD_UPDATE_FEED");
    feed_ = QUrl(env.isEmpty() ? QString::fromLatin1(kFeed) : QString::fromLocal8Bit(env));
}

QString Updater::currentVersion() const {
    const QString v = QCoreApplication::applicationVersion();
    return v.isEmpty() ? QStringLiteral("0.0.0-dev") : v;
}

bool Updater::canInstall() const {
    return !update::assetFor(release_, update::currentPlatform()).name.isEmpty();
}

bool Updater::autoCheck() const { return QSettings().value("update/auto", true).toBool(); }

void Updater::setAutoCheck(bool on) {
    if (on == autoCheck()) return;
    QSettings().setValue("update/auto", on);
    emit autoCheckChanged();
}

bool Updater::includePrereleases() const {
    // the versions below 1.0 are all pre-releases on GitHub, so they are offered until the owner says otherwise
    return QSettings().value("update/prereleases", update::parseVersion(currentVersion()).major < 1).toBool();
}

void Updater::setIncludePrereleases(bool on) {
    if (on == includePrereleases()) return;
    QSettings().setValue("update/prereleases", on);
    emit includePrereleasesChanged();
}

void Updater::setFeedUrl(const QUrl& url) {
    if (url == feed_) return;
    feed_ = url;
    emit feedUrlChanged();
}

void Updater::setState(const QString& state, const QString& error) {
    state_ = state;
    error_ = error;
    emit stateChanged();
}

void Updater::checkOnStartup() {
    if (!autoCheck() || currentVersion().contains(QLatin1String("dev"))) return;
    QSettings s;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if (now - s.value("update/lastCheck", 0).toLongLong() < 24 * 3600) return;
    silent_ = true;
    check();
}

void Updater::check() {
    if (state_ == QLatin1String("checking") || state_ == QLatin1String("downloading") || state_ == QLatin1String("installing")) return;
    setState(QStringLiteral("checking"));
    QNetworkRequest req(feed_);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("JAD-Daw/%1").arg(currentVersion()));
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    req.setTransferTimeout(20000);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = net_.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] { onFeed(reply); });
}

void Updater::onFeed(QNetworkReply* reply) {
    reply->deleteLater();
    const bool silent = silent_;
    silent_ = false;
    if (reply->error() != QNetworkReply::NoError) {
        if (silent) setState(QStringLiteral("idle"));  // the start-up look says nothing when it cannot reach GitHub
        else setState(QStringLiteral("error"), tr("Could not reach GitHub: %1").arg(reply->errorString()));
        return;
    }
    QSettings().setValue("update/lastCheck", QDateTime::currentSecsSinceEpoch());
    const auto releases = update::parseReleases(reply->readAll());
    const update::Release* best = update::newest(releases, includePrereleases());
    const update::Version current = update::parseVersion(currentVersion());
    if (!best || (current.valid && update::compareVersions(update::parseVersion(best->tag), current) <= 0)) {
        release_ = best ? *best : update::Release();
        asset_ = {};
        emit releaseChanged();
        setState(silent ? QStringLiteral("idle") : QStringLiteral("upToDate"));
        return;
    }
    release_ = *best;
    asset_ = update::assetFor(release_, update::currentPlatform());
    emit releaseChanged();
    if (silent && QSettings().value("update/skipped").toString() == release_.tag) {
        setState(QStringLiteral("idle"));
        return;
    }
    setState(QStringLiteral("available"));
    if (silent) emit updateFound();
}

void Updater::skipThisVersion() {
    QSettings().setValue("update/skipped", release_.tag);
    setState(QStringLiteral("idle"));
}

void Updater::openReleasePage() const {
    if (release_.page.isValid()) QDesktopServices::openUrl(release_.page);
}

void Updater::install() {
    if (state_ != QLatin1String("available") && state_ != QLatin1String("error")) return;
    if (asset_.name.isEmpty()) {
        openReleasePage();  // nothing built for this system: the page lists what there is
        return;
    }
    QDir dir(QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/jad-daw-update");
    dir.removeRecursively();
    QDir().mkpath(dir.path());
    file_ = dir.filePath(asset_.name);
    out_ = new QFile(file_, this);
    if (!out_->open(QIODevice::WriteOnly)) {
        const QString why = out_->errorString();
        delete out_;
        out_ = nullptr;
        setState(QStringLiteral("error"), tr("Cannot write %1: %2").arg(file_, why));
        return;
    }
    progress_ = 0;
    emit progressChanged();
    setState(QStringLiteral("downloading"));
    QNetworkRequest req(asset_.url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("JAD-Daw/%1").arg(currentVersion()));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(60000);
    reply_ = net_.get(req);
    connect(reply_, &QNetworkReply::readyRead, this, [this] {
        if (out_ && reply_) out_->write(reply_->readAll());
    });
    connect(reply_, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64 total) {
        const qint64 all = total > 0 ? total : asset_.size;
        progress_ = all > 0 ? double(got) / double(all) : 0;
        emit progressChanged();
    });
    connect(reply_, &QNetworkReply::finished, this, [this] { onDownloaded(reply_); });
}

void Updater::cancel() {
    cancelled_ = true;
    if (reply_) reply_->abort();
}

void Updater::onDownloaded(QNetworkReply* reply) {
    reply->deleteLater();
    reply_ = nullptr;
    const bool aborted = cancelled_;
    cancelled_ = false;
    if (out_) {
        out_->write(reply->readAll());
        out_->close();
        out_->deleteLater();
        out_ = nullptr;
    }
    if (reply->error() != QNetworkReply::NoError) {
        QFile::remove(file_);
        if (aborted) setState(QStringLiteral("available"));
        else setState(QStringLiteral("error"), tr("The download failed: %1").arg(reply->errorString()));
        return;
    }
    const qint64 size = QFileInfo(file_).size();
    if (asset_.size > 0 && size != asset_.size) {
        QFile::remove(file_);
        setState(QStringLiteral("error"), tr("The downloaded file is %1 bytes, expected %2.").arg(size).arg(asset_.size));
        return;
    }
    if (!asset_.sha256.isEmpty() && update::hexSha256(file_) != asset_.sha256) {
        QFile::remove(file_);
        setState(QStringLiteral("error"), tr("The downloaded file does not match its checksum."));
        return;
    }
    progress_ = 1;
    emit progressChanged();
    setState(QStringLiteral("ready"));
    if (launch_) finish();
}

void Updater::finish() {
    setState(QStringLiteral("installing"));
    const auto platform = update::currentPlatform();
    const bool started = platform == update::Platform::Windows ? startWindows() : startMac();
    if (!started) return;  // the state says why
    QMetaObject::invokeMethod(QCoreApplication::instance(), &QCoreApplication::quit, Qt::QueuedConnection);
}

bool Updater::startWindows() {
    // The setup closes this program, replaces the files of the installed copy and starts the program again (/RELAUNCH=1 is read by the script).
    const QStringList args{"/SILENT", "/SUPPRESSMSGBOXES", "/NOCANCEL", "/CLOSEAPPLICATIONS", "/RELAUNCH=1"};
    if (!QProcess::startDetached(file_, args)) {
        setState(QStringLiteral("error"), tr("Could not start the installer %1.").arg(file_));
        return false;
    }
    return true;
}

bool Updater::startMac() {
    QDir bundle(QCoreApplication::applicationDirPath());  // <name>.app/Contents/MacOS
    bundle.cdUp();
    bundle.cdUp();
    const QString target = bundle.absolutePath();
    if (!target.endsWith(QLatin1String(".app"))) {
        setState(QStringLiteral("error"), tr("This copy does not run from an application bundle; download the new version from the release page."));
        return false;
    }
    if (!QFileInfo(QFileInfo(target).absolutePath()).isWritable()) {
        setState(QStringLiteral("error"), tr("Cannot change %1: no permission. Download the new version from the release page.").arg(target));
        return false;
    }
    const QString work = QFileInfo(file_).absolutePath() + "/unpacked";
    QDir(work).removeRecursively();
    QDir().mkpath(work);
    QProcess unzip;
    unzip.start("/usr/bin/ditto", {"-x", "-k", file_, work});
    if (!unzip.waitForFinished(120000) || unzip.exitCode() != 0) {
        setState(QStringLiteral("error"), tr("Could not unpack the download."));
        return false;
    }
    const QStringList apps = QDir(work).entryList({"*.app"}, QDir::Dirs);
    if (apps.isEmpty()) {
        setState(QStringLiteral("error"), tr("The download holds no application."));
        return false;
    }
    const QString script = work + "/swap.sh";
    QFile f(script);
    if (!f.open(QIODevice::WriteOnly)) {
        setState(QStringLiteral("error"), tr("Cannot write %1.").arg(script));
        return false;
    }
    f.write(
        "#!/bin/sh\n"
        "PID=\"$1\"; NEW=\"$2\"; TARGET=\"$3\"\n"
        "while kill -0 \"$PID\" 2>/dev/null; do sleep 0.3; done\n"
        "rm -rf \"$TARGET.old\"\n"
        "if mv \"$TARGET\" \"$TARGET.old\" && ditto \"$NEW\" \"$TARGET\"; then\n"
        "  xattr -dr com.apple.quarantine \"$TARGET\" 2>/dev/null\n"
        "  rm -rf \"$TARGET.old\"\n"
        "else\n"
        "  rm -rf \"$TARGET\"; mv \"$TARGET.old\" \"$TARGET\"\n"
        "fi\n"
        "open \"$TARGET\"\n");
    f.close();
    f.setPermissions(f.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeUser);
    if (!QProcess::startDetached("/bin/sh", {script, QString::number(QCoreApplication::applicationPid()), work + "/" + apps.first(), target})) {
        setState(QStringLiteral("error"), tr("Could not start the update."));
        return false;
    }
    return true;
}

}  // namespace jad
