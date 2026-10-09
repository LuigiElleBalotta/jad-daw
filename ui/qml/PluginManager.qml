import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Jad

// The Plug-in Manager: what the scan found, what failed and why, and the rescan buttons.
Dialog {
    id: root
    required property ProjectController project
    modal: true
    title: qsTr("Plug-in Manager")
    anchors.centerIn: parent
    width: Math.min(760, parent ? parent.width - 48 : 760)
    height: Math.min(520, parent ? parent.height - 48 : 520)

    background: Rectangle {
        color: Theme.surfacePanel
        border.color: Theme.borderStrong
        radius: Theme.radiusDialog
    }
    header: Item { height: 0 }

    contentItem: ColumnLayout {
        spacing: Theme.spacing[3]
        Text {
            text: qsTr("Plug-in Manager")
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTypeBodySize
            font.weight: Theme.fontTypeLabelWeight
        }
        Text {
            visible: !root.project.plugins.supported
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: root.project.audioEnabled ? qsTr("Plug-in hosting is not available in this build.")
                                            : qsTr("Plug-in hosting is off because audio output is disabled (--no-audio).")
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTypeLabelSize
        }
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.project.plugins
            headerPositioning: ListView.OverlayHeader
            header: Rectangle {
                width: list.width
                height: 22
                color: Theme.surfaceRaised
                z: 2
                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    leftPadding: Theme.spacing[2]
                    spacing: Theme.spacing[2]
                    Repeater {
                        model: [qsTr("Name"), qsTr("Vendor"), qsTr("Status"), qsTr("Path / reason")]
                        Text {
                            required property string modelData
                            required property int index
                            width: [180, 140, 60, 340][index]
                            text: modelData
                            color: Theme.textSecondary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontTypeLabelSize
                        }
                    }
                }
            }
            delegate: Rectangle {
                id: row
                required property int index
                required property string name
                required property string vendor
                required property string status
                required property string path
                required property string reason
                width: list.width
                height: 22
                color: index % 2 ? "transparent" : Theme.surfaceRaised
                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    leftPadding: Theme.spacing[2]
                    spacing: Theme.spacing[2]
                    Text { width: 180; text: row.name; elide: Text.ElideRight; color: Theme.textPrimary
                           font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeLabelSize }
                    Text { width: 140; text: row.vendor; elide: Text.ElideRight; color: Theme.textSecondary
                           font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeLabelSize }
                    Text { width: 60; text: row.status === "ok" ? qsTr("OK") : qsTr("Failed")
                           color: row.status === "ok" ? Theme.textValue : Theme.stateClip
                           font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeLabelSize }
                    Text { width: 340; text: row.status === "ok" ? row.path : row.reason + " - " + row.path
                           elide: Text.ElideMiddle; color: Theme.textSecondary
                           font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeLabelSize }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing[2]
            IconButton { implicitWidth: 72; implicitHeight: 24; label: qsTr("Rescan")
                         enabled: root.project.plugins.supported && !root.project.plugins.scanning
                         onClicked: root.project.plugins.rescanNew() }
            IconButton { implicitWidth: 96; implicitHeight: 24; label: qsTr("Rescan failed")
                         enabled: root.project.plugins.supported && !root.project.plugins.scanning
                         onClicked: root.project.plugins.rescanFailed() }
            IconButton { implicitWidth: 84; implicitHeight: 24; label: qsTr("Rescan all")
                         enabled: root.project.plugins.supported && !root.project.plugins.scanning
                         onClicked: root.project.plugins.rescanAll() }
            Text { Layout.fillWidth: true; text: root.project.plugins.scanText; color: Theme.textSecondary
                   font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeLabelSize }
            IconButton { implicitWidth: 72; implicitHeight: 24; label: qsTr("Close"); onClicked: root.close() }
        }
    }
}
