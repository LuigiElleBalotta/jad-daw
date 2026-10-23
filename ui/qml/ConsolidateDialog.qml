import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Jad

// File > Project Management > Consolidate…: the audio the project refers to from outside is copied into the project, for the kinds that are ticked.
Dialog {
    id: root
    required property ProjectController project
    property bool audio: true
    property bool sampler: true
    modal: false
    anchors.centerIn: parent
    width: 400
    onAboutToShow: { audio = true; sampler = true }
    background: Rectangle { color: Theme.surfacePanel; border.color: Theme.borderStrong; radius: Theme.radiusDialog }
    header: Item { height: 0 }
    contentItem: ColumnLayout {
        spacing: Theme.spacing[3]
        Text { text: qsTr("Consolidate Project"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeTitleSize; font.weight: Theme.fontTypeTitleWeight }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: root.project.externalMediaCount() > 0
                  ? qsTr("%1 audio file(s) are kept outside the project. Copy them into it, so that the project can be moved or archived.").arg(root.project.externalMediaCount())
                  : qsTr("Every audio file of the project is already inside it.")
            color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize
        }
        ThemedCheckBox { objectName: "consolidateAudio"; text: qsTr("Audio files"); checked: root.audio; onToggled: root.audio = checked }
        ThemedCheckBox { objectName: "consolidateSampler"; text: qsTr("Sampler instruments audio data"); checked: root.sampler; onToggled: root.sampler = checked }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            IconButton { implicitHeight: 26; label: qsTr("Cancel"); onClicked: root.close() }
            IconButton {
                objectName: "okButton"; implicitHeight: 26; label: qsTr("OK"); active: true; fillActive: true; fillText: Theme.textPrimary
                onClicked: {
                    const n = root.project.consolidateMedia(root.audio, root.sampler)
                    root.project.notice(n > 0 ? qsTr("%1 audio file(s) copied into the project").arg(n) : qsTr("Nothing to copy"))
                    root.close()
                }
            }
        }
    }
}
