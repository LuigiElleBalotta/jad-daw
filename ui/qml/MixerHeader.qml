import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Jad

// The local menu bar of the Mixer: Edit / Options / View menus, Single | Tracks | All, and the strip type filters.
// The menu entries follow Logic's (docs/logic-reference/mixer.md); the ones without a function say so when used.
Item {
    id: root
    required property ProjectController project
    property string scope: "tracks"                      // "single", "tracks" or "all"
    property var hiddenTypes: ({})                         // type id -> true when its filter button is off
    readonly property var types: [
        { id: "audio", label: qsTr("Audio") }, { id: "instrument", label: qsTr("Inst") }, { id: "aux", label: qsTr("Aux") },
        { id: "bus", label: qsTr("Bus") }, { id: "input", label: qsTr("Input") }, { id: "output", label: qsTr("Output") },
        { id: "master", label: qsTr("Master/VCA") }, { id: "midi", label: qsTr("MIDI") }]
    property bool longFaders: false
    property bool legendHidden: false
    signal scopeSelected(string scope)
    signal typeToggled(string typeId, bool visible)
    signal onlyTypeRequested(string typeId)               // Option-click: only this type

    implicitHeight: 24

    function stub(label) { project.announceStub(label) }

    component Entry: ThemedMenuItem {
        property string stubLabel: text
        onTriggered: root.stub(stubLabel)
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacing[3]
        anchors.rightMargin: Theme.spacing[3]
        spacing: Theme.spacing[3]

        IconButton {
            implicitHeight: 20
            label: qsTr("Edit ▾")
            onClicked: editMenu.popup(0, height)
            ThemedMenu {
                id: editMenu
                Entry { text: qsTr("Undo"); onTriggered: root.project.undo() }
                Entry { text: qsTr("Redo"); onTriggered: root.project.redo() }
                MenuSeparator {}
                Entry { text: qsTr("Mixer Undo") }
                Entry { text: qsTr("Mixer Redo") }
                Entry { text: qsTr("Undo selected Channel Strips") }
                Entry { text: qsTr("Redo selected Channel Strips") }
                Entry { text: qsTr("Delete Mixer Undo History") }
                Entry { text: qsTr("Include Mixer Undo Steps in Project Undo History"); checkable: true; checked: true }
                MenuSeparator {}
                Entry { text: qsTr("Select All"); onTriggered: root.project.selectAll() }
                Entry { text: qsTr("Deselect All"); onTriggered: root.project.clearSelection() }
                Entry { text: qsTr("Invert Selection") }
                Entry { text: qsTr("Select Audio Channel Strips") }
                Entry { text: qsTr("Select Instrument Channel Strips") }
                Entry { text: qsTr("Select Auxiliary Channel Strips") }
                Entry { text: qsTr("Select Output Channel Strips") }
                Entry { text: qsTr("Select Muted Channel Strips") }
            }
        }
        IconButton {
            implicitHeight: 20
            label: qsTr("Options ▾")
            onClicked: optionsMenu.popup(0, height)
            ThemedMenu {
                id: optionsMenu
                Entry { text: qsTr("Create New Auxiliary Channel Strip"); onTriggered: root.project.addTrack("bus") }
                Entry { text: qsTr("Create New VCA for Selected Channel Strips") }
                Entry { text: qsTr("Create Tracks for Selected Channel Strips") }
                Entry { text: qsTr("Create Track Stack for Selected Channel Strips") }
                Entry { text: qsTr("Send All MIDI Mixer Data") }
                MenuSeparator {}
                Entry { text: qsTr("Enable Groups"); checkable: true; checked: true }
                Entry { text: qsTr("I/O Labels…") }
            }
        }
        IconButton {
            implicitHeight: 20
            label: qsTr("View ▾")
            onClicked: viewMenu.popup(0, height)
            ThemedMenu {
                id: viewMenu
                Entry { text: root.legendHidden ? qsTr("Show Legend") : qsTr("Hide Legend"); onTriggered: root.legendHidden = !root.legendHidden }
                Entry { text: qsTr("Link Control Surfaces"); checkable: true; checked: true }
                Entry { text: qsTr("Autoscroll to Selection"); checkable: true; checked: true }
                MenuSeparator {}
                Entry { text: qsTr("Signal Flow Channel Strips"); checkable: true; checked: true }
                Entry { text: qsTr("Channels with Sends only"); checkable: true }
                Entry { text: qsTr("Folder Tracks"); checkable: true; checked: true }
                Entry { text: qsTr("Other Tracks"); checkable: true }
                Entry { text: qsTr("All Tracks with Same Channel Strip/Instrument"); checkable: true }
                MenuSeparator {}
                Entry { text: qsTr("Long Faders"); checkable: true; checked: root.longFaders; onTriggered: root.longFaders = !root.longFaders }
                Entry { text: qsTr("Configure Channel Strip Components…") }
            }
        }

        Item { Layout.fillWidth: true }

        Row {  // Single | Tracks | All
            spacing: 0
            Repeater {
                model: [{ id: "single", label: qsTr("Single") }, { id: "tracks", label: qsTr("Tracks") }, { id: "all", label: qsTr("All") }]
                delegate: IconButton {
                    required property var modelData
                    implicitHeight: 20
                    label: modelData.label
                    active: root.scope === modelData.id
                    fillActive: true
                    fillText: Theme.textPrimary
                    onClicked: root.scopeSelected(modelData.id)
                }
            }
        }

        Item { Layout.fillWidth: true }

        Row {  // the type filters: lit = shown; Option-click shows only that type
            spacing: Theme.spacing[1]
            Repeater {
                model: root.types
                delegate: IconButton {
                    id: filter
                    required property var modelData
                    implicitHeight: 20
                    label: modelData.label
                    active: root.hiddenTypes[modelData.id] !== true
                    fillActive: true
                    fillText: Theme.textPrimary
                    MouseArea {
                        anchors.fill: parent
                        onClicked: (m) => {
                            if (m.modifiers & Qt.AltModifier) root.onlyTypeRequested(filter.modelData.id)
                            else root.typeToggled(filter.modelData.id, !filter.active)
                        }
                    }
                }
            }
        }
    }
}
