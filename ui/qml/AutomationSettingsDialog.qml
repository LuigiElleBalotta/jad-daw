import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Jad

// Mix > Automation Settings.
Dialog {
    id: root
    required property ProjectController project
    modal: false
    anchors.centerIn: parent
    width: 460
    background: Rectangle { color: Theme.surfacePanel; border.color: Theme.borderStrong; radius: Theme.radiusDialog }
    header: Item { height: 0 }
    contentItem: ColumnLayout {
        spacing: Theme.spacing[3]
        Text { text: qsTr("Automation Settings"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeTitleSize; font.weight: Theme.fontTypeTitleWeight }
        RowLayout {
            spacing: Theme.spacing[2]
            FlagCheck { objectName: "quickAccess"; on: root.project.automationQuickAccess; onFlipped: root.project.automationQuickAccess = !on }
            Text { Layout.fillWidth: true; text: qsTr("Automation Quick Access"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize }
            IconButton { objectName: "learnButton"; implicitHeight: 24; label: qsTr("Learn"); enabled: root.project.automationQuickAccess; onClicked: root.project.learnQuickAccessController() }
        }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            visible: root.project.automationQuickAccess
            text: root.project.quickAccessController === -2 ? qsTr("Move the controller you want to use.") : qsTr("MIDI controller %1 writes the active automation parameter (Volume or Pan) of the selected track.").arg(root.project.quickAccessController)
            color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeCaptionSize
        }
        RowLayout {
            spacing: Theme.spacing[2]
            FlagCheck { objectName: "autoselect"; on: root.project.autoselectAutomationParam; onFlipped: root.project.autoselectAutomationParam = !on }
            Text { Layout.fillWidth: true; text: qsTr("Autoselect Automation Parameter in Read Mode"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize }
        }
        RowLayout {
            spacing: Theme.spacing[2]
            FlagCheck { objectName: "followRegions"; on: root.project.automationFollowsRegions; onFlipped: root.project.automationFollowsRegions = !on }
            Text { Layout.fillWidth: true; text: qsTr("Move Track Automation with Regions"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize }
        }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            IconButton { implicitHeight: 26; label: qsTr("Close"); onClicked: root.close() }
        }
    }
}
