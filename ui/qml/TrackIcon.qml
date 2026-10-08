import QtQuick
import QtQuick.Effects
import Jad

// The glyph of a track kind (audio, instrument, aux, bus, master).
Item {
    id: root
    property string kind: "audio"
    property color tint: Theme.textPrimary
    property real size: Theme.sizeIcon
    readonly property string file: ({ "audio": "track-audio", "instrument": "track-instrument", "aux": "track-aux",
                                      "bus": "track-bus", "master": "track-bus" })[kind] ?? "track-audio"
    implicitWidth: size
    implicitHeight: size
    width: size
    height: size
    Image {
        id: glyph
        anchors.fill: parent
        source: "icons/" + root.file + ".svg"
        sourceSize: Qt.size(root.size * 2, root.size * 2)
        visible: false
    }
    MultiEffect {
        anchors.fill: glyph
        source: glyph
        brightness: 1.0
        colorization: 1.0
        colorizationColor: root.tint
    }
}
