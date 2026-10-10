import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Jad

// View > List Editors (also Window > Open Event List and Open Signature List): the events of the project as tables.
//   Event: the notes and controller events of the selected MIDI region (a note's pitch, velocity and length can be edited, an event deleted)
//   Marker, Tempo and Signature: the global events, each can be changed or removed.
Dialog {
    id: root
    required property ProjectController project
    property int tab: 0                       // 0 Event, 1 Marker, 2 Tempo, 3 Signature
    modal: false
    anchors.centerIn: parent
    width: 640
    height: 460
    readonly property string regionId: project.selectedRegionIds.length > 0 ? project.selectedRegionIds[0] : ""
    property var events: []                   // the rows of the Event tab
    property var markers: []
    property var tempos: []
    property var signatures: []
    function reload() {
        const info = regionId !== "" ? project.regionInfo(regionId) : ({ found: false })
        const rows = []
        if (info.found === true && info.audio !== true) {
            const notes = project.regionNotes(regionId)
            for (let i = 0; i < notes.length; ++i) rows.push({ kind: "note", index: i, beats: info.startBeats + notes[i].start, note: notes[i] })
            for (const c of project.regionControlEvents(regionId)) rows.push({ kind: c.status === 0xB0 ? "cc" : (c.status === 0xE0 ? "bend" : "touch"), beats: info.startBeats + c.beats, c: c })
            rows.sort((a, b) => a.beats - b.beats)
        }
        events = rows
        markers = project.markers()
        tempos = project.tempoEvents()
        signatures = project.signatureEvents()
    }
    onAboutToShow: reload()
    onRegionIdChanged: if (visible) reload()
    Connections { target: root.project; function onProjectChanged() { if (root.visible) root.reload() } }
    background: Rectangle { color: Theme.surfacePanel; border.color: Theme.borderStrong; radius: Theme.radiusDialog }
    header: Item { height: 0 }

    function position(beats) {
        const bb = project.barBeats
        const bar = Math.floor(beats / bb + 1e-9)
        const beat = Math.floor(beats - bar * bb + 1e-9)
        const sub = Math.round((beats - bar * bb - beat) * 960)
        return (bar + 1) + " " + (beat + 1) + " " + sub
    }
    function noteName(n) { return ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"][n % 12] + (Math.floor(n / 12) - 2) }
    function notesCopy() { return project.regionNotes(regionId).map(n => ({ start: n.start, length: n.length, note: n.note, velocity: n.velocity, muted: n.muted === true })) }
    function setNote(i, field, value) {
        const list = notesCopy()
        if (i < 0 || i >= list.length) return
        list[i][field] = value
        project.setRegionNotes(regionId, list)
    }
    function removeNote(i) {
        const list = notesCopy()
        list.splice(i, 1)
        project.setRegionNotes(regionId, list)
    }

    component Cell: Text {
        Layout.alignment: Qt.AlignVCenter
        color: Theme.textPrimary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontTypeBodySize
        elide: Text.ElideRight
    }
    component Head: Text {
        color: Theme.textSecondary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontTypeCaptionSize
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacing[2]
        Text { text: qsTr("List Editors"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeTitleSize; font.weight: Theme.fontTypeTitleWeight }
        Row {
            Layout.fillWidth: true
            spacing: 0
            Repeater {
                model: [qsTr("Event"), qsTr("Marker"), qsTr("Tempo"), qsTr("Signature")]
                delegate: IconButton {
                    required property string modelData
                    required property int index
                    objectName: "listTab" + index
                    implicitHeight: 24
                    label: modelData
                    active: root.tab === index
                    fillActive: true
                    fillText: Theme.textPrimary
                    onClicked: root.tab = index
                }
            }
        }
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.tab

            // ---- Event
            ColumnLayout {
                spacing: 2
                Text { visible: root.events.length === 0; text: qsTr("Select a MIDI region to list its events."); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacing[2]
                    Head { Layout.preferredWidth: 90; text: qsTr("Position") }
                    Head { Layout.preferredWidth: 90; text: qsTr("Type") }
                    Head { Layout.preferredWidth: 80; text: qsTr("Num / Pitch") }
                    Head { Layout.preferredWidth: 70; text: qsTr("Val / Vel") }
                    Head { Layout.preferredWidth: 70; text: qsTr("Length") }
                }
                ListView {
                    objectName: "eventList"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: root.events
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}
                    delegate: RowLayout {
                        id: row
                        required property var modelData
                        width: ListView.view.width - 10
                        height: 24
                        spacing: Theme.spacing[2]
                        readonly property bool isNote: modelData.kind === "note"
                        Cell { Layout.preferredWidth: 90; text: root.position(row.modelData.beats) }
                        Cell { Layout.preferredWidth: 90; text: row.isNote ? qsTr("Note") : (row.modelData.kind === "cc" ? qsTr("Control") : (row.modelData.kind === "bend" ? qsTr("Pitch Bend") : qsTr("Aftertouch"))) }
                        Cell {
                            visible: !row.isNote
                            Layout.preferredWidth: 80
                            text: row.modelData.c && row.modelData.kind === "cc" ? String(row.modelData.c.data1) : ""
                        }
                        Item { visible: !row.isNote; Layout.preferredWidth: 40 }  // where a note shows its name
                        NumberField {
                            visible: row.isNote
                            objectName: "eventPitch"
                            Layout.preferredWidth: 80
                            from: 0; to: 127; decimals: 0
                            value: row.isNote ? row.modelData.note.note : 0
                            onCommitted: (v) => root.setNote(row.modelData.index, "note", Math.round(v))
                        }
                        Cell { visible: row.isNote; Layout.preferredWidth: 40; text: row.isNote ? root.noteName(row.modelData.note.note) : "" }
                        NumberField {
                            visible: row.isNote
                            objectName: "eventVelocity"
                            Layout.preferredWidth: 70
                            from: 1; to: 127; decimals: 0
                            value: row.isNote ? row.modelData.note.velocity : 0
                            onCommitted: (v) => root.setNote(row.modelData.index, "velocity", Math.round(v))
                        }
                        Cell {
                            visible: !row.isNote
                            Layout.preferredWidth: 70
                            text: !row.modelData.c ? "" : (row.modelData.kind === "cc" ? String(row.modelData.c.data2) : (row.modelData.kind === "bend" ? String((row.modelData.c.data1 | (row.modelData.c.data2 << 7)) - 8192) : String(row.modelData.c.data1)))
                        }
                        NumberField {
                            visible: row.isNote
                            objectName: "eventLength"
                            Layout.preferredWidth: 70
                            from: 0.0078125; to: 4096; decimals: 2
                            value: row.isNote ? row.modelData.note.length : 0
                            onCommitted: (v) => root.setNote(row.modelData.index, "length", v)
                        }
                        Item { Layout.fillWidth: true }
                        IconButton { implicitHeight: 26; visible: row.isNote; label: qsTr("Delete"); onClicked: root.removeNote(row.modelData.index) }
                    }
                }
            }

            // ---- Marker
            ColumnLayout {
                spacing: 2
                Text { visible: root.markers.length === 0; text: qsTr("No markers yet."); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize }
                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: root.markers
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}
                    delegate: RowLayout {
                        id: mrow
                        required property var modelData
                        width: ListView.view.width - 10
                        spacing: Theme.spacing[2]
                        Cell { Layout.preferredWidth: 90; text: root.position(mrow.modelData.beats) }
                        TextEntry { Layout.fillWidth: true; text: mrow.modelData.name; onEdited: (t) => { if (t !== mrow.modelData.name) root.project.renameMarker(mrow.modelData.id, t) } }
                        IconButton { implicitHeight: 26; label: qsTr("Delete"); onClicked: root.project.removeMarker(mrow.modelData.id) }
                    }
                }
                IconButton { implicitHeight: 26; label: qsTr("New Marker at Playhead"); onClicked: root.project.createMarkerAtPlayhead() }
            }

            // ---- Tempo
            ColumnLayout {
                spacing: 2
                ListView {
                    objectName: "tempoList"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: root.tempos
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}
                    delegate: RowLayout {
                        id: trow
                        required property var modelData
                        required property int index
                        width: ListView.view.width - 10
                        spacing: Theme.spacing[2]
                        Cell { Layout.preferredWidth: 90; text: root.position(trow.modelData.beats) }
                        NumberField {
                            objectName: "tempoBpm"
                            Layout.preferredWidth: 110
                            from: 20; to: 999; decimals: 2
                            suffix: " bpm"
                            value: trow.modelData.bpm
                            onCommitted: (v) => root.project.setTempoAt(trow.modelData.beats, v)
                        }
                        Item { Layout.fillWidth: true }
                        IconButton { implicitHeight: 26; visible: trow.index > 0; label: qsTr("Delete"); onClicked: root.project.removeTempoAt(trow.modelData.beats) }
                    }
                }
                RowLayout {
                    spacing: Theme.spacing[2]
                    IconButton { implicitHeight: 26; label: qsTr("New Tempo at Playhead"); onClicked: root.project.setTempoAt(Math.round(root.project.positionBeats), root.project.bpm) }
                }
            }

            // ---- Signature
            ColumnLayout {
                spacing: 2
                ListView {
                    objectName: "signatureList"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: root.signatures
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}
                    delegate: RowLayout {
                        id: srow
                        required property var modelData
                        required property int index
                        width: ListView.view.width - 10
                        spacing: Theme.spacing[2]
                        Cell { Layout.preferredWidth: 90; text: root.position(srow.modelData.beats) }
                        NumberField {
                            objectName: "signatureNumerator"
                            Layout.preferredWidth: 60
                            from: 1; to: 32; decimals: 0
                            value: srow.modelData.numerator
                            onCommitted: (v) => root.project.setSignatureAt(srow.modelData.beats, Math.round(v), srow.modelData.denominator)
                        }
                        Cell { Layout.preferredWidth: 12; text: "/" }
                        SelectField {
                            Layout.preferredWidth: 70
                            choices: [1, 2, 4, 8, 16, 32]
                            value: srow.modelData.denominator
                            onChosen: (d) => root.project.setSignatureAt(srow.modelData.beats, srow.modelData.numerator, d)
                        }
                        Item { Layout.fillWidth: true }
                        IconButton { implicitHeight: 26; visible: srow.index > 0; label: qsTr("Delete"); onClicked: root.project.removeSignatureAt(srow.modelData.beats) }
                    }
                }
                IconButton { implicitHeight: 26; label: qsTr("New Signature at Playhead"); onClicked: root.project.setSignatureAt(Math.ceil(root.project.positionBeats / root.project.barBeats) * root.project.barBeats, root.project.beatsPerBar, 4) }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            IconButton { implicitHeight: 26; label: qsTr("Close"); onClicked: root.close() }
        }
    }
}
