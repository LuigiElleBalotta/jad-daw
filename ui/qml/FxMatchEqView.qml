import QtQuick
import QtQuick.Controls.Basic
import Jad

// Match EQ of Logic Pro 11.2: the graph with the correction curve (right scale +30 to -30 dB), the pipeline bar Current, Reference, EQ Curve, the Apply slider at the
// right and a row of controls under it. The reference is one of the stored curves (the Reference segment is its menu); Match reads the sound of the track, matches it
// to that curve and writes the correction into the 31 bands. The live spectra of Current and Reference are not drawn.
FxShell {
    id: root
    title: qsTr("Match EQ")
    bodyWidth: 1028
    bodyHeight: 400
    withSideChain: true
    chassis: "#06101c"
    property string curveName: ""
    readonly property bool busy: editor.project.masteringBusy
    readonly property var curves: editor.project.matchEqCurveNames()
    readonly property bool matched: { editor.insert; editor.pending; let any = false; for (let i = 0; i < 31; ++i) if (Math.abs(val("b" + i)) > 0.01) any = true; return any }
    function val(name) { return editor.valueOf(editor.param(name)) }
    function bandFreq(i) { return 1000 * Math.pow(10, (i - 17) / 10) }

    Rectangle {
        x: 0; y: 0; width: root.bodyWidth; height: root.bodyHeight
        gradient: Gradient { GradientStop { position: 0; color: "#0d2038" } GradientStop { position: 0.7; color: "#07131f" } GradientStop { position: 1; color: "#050f1a" } }
    }

    // the graph
    Item {
        id: graph
        x: 37; y: 39; width: 901; height: 240
        function xOf(f) { return width * Math.log(f / 20) / Math.log(1000) }
        Repeater {
            model: [20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000]
            delegate: Item {
                required property real modelData
                x: graph.xOf(modelData)
                Rectangle { width: 1; height: graph.height; color: "#12283d" }
                Text { x: -width / 2 + (modelData === 20 ? width / 2 : 0); y: graph.height * 0.52; text: modelData >= 1000 ? (modelData / 1000) + "k" : String(modelData); color: "#6c8196"; font.family: Theme.fontFamily; font.pixelSize: 14 }
            }
        }
        Repeater {
            model: 13
            delegate: Item {
                required property int index
                y: index * graph.height / 12
                Rectangle { width: graph.width; height: 1; color: "#0f2236" }
                Text { x: -30; y: -height / 2; width: 24; horizontalAlignment: Text.AlignRight; text: -index * 5; color: "#6c8196"; font.family: Theme.fontFamily; font.pixelSize: 13 }
                Text { x: graph.width + 10; y: -height / 2; text: 30 - index * 5; color: "#3b5268"; font.family: Theme.fontFamily; font.pixelSize: 13 }
            }
        }
        Canvas {
            id: curve
            anchors.fill: parent
            readonly property var vals: { root.editor.insert; root.editor.pending; const r = []; for (let i = 0; i < 31; ++i) r.push(root.val("b" + i)); return r }
            onValsChanged: requestPaint()
            Component.onCompleted: requestPaint()
            onPaint: {
                const c = getContext("2d")
                c.reset()
                const y = (db) => height / 2 - Math.max(-30, Math.min(30, db)) / 30 * height / 2
                c.beginPath()
                for (let i = 0; i < 31; ++i) { const px = graph.xOf(root.bandFreq(i)); if (i === 0) c.moveTo(px, y(vals[i])); else c.lineTo(px, y(vals[i])) }
                c.strokeStyle = "#f2e04a"; c.lineWidth = 1.6; c.stroke()
            }
        }
        Text {
            visible: !root.matched
            x: parent.width * 0.15; y: parent.height * 0.18; width: parent.width * 0.7; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
            text: root.curveName === "" ? qsTr("Pick a reference curve in the Reference segment, then press Match to match the sound of this track to it.") : qsTr("Press Match to match the sound of this track to %1.").arg(root.curveName)
            color: "#4a647c"; font.family: Theme.fontFamily; font.pixelSize: 19
        }
    }

    // Apply
    Item {
        id: apply
        objectName: "matchEqApply"
        x: 980; y: 39; width: 28; height: 240
        property bool dragging: false
        property real dragValue: root.val("amount")
        readonly property real shown: dragging ? dragValue : root.val("amount")
        function fromY(py) { return Math.max(0, Math.min(200, (1 - py / height) * 200)) }
        Rectangle { x: 13; y: 0; width: 1.5; height: parent.height; color: "#1c3550" }
        Rectangle {
            x: 14 - 8; y: apply.height * (1 - apply.shown / 200) - 8; width: 16; height: 16; radius: 8; color: "#0a1a2e"; border.color: "#8ec6ff"; border.width: 2
        }
        MouseArea {
            anchors.fill: parent
            onPressed: (m) => { apply.dragging = true; apply.dragValue = apply.fromY(m.y); root.editor.edit("amount", apply.dragValue) }
            onPositionChanged: (m) => { if (!pressed) return; apply.dragValue = apply.fromY(m.y); root.editor.edit("amount", apply.dragValue) }
            onReleased: { const v = apply.dragValue; apply.dragging = false; root.editor.commit("amount", v) }
            onDoubleClicked: { apply.dragging = false; root.editor.setNow("amount", 100) }
        }
    }
    Text { x: 996 - width / 2; y: 290; text: qsTr("Apply"); color: "#6c8196"; font.family: Theme.fontFamily; font.pixelSize: 16 }
    Text { x: 996 - width / 2; y: 308; text: (apply.shown >= 0 ? "+" : "") + Math.round(apply.shown) + " %"; color: "#9db2c8"; font.family: Theme.fontFamily; font.pixelSize: 16 }

    // the pipeline: Current, Reference, EQ Curve
    component Stage: Item {
        id: st
        property string label
        property string buttonLabel
        property color tint
        property bool lit: false
        property bool armed: false
        property bool buttonActive: true
        property real shift: 0
        signal buttonClicked()
        signal bodyClicked()
        Rectangle { anchors.fill: parent; color: st.lit ? st.tint : "#102740" }
        MouseArea { anchors.fill: parent; onClicked: st.bodyClicked() }
        Text { x: 8; anchors.verticalCenter: parent.verticalCenter; width: parent.width - 150; horizontalAlignment: Text.AlignHCenter; text: st.label; color: st.lit ? "#ffffff" : "#5c7a96"; font.family: Theme.fontFamily; font.pixelSize: 22; elide: Text.ElideRight }
        Rectangle {
            x: parent.width - 138; anchors.verticalCenter: parent.verticalCenter; width: 62; height: 25; radius: 3
            color: st.buttonActive ? (st.armed ? "#e23a2e" : "#8e1f1a") : "#3a1512"
            Text { anchors.centerIn: parent; text: st.buttonLabel; color: st.buttonActive ? "#ffd8d4" : "#7a4a46"; font.family: Theme.fontFamily; font.pixelSize: 15 }
            MouseArea { anchors.fill: parent; enabled: st.buttonActive; onClicked: st.buttonClicked() }
        }
    }
    Row {
        x: 37; y: 283; width: 901; height: 36
        Stage { width: 300; height: 36; label: qsTr("Current"); buttonLabel: qsTr("Learn"); tint: "#1e8f93"; lit: !root.matched && root.curveName === ""; buttonActive: false }
        Stage {
            objectName: "matchEqReference"
            width: 300; height: 36; label: root.curveName === "" ? qsTr("Reference") : root.curveName; buttonLabel: qsTr("Learn"); tint: "#7a3aa8"
            lit: root.curveName !== "" && !root.matched; buttonActive: false
            onBodyClicked: referenceMenu.popup(root, 337 + 300, 283 + 36)
        }
        Stage {
            objectName: "matchEqMatch"
            width: 301; height: 36; label: qsTr("EQ Curve"); buttonLabel: root.busy ? qsTr("…") : qsTr("Match"); tint: "#7e6b1a"
            lit: root.matched; buttonActive: !root.busy && root.curveName !== ""
            onButtonClicked: root.editor.project.matchEqLearn(root.editor.trackId, root.editor.index, root.curveName)
        }
    }
    Menu {
        id: referenceMenu
        Repeater {
            model: root.curves
            delegate: MenuItem {
                required property string modelData
                text: modelData
                checkable: true
                checked: root.curveName === modelData
                onTriggered: root.curveName = modelData
            }
        }
    }

    // the controls
    FxHSlider {
        x: 37; y: 335; width: 310
        property var p: root.editor.param("fadeLow")
        label: qsTr("Fade Low Extremes"); unit: "Hz"; decimals: 0; logarithmic: true; from: p.min; to: p.max; defaultValue: p.def
        value: root.val("fadeLow")
        onMoved: (v) => root.editor.edit("fadeLow", v)
        onReleased: (v) => root.editor.commit("fadeLow", v)
    }
    FxHSlider {
        x: 390; y: 335; width: 330
        property var p: root.editor.param("fadeHigh")
        label: qsTr("Fade High Extremes"); unit: "Hz"; decimals: 0; logarithmic: true; from: p.min; to: p.max; defaultValue: p.def
        value: root.val("fadeHigh")
        onMoved: (v) => root.editor.edit("fadeHigh", v)
        onReleased: (v) => root.editor.commit("fadeHigh", v)
    }
}
