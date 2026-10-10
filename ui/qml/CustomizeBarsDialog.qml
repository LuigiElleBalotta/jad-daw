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
    readonly property var parts: section === "th"
        ? [{ key: "th.icon", label: qsTr("Track icon") }, { key: "th.arm", label: qsTr("Record Enable (R)") }, { key: "th.monitor", label: qsTr("Input Monitoring (I)") },
           { key: "th.mute", label: qsTr("Mute (M)") }, { key: "th.solo", label: qsTr("Solo (S)") }, { key: "th.sliders", label: qsTr("Volume and pan sliders") }]
        : section === "cb"
        ? [{ key: "cb.panels", label: qsTr("Panel buttons (Library, Inspector, Mixer, Editors …)") }, { key: "cb.transport", label: qsTr("Transport buttons") },
           { key: "cb.lcd", label: qsTr("LCD display") }, { key: "cb.modes", label: qsTr("Metronome, Count-in, Cycle and Punch") }, { key: "cb.master", label: qsTr("Master volume") }]
        : [{ key: "tb.menus", label: qsTr("Edit, Functions and View menus") }, { key: "tb.tools", label: qsTr("Tools") }, { key: "tb.snap", label: qsTr("Snap and Drag mode") },
           { key: "tb.heights", label: qsTr("Track height buttons") }, { key: "tb.zoom", label: qsTr("Zoom and Catch Playhead") }, { key: "tb.undo", label: qsTr("Undo and Redo") }]
    contentItem: ColumnLayout {
        spacing: Theme.spacing[2]
        Text { text: root.section === "th" ? qsTr("Configure Track Header") : (root.section === "cb" ? qsTr("Customize Control Bar and Display") : qsTr("Customize Toolbar")); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeTitleSize; font.weight: Theme.fontTypeTitleWeight }
        Repeater {
            model: root.parts
            delegate: RowLayout {
                required property var modelData
                spacing: Theme.spacing[2]
                FlagCheck {
                    objectName: "part_" + modelData.key
                    on: { root.project.barItemsRevision; return root.project.barItem(modelData.key) }
                    onFlipped: root.project.setBarItem(modelData.key, !on)
                }
                Text { text: modelData.label; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            IconButton { implicitHeight: 26; label: qsTr("Reset"); onClicked: root.project.resetBarItems() }
            Item { Layout.fillWidth: true }
            IconButton { implicitHeight: 26; label: qsTr("Close"); onClicked: root.close() }
        }
    }
}
