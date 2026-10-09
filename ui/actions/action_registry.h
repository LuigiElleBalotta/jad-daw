#pragma once
#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <memory>

#include "shortcuts/shortcut_map.h"

namespace jad {

struct ActionDef {
    QString id, label, menu, shortcut, kind, status, group;
    bool verified = false;
    bool sep = false;     // a separator is drawn before this entry
    bool subsep = false;  // the entry opens a submenu (its path): the separator goes before the submenu in the parent menu
    bool isToggle() const { return kind == "toggle"; }
    bool isStub() const { return status == "stub"; }
};

// The single table of actions: menus, buttons, shortcuts and the "not implemented" notice all come from here.
class ActionRegistry : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QStringList problems READ problems CONSTANT)
public:
    explicit ActionRegistry(QObject* parent = nullptr);

    // Parses the table; entries with a missing field, an unknown kind or status or a repeated id are reported and skipped.
    static QList<ActionDef> parseTable(const QString& json, QStringList* problems);

    // For tests: replaces the table and the user overrides.
    void loadForTest(const QString& tableJson, const QString& userJson) { load(tableJson, userJson); }

    Q_INVOKABLE QString label(const QString& id) const;
    Q_INVOKABLE QString shortcut(const QString& id) const;  // the user's override when valid, else the table's
    Q_INVOKABLE QString menu(const QString& id) const;
    Q_INVOKABLE QString group(const QString& id) const;
    Q_INVOKABLE bool isStub(const QString& id) const;
    Q_INVOKABLE bool isToggle(const QString& id) const;
    Q_INVOKABLE bool isRadio(const QString& id) const;
    Q_INVOKABLE QStringList ids() const;
    Q_INVOKABLE QStringList topMenus() const;
    Q_INVOKABLE QVariantList entries(const QString& topMenu) const;  // [{id, label, path}] in table order
    // True when a notice was emitted: a stub toggle switched on, or any stub command.
    Q_INVOKABLE bool stubTriggered(const QString& id, bool checked);
    Q_INVOKABLE void noteHandler(const QString& id) { handled_.insert(id); }
    QStringList handledIds() const { return QStringList(handled_.begin(), handled_.end()); }
    QStringList readyIds() const;
    QStringList problems() const { return problems_; }

signals:
    void notImplemented(const QString& label);

private:
    void load(const QString& tableJson, const QString& userJson);
    const ActionDef* find(const QString& id) const;

    QList<ActionDef> defs_;
    QHash<QString, int> index_;
    QSet<QString> handled_;
    QStringList problems_;
    std::unique_ptr<ShortcutMap> shortcuts_;
};

}  // namespace jad
