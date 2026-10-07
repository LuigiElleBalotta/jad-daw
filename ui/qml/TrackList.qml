import QtQuick
import Jad

Rectangle {
    id: root
    required property ProjectController project
    property real scrollY: 0
    property real rowHeight: Theme.sizeTrackHeight[1]
    property real headerHeight: 24

    color: Theme.surfacePanel

    Rectangle {
        id: header
        width: parent.width
        height: root.headerHeight
        color: Theme.surfacePanel
        Text {
            x: Theme.spacing[4]
            anchors.verticalCenter: parent.verticalCenter
            text: root.project.hasProject ? root.project.projectName : qsTr("No project")
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTypeLabelSize
            font.weight: Theme.fontTypeLabelWeight
            elide: Text.ElideRight
            width: parent.width - Theme.spacing[4] * 2
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.borderSubtle }
    }

    ListView {
        y: root.headerHeight
        width: parent.width
        height: parent.height - root.headerHeight
        clip: true
        interactive: false
        contentY: root.scrollY
        model: root.project.tracks
        delegate: Item {
            id: row
            required property string name
            required property string kind
            required property string color
            width: ListView.view.width
            height: root.rowHeight
            readonly property string capitalColor: color.charAt(0).toUpperCase() + color.slice(1)

            Rectangle {
                anchors.fill: parent
                color: "transparent"
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.borderSubtle }
            }
            Rectangle {
                id: chip
                x: Theme.spacing[3]
                anchors.verticalCenter: parent.verticalCenter
                width: 4
                height: parent.height - 16
                radius: 2
                color: Theme["track" + row.capitalColor + "Solid"]
            }
            Column {
                anchors.left: chip.right
                anchors.leftMargin: Theme.spacing[4]
                anchors.right: parent.right
                anchors.rightMargin: Theme.spacing[3]
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2
                Text {
                    width: parent.width
                    text: row.name
                    elide: Text.ElideRight
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontTypeBodySize
                    font.weight: Theme.fontTypeTitleWeight
                }
                Text {
                    text: row.kind
                    color: Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontTypeCaptionSize
                }
            }
        }
    }
}
