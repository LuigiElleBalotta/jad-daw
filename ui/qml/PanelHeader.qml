import QtQuick
import Jad

// A collapsible section header: chevron and title.
Rectangle {
    id: root
    property string title
    property bool expanded: true
    signal toggled()
    implicitHeight: 24
    color: Theme.surfaceRaised
    Text {
        id: chevron
        x: Theme.spacing[3]
        anchors.verticalCenter: parent.verticalCenter
        text: root.expanded ? "▾" : "▸"
        color: Theme.textSecondary
        font.pixelSize: Theme.fontTypeBodySize
    }
    Text {
        anchors.left: chevron.right
        anchors.leftMargin: Theme.spacing[3]
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        text: root.title
        elide: Text.ElideRight
        color: Theme.textPrimary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontTypeLabelSize
        font.weight: Theme.fontTypeLabelWeight
    }
    MouseArea {
        anchors.fill: parent
        onClicked: { root.expanded = !root.expanded; root.toggled() }
    }
}
