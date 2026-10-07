#include <QFile>
#include <QtTest>
#include "shortcuts/shortcut_map.h"

class ShortcutsTest : public QObject {
    Q_OBJECT
    const QString defaults = R"({"transport.playStop":"Space","edit.undo":"Ctrl+Z","edit.redo":"Ctrl+Shift+Z"})";
private slots:
    void defaultsAreLoaded() {
        QStringList p;
        auto m = jad::ShortcutMap::fromFiles(defaults, "{}", &p);
        QCOMPARE(m.sequence("transport.playStop"), QStringLiteral("Space"));
        QVERIFY(p.isEmpty());
    }
    void userOverridesOneAction() {
        QStringList p;
        auto m = jad::ShortcutMap::fromFiles(defaults, R"({"transport.playStop":"P"})", &p);
        QCOMPARE(m.sequence("transport.playStop"), QStringLiteral("P"));
        QCOMPARE(m.sequence("edit.undo"), QStringLiteral("Ctrl+Z"));
    }
    void unknownIdAndClashAreReportedAndIgnored() {
        QStringList p;
        auto m = jad::ShortcutMap::fromFiles(defaults, R"({"nope.action":"X","edit.redo":"Ctrl+Z"})", &p);
        QCOMPARE(p.size(), 2);
        QCOMPARE(m.sequence("edit.redo"), QStringLiteral("Ctrl+Shift+Z"));
        QVERIFY(m.sequence("nope.action").isEmpty());
    }
    void brokenUserFileFallsBackToDefaults() {
        QStringList p;
        auto m = jad::ShortcutMap::fromFiles(defaults, "{ not json", &p);
        QCOMPARE(m.sequence("edit.undo"), QStringLiteral("Ctrl+Z"));
        QVERIFY(!p.isEmpty());
    }
    void invalidSequenceIsReportedAndIgnored() {
        QStringList p;
        auto m = jad::ShortcutMap::fromFiles(defaults, R"({"edit.undo":"Ctrl+Banana"})", &p);
        QCOMPARE(p.size(), 1);
        QCOMPARE(m.sequence("edit.undo"), QStringLiteral("Ctrl+Z"));
    }
    void aNonStringValueIsReportedAndIgnored() {
        QStringList p;
        auto m = jad::ShortcutMap::fromFiles(defaults, R"({"edit.undo":42})", &p);
        QCOMPARE(p.size(), 1);
        QCOMPARE(m.sequence("edit.undo"), QStringLiteral("Ctrl+Z"));
    }
    void swappingTwoActionsInTheUserFileIsAClash() {
        // each sequence is compared against the other action's effective sequence, so the first one wins and the
        // second one is refused: the result must never hold the same sequence twice
        QStringList p;
        auto m = jad::ShortcutMap::fromFiles(defaults, R"({"edit.undo":"Ctrl+Shift+Z","edit.redo":"Ctrl+Z"})", &p);
        QVERIFY(m.sequence("edit.undo") != m.sequence("edit.redo"));
    }
    void shippedDefaultsAreValidAndClashFree() {
        QFile f(QStringLiteral(JAD_DEFAULT_SHORTCUTS));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QStringList p;
        auto m = jad::ShortcutMap::fromFiles(QString::fromUtf8(f.readAll()), "{}", &p);
        QVERIFY2(p.isEmpty(), qPrintable(p.join("; ")));
        QCOMPARE(m.sequence("transport.playStop"), QStringLiteral("Space"));
        QCOMPARE(m.sequence("edit.redo"), QStringLiteral("Ctrl+Shift+Z"));
        QCOMPARE(m.sequence("view.zoomIn"), QStringLiteral("+"));
    }
};
QTEST_MAIN(ShortcutsTest)
#include "tst_shortcuts.moc"
