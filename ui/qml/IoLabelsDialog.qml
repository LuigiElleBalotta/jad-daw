import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Jad

// Mix > I/O Labels: names for the inputs of the audio interface (shown in the Input menu of the strips).
Dialog {
    id: root
    required property ProjectController project
    modal: true
    anchors.centerIn: parent
    width: 400
    height: Math.min(parent ? parent.height - 60 : 500, 460)
    background: Rectangle { color: Theme.surfacePanel; border.color: Theme.borderStrong; radius: Theme.radiusDialog }
    header: Item { height: 0 }
    standardButtons: Dialog.Close

    contentItem: ColumnLayout {
        spacing: Theme.spacing[3]
        Text { text: qsTr("I/O Labels"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeTitleSize; font.weight: Theme.fontTypeTitleWeight }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: qsTr("Names for the inputs of the audio interface. Leave a field empty to use the default name.")
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTypeLabelSize
        }
        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: Math.max(root.project.inputChannels, 2)
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            delegate: RowLayout {
                required property int index
                width: ListView.view.width - 10
                height: 30
                spacing: Theme.spacing[2]
                Text { Layout.preferredWidth: 60; text: qsTr("Input %1").arg(index + 1); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeLabelSize }
                Rectangle {
                    Layout.fillWidth: true; Layout.preferredHeight: 24
                    color: Theme.surfaceRaised; radius: Theme.radiusControl - 2
                    border.color: field.activeFocus ? Theme.accentPrimary : Theme.borderSubtle
                    TextInput {
                        id: field
                        objectName: "labelField" + (index + 1)
                        anchors.fill: parent; anchors.margins: 5
                        verticalAlignment: TextInput.AlignVCenter
                        maximumLength: 32
                        text: { root.project.inputLabelsRevision; const l = root.project.inputLabel(index + 1); return l === qsTr("Input %1").arg(index + 1) ? "" : l }
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontTypeBodySize
                        onEditingFinished: root.project.setInputLabel(index + 1, text)
                    }
                }
            }
        }
    }
}
