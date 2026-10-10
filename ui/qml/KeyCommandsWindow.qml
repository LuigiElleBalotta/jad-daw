import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Window
import Jad

// Help > Key Commands: every command with its key. Click a key to change it (press the new combination), Remove frees it, Reset All goes back
// to the defaults. A key that another command uses is refused with the name of that command.
Window {
    id: root
    required property var registry
    property string filter: ""
    property string learning: ""        // the id of the command waiting for a key
    property string message: ""
    property var commands: []
    function reload() { commands = registry.keyCommands() }
    Component.onCompleted: reload()
    Connections { target: root.registry; function onShortcutsChanged() { root.reload() } }

    width: 640
    height: 560
    minimumWidth: 480
    minimumHeight: 320
    title: qsTr("Key Commands")
    color: Theme.surfacePanel
    flags: Qt.Tool
    onVisibleChanged: if (visible) { reload(); filter = ""; learning = ""; message = ""; searchField.forceActiveFocus() }

    readonly property var shown: {
        const f = filter.trim().toLowerCase()
        return f === "" ? commands : commands.filter(c => c.label.toLowerCase().indexOf(f) >= 0 || c.menu.toLowerCase().indexOf(f) >= 0 || c.shortcut.toLowerCase().indexOf(f) >= 0)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing[3]
        spacing: Theme.spacing[2]
        RowLayout {
            spacing: Theme.spacing[2]
            Rectangle {
                Layout.fillWidth: true; Layout.preferredHeight: 26
                color: Theme.surfaceRaised; radius: Theme.radiusControl - 2
                border.color: searchField.activeFocus ? Theme.accentPrimary : Theme.borderSubtle
                TextInput {
                    id: searchField
                    objectName: "searchField"
                    anchors.fill: parent; anchors.margins: 6
                    verticalAlignment: TextInput.AlignVCenter
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontTypeBodySize
                    onTextChanged: root.filter = text
                    Text { visible: parent.text === ""; anchors.verticalCenter: parent.verticalCenter; text: qsTr("Search commands and keys"); color: Theme.textSecondary; font: parent.font }
                }
            }
            IconButton { objectName: "resetAll"; implicitHeight: 26; label: qsTr("Reset All"); onClicked: { root.registry.resetShortcuts(); root.message = "" } }
        }
        Text {
            Layout.fillWidth: true
            visible: root.message !== ""
            text: root.message
            color: Theme.stateClip
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTypeLabelSize
        }
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.shown
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            delegate: Rectangle {
                id: row
                required property var modelData
                width: list.width - 10
                height: 26
                color: root.learning === modelData.id ? Theme.surfaceRaisedHover : "transparent"
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spacing[2]
                    spacing: Theme.spacing[2]
                    Text {
                        Layout.fillWidth: true
                        text: (row.modelData.menu !== "" ? row.modelData.menu + " \u203a " : "") + row.modelData.label
                        color: row.modelData.stub ? Theme.textSecondary : Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontTypeBodySize
                        elide: Text.ElideRight
                    }
                    IconButton {
                        objectName: "key_" + row.modelData.id
                        implicitHeight: 22
                        implicitWidth: 120
                        label: root.learning === row.modelData.id ? qsTr("Press the key…") : (row.modelData.shortcut !== "" ? row.modelData.shortcut : qsTr("none"))
                        active: root.learning === row.modelData.id || row.modelData.changed
                        onClicked: { root.learning = row.modelData.id; root.message = ""; keys.forceActiveFocus() }
                    }
                    IconButton {
                        implicitHeight: 22
                        label: qsTr("Remove")
                        enabled: row.modelData.shortcut !== ""
                        onClicked: root.message = root.registry.setUserShortcut(row.modelData.id, "")
                    }
                }
            }
        }
    }
    Item {  // takes the key press of a command that is being changed
        id: keys
        focus: true
        Keys.onPressed: (event) => {
            if (root.learning === "") return
            if (event.key === Qt.Key_Escape) { root.learning = ""; event.accepted = true; return }
            const text = root.registry.keySequenceText(event.key, event.modifiers)
            if (text === "") return
            root.message = root.registry.setUserShortcut(root.learning, text)
            if (root.message === "") root.learning = ""
            event.accepted = true
        }
    }
}
