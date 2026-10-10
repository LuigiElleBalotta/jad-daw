import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Jad

// A small dialog that asks for one number (Edit > Repeat > Multiple, Edit > Length > Change).
Dialog {
    id: root
    property string heading
    property string prompt
    property real value: 1
    property real from: 1
    property real to: 64
    property int decimals: 0
    signal accepted2(real value)
    modal: true
    anchors.centerIn: parent
    width: 340
    background: Rectangle { color: Theme.surfacePanel; border.color: Theme.borderStrong; radius: Theme.radiusDialog }
    header: Item { height: 0 }
    onAboutToShow: { field.text = root.value.toFixed(root.decimals); field.forceActiveFocus(); field.selectAll() }
    function commit() {
        const v = parseFloat(field.text.replace(",", "."))
        if (!isFinite(v)) return
        root.accepted2(Math.max(root.from, Math.min(root.to, v)))
        root.close()
    }
    contentItem: ColumnLayout {
        spacing: Theme.spacing[3]
        Text { text: root.heading; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeTitleSize; font.weight: Theme.fontTypeTitleWeight }
        RowLayout {
            spacing: Theme.spacing[3]
            Text { text: root.prompt; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize }
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 26
                color: Theme.surfaceRaised
                radius: Theme.radiusControl - 2
                border.color: field.activeFocus ? Theme.accentPrimary : Theme.borderSubtle
                TextInput {
                    id: field
                    objectName: "numberField"
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
        }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            Button { text: qsTr("Cancel"); onClicked: root.close() }
            Button { objectName: "okButton"; text: qsTr("OK"); onClicked: root.commit() }
        }
    }
}
