import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Jad

// Mix > Search and Add Plug-in: type a part of a name or of a manufacturer; Return adds the first match to the selected track, or click one.
Dialog {
    id: root
    required property ProjectController project
    modal: true
    anchors.centerIn: parent
    width: 420
    height: 400
    property var all: []
    readonly property var matches: {
        const q = field.text.trim().toLowerCase()
        return all.filter(p => q === "" || p.name.toLowerCase().indexOf(q) >= 0 || p.vendor.toLowerCase().indexOf(q) >= 0)
    }
    onAboutToShow: { all = project.searchablePlugins(); field.text = ""; field.forceActiveFocus() }
    function choose(p) { project.addSearchedPlugin(p.id, p.kind, p.name); close() }
    background: Rectangle { color: Theme.surfacePanel; border.color: Theme.borderStrong; radius: Theme.radiusDialog }
    header: Item { height: 0 }
    contentItem: ColumnLayout {
        spacing: Theme.spacing[3]
        Text { text: qsTr("Search and Add Plug-in"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeTitleSize; font.weight: Theme.fontTypeTitleWeight }
        Text {
            visible: root.all.length === 0
            text: qsTr("Select a track first.")
            color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 26
            color: Theme.surfaceRaised
            radius: Theme.radiusControl - 2
            border.color: field.activeFocus ? Theme.accentPrimary : Theme.borderSubtle
            TextInput {
                id: field
                objectName: "pluginSearch"
                anchors.fill: parent
                anchors.margins: 5
                verticalAlignment: TextInput.AlignVCenter
                color: Theme.textValue
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTypeBodySize
                selectByMouse: true
                onAccepted: if (root.matches.length > 0) root.choose(root.matches[0])
            }
        }
        ListView {
            objectName: "pluginResults"
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
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spacing[2]
                    anchors.rightMargin: Theme.spacing[2]
                    Text { Layout.fillWidth: true; text: modelData.name; elide: Text.ElideRight; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize }
                    Text { text: modelData.vendor + (modelData.kind === "instrument" ? " \u00b7 " + qsTr("instrument") : ""); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeCaptionSize }
                }
                MouseArea { id: area; anchors.fill: parent; hoverEnabled: true; onClicked: root.choose(modelData) }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            IconButton { implicitHeight: 26; label: qsTr("Close"); onClicked: root.close() }
        }
    }
}
