import QtQuick
import Jad

// Logic's frame round a knob for every parameter: what every stock effect and instrument that has no layout of its own looks like.
FxShell {
    id: root
    title: editor.spec ? editor.spec.name : ""
    bodyWidth: 700
    bodyHeight: Math.max(220, grid.implicitHeight + 24)
    chassis: "#27303b"
    FxKnobGrid { id: grid; editor: root.editor; width: root.bodyWidth; y: 12 }
}
