#include <QFile>
#include <QSet>
#include <QSignalSpy>
#include <QtTest>

#include "actions/action_registry.h"

class ActionsTest : public QObject {
    Q_OBJECT
    static QString shipped() {
        QFile f(QStringLiteral(JAD_ACTIONS_JSON));
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
    }
private slots:
    void theShippedTableIsValid() {
        QStringList problems;
        const auto defs = jad::ActionRegistry::parseTable(shipped(), &problems);
        QVERIFY2(problems.isEmpty(), qPrintable(problems.join("; ")));
        QVERIFY(defs.size() > 60);
        QSet<QString> ids, sequences;
        const QSet<QString> tops = {"File", "Edit", "Track", "Navigate", "Record", "Mix", "View", "Window", "Help"};
        for (const auto& d : defs) {
            QVERIFY2(!d.label.isEmpty(), qPrintable(d.id));
            QVERIFY2(!ids.contains(d.id), qPrintable("duplicate id " + d.id));
            ids.insert(d.id);
            if (!d.menu.isEmpty()) QVERIFY2(tops.contains(d.menu.section('/', 0, 0)), qPrintable(d.id));
            if (!d.shortcut.isEmpty()) {
                QVERIFY2(!sequences.contains(d.shortcut), qPrintable("shortcut used twice: " + d.shortcut));
                sequences.insert(d.shortcut);
                QVERIFY2(!QKeySequence(d.shortcut, QKeySequence::PortableText).isEmpty(), qPrintable("bad sequence for " + d.id));
            }
        }
        QVERIFY(ids.contains("transport.playStop"));
        QVERIFY(ids.contains("edit.undo"));
    }
    void badTablesAreReportedNotCrashed() {
        QStringList problems;
        QVERIFY(jad::ActionRegistry::parseTable("{ nope", &problems).isEmpty());
        QVERIFY(!problems.isEmpty());
        problems.clear();
        const auto defs = jad::ActionRegistry::parseTable(R"([{"id":"a","label":"A","menu":"File","shortcut":"","kind":"command","status":"ready"},
            {"id":"a","label":"Dup","menu":"File","shortcut":"","kind":"command","status":"ready"},
            {"label":"no id"},
            {"id":"b","label":"B","menu":"File","shortcut":"","kind":"weird","status":"ready"}])", &problems);
        QCOMPARE(defs.size(), 1);
        QCOMPARE(problems.size(), 3);
    }
    void stubToggleAnnouncesOnlyWhenSwitchedOn() {
        jad::ActionRegistry reg;
        reg.loadForTest(R"([{"id":"t","label":"Metronome","menu":"Record","shortcut":"","kind":"toggle","status":"stub"},
                            {"id":"c","label":"Bounce","menu":"File","shortcut":"","kind":"command","status":"stub"},
                            {"id":"r","label":"Play","menu":"File","shortcut":"","kind":"command","status":"ready"}])", "{}");
        QSignalSpy spy(&reg, &jad::ActionRegistry::notImplemented);
        QVERIFY(reg.stubTriggered("t", true));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toString(), QStringLiteral("Metronome"));
        QVERIFY(!reg.stubTriggered("t", false));  // switching off: silence
        QCOMPARE(spy.count(), 0);
        QVERIFY(reg.stubTriggered("c", false));  // a stub command announces every time
        QVERIFY(reg.stubTriggered("c", false));
        QCOMPARE(spy.count(), 2);
        QVERIFY(!reg.stubTriggered("r", true));    // ready actions never announce
        QVERIFY(!reg.stubTriggered("nope", true));  // unknown ids neither
    }
    void userShortcutsOverrideTheTable() {
        jad::ActionRegistry reg;
        reg.loadForTest(R"([{"id":"x","label":"X","menu":"File","shortcut":"Ctrl+X","kind":"command","status":"ready"},
                            {"id":"y","label":"Y","menu":"File","shortcut":"Ctrl+Y","kind":"command","status":"ready"}])",
                        R"({"x":"Ctrl+K","y":"Ctrl+K","zzz":"A"})");
        QCOMPARE(reg.shortcut("x"), QStringLiteral("Ctrl+K"));
        QCOMPARE(reg.shortcut("y"), QStringLiteral("Ctrl+Y"));  // the clash is refused
        QCOMPARE(reg.problems().size(), 2);                     // the clash and the unknown id
    }
    void entriesFollowTableOrderAndSubmenus() {
        jad::ActionRegistry reg;
        reg.loadForTest(R"([{"id":"a","label":"A","menu":"Track","shortcut":"","kind":"command","status":"ready"},
                            {"id":"b","label":"Purple","menu":"Track/Color","shortcut":"","kind":"command","status":"ready"},
                            {"id":"c","label":"C","menu":"Track","shortcut":"","kind":"command","status":"ready"}])", "{}");
        const QVariantList e = reg.entries("Track");
        QCOMPARE(e.size(), 3);
        QCOMPARE(e[0].toMap().value("id").toString(), QStringLiteral("a"));
        QCOMPARE(e[1].toMap().value("path").toString(), QStringLiteral("Color"));
        QCOMPARE(e[2].toMap().value("id").toString(), QStringLiteral("c"));
        QCOMPARE(reg.topMenus(), (QStringList{"Track"}));
    }
};

QTEST_MAIN(ActionsTest)
#include "tst_actions.moc"
