import QtQuick
import QtQuick.Controls.Basic
import Jad

// The frame every stock plug-in window of Logic Pro has: a dark header (power, preset menu with the arrows, Compare, Copy and Paste, Undo and Redo, and on the
// right the Side Chain menu and the View size), the body of the plug-in, and a bar with the name of the plug-in at the bottom. `editor` is the EffectEditorWindow.
Item {
    id: root
    required property var editor
    property string title: ""
    property real bodyWidth: 700                  // the body at 100 %, in points
    property real bodyHeight: 380
    property bool withSideChain: false
    property color chassis: "#1b2a3d"             // behind the body, seen where the body does not cover
    default property alias content: body.data
    readonly property real zoom: editor.viewPercent / 100
    readonly property real headerHeight: 90
    readonly property real footerHeight: 36
    Component.onCompleted: editor.fitView(headerHeight + footerHeight, bodyHeight, bodyWidth)
    implicitWidth: bodyWidth * zoom
    implicitHeight: headerHeight + bodyHeight * zoom + footerHeight

    readonly property color headerColor: "#232323"
    readonly property color keyColor: "#3a3a3d"
    readonly property color keyText: "#d6d6d8"
    readonly property color keyDim: "#7c7c80"

    component HeaderKey: Rectangle {
        id: key
        property string label
        property bool enabledKey: true
        property bool lit: false
        property color litColor: "#1f78d6"
        signal clicked()
        height: 30
        implicitWidth: Math.max(46, text.implicitWidth + 24)
        radius: 5
        color: lit ? litColor : (mouse.containsMouse && enabledKey ? "#4a4a4e" : root.keyColor)
        Text {
            id: text
            anchors.centerIn: parent
            text: key.label
            color: key.enabledKey ? (key.lit ? "#ffffff" : root.keyText) : root.keyDim
            font.family: Theme.fontFamily
            font.pixelSize: 15
        }
        MouseArea { id: mouse; anchors.fill: parent; hoverEnabled: true; enabled: key.enabledKey; onClicked: key.clicked() }
    }

    Rectangle {  // the header
        id: header
        width: parent.width
        height: root.headerHeight
        color: root.headerColor
        // power: the insert is on or bypassed
        Rectangle {
            id: power
            objectName: "fxPower"
            x: 16; y: 28
            width: 38; height: 38; radius: 19
            color: root.editor.bypassed ? "#3a3a3d" : "#1f5fae"
            Canvas {
                anchors.centerIn: parent
                width: 20; height: 20
                readonly property bool off: root.editor.bypassed
                onOffChanged: requestPaint()
                onPaint: {
                    const c = getContext("2d")
                    c.reset()
                    c.lineWidth = 2.4; c.lineCap = "round"
                    c.strokeStyle = off ? "#8d8d92" : "#ffffff"
                    c.beginPath(); c.arc(10, 11, 7, -Math.PI * 0.32, Math.PI * 1.32 + 0.0, false); c.stroke()
                    c.beginPath(); c.moveTo(10, 1.5); c.lineTo(10, 9.5); c.stroke()
                }
            }
            MouseArea { anchors.fill: parent; onClicked: root.editor.toggleBypass() }
        }
        // the preset
        Rectangle {
            id: presetBox
            objectName: "fxPreset"
            x: 66; y: 14
            width: 262; height: 30; radius: 6
            color: "#2f2f32"
            Text { x: 10; anchors.verticalCenter: parent.verticalCenter; text: root.editor.presetName; color: "#e4e4e6"; font.family: Theme.fontFamily; font.pixelSize: 16; elide: Text.ElideRight; width: parent.width - 36 }
            Text { anchors.right: parent.right; anchors.rightMargin: 9; anchors.verticalCenter: parent.verticalCenter; text: "⌄"; color: "#a8a8ac"; font.pixelSize: 15 }
            MouseArea { anchors.fill: parent; onClicked: root.editor.openPresetMenu(presetBox) }
        }
        Row {
            x: 66; y: 52
            spacing: 8
            Row {
                spacing: 1
                HeaderKey { label: "‹"; implicitWidth: 34; onClicked: root.editor.stepPreset(-1) }
                HeaderKey { label: "›"; implicitWidth: 34; onClicked: root.editor.stepPreset(1) }
            }
            HeaderKey { objectName: "fxCompare"; label: qsTr("Compare"); lit: root.editor.edited; litColor: "#1f78d6"; onClicked: root.editor.compare() }
            Row {
                spacing: 1
                HeaderKey { label: qsTr("Copy"); onClicked: root.editor.copySettings() }
                HeaderKey { label: qsTr("Paste"); enabledKey: root.editor.canPaste; onClicked: root.editor.pasteSettings() }
            }
            Row {
                spacing: 1
                HeaderKey { label: qsTr("Undo"); onClicked: root.editor.project.undo() }
                HeaderKey { label: qsTr("Redo"); onClicked: root.editor.project.redo() }
            }
        }
        Row {  // Side Chain and View
            anchors.right: parent.right
            anchors.rightMargin: 16
            y: 14
            spacing: 12
            Text { visible: root.withSideChain; anchors.verticalCenter: parent.verticalCenter; text: qsTr("Side Chain:"); color: "#9d9da2"; font.family: Theme.fontFamily; font.pixelSize: 15 }
            SelectField {
                visible: root.withSideChain
                objectName: "fxSideChain"
                width: 150
                placeholder: root.editor.kind === "builtin.compressor" ? qsTr("Internal") : qsTr("None")
                // none (the insert's own input, shown as Internal for the compressor), then the tracks that make a signal
                readonly property var sources: { root.editor.project.revision; return root.editor.project.sideChainSources(root.editor.trackId) }
                choices: { const out = [""]; for (const s of sources) out.push(s.id); return out }
                value: { root.editor.project.revision; return root.editor.project.insertSideChain(root.editor.trackId, root.editor.index) }
                format: (id) => { if (id === "") return root.editor.kind === "builtin.compressor" ? qsTr("Internal") : qsTr("None"); for (const s of sources) if (s.id === id) return s.name; return qsTr("None") }
                onChosen: (c) => root.editor.project.setInsertSideChain(root.editor.trackId, root.editor.index, c)
            }
        }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 16
            y: 52
            spacing: 8
            Text { anchors.verticalCenter: parent.verticalCenter; text: qsTr("View:"); color: "#9d9da2"; font.family: Theme.fontFamily; font.pixelSize: 15 }
            Rectangle {
                objectName: "fxView"
                width: 74; height: 30; radius: 6
                color: "#2f2f32"
                Text { x: 10; anchors.verticalCenter: parent.verticalCenter; text: root.editor.viewPercent + "%"; color: "#e4e4e6"; font.family: Theme.fontFamily; font.pixelSize: 16 }
                Text { anchors.right: parent.right; anchors.rightMargin: 7; anchors.verticalCenter: parent.verticalCenter; text: "↕"; color: "#a8a8ac"; font.pixelSize: 14 }
                MouseArea { anchors.fill: parent; onClicked: root.editor.stepView(mouseY < height / 2 ? 1 : -1) }
                MouseArea { anchors.fill: parent; acceptedButtons: Qt.NoButton; onWheel: (w) => root.editor.stepView(w.angleDelta.y > 0 ? 1 : -1) }
            }
        }
    }

    Rectangle {  // the body
        x: 0
        y: root.headerHeight
        width: parent.width
        height: root.bodyHeight * root.zoom
        color: root.chassis
        clip: true
        Item {
            id: body
            width: root.bodyWidth
            height: root.bodyHeight
            scale: root.zoom
            transformOrigin: Item.TopLeft
        }
    }

    Rectangle {  // the name of the plug-in
        x: 0
        y: parent.height - root.footerHeight
        width: parent.width
        height: root.footerHeight
        color: "#202020"
        Text {
            anchors.centerIn: parent
            text: root.title
            color: "#e8e8ea"
            font.family: Theme.fontFamily
            font.pixelSize: 21
        }
        Text {  // the disclosure arrow at the left: the plain list of parameters
            objectName: "fxShowParameters"
            x: 12
            anchors.verticalCenter: parent.verticalCenter
            text: "›"
            color: "#a8a8ac"
            font.pixelSize: 24
            MouseArea { anchors.fill: parent; anchors.margins: -8; onClicked: root.editor.showList = true }
        }
    }
}
