import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects
import Jad

// The header of one track: colour chip, kind, number, name (double click renames), R and I (not implemented yet),
// M and S; from 56 px of height also the volume and pan.
Item {
    id: root
    property string trackId
    property string trackName
    property string kind: "audio"
    property string trackColor: "purple"
    property int number: 1
    property real gainDb: 0
    property real pan: 0
    property bool mute: false
    property bool solo: false
    property bool recordArm: false
    property bool inputMonitor: false
    property bool frozen: false          // plays its rendered audio
    property string icon: ""             // Track > Assign Track Icon
    // Track > Configure Track Header: which parts of the header are shown
    property bool showIcon: true
    property bool showArm: true
    property bool showMonitor: true
    property bool showMute: true
    property bool showSolo: true
    property bool showSliders: true
    property bool selected: false

    readonly property bool slidersVisible: height >= 56 && showSliders
    readonly property url iconSource: icon === "" ? "" : (icon === "audio" || icon === "instrument" ? "icons/icon-" + icon + ".svg" : "icons/track-" + icon + ".svg")
    property bool editing: false
    readonly property alias muteButton: muteButton
    readonly property alias soloButton: soloButton
    readonly property alias armButton: armButton
    readonly property alias monitorButton: monitorButton
    readonly property alias nameLabel: nameLabel
    readonly property alias nameInput: nameInput

    signal muteToggled(string id, bool on)
    signal soloToggled(string id, bool on)
    signal renamed(string id, string name)
    signal libraryRequested(string id)  // a double click on the header outside the name and the buttons
    signal selectRequested(string id, string mode)
    // the pan as Logic shows it: -64 (hard left) to +63 (hard right), 0 in the centre
    function panText(v) { const n = Math.max(-64, Math.min(63, Math.round(v * 64))); return (n > 0 ? "+" : "") + n }
    signal gestureStarted()
    signal gainMoved(string id, real db)
    signal panMoved(string id, real pan)
    signal gainReleased(string id, real db)
    signal panReleased(string id, real pan)
    signal trackToggled(string trackId, string actionId, bool on)

    readonly property string capitalColor: trackColor.charAt(0).toUpperCase() + trackColor.slice(1)
    readonly property string kindLabel: (({ "audio": "Au", "instrument": "Inst", "midi": "MIDI", "bus": "Bus", "aux": "Aux" })[kind] ?? kind) + (frozen ? " ❄" : "")  // a snowflake: frozen

    function modeFor(modifiers) {
        if (modifiers & Qt.ControlModifier) return "toggle"
        if (modifiers & Qt.ShiftModifier) return "extend"
        return "replace"
    }
    function beginRename() {
        editing = true
        nameInput.text = trackName
        nameInput.forceActiveFocus()
        nameInput.selectAll()
    }

    Rectangle {
        anchors.fill: parent
        color: root.selected ? Theme.surfaceRaised : "transparent"
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.borderSubtle }
    }
    // the whole header selects the track; the controls on top take their own clicks
    MouseArea {
        anchors.fill: parent
        onClicked: (m) => root.selectRequested(root.trackId, root.modeFor(m.modifiers))
        onDoubleClicked: root.libraryRequested(root.trackId)
    }
    Rectangle {
        id: chip
        x: Theme.spacing[3]
        anchors.verticalCenter: parent.verticalCenter
        width: 4
        height: parent.height - 16
        radius: 2
        color: Theme["track" + root.capitalColor + "Solid"]
    }

    Item {
        id: top
        anchors.left: chip.right
        anchors.leftMargin: Theme.spacing[3]
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacing[3]
        y: root.slidersVisible ? Theme.spacing[3] : (root.height - height) / 2
        height: 22

        Item {
            id: iconSlot
            objectName: "headerIcon"
            visible: root.showIcon && root.icon !== ""
            width: visible ? 20 : 0
            height: 20
            anchors.verticalCenter: parent.verticalCenter
            Image { id: iconImage; anchors.fill: parent; source: root.iconSource; sourceSize: Qt.size(20, 20); visible: false }
            MultiEffect { anchors.fill: iconImage; source: iconImage; brightness: 1.0; colorization: 1.0; colorizationColor: Theme["track" + root.capitalColor + "Solid"] }
        }
        Text {
            id: kindText
            anchors.left: iconSlot.right
            anchors.leftMargin: iconSlot.visible ? Theme.spacing[1] : 0
            anchors.verticalCenter: parent.verticalCenter
            width: 28
            text: root.kindLabel
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTypeCaptionSize
        }
        Text {
            id: numberText
            anchors.left: kindText.right
            anchors.verticalCenter: parent.verticalCenter
            text: root.number
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTypeLabelSize
        }
        Item {
            anchors.left: numberText.right
            anchors.leftMargin: Theme.spacing[2]
            anchors.right: buttons.left
            anchors.rightMargin: Theme.spacing[2]
            anchors.verticalCenter: parent.verticalCenter
            height: parent.height
            Text {
                id: nameLabel
                anchors.fill: parent
                visible: !root.editing
                verticalAlignment: Text.AlignVCenter
                text: root.trackName
                elide: Text.ElideRight
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTypeBodySize
                font.weight: Theme.fontTypeTitleWeight
            }
            MouseArea {
                anchors.fill: parent
                enabled: !root.editing
                onClicked: (m) => root.selectRequested(root.trackId, root.modeFor(m.modifiers))
                onDoubleClicked: root.beginRename()
            }
            TextInput {
                id: nameInput
                anchors.fill: parent
                visible: root.editing
                verticalAlignment: Text.AlignVCenter
                color: Theme.textPrimary
                selectByMouse: true
                clip: true
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTypeBodySize
                // Return and a click elsewhere confirm the name (as in Logic); Escape cancels
                function commit() {
                    const name = text.trim()
                    root.editing = false
                    if (name !== "" && name !== root.trackName) root.renamed(root.trackId, name)
                }
                onAccepted: commit()
                // Return is also a global shortcut (go to beginning): it must confirm the field, not run the shortcut
                Keys.onShortcutOverride: (event) => { if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) event.accepted = true }
                Keys.onEscapePressed: root.editing = false
                onActiveFocusChanged: if (!activeFocus && root.editing) commit()
            }
        }
        Row {
            id: buttons
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacing[1]
            IconButton {
                id: armButton
                visible: root.showArm
                implicitWidth: 20; implicitHeight: 20
                label: "R"
                active: root.recordArm
                onClicked: root.trackToggled(root.trackId, "track.recordArm", !root.recordArm)
            }
            IconButton {
                id: monitorButton
                visible: root.showMonitor
                implicitWidth: 20; implicitHeight: 20
                label: "I"
                active: root.inputMonitor
                onClicked: root.trackToggled(root.trackId, "track.inputMonitor", !root.inputMonitor)
            }
            IconButton {
                id: muteButton
                visible: root.showMute
                implicitWidth: 20; implicitHeight: 20
                label: "M"
                active: root.mute
                onClicked: root.muteToggled(root.trackId, !root.mute)
            }
            IconButton {
                id: soloButton
                visible: root.showSolo
                implicitWidth: 20; implicitHeight: 20
                label: "S"
                active: root.solo
                onClicked: root.soloToggled(root.trackId, !root.solo)
            }
        }
    }

    Item {
        visible: root.slidersVisible
        anchors.left: chip.right
        anchors.leftMargin: Theme.spacing[3]
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacing[3]
        anchors.top: top.bottom
        anchors.topMargin: Theme.spacing[2]
        height: 20

        Slider {
            id: volume
            anchors.left: parent.left
            anchors.right: panKnob.left
            anchors.rightMargin: Theme.spacing[3]
            anchors.verticalCenter: parent.verticalCenter
            from: -96
            to: 6
            // the slider shows its own value while it is dragged; the command goes out on release
            property bool started: false
            onMoved: {  // live: the strips and fields elsewhere follow
                if (!started) { started = true; root.gestureStarted() }
                root.gainMoved(root.trackId, value)
            }
            onPressedChanged: if (!pressed) { started = false; root.gainReleased(root.trackId, value) }
            Binding { target: volume; property: "value"; value: root.gainDb; when: !volume.pressed }
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Volume: %1 dB").arg(Math.round(value))
            background: Rectangle {
                x: volume.leftPadding
                y: volume.topPadding + volume.availableHeight / 2 - height / 2
                width: volume.availableWidth
                height: 4
                radius: 2
                color: Theme.surfaceRaised
                Rectangle { width: volume.visualPosition * parent.width; height: parent.height; radius: 2; color: Theme.textSecondary }
            }
            handle: Rectangle {
                x: volume.leftPadding + volume.visualPosition * (volume.availableWidth - width)
                y: volume.topPadding + volume.availableHeight / 2 - height / 2
                width: 10; height: 10; radius: 5
                color: Theme.textPrimary
            }
        }
        Knob {
            id: panKnob
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            implicitWidth: 20
            implicitHeight: 20
            fillArc: true
            format: (v) => root.panText(v)
            value: root.pan
            property bool started: false
            onMoved: (v) => {
                if (!started) { started = true; root.gestureStarted() }
                root.panMoved(root.trackId, v)
            }
            onReleased: (v) => { started = false; root.panReleased(root.trackId, v) }
        }
    }
}
