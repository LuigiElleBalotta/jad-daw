import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Jad

// File > New from Template: the templates saved with Save as Template; choosing one asks for the folder of the new project.
Dialog {
    id: root
    required property ProjectController project
    signal chosen(string name)
    modal: true
    anchors.centerIn: parent
    width: 380
    height: 360
    property var list: []
    property string picked: ""
    onAboutToShow: { list = project.templates(); picked = "" }
    background: Rectangle { color: Theme.surfacePanel; border.color: Theme.borderStrong; radius: Theme.radiusDialog }
    header: Item { height: 0 }
    contentItem: ColumnLayout {
        spacing: Theme.spacing[3]
        Text { text: qsTr("New from Template"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeTitleSize; font.weight: Theme.fontTypeTitleWeight }
        Text { visible: root.list.length === 0; text: qsTr("No templates yet: open a project and choose Save as Template."); color: Theme.textSecondary; wrapMode: Text.Wrap; Layout.fillWidth: true; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize }
        ListView {
            objectName: "templateList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.list
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            delegate: Rectangle {
                required property string modelData
                width: ListView.view.width - 10
                height: 26
                radius: 3
                color: root.picked === modelData ? Theme.accentPrimary : (area.containsMouse ? Theme.surfaceRaisedHover : "transparent")
                Text { anchors.fill: parent; anchors.leftMargin: Theme.spacing[2]; verticalAlignment: Text.AlignVCenter; text: modelData; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize }
                MouseArea {
                    id: area
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: root.picked = modelData
                    onDoubleClicked: { root.picked = modelData; root.chosen(modelData); root.close() }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            Button { text: qsTr("Cancel"); onClicked: root.close() }
            Button { objectName: "chooseButton"; text: qsTr("Choose Folder…"); enabled: root.picked !== ""; onClicked: { root.chosen(root.picked); root.close() } }
        }
    }
}
