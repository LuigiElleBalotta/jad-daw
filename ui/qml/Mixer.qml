import QtQuick
import QtQuick.Layouts
import Jad

Rectangle {
    id: root
    required property ProjectController project
    property bool expanded: true
    readonly property real headerHeight: 24
    readonly property real stripHeight: 420

    color: Theme.surfaceCanvas
    implicitHeight: expanded ? headerHeight + stripHeight : headerHeight
    clip: true

    Rectangle {
        id: header
        width: parent.width
        height: root.headerHeight
        color: Theme.surfacePanel
        Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.borderSubtle }
        Row {
            anchors.verticalCenter: parent.verticalCenter
            x: Theme.spacing[3]
            spacing: Theme.spacing[3]
            IconButton {
                implicitWidth: 20
                implicitHeight: 20
                source: root.expanded ? "icons/chevron-down.svg" : "icons/chevron-right.svg"
                onClicked: root.expanded = !root.expanded
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Mixer")
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTypeLabelSize
                font.weight: Theme.fontTypeLabelWeight
            }
        }
    }

    Flickable {
        visible: root.expanded
        y: root.headerHeight
        width: parent.width
        height: root.stripHeight
        contentWidth: strips.width + Theme.spacing[4] * 2
        contentHeight: height
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Row {
            id: strips
            x: Theme.spacing[4]
            y: Theme.spacing[3]
            height: parent.height - Theme.spacing[3] * 2
            spacing: Theme.spacing[2]

            // tracks first, the master strip last
            Repeater {
                model: root.project.mixer
                delegate: ProjectStrip {
                    required property var model
                    visible: !model.isMaster
                    width: visible ? implicitWidth : 0
                    height: strips.height
                    project: root.project
                    info: model.info
                }
            }
            Repeater {
                model: root.project.mixer
                delegate: ProjectStrip {
                    required property var model
                    visible: model.isMaster
                    width: visible ? implicitWidth : 0
                    height: strips.height
                    project: root.project
                    info: model.info
                    peak: root.project.masterPeak
                }
            }
        }
    }
}
