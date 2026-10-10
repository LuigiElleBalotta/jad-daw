import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Jad

// File > Project Settings: the name, the sample rate (fixed when the project was made), the tempo and the time signature at the start,
// and the places of the other settings (the metronome, recording and audio ones).
Dialog {
    id: root
    required property ProjectController project
    signal metronomeRequested()
    signal preferencesRequested()
    modal: true
    anchors.centerIn: parent
    width: 460
    background: Rectangle { color: Theme.surfacePanel; border.color: Theme.borderStrong; radius: Theme.radiusDialog }
    header: Item { height: 0 }
    onAboutToShow: { nameField.text = project.projectName }

    component Row2: RowLayout {
        property string label
        default property alias content: slot.data
        spacing: Theme.spacing[3]
        Text { Layout.preferredWidth: 130; text: parent.label; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeLabelSize }
        RowLayout { id: slot; Layout.fillWidth: true; spacing: Theme.spacing[2] }
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacing[3]
        Text { text: qsTr("Project Settings"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeTitleSize; font.weight: Theme.fontTypeTitleWeight }
        Row2 {
            label: qsTr("Name")
            Rectangle {
                Layout.fillWidth: true; Layout.preferredHeight: 24
                color: Theme.surfaceRaised; radius: Theme.radiusControl - 2
                border.color: nameField.activeFocus ? Theme.accentPrimary : Theme.borderSubtle
                TextInput {
                    id: nameField
                    objectName: "projectName"
                    anchors.fill: parent; anchors.margins: 5
                    verticalAlignment: TextInput.AlignVCenter
                    maximumLength: 128
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontTypeBodySize
                    onEditingFinished: root.project.setProjectName(text)
                }
            }
        }
        Row2 { label: qsTr("Folder"); Text { Layout.fillWidth: true; text: root.project.projectFolder(); elide: Text.ElideMiddle; color: Theme.textValue; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize } }
        Row2 { label: qsTr("Sample rate"); Text { text: qsTr("%1 Hz (set when the project was made)").arg(root.project.sampleRateHz); color: Theme.textValue; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize } }
        Row2 { label: qsTr("Tempo"); Text { text: qsTr("%1 BPM, %2 (change them in the LCD)").arg(root.project.bpm).arg(root.project.signatureText); color: Theme.textValue; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize } }
        Row2 {
            label: qsTr("Count-in")
            Text { text: root.project.countInEnabled ? qsTr("On, %1").arg(root.project.countInChoice > 0 ? qsTr("%1 bar(s)").arg(root.project.countInChoice) : qsTr("%1/4").arg(-root.project.countInChoice)) : qsTr("Off"); color: Theme.textValue; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize }
        }
        Row2 {
            label: qsTr("Recording")
            CheckBox {
                objectName: "autoInputMonitoring"
                text: qsTr("Auto input monitoring")
                checked: root.project.autoInputMonitoring
                onToggled: root.project.autoInputMonitoring = checked
                contentItem: Text { leftPadding: 24; text: parent.text; color: Theme.textValue; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize; verticalAlignment: Text.AlignVCenter }
            }
        }
        RowLayout {
            spacing: Theme.spacing[2]
            IconButton { implicitHeight: 24; label: qsTr("Metronome Settings…"); onClicked: { root.close(); root.metronomeRequested() } }
            IconButton { implicitHeight: 24; label: qsTr("Audio and MIDI Preferences…"); onClicked: { root.close(); root.preferencesRequested() } }
        }
        IconButton { Layout.alignment: Qt.AlignRight; implicitHeight: 26; label: qsTr("Close"); onClicked: root.close() }
    }
}
