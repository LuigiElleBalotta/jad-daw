import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Jad

// Track > New Session Player Track: a Drummer, Bass Player or Keyboard Player that plays a chord progression in a style; the part is a MIDI region
// on a new track, which can be edited like any other.
Dialog {
    id: root
    required property ProjectController project
    modal: true
    anchors.centerIn: parent
    width: 480
    property string kind: "drummer"
    property string style: "rock"
    property string chords: "Am F C G"
    property real repeats: 4
    property real intensity: 60
    property real swing: 0
    property int seed: 1
    readonly property var kinds: [{ id: "drummer", label: qsTr("Drummer") }, { id: "bass", label: qsTr("Bass Player") }, { id: "keys", label: qsTr("Keyboard Player") }]
    readonly property var styles: ({ drummer: ["rock", "pop", "funk", "ballad", "shuffle"], bass: ["root", "eighths", "walking", "funk"], keys: ["block", "arpeggio", "comp", "pad"] })
    background: Rectangle { color: Theme.surfacePanel; border.color: Theme.borderStrong; radius: Theme.radiusDialog }
    header: Item { height: 0 }

    function create() {
        project.newSessionPlayer({ kind: kind, style: style, chords: chords, repeats: Math.round(repeats), intensity: intensity, swing: swing, seed: seed })
        seed = seed + 1   // the next one made with the same settings plays differently
        close()
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacing[3]
        Text { text: qsTr("New Session Player Track"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeTitleSize; font.weight: Theme.fontTypeTitleWeight }
        RowLayout {
            spacing: Theme.spacing[2]
            Repeater {
                model: root.kinds
                delegate: IconButton {
                    required property var modelData
                    objectName: "sessionKind_" + modelData.id
                    implicitHeight: 26
                    label: modelData.label
                    active: root.kind === modelData.id
                    onClicked: { root.kind = modelData.id; root.style = root.styles[modelData.id][0] }
                }
            }
        }
        RowLayout {
            spacing: Theme.spacing[2]
            Text { text: qsTr("Style"); Layout.preferredWidth: 110; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeLabelSize }
            SelectField {
                Layout.preferredWidth: 200
                choices: root.styles[root.kind]
                value: root.style
                format: (c) => c.charAt(0).toUpperCase() + c.slice(1)
                onChosen: (c) => root.style = c
            }
        }
        RowLayout {
            spacing: Theme.spacing[2]
            Text { text: qsTr("Chords"); Layout.preferredWidth: 110; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeLabelSize }
            Rectangle {
                Layout.fillWidth: true; Layout.preferredHeight: 24
                color: Theme.surfaceRaised; radius: Theme.radiusControl - 2
                border.color: chordField.activeFocus ? Theme.accentPrimary : Theme.borderSubtle
                TextInput {
                    id: chordField
                    objectName: "sessionChords"
                    anchors.fill: parent; anchors.margins: 5
                    verticalAlignment: TextInput.AlignVCenter
                    text: root.chords
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontTypeBodySize
                    onTextEdited: root.chords = text
                    onAccepted: root.create()
                }
            }
        }
        Text {
            text: qsTr("One chord a bar, separated by spaces: Am7 D7 Gmaj7 Cmaj7. C.G splits a bar, % repeats, - is a rest, C/E is a slash chord.")
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTypeCaptionSize
        }
        Repeater {
            model: [{ key: "repeats", label: qsTr("Repeats"), from: 1, to: 16, def: 4, unit: "×" },
                    { key: "intensity", label: qsTr("Intensity"), from: 0, to: 100, def: 60, unit: "%" },
                    { key: "swing", label: qsTr("Swing"), from: 0, to: 100, def: 0, unit: "%" }]
            delegate: RowLayout {
                id: row
                required property var modelData
                spacing: Theme.spacing[2]
                Text { text: row.modelData.label; Layout.preferredWidth: 110; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeLabelSize }
                ParamSlider {
                    objectName: "sessionSlider_" + row.modelData.key
                    Layout.fillWidth: true
                    from: row.modelData.from
                    to: row.modelData.to
                    defaultValue: row.modelData.def
                    value: root[row.modelData.key]
                    onMoved: (v) => root[row.modelData.key] = v
                    onReleased: (v) => root[row.modelData.key] = v
                }
                Text {
                    Layout.preferredWidth: 50
                    horizontalAlignment: Text.AlignRight
                    text: Math.round(root[row.modelData.key]) + row.modelData.unit
                    color: Theme.textValue
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontTypeLabelSize
                }
            }
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: Theme.spacing[2]
            IconButton { implicitHeight: 26; label: qsTr("Cancel"); onClicked: root.close() }
            IconButton { objectName: "createButton"; implicitHeight: 26; label: qsTr("Create"); active: true; fillActive: true; fillText: Theme.textPrimary; onClicked: root.create() }
        }
    }
}
