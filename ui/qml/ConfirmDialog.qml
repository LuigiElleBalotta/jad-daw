import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Jad

// A question with a button that does it and one that does not.
Dialog {
    id: root
    property string heading: ""
    property string message: ""
    property string acceptLabel: qsTr("OK")
    signal confirmed()
    modal: true
    anchors.centerIn: parent
    width: 420
    background: Rectangle { color: Theme.surfacePanel; border.color: Theme.borderStrong; radius: Theme.radiusDialog }
    header: Item { height: 0 }
    function ask(title, text, label, then) {
        heading = title; message = text; acceptLabel = label
        root.confirmed.connect(function once() { root.confirmed.disconnect(once); then() })
        open()
    }
    onClosed: { /* a refused question forgets its answer */ }
    contentItem: ColumnLayout {
        spacing: Theme.spacing[3]
        Text { text: root.heading; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeTitleSize; font.weight: Theme.fontTypeTitleWeight }
        Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: root.message; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: Theme.spacing[2]
            IconButton { implicitHeight: 26; label: qsTr("Cancel"); onClicked: root.close() }
            IconButton { objectName: "acceptButton"; implicitHeight: 26; label: root.acceptLabel; active: true; fillActive: true; fillText: Theme.textPrimary; onClicked: { root.close(); root.confirmed() } }
        }
    }
}
