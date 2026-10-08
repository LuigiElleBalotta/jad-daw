#include "actions/action_registry.h"

#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace jad {

namespace {

QString readFile(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

}  // namespace

ActionRegistry::ActionRegistry(QObject* parent) : QObject(parent) {
    // a missing user file is normal and not a problem
    load(readFile(":/qt/qml/Jad/actions/actions.json"),
         readFile(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/shortcuts.json"));
}

QList<ActionDef> ActionRegistry::parseTable(const QString& json, QStringList* problems) {
    QList<ActionDef> out;
    const auto report = [&](const QString& message) {
        if (problems) problems->append(message);
    };
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &error);
    if (doc.isNull() || !doc.isArray()) {
        report("actions table: " + (doc.isNull() ? error.errorString() : QStringLiteral("the top level must be an array")));
        return out;
    }
    QSet<QString> seen;
    int n = 0;
    for (const QJsonValue& v : doc.array()) {
        ++n;
        const QString at = QStringLiteral("entry %1: ").arg(n);
        if (!v.isObject()) {
            report(at + "not an object");
            continue;
        }
        const QJsonObject o = v.toObject();
        ActionDef d;
        d.id = o.value("id").toString();
        d.label = o.value("label").toString();
        d.menu = o.value("menu").toString();
        d.shortcut = o.value("shortcut").toString();
        d.kind = o.value("kind").toString();
        d.status = o.value("status").toString();
        d.group = o.value("group").toString();
        d.verified = o.value("verified").toBool(false);
        if (d.id.isEmpty()) {
            report(at + "missing id");
            continue;
        }
        if (d.label.isEmpty()) {
            report(at + d.id + ": missing label");
            continue;
        }
        if (d.kind != "command" && d.kind != "toggle") {
            report(at + d.id + ": unknown kind '" + d.kind + "'");
            continue;
        }
        if (d.status != "ready" && d.status != "stub") {
            report(at + d.id + ": unknown status '" + d.status + "'");
            continue;
        }
        if (seen.contains(d.id)) {
            report(at + d.id + ": repeated id");
            continue;
        }
        seen.insert(d.id);
        out.append(d);
    }
    return out;
}

void ActionRegistry::load(const QString& tableJson, const QString& userJson) {
    problems_.clear();
    handled_.clear();
    defs_ = parseTable(tableJson, &problems_);
    index_.clear();
    QJsonObject defaults;
    for (int i = 0; i < defs_.size(); ++i) {
        index_.insert(defs_[i].id, i);
        if (!defs_[i].shortcut.isEmpty()) defaults.insert(defs_[i].id, defs_[i].shortcut);
    }
    QStringList shortcutProblems;
    shortcuts_.reset(new ShortcutMap(ShortcutMap::fromFiles(QString::fromUtf8(QJsonDocument(defaults).toJson()), userJson, &shortcutProblems)));
    // ids that the user file names but the table does not know are reported by ShortcutMap; keep every message
    problems_ += shortcutProblems;
    for (const QString& p : std::as_const(problems_)) qWarning().noquote() << "actions:" << p;
}

const ActionDef* ActionRegistry::find(const QString& id) const {
    const auto it = index_.constFind(id);
    return it == index_.constEnd() ? nullptr : &defs_[it.value()];
}

QString ActionRegistry::label(const QString& id) const { return find(id) ? find(id)->label : QString(); }
QString ActionRegistry::shortcut(const QString& id) const { return shortcuts_ ? shortcuts_->sequence(id) : QString(); }
QString ActionRegistry::menu(const QString& id) const { return find(id) ? find(id)->menu : QString(); }
QString ActionRegistry::group(const QString& id) const { return find(id) ? find(id)->group : QString(); }
bool ActionRegistry::isStub(const QString& id) const { return find(id) && find(id)->isStub(); }
bool ActionRegistry::isToggle(const QString& id) const { return find(id) && find(id)->isToggle(); }
bool ActionRegistry::isRadio(const QString& id) const { return find(id) && !find(id)->group.isEmpty(); }

QStringList ActionRegistry::ids() const {
    QStringList out;
    for (const ActionDef& d : defs_) out << d.id;
    return out;
}

QStringList ActionRegistry::readyIds() const {
    QStringList out;
    for (const ActionDef& d : defs_)
        if (!d.isStub()) out << d.id;
    return out;
}

QStringList ActionRegistry::topMenus() const {
    QStringList out;
    for (const ActionDef& d : defs_) {
        const QString top = d.menu.section('/', 0, 0);
        if (!top.isEmpty() && !out.contains(top)) out << top;
    }
    return out;
}

QVariantList ActionRegistry::entries(const QString& topMenu) const {
    QVariantList out;
    for (const ActionDef& d : defs_) {
        if (d.menu.section('/', 0, 0) != topMenu) continue;
        QVariantMap m;
        m["id"] = d.id;
        m["label"] = d.label;
        m["path"] = d.menu.section('/', 1);
        out.append(m);
    }
    return out;
}

bool ActionRegistry::stubTriggered(const QString& id, bool checked) {
    const ActionDef* d = find(id);
    if (!d || !d->isStub()) return false;
    if (d->isToggle() && !checked) return false;  // switching off is silent
    emit notImplemented(d->label);
    return true;
}

}  // namespace jad
