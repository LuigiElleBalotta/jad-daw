import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Jad

// Window > Open Project Audio: the audio files kept in the project folder and how many regions play each one.
Dialog {
    id: root
    required property ProjectController project
    modal: false
    anchors.centerIn: parent
    width: 560
    height: 400
    property var list: []
    function reload() { list = project.projectAudio() }
    onAboutToShow: reload()
    Connections { target: root.project; function onProjectChanged() { if (root.visible) root.reload() } }
    background: Rectangle { color: Theme.surfacePanel; border.color: Theme.borderStrong; radius: Theme.radiusDialog }
    header: Item { height: 0 }
    contentItem: ColumnLayout {
        spacing: Theme.spacing[3]
        Text { text: qsTr("Project Audio"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeTitleSize; font.weight: Theme.fontTypeTitleWeight }
        Text { visible: root.list.length === 0; text: qsTr("The project has no audio files yet."); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize }
        ListView {
            objectName: "projectAudioList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.list
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            delegate: RowLayout {
                id: row
                required property var modelData
                width: ListView.view.width - 10
                height: 26
                spacing: Theme.spacing[2]
                Text { Layout.fillWidth: true; text: row.modelData.name; elide: Text.ElideMiddle; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize }
                Text { Layout.preferredWidth: 70; text: row.modelData.seconds.toFixed(1) + " s"; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize }
                Text { Layout.preferredWidth: 110; text: row.modelData.sampleRate + " Hz, " + (row.modelData.channels === 1 ? qsTr("mono") : qsTr("stereo")); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize }
                Text { Layout.preferredWidth: 70; text: row.modelData.used === 0 ? qsTr("unused") : qsTr("%n region(s)", "", row.modelData.used); color: row.modelData.used === 0 ? Theme.textSecondary : Theme.textValue; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            IconButton { implicitHeight: 26; label: qsTr("Close"); onClicked: root.close() }
        }
    }
}
