import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Jad

// Track > Search and Select Track: type a part of a name, Return selects the first match (or click one).
Dialog {
    id: root
    required property ProjectController project
    modal: true
    anchors.centerIn: parent
    width: 380
    height: 360
    property var all: []
    readonly property var matches: {
        const q = field.text.trim().toLowerCase()
        return all.filter(t => q === "" || t.name.toLowerCase().indexOf(q) >= 0)
    }
    function reload() { all = project.trackList() }
    onAboutToShow: { reload(); field.text = ""; field.forceActiveFocus() }
    function choose(id) { project.selectTrack(id, "replace"); close() }
    background: Rectangle { color: Theme.surfacePanel; border.color: Theme.borderStrong; radius: Theme.radiusDialog }
    header: Item { height: 0 }
    contentItem: ColumnLayout {
        spacing: Theme.spacing[3]
        Text { text: qsTr("Search and Select Track"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeTitleSize; font.weight: Theme.fontTypeTitleWeight }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 26
            color: Theme.surfaceRaised
            radius: Theme.radiusControl - 2
            border.color: field.activeFocus ? Theme.accentPrimary : Theme.borderSubtle
            TextInput {
                id: field
                objectName: "searchField"
                anchors.fill: parent
                anchors.margins: 5
                verticalAlignment: TextInput.AlignVCenter
                color: Theme.textValue
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTypeBodySize
                selectByMouse: true
                onAccepted: if (root.matches.length > 0) root.choose(root.matches[0].id)
            }
        }
        ListView {
            objectName: "searchResults"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.matches
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            delegate: Rectangle {
                required property var modelData
                width: ListView.view.width - 10
                height: 26
                radius: 3
                color: area.containsMouse ? Theme.surfaceRaisedHover : "transparent"
                Text { anchors.fill: parent; anchors.leftMargin: Theme.spacing[2]; verticalAlignment: Text.AlignVCenter; text: modelData.name; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize }
                MouseArea { id: area; anchors.fill: parent; hoverEnabled: true; onClicked: root.choose(modelData.id) }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            Button { text: qsTr("Close"); onClicked: root.close() }
        }
    }
}
