#include <QFontDatabase>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName("JAD Daw");
    QGuiApplication::setOrganizationName("JAD");
    QQuickStyle::setStyle("Basic");
    QFontDatabase::addApplicationFont(":/qt/qml/Jad/fonts/InterVariable.ttf");
    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("Jad", "Main");
    return app.exec();
}
