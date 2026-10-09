import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Jad

Dialog {
    id: root
    modal: true
    title: qsTr("About JAD Daw")
    anchors.centerIn: parent
    width: 360

    background: Rectangle {
        color: Theme.surfacePanel
        border.color: Theme.borderStrong
        radius: Theme.radiusDialog
    }
    header: Item { height: 0 }

    contentItem: ColumnLayout {
        spacing: Theme.spacing[4]
        Image {
            Layout.alignment: Qt.AlignHCenter
            source: "icons/app-icon.png"
            sourceSize: Qt.size(96, 96)
            width: 96
            height: 96
        }
        Text {
            Layout.alignment: Qt.AlignHCenter
            text: "JAD Daw"
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTypeTitleSize + 4
            font.weight: Theme.fontTypeTitleWeight
        }
        Text {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Just Another Daw")
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTypeBodySize
        }
        Text {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Version %1").arg(Qt.application.version !== "" ? Qt.application.version : qsTr("development"))
            color: Theme.textValue
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTypeBodySize
        }
        Text {
            Layout.alignment: Qt.AlignHCenter
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: qsTr("Open source, GNU AGPL v3. Built with Qt (LGPLv3), JUCE (AGPLv3) and the Inter font (SIL OFL).")
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTypeLabelSize
        }
        IconButton {
            Layout.alignment: Qt.AlignHCenter
            implicitWidth: 72
            implicitHeight: 24
            label: qsTr("Close")
            onClicked: root.close()
        }
    }
}
