import QtQuick
import QtQuick.Layouts
import Jad

// The Editors area (control bar button Ed, key E): a tab strip that depends on the selection and the editor below it.
// With no region or a MIDI region the first tab is the Piano Roll; with an audio region the tabs are Track | File | Smart Tempo.
// The Piano Roll, the Score (view only), the Step Sequencer and, for audio regions, the Audio Track and File editors exist; the other tabs say so.
Rectangle {
    id: root
    required property ProjectController project
    property real wantedHeight: 380
    property int tab: 0

    readonly property string regionId: project.selectedRegionIds.length > 0 ? project.selectedRegionIds[0] : ""
    readonly property var info: { project.revision; return regionId !== "" ? project.regionInfo(regionId) : ({ found: false }) }
    readonly property bool audioRegion: info.found === true && info.audio === true
    readonly property bool midiRegion: info.found === true && info.audio !== true
    readonly property var tabs: audioRegion ? [qsTr("Track"), qsTr("File"), qsTr("Smart Tempo")]
                              : (midiRegion ? [qsTr("Piano Roll"), qsTr("Score"), qsTr("Step Sequencer"), qsTr("Smart Tempo")]
                                            : [qsTr("Piano Roll"), qsTr("Score"), qsTr("Step Sequencer"), qsTr("Session Player")])
    onTabsChanged: if (tab >= tabs.length) tab = 0

    color: Theme.surfaceCanvas
    implicitHeight: wantedHeight
    clip: true

    Rectangle {
        id: strip
        width: parent.width
        height: 26
        color: Theme.surfacePanel
        Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.borderSubtle }
        Row {
            anchors.centerIn: parent
            spacing: 0
            Repeater {
                model: root.tabs
                delegate: IconButton {
                    required property string modelData
                    required property int index
                    implicitHeight: 20
                    label: modelData
                    active: root.tab === index
                    fillActive: true
                    fillText: Theme.textPrimary
                    onClicked: root.tab = index
                }
            }
        }
    }

    readonly property alias piano: piano
    readonly property bool pianoFocused: piano.visible && piano.activeFocus

    PianoRoll {
        id: piano
        y: strip.height
        width: parent.width
        height: parent.height - strip.height
        visible: root.tab === 0 && !root.audioRegion
        project: root.project
        onTransformRequested: root.transformRequested()
    }
    signal processRequested(string op)
    signal transformRequested()
    AudioEditor {
        id: audioEditor
        y: strip.height
        width: parent.width
        height: parent.height - strip.height
        visible: root.audioRegion && root.tab <= 1
        project: root.project
        regionId: root.regionId
        fileMode: root.tab === 1
        onProcessRequested: (op) => root.processRequested(op)
    }
    ScoreView {
        id: score
        y: strip.height
        width: parent.width
        height: parent.height - strip.height
        visible: !root.audioRegion && root.tab === 1
        project: root.project
        regionId: root.regionId
    }
    StepSequencer {
        id: steps
        y: strip.height
        width: parent.width
        height: parent.height - strip.height
        visible: !root.audioRegion && root.tab === 2 && root.tabs[2] === qsTr("Step Sequencer")
        project: root.project
        regionId: root.regionId
    }
    Text {
        y: strip.height
        width: parent.width
        height: parent.height - strip.height
        visible: (root.tab !== 0 || root.audioRegion) && !(root.audioRegion && root.tab <= 1) && !score.visible && !steps.visible
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        text: qsTr("%1 is not implemented yet").arg(root.tabs[root.tab] ?? "")
        color: Theme.textSecondary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontTypeBodySize
    }

    Splitter {  // the top edge: dragging up makes the editor taller
        orientation: Qt.Vertical
        anchors.top: parent.top
        width: parent.width
        z: 10
        onDragged: (dy) => root.wantedHeight = Math.max(160, Math.min(700, root.wantedHeight - dy))
    }
}
