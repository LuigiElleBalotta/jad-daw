import QtQuick
import QtQuick.Window
import Jad

// Window > Show Event Float: the note selected in the Piano Roll (position, length, pitch, velocity), else the selected region (position, length).
Window {
    id: root
    required property ProjectController project
    property var piano: null                       // the Piano Roll whose selection is shown
    readonly property int index: piano && piano.selected.length === 1 ? piano.selected[0] : -1
    readonly property var note: index >= 0 && piano.notes[index] ? piano.notes[index] : null
    readonly property var region: { project.revision; return project.selectedRegionIds.length > 0 ? project.regionInfo(project.selectedRegionIds[0]) : ({ found: false }) }

    function setNote(key, value) {
        const list = piano.copyNotes(piano.notes)
        list[index][key] = value
        piano.commit(list)
    }

    width: 300
    height: 190
    title: qsTr("Event Float")
    color: Theme.surfacePanel
    flags: Qt.Tool | Qt.WindowStaysOnTopHint

    component Line: Row {
        property string caption
        default property alias content: slot.data
        spacing: Theme.spacing[3]
        height: 26
        Text { width: 90; anchors.verticalCenter: parent.verticalCenter; text: parent.caption; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize }
        Item { id: slot; width: 150; height: 26 }
    }

    Column {
        anchors.fill: parent
        anchors.margins: Theme.spacing[4]
        spacing: Theme.spacing[2]
        Text {
            width: parent.width
            text: root.note ? qsTr("Note %1").arg(root.piano.noteName(root.note.note)) : (root.region.found === true ? qsTr("Region") : qsTr("Nothing selected"))
            color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeTitleSize; font.weight: Theme.fontTypeTitleWeight
        }
        Line {
            visible: root.note !== null
            caption: qsTr("Position")
            NumberField { objectName: "eventPosition"; from: 0; to: 100000; decimals: 3; value: root.note ? root.piano.regionStart + root.note.start : 0; onCommitted: (v) => root.setNote("start", Math.max(0, v - root.piano.regionStart)) }
        }
        Line {
            visible: root.note !== null
            caption: qsTr("Length")
            NumberField { objectName: "eventLength"; from: 0.0625; to: 1000; decimals: 3; value: root.note ? root.note.length : 1; onCommitted: (v) => root.setNote("length", v) }
        }
        Line {
            visible: root.note !== null
            caption: qsTr("Pitch")
            NumberField { objectName: "eventPitch"; from: 0; to: 127; decimals: 0; value: root.note ? root.note.note : 60; onCommitted: (v) => root.setNote("note", Math.round(v)) }
        }
        Line {
            visible: root.note !== null
            caption: qsTr("Velocity")
            NumberField { objectName: "eventVelocity"; from: 1; to: 127; decimals: 0; value: root.note ? root.note.velocity : 80; onCommitted: (v) => root.setNote("velocity", Math.round(v)) }
        }
        Line {
            visible: root.note === null && root.region.found === true
            caption: qsTr("Position")
            NumberField { objectName: "regionPosition"; from: 0; to: 100000; decimals: 3; value: root.region.startBeats ?? 0; onCommitted: (v) => root.project.moveRegion(root.project.selectedRegionIds[0], v) }
        }
        Line {
            visible: root.note === null && root.region.found === true
            caption: qsTr("Length")
            NumberField { objectName: "regionLength"; from: 0.0625; to: 100000; decimals: 3; value: root.region.lengthBeats ?? 1; onCommitted: (v) => root.project.resizeRegion(root.project.selectedRegionIds[0], root.region.startBeats, v) }
        }
    }
}
