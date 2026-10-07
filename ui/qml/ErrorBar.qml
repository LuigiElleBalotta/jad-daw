import QtQuick
import QtQuick.Layouts
import Jad

Rectangle {
    id: root
    property string message
    signal dismissed()

    implicitHeight: 32
    color: Qt.rgba(Theme.stateRecord.r, Theme.stateRecord.g, Theme.stateRecord.b, 0.25)
    border.color: Theme.stateRecord
    border.width: 1

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacing[4]
        anchors.rightMargin: Theme.spacing[2]
        Text {
            Layout.fillWidth: true
            text: root.message
            elide: Text.ElideRight
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTypeBodySize
        }
        Text {
            text: "✕"
            color: Theme.textPrimary
            font.pixelSize: Theme.fontTypeBodySize
            MouseArea {
                anchors.fill: parent
                anchors.margins: -6
                onClicked: root.dismissed()
            }
        }
    }
}
