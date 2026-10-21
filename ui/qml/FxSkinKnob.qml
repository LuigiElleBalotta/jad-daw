import QtQuick
import Jad

// The knob of the Compressor window: a dial in the style of the circuit (silver, black, chrome, blue or FET), a ring of dots with the numbers of the scale round it, the
// unit under it. `stops` lists the marked values in order, spread evenly over 270 degrees: {v, label} (a stop without a label is a dot only). The value goes between
// the stops in a straight line, so a scale like 1, 1.4, 2, 3, 5, 8, 12, 20, 30 is as it is on the real knob. Drag up and down (Shift: fine), scroll, double-click or
// Option-click to put the default back.
Item {
    id: root
    property var stops: []
    property real value: 0
    property real defaultValue: 0
    property string unit: ""
    property string style: "silver"          // silver, ridged, chrome, blue, fet
    property real dial: 76                   // diameter of the dial in points
    property color labelColor: "#d7dfe8"
    property color tickColor: "#d7dfe8"
    property real ringRadius: 58             // where the numbers sit
    property int labelSize: 13
    signal moved(real value)
    signal released(real value)

    implicitWidth: 150
    implicitHeight: 150
    width: implicitWidth
    height: implicitHeight
    property real dragValue: value
    property bool dragging: false
    property bool changed: false
    readonly property real shown: dragging ? dragValue : value
    readonly property real lo: stops.length > 0 ? stops[0].v : 0
    readonly property real hi: stops.length > 0 ? stops[stops.length - 1].v : 1
    function toUnit(v) {
        const s = stops
        if (s.length < 2) return 0
        if (v <= s[0].v) return 0
        if (v >= s[s.length - 1].v) return 1
        for (let i = 0; i + 1 < s.length; ++i)
            if (v <= s[i + 1].v) return (i + (v - s[i].v) / (s[i + 1].v - s[i].v)) / (s.length - 1)
        return 1
    }
    function fromUnit(t) {
        const s = stops
        if (s.length < 2) return 0
        t = Math.max(0, Math.min(1, t))
        const pos = t * (s.length - 1), i = Math.min(s.length - 2, Math.floor(pos))
        return s[i].v + (pos - i) * (s[i + 1].v - s[i].v)
    }
    function cancel() { dragging = false; changed = false; dragValue = value }
    readonly property real angle: -135 + toUnit(shown) * 270       // degrees from the top, clockwise
    readonly property real cx: width / 2
    readonly property real cy: height / 2

    // the numbers and the dots
    Repeater {
        model: root.stops
        delegate: Item {
            required property var modelData
            required property int index
            readonly property real a: (-135 + 270 * index / Math.max(1, root.stops.length - 1)) * Math.PI / 180
            Rectangle {
                x: root.cx + Math.sin(parent.a) * (root.dial / 2 + 8) - 1.5
                y: root.cy - Math.cos(parent.a) * (root.dial / 2 + 8) - 1.5
                width: 3; height: 3; radius: 1.5
                color: root.tickColor
            }
            Text {
                visible: modelData.label !== undefined && modelData.label !== ""
                text: modelData.label ?? ""
                color: root.labelColor
                font.family: Theme.fontFamily
                font.pixelSize: root.labelSize
                x: root.cx + Math.sin(parent.a) * (root.dial / 2 + 21) - width / 2
                y: root.cy - Math.cos(parent.a) * (root.dial / 2 + 17) - height / 2
            }
        }
    }
    Text {  // the unit
        visible: root.unit !== ""
        text: root.unit
        color: root.labelColor
        font.family: Theme.fontFamily
        font.pixelSize: root.labelSize
        x: root.cx - width / 2
        y: root.cy + root.dial / 2 + 10
    }

    // the dial
    Canvas {
        id: face
        x: root.cx - root.dial / 2 - 6
        y: root.cy - root.dial / 2 - 6
        width: root.dial + 12
        height: root.dial + 12
        readonly property string st: root.style
        onStChanged: requestPaint()
        onWidthChanged: requestPaint()
        Component.onCompleted: requestPaint()
        onPaint: {
            const c = getContext("2d")
            c.reset()
            const r = root.dial / 2, mx = width / 2, my = height / 2
            // the shadow under the dial
            const sh = c.createRadialGradient(mx, my + 2, r * 0.8, mx, my + 2, r + 5)
            sh.addColorStop(0, "rgba(0,0,0,0.55)"); sh.addColorStop(1, "rgba(0,0,0,0)")
            c.fillStyle = sh; c.beginPath(); c.arc(mx, my + 2, r + 5, 0, Math.PI * 2); c.fill()
            if (st === "silver" || st === "ridged" || st === "fet") {
                // the rim
                const rim = c.createConicalGradient(mx, my, 0)
                for (let i = 0; i <= 12; ++i) rim.addColorStop(i / 12, i % 2 ? "#f4f4f4" : "#6e6e72")
                c.fillStyle = rim; c.beginPath(); c.arc(mx, my, r, 0, Math.PI * 2); c.fill()
            }
            if (st === "silver") {
                const g = c.createRadialGradient(mx - r * 0.25, my - r * 0.3, r * 0.1, mx, my, r * 0.9)
                g.addColorStop(0, "#fbfbfb"); g.addColorStop(0.55, "#bdbdbf"); g.addColorStop(1, "#8b8b90")
                c.fillStyle = g; c.beginPath(); c.arc(mx, my, r * 0.88, 0, Math.PI * 2); c.fill()
            } else if (st === "ridged") {
                c.fillStyle = "#070707"; c.beginPath(); c.arc(mx, my, r, 0, Math.PI * 2); c.fill()
                c.strokeStyle = "#3a3a3a"; c.lineWidth = 1
                for (let i = 0; i < 60; ++i) { const a = i / 60 * Math.PI * 2; c.beginPath(); c.moveTo(mx + Math.cos(a) * r * 0.9, my + Math.sin(a) * r * 0.9); c.lineTo(mx + Math.cos(a) * r, my + Math.sin(a) * r); c.stroke() }
                const g = c.createRadialGradient(mx - r * 0.2, my - r * 0.25, r * 0.1, mx, my, r * 0.85)
                g.addColorStop(0, "#3c3c3e"); g.addColorStop(1, "#0d0d0e")
                c.fillStyle = g; c.beginPath(); c.arc(mx, my, r * 0.82, 0, Math.PI * 2); c.fill()
            } else if (st === "fet") {
                c.fillStyle = "#0b0b0c"; c.beginPath(); c.arc(mx, my, r, 0, Math.PI * 2); c.fill()
                const g1 = c.createLinearGradient(mx, my - r, mx, my + r)
                g1.addColorStop(0, "#4a4a4d"); g1.addColorStop(1, "#050505")
                c.fillStyle = g1; c.beginPath(); c.arc(mx, my, r * 0.97, 0, Math.PI * 2); c.fill()
                const cap = c.createConicalGradient(mx, my, 0.4)
                for (let i = 0; i <= 10; ++i) cap.addColorStop(i / 10, i % 2 ? "#f2f2f2" : "#8a8a8e")
                c.fillStyle = cap; c.beginPath(); c.arc(mx, my, r * 0.52, 0, Math.PI * 2); c.fill()
            } else if (st === "chrome") {
                const g = c.createRadialGradient(mx - r * 0.3, my - r * 0.35, r * 0.05, mx, my, r)
                g.addColorStop(0, "#f2eee8"); g.addColorStop(0.5, "#9a8d86"); g.addColorStop(1, "#4d3f3d")
                c.fillStyle = g; c.beginPath(); c.arc(mx, my, r, 0, Math.PI * 2); c.fill()
                c.lineWidth = 2; c.strokeStyle = "#2c2020"; c.stroke()
                // the brushed slot across it
                c.save(); c.translate(mx, my); c.rotate(root.angle * Math.PI / 180)
                const bar = c.createLinearGradient(-r * 0.22, 0, r * 0.22, 0)
                bar.addColorStop(0, "#f4f1ec"); bar.addColorStop(0.45, "#6f6660"); bar.addColorStop(1, "#d8d2cc")
                c.fillStyle = bar; c.fillRect(-r * 0.2, -r, r * 0.4, r * 2)
                c.restore()
            } else if (st === "blue") {
                const rim = c.createConicalGradient(mx, my, 0)
                for (let i = 0; i <= 12; ++i) rim.addColorStop(i / 12, i % 2 ? "#e8e8ea" : "#7d7d82")
                c.fillStyle = rim; c.beginPath(); c.arc(mx, my, r, 0, Math.PI * 2); c.fill()
                const g = c.createRadialGradient(mx - r * 0.25, my - r * 0.3, r * 0.05, mx, my, r * 0.86)
                g.addColorStop(0, "#3d8be0"); g.addColorStop(0.6, "#165ba8"); g.addColorStop(1, "#0d3f7d")
                c.fillStyle = g; c.beginPath(); c.arc(mx, my, r * 0.86, 0, Math.PI * 2); c.fill()
            }
        }
    }
    Item {  // the pointer
        x: root.cx; y: root.cy
        rotation: root.angle
        Rectangle {
            visible: root.style !== "chrome"
            x: -1.5; y: root.style === "fet" ? -root.dial * 0.5 : -root.dial * 0.46
            width: 3; height: root.style === "fet" ? root.dial * 0.22 : root.dial * 0.4
            radius: 1.5
            color: root.style === "silver" ? "#101010" : "#ffffff"
        }
        Rectangle {  // the little ring of the FET knobs
            visible: root.style === "fet"
            x: -4; y: -root.dial * 0.4; width: 8; height: 8; radius: 4
            color: "transparent"; border.color: "#111"; border.width: 2
        }
    }
    MouseArea {
        x: root.cx - root.dial / 2; y: root.cy - root.dial / 2
        width: root.dial; height: root.dial
        property real startY: 0
        property real startT: 0
        onPressed: (m) => {
            if (m.modifiers & Qt.AltModifier) { root.cancel(); if (root.value !== root.defaultValue) root.released(root.defaultValue); return }
            root.dragging = true; root.changed = false; startY = m.y; startT = root.toUnit(root.value); root.dragValue = root.value
        }
        onPositionChanged: (m) => {
            if (!pressed || !root.dragging) return
            root.dragValue = root.fromUnit(startT + (startY - m.y) / ((m.modifiers & Qt.ShiftModifier) ? 900 : 220))
            root.changed = true
            root.moved(root.dragValue)
        }
        onReleased: { const send = root.dragging && root.changed; const v = root.dragValue; root.cancel(); if (send) root.released(v) }
        onCanceled: root.cancel()
        onDoubleClicked: { root.cancel(); root.released(root.defaultValue) }
        onWheel: (w) => root.released(root.fromUnit(root.toUnit(root.value) + (w.angleDelta.y > 0 ? 1 : -1) * ((w.modifiers & Qt.ShiftModifier) ? 0.004 : 0.02)))
    }
    Text {  // the value while it is dragged
        visible: root.dragging && root.changed
        x: root.cx - width / 2; y: root.cy - 11
        text: root.shown.toFixed(Math.abs(root.shown) >= 100 ? 0 : (Math.abs(root.shown) >= 10 ? 1 : 2))
        color: "#ffffff"
        font.family: Theme.fontFamily; font.pixelSize: 15; font.weight: 600
        style: Text.Outline; styleColor: "#000000"
    }
}
