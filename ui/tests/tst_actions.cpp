#include <QFile>
#include <QTemporaryDir>
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
    void keysCanBeChangedFreedAndResetWithTheConflictNamed() {
        const QString table = R"([
            {"id":"a.one","label":"One","menu":"File","shortcut":"Ctrl+K","kind":"command","status":"ready"},
            {"id":"a.two","label":"Two","menu":"File","shortcut":"","kind":"command","status":"ready"},
            {"id":"a.three","label":"Three","menu":"File","shortcut":"Ctrl+J","kind":"command","status":"ready"}])";
        QTemporaryDir dir;
        jad::ActionRegistry r;
        r.loadForTest(table, "");
        r.setUserFileForTest(dir.filePath("shortcuts.json"));
        QSignalSpy spy(&r, &jad::ActionRegistry::shortcutsChanged);
        QCOMPARE(r.shortcut("a.two"), QString());
        QCOMPARE(r.setUserShortcut("a.two", "Ctrl+Shift+P"), QString());           // an action without a key gets one
        QCOMPARE(r.shortcut("a.two"), QString("Ctrl+Shift+P"));
        QCOMPARE(spy.count(), 1);
        QVERIFY(r.setUserShortcut("a.three", "Ctrl+K").contains("One"));             // used by "One": refused, with its name
        QCOMPARE(r.shortcut("a.three"), QString("Ctrl+J"));
        QVERIFY(!r.setUserShortcut("a.three", "Ctrl+Nonsense+Zzz").isEmpty());       // not a key combination
        QCOMPARE(r.setUserShortcut("a.one", ""), QString());                         // frees a default key
        QCOMPARE(r.shortcut("a.one"), QString());
        QCOMPARE(r.setUserShortcut("a.three", "Ctrl+K"), QString());                 // which another action can now take
        QVERIFY(QFile::exists(dir.filePath("shortcuts.json")));
        bool changedSeen = false;
        for (const QVariant& v : r.keyCommands()) changedSeen = changedSeen || (v.toMap().value("id") == "a.three" && v.toMap().value("changed").toBool());
        QVERIFY(changedSeen);
        jad::ActionRegistry again;                                                    // a new session reads the file
        again.loadForTest(table, QString::fromUtf8([&] { QFile f(dir.filePath("shortcuts.json")); f.open(QIODevice::ReadOnly); return f.readAll(); }()));
        QCOMPARE(again.shortcut("a.three"), QString("Ctrl+K"));
        QCOMPARE(again.shortcut("a.two"), QString("Ctrl+Shift+P"));
        QCOMPARE(again.shortcut("a.one"), QString());
        r.resetShortcuts();
        QCOMPARE(r.shortcut("a.one"), QString("Ctrl+K"));
        QCOMPARE(r.shortcut("a.two"), QString());
        QVERIFY(!QFile::exists(dir.filePath("shortcuts.json")));
        QCOMPARE(r.keySequenceText(Qt::Key_K, Qt::ControlModifier | Qt::ShiftModifier), QString("Ctrl+Shift+K"));
        QCOMPARE(r.keySequenceText(Qt::Key_Shift, Qt::ShiftModifier), QString());    // a lone modifier is not a key
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
