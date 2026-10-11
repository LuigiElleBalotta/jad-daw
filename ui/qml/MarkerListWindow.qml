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
    property string selectedId: ""          // the marker whose text is in the Marker Text area
    readonly property var selected: { for (const m of list) if (m.id === selectedId) return m; return null }
    function reload() { list = project.markers() }
    // Navigate > Open Marker Text: the list with the text of the marker the playhead is in
    function openText() {
        reload()
        selectedId = project.markerAtOrBefore(project.positionBeats)
        if (selectedId === "" && list.length > 0) selectedId = list[0].id
        open()
        textArea.forceActiveFocus()
    }
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
                Rectangle {  // the marker whose text is shown
                    Layout.preferredWidth: 6; Layout.preferredHeight: 18
                    radius: 3
                    color: root.selectedId === row.modelData.id ? Theme.accentPrimary : "transparent"
                    MouseArea { anchors.fill: parent; anchors.margins: -4; onClicked: root.selectedId = row.modelData.id }
                }
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
        Text { text: root.selected ? qsTr("Marker Text: %1").arg(root.selected.name) : qsTr("Marker Text (click the bar left of a marker)"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeCaptionSize }
        ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: 110
            TextArea {
                id: textArea
                objectName: "markerText"
                enabled: root.selected !== null
                text: root.selected ? root.selected.text : ""
                wrapMode: TextArea.Wrap
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTypeBodySize
                background: Rectangle { color: Theme.surfaceRaised; radius: Theme.radiusControl - 2; border.color: textArea.activeFocus ? Theme.accentPrimary : Theme.borderSubtle }
                onActiveFocusChanged: if (!activeFocus && root.selected && text !== root.selected.text) root.project.setMarkerText(root.selectedId, text)
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
