import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Jad

// The Metronome tab of Project Settings (Record > Metronome Settings… opens it): Logic's page. Options, then the Audio Click (the Klopfgeist, Logic's own
// click synthesised per tick) with a row for each accent level (Bar, Group, Beat, Division: a note, a velocity and whether it sounds) and Tone and Volume,
// then the MIDI Click with the same four rows. The values are kept in the project's settings ("click.<level>.note", ...); the click subdivision
// (Beats, Eighths, Sixteenths or Grouped) says where the Division and Group clicks fall.
Item {
    id: root
    required property ProjectController project
    readonly property var values: project.projectSettings
    readonly property var levels: [{ id: "bar", label: qsTr("Bar"), note: 91, velocity: 127, on: true, midiNote: 91 },
                                   { id: "group", label: qsTr("Group"), note: 87, velocity: 119, on: true, midiNote: 87 },
                                   { id: "beat", label: qsTr("Beat"), note: 84, velocity: 112, on: true, midiNote: 84 },
                                   { id: "division", label: qsTr("Division"), note: 48, velocity: 60, on: false, midiNote: 48 }]
    readonly property var modes: [{ id: "beats", label: qsTr("Beats"), hint: "1  2  3  4" }, { id: "eighths", label: qsTr("Eighths"), hint: "1 &  2 &" },
                                  { id: "sixteenths", label: qsTr("Sixteenths"), hint: "1 e & a  2 e & a" }, { id: "grouped", label: qsTr("Grouped"), hint: "1 . .  2 . ." }]
    implicitHeight: column.implicitHeight

    function num(key, fallback) { const v = values[key]; return v === undefined ? fallback : parseFloat(v) }
    function flag(key, fallback) { const v = values[key]; return v === undefined ? fallback : v === "1" }
    function setNum(key, v) { project.setProjectSetting(key, String(v)) }
    function setFlag(key, on) { project.setProjectSetting(key, on ? "1" : "0") }
    // Logic names the middle C of the keyboard C3, so note 60 is C3 and 91 is G5
    function noteName(n) {
        const names = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
        return names[n % 12] + (Math.floor(n / 12) - 2)
    }

    component Head: Text { color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize; font.weight: Font.DemiBold; Layout.topMargin: Theme.spacing[2] }
    component Cap: Text { color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeLabelSize }
    component Val: Text { color: Theme.textValue; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize }
    component Opt: RowLayout {
        property string key
        property bool fallback: false
        property string label
        spacing: Theme.spacing[2]
        FlagCheck { objectName: "opt_" + parent.key; on: root.flag(parent.key, parent.fallback); onFlipped: root.setFlag(parent.key, !on) }
        Val { text: parent.label }
    }

    ColumnLayout {
        id: column
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: Theme.spacing[2]

        Head { text: qsTr("Options") }
        Opt { key: "click.simple"; label: qsTr("Simple mode") }
        Opt { key: "click.whileRecording"; fallback: true; label: qsTr("Click while recording") }
        Opt { key: "click.onlyCountIn"; label: qsTr("Only during count-in") }
        Opt { key: "click.whilePlaying"; fallback: true; label: qsTr("Click while playing") }
        Opt { key: "click.polyphonic"; label: qsTr("Polyphonic clicks") }
        RowLayout {
            spacing: Theme.spacing[2]
            Cap { Layout.preferredWidth: 90; text: qsTr("Subdivision") }
            Repeater {
                model: root.modes
                delegate: IconButton {
                    required property var modelData
                    objectName: "mode_" + modelData.id
                    implicitHeight: 22
                    label: modelData.label
                    active: root.project.clickMode === modelData.id
                    fillActive: true
                    fillText: Theme.textPrimary
                    onClicked: {
                        root.project.clickMode = modelData.id
                        if (modelData.id !== "beats" && modelData.id !== "grouped") root.setFlag("click.division.on", true)  // the Division row is what sounds the subdivisions
                    }
                }
            }
            Val { text: { for (const m of root.modes) if (m.id === root.project.clickMode) return m.hint; return "" } }
        }
        RowLayout {
            visible: root.project.clickMode === "grouped"
            spacing: Theme.spacing[2]
            Cap { Layout.preferredWidth: 90; text: qsTr("Groups") }
            Rectangle {
                Layout.preferredWidth: 120
                Layout.preferredHeight: 22
                color: Theme.surfaceRaised
                radius: Theme.radiusControl - 2
                border.color: groupField.activeFocus ? Theme.accentPrimary : Theme.borderSubtle
                TextInput {
                    id: groupField
                    objectName: "groupField"
                    anchors.fill: parent
                    anchors.leftMargin: 6
                    anchors.rightMargin: 6
                    verticalAlignment: TextInput.AlignVCenter
                    text: root.project.clickGrouping
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontTypeBodySize
                    onEditingFinished: root.project.clickGrouping = text
                }
            }
            Cap { Layout.fillWidth: true; text: qsTr("for example 3+2+2 in 7/8; empty: groups of three in 6/8, 9/8 and 12/8"); elide: Text.ElideRight }
        }

        Head { text: qsTr("Source: Audio Click") }
        Val { text: qsTr("Klopfgeist") }
        GridLayout {
            columns: 4
            columnSpacing: Theme.spacing[3]
            rowSpacing: Theme.spacing[1]
            Cap { text: "" }
            Cap { text: qsTr("Note") }
            Cap { text: qsTr("Velocity") }
            Cap { text: "" }
            Repeater {
                model: root.levels
                delegate: Item {
                    id: rowItem
                    required property var modelData
                    Layout.columnSpan: 4
                    Layout.fillWidth: true
                    implicitHeight: 24
                    RowLayout {
                        anchors.fill: parent
                        spacing: Theme.spacing[3]
                        RowLayout {
                            Layout.preferredWidth: 110
                            spacing: Theme.spacing[2]
                            FlagCheck { objectName: "on_" + rowItem.modelData.id; on: root.flag("click." + rowItem.modelData.id + ".on", rowItem.modelData.on); onFlipped: root.setFlag("click." + rowItem.modelData.id + ".on", !on) }
                            Val { text: rowItem.modelData.label }
                        }
                        NumberField {
                            objectName: "note_" + rowItem.modelData.id
                            implicitWidth: 70; decimals: 0; from: 0; to: 127
                            value: root.num("click." + rowItem.modelData.id + ".note", rowItem.modelData.note)
                            onCommitted: (v) => root.setNum("click." + rowItem.modelData.id + ".note", Math.round(v))
                        }
                        Cap { Layout.preferredWidth: 36; text: root.noteName(Math.round(root.num("click." + rowItem.modelData.id + ".note", rowItem.modelData.note))) }
                        NumberField {
                            objectName: "velocity_" + rowItem.modelData.id
                            implicitWidth: 70; decimals: 0; from: 1; to: 127
                            value: root.num("click." + rowItem.modelData.id + ".velocity", rowItem.modelData.velocity)
                            onCommitted: (v) => root.setNum("click." + rowItem.modelData.id + ".velocity", Math.round(v))
                        }
                        Item { Layout.fillWidth: true }
                    }
                }
            }
        }
        RowLayout {
            spacing: Theme.spacing[2]
            Cap { Layout.preferredWidth: 90; text: qsTr("Tone") }
            ParamSlider { objectName: "toneSlider"; width: 160; from: 0; to: 1; defaultValue: 0.5; value: root.num("click.tone", 0.5); onMoved: (v) => root.setNum("click.tone", v.toFixed(3)) }
            Cap { Layout.preferredWidth: 90; Layout.leftMargin: Theme.spacing[3]; text: qsTr("Volume") }
            ParamSlider { objectName: "volumeSlider"; width: 160; from: 0; to: 1; defaultValue: 0.8; value: root.num("click.volume", 0.8); onMoved: (v) => root.setNum("click.volume", v.toFixed(3)) }
        }
        RowLayout {
            spacing: Theme.spacing[2]
            Cap { Layout.preferredWidth: 90; text: qsTr("Output") }
            SelectField { Layout.preferredWidth: 160; choices: ["main"]; value: "main"; format: (id) => qsTr("Output 1-2"); onChosen: (c) => {} }
        }

        Head { text: qsTr("Source: MIDI Click") }
        RowLayout {
            spacing: Theme.spacing[3]
            Cap { Layout.preferredWidth: 110; text: "" }
            Cap { Layout.preferredWidth: 60; text: qsTr("Channel") }
            Cap { Layout.preferredWidth: 60; text: qsTr("Note") }
            Cap { Layout.preferredWidth: 60; text: qsTr("Velocity") }
        }
        Repeater {
            model: root.levels
            delegate: RowLayout {
                id: midiRow
                required property var modelData
                spacing: Theme.spacing[3]
                Val { Layout.preferredWidth: 110; text: midiRow.modelData.label }
                NumberField { implicitWidth: 60; decimals: 0; from: 1; to: 16; value: root.num("midiClick." + midiRow.modelData.id + ".channel", 10); onCommitted: (v) => root.setNum("midiClick." + midiRow.modelData.id + ".channel", Math.round(v)) }
                NumberField { implicitWidth: 60; decimals: 0; from: 0; to: 127; value: root.num("midiClick." + midiRow.modelData.id + ".note", midiRow.modelData.midiNote); onCommitted: (v) => root.setNum("midiClick." + midiRow.modelData.id + ".note", Math.round(v)) }
                NumberField { implicitWidth: 60; decimals: 0; from: 1; to: 127; value: root.num("midiClick." + midiRow.modelData.id + ".velocity", midiRow.modelData.velocity); onCommitted: (v) => root.setNum("midiClick." + midiRow.modelData.id + ".velocity", Math.round(v)) }
                Item { Layout.fillWidth: true }
            }
        }
        Cap { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: qsTr("The MIDI click is kept with the project; it is not sent to a MIDI port yet.") }
    }
}
