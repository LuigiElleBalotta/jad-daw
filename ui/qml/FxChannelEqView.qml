import QtQuick
import Jad

// Channel EQ of Logic Pro 11.2 as its window looks: the eight filter-type icons above the graph, the graph (log frequency 20 Hz to 20 kHz, the EQ curve in a +15 to
// -15 dB scale on the right, a dB scale for the analyzer on the left), a column of Frequency, Gain (or Slope) and Q under it for each band, the Analyzer, Q-Couple and
// HQ switches, the Processing menu and, at the far right, the gain of the whole EQ. Drag a band's node for frequency and gain, scroll on it for Q, double-click a
// column's number to type it. A band is lit in its own colour while it is on; the one under the pointer or being dragged gets a tinted column.
FxShell {
    id: root
    title: qsTr("Channel EQ")
    bodyWidth: 1000
    bodyHeight: 400
    chassis: "#0f1a27"

    readonly property var bands: [
        { key: "lc", shape: "lowcut", cut: true, color: "#c9ccd2" },
        { key: "ls", shape: "lowshelf", cut: false, color: "#f29a38" },
        { key: "p1", shape: "peak", cut: false, color: "#eadc3a" },
        { key: "p2", shape: "peak", cut: false, color: "#4fd864" },
        { key: "p3", shape: "peak", cut: false, color: "#25d0a0" },
        { key: "p4", shape: "peak", cut: false, color: "#58b8f0" },
        { key: "hs", shape: "highshelf", cut: false, color: "#a572e8" },
        { key: "hc", shape: "highcut", cut: true, color: "#c9ccd2" }
    ]
    property int hot: -1                         // the band under the pointer or being dragged
    readonly property real plotX: 37
    readonly property real plotW: 876
    readonly property real zeroY: 162                              // the 0 dB line and the scale of the EQ curve
    readonly property real dbPerPt: 30 / 237
    readonly property real colW: 115                               // the columns of the bands
    readonly property real colX0: 14

    function val(name) { return editor.valueOf(editor.param(name)) }
    function xOfFreq(f) { return plotX + Math.log(Math.max(20, f) / 20) / Math.log(1000) * plotW }
    function freqOfX(x) { return 20 * Math.pow(1000, Math.max(0, Math.min(1, (x - plotX) / plotW))) }
    function yOfDb(d) { return zeroY - Math.max(-15, Math.min(15, d)) / dbPerPt }
    function dbOfY(y) { return Math.max(-15, Math.min(15, (zeroY - y) * dbPerPt)) }
    function dim(c, a) { const q = Qt.color(c); return Qt.rgba(q.r, q.g, q.b, a) }

    // the plot
    Rectangle {
        x: 0; y: 0; width: root.bodyWidth; height: 400
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#0c1622" }
            GradientStop { position: 0.45; color: "#162a40" }
            GradientStop { position: 1.0; color: "#08121c" }
        }
    }
    Canvas {
        id: canvas
        x: 0; y: 0; width: root.bodyWidth; height: 300
        function repaint() { requestPaint() }
        onPaint: {
            const c = getContext("2d")
            c.reset()
            c.lineWidth = 1
            // the grid: a line for each of the marked frequencies and every 5 dB
            const marks = [20, 30, 40, 50, 60, 80, 100, 200, 300, 400, 500, 800, 1000, 2000, 3000, 4000, 6000, 8000, 10000, 20000]
            for (const f of [20, 30, 40, 50, 60, 70, 80, 90, 100, 200, 300, 400, 500, 600, 700, 800, 900, 1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000, 10000, 20000]) {
                const x = Math.round(root.xOfFreq(f)) + 0.5
                c.strokeStyle = (f === 100 || f === 1000 || f === 10000) ? "#23384f" : "#1a2d43"
                c.beginPath(); c.moveTo(x, 44); c.lineTo(x, 281); c.stroke()
            }
            for (let d = -15; d <= 15; d += 5) {
                const y = Math.round(root.yOfDb(d)) + 0.5
                c.strokeStyle = d === 0 ? "#3a526b" : "#1a2d43"
                c.beginPath(); c.moveTo(root.plotX, y); c.lineTo(root.plotX + root.plotW, y); c.stroke()
            }
            c.font = "13px sans-serif"
            c.fillStyle = "#c4cdd6"
            const labels = [[20, "20"], [30, "30"], [40, "40"], [50, "50"], [60, "60"], [80, "80"], [100, "100"], [200, "200"], [300, "300"], [400, "400"], [500, "500"], [800, "800"], [1000, "1k"], [2000, "2k"], [3000, "3k"], [4000, "4k"], [6000, "6k"], [8000, "8k"], [10000, "10k"], [20000, "20k"]]
            for (const [f, t] of labels) c.fillText(t, root.xOfFreq(f) - (f === 20 ? 0 : 0), root.zeroY + 20)
            // the curve and the area between it and the 0 dB line, in the colour of the band that is being worked on
            const curve = root.editor.curve
            if (curve.length < 2) return
            const hot = root.hot >= 0 ? root.bands[root.hot].color : "#c9d8e8"
            c.beginPath()
            c.moveTo(root.plotX, root.yOfDb(curve[0]))
            for (let i = 1; i < curve.length; ++i) c.lineTo(root.plotX + i / (curve.length - 1) * root.plotW, root.yOfDb(curve[i]))
            c.lineTo(root.plotX + root.plotW, root.zeroY); c.lineTo(root.plotX, root.zeroY); c.closePath()
            c.fillStyle = root.dim(Qt.color(hot), 0.38)
            c.fill()
            c.strokeStyle = "#dfe8f0"
            c.lineWidth = 1.5
            c.beginPath()
            c.moveTo(root.plotX, root.yOfDb(curve[0]))
            for (let i = 1; i < curve.length; ++i) c.lineTo(root.plotX + i / (curve.length - 1) * root.plotW, root.yOfDb(curve[i]))
            c.stroke()
        }
        Connections { target: root.editor; function onCurveChanged() { canvas.requestPaint() } }
        Connections { target: root; function onHotChanged() { canvas.requestPaint() } }
        Component.onCompleted: requestPaint()
    }
    // the two scales
    Repeater {
        model: 13
        delegate: Text {
            required property int index
            x: 4; y: 44 + index * 19.7 - 8
            width: 32; horizontalAlignment: Text.AlignRight
            text: String(index * 5)
            color: "#7e8b99"; font.family: Theme.fontFamily; font.pixelSize: 13
        }
    }
    Repeater {
        model: 7
        delegate: Text {
            required property int index
            x: 920; y: root.yOfDb(15 - index * 5) - 8
            width: 28; horizontalAlignment: Text.AlignRight
            text: String(Math.abs(15 - index * 5))
            color: "#9fb0c2"; font.family: Theme.fontFamily; font.pixelSize: 13
        }
    }

    // the band icons
    Repeater {
        model: root.bands
        delegate: Item {
            id: icon
            required property var modelData
            required property int index
            readonly property bool bandOn: root.val(modelData.key + "On") >= 0.5
            x: root.colX0 + index * root.colW + root.colW / 2 - 28
            y: 6
            width: 56; height: 30
            Rectangle { anchors.fill: parent; radius: 3; color: root.dim(icon.modelData.color, 0.28); visible: root.hot === icon.index }
            Canvas {
                anchors.centerIn: parent
                width: 36; height: 22
                readonly property bool on: icon.bandOn
                readonly property bool hotNow: root.hot === icon.index
                onOnChanged: requestPaint()
                onHotNowChanged: requestPaint()
                Component.onCompleted: requestPaint()
                onPaint: {
                    const c = getContext("2d")
                    c.reset()
                    c.lineWidth = 1.6
                    c.strokeStyle = on || hotNow ? icon.modelData.color : "#5f7388"
                    c.lineJoin = "round"
                    const s = icon.modelData.shape
                    c.beginPath()
                    if (s === "peak") { c.moveTo(0, 11); c.lineTo(9, 11); c.lineTo(18, 3); c.lineTo(27, 11); c.lineTo(36, 11); c.moveTo(9, 11); c.lineTo(18, 19); c.lineTo(27, 11) }
                    else if (s === "lowshelf") { c.moveTo(0, 5); c.lineTo(8, 5); c.bezierCurveTo(14, 5, 12, 17, 20, 17); c.lineTo(28, 17); c.moveTo(0, 17); c.lineTo(8, 17) }
                    else if (s === "highshelf") { c.moveTo(36, 5); c.lineTo(28, 5); c.bezierCurveTo(22, 5, 24, 17, 16, 17); c.lineTo(8, 17); c.moveTo(36, 17); c.lineTo(28, 17) }
                    else if (s === "lowcut") { c.moveTo(0, 19); c.bezierCurveTo(10, 18, 14, 6, 26, 5) }
                    else { c.moveTo(36, 19); c.bezierCurveTo(26, 18, 22, 6, 10, 5) }
                    c.stroke()
                }
            }
            MouseArea { anchors.fill: parent; onClicked: root.editor.setNow(icon.modelData.key + "On", icon.bandOn ? 0 : 1) }
        }
    }

    // the nodes (a node shows while its band is on and it is hot or away from 0 dB)
    Item {
        x: 0; y: 0; width: root.bodyWidth; height: 300
        Repeater {
            model: root.bands
            delegate: Item {
                id: node
                required property var modelData
                required property int index
                readonly property bool bandOn: root.val(modelData.key + "On") >= 0.5
                readonly property real freq: root.val(modelData.key + "Freq")
                readonly property real gainDb: modelData.cut ? 0 : root.val(modelData.key + "Gain")
                property real dragFreq: -1
                property real dragGain: -1000
                x: root.xOfFreq(dragFreq > 0 ? dragFreq : freq) - 14
                y: root.yOfDb(dragGain > -1000 ? dragGain : gainDb) - 14
                width: 28; height: 28
                visible: bandOn
                objectName: "eqHandle_" + modelData.key
                Rectangle {
                    anchors.centerIn: parent
                    width: root.hot === node.index ? 14 : 10; height: width; radius: width / 2
                    color: root.hot === node.index ? node.modelData.color : root.dim(node.modelData.color, 0.55)
                }
                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    onEntered: if (!pressed) root.hot = node.index
                    onExited: if (!pressed) root.hot = -1
                    onPositionChanged: (m) => {
                        if (!pressed) return
                        const p = mapToItem(node.parent, m.x, m.y)
                        const f = Math.max(20, Math.min(20000, root.freqOfX(p.x)))
                        node.dragFreq = f
                        root.editor.edit(node.modelData.key + "Freq", f)
                        if (!node.modelData.cut) {
                            const g = root.dbOfY(p.y)
                            node.dragGain = g
                            root.editor.edit(node.modelData.key + "Gain", g)
                        }
                    }
                    onPressed: root.hot = node.index
                    onReleased: {
                        const changes = {}
                        if (node.dragFreq > 0) changes[node.modelData.key + "Freq"] = node.dragFreq
                        if (node.dragGain > -1000) changes[node.modelData.key + "Gain"] = node.dragGain
                        node.dragFreq = -1
                        node.dragGain = -1000
                        if (Object.keys(changes).length > 0) root.editor.commitMany(changes)
                        if (!containsMouse) root.hot = -1
                    }
                    onDoubleClicked: root.editor.setNow(node.modelData.key + "On", 0)
                    onWheel: (w) => {
                        const q = root.editor.param(node.modelData.key + "Q")
                        const now = root.val(node.modelData.key + "Q")
                        root.editor.setNow(node.modelData.key + "Q", Math.max(q.min, Math.min(q.max, now * Math.pow(1.1, w.angleDelta.y > 0 ? 1 : -1))))
                    }
                }
            }
        }
    }

    // the three numbers of each band
    Repeater {
        model: root.bands
        delegate: Rectangle {
            id: col
            required property var modelData
            required property int index
            readonly property bool bandOn: root.val(modelData.key + "On") >= 0.5
            readonly property string second: modelData.cut ? "Slope" : "Gain"
            x: root.colX0 + index * root.colW
            y: 300
            width: root.colW; height: 54
            color: root.hot === index ? root.dim(modelData.color, 0.4) : "transparent"
            MouseArea { anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton; onEntered: root.hot = col.index; onExited: if (root.hot === col.index) root.hot = -1 }
            Column {
                anchors.centerIn: parent
                spacing: 0
                FxField {
                    width: col.width
                    property var p: root.editor.param(col.modelData.key + "Freq")
                    from: p.min; to: p.max; logarithmic: true; defaultValue: p.def
                    value: root.val(col.modelData.key + "Freq")
                    unit: "Hz"; decimals: value >= 1000 ? 0 : (value >= 100 ? 0 : 1)
                    textColor: col.bandOn ? col.modelData.color : "#566a80"
                    onMoved: (v) => root.editor.edit(col.modelData.key + "Freq", v)
                    onReleased: (v) => root.editor.commit(col.modelData.key + "Freq", v)
                }
                FxField {
                    width: col.width
                    property var p: root.editor.param(col.modelData.key + col.second)
                    from: p.min; to: p.max; defaultValue: p.def
                    value: root.val(col.modelData.key + col.second)
                    unit: col.modelData.cut ? "dB/Oct" : "dB"
                    decimals: col.modelData.cut ? 0 : 1
                    scaleBy: col.modelData.cut ? 6 : 1
                    plusSign: !col.modelData.cut
                    snap: col.modelData.cut
                    textColor: col.bandOn ? col.modelData.color : "#566a80"
                    onMoved: (v) => root.editor.edit(col.modelData.key + col.second, v)
                    onReleased: (v) => root.editor.commit(col.modelData.key + col.second, v)
                }
                FxField {
                    width: col.width
                    property var p: root.editor.param(col.modelData.key + "Q")
                    from: p.min; to: p.max; logarithmic: true; defaultValue: p.def
                    value: root.val(col.modelData.key + "Q")
                    decimals: 2
                    textColor: col.bandOn ? col.modelData.color : "#566a80"
                    onMoved: (v) => root.editor.edit(col.modelData.key + "Q", v)
                    onReleased: (v) => root.editor.commit(col.modelData.key + "Q", v)
                }
            }
        }
    }

    // the gain of the whole EQ: a vertical slider at the right
    Item {
        x: 950; y: 40; width: 48; height: 330
        Rectangle { x: 16; y: 4; width: 1; height: 240; color: "#2e4258" }
        Rectangle {
            id: gainThumb
            readonly property real gain: root.val("masterGain")
            property real dragGain: gain
            x: 6; width: 22; height: 22; radius: 11
            y: 4 + (1 - (Math.max(-15, Math.min(15, dragGain)) + 15) / 30) * 240 - 11
            color: "#22405f"; border.color: "#8fb4dd"; border.width: 2
            MouseArea {
                anchors.fill: parent; anchors.margins: -6
                property bool moved: false
                onPositionChanged: (m) => {
                    if (!pressed) return
                    const y = mapToItem(parent.parent, m.x, m.y).y
                    const g = Math.round((1 - Math.max(0, Math.min(1, (y - 4) / 240))) * 300 - 150) / 10
                    gainThumb.dragGain = g; moved = true
                    root.editor.edit("masterGain", g)
                }
                onReleased: { if (moved) root.editor.commit("masterGain", gainThumb.dragGain); moved = false }
                onDoubleClicked: root.editor.commit("masterGain", 0)
            }
            Connections { target: root.editor; function onPendingChanged() { if (!gainMouse.pressed) gainThumb.dragGain = gainThumb.gain } }
            MouseArea { id: gainMouse; anchors.fill: parent; enabled: false }
        }
        Text { x: 0; y: 255; width: 48; horizontalAlignment: Text.AlignHCenter; text: qsTr("Gain"); color: "#9fb0c2"; font.family: Theme.fontFamily; font.pixelSize: 17 }
        FxField {
            x: 0; y: 283; width: 48
            property var p: root.editor.param("masterGain")
            from: p.min; to: p.max; defaultValue: p.def
            value: root.val("masterGain")
            unit: "dB"; decimals: 1; plusSign: false
            textColor: "#d5dee8"
            onMoved: (v) => root.editor.edit("masterGain", v)
            onReleased: (v) => root.editor.commit("masterGain", v)
        }
    }

    // the switches under the numbers
    Row {
        x: 36; y: 360
        spacing: 6
        FxPlainKey { label: qsTr("Analyzer"); sup: "POST"; on: true; enabledKey: false }
        FxPlainKey { label: qsTr("Q-Couple"); on: true; enabledKey: false }
        FxPlainKey { label: qsTr("HQ"); on: false; enabledKey: false }
        Item { width: 14; height: 1 }
        Text { anchors.verticalCenter: parent.verticalCenter; text: qsTr("Processing:"); color: "#a8b6c6"; font.family: Theme.fontFamily; font.pixelSize: 17 }
        Rectangle {
            width: 105; height: 28; radius: 3; color: "#142537"
            anchors.verticalCenter: parent.verticalCenter
            Text { x: 8; anchors.verticalCenter: parent.verticalCenter; text: qsTr("Stereo"); color: "#c6d2df"; font.family: Theme.fontFamily; font.pixelSize: 17 }
            Text { anchors.right: parent.right; anchors.rightMargin: 7; anchors.verticalCenter: parent.verticalCenter; text: "↕"; color: "#7f90a3"; font.pixelSize: 13 }
        }
    }
}
