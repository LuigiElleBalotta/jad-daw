import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Jad

// Help > Check for Updates: looks at the GitHub releases, shows what is new and installs the new version over this one.
Dialog {
    id: root
    property var updater: null
    readonly property string state: updater ? updater.state : "idle"
    modal: true
    anchors.centerIn: parent
    width: 440
    closePolicy: root.state === "downloading" || root.state === "installing" ? Popup.NoAutoClose : Popup.CloseOnEscape
    background: Rectangle { color: Theme.surfacePanel; border.color: Theme.borderStrong; radius: Theme.radiusDialog }
    header: Item { height: 0 }

    // opens the dialog and looks (the menu command)
    function checkNow() {
        if (updater) updater.check()
        open()
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacing[3]
        Text {
            text: qsTr("Software Update")
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTypeTitleSize
            font.weight: Theme.fontTypeTitleWeight
        }
        Text {
            objectName: "updateMessage"
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: root.state === "error" ? Theme.trackRedSolid : Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTypeBodySize
            text: {
                const u = root.updater
                if (!u) return ""
                switch (root.state) {
                case "checking": return qsTr("Looking for a new version…")
                case "upToDate": return qsTr("JAD Daw %1 is the newest version.").arg(u.currentVersion)
                case "available": return qsTr("JAD Daw %1 is available. You have %2.").arg(u.latestVersion).arg(u.currentVersion)
                case "downloading": return qsTr("Downloading JAD Daw %1…").arg(u.latestVersion)
                case "ready": return qsTr("Downloaded.")
                case "installing": return qsTr("Installing: the program closes and opens again by itself.")
                case "error": return u.error
                default: return qsTr("You have JAD Daw %1.").arg(u.currentVersion)
                }
            }
        }
        Rectangle {  // what the new version brings (the text of the release)
            objectName: "updateNotes"
            visible: root.state === "available" && root.updater && root.updater.notes !== ""
            Layout.fillWidth: true
            Layout.preferredHeight: 140
            color: Theme.surfaceRaised
            radius: 3
            clip: true
            Flickable {
                anchors.fill: parent
                anchors.margins: 8
                contentHeight: notes.implicitHeight
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                Text {
                    id: notes
                    width: parent.width - 12
                    wrapMode: Text.WordWrap
                    text: root.updater ? root.updater.notes : ""
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontTypeLabelSize
                }
            }
        }
        Rectangle {  // download progress
            objectName: "updateProgress"
            visible: root.state === "downloading"
            Layout.fillWidth: true
            height: 6
            radius: 3
            color: Theme.surfaceRaised
            Rectangle {
                width: parent.width * (root.updater ? root.updater.progress : 0)
                height: parent.height
                radius: 3
                color: Theme.accentPrimary
            }
        }
        ThemedCheckBox {
            objectName: "updateAuto"
            visible: root.state !== "downloading" && root.state !== "installing"
            text: qsTr("Check for updates when the program starts")
            checked: root.updater ? root.updater.autoCheck : true
            onToggled: if (root.updater) root.updater.autoCheck = checked
        }
        ThemedCheckBox {
            objectName: "updatePrereleases"
            visible: root.state !== "downloading" && root.state !== "installing"
            text: qsTr("Also offer pre-releases (alpha and beta)")
            checked: root.updater ? root.updater.includePrereleases : false
            onToggled: if (root.updater) root.updater.includePrereleases = checked
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: Theme.spacing[2]
            IconButton {
                objectName: "updateSkip"
                visible: root.state === "available"
                implicitHeight: 26
                label: qsTr("Skip This Version")
                onClicked: { root.updater.skipThisVersion(); root.close() }
            }
            IconButton {
                objectName: "updateCancel"
                visible: root.state === "downloading"
                implicitHeight: 26
                label: qsTr("Cancel")
                onClicked: root.updater.cancel()
            }
            IconButton {
                objectName: "updateRetry"
                visible: root.state === "error"
                implicitHeight: 26
                label: qsTr("Try Again")
                onClicked: root.updater.check()
            }
            IconButton {
                objectName: "updateClose"
                visible: root.state !== "downloading" && root.state !== "installing"
                implicitHeight: 26
                label: root.state === "available" ? qsTr("Later") : qsTr("Close")
                onClicked: root.close()
            }
            IconButton {
                objectName: "updateInstall"
                visible: root.state === "available"
                implicitHeight: 26
                label: root.updater && root.updater.canInstall ? qsTr("Install and Restart") : qsTr("Open Release Page")
                active: true
                fillActive: true
                fillText: Theme.textPrimary
                onClicked: root.updater.canInstall ? root.updater.install() : root.updater.openReleasePage()
            }
        }
    }
}
