import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Jad

// The parameter of an audio operation on the selected regions (time stretch, pitch shift, strip silence...): a title, a number
// with its unit, Apply. `op` says what the controller does with it.
Dialog {
    id: root
    required property ProjectController project
    modal: true
    anchors.centerIn: parent
    width: 380
    property string op: "stretch"
    property string heading: ""
    property string label: ""
    property string unit: ""
    property real value: 100
    property real minValue: 0
    property real maxValue: 100
    property string secondLabel: ""      // strip silence has a second number: the shortest silence to cut
    property real secondValue: 100
    property bool hasSecond: false

    background: Rectangle { color: Theme.surfacePanel; border.color: Theme.borderStrong; radius: Theme.radiusDialog }
    header: Item { height: 0 }

    function show(operation, title, text, unit_, initial, min, max) {
        op = operation; heading = title; label = text; unit = unit_; value = initial; minValue = min; maxValue = max
        hasSecond = operation === "strip"
        if (hasSecond) { secondLabel = qsTr("Shortest silence"); secondValue = 100 }
        open()
    }
    function apply() {
        const v = Math.max(minValue, Math.min(maxValue, value))
        if (op === "strip") project.stripSilence(v, secondValue)
        else project.processSelectedRegions(op, v)
        close()
    }
    onOpened: { field.text = String(value); field.forceActiveFocus(); field.selectAll() }

    contentItem: ColumnLayout {
        spacing: Theme.spacing[3]
        Text { text: root.heading; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeTitleSize; font.weight: Theme.fontTypeTitleWeight }
        RowLayout {
            spacing: Theme.spacing[2]
            Text { text: root.label; Layout.preferredWidth: 130; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeLabelSize }
            Rectangle {
                Layout.preferredWidth: 90; Layout.preferredHeight: 24
                color: Theme.surfaceRaised; radius: Theme.radiusControl - 2
                border.color: field.activeFocus ? Theme.accentPrimary : Theme.borderSubtle
                TextInput {
                    id: field
                    objectName: "valueField"
                    anchors.fill: parent; anchors.margins: 5
                    verticalAlignment: TextInput.AlignVCenter
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontTypeBodySize
                    onTextEdited: { const v = parseFloat(text.replace(",", ".")); if (isFinite(v)) root.value = v }
                    onAccepted: root.apply()
                }
            }
            Text { text: root.unit; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeLabelSize }
        }
        RowLayout {
            visible: root.hasSecond
            spacing: Theme.spacing[2]
            Text { text: root.secondLabel; Layout.preferredWidth: 130; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeLabelSize }
            Rectangle {
                Layout.preferredWidth: 90; Layout.preferredHeight: 24
                color: Theme.surfaceRaised; radius: Theme.radiusControl - 2
                border.color: second.activeFocus ? Theme.accentPrimary : Theme.borderSubtle
                TextInput {
                    id: second
                    anchors.fill: parent; anchors.margins: 5
                    verticalAlignment: TextInput.AlignVCenter
                    text: String(root.secondValue)
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontTypeBodySize
                    onTextEdited: { const v = parseFloat(text.replace(",", ".")); if (isFinite(v)) root.secondValue = Math.max(0, v) }
                    onAccepted: root.apply()
                }
            }
            Text { text: "ms"; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeLabelSize }
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: Theme.spacing[2]
            IconButton { implicitHeight: 26; label: qsTr("Cancel"); onClicked: root.close() }
            IconButton { objectName: "applyButton"; implicitHeight: 26; label: qsTr("Apply"); active: true; fillActive: true; fillText: Theme.textPrimary; onClicked: root.apply() }
        }
    }
}
