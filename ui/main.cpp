#include <QCommandLineParser>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTimer>
#include <QUrl>

#include "bridge/project_controller.h"

int main(int argc, char** argv) {
    // --software: render without a GPU (no icons: the effects they use need one); the default is the platform's renderer
    for (int i = 1; i < argc; ++i)
        if (QByteArray(argv[i]) == "--software") QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);

    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName("JAD Daw");
    QGuiApplication::setOrganizationName("JAD");
#ifdef JAD_VERSION
    QGuiApplication::setApplicationVersion(JAD_VERSION);
#endif
    QQuickStyle::setStyle("Basic");
    QFontDatabase::addApplicationFont(":/qt/qml/Jad/fonts/InterVariable.ttf");

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption projectOption("project", "Open the project in <folder> at startup.", "folder");
    QCommandLineOption noAudioOption("no-audio", "Do not open the audio device.");
    QCommandLineOption screenshotOption("screenshot", "Save a picture of the window to <file> and quit (for the README).", "file");
    QCommandLineOption softwareOption("software", "Render without a GPU (icons are missing).");
    QCommandLineOption sizeOption("size", "Window size for --screenshot, for example 1280x800.", "WxH", "1280x800");
    QCommandLineOption delayOption("delay", "Milliseconds to wait before the screenshot.", "ms", "1500");
    parser.addOption(projectOption);
    parser.addOption(noAudioOption);
    parser.addOption(screenshotOption);
    parser.addOption(softwareOption);
    parser.addOption(sizeOption);
    parser.addOption(delayOption);
    parser.process(app);

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("Jad", "Main");
    if (engine.rootObjects().isEmpty()) return 1;
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());

    if (parser.isSet(sizeOption) && window) {
        const QStringList wh = parser.value(sizeOption).split('x');
        if (wh.size() == 2 && wh[0].toInt() > 0 && wh[1].toInt() > 0) window->resize(wh[0].toInt(), wh[1].toInt());
    }
    auto* controller = engine.rootObjects().first()->property("project").value<jad::ProjectController*>();
    if (controller) {
        if (parser.isSet(noAudioOption)) controller->setAudioEnabled(false);
        if (parser.isSet(projectOption)) controller->openProject(QUrl::fromLocalFile(parser.value(projectOption)));
    }

    if (parser.isSet(screenshotOption) && window) {
        const QString file = parser.value(screenshotOption);
        QTimer::singleShot(parser.value(delayOption).toInt(), &app, [window, file] {
            const QImage image = window->grabWindow();
            QCoreApplication::exit(!image.isNull() && image.save(file) ? 0 : 2);
        });
    }
    return app.exec();
}
