import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Jad

// View > Browsers and Loop Browser: the folders and audio files of the disk. Double click a folder to enter it, an audio file (or Import) to
// bring it into the project at the playhead, on the selected audio track or on a new track named after it.
Dialog {
    id: root
    required property ProjectController project
    modal: false
    anchors.centerIn: parent
    width: 640
    height: 460
    property string path: ""
    property string parentPath: ""
    property var entries: []
    property var locations: []
    property string picked: ""                    // the audio file chosen
    function go(p) {
        const r = project.browseFolder(p)
        path = r.path
        parentPath = r.parent
        entries = r.entries
        picked = ""
    }
    function importPicked() {
        if (picked === "") return
        project.importAudioPath(picked)
    }
    onAboutToShow: { locations = project.standardLocations(); go(path === "" && locations.length > 0 ? locations[0].path : path) }
    background: Rectangle { color: Theme.surfacePanel; border.color: Theme.borderStrong; radius: Theme.radiusDialog }
    header: Item { height: 0 }
    contentItem: ColumnLayout {
        spacing: Theme.spacing[2]
        Text { text: qsTr("Browser"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeTitleSize; font.weight: Theme.fontTypeTitleWeight }
        RowLayout {
            Layout.fillWidth: true
            Button { text: qsTr("Up"); enabled: root.parentPath !== ""; onClicked: root.go(root.parentPath) }
            Text { Layout.fillWidth: true; text: root.path; elide: Text.ElideMiddle; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spacing[3]
            ListView {  // the places
                Layout.preferredWidth: 140
                Layout.fillHeight: true
                clip: true
                model: root.locations
                delegate: Rectangle {
                    required property var modelData
                    width: ListView.view.width
                    height: 24
                    radius: 3
                    color: root.path === modelData.path ? Theme.surfaceRaisedHover : "transparent"
                    Text { anchors.fill: parent; anchors.leftMargin: Theme.spacing[2]; verticalAlignment: Text.AlignVCenter; text: modelData.name; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize }
                    MouseArea { anchors.fill: parent; onClicked: root.go(modelData.path) }
                }
            }
            ListView {
                id: files
                objectName: "browserEntries"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: root.entries
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                delegate: Rectangle {
                    required property var modelData
                    width: ListView.view.width - 10
                    height: 24
                    radius: 3
                    color: root.picked === modelData.path ? Theme.accentPrimary : (area.containsMouse ? Theme.surfaceRaisedHover : "transparent")
                    Text {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.spacing[2]
                        verticalAlignment: Text.AlignVCenter
                        text: (modelData.dir ? "\u25B8 " : "\u266A ") + modelData.name
                        elide: Text.ElideRight
                        color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize
                    }
                    MouseArea {
                        id: area
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: root.picked = modelData.dir ? "" : modelData.path
                        onDoubleClicked: { if (modelData.dir) root.go(modelData.path); else { root.picked = modelData.path; root.importPicked() } }
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Button { objectName: "importButton"; text: qsTr("Import"); enabled: root.picked !== ""; onClicked: root.importPicked() }
            Item { Layout.fillWidth: true }
            Button { text: qsTr("Close"); onClicked: root.close() }
        }
    }
}
