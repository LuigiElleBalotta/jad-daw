pragma Singleton
import QtQuick

// The rows of a channel strip, top to bottom: the strips and the legend of the Mixer both follow these heights, so that
// every label sits at the height of its row.
QtObject {
    readonly property real margin: 4
    readonly property real spacing: 2
    readonly property real setting: 18
    readonly property real gainReduction: 6
    readonly property real eq: 26
    readonly property real input: 18
    readonly property real fx: 66
    readonly property real sends: 26
    readonly property real output: 18
    readonly property real group: 18
    readonly property real automation: 18
    readonly property real icon: 24
    readonly property real pan: 30
    readonly property real db: 18
    readonly property real faderMin: 110
}
