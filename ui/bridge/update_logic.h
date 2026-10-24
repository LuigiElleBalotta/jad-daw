#pragma once
#include <QByteArray>
#include <QList>
#include <QString>
#include <QUrl>

namespace jad::update {

// A version as the release tags write it: 1.2.3 or 1.2.3-N / 0.0.2-beta (a suffix makes it older than the same number without one).
struct Version {
    int major = 0, minor = 0, patch = 0;
    QString pre;
    bool valid = false;
};

Version parseVersion(const QString& text);  // a leading "v" is allowed
int compareVersions(const Version& a, const Version& b);  // < 0 older, 0 same, > 0 newer

struct Asset {
    QString name;
    QUrl url;
    qint64 size = 0;
    QString sha256;  // lower case hex, empty when GitHub gave no digest
};

struct Release {
    QString tag;
    QString title;
    QString notes;
    QUrl page;
    bool prerelease = false;
    bool draft = false;
    QList<Asset> assets;
};

// The answer of GET /repos/<owner>/<repo>/releases (an array) or of /releases/latest (one object).
QList<Release> parseReleases(const QByteArray& json);

// The newest release that can be offered: no drafts, and no pre-releases unless asked (the versions below 1.0 are all pre-releases).
const Release* newest(const QList<Release>& releases, bool includePrereleases);

enum class Platform { Windows, MacArm, MacIntel, Other };
Platform currentPlatform();

// The file of a release that installs on this platform: the Windows setup (*-setup.exe), the macOS app archive (*-macos-arm64.zip). A null asset when there is none.
Asset assetFor(const Release& release, Platform platform);

QString hexSha256(const QString& file);  // empty if the file cannot be read

}  // namespace jad::update
