import QtQuick
import QtQuick.Layouts
import Jad

// The default graphical layout of a stock effect or instrument: a knob for every parameter, a key for every on/off, a menu for every choice.
// `editor` is the EffectEditorWindow that owns the values.
Item {
    id: root
    required property var editor
    property var hidden: []                   // parameter names left out
    readonly property var choices: ({
        "mode": ["Low", "Average", "High"], "character": ["Clean", "Valve", "Punch", "Transparent"],
        "wave": ["Sine", "Triangle", "Saw", "Square"], "autoGain": ["Off", "On", "On (ratio)"],
        "outputDistortion": ["Off", "Soft", "Hard", "Clip"]
    })
    implicitHeight: flow.implicitHeight + 16
    Flow {
        id: flow
        x: 8; y: 8
        width: parent.width - 16
        spacing: 10
        Repeater {
            model: root.editor.spec ? root.editor.spec.params.filter(p => root.hidden.indexOf(p.name) < 0) : []
            delegate: Loader {
                id: slot
                required property var modelData
                readonly property bool isSwitch: modelData.min === 0 && modelData.max === 1
                readonly property var options: root.choices[modelData.name]
                sourceComponent: isSwitch ? sw : (options !== undefined ? menu : knob)
                Component {
                    id: knob
                    FxKnob {
                        label: slot.modelData.label
                        from: slot.modelData.min; to: slot.modelData.max; defaultValue: slot.modelData.def; logarithmic: slot.modelData.logarithmic
                        unit: slot.modelData.unit
                        bipolar: slot.modelData.min < 0 && slot.modelData.max > 0
                        value: root.editor.valueOf(slot.modelData)
                        onMoved: (v) => root.editor.edit(slot.modelData.name, v)
                        onReleased: (v) => root.editor.commit(slot.modelData.name, v)
                    }
                }
                Component {
                    id: sw
                    Column {
                        spacing: 4
                        width: 76
                        Item { width: 1; height: 14 }
                        FxSwitch {
                            anchors.horizontalCenter: parent.horizontalCenter
                            label: qsTr("On")
                            on: root.editor.valueOf(slot.modelData) >= 0.5
                            onToggled: (v) => root.editor.setNow(slot.modelData.name, v ? 1 : 0)
                        }
                        Text { width: parent.width; horizontalAlignment: Text.AlignHCenter; text: slot.modelData.label; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeCaptionSize; wrapMode: Text.WordWrap }
                    }
                }
                Component {
                    id: menu
                    Column {
                        spacing: 4
                        width: 96
                        Item { width: 1; height: 14 }
                        SelectField {
                            width: parent.width
                            choices: slot.options.map((n, i) => i)
                            value: Math.round(root.editor.valueOf(slot.modelData))
                            format: (i) => slot.options[i] ?? String(i)
                            onChosen: (c) => root.editor.setNow(slot.modelData.name, c)
                        }
                        Text { width: parent.width; horizontalAlignment: Text.AlignHCenter; text: slot.modelData.label.replace(/ \(.*\)$/, ""); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeCaptionSize; wrapMode: Text.WordWrap }
                    }
                }
            }
        }
    }
}
