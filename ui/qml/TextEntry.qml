import QtQuick
import Jad

// A line of text in the style of the Inspector's fields: shows `text`, and says when an edit is finished (Return or focus lost).
Rectangle {
    id: root
    property alias text: input.text
    property alias input: input
    signal edited(string text)
    implicitWidth: 160
    implicitHeight: 24
    radius: Theme.radiusControl - 2
    color: input.activeFocus ? Theme.surfaceRaisedHover : Theme.surfaceRaised
    border.color: input.activeFocus ? Theme.accentPrimary : Theme.borderSubtle
    TextInput {
        id: input
        anchors.fill: parent
        anchors.leftMargin: Theme.spacing[2]
        anchors.rightMargin: Theme.spacing[2]
        verticalAlignment: TextInput.AlignVCenter
        color: Theme.textValue
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontTypeBodySize
        selectByMouse: true
        clip: true
        onEditingFinished: root.edited(text)
    }
}
