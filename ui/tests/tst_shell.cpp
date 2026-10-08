#include <QtTest>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>

#include "actions/action_registry.h"

class ShellTest : public QObject {
    Q_OBJECT
private slots:
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
