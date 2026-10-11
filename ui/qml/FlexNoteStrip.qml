import QtQuick
import Jad

// The inspector of the Audio Track Editor in Flex Pitch: the correction and the six hotspots of the selected note as sliders. While a slider
// moves the note is heard with that setting; letting go makes the edit.
Rectangle {
    id: root
    required property var project
    property string regionId
    property int noteIndex: -1
    property var note: null          // the note's map from flexPitchNotes
    color: Theme.surfacePanel

    readonly property var rows: [
        { key: "correct", label: qsTr("Pitch Correction"), from: 0, to: 100, def: 0, scale: 0.01, unit: "%" },
        { key: "fineCents", label: qsTr("Fine Pitch"), from: -50, to: 50, def: 0, scale: 1, unit: " ct" },
        { key: "vibrato", label: qsTr("Vibrato"), from: 0, to: 200, def: 100, scale: 0.01, unit: "%" },
        { key: "gainDb", label: qsTr("Gain"), from: -12, to: 12, def: 0, scale: 1, unit: " dB" },
        { key: "driftIn", label: qsTr("Pitch Drift In"), from: 0, to: 100, def: 0, scale: 0.01, unit: "%" },
        { key: "driftOut", label: qsTr("Pitch Drift Out"), from: 0, to: 100, def: 0, scale: 0.01, unit: "%" },
        { key: "formantCents", label: qsTr("Formant Shift"), from: -600, to: 600, def: 0, scale: 1, unit: " ct" }
    ]

    Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.borderSubtle }
    Column {
        anchors.fill: parent
        anchors.margins: Theme.spacing[3]
        spacing: Theme.spacing[2]
        Text {
            width: parent.width
            text: qsTr("Flex Pitch")
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTypeLabelSize
            font.weight: Theme.fontTypeTitleWeight
        }
        Text {
            width: parent.width
            wrapMode: Text.WordWrap
            visible: root.note === null
            text: qsTr("Select a note to edit it. Drag it up or down for its pitch, sideways to move it; the Scissors cut it, the Glue joins it with the next.")
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTypeCaptionSize
        }
        Repeater {
            model: root.note !== null ? root.rows : []
            delegate: Column {
                id: cell
                required property var modelData
                width: parent.width
                spacing: 1
                readonly property real current: root.note ? root.note[modelData.key] / modelData.scale : modelData.def
                Row {
                    width: parent.width
                    Text {
                        width: parent.width - valueText.implicitWidth
                        text: cell.modelData.label
                        color: Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontTypeCaptionSize
                    }
                    Text {
                        id: valueText
                        text: Math.round(slider.shown * 10) / 10 + cell.modelData.unit
                        color: Theme.textValue
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontTypeCaptionSize
                    }
                }
                ParamSlider {
                    id: slider
                    objectName: "flexSlider_" + cell.modelData.key
                    width: parent.width
                    from: cell.modelData.from
                    to: cell.modelData.to
                    defaultValue: cell.modelData.def
                    value: cell.current
                    function edit(v) { const e = {}; e[cell.modelData.key] = v * cell.modelData.scale; return e }
                    onMoved: (v) => root.project.flexNotePreview(root.regionId, root.noteIndex, edit(v))
                    onReleased: (v) => { root.project.flexNoteEdit(root.regionId, root.noteIndex, edit(v)); root.project.flexStopPreview() }
                }
            }
        }
    }
}
