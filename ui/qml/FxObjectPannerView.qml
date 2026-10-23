import QtQuick
import Jad

// 3D Object Panner of a Dolby Atmos project: the room from above (the listener in the middle, the front at the top) with the object as a dot that is dragged,
// a bar for the height, a knob for the size, and the choice of how the object is heard: binaural over headphones or as a plain stereo pan.
FxShell {
    id: root
    title: qsTr("3D Object Panner")
    bodyWidth: 640
    bodyHeight: 380
    chassis: "#1f3249"
    function val(name) { return editor.valueOf(editor.param(name)) }
    readonly property real objX: val("x")
    readonly property real objY: val("y")
    readonly property real objZ: val("z")
    readonly property real objSize: val("size")
    readonly property int format: Math.round(val("mode"))
    readonly property var formatNames: [qsTr("Stereo pan"), qsTr("Binaural"), qsTr("2.0 Direct"), qsTr("2.0 from 5.1"), qsTr("5.1"), qsTr("7.1"), qsTr("5.1.2"), qsTr("5.1.4"), qsTr("7.1.4")]

    Rectangle { x: 0; y: 0; width: root.bodyWidth; height: root.bodyHeight; gradient: Gradient { GradientStop { position: 0; color: "#2a4663" } GradientStop { position: 1; color: "#1d3148" } } }

    // the room from above
    Rectangle {
        id: room
        x: 24; y: 24; width: 320; height: 320
        color: "#0d1823"
        border.color: "#3a5878"
        Repeater {
            model: 5
            delegate: Item {
                required property int index
                Rectangle { x: room.width * index / 4; y: 0; width: 1; height: room.height; color: index === 2 ? "#2a4663" : "#18293a" }
                Rectangle { x: 0; y: room.height * index / 4; width: room.width; height: 1; color: index === 2 ? "#2a4663" : "#18293a" }
            }
        }
        Text { x: room.width / 2 - 20; y: 4; width: 40; horizontalAlignment: Text.AlignHCenter; text: qsTr("Front"); color: "#6c8196"; font.family: Theme.fontFamily; font.pixelSize: 12 }
        Text { x: room.width / 2 - 20; y: room.height - 18; width: 40; horizontalAlignment: Text.AlignHCenter; text: qsTr("Back"); color: "#6c8196"; font.family: Theme.fontFamily; font.pixelSize: 12 }
        Rectangle {  // the listener
            x: room.width / 2 - 9; y: room.height / 2 - 9; width: 18; height: 18; radius: 9; color: "#2a4663"; border.color: "#8da2b8"
            Rectangle { x: 7; y: -5; width: 4; height: 6; color: "#8da2b8" }
        }
        Rectangle {  // the size of the object
            readonly property real d: 30 + root.objSize * 150
            x: dot.x + dot.width / 2 - d / 2; y: dot.y + dot.height / 2 - d / 2; width: d; height: d; radius: d / 2
            color: "#208dffa0"; border.color: "#608dffa0"
        }
        Rectangle {
            id: dot
            objectName: "objectDot"
            width: 20; height: 20; radius: 10
            x: (root.objX + 1) / 2 * room.width - width / 2
            y: (1 - root.objY) / 2 * room.height - height / 2
            color: "#8dffa0"; border.color: "#d6ffe0"; border.width: 2
            scale: 0.8 + root.objZ * 0.6  // an object that is high looks nearer
        }
        MouseArea {
            anchors.fill: parent
            function place(mouse, done) {
                const x = Math.max(-1, Math.min(1, mouse.x / room.width * 2 - 1))
                const y = Math.max(-1, Math.min(1, 1 - mouse.y / room.height * 2))
                if (done) root.editor.commitMany({ x: x, y: y })
                else { root.editor.edit("x", x); root.editor.edit("y", y) }
            }
            onPressed: (mouse) => place(mouse, false)
            onPositionChanged: (mouse) => { if (pressed) place(mouse, false) }
            onReleased: (mouse) => place(mouse, true)
        }
    }

    // the height
    Item {
        x: 372; y: 24; width: 80; height: 320
        Text { width: parent.width; horizontalAlignment: Text.AlignHCenter; text: qsTr("Height"); color: "#9db2c8"; font.family: Theme.fontFamily; font.pixelSize: 19 }
        Rectangle {
            id: bar
            x: 34; y: 34; width: 12; height: 250; radius: 6; color: "#0d1823"; border.color: "#3a5878"
            Rectangle { width: parent.width; height: parent.height * root.objZ; y: parent.height - height; radius: 6; color: "#5cc8ff" }
            Rectangle { x: -6; width: 24; height: 8; radius: 4; y: parent.height * (1 - root.objZ) - 4; color: "#d6e2ee" }
            MouseArea {
                x: -20; width: parent.width + 40; height: parent.height
                function place(mouse, done) {
                    const z = Math.max(0, Math.min(1, 1 - mouse.y / bar.height))
                    if (done) root.editor.commit("z", z)
                    else root.editor.edit("z", z)
                }
                onPressed: (mouse) => place(mouse, false)
                onPositionChanged: (mouse) => { if (pressed) place(mouse, false) }
                onReleased: (mouse) => place(mouse, true)
            }
        }
        Text { x: 0; y: 292; width: parent.width; horizontalAlignment: Text.AlignHCenter; text: (root.objZ * 100).toFixed(0) + " %"; color: "#d6e2ee"; font.family: Theme.fontFamily; font.pixelSize: 17 }
    }

    // size and monitoring
    FxArcKnob {
        id: sizeKnob
        x: 500; y: 40
        showHeader: true
        label: qsTr("Size"); unit: ""; decimals: 2; dial: 72; minLabel: "0"; maxLabel: "1"
        readonly property var spec: root.editor.param("size")
        from: spec.min; to: spec.max; defaultValue: spec.def
        value: root.objSize
        onMoved: (v) => root.editor.edit("size", v)
        onReleased: (v) => root.editor.commit("size", v)
    }
    Item {
        x: 470; y: 200; width: 150; height: 90
        Text { width: parent.width; horizontalAlignment: Text.AlignHCenter; text: qsTr("Monitoring"); color: "#9db2c8"; font.family: Theme.fontFamily; font.pixelSize: 19 }
        Rectangle {
            x: 10; y: 34; width: 130; height: 36; radius: 4; color: "#12202e"
            Text { objectName: "monitoringFormat"; anchors.centerIn: parent; text: root.formatNames[Math.max(0, Math.min(8, root.format))]; color: root.format >= 4 ? "#5cc8ff" : "#8dffa0"; font.family: Theme.fontFamily; font.pixelSize: 20 }
        }
        Text { x: 0; y: 76; width: parent.width; horizontalAlignment: Text.AlignHCenter; text: qsTr("set in Mix > Dolby Atmos"); color: "#6c8196"; font.family: Theme.fontFamily; font.pixelSize: 12 }
    }
}
