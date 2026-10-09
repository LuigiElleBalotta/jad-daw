import QtQuick
import Jad

// The global tracks under the ruler (Track > Show Global Tracks, key G): Marker, Tempo and Signature lanes.
// Marker: double-click on the lane creates a marker, a drag moves it, a double-click on it renames it, right-click deletes it.
// Tempo and Signature: double-click on the lane adds an event at the bar, a double-click on an event edits it, right-click deletes it.
Item {
    id: root
    required property ProjectController project
    required property real pixelsPerBeat
    required property real scrollBeats
    property real snapUnit: 1                       // beats; the timeline's snap, at least a beat for events
    readonly property real laneHeight: 18
    readonly property int lanes: 3
    height: laneHeight * lanes
    clip: true

    // refreshed whenever the project changes (a binding on project.revision alone was not re-evaluated for a project opened at start)
    property var markerList: []
    property var tempoList: []
    property var signatureList: []
    function refresh() {
        markerList = project.markers()
        tempoList = project.tempoEvents()
        signatureList = project.signatureEvents()
    }
    Component.onCompleted: refresh()
    Connections { target: root.project; function onProjectChanged() { root.refresh() } }
    readonly property real barBeats: project.barBeats

    function beatsToX(b) { return (b - scrollBeats) * pixelsPerBeat }
    function xToBeats(x) { return x / pixelsPerBeat + scrollBeats }
    function snapTo(b, unit) { return Math.max(0, Math.round(b / unit) * unit) }
    function bpmText(v) { return Math.abs(v - Math.round(v)) < 0.005 ? String(Math.round(v)) : v.toFixed(2) }

    // one text field for every rename and value entry
    property string editKind: ""                     // "marker", "tempo" or "signature"
    property string editKey: ""                      // a marker id, or the position of an event in beats
    property real editBeats: 0
    function startEdit(kind, key, beats, text, x) {
        editKind = kind; editKey = key; editBeats = beats
        editor.x = Math.max(0, Math.min(root.width - editor.width, x))
        editor.y = (kind === "marker" ? 0 : (kind === "tempo" ? 1 : 2)) * laneHeight
        editField.text = text
        editor.visible = true
        editField.forceActiveFocus()
        editField.selectAll()
    }
    function finishEdit(accept) {
        if (!editor.visible) return
        editor.visible = false
        if (!accept) return
        const t = editField.text.trim()
        if (editKind === "marker") {
            if (editKey === "") project.addMarker(editBeats, t)
            else if (t !== "") project.renameMarker(editKey, t)
        } else if (editKind === "tempo") {
            const bpm = parseFloat(t.replace(",", "."))
            if (isFinite(bpm) && bpm >= 20 && bpm <= 999) project.setTempoAt(editBeats, bpm)
        } else if (editKind === "signature") {
            const m = t.match(/^(\d+)\s*\/\s*(\d+)$/)
            if (m) project.setSignatureAt(editBeats, parseInt(m[1]), parseInt(m[2]))
        }
    }

    component Lane: Rectangle {
        required property int index
        y: index * root.laneHeight
        width: root.width
        height: root.laneHeight
        color: index % 2 === 0 ? Theme.surfaceCanvas : Theme.surfaceApp
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.borderSubtle }
    }
    Lane { index: 0 }
    Lane { index: 1 }
    Lane { index: 2 }

    // empty lane: double-click adds
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        onDoubleClicked: (m) => {
            const lane = Math.floor(m.y / root.laneHeight)
            const beats = root.snapTo(root.xToBeats(m.x), lane === 0 ? Math.min(root.snapUnit, 1) : root.barBeats)
            if (lane === 0) root.project.addMarker(beats)
            else if (lane === 1) root.startEdit("tempo", "", beats, root.bpmText(root.project.bpm), m.x)
            else root.startEdit("signature", "", beats, root.project.signatureText, m.x)
        }
    }

    // ---- markers
    Repeater {
        model: root.markerList
        delegate: Item {
            id: marker
            required property var modelData
            property real dragBeats: -1
            readonly property real beats: dragBeats >= 0 ? dragBeats : modelData.beats
            x: root.beatsToX(beats)
            width: Math.max(32, label.implicitWidth + 14)
            height: root.laneHeight
            visible: x + width > 0 && x < root.width
            Rectangle {  // the flag: a pole and a tag
                anchors.fill: parent
                anchors.topMargin: 2
                anchors.bottomMargin: 2
                radius: 2
                color: markerArea.pressed ? Theme.accentPrimary : Theme.surfaceRaised
                border.color: Theme.borderStrong
            }
            Rectangle { x: 0; width: 2; height: parent.height; color: Theme.accentPrimary }
            Text {
                id: label
                x: 8
                anchors.verticalCenter: parent.verticalCenter
                text: marker.modelData.name
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTypeCaptionSize
            }
            MouseArea {
                id: markerArea
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                property real pressX: 0
                onPressed: (m) => {
                    if (m.button === Qt.RightButton) { markerMenu.target = marker.modelData.id; markerMenu.popup(); return }
                    pressX = m.x
                }
                onPositionChanged: (m) => {
                    if (!pressed || Math.abs(m.x - pressX) < 3 && marker.dragBeats < 0) return
                    marker.dragBeats = root.snapTo(marker.modelData.beats + (m.x - pressX) / root.pixelsPerBeat, root.snapUnit > 0 ? Math.min(root.snapUnit, 1) : 1 / 16)
                }
                onReleased: {
                    if (marker.dragBeats >= 0 && marker.dragBeats !== marker.modelData.beats) root.project.moveMarker(marker.modelData.id, marker.dragBeats)
                    marker.dragBeats = -1
                }
                onClicked: (m) => { if (m.button === Qt.LeftButton && marker.dragBeats < 0) root.project.locateBeats(marker.modelData.beats) }
                onDoubleClicked: root.startEdit("marker", marker.modelData.id, marker.modelData.beats, marker.modelData.name, marker.x)
            }
        }
    }

    // ---- tempo
    Repeater {
        model: root.tempoList
        delegate: Item {
            id: tempo
            required property var modelData
            required property int index
            readonly property real nextBeats: index + 1 < root.tempoList.length ? root.tempoList[index + 1].beats : root.xToBeats(root.width) + 1
            x: root.beatsToX(modelData.beats)
            y: root.laneHeight
            width: Math.max(2, (nextBeats - modelData.beats) * root.pixelsPerBeat)
            height: root.laneHeight
            visible: x + width > 0 && x < root.width
            Rectangle { x: 0; y: 3; width: parent.width; height: parent.height - 6; radius: 2; color: Qt.rgba(Theme.accentPrimary.r, Theme.accentPrimary.g, Theme.accentPrimary.b, 0.22); border.color: Theme.accentPrimary }
            Text {
                x: 5
                anchors.verticalCenter: parent.verticalCenter
                text: root.bpmText(tempo.modelData.bpm)
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTypeCaptionSize
            }
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onPressed: (m) => { if (m.button === Qt.RightButton) { eventMenu.kind = "tempo"; eventMenu.beats = tempo.modelData.beats; eventMenu.removable = tempo.modelData.beats > 0; eventMenu.popup() } }
                onDoubleClicked: root.startEdit("tempo", "e", tempo.modelData.beats, root.bpmText(tempo.modelData.bpm), tempo.x)
            }
        }
    }

    // ---- signature
    Repeater {
        model: root.signatureList
        delegate: Item {
            id: sig
            required property var modelData
            required property int index
            readonly property real nextBeats: index + 1 < root.signatureList.length ? root.signatureList[index + 1].beats : root.xToBeats(root.width) + 1
            x: root.beatsToX(modelData.beats)
            y: root.laneHeight * 2
            width: Math.max(2, (nextBeats - modelData.beats) * root.pixelsPerBeat)
            height: root.laneHeight
            visible: x + width > 0 && x < root.width
            Rectangle { x: 0; y: 3; width: parent.width; height: parent.height - 6; radius: 2; color: Qt.rgba(Theme.stateSolo.r, Theme.stateSolo.g, Theme.stateSolo.b, 0.18); border.color: Theme.stateSolo }
            Text {
                x: 5
                anchors.verticalCenter: parent.verticalCenter
                text: sig.modelData.numerator + "/" + sig.modelData.denominator
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTypeCaptionSize
            }
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onPressed: (m) => { if (m.button === Qt.RightButton) { eventMenu.kind = "signature"; eventMenu.beats = sig.modelData.beats; eventMenu.removable = sig.modelData.beats > 0; eventMenu.popup() } }
                onDoubleClicked: root.startEdit("signature", "e", sig.modelData.beats, sig.modelData.numerator + "/" + sig.modelData.denominator, sig.x)
            }
        }
    }

    ThemedMenu {
        id: markerMenu
        property string target
        ThemedMenuItem { text: qsTr("Delete Marker"); onTriggered: root.project.removeMarker(markerMenu.target) }
    }
    ThemedMenu {
        id: eventMenu
        property string kind
        property real beats: 0
        property bool removable: true
        ThemedMenuItem {
            text: eventMenu.kind === "tempo" ? qsTr("Delete Tempo Change") : qsTr("Delete Time Signature Change")
            enabled: eventMenu.removable
            onTriggered: eventMenu.kind === "tempo" ? root.project.removeTempoAt(eventMenu.beats) : root.project.removeSignatureAt(eventMenu.beats)
        }
    }

    Rectangle {  // the inline editor
        id: editor
        visible: false
        width: 110
        height: root.laneHeight
        color: Theme.surfaceRaised
        border.color: Theme.accentPrimary
        radius: 2
        z: 5
        TextInput {
            id: editField
            anchors.fill: parent
            anchors.leftMargin: 5
            anchors.rightMargin: 5
            verticalAlignment: TextInput.AlignVCenter
            color: Theme.textPrimary
            selectionColor: Theme.accentPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTypeCaptionSize
            clip: true
            onAccepted: root.finishEdit(true)
            Keys.onEscapePressed: root.finishEdit(false)
            onActiveFocusChanged: if (!activeFocus) root.finishEdit(true)
        }
    }
}
