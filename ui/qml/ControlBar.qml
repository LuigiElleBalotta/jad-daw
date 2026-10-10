import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Jad

// The control bar: panel toggles, transport, LCD, and the metronome / count-in / cycle / punch group with the master volume.
Panel {
    id: root
    required property ProjectController project
    readonly property alias lcd: lcdItem
    signal message(string text)

    implicitHeight: 48
    radius: 0

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacing[5]
        anchors.rightMargin: Theme.spacing[5]
        spacing: Theme.spacing[3]

        Row {
            visible: { root.project.barItemsRevision; return root.project.barItem("cb.panels") }
            spacing: Theme.spacing[2]
            ActionButton { actionId: "view.library"; label: qsTr("Lib") }
            ActionButton { actionId: "view.inspector"; label: qsTr("Insp") }
            ActionButton { actionId: "view.quickHelp"; label: "?" }
            ActionButton { actionId: "view.smartControls"; label: qsTr("SC") }
            ActionButton { actionId: "view.mixer"; source: "icons/mixer.svg" }
            ActionButton { actionId: "view.editors"; label: qsTr("Ed") }
            ActionButton { actionId: "view.loops"; label: qsTr("Lp") }
        }

        Item { Layout.fillWidth: true }

        Row {
            visible: { root.project.barItemsRevision; return root.project.barItem("cb.transport") }
            spacing: Theme.spacing[2]
            ActionButton { actionId: "transport.toStart"; source: "icons/rewind.svg" }
            ActionButton { actionId: "transport.barBack"; label: "<" }
            ActionButton { actionId: "transport.barForward"; label: ">" }
            ActionButton { actionId: "transport.stop"; source: "icons/stop.svg" }
            ActionButton { actionId: "transport.playStop"; source: "icons/play.svg"; active: root.project.playing }
            ActionButton { actionId: "transport.record"; label: "●" }
        }

        Lcd {
            id: lcdItem
            visible: { root.project.barItemsRevision; return root.project.barItem("cb.lcd") }
            project: root.project
            onMessage: (text) => root.message(text)
        }

        Item { Layout.fillWidth: true }

        Row {
            visible: { root.project.barItemsRevision; return root.project.barItem("cb.modes") }
            spacing: Theme.spacing[2]
            ActionButton { actionId: "transport.metronome"; label: qsTr("Met") }
            ActionButton { actionId: "transport.countIn"; label: "1 2 3" }
            ActionButton { actionId: "transport.loop"; source: "icons/loop.svg" }
            ActionButton { actionId: "transport.punch"; label: qsTr("Pnc") }
        }

        Slider {
            id: master
            visible: { root.project.barItemsRevision; return root.project.barItem("cb.master") }
            Layout.preferredWidth: 90
            from: -96
            to: 24
            // while it is dragged the slider shows its own value; the command goes out on release
            onPressedChanged: if (!pressed) root.project.setMasterGain(value)
            Binding { target: master; property: "value"; value: root.project.masterGainDb; when: !master.pressed }
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Master volume: %1 dB").arg(Math.round(value))
            background: Rectangle {
                x: master.leftPadding
                y: master.topPadding + master.availableHeight / 2 - height / 2
                width: master.availableWidth
                height: 4
                radius: 2
                color: Theme.surfaceRaised
                Rectangle { width: master.visualPosition * parent.width; height: parent.height; radius: 2; color: Theme.textSecondary }
            }
            handle: Rectangle {
                x: master.leftPadding + master.visualPosition * (master.availableWidth - width)
                y: master.topPadding + master.availableHeight / 2 - height / 2
                width: 12; height: 12; radius: 6
                color: Theme.textPrimary
            }
        }

        Rectangle {
            visible: root.project.degraded || root.project.deviceError !== ""
            Layout.preferredHeight: 22
            Layout.preferredWidth: pillText.implicitWidth + 16
            radius: Theme.radiusPill
            color: Theme.stateRecord
            Text {
                id: pillText
                anchors.centerIn: parent
                text: qsTr("audio not running")
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTypeLabelSize
                font.weight: Theme.fontTypeLabelWeight
            }
        }
    }
}
