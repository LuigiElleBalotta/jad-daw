import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Jad

// Window > Open MIDI Transform (also in the Piano Roll's Functions menu): one change applied to every note of the selected MIDI regions.
Dialog {
    id: root
    required property ProjectController project
    modal: false
    anchors.centerIn: parent
    width: 400
    property string op: "transpose"
    readonly property var ops: [
        { id: "transpose", label: qsTr("Transpose"), a: qsTr("Semitones"), av: 12, amin: -48, amax: 48, ad: 0, b: "" },
        { id: "velocityScale", label: qsTr("Scale Velocity"), a: qsTr("Percent"), av: 100, amin: 1, amax: 400, ad: 0, b: "" },
        { id: "velocityAdd", label: qsTr("Add to Velocity"), a: qsTr("Amount"), av: 10, amin: -127, amax: 127, ad: 0, b: "" },
        { id: "lengthScale", label: qsTr("Scale Length"), a: qsTr("Percent"), av: 50, amin: 1, amax: 1000, ad: 0, b: "" },
        { id: "humanize", label: qsTr("Humanize"), a: qsTr("Timing (1/960 beat)"), av: 30, amin: 0, amax: 480, ad: 0, b: qsTr("Velocity") },
        { id: "reverse", label: qsTr("Reverse"), a: "", av: 0, amin: 0, amax: 0, ad: 0, b: "" },
        { id: "invert", label: qsTr("Invert Pitches"), a: "", av: 0, amin: 0, amax: 0, ad: 0, b: "" }]
    readonly property var current: { for (const o of ops) if (o.id === op) return o; return ops[0] }
    onOpChanged: { first.value = current.av; second.value = 10 }
    background: Rectangle { color: Theme.surfacePanel; border.color: Theme.borderStrong; radius: Theme.radiusDialog }
    header: Item { height: 0 }
    contentItem: ColumnLayout {
        spacing: Theme.spacing[3]
        Text { text: qsTr("MIDI Transform"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeTitleSize; font.weight: Theme.fontTypeTitleWeight }
        Text {
            text: root.project.selectedRegionIds.length === 0 ? qsTr("Select MIDI regions first.") : qsTr("Applies to every note of the selected regions.")
            color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize
        }
        SelectField {
            objectName: "transformOp"
            Layout.fillWidth: true
            choices: root.ops.map(o => o.id)
            value: root.op
            format: (id) => root.ops.find(o => o.id === id).label
            onChosen: (c) => root.op = c
        }
        RowLayout {
            visible: root.current.a !== ""
            Text { Layout.preferredWidth: 150; text: root.current.a; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize }
            NumberField { id: first; objectName: "transformA"; from: root.current.amin; to: root.current.amax; decimals: 0; value: 12; onCommitted: (v) => value = v }
        }
        RowLayout {
            visible: root.current.b !== ""
            Text { Layout.preferredWidth: 150; text: root.current.b; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize }
            NumberField { id: second; objectName: "transformB"; from: 0; to: 127; decimals: 0; value: 10; onCommitted: (v) => value = v }
        }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            IconButton { implicitHeight: 26; label: qsTr("Close"); onClicked: root.close() }
            IconButton { implicitHeight: 26; objectName: "applyButton"; label: qsTr("Apply"); enabled: root.project.selectedRegionIds.length > 0; onClicked: root.project.transformNotes(root.op, first.value, second.value) }
        }
    }
}
