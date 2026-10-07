#include <QtTest>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>

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
};

QTEST_MAIN(ShellTest)
#include "tst_shell.moc"
