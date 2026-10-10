import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Jad

// View > Customize Control Bar and Display / Customize Toolbar: which parts of the two bars are shown.
Dialog {
    id: root
    required property ProjectController project
    property string section: "cb"            // "cb": the control bar, "tb": the toolbar of the tracks area
    modal: true
    anchors.centerIn: parent
    width: 380
    background: Rectangle { color: Theme.surfacePanel; border.color: Theme.borderStrong; radius: Theme.radiusDialog }
    header: Item { height: 0 }
    readonly property var parts: section === "cb"
        ? [{ key: "cb.panels", label: qsTr("Panel buttons (Library, Inspector, Mixer, Editors …)") }, { key: "cb.transport", label: qsTr("Transport buttons") },
           { key: "cb.lcd", label: qsTr("LCD display") }, { key: "cb.modes", label: qsTr("Metronome, Count-in, Cycle and Punch") }, { key: "cb.master", label: qsTr("Master volume") }]
        : [{ key: "tb.menus", label: qsTr("Edit, Functions and View menus") }, { key: "tb.tools", label: qsTr("Tools") }, { key: "tb.snap", label: qsTr("Snap and Drag mode") },
           { key: "tb.heights", label: qsTr("Track height buttons") }, { key: "tb.zoom", label: qsTr("Zoom and Catch Playhead") }, { key: "tb.undo", label: qsTr("Undo and Redo") }]
    contentItem: ColumnLayout {
        spacing: Theme.spacing[2]
        Text { text: root.section === "cb" ? qsTr("Customize Control Bar and Display") : qsTr("Customize Toolbar"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeTitleSize; font.weight: Theme.fontTypeTitleWeight }
        Repeater {
            model: root.parts
            delegate: CheckBox {
                required property var modelData
                objectName: "part_" + modelData.key
                text: modelData.label
                checked: { root.project.barItemsRevision; return root.project.barItem(modelData.key) }
                onToggled: root.project.setBarItem(modelData.key, checked)
                contentItem: Text { leftPadding: 26; text: parent.text; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize; verticalAlignment: Text.AlignVCenter }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Button { text: qsTr("Reset"); onClicked: root.project.resetBarItems() }
            Item { Layout.fillWidth: true }
            Button { text: qsTr("Close"); onClicked: root.close() }
        }
    }
}
