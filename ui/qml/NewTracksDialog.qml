import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Jad

// Track > New Tracks: how many tracks of which type, with an optional name; they are created in one undo step.
Dialog {
    id: root
    required property ProjectController project
    modal: true
    anchors.centerIn: parent
    width: 380
    property string kind: "audio"
    property int count: 1

    background: Rectangle { color: Theme.surfacePanel; border.color: Theme.borderStrong; radius: Theme.radiusDialog }
    header: Item { height: 0 }

    function create() {
        project.addTracks(kind, count, nameField.text.trim())
        close()
    }
    onOpened: { nameField.text = ""; countField.text = String(count); countField.forceActiveFocus() }

    contentItem: ColumnLayout {
        spacing: Theme.spacing[3]
        Text {
            text: qsTr("New Tracks")
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTypeTitleSize
            font.weight: Theme.fontTypeTitleWeight
        }
        Row {
            spacing: Theme.spacing[1]
            Repeater {
                model: [{ id: "audio", label: qsTr("Audio") }, { id: "instrument", label: qsTr("Software Instrument") }, { id: "bus", label: qsTr("Aux") }]
                delegate: IconButton {
                    required property var modelData
                    implicitHeight: 26
                    label: modelData.label
                    active: root.kind === modelData.id
                    fillActive: true
                    fillText: Theme.textPrimary
                    onClicked: root.kind = modelData.id
                }
            }
        }
        RowLayout {
            spacing: Theme.spacing[2]
            Text { text: qsTr("Number"); Layout.preferredWidth: 60; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeLabelSize }
            Rectangle {
                Layout.preferredWidth: 60; Layout.preferredHeight: 24
                color: Theme.surfaceRaised; radius: Theme.radiusControl - 2
                border.color: countField.activeFocus ? Theme.accentPrimary : Theme.borderSubtle
                TextInput {
                    id: countField
                    anchors.fill: parent; anchors.margins: 5
                    verticalAlignment: TextInput.AlignVCenter
                    validator: IntValidator { bottom: 1; top: 64 }
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontTypeBodySize
                    onTextEdited: if (text !== "") root.count = Math.max(1, Math.min(64, parseInt(text)))
                    onAccepted: root.create()
                }
            }
        }
        RowLayout {
            spacing: Theme.spacing[2]
            Text { text: qsTr("Name"); Layout.preferredWidth: 60; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeLabelSize }
            Rectangle {
                Layout.fillWidth: true; Layout.preferredHeight: 24
                color: Theme.surfaceRaised; radius: Theme.radiusControl - 2
                border.color: nameField.activeFocus ? Theme.accentPrimary : Theme.borderSubtle
                TextInput {
                    id: nameField
                    anchors.fill: parent; anchors.margins: 5
                    verticalAlignment: TextInput.AlignVCenter
                    maximumLength: 60
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontTypeBodySize
                    onAccepted: root.create()
                }
            }
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: Theme.spacing[2]
            IconButton { implicitHeight: 26; label: qsTr("Cancel"); onClicked: root.close() }
            IconButton { implicitHeight: 26; label: qsTr("Create"); active: true; fillActive: true; fillText: Theme.textPrimary; onClicked: root.create() }
        }
    }
}
