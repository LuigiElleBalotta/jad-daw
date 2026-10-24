#include "update_logic.h"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringList>
#include <QSysInfo>

namespace jad::update {

Version parseVersion(const QString& text) {
    static const QRegularExpression re(QStringLiteral("^v?(\\d+)\\.(\\d+)\\.(\\d+)(?:-([0-9A-Za-z.-]+))?$"));
    Version v;
    const auto m = re.match(text.trimmed());
    if (!m.hasMatch()) return v;
    v.major = m.captured(1).toInt();
    v.minor = m.captured(2).toInt();
    v.patch = m.captured(3).toInt();
    v.pre = m.captured(4);
    v.valid = true;
    return v;
}

// semver rules for the suffix: dot separated parts, numbers compare as numbers and are older than words, a longer list is newer
static int comparePre(const QString& a, const QString& b) {
    if (a.isEmpty() && b.isEmpty()) return 0;
    if (a.isEmpty()) return 1;   // a release is newer than its pre-releases
    if (b.isEmpty()) return -1;
    const QStringList pa = a.split('.'), pb = b.split('.');
    for (int i = 0; i < qMin(pa.size(), pb.size()); ++i) {
        bool na = false, nb = false;
        const int ia = pa[i].toInt(&na), ib = pb[i].toInt(&nb);
        if (na && nb) { if (ia != ib) return ia < ib ? -1 : 1; }
        else if (na != nb) return na ? -1 : 1;
        else if (const int c = QString::compare(pa[i], pb[i]); c != 0) return c < 0 ? -1 : 1;
    }
    if (pa.size() == pb.size()) return 0;
    return pa.size() < pb.size() ? -1 : 1;
}

int compareVersions(const Version& a, const Version& b) {
    if (a.major != b.major) return a.major < b.major ? -1 : 1;
    if (a.minor != b.minor) return a.minor < b.minor ? -1 : 1;
    if (a.patch != b.patch) return a.patch < b.patch ? -1 : 1;
    return comparePre(a.pre, b.pre);
}

static Release parseOne(const QJsonObject& o) {
    Release r;
    r.tag = o.value("tag_name").toString();
    r.title = o.value("name").toString();
    r.notes = o.value("body").toString();
    r.page = QUrl(o.value("html_url").toString());
    r.prerelease = o.value("prerelease").toBool();
    r.draft = o.value("draft").toBool();
    for (const QJsonValue& av : o.value("assets").toArray()) {
        const QJsonObject ao = av.toObject();
        Asset a;
        a.name = ao.value("name").toString();
        a.url = QUrl(ao.value("browser_download_url").toString());
        a.size = ao.value("size").toInteger();
        const QString digest = ao.value("digest").toString();  // "sha256:<hex>"
        if (digest.startsWith(QLatin1String("sha256:"))) a.sha256 = digest.mid(7).toLower();
        if (!a.name.isEmpty() && a.url.isValid()) r.assets.append(a);
    }
    return r;
}

QList<Release> parseReleases(const QByteArray& json) {
    QList<Release> out;
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (doc.isArray()) {
        for (const QJsonValue& v : doc.array())
            if (v.isObject()) out.append(parseOne(v.toObject()));
    } else if (doc.isObject() && doc.object().contains("tag_name")) {
        out.append(parseOne(doc.object()));
    }
    return out;
}

const Release* newest(const QList<Release>& releases, bool includePrereleases) {
    const Release* best = nullptr;
    Version bestV;
    for (const Release& r : releases) {
        if (r.draft || (r.prerelease && !includePrereleases)) continue;
        const Version v = parseVersion(r.tag);
        if (!v.valid) continue;
        if (!best || compareVersions(v, bestV) > 0) { best = &r; bestV = v; }
    }
    return best;
}

Platform currentPlatform() {
#if defined(Q_OS_WIN)
    return Platform::Windows;
#elif defined(Q_OS_MACOS)
    return QSysInfo::currentCpuArchitecture().contains(QLatin1String("arm")) ? Platform::MacArm : Platform::MacIntel;
#else
    return Platform::Other;
#endif
}

Asset assetFor(const Release& release, Platform platform) {
    for (const Asset& a : release.assets) {
        const QString n = a.name.toLower();
        if (platform == Platform::Windows && n.endsWith(QLatin1String("-setup.exe"))) return a;
        if (platform == Platform::MacArm && n.contains(QLatin1String("macos-arm64")) && n.endsWith(QLatin1String(".zip"))) return a;
        if (platform == Platform::MacIntel && n.contains(QLatin1String("macos-x64")) && n.endsWith(QLatin1String(".zip"))) return a;
    }
    return {};
}

QString hexSha256(const QString& file) {
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash h(QCryptographicHash::Sha256);
    if (!h.addData(&f)) return {};
    return QString::fromLatin1(h.result().toHex());
}

}  // namespace jad::update
