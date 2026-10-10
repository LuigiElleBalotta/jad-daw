import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Jad

// File > Open Recent: the projects opened before, the latest first.
Dialog {
    id: root
    required property ProjectController project
    modal: true
    anchors.centerIn: parent
    width: 520
    height: 360
    property var list: []
    onAboutToShow: list = project.recentProjects()
    Connections { target: root.project; function onRecentChanged() { root.list = root.project.recentProjects() } }
    background: Rectangle { color: Theme.surfacePanel; border.color: Theme.borderStrong; radius: Theme.radiusDialog }
    header: Item { height: 0 }
    contentItem: ColumnLayout {
        spacing: Theme.spacing[3]
        Text { text: qsTr("Open Recent"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeTitleSize; font.weight: Theme.fontTypeTitleWeight }
        Text { visible: root.list.length === 0; text: qsTr("No recent projects yet."); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize }
        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.list
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            delegate: Rectangle {
                required property string modelData
                width: ListView.view.width - 10
                height: 30
                radius: 3
                color: area.containsMouse ? Theme.surfaceRaisedHover : "transparent"
                Text {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spacing[2]
                    verticalAlignment: Text.AlignVCenter
                    text: modelData
                    color: Theme.textValue
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontTypeBodySize
                    elide: Text.ElideMiddle
                }
                MouseArea { id: area; anchors.fill: parent; hoverEnabled: true; onClicked: { root.close(); root.project.openRecent(modelData) } }
            }
        }
        IconButton { Layout.alignment: Qt.AlignRight; implicitHeight: 26; label: qsTr("Close"); onClicked: root.close() }
    }
}
