import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Jad

// File > Page Setup…: the paper and the orientation of what File > Print… writes (a PDF file of the arrangement and the table of the tracks).
Dialog {
    id: root
    required property ProjectController project
    modal: false
    anchors.centerIn: parent
    width: 360
    background: Rectangle { color: Theme.surfacePanel; border.color: Theme.borderStrong; radius: Theme.radiusDialog }
    header: Item { height: 0 }
    contentItem: ColumnLayout {
        spacing: Theme.spacing[3]
        Text { text: qsTr("Page Setup"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeTitleSize; font.weight: Theme.fontTypeTitleWeight }
        RowLayout {
            Layout.fillWidth: true
            Text { text: qsTr("Paper"); Layout.preferredWidth: 110; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize }
            SelectField {
                objectName: "pagePaper"
                Layout.fillWidth: true
                choices: ["A4", "A3", "Letter", "Legal"]
                value: root.project.printPaper
                onChosen: (c) => root.project.printPaper = c
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Text { text: qsTr("Orientation"); Layout.preferredWidth: 110; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize }
            SelectField {
                objectName: "pageOrientation"
                Layout.fillWidth: true
                choices: ["Landscape", "Portrait"]
                value: root.project.printLandscape ? "Landscape" : "Portrait"
                onChosen: (c) => root.project.printLandscape = c === "Landscape"
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            IconButton { objectName: "okButton"; implicitHeight: 26; label: qsTr("OK"); active: true; fillActive: true; fillText: Theme.textPrimary; onClicked: root.close() }
        }
    }
}
