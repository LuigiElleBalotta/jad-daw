import QtQuick
import Jad

// An editable number: shows `value` with one decimal and `suffix`; Enter or focus loss commits a valid number.
Rectangle {
    id: root
    property real value: 0
    property string suffix: ""
    property real from: -96
    property real to: 24
    signal committed(real value)
    implicitWidth: 72
    implicitHeight: 20
    radius: Theme.radiusControl - 2
    color: input.activeFocus ? Theme.surfaceRaisedHover : Theme.surfaceRaised
    border.color: input.activeFocus ? Theme.accentPrimary : Theme.borderSubtle
    TextInput {
        id: input
        anchors.fill: parent
        anchors.leftMargin: Theme.spacing[2]
        anchors.rightMargin: Theme.spacing[2]
        verticalAlignment: TextInput.AlignVCenter
        text: root.value.toFixed(1) + root.suffix
        color: Theme.textValue
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontTypeLabelSize
        selectByMouse: true
        onActiveFocusChanged: if (activeFocus) selectAll()
        onEditingFinished: {
            const parsed = parseFloat(text.replace(root.suffix, "").replace(",", "."))
            if (isFinite(parsed)) root.committed(Math.max(root.from, Math.min(root.to, parsed)))
            else text = Qt.binding(function () { return root.value.toFixed(1) + root.suffix })
        }
    }
}
