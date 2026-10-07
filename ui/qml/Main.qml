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

    ProjectController { id: project }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        TransportBar {
            Layout.fillWidth: true
            project: project
        }
        Item { Layout.fillWidth: true; Layout.fillHeight: true }
        ErrorBar {
            Layout.fillWidth: true
            message: project.lastError
            visible: message !== ""
            onDismissed: project.clearError()
        }
    }
}
