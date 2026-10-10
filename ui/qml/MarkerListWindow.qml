import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Jad

// Navigate > Open Marker List (also Go To > Marker and Rename Marker): every marker with its position, a name field,
// Go (moves the playhead there) and Delete.
Dialog {
    id: root
    required property ProjectController project
    modal: false
    anchors.centerIn: parent
    width: 460
    height: 420
    property var list: []
    function reload() { list = project.markers() }
    onAboutToShow: reload()
    Connections { target: root.project; function onProjectChanged() { if (root.visible) root.reload() } }
    background: Rectangle { color: Theme.surfacePanel; border.color: Theme.borderStrong; radius: Theme.radiusDialog }
    header: Item { height: 0 }
    contentItem: ColumnLayout {
        spacing: Theme.spacing[3]
        Text { text: qsTr("Marker List"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeTitleSize; font.weight: Theme.fontTypeTitleWeight }
        Text { visible: root.list.length === 0; text: qsTr("No markers yet. Create one at the playhead."); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize }
        ListView {
            id: view
            objectName: "markerListView"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.list
            spacing: 2
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            delegate: RowLayout {
                id: row
                required property var modelData
                width: ListView.view.width - 10
                spacing: Theme.spacing[2]
                Text {
                    Layout.preferredWidth: 70
                    text: Math.floor(row.modelData.beats / 4) + 1 + "." + (Math.floor(row.modelData.beats % 4) + 1)
                    color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize
                }
                TextEntry {
                    objectName: "markerName"
                    Layout.fillWidth: true
                    text: row.modelData.name
                    onEdited: (t) => { if (t !== row.modelData.name) root.project.renameMarker(row.modelData.id, t) }
                }
                IconButton { implicitHeight: 26; label: qsTr("Go"); onClicked: root.project.locateBeats(row.modelData.beats) }
                IconButton { implicitHeight: 26; label: qsTr("Delete"); onClicked: root.project.removeMarker(row.modelData.id) }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            IconButton { implicitHeight: 26; label: qsTr("New Marker"); onClicked: root.project.createMarkerAtPlayhead() }
            Item { Layout.fillWidth: true }
            IconButton { implicitHeight: 26; label: qsTr("Close"); onClicked: root.close() }
        }
    }
}
