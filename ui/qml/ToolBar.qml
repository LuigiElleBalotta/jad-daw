import QtQuick
import QtQuick.Layouts
import Jad

// The local toolbar of the tracks area: Edit / Functions / View menus, the five tools, snap and drag mode, track
// heights, zoom, catch playhead, undo and redo.
Panel {
    id: root
    required property ProjectController project
    readonly property alias pointerButton: pointerButton
    readonly property alias pencilButton: pencilButton
    readonly property alias eraserButton: eraserButton
    readonly property alias scissorsButton: scissorsButton
    readonly property alias glueButton: glueButton
    readonly property string snapLabel: ({ "off": "Off", "bar": "Bar", "half": "1/2", "quarter": "1/4", "eighth": "1/8", "sixteenth": "1/16" })[project.snap] ?? project.snap

    implicitHeight: 32
    radius: 0

    component MenuButton: IconButton {
        id: button
        property alias ids: menu.ids
        implicitHeight: 24
        onClicked: menu.popup(0, height)
        ActionMenu { id: menu }
    }
    component Separator: Rectangle {
        width: 1
        height: 18
        color: Theme.borderStrong
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacing[4]
        anchors.rightMargin: Theme.spacing[4]
        spacing: Theme.spacing[3]

        MenuButton {
            label: qsTr("Edit ▾")
            ids: ["edit.undo", "edit.redo", "edit.cut", "edit.copy", "edit.paste", "edit.duplicate", "edit.delete", "edit.selectAll", "edit.deselectAll"]
        }
        MenuButton {
            label: qsTr("Functions ▾")
            ids: ["edit.splitAtPlayhead", "edit.joinRegions", "transport.goToPosition", "nav.followPlayhead"]
        }
        MenuButton {
            label: qsTr("View ▾")
            ids: ["view.zoomIn", "view.zoomOut", "view.zoomFit", "view.waveformZoom", "track.height.compact", "track.height.normal", "track.height.large", "track.height.xlarge"]
        }
        Separator {}

        Row {
            spacing: Theme.spacing[1]
            ActionButton { id: pointerButton; actionId: "tool.pointer"; source: "icons/pointer.svg"; implicitHeight: 24 }
            ActionButton { id: pencilButton; actionId: "tool.pencil"; source: "icons/pencil.svg"; implicitHeight: 24 }
            ActionButton { id: eraserButton; actionId: "tool.eraser"; source: "icons/eraser.svg"; implicitHeight: 24 }
            ActionButton { id: scissorsButton; actionId: "tool.scissors"; source: "icons/scissors.svg"; implicitHeight: 24 }
            ActionButton { id: glueButton; actionId: "tool.glue"; source: "icons/glue.svg"; implicitHeight: 24 }
        }
        Separator {}

        MenuButton {
            label: qsTr("Snap: %1 ▾").arg(root.snapLabel)
            ids: ["snap.off", "snap.bar", "snap.half", "snap.quarter", "snap.eighth", "snap.sixteenth", "snap.smart"]
        }
        MenuButton {
            label: qsTr("Drag: Overlap ▾")
            ids: ["drag.overlap", "drag.noOverlap", "drag.xfade"]
        }
        Separator {}

        Row {
            spacing: Theme.spacing[1]
            ActionButton { actionId: "track.height.compact"; label: "S"; implicitHeight: 24 }
            ActionButton { actionId: "track.height.normal"; label: "M"; implicitHeight: 24 }
            ActionButton { actionId: "track.height.large"; label: "L"; implicitHeight: 24 }
            ActionButton { actionId: "track.height.xlarge"; label: "XL"; implicitHeight: 24 }
        }
        Row {
            spacing: Theme.spacing[1]
            ActionButton { actionId: "view.zoomOut"; source: "icons/zoom-out.svg"; implicitHeight: 24 }
            ActionButton { actionId: "view.zoomIn"; source: "icons/zoom-in.svg"; implicitHeight: 24 }
            ActionButton { actionId: "nav.followPlayhead"; label: qsTr("Catch"); implicitHeight: 24 }
        }

        Item { Layout.fillWidth: true }

        Row {
            spacing: Theme.spacing[1]
            ActionButton { actionId: "edit.undo"; label: qsTr("Undo"); implicitHeight: 24 }
            ActionButton { actionId: "edit.redo"; label: qsTr("Redo"); implicitHeight: 24 }
        }
    }
}
