import QtQuick
import QtQuick.Controls.Basic
import Jad

// Noise Gate of Logic Pro 11.2: the Gate and Ducker tabs, Threshold and Reduction knobs and the Hysteresis slider on the left; the Open and Close lights, the Attack, Hold
// and Release knobs and the Lookahead slider in the middle; the side chain filter (its power key, the Characteristics menu, the cutoff sliders) on the right.
FxShell {
    id: root
    title: qsTr("Noise Gate")
    bodyWidth: 1029
    bodyHeight: 336
    withSideChain: true
    chassis: "#1f3249"
    readonly property var meters: editor.project.editorMeters
    readonly property bool closed: (meters.reductionDb ?? 0) > 3
    readonly property bool ducker: val("ducker") >= 0.5
    readonly property var keyModes: [qsTr("Bandpass"), qsTr("Lowpass"), qsTr("Highpass")]
    function val(name) { return editor.valueOf(editor.param(name)) }

    Rectangle {
        x: 0; y: 0; width: root.bodyWidth; height: root.bodyHeight
        gradient: Gradient { GradientStop { position: 0; color: "#2a4663" } GradientStop { position: 1; color: "#1d3148" } }
    }
    Rectangle { x: 315; y: 20; width: 1; height: 296; color: "#17283a" }
    Rectangle { x: 726; y: 20; width: 1; height: 296; color: "#17283a" }

    Row {  // Gate | Ducker
        x: 90; y: 18
        Repeater {
            model: [qsTr("Gate"), qsTr("Ducker")]
            delegate: Rectangle {
                required property string modelData
                required property int index
                readonly property bool picked: (index === 1) === root.ducker
                width: 70; height: 27; color: picked ? "#162739" : "#2a4056"
                Text { anchors.centerIn: parent; text: parent.modelData; color: picked ? "#8dffa0" : "#8da2b8"; font.family: Theme.fontFamily; font.pixelSize: 19 }
                MouseArea { anchors.fill: parent; onClicked: root.editor.setNow("ducker", index) }
            }
        }
    }
    component K: FxArcKnob {
        id: k
        required property string param
        required property real cxPt
        x: cxPt - width / 2; y: 40
        readonly property var spec: root.editor.param(param)
        from: spec.min; to: spec.max; defaultValue: spec.def; logarithmic: spec.logarithmic
        minLabel: from === Math.round(from) ? String(from) : from.toFixed(1)
        maxLabel: to >= 1000 ? (to / 1000) + " k" : String(to)
        value: root.val(param)
        onMoved: (v) => root.editor.edit(param, v)
        onReleased: (v) => root.editor.commit(param, v)
    }
    K { param: "threshold"; cxPt: 87; label: qsTr("Threshold"); unit: "dB"; decimals: 0; dial: 87 }
    K { param: "range"; cxPt: 231; label: root.ducker ? qsTr("Ducking") : qsTr("Reduction"); unit: "dB"; decimals: 0; dial: 87 }
    K { param: "attack"; cxPt: 397; label: qsTr("Attack"); unit: "ms"; decimals: 0; dial: 67 }
    K { param: "hold"; cxPt: 522; label: qsTr("Hold"); unit: "ms"; decimals: 0; dial: 67 }
    K { param: "release"; cxPt: 647; label: qsTr("Release"); unit: "ms"; decimals: 1; dial: 67 }
    FxHSlider {
        x: 47; y: 247; width: 202
        property var p: root.editor.param("hysteresis")
        label: qsTr("Hysteresis"); unit: "dB"; from: p.min; to: p.max; defaultValue: p.def
        value: root.val("hysteresis")
        onMoved: (v) => root.editor.edit("hysteresis", v)
        onReleased: (v) => root.editor.commit("hysteresis", v)
    }
    FxHSlider {
        x: 420; y: 247; width: 201
        property var p: root.editor.param("lookahead")
        label: qsTr("Lookahead"); unit: "ms"; from: p.min; to: p.max; defaultValue: p.def
        value: root.val("lookahead")
        onMoved: (v) => root.editor.edit("lookahead", v)
        onReleased: (v) => root.editor.commit("lookahead", v)
    }
    Row {  // Open and Close
        x: 470; y: 22
        spacing: 38
        Row { spacing: 8; Text { text: qsTr("Open"); color: "#7f93a8"; font.family: Theme.fontFamily; font.pixelSize: 19 } Rectangle { y: 7; width: 10; height: 10; radius: 5; color: !root.closed ? "#8dffa0" : "#0f1a26" } }
        Row { spacing: 8; Text { text: qsTr("Close"); color: "#7f93a8"; font.family: Theme.fontFamily; font.pixelSize: 19 } Rectangle { y: 7; width: 10; height: 10; radius: 5; color: root.closed ? "#8dffa0" : "#0f1a26" } }
    }
    // the side chain filter
    Text { x: 837 - width / 2; y: 66; text: qsTr("Characteristics"); color: "#9db2c8"; font.family: Theme.fontFamily; font.pixelSize: 19 }
    Text {
        objectName: "gateCharacteristics"
        x: 837 - width / 2; y: 88; text: root.keyModes[Math.round(root.val("keyMode"))]; color: "#8dffa0"; font.family: Theme.fontFamily; font.pixelSize: 27
        MouseArea { anchors.fill: parent; anchors.margins: -6; onClicked: keyModeMenu.popup(parent, 0, parent.height) }
        Menu {
            id: keyModeMenu
            Repeater {
                model: root.keyModes
                delegate: MenuItem {
                    required property string modelData
                    required property int index
                    text: modelData
                    checkable: true
                    checked: Math.round(root.val("keyMode")) === index
                    onTriggered: root.editor.setNow("keyMode", index)
                }
            }
        }
    }
    Text { x: 970 - width / 2; y: 66; text: qsTr("Monitor"); color: "#9db2c8"; font.family: Theme.fontFamily; font.pixelSize: 19 }
    Rectangle {
        objectName: "gateMonitor"
        readonly property bool lit: root.val("keyMonitor") >= 0.5
        x: 944; y: 91; width: 48; height: 30; radius: 4; color: lit ? "#1f6b3c" : "#12202e"; opacity: root.val("keyFilter") >= 0.5 ? 1 : 0.6
        Text { anchors.centerIn: parent; text: parent.lit ? qsTr("ON") : qsTr("OFF"); color: parent.lit ? "#d8ffe4" : "#6c8196"; font.family: Theme.fontFamily; font.pixelSize: 20 }
        MouseArea { anchors.fill: parent; onClicked: root.editor.setNow("keyMonitor", parent.lit ? 0 : 1) }
    }
    Item {  // the power key of the filter and the two cutoffs
        x: 747; y: 165; width: 260; height: 140
        Rectangle {
            x: 4; y: 66; width: 22; height: 22; radius: 11; color: "transparent"; border.color: root.val("keyFilter") >= 0.5 ? "#8dffa0" : "#7f93a8"; border.width: 1.5
            Rectangle { x: 10; y: 3; width: 2; height: 9; color: parent.border.color }
            MouseArea { anchors.fill: parent; onClicked: root.editor.setNow("keyFilter", root.val("keyFilter") >= 0.5 ? 0 : 1) }
        }
        Rectangle { x: 15; y: 40; width: 1; height: 26; color: "#32485f" }
        Rectangle { x: 15; y: 88; width: 1; height: 50; color: "#32485f" }
        FxHSlider {
            x: 40; y: 0; width: 240
            property var p: root.editor.param("keyHighCut")
            label: qsTr("High Cutoff"); unit: "Hz"; decimals: 0; logarithmic: true; from: p.min; to: p.max; defaultValue: p.def
            value: root.val("keyHighCut"); dimmed: root.val("keyFilter") < 0.5
            onMoved: (v) => root.editor.edit("keyHighCut", v)
            onReleased: (v) => root.editor.commit("keyHighCut", v)
        }
        FxHSlider {
            x: 40; y: 84; width: 240
            property var p: root.editor.param("keyLowCut")
            label: qsTr("Low Cutoff"); unit: "Hz"; decimals: 0; logarithmic: true; from: p.min; to: p.max; defaultValue: p.def
            value: root.val("keyLowCut"); dimmed: root.val("keyFilter") < 0.5
            onMoved: (v) => root.editor.edit("keyLowCut", v)
            onReleased: (v) => root.editor.commit("keyLowCut", v)
        }
    }
}
