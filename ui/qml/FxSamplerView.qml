import QtQuick
import Jad

// The Sampler: the zones of recorded sound over the keyboard (name, the key the sound has its own pitch at, the keys the zone answers to, gain, 1Shot and Loop),
// a keyboard strip that shows what each zone covers, and the Level, Attack and Release of the instrument.
FxShell {
    id: root
    title: qsTr("Sampler")
    bodyWidth: 900
    bodyHeight: 460
    chassis: "#1d1a17"
    readonly property color tint: "#ffb347"
    function val(name) { return editor.valueOf(editor.param(name)) }
    // the zones are read again whenever the project changes
    property var zones: []
    function refresh() { zones = editor.project.samplerZones(editor.trackId) }
    Component.onCompleted: refresh()
    Connections { target: root.editor.project; function onProjectChanged() { root.refresh() } }
    function noteName(n) { return ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"][n % 12] + (Math.floor(n / 12) - 2) }

    Rectangle { x: 0; y: 0; width: root.bodyWidth; height: root.bodyHeight; gradient: Gradient { GradientStop { position: 0; color: "#2a2520" } GradientStop { position: 1; color: "#16130f" } } }

    // the keyboard: a bar for every zone over the keys it answers to
    Rectangle {
        id: keyboard
        objectName: "samplerKeyboard"
        x: 24; y: 16; width: 852; height: 78; color: "#120f0c"; radius: 4
        readonly property real keyWidth: width / 128
        Repeater {
            model: 128
            delegate: Rectangle {
                required property int index
                readonly property bool black: [1, 3, 6, 8, 10].indexOf(index % 12) >= 0
                x: index * keyboard.keyWidth; y: black ? 0 : 36; width: keyboard.keyWidth; height: black ? 36 : 42
                color: black ? "#0a0806" : "#241f1a"; border.color: "#16130f"; border.width: 0.5
            }
        }
        Repeater {
            model: root.zones
            delegate: Rectangle {
                required property var modelData
                required property int index
                x: modelData.lo * keyboard.keyWidth; y: 4 + (index % 4) * 8; width: (modelData.hi - modelData.lo + 1) * keyboard.keyWidth; height: 7; radius: 2
                color: root.tint; opacity: 0.85
            }
        }
    }

    Text { x: 24; y: 104; text: qsTr("Zones"); color: "#9c8f80"; font.family: Theme.fontFamily; font.pixelSize: 17 }
    Text { x: 300; y: 108; text: qsTr("Root"); color: "#7d7164"; font.family: Theme.fontFamily; font.pixelSize: 13 }
    Text { x: 372; y: 108; text: qsTr("Low"); color: "#7d7164"; font.family: Theme.fontFamily; font.pixelSize: 13 }
    Text { x: 444; y: 108; text: qsTr("High"); color: "#7d7164"; font.family: Theme.fontFamily; font.pixelSize: 13 }
    Text { x: 516; y: 108; text: qsTr("Gain"); color: "#7d7164"; font.family: Theme.fontFamily; font.pixelSize: 13 }
    Text {
        visible: root.zones.length === 0
        x: 24; y: 140; width: 560; wrapMode: Text.WordWrap
        text: qsTr("No zones yet. Select audio regions and choose Track > Convert Regions to New Sampler Track.")
        color: "#7d7164"; font.family: Theme.fontFamily; font.pixelSize: 16
    }
    ListView {
        id: list
        objectName: "samplerZones"
        x: 24; y: 128; width: 640; height: 300
        clip: true
        model: root.zones
        delegate: Rectangle {
            id: row
            required property var modelData
            required property int index
            width: list.width; height: 34; color: index % 2 === 0 ? "#201c18" : "#1a1612"
            Text { x: 10; width: 250; anchors.verticalCenter: parent.verticalCenter; elide: Text.ElideRight; text: row.modelData.name; color: "#e8dccb"; font.family: Theme.fontFamily; font.pixelSize: 16 }
            component Num: FxField {
                id: num
                property string key
                property int zoneValue
                y: 5; width: 64; height: 24
                from: key === "gainDb" ? -60 : 0; to: key === "gainDb" ? 24 : 127; defaultValue: key === "gainDb" ? 0 : 60
                value: zoneValue; decimals: key === "gainDb" ? 1 : 0; snap: key !== "gainDb"
                textColor: "#e8dccb"; pixelSize: 15
                showUnit: false
                onReleased: (v) => { const c = {}; c[key] = key === "gainDb" ? v : Math.round(v); root.editor.project.setSamplerZone(root.editor.trackId, row.index, c) }
            }
            Num { x: 290; key: "root"; zoneValue: row.modelData.root }
            Num { x: 362; key: "lo"; zoneValue: row.modelData.lo }
            Num { x: 434; key: "hi"; zoneValue: row.modelData.hi }
            Num { x: 506; key: "gainDb"; zoneValue: row.modelData.gainDb }
            FxPlainKey {
                x: 580; y: 2; label: qsTr("1Shot"); pixelSize: 13; on: row.modelData.oneShot
                onToggled: (v) => root.editor.project.setSamplerZone(root.editor.trackId, row.index, { oneShot: v })
            }
            Text {
                x: 624 - 20; anchors.verticalCenter: parent.verticalCenter; text: "×"; color: "#9c8f80"; font.pixelSize: 20
                MouseArea { anchors.fill: parent; anchors.margins: -6; onClicked: root.editor.project.removeSamplerZone(root.editor.trackId, row.index) }
            }
        }
    }

    component Param: FxArcKnob {
        required property string name
        required property string caption
        tint: root.tint; labelColor: "#9c8f80"; showHeader: true
        readonly property var spec: root.editor.param(name)
        from: spec.min; to: spec.max; defaultValue: spec.def
        value: root.val(name)
        label: caption; dial: 70
        onMoved: (v) => root.editor.edit(name, v)
        onReleased: (v) => root.editor.commit(name, v)
    }
    Param { x: 700; y: 150; name: "level"; caption: qsTr("Level"); unit: "dB"; decimals: 1; minLabel: "-24"; maxLabel: "6" }
    Param { x: 700; y: 270; name: "attack"; caption: qsTr("Attack"); unit: "ms"; decimals: 1; minLabel: "0.5"; maxLabel: "2000" }
    Param { x: 790; y: 150; name: "release"; caption: qsTr("Release"); unit: "ms"; decimals: 0; minLabel: "1"; maxLabel: "5000" }
}
