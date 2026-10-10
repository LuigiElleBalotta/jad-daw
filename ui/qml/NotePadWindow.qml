import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Jad

// View > Note Pads: free text kept with the project (notes.txt in the project folder).
Dialog {
    id: root
    required property ProjectController project
    modal: false
    anchors.centerIn: parent
    width: 460
    height: 380
    onAboutToShow: pad.text = project.projectNotes()
    onClosed: project.setProjectNotes(pad.text)
    background: Rectangle { color: Theme.surfacePanel; border.color: Theme.borderStrong; radius: Theme.radiusDialog }
    header: Item { height: 0 }
    contentItem: ColumnLayout {
        spacing: Theme.spacing[3]
        Text { text: qsTr("Note Pad"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeTitleSize; font.weight: Theme.fontTypeTitleWeight }
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            TextArea {
                id: pad
                objectName: "notePad"
                wrapMode: TextArea.Wrap
                color: Theme.textPrimary
                selectByMouse: true
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTypeBodySize
                background: Rectangle { color: Theme.surfaceRaised; radius: Theme.radiusControl - 2 }
                onTextChanged: if (activeFocus) root.project.setProjectNotes(text)
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            Button { text: qsTr("Close"); onClicked: root.close() }
        }
    }
}
