import QtQuick
import QtQuick.Controls.Basic
import Jad

// Mastering Assistant of Logic Pro 11.2: on the left the Character box, the Auto EQ fader and the curve the analysis found (translucent magenta over the zero line);
// on the right the Dynamics panel (Loudness knob, Excite) and the figures of the analysis; at the bottom Analyze, Bypass and Loudness Compensation.
// Valve, Punch and Transparent are always offered (Logic greys them out on a Mac without Apple silicon: the models run here on the CPU).
FxShell {
    id: root
    title: qsTr("Mastering Assistant")
    bodyWidth: 1028
    bodyHeight: 520
    chassis: "#06101c"
    readonly property color accent: "#ff4de8"
    readonly property color panel: "#081a2b"
    readonly property color key: "#0b2236"
    readonly property color keyText: "#2f9bff"
    readonly property var stats: editor.masterStats
    readonly property var characters: [qsTr("Clean"), qsTr("Valve"), qsTr("Punch"), qsTr("Transparent")]
    readonly property var centres: [40, 100, 250, 630, 1600, 4000, 8000, 14000]
    function val(name) { return editor.valueOf(editor.param(name)) }
    function on(name) { return val(name) >= 0.5 }

    Rectangle { x: 0; y: 0; width: root.bodyWidth; height: root.bodyHeight; color: "#050f1a" }

    component Key: Rectangle {
        id: k
        property string label
        property bool lit: false
        property color litColor: "#0a8fe0"
        property color litText: "#04131f"
        property color textColor: root.keyText
        property bool active: true
        property int fontSize: 15
        signal clicked()
        radius: 5
        color: lit ? litColor : root.key
        opacity: active ? 1 : 0.5
        Text {
            anchors.centerIn: parent; width: parent.width - 8; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
            text: k.label; color: k.lit ? k.litText : k.textColor; font.family: Theme.fontFamily; font.pixelSize: k.fontSize
        }
        MouseArea { anchors.fill: parent; enabled: k.active; onClicked: k.clicked() }
    }

    // Character
    Rectangle {
        objectName: "masteringCharacter"
        x: 12; y: 9; width: 137; height: 57; color: "#150a24"; radius: 2
        Text { x: 15; y: 8; text: qsTr("Character"); color: "#8da2b8"; font.family: Theme.fontFamily; font.pixelSize: 15 }
        Text { x: 15; y: 25; text: root.characters[Math.round(root.val("character"))]; color: root.accent; font.family: Theme.fontFamily; font.pixelSize: 22; font.bold: true }
        Text { x: 15 + 8 + 0; y: 27; anchors.right: parent.right; anchors.rightMargin: 14; text: "⌃⌄"; color: root.accent; font.pixelSize: 15 }
        MouseArea { anchors.fill: parent; onClicked: characterMenu.popup(parent, 0, parent.height) }
        Menu {
            id: characterMenu
            Repeater {
                model: root.characters
                delegate: MenuItem {
                    required property string modelData
                    required property int index
                    text: modelData
                    checkable: true
                    checked: Math.round(root.val("character")) === index
                    onTriggered: root.editor.setNow("character", index)
                }
            }
        }
    }

    // the curve: the correction of the eight bands, scaled by Auto EQ
    Item {
        id: display
        x: 118; y: 107; width: 432; height: 294
        readonly property real zeroY: 147
        function xOf(f) { return width * Math.log(f / 20) / Math.log(1000) }
        function yOf(db) { return zeroY - db * 147 / 12 }
        Repeater {
            model: [20, 30, 40, 50, 60, 70, 80, 90, 100, 200, 300, 400, 500, 600, 700, 800, 900, 1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000, 10000]
            delegate: Rectangle { required property real modelData; x: display.xOf(modelData); y: 0; width: 1; height: display.height; color: "#0b1d2c" }
        }
        Rectangle { x: 0; y: display.zeroY; width: display.width; height: 1; color: "#12354d" }
        Repeater {
            model: [{ f: 20, t: "20" }, { f: 50, t: "50" }, { f: 100, t: "100" }, { f: 300, t: "300" }, { f: 700, t: "700" }, { f: 2000, t: "2k" }, { f: 5000, t: "5k" }, { f: 10000, t: "10k" }]
            delegate: Text { required property var modelData; x: display.xOf(modelData.f) - width / 2; y: display.zeroY + 11; text: modelData.t; color: "#3d556a"; font.family: Theme.fontFamily; font.pixelSize: 14 }
        }
        Repeater {
            model: [12, 6, 0, -6, -12]
            delegate: Text { required property real modelData; x: display.width + 8; y: display.yOf(modelData) - height / 2; text: modelData; color: "#3d556a"; font.family: Theme.fontFamily; font.pixelSize: 14 }
        }
        Canvas {
            id: curve
            anchors.fill: parent
            readonly property var vals: { root.editor.insert; root.editor.pending; const a = root.val("autoEq") / 100; const r = []; for (let i = 1; i <= 8; ++i) r.push(root.val("eq" + i) * a); return r }
            onValsChanged: requestPaint()
            onWidthChanged: requestPaint()
            Component.onCompleted: requestPaint()
            onPaint: {
                const c = getContext("2d")
                c.reset()
                const pts = [{ x: 0, y: 0 }]
                for (let i = 0; i < 8; ++i) pts.push({ x: display.xOf(root.centres[i]), y: Math.max(-14, Math.min(14, vals[i])) })
                pts.push({ x: width, y: 0 })
                const at = (px) => {  // a smooth interpolation (cosine) between the neighbouring points
                    for (let i = 0; i + 1 < pts.length; ++i) if (px >= pts[i].x && px <= pts[i + 1].x) {
                        const t = (px - pts[i].x) / Math.max(1, pts[i + 1].x - pts[i].x)
                        const s = (1 - Math.cos(t * Math.PI)) / 2
                        return pts[i].y + (pts[i + 1].y - pts[i].y) * s
                    }
                    return 0
                }
                c.beginPath(); c.moveTo(0, display.zeroY)
                for (let px = 0; px <= width; px += 2) c.lineTo(px, display.yOf(at(px)))
                c.lineTo(width, display.zeroY); c.closePath()
                c.fillStyle = "rgba(255,77,232,0.28)"; c.fill()
                c.beginPath()
                for (let px = 0; px <= width; px += 2) { const y = display.yOf(at(px)); if (px === 0) c.moveTo(px, y); else c.lineTo(px, y) }
                c.strokeStyle = "#ff4de8"; c.lineWidth = 2; c.stroke()
            }
        }
    }

    // Custom EQ: a dashed curve and three blue control points (low shelf, broad mid band, high shelf, up to 6 dB each way) over the analysis curve
    Item {
        id: custom
        objectName: "masteringCustomEq"
        visible: root.on("customEq")
        x: 118; y: 107; width: 432; height: 294
        readonly property var bands: [
            { f: "cLowFreq", g: "cLowGain", lo: 20, hi: 200, kind: 0 },
            { f: "cMidFreq", g: "cMidGain", lo: 200, hi: 8000, kind: 1 },
            { f: "cHighFreq", g: "cHighGain", lo: 8000, hi: 20000, kind: 2 }
        ]
        readonly property var state: { root.editor.insert; root.editor.pending; return custom.bands.map(b => ({ f: root.val(b.f), g: root.val(b.g) })) }
        // the shape of one band at a frequency, in dB (smooth stand-ins for the filters, for the drawing only)
        function bandDb(kind, fc, gain, f) {
            const r = (f / fc) * (f / fc)
            if (kind === 0) return gain / (1 + r)
            if (kind === 2) return gain * r / (1 + r)
            const x = Math.log(f / fc) / Math.log(2)
            return gain / (1 + (x / 0.9) * (x / 0.9))
        }
        Canvas {
            anchors.fill: parent
            readonly property var st: custom.state
            onStChanged: requestPaint()
            onWidthChanged: requestPaint()
            Component.onCompleted: requestPaint()
            onPaint: {
                const c = getContext("2d")
                c.reset()
                c.setLineDash([6, 4]); c.strokeStyle = "#2f9bff"; c.lineWidth = 2
                c.beginPath()
                for (let px = 0; px <= width; px += 2) {
                    const f = 20 * Math.pow(1000, px / width)
                    let db = 0
                    for (let i = 0; i < 3; ++i) db += custom.bandDb(custom.bands[i].kind, st[i].f, st[i].g, f)
                    if (px === 0) c.moveTo(px, display.yOf(db)); else c.lineTo(px, display.yOf(db))
                }
                c.stroke()
            }
        }
        Repeater {
            model: 3
            delegate: Rectangle {
                id: dot
                required property int index
                readonly property var band: custom.bands[index]
                property real fDrag: custom.state[index].f
                property real gDrag: custom.state[index].g
                property bool dragging: false
                readonly property real f: dragging ? fDrag : custom.state[index].f
                readonly property real g: dragging ? gDrag : custom.state[index].g
                objectName: "masteringCustomDot" + index
                width: 14; height: 14; radius: 7
                x: display.xOf(f) - 7; y: display.yOf(g) - 7
                color: "#2f9bff"; border.color: "#d6e2ee"; border.width: 1
                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -6
                    onPressed: dot.dragging = true
                    onPositionChanged: (m) => {
                        if (!pressed) return
                        const at = mapToItem(custom, m.x, m.y)
                        const px = Math.max(0, Math.min(display.width, at.x))
                        const py = Math.max(display.yOf(6), Math.min(display.yOf(-6), at.y))
                        const f = 20 * Math.pow(1000, px / display.width)
                        dot.fDrag = Math.max(dot.band.lo, Math.min(dot.band.hi, f))
                        dot.gDrag = Math.round((display.zeroY - py) * 12 / 147 * 10) / 10
                        root.editor.edit(dot.band.f, dot.fDrag)
                        root.editor.edit(dot.band.g, dot.gDrag)
                    }
                    onReleased: {
                        dot.dragging = false
                        root.editor.commit(dot.band.f, dot.fDrag)
                        root.editor.commit(dot.band.g, dot.gDrag)
                    }
                }
            }
        }
    }

    // Auto EQ fader (0 to 200 %, 100 % on the zero line)
    Item {
        id: fader
        objectName: "masteringAutoEq"
        x: 40; y: 100; width: 36; height: 320
        readonly property real barTop: 3
        readonly property real span: 304
        property real dragValue: root.val("autoEq")
        property bool dragging: false
        readonly property real shown: dragging ? dragValue : root.val("autoEq")
        function fromY(py) { return Math.max(0, Math.min(150, (1 - (py - barTop) / span) * 200)) }
        Rectangle { x: 16; y: fader.barTop; width: 4; height: fader.span; radius: 2; color: "#0d2234" }
        Rectangle { x: 16; y: fader.barTop + fader.span * (1 - Math.min(fader.shown, 100) / 200); width: 4; height: fader.span * Math.min(fader.shown, 100) / 200; color: "#5c1f6e" }
        Repeater { model: 5; delegate: Rectangle { required property int index; x: 24; y: fader.barTop + index * fader.span / 4; width: 8; height: 1; color: "#26405a" } }
        Rectangle {
            x: 6; y: fader.barTop + fader.span * (1 - fader.shown / 200) - 7; width: 24; height: 14; radius: 7; color: "#c63fcf"
            Rectangle { anchors.centerIn: parent; width: 8; height: 1.5; color: "#2b0a33" }
        }
        MouseArea {
            anchors.fill: parent
            onPressed: (m) => { fader.dragging = true; fader.dragValue = fader.fromY(m.y); root.editor.edit("autoEq", fader.dragValue) }
            onPositionChanged: (m) => { if (!pressed) return; fader.dragValue = fader.fromY(m.y); root.editor.edit("autoEq", fader.dragValue) }
            onReleased: { const v = fader.dragValue; fader.dragging = false; root.editor.commit("autoEq", v) }
            onDoubleClicked: { fader.dragging = false; root.editor.setNow("autoEq", 100) }
        }
    }
    Text { x: 56 - width / 2; y: 424; text: qsTr("Auto EQ"); color: "#8da2b8"; font.family: Theme.fontFamily; font.pixelSize: 16 }
    Text { x: 56 - width / 2; y: 442; text: Math.round(fader.shown) + " %"; color: root.accent; font.family: Theme.fontFamily; font.pixelSize: 20 }
    Key {
        objectName: "masteringCustomEqKey"
        x: 12; y: 475; width: 92; height: 31; label: qsTr("Custom EQ"); lit: root.on("customEq")
        onClicked: root.editor.setNow("customEq", root.on("customEq") ? 0 : 1)
    }

    // Dynamics
    Rectangle { x: 606; y: 9; width: 409; height: 279; color: root.panel }
    Text { x: 626; y: 24; text: qsTr("Dynamics"); color: "#9db2c8"; font.family: Theme.fontFamily; font.pixelSize: 23; font.bold: true }
    Text { x: 676 - width / 2; y: 54; text: qsTr("Loudness"); color: "#8da2b8"; font.family: Theme.fontFamily; font.pixelSize: 16 }
    Text { x: 676 - width / 2; y: 74; visible: Math.abs(root.val("loudness")) > 0.5; text: Math.round(root.val("loudness")) + " %"; color: root.accent; font.family: Theme.fontFamily; font.pixelSize: 16 }
    FxArcKnob {
        x: 616; y: 88; showHeader: false
        tint: root.accent
        dial: 58; bipolar: true; decimals: 0; minLabel: ""; maxLabel: ""
        readonly property var spec: root.editor.param("loudness")
        from: spec.min; to: spec.max; defaultValue: spec.def
        value: root.val("loudness")
        onMoved: (v) => root.editor.edit("loudness", v)
        onReleased: (v) => root.editor.commit("loudness", v)
    }
    Key {
        objectName: "masteringExcite"
        x: 654; y: 172; width: 44; height: 31
        label: qsTr("Excite"); textColor: root.accent; lit: root.on("excite"); litColor: "#6a1f72"; litText: "#ffd6fa"
        onClicked: root.editor.setNow("excite", root.on("excite") ? 0 : 1)
    }
    // what the analysis found
    Column {
        x: 758; y: 52; width: 240; spacing: 6
        Text { text: qsTr("Analysis"); color: "#8da2b8"; font.family: Theme.fontFamily; font.pixelSize: 16 }
        Text {
            width: parent.width; wrapMode: Text.WordWrap
            visible: root.stats.integratedLufs !== undefined
            text: root.stats.integratedLufs !== undefined ? qsTr("%1 LUFS-I\n%2 LU range\n%3 dBTP").arg(root.stats.integratedLufs.toFixed(1)).arg(root.stats.range.toFixed(1)).arg(root.stats.truePeakDb.toFixed(1)) : ""
            color: root.accent; font.family: Theme.fontFamily; font.pixelSize: 19
        }
        Text {
            width: parent.width; wrapMode: Text.WordWrap
            visible: root.stats.integratedLufs === undefined
            text: qsTr("Not analysed yet. Play the section and press Analyze.")
            color: "#3d556a"; font.family: Theme.fontFamily; font.pixelSize: 16
        }
        Text {
            width: parent.width; wrapMode: Text.WordWrap
            visible: root.stats.outputLufs !== undefined
            text: root.stats.outputLufs !== undefined ? qsTr("Output: %1 LUFS-I, %2 dBTP").arg(root.stats.outputLufs.toFixed(1)).arg(root.stats.outputTruePeakDb.toFixed(1)) : ""
            color: "#8da2b8"; font.family: Theme.fontFamily; font.pixelSize: 16
        }
    }
    Key {
        objectName: "masteringMeasure"
        x: 758; y: 232; width: 130; height: 36
        label: qsTr("Measure Output"); active: !root.editor.project.masteringBusy
        onClicked: root.editor.project.masteringMeasure(root.editor.trackId, root.editor.index)
    }

    // the notes
    Rectangle { x: 606; y: 294; width: 409; height: 173; color: root.panel }
    Text {
        x: 626; y: 310; width: 240; wrapMode: Text.WordWrap
        text: root.stats.gainDb !== undefined ? qsTr("Brings the mix to -14 LUFS (%1 dB), limited at -1 dBTP.").arg(root.stats.gainDb.toFixed(1)) : qsTr("The Loudness knob centre is -14 LUFS; the output never goes above -1 dB.")
        color: "#8da2b8"; font.family: Theme.fontFamily; font.pixelSize: 16
    }
    Text {
        x: 626; y: 372; width: 240; wrapMode: Text.WordWrap
        visible: root.stats.outputLufs !== undefined && root.stats.outputLufs < -16 && root.stats.truePeakDb - root.stats.integratedLufs > 16
        text: root.stats.outputLufs !== undefined ? qsTr("The mix peaks %1 dB above its loudness: -14 LUFS would need heavy limiting, so it stays at %2 LUFS.").arg((root.stats.truePeakDb - root.stats.integratedLufs).toFixed(0)).arg(root.stats.outputLufs.toFixed(1)) : ""
        color: "#ff6a5a"; font.family: Theme.fontFamily; font.pixelSize: 15
    }

    // Spread: the width of the mix (all the way left is mono, to check the mono compatibility); the correlation meter below shows the phase relation
    Text { x: 940 - width / 2; y: 304; text: qsTr("Spread"); color: "#8da2b8"; font.family: Theme.fontFamily; font.pixelSize: 16 }
    FxArcKnob {
        objectName: "masteringWidth"
        x: 906; y: 322; showHeader: false
        tint: root.accent
        dial: 58; decimals: 0; minLabel: ""; maxLabel: ""
        readonly property var spec: root.editor.param("width")
        from: spec.min; to: spec.max; defaultValue: spec.def
        value: root.val("width")
        onMoved: (v) => root.editor.edit("width", v)
        onReleased: (v) => root.editor.commit("width", v)
    }
    Text { x: 940 - width / 2; y: 394; text: Math.round(root.val("width")) + " %"; color: root.accent; font.family: Theme.fontFamily; font.pixelSize: 16 }

    // the live meters: what the insert puts out right now (momentary and short-term loudness, integrated since the window opened, true peak, stereo correlation)
    property var live: ({})
    Component.onCompleted: root.editor.project.setEditorAnalyzer(true, true)
    Timer {
        interval: 100; repeat: true; running: root.visible
        onTriggered: root.live = root.editor.project.editorLoudness()
    }
    Row {
        objectName: "masteringLive"
        x: 618; y: 420; spacing: 6
        Repeater {
            model: [
                { label: "M", value: root.live.momentary, unit: "LUFS" },
                { label: "S", value: root.live.shortTerm, unit: "LUFS" },
                { label: "I", value: root.live.integrated, unit: "LUFS" },
                { label: "TP", value: root.live.truePeakDb, unit: "dBTP" }
            ]
            delegate: Rectangle {
                required property var modelData
                width: 72; height: 40; radius: 3; color: "#0b2236"
                Text { x: 6; y: 3; text: parent.modelData.label; color: "#6f89a3"; font.family: Theme.fontFamily; font.pixelSize: 12 }
                Text { x: 6; y: 3; width: parent.width - 12; horizontalAlignment: Text.AlignRight; text: parent.modelData.unit; color: "#4d667f"; font.family: Theme.fontFamily; font.pixelSize: 11 }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter; y: 17
                    text: parent.modelData.value === undefined || parent.modelData.value <= -199 ? "-∞" : parent.modelData.value.toFixed(1)
                    color: parent.modelData.label === "TP" && parent.modelData.value > -1 ? "#ff6a5a" : root.accent
                    font.family: Theme.fontFamily; font.pixelSize: 17
                }
            }
        }
        Rectangle {  // the correlation: -1 at the left, +1 at the right, the bar grows from the middle
            objectName: "masteringCorrelation"
            width: 72; height: 40; radius: 3; color: "#0b2236"
            Text { x: 6; y: 3; text: qsTr("Corr"); color: "#6f89a3"; font.family: Theme.fontFamily; font.pixelSize: 12 }
            Rectangle { x: 6; y: 24; width: parent.width - 12; height: 8; color: "#050f1a" }
            Rectangle {
                readonly property real c: root.live.correlation ?? 0
                x: 6 + (parent.width - 12) / 2 + Math.min(0, c) * (parent.width - 12) / 2; y: 24; height: 8
                width: Math.abs(c) * (parent.width - 12) / 2
                color: c < 0 ? "#ff6a5a" : "#4fe05a"
            }
            Rectangle { x: parent.width / 2 - 0.5; y: 22; width: 1; height: 12; color: "#6f89a3" }
        }
    }

    // bottom row
    Key {
        objectName: "masteringReanalyze"
        x: 606; y: 475; width: 138; height: 31
        label: root.editor.project.masteringBusy ? qsTr("Analysing…") : (root.stats.integratedLufs !== undefined || root.editor.project.loopEndBeats > root.editor.project.loopStartBeats ? qsTr("Reanalyze Section") : qsTr("Analyze"))
        active: !root.editor.project.masteringBusy
        onClicked: root.editor.project.masteringAnalyse(root.editor.trackId, root.editor.index)
    }
    Key {
        x: 812; y: 475; width: 100; height: 31
        label: qsTr("Bypass"); lit: root.on("bypass"); litColor: "#0a8fe0"
        onClicked: root.editor.setNow("bypass", root.on("bypass") ? 0 : 1)
    }
    Key {
        x: 916; y: 475; width: 99; height: 31
        fontSize: 13; label: qsTr("Loudness Compensation"); textColor: "#ff8a1a"; lit: root.on("compensate"); litColor: "#3a1f0a"; litText: "#ff8a1a"
        onClicked: root.editor.setNow("compensate", root.on("compensate") ? 0 : 1)
    }
}
