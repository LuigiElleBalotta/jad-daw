import QtQuick
import Jad

// "Track: ..." section of the Inspector. Name and colour are real; the other fields have no engine yet (visual only).
Column {
    id: root
    required property ProjectController project
    property var track: ({})
    readonly property alias header: head
    readonly property alias body: content
    readonly property alias nameField: nameInput
    readonly property alias swatchList: swatches
    readonly property var palette: ["purple", "indigo", "blue", "teal", "green", "yellow", "orange", "red", "pink", "magenta"]
    readonly property string colorName: track.color ?? "purple"
    function commitName(text) {
        const t = text.trim()
        if (t !== "" && t !== (track.name ?? "")) project.renameTrack(track.trackId, t)
    }

    PanelHeader {
        id: head
        width: parent.width
        title: qsTr("Track: %1").arg(root.track.name ?? "")
    }
    Column {
        id: content
        width: parent.width
        visible: head.expanded
        topPadding: Theme.spacing[2]
        bottomPadding: Theme.spacing[2]
        InspectorRow {
            label: qsTr("Name")
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width
                height: 20
                radius: Theme.radiusControl - 2
                color: Theme.surfaceRaised
                border.color: nameInput.activeFocus ? Theme.accentPrimary : Theme.borderSubtle
                TextInput {
                    id: nameInput
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spacing[2]
                    anchors.rightMargin: Theme.spacing[2]
                    verticalAlignment: TextInput.AlignVCenter
                    text: root.track.name ?? ""
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontTypeLabelSize
                    selectByMouse: true
                    maximumLength: 64
                    onEditingFinished: root.commitName(text)
                }
            }
        }
        InspectorRow {
            label: qsTr("Icon")
            MouseArea {
                anchors.verticalCenter: parent.verticalCenter
                width: 24
                height: 24
                objectName: "trackIconButton"
                onClicked: root.project.requestTrackIconDialog()
                TrackIcon {
                    anchors.centerIn: parent
                    kind: root.track.kind ?? "audio"
                    icon: root.track.icon ?? ""
                    tint: Theme["track" + root.colorName.charAt(0).toUpperCase() + root.colorName.slice(1) + "Solid"]
                }
            }
        }
        InspectorRow {
            label: qsTr("Color")
            Row {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 3
                Repeater {
                    id: swatches
                    model: root.palette
                    delegate: Rectangle {
                        id: swatch
                        required property string modelData
                        signal clicked()
                        width: 11
                        height: 11
                        radius: 3
                        color: Theme["track" + modelData.charAt(0).toUpperCase() + modelData.slice(1) + "Solid"]
                        border.width: root.colorName === modelData ? 2 : 0
                        border.color: Theme.textPrimary
                        onClicked: root.project.setTrackColor(root.track.trackId, modelData)
                        MouseArea { anchors.fill: parent; onClicked: swatch.clicked() }
                    }
                }
            }
        }
        Column {
            width: parent.width
            visible: root.track.kind === "instrument"
            InspectorRow { label: qsTr("Region type"); StubValue { project: root.project; label: qsTr("Default Region Type"); text: qsTr("MIDI") } }
            InspectorRow {
                label: qsTr("Transpose")
                NumberField { objectName: "trackTranspose"; anchors.verticalCenter: parent.verticalCenter; from: -48; to: 48; decimals: 0; value: root.track.transpose ?? 0; onCommitted: (v) => root.project.setTrackMidi(root.track.trackId, "transpose", v) }
            }
            InspectorRow {
                label: qsTr("Velocity")
                NumberField { objectName: "trackVelocity"; anchors.verticalCenter: parent.verticalCenter; from: -127; to: 127; decimals: 0; value: root.track.velocity ?? 0; onCommitted: (v) => root.project.setTrackMidi(root.track.trackId, "velocity", v) }
            }
            InspectorRow {
                label: qsTr("Key limit")
                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.spacing[1]
                    NumberField { objectName: "trackKeyLow"; implicitWidth: 52; from: 0; to: 127; decimals: 0; value: root.track.keyLow ?? 0; onCommitted: (v) => root.project.setTrackMidi(root.track.trackId, "keyLow", v) }
                    NumberField { objectName: "trackKeyHigh"; implicitWidth: 52; from: 0; to: 127; decimals: 0; value: root.track.keyHigh ?? 127; onCommitted: (v) => root.project.setTrackMidi(root.track.trackId, "keyHigh", v) }
                }
            }
            InspectorRow {
                label: qsTr("Velocity limit")
                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.spacing[1]
                    NumberField { objectName: "trackVelocityLow"; implicitWidth: 52; from: 1; to: 127; decimals: 0; value: root.track.velocityLow ?? 1; onCommitted: (v) => root.project.setTrackMidi(root.track.trackId, "velocityLow", v) }
                    NumberField { objectName: "trackVelocityHigh"; implicitWidth: 52; from: 1; to: 127; decimals: 0; value: root.track.velocityHigh ?? 127; onCommitted: (v) => root.project.setTrackMidi(root.track.trackId, "velocityHigh", v) }
                }
            }
            InspectorRow { label: qsTr("No transpose"); StubCheck { project: root.project; label: qsTr("No Transpose"); anchors.verticalCenter: parent.verticalCenter } }
        }
        InspectorRow {
            label: qsTr("Delay")
            visible: root.track.kind === "instrument" || root.track.kind === "audio"
            NumberField { objectName: "trackDelay"; anchors.verticalCenter: parent.verticalCenter; from: -1000; to: 1000; decimals: 0; suffix: " ms"; value: root.track.delayMs ?? 0; onCommitted: (v) => root.project.setTrackDelay(root.track.trackId, v) }
        }
        Column {
            width: parent.width
            visible: root.track.kind === "audio"
            InspectorRow { label: qsTr("Freeze mode"); StubValue { project: root.project; label: qsTr("Freeze Mode"); text: qsTr("Pre Fader") } }
            InspectorRow { label: qsTr("Q-reference"); StubCheck { project: root.project; label: qsTr("Q-Reference"); anchors.verticalCenter: parent.verticalCenter } }
        }
    }
}
