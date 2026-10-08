#include <QtTest>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QAbstractItemModel>
#include <QTemporaryDir>

#include "actions/action_registry.h"

namespace {

// the first object below `root` that has the property `name` (the LCD, a track header, ...)
QObject* findWith(QObject* root, const char* name) {
    if (!root) return nullptr;
    if (root->property(name).isValid()) return root;
    QList<QObject*> kids = root->children();
    if (auto* item = qobject_cast<QQuickItem*>(root))  // delegates are visual children, not always QObject children
        for (QQuickItem* child : item->childItems()) if (!kids.contains(child)) kids << child;
    for (QObject* child : kids)
        if (QObject* found = findWith(child, name)) return found;
    return nullptr;
}

// the window of the real Main, with audio off and a fresh project in `dir` open
QQuickWindow* openMainWithProject(QQmlApplicationEngine& engine, QObject*& project, QTemporaryDir& dir) {
    engine.loadFromModule("Jad", "Main");
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if (!window) return nullptr;
    project = window->property("project").value<QObject*>();
    if (!project) return nullptr;
    project->setProperty("audioEnabled", false);
    bool ok = false;
    QMetaObject::invokeMethod(project, "newProject", Q_RETURN_ARG(bool, ok),
                              Q_ARG(QUrl, QUrl::fromLocalFile(dir.path())));
    if (!ok) return nullptr;
    window->requestActivate();
    if (!QTest::qWaitForWindowActive(window)) return nullptr;
    return window;
}

// typed text, one key at a time, into whatever has focus in the window
void typeText(QQuickWindow* window, const QString& text) {
    for (const QChar c : text) QTest::keyClick(window, static_cast<char>(c.toLatin1()));
}

// a double click (or a single one) at the centre of an item of the window
void clickItem(QQuickWindow* window, QQuickItem* item, bool doubleClick) {
    const QPoint p = item->mapToScene(item->boundingRect().center()).toPoint();
    if (doubleClick) QTest::mouseDClick(window, Qt::LeftButton, Qt::NoModifier, p);
    else QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, p);
}

}  // namespace

class ShellTest : public QObject {
    Q_OBJECT
private slots:
    // Return is also the global "Go to Beginning" shortcut; it must confirm the field instead
    void enterConfirmsATempoEditAndKeepsThePlayhead() {
        QQmlApplicationEngine engine;
        QObject* project = nullptr;
        QTemporaryDir dir;
        QQuickWindow* window = openMainWithProject(engine, project, dir);
        QVERIFY(window);
        QMetaObject::invokeMethod(project, "locateBeats", Q_ARG(double, 8.0));
        QTRY_COMPARE(project->property("positionBeats").toDouble(), 8.0);
        auto* lcd = findWith(window, "tempoCell");
        QVERIFY(lcd);
        auto* cell = qobject_cast<QQuickItem*>(lcd->property("tempoCell").value<QObject*>());
        QVERIFY(cell);
        clickItem(window, cell, true);
        QVERIFY(cell->property("editing").toBool());
        typeText(window, QStringLiteral("95"));
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_COMPARE(project->property("bpm").toDouble(), 95.0);
        QTest::qWait(300);
        QCOMPARE(project->property("positionBeats").toDouble(), 8.0);
    }
    void enterConfirmsATrackRename() {
        QQmlApplicationEngine engine;
        QObject* project = nullptr;
        QTemporaryDir dir;
        QQuickWindow* window = openMainWithProject(engine, project, dir);
        QVERIFY(window);
        QMetaObject::invokeMethod(project, "addTrack", Q_ARG(QString, QStringLiteral("audio")));
        auto* tracks = qobject_cast<QAbstractItemModel*>(project->property("tracks").value<QObject*>());
        QVERIFY(tracks);
        QTRY_VERIFY(tracks->rowCount() > 0);
        QObject* header = nullptr;
        QTRY_VERIFY((header = findWith(window, "nameLabel")) != nullptr);
        auto* label = qobject_cast<QQuickItem*>(header->property("nameLabel").value<QObject*>());
        QVERIFY(label);
        clickItem(window, label, true);
        QVERIFY(header->property("editing").toBool());
        typeText(window, QStringLiteral("Lead"));
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_COMPARE(header->property("trackName").toString(), QStringLiteral("Lead"));
    }

    void mainWindowLoadsWithTheme() {
        QQmlApplicationEngine engine;
        engine.loadFromModule("Jad", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(window);
        QCOMPARE(window->title(), QStringLiteral("JAD Daw"));
        QCOMPARE(window->color(), QColor("#0e0e0e"));  // Theme.surfaceApp
    }
    void aboutDialogOpens() {
        QQmlApplicationEngine engine;
        engine.loadFromModule("Jad", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* dialog = engine.rootObjects().first()->findChild<QObject*>("aboutDialog");
        QVERIFY(dialog);
        QVERIFY(!dialog->property("visible").toBool());
        QMetaObject::invokeMethod(dialog, "open");
        QVERIFY(dialog->property("visible").toBool());
    }
    void everyReadyActionHasAHandler() {
        QQmlApplicationEngine engine;
        engine.loadFromModule("Jad", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* registry = engine.rootObjects().first()->findChild<jad::ActionRegistry*>("registry");
        QVERIFY(registry);
        const QStringList handled = registry->handledIds();
        QStringList missing;
        for (const QString& id : registry->readyIds())
            if (!handled.contains(id)) missing << id;
        QVERIFY2(missing.isEmpty(), qPrintable("ready actions without a handler: " + missing.join(", ")));
    }
};

QTEST_MAIN(ShellTest)
#include "tst_shell.moc"
