#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTimer>
#include <QUrl>

#include <QSettings>

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
    // San Francisco (SF Pro Text) is used when it is there: Apple's licence does not let it be redistributed, so it is not in the repository. Put the .otf files in
    // `fonts/` next to the program or in `assets/fonts/sf-pro/` of the source tree (any folder above the program); Theme.fontFamily falls back to Inter without them.
    {
        QDir dir(QCoreApplication::applicationDirPath());
        for (int up = 0; up < 8; ++up, dir.cdUp()) {
            for (const QString& sub : {QStringLiteral("fonts"), QStringLiteral("assets/fonts/sf-pro")}) {
                const QDir fonts(dir.filePath(sub));
                for (const QString& file : fonts.entryList({QStringLiteral("SF-Pro-*.otf"), QStringLiteral("SF-Pro-*.ttf")}, QDir::Files))
                    QFontDatabase::addApplicationFont(fonts.filePath(file));
            }
        }
    }

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption projectOption("project", "Open the project in <folder> at startup.", "folder");
    QCommandLineOption noAudioOption("no-audio", "Do not open the audio device.");
    QCommandLineOption screenshotOption("screenshot", "Save a picture of the window to <file> and quit (for the README).", "file");
    QCommandLineOption toolOption("tool", "Active tool for --screenshot.", "name");
    QCommandLineOption selectTrackOption("select-track", "Select track number <n> (1-based) for --screenshot.", "n");
    QCommandLineOption menuOption("open-menu", "Open the menu at <index> of the menu bar for --screenshot.", "index");
    QCommandLineOption panelsOption("panels", "Panels shown for --screenshot, comma separated: library, inspector, smart, mixer, editors, global.", "list");
    QCommandLineOption selectRegionOption("select-region", "Select region number <n> (1-based) for --screenshot.", "n");
    QCommandLineOption applyPatchOption("apply-patch", "Apply the patch <id> to the selected track for --screenshot.", "id");
    QCommandLineOption softwareOption("software", "Render without a GPU (icons are missing).");
    QCommandLineOption flexOption("flex", "Show Flex and switch the first audio track to the Flex mode <mode> for --screenshot.", "mode");
    QCommandLineOption sizeOption("size", "Window size for --screenshot, for example 1280x800.", "WxH", "1280x800");
    QCommandLineOption windowOption("window", "Open the dialog or window <name> for --screenshot (a window is pictured on its own).", "name");
    QCommandLineOption effectOption("effect", "Open the window of an insert for --screenshot: <track>:<insert>, tracks counted from 1 (m: the master), the insert from 0 (-2: the instrument).", "track:insert");
    QCommandLineOption delayOption("delay", "Milliseconds to wait before the screenshot.", "ms", "1500");
    parser.addOption(projectOption);
    parser.addOption(noAudioOption);
    parser.addOption(screenshotOption);
    parser.addOption(softwareOption);
    parser.addOption(panelsOption);
    parser.addOption(applyPatchOption);
    parser.addOption(selectRegionOption);
    parser.addOption(toolOption);
    parser.addOption(flexOption);
    parser.addOption(selectTrackOption);
    parser.addOption(menuOption);
    parser.addOption(sizeOption);
    parser.addOption(delayOption);
    parser.addOption(windowOption);
    parser.addOption(effectOption);
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
        if (!parser.isSet(screenshotOption)) controller->startPlugins();  // hosting and the first scan do not wait for a project
        if (!parser.isSet(screenshotOption)) {  // pictures start from the defaults, not from the last session
            QSettings settings;
            controller->loadPanelState(settings);
            QObject::connect(&app, &QCoreApplication::aboutToQuit, controller, [controller] {
                QSettings s;
                controller->savePanelState(s);
            });
        }
        if (parser.isSet(projectOption)) controller->openProject(QUrl::fromLocalFile(parser.value(projectOption)));
    }
    // a day after the last look the program asks GitHub for a newer release (Help > Check for Updates does it on demand); pictures and tests stay offline
    if (!parser.isSet(screenshotOption) && qEnvironmentVariableIsEmpty("JAD_NO_UPDATE_CHECK")) {
        if (auto* updater = engine.rootObjects().first()->property("updater").value<QObject*>())
            QTimer::singleShot(5000, updater, [updater] { QMetaObject::invokeMethod(updater, "checkOnStartup"); });
    }

    if (parser.isSet(screenshotOption) && window) {
        const QString file = parser.value(screenshotOption);
        const int delay = parser.value(delayOption).toInt();
        // the state for the picture is set shortly before it is taken, once the project has been shown
        QTimer::singleShot(delay, &app, [&parser, &flexOption, &toolOption, &selectTrackOption, &selectRegionOption, &panelsOption, &applyPatchOption, &menuOption, &windowOption, &effectOption, controller, window, file, &app] {
            if (controller && parser.isSet(toolOption)) controller->setTool(parser.value(toolOption));
            if (controller && parser.isSet(flexOption)) {
                controller->setFlexView(true);
                for (int i = 0; i < controller->tracks()->rowCount(); ++i) {
                    const QString id = controller->tracks()->trackIdAt(i);
                    const auto roles = controller->tracks()->roleNames();
                    if (controller->tracks()->data(controller->tracks()->index(i), roles.key("kind")).toString() == "audio") {
                        controller->setTrackFlexMode(id, parser.value(flexOption));
                        break;
                    }
                }
            }
            if (controller && parser.isSet(selectTrackOption))
                controller->selectTrack(controller->tracks()->trackIdAt(parser.value(selectTrackOption).toInt() - 1), "replace");
            if (controller && parser.isSet(panelsOption)) {
                const QStringList shown = parser.value(panelsOption).split(',', Qt::SkipEmptyParts);
                controller->setLibraryVisible(shown.contains("library"));
                controller->setInspectorVisible(shown.contains("inspector"));
                controller->setSmartControlsVisible(shown.contains("smart"));
                controller->setMixerVisible(shown.contains("mixer"));
                window->setProperty("editorsVisible", shown.contains("editors"));
                controller->setGlobalTracksVisible(shown.contains("global"));
                controller->setAutomationVisible(shown.contains("automation"));
            }
            if (controller && parser.isSet(applyPatchOption)) controller->applyPatch(parser.value(applyPatchOption));
            if (controller && parser.isSet(selectRegionOption))
                controller->selectRegion(controller->regions()->regionIdAt(parser.value(selectRegionOption).toInt() - 1), "replace");
            if (parser.isSet(menuOption)) QMetaObject::invokeMethod(window, "showMenu", Q_ARG(QVariant, parser.value(menuOption).toInt()));
            QQuickWindow* shot = window;
            QString windowName = parser.isSet(windowOption) ? parser.value(windowOption) : QString();
            if (controller && parser.isSet(effectOption)) {
                const QStringList at = parser.value(effectOption).split(':');
                if (at.size() == 2) {
                    controller->openEffectEditor(at[0] == QLatin1String("m") ? controller->masterTrackId() : controller->tracks()->trackIdAt(at[0].toInt() - 1), at[1].toInt());
                    windowName = QStringLiteral("effect");
                }
            }
            if (!windowName.isEmpty()) {
                QVariant opened;
                QMetaObject::invokeMethod(window, "showWindow", Q_RETURN_ARG(QVariant, opened), Q_ARG(QVariant, windowName));
                if (auto* other = qobject_cast<QQuickWindow*>(opened.value<QObject*>())) shot = other;  // a window of its own
            }
            QTimer::singleShot(parser.isSet(flexOption) ? 4000 : 600, &app, [shot, file] {
                const QImage image = shot->grabWindow();
                QCoreApplication::exit(!image.isNull() && image.save(file) ? 0 : 2);
            });
        });
    }
    return app.exec();
}
