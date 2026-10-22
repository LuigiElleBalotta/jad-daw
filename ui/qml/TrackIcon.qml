import QtQuick
import QtQuick.Effects
import Jad

// The glyph of a track kind (audio, instrument, aux, bus, master).
Item {
    id: root
    property string kind: "audio"
    property string glyphFile: ""         // a picture by file name (icons/<glyph>.svg), whatever the kind
    property string icon: ""            // Track > Assign Track Icon: a picture of its own instead of the one of the kind
    property color tint: Theme.textPrimary
    property real size: Theme.sizeIcon
    readonly property string file: glyphFile !== "" ? glyphFile : icon !== "" ? (icon === "audio" || icon === "instrument" ? "icon-" + icon : "track-" + icon)
                                              : (({ "audio": "track-audio", "instrument": "track-instrument", "aux": "track-aux",
                                                    "bus": "track-bus", "master": "track-bus" })[kind] ?? "track-audio")
    // a glyph picked by file is white and drawn as it is (opacity dims it): the colourizing effect does not draw inside popups
    readonly property bool plain: glyphFile !== ""
    implicitWidth: size
    implicitHeight: size
    width: size
    height: size
    Image {
        id: glyph
        anchors.fill: parent
        source: "icons/" + root.file + ".svg"
        sourceSize: Qt.size(root.size * 2, root.size * 2)
        visible: root.plain
    }
    MultiEffect {
        visible: !root.plain
        anchors.fill: glyph
        source: glyph
        brightness: 1.0
        colorization: 1.0
        colorizationColor: root.tint
    }
}
