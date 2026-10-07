#include <QCommandLineParser>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QUrl>

#include "bridge/project_controller.h"

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName("JAD Daw");
    QGuiApplication::setOrganizationName("JAD");
    QQuickStyle::setStyle("Basic");
    QFontDatabase::addApplicationFont(":/qt/qml/Jad/fonts/InterVariable.ttf");

    QCommandLineParser parser;
    parser.addHelpOption();
    QCommandLineOption projectOption("project", "Open the project in <folder> at startup.", "folder");
    parser.addOption(projectOption);
    parser.process(app);

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("Jad", "Main");
    if (parser.isSet(projectOption) && !engine.rootObjects().isEmpty()) {
        auto* controller = engine.rootObjects().first()->property("project").value<jad::ProjectController*>();
        if (controller) controller->openProject(QUrl::fromLocalFile(parser.value(projectOption)));
    }
    return app.exec();
}
