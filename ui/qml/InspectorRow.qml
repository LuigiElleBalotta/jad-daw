import QtQuick
import Jad

// A label on the left, the control on the right.
Item {
    id: root
    property string label
    default property alias content: slot.data
    implicitHeight: 24
    width: parent ? parent.width : 200
    Text {
        x: Theme.spacing[3]
        width: parent.width * 0.42
        anchors.verticalCenter: parent.verticalCenter
        text: root.label
        elide: Text.ElideRight
        horizontalAlignment: Text.AlignRight
        color: Theme.textSecondary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontTypeLabelSize
    }
    Item {
        id: slot
        x: parent.width * 0.42 + Theme.spacing[4]
        width: parent.width - x - Theme.spacing[3]
        height: parent.height
    }
}
