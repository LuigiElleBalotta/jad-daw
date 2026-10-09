import QtQuick
import Jad

// The volume field of a strip: shows the fader position in dB; one click edits it (the value selected), Enter or focus loss
// commits, Escape cancels. Logic's range ends at +6 dB; "-inf" or anything at the bottom of the range means -96.
Rectangle {
    id: root
    property real value: 0
    property real from: -96
    property real to: 6
    readonly property alias input: input
    signal committed(real db)

    implicitWidth: 44
    implicitHeight: 18
    radius: Theme.radiusControl - 2
    color: input.activeFocus ? Theme.surfaceRaisedHover : Theme.surfaceLcd
    border.color: input.activeFocus ? Theme.accentPrimary : Theme.borderSubtle

    function display(v) { return v <= root.from ? "-∞" : v.toFixed(1).replace(".", ",") }
    function parse(text) {
        const t = text.trim().toLowerCase().replace(",", ".")
        if (t === "-inf" || t === "-∞" || t === "inf") return root.from
        const v = parseFloat(t)
        return isFinite(v) ? Math.max(root.from, Math.min(root.to, v)) : NaN
    }

    TextInput {
        id: input
        anchors.fill: parent
        anchors.leftMargin: Theme.spacing[1]
        anchors.rightMargin: Theme.spacing[1]
        horizontalAlignment: TextInput.AlignHCenter
        verticalAlignment: TextInput.AlignVCenter
        color: Theme.textValue
        selectByMouse: false
        clip: true
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontTypeLabelSize
        text: root.display(root.value)
        onActiveFocusChanged: {
            if (activeFocus) selectAll()
            else text = Qt.binding(function () { return root.display(root.value) })
        }
        Keys.onShortcutOverride: (event) => { if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Escape) event.accepted = true }
        Keys.onEscapePressed: root.parent.forceActiveFocus()
        onAccepted: {
            const v = root.parse(text)
            if (!isNaN(v) && Math.abs(v - root.value) > 0.001) root.committed(v)
            root.parent.forceActiveFocus()
        }
    }
    MouseArea {  // one click puts the field in edit mode
        anchors.fill: parent
        enabled: !input.activeFocus
        onPressed: input.forceActiveFocus()
    }
}
