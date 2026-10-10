import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Jad

// A small dialog that asks for a line of text (Save as Template …).
Dialog {
    id: root
    property string heading
    property string prompt
    property string text: ""
    signal accepted2(string text)
    modal: true
    anchors.centerIn: parent
    width: 380
    background: Rectangle { color: Theme.surfacePanel; border.color: Theme.borderStrong; radius: Theme.radiusDialog }
    header: Item { height: 0 }
    onAboutToShow: { field.text = root.text; field.forceActiveFocus(); field.selectAll() }
    function commit() {
        if (field.text.trim() === "") return
        root.accepted2(field.text.trim())
        root.close()
    }
    contentItem: ColumnLayout {
        spacing: Theme.spacing[3]
        Text { text: root.heading; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeTitleSize; font.weight: Theme.fontTypeTitleWeight }
        Text { text: root.prompt; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 26
            color: Theme.surfaceRaised
            radius: Theme.radiusControl - 2
            border.color: field.activeFocus ? Theme.accentPrimary : Theme.borderSubtle
            TextInput {
                id: field
                objectName: "textField"
                anchors.fill: parent
                anchors.margins: 5
                verticalAlignment: TextInput.AlignVCenter
                color: Theme.textValue
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTypeBodySize
                selectByMouse: true
                onAccepted: root.commit()
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            Button { text: qsTr("Cancel"); onClicked: root.close() }
            Button { objectName: "okButton"; text: qsTr("OK"); onClicked: root.commit() }
        }
    }
}
