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
    QCommandLineOption toolOption("tool", "Active tool for --screenshot.", "name");
    QCommandLineOption selectTrackOption("select-track", "Select track number <n> (1-based) for --screenshot.", "n");
    QCommandLineOption menuOption("open-menu", "Open the menu at <index> of the menu bar for --screenshot.", "index");
    QCommandLineOption panelsOption("panels", "Panels shown for --screenshot, comma separated: library, inspector, smart, mixer.", "list");
    QCommandLineOption selectRegionOption("select-region", "Select region number <n> (1-based) for --screenshot.", "n");
    QCommandLineOption softwareOption("software", "Render without a GPU (icons are missing).");
    QCommandLineOption sizeOption("size", "Window size for --screenshot, for example 1280x800.", "WxH", "1280x800");
    QCommandLineOption delayOption("delay", "Milliseconds to wait before the screenshot.", "ms", "1500");
    parser.addOption(projectOption);
    parser.addOption(noAudioOption);
    parser.addOption(screenshotOption);
    parser.addOption(softwareOption);
    parser.addOption(panelsOption);
    parser.addOption(selectRegionOption);
    parser.addOption(toolOption);
    parser.addOption(selectTrackOption);
    parser.addOption(menuOption);
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
        const int delay = parser.value(delayOption).toInt();
        // the state for the picture is set shortly before it is taken, once the project has been shown
        QTimer::singleShot(delay, &app, [&parser, &toolOption, &selectTrackOption, &selectRegionOption, &panelsOption, &menuOption, controller, window, file, &app] {
            if (controller && parser.isSet(toolOption)) controller->setTool(parser.value(toolOption));
            if (controller && parser.isSet(selectTrackOption))
                controller->selectTrack(controller->tracks()->trackIdAt(parser.value(selectTrackOption).toInt() - 1), "replace");
            if (controller && parser.isSet(panelsOption)) {
                const QStringList shown = parser.value(panelsOption).split(',', Qt::SkipEmptyParts);
                controller->setLibraryVisible(shown.contains("library"));
                controller->setInspectorVisible(shown.contains("inspector"));
                controller->setSmartControlsVisible(shown.contains("smart"));
                controller->setMixerVisible(shown.contains("mixer"));
            }
            if (controller && parser.isSet(selectRegionOption))
                controller->selectRegion(controller->regions()->regionIdAt(parser.value(selectRegionOption).toInt() - 1), "replace");
            if (parser.isSet(menuOption)) QMetaObject::invokeMethod(window, "showMenu", Q_ARG(QVariant, parser.value(menuOption).toInt()));
            QTimer::singleShot(400, &app, [window, file] {
                const QImage image = window->grabWindow();
                QCoreApplication::exit(!image.isNull() && image.save(file) ? 0 : 2);
            });
        });
    }
    return app.exec();
}
