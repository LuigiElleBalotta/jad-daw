import QtQuick
import QtQuick.Layouts
import Jad

// Channel EQ of Logic Pro 11.2: the response curve with eight band handles (low cut, low shelf, four peaks, high shelf, high cut) over a log frequency scale,
// and under it a column of fields for each band. Drag a handle for frequency and gain, scroll on it for Q, double-click it to switch the band on or off.
Item {
    id: root
    required property var editor
    readonly property var bands: [
        { key: "lc", name: qsTr("Low Cut"), cut: true, color: "#e5645f" },
        { key: "ls", name: qsTr("Low Shelf"), cut: false, color: "#f0963c" },
        { key: "p1", name: qsTr("Peak 1"), cut: false, color: "#e8c93a" },
        { key: "p2", name: qsTr("Peak 2"), cut: false, color: "#62c460" },
        { key: "p3", name: qsTr("Peak 3"), cut: false, color: "#3bb7d8" },
        { key: "p4", name: qsTr("Peak 4"), cut: false, color: "#5b7be0" },
        { key: "hs", name: qsTr("High Shelf"), cut: false, color: "#a56cd8" },
        { key: "hc", name: qsTr("High Cut"), cut: true, color: "#d86cb0" }
    ]
    readonly property real dbRange: 24
    property int selected: -1
    readonly property real columnWidth: (width - 7 * 6) / 8
    implicitHeight: 200 + 10 + 168 + 8 + 40

    function val(name) { return editor.valueOf(editor.param(name)) }
    function xOfFreq(f, w) { return Math.log(Math.max(20, f) / 20) / Math.log(1000) * w }
    function freqOfX(x, w) { return 20 * Math.pow(1000, Math.max(0, Math.min(1, x / w))) }
    function yOfDb(d, h) { return h / 2 - Math.max(-dbRange, Math.min(dbRange, d)) / dbRange * (h / 2) }
    function dbOfY(y, h) { return Math.max(-dbRange, Math.min(dbRange, (h / 2 - y) / (h / 2) * dbRange)) }

    FxDisplay {
        id: screen
        x: 0; y: 0
        width: parent.width
        height: 200
        Canvas {
            id: canvas
            anchors.fill: parent
            anchors.margins: 6
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                ctx.font = "9px sans-serif"
                ctx.lineWidth = 1
                for (const f of [30, 40, 50, 60, 70, 80, 90, 100, 200, 300, 400, 500, 600, 700, 800, 900, 1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000, 10000]) {
                    const x = Math.round(root.xOfFreq(f, width)) + 0.5
                    ctx.strokeStyle = (f === 100 || f === 1000 || f === 10000) ? "#34343a" : "#1f1f23"
                    ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, height); ctx.stroke()
                }
                ctx.fillStyle = "#77777f"
                for (const [f, t] of [[100, "100"], [1000, "1k"], [10000, "10k"]]) ctx.fillText(t, root.xOfFreq(f, width) + 3, height - 3)
                for (const d of [-18, -12, -6, 6, 12, 18]) {
                    const y = Math.round(root.yOfDb(d, height)) + 0.5
                    ctx.strokeStyle = "#1f1f23"
                    ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(width, y); ctx.stroke()
                    ctx.fillStyle = "#5f5f66"
                    ctx.fillText((d > 0 ? "+" : "") + d, 3, y - 2)
                }
                ctx.strokeStyle = "#4a4a52"
                const y0 = Math.round(height / 2) + 0.5
                ctx.beginPath(); ctx.moveTo(0, y0); ctx.lineTo(width, y0); ctx.stroke()
                const c = root.editor.curve
                if (c.length < 2) return
                ctx.beginPath()
                ctx.moveTo(0, root.yOfDb(c[0], height))
                for (let i = 1; i < c.length; ++i) ctx.lineTo(i / (c.length - 1) * width, root.yOfDb(c[i], height))
                ctx.lineTo(width, height / 2); ctx.lineTo(0, height / 2); ctx.closePath()
                ctx.fillStyle = "rgba(140, 200, 140, 0.20)"
                ctx.fill()
                ctx.strokeStyle = "#cfe9cf"
                ctx.lineWidth = 2
                ctx.beginPath()
                ctx.moveTo(0, root.yOfDb(c[0], height))
                for (let i = 1; i < c.length; ++i) ctx.lineTo(i / (c.length - 1) * width, root.yOfDb(c[i], height))
                ctx.stroke()
            }
            Connections { target: root.editor; function onCurveChanged() { canvas.requestPaint() } }
            Component.onCompleted: requestPaint()
            onWidthChanged: requestPaint()
        }
        Item {  // the handles
            anchors.fill: canvas
            Repeater {
                model: root.bands
                delegate: Rectangle {
                    id: handle
                    required property var modelData
                    required property int index
                    readonly property bool bandOn: root.val(modelData.key + "On") >= 0.5
                    readonly property real freq: root.val(modelData.key + "Freq")
                    readonly property real gainDb: modelData.cut ? 0 : root.val(modelData.key + "Gain")
                    property real dragFreq: -1
                    property real dragGain: -1000
                    x: root.xOfFreq(dragFreq > 0 ? dragFreq : freq, parent.width) - width / 2
                    y: root.yOfDb(dragGain > -1000 ? dragGain : gainDb, parent.height) - height / 2
                    width: root.selected === index ? 16 : 13
                    height: width
                    radius: width / 2
                    color: bandOn ? modelData.color : "#26262a"
                    border.color: bandOn ? "#ffffff" : modelData.color
                    border.width: root.selected === index ? 2 : 1
                    objectName: "eqHandle_" + modelData.key
                    Text { anchors.centerIn: parent; text: handle.index + 1; font.pixelSize: 8; font.weight: 700; color: handle.bandOn ? "#101012" : handle.modelData.color }
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -5
                        onPressed: root.selected = handle.index
                        onPositionChanged: (m) => {
                            if (!pressed) return
                            const p = mapToItem(handle.parent, m.x, m.y)
                            const f = Math.max(20, Math.min(20000, root.freqOfX(p.x, handle.parent.width)))
                            handle.dragFreq = f
                            root.editor.edit(handle.modelData.key + "Freq", f)
                            if (!handle.modelData.cut) {
                                const g = root.dbOfY(p.y, handle.parent.height)
                                handle.dragGain = g
                                root.editor.edit(handle.modelData.key + "Gain", g)
                            }
                            if (!handle.bandOn) root.editor.edit(handle.modelData.key + "On", 1)
                        }
                        onReleased: {
                            const changes = {}
                            if (handle.dragFreq > 0) changes[handle.modelData.key + "Freq"] = handle.dragFreq
                            if (handle.dragGain > -1000) changes[handle.modelData.key + "Gain"] = handle.dragGain
                            if (!handle.bandOn && Object.keys(changes).length > 0) changes[handle.modelData.key + "On"] = 1
                            handle.dragFreq = -1
                            handle.dragGain = -1000
                            if (Object.keys(changes).length > 0) root.editor.commitMany(changes)
                        }
                        onDoubleClicked: root.editor.setNow(handle.modelData.key + "On", handle.bandOn ? 0 : 1)
                        onWheel: (w) => {
                            const q = root.editor.param(handle.modelData.key + "Q")
                            const now = root.val(handle.modelData.key + "Q")
                            root.editor.setNow(handle.modelData.key + "Q", Math.max(q.min, Math.min(q.max, now * Math.pow(1.12, w.angleDelta.y > 0 ? 1 : -1))))
                        }
                    }
                }
            }
        }
    }

    // the fields of each band
    Row {
        x: 0
        y: screen.height + 10
        spacing: 6
        Repeater {
            model: root.bands
            delegate: Rectangle {
                id: col
                required property var modelData
                required property int index
                readonly property bool bandOn: root.val(modelData.key + "On") >= 0.5
                readonly property string slopeOrGain: modelData.cut ? "Slope" : "Gain"
                width: root.columnWidth
                height: 168
                radius: 5
                color: root.selected === index ? "#2a2a2f" : "#222226"
                border.color: root.selected === index ? modelData.color : "#303035"
                MouseArea { anchors.fill: parent; onPressed: (m) => { root.selected = col.index; m.accepted = false } }
                Column {
                    anchors.fill: parent
                    anchors.margins: 6
                    spacing: 4
                    Row {
                        spacing: 4
                        Rectangle { y: 4; width: 9; height: 9; radius: 5; color: col.bandOn ? col.modelData.color : "#44444a" }
                        Text { text: col.modelData.name; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeCaptionSize; font.weight: 600 }
                    }
                    FxSwitch { label: qsTr("On"); on: col.bandOn; tint: col.modelData.color; width: parent.width; onToggled: (v) => root.editor.setNow(col.modelData.key + "On", v ? 1 : 0) }
                    FxValue {
                        width: parent.width
                        label: qsTr("Frequency")
                        readonly property var p: root.editor.param(col.modelData.key + "Freq")
                        from: p.min; to: p.max; logarithmic: true; defaultValue: p.def
                        value: root.val(col.modelData.key + "Freq")
                        format: (v) => v >= 1000 ? (v / 1000).toFixed(v >= 10000 ? 1 : 2) + " kHz" : v.toFixed(v >= 100 ? 0 : 1) + " Hz"
                        enabledLook: col.bandOn
                        onMoved: (v) => root.editor.edit(col.modelData.key + "Freq", v)
                        onReleased: (v) => root.editor.commit(col.modelData.key + "Freq", v)
                    }
                    FxValue {
                        width: parent.width
                        label: col.modelData.cut ? qsTr("Slope") : qsTr("Gain")
                        readonly property var p: root.editor.param(col.modelData.key + col.slopeOrGain)
                        from: p.min; to: p.max; defaultValue: p.def
                        value: root.val(col.modelData.key + col.slopeOrGain)
                        format: (v) => col.modelData.cut ? Math.round(v) * 6 + " dB/oct" : (v > 0 ? "+" : "") + v.toFixed(1) + " dB"
                        enabledLook: col.bandOn
                        onMoved: (v) => root.editor.edit(col.modelData.key + col.slopeOrGain, col.modelData.cut ? Math.round(v) : v)
                        onReleased: (v) => root.editor.commit(col.modelData.key + col.slopeOrGain, col.modelData.cut ? Math.round(v) : v)
                    }
                    FxValue {
                        width: parent.width
                        label: qsTr("Q")
                        readonly property var p: root.editor.param(col.modelData.key + "Q")
                        from: p.min; to: p.max; logarithmic: true; defaultValue: p.def
                        value: root.val(col.modelData.key + "Q")
                        format: (v) => v.toFixed(2)
                        enabledLook: col.bandOn
                        onMoved: (v) => root.editor.edit(col.modelData.key + "Q", v)
                        onReleased: (v) => root.editor.commit(col.modelData.key + "Q", v)
                    }
                }
            }
        }
    }
    FxValue {  // the gain of the whole EQ
        x: parent.width - width
        y: screen.height + 10 + 168 + 8
        width: 90
        label: qsTr("Master Gain")
        readonly property var p: root.editor.param("masterGain")
        from: p.min; to: p.max; defaultValue: p.def
        value: root.val("masterGain")
        format: (v) => (v > 0 ? "+" : "") + v.toFixed(1) + " dB"
        onMoved: (v) => root.editor.edit("masterGain", v)
        onReleased: (v) => root.editor.commit("masterGain", v)
    }
}
