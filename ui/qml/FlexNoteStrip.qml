import QtQuick
import Jad

// The six hotspots of a Flex Pitch note and its correction, as sliders under the editor: Pitch Correction, Fine Pitch, Vibrato, Gain, the
// drift into and out of the note, Formant Shift. While a slider moves the note is heard with that setting; letting go makes the edit.
Rectangle {
    id: root
    required property var project
    property string regionId
    property int noteIndex: -1
    property var note: null          // the note's map from flexPitchNotes
    color: Theme.surfacePanel

    readonly property var rows: [
        { key: "correct", label: qsTr("Correct"), from: 0, to: 100, def: 0, scale: 0.01, unit: "%" },
        { key: "fineCents", label: qsTr("Fine Pitch"), from: -50, to: 50, def: 0, scale: 1, unit: " ct" },
        { key: "vibrato", label: qsTr("Vibrato"), from: 0, to: 200, def: 100, scale: 0.01, unit: "%" },
        { key: "gainDb", label: qsTr("Gain"), from: -12, to: 12, def: 0, scale: 1, unit: " dB" },
        { key: "driftIn", label: qsTr("Drift In"), from: 0, to: 100, def: 0, scale: 0.01, unit: "%" },
        { key: "driftOut", label: qsTr("Drift Out"), from: 0, to: 100, def: 0, scale: 0.01, unit: "%" },
        { key: "formantCents", label: qsTr("Formant"), from: -600, to: 600, def: 0, scale: 1, unit: " ct" }
    ]

    Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.borderSubtle }
    Text {
        anchors.centerIn: parent
        visible: root.note === null
        text: qsTr("Select a note to edit it. Drag it up or down to change its pitch, sideways to move it; the Scissors cut it, the Glue joins it with the next.")
        color: Theme.textSecondary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontTypeCaptionSize
    }
    Row {
        visible: root.note !== null
        anchors.centerIn: parent
        spacing: Theme.spacing[3]
        Repeater {
            model: root.rows
            delegate: Column {
                id: cell
                required property var modelData
                spacing: 1
                readonly property real current: root.note ? root.note[modelData.key] / modelData.scale : modelData.def
                Text {
                    text: modelData.label + "  " + Math.round(slider.shown * 10) / 10 + modelData.unit
                    color: Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontTypeCaptionSize
                }
                ParamSlider {
                    id: slider
                    objectName: "flexSlider_" + modelData.key
                    width: 108
                    from: modelData.from
                    to: modelData.to
                    defaultValue: modelData.def
                    value: cell.current
                    function edit(v) { const e = {}; e[modelData.key] = v * modelData.scale; return e }
                    onMoved: (v) => root.project.flexNotePreview(root.regionId, root.noteIndex, edit(v))
                    onReleased: (v) => { root.project.flexNoteEdit(root.regionId, root.noteIndex, edit(v)); root.project.flexStopPreview() }
                }
            }
        }
    }
}
