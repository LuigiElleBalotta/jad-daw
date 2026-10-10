import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Jad

// Edit > Copy MIDI Events: the events between the locators go to the playhead, merged, replacing what is there or pushing it to the right.
Dialog {
    id: root
    required property ProjectController project
    property string mode: "copyMerge"
    property string dest: ""
    readonly property var modes: [
        { id: "copyMerge", label: qsTr("Copy Merge") }, { id: "copyReplace", label: qsTr("Copy Replace") }, { id: "copyInsert", label: qsTr("Copy Insert") },
        { id: "moveMerge", label: qsTr("Move Merge") }, { id: "moveReplace", label: qsTr("Move Replace") }, { id: "moveInsert", label: qsTr("Move Insert") }]
    property var tracksList: []
    modal: false
    anchors.centerIn: parent
    width: 420
    onAboutToShow: { tracksList = project.midiTrackChoices(); dest = "" }
    background: Rectangle { color: Theme.surfacePanel; border.color: Theme.borderStrong; radius: Theme.radiusDialog }
    header: Item { height: 0 }
    contentItem: ColumnLayout {
        spacing: Theme.spacing[3]
        Text { text: qsTr("Copy MIDI Events"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeTitleSize; font.weight: Theme.fontTypeTitleWeight }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: qsTr("The events between the locators of the selected MIDI regions go to the playhead.")
            color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize
        }
        RowLayout {
            Text { Layout.preferredWidth: 110; text: qsTr("Mode"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize }
            SelectField {
                objectName: "copyMode"
                Layout.fillWidth: true
                choices: root.modes.map(m => m.id)
                value: root.mode
                format: (id) => root.modes.find(m => m.id === id).label
                onChosen: (c) => root.mode = c
            }
        }
        RowLayout {
            Text { Layout.preferredWidth: 110; text: qsTr("To track"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize }
            SelectField {
                objectName: "copyDest"
                Layout.fillWidth: true
                choices: [""].concat(root.tracksList.map(t => t.id))
                value: root.dest
                format: (id) => id === "" ? qsTr("Selected track") : root.tracksList.find(t => t.id === id).name
                onChosen: (c) => root.dest = c
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            IconButton { implicitHeight: 26; label: qsTr("Cancel"); onClicked: root.close() }
            IconButton { objectName: "okButton"; implicitHeight: 26; label: qsTr("OK"); active: true; fillActive: true; fillText: Theme.textPrimary; onClicked: { root.project.copyMidiEvents(root.mode, root.dest); root.close() } }
        }
    }
}
