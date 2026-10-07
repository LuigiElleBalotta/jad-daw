import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Jad

ApplicationWindow {
    id: root
    width: 1280
    height: 800
    visible: true
    title: "JAD Daw"
    color: Theme.surfaceApp

    property alias project: controller
    ProjectController { id: controller }

    Shortcut { sequences: [StandardKey.Undo]; onActivated: controller.undo() }
    Shortcut { sequences: [StandardKey.Redo]; onActivated: controller.redo() }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        TransportBar {
            Layout.fillWidth: true
            project: controller
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0
            TrackList {
                Layout.preferredWidth: 220
                Layout.fillHeight: true
                project: controller
                scrollY: timeline.scrollY
                headerHeight: timeline.rulerHeight
            }
            Timeline {
                id: timeline
                Layout.fillWidth: true
                Layout.fillHeight: true
                project: controller
            }
        }
        ErrorBar {
            Layout.fillWidth: true
            message: controller.lastError
            visible: message !== ""
            onDismissed: controller.clearError()
        }
    }
}
