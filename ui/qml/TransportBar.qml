import QtQuick
import QtQuick.Layouts
import Jad

Panel {
    id: root
    required property ProjectController project

    implicitHeight: 48
    radius: 0

    function pad(n, w) { let s = String(n); while (s.length < w) s = "0" + s; return s }
    readonly property string barBeat: {
        const b = Math.max(0, project.positionBeats)
        const bar = Math.floor(b / project.beatsPerBar) + 1
        const beat = Math.floor(b % project.beatsPerBar) + 1
        return pad(bar, 3) + "." + beat
    }
    readonly property string clock: {
        const s = Math.max(0, project.positionSeconds)
        return pad(Math.floor(s / 60), 2) + ":" + pad(Math.floor(s % 60), 2)
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacing[5]
        anchors.rightMargin: Theme.spacing[5]
        spacing: Theme.spacing[3]

        IconButton { source: "icons/rewind.svg"; onClicked: root.project.locateBeats(0) }
        IconButton {
            source: "icons/play.svg"
            active: root.project.playing
            onClicked: root.project.play()
        }
        IconButton { source: "icons/stop.svg"; onClicked: root.project.stop() }
        IconButton {
            source: "icons/loop.svg"
            toggle: true
            active: root.project.loopEnabled
            onClicked: root.project.setLoopBeats(0, active ? root.project.beatsPerBar * 4 : 0)
        }

        Rectangle {
            Layout.preferredWidth: 220
            Layout.preferredHeight: Theme.sizeControlLarge
            radius: Theme.radiusControl
            color: Theme.surfaceLcd
            border.color: Theme.surfaceLcdBezel
            border.width: 1
            Row {
                anchors.centerIn: parent
                spacing: Theme.spacing[5]
                Text {
                    text: root.barBeat
                    color: Theme.textValue
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontTypeLcdSize
                    font.weight: Theme.fontTypeLcdWeight
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.clock
                    color: Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontTypeBodySize
                }
            }
        }
        Text {
            text: root.project.bpm.toFixed(1) + " bpm"
            color: Theme.textValue
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTypeBodySize
        }

        Item { Layout.fillWidth: true }

        Rectangle {
            visible: root.project.degraded || root.project.deviceError !== ""
            Layout.preferredHeight: 22
            Layout.preferredWidth: pillText.implicitWidth + 16
            radius: Theme.radiusPill
            color: Theme.stateRecord
            Text {
                id: pillText
                anchors.centerIn: parent
                text: "audio not running"
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTypeLabelSize
                font.weight: Theme.fontTypeLabelWeight
            }
        }
        IconButton { source: "icons/rewind.svg"; onClicked: root.project.undo() }
        IconButton { source: "icons/forward.svg"; onClicked: root.project.redo() }
    }
}
