import QtQuick
import Jad

// A field that shows the chosen value and opens a menu of choices (the style of the Inspector's popups).
Rectangle {
    id: root
    property var choices: []                 // strings (or numbers); shown as they are, or through `format`
    property var value                       // the chosen one
    property var format: null                // choice -> text
    property string placeholder: ""
    signal chosen(var choice)

    implicitWidth: 200
    implicitHeight: 24
    radius: Theme.radiusControl - 2
    color: area.containsMouse ? Theme.surfaceRaisedHover : Theme.surfaceRaised
    border.color: Theme.borderSubtle

    function textOf(c) { return root.format !== null ? root.format(c) : String(c) }

    Text {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacing[2]
        anchors.rightMargin: 16
        verticalAlignment: Text.AlignVCenter
        text: root.value === undefined || root.value === null || root.value === "" ? root.placeholder : root.textOf(root.value)
        color: Theme.textValue
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontTypeBodySize
        elide: Text.ElideRight
    }
    Text {
        anchors.right: parent.right
        anchors.rightMargin: 6
        anchors.verticalCenter: parent.verticalCenter
        text: "▾"
        color: Theme.textSecondary
        font.pixelSize: Theme.fontTypeBodySize
    }
    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        enabled: root.enabled && root.choices.length > 0
        onClicked: menu.popup(root, 0, root.height)
    }
    ThemedMenu {
        id: menu
        Instantiator {
            model: root.choices
            delegate: ThemedMenuItem {
                required property var modelData
                text: root.textOf(modelData)
                onTriggered: root.chosen(modelData)
            }
            onObjectAdded: (index, object) => menu.insertItem(index, object)
            onObjectRemoved: (index, object) => menu.removeItem(object)
        }
    }
}
