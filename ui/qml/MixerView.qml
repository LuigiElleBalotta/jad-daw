import QtQuick
import QtQuick.Layouts
import Jad

// The Mixer itself: its local bar and the strips, for any size. The docked Mixer (Mixer.qml) and the Mixer window both
// show it. A short view gets compact strips and no legend; a view shorter than a compact strip scrolls; a narrow one
// drops the type filters and then Single | Tracks | All from the bar and scrolls sideways.
Item {
    id: root
    required property ProjectController project
    property bool detached: false
    signal detachToggled()
    readonly property alias bar: bar

    readonly property real barHeight: 24
    // the shortest strip that shows every row and the legend (View > Long Faders asks for longer faders); the view takes all the
    // height it is given: the fader of every strip grows with it, however tall the window or the screen is
    readonly property real minStripHeight: bar.longFaders ? 694 : 536
    readonly property real stripHeight: Math.max(minStripHeight, height - barHeight)
    readonly property bool compact: stripHeight - Theme.spacing[3] * 2 < 520
    // the type filter id of a strip: Logic's aux is our bus kind
    function typeOf(info) { return info.master ? "master" : (info.kind === "bus" ? "aux" : info.kind) }
    // does the strip pass the Single | Tracks | All choice and the type filters
    function shown(info) {
        if (bar.hiddenTypes[typeOf(info)] === true) return false
        if (bar.scope === "all") return true
        if (bar.scope === "single") return project.selectedTrackIds.indexOf(info.trackId) >= 0
        return info.master || info.kind !== "bus"  // Tracks: the track strips and the master
    }

    clip: true

    MixerHeader {
        id: bar
        width: parent.width
        height: root.barHeight
        project: root.project
        detached: root.detached
        onDetachToggled: root.detachToggled()
        onScopeSelected: (scope) => { bar.scope = scope }
        onTypeToggled: (typeId, on) => {
            const h = Object.assign({}, bar.hiddenTypes)
            if (on) delete h[typeId]; else h[typeId] = true
            bar.hiddenTypes = h
        }
        onOnlyTypeRequested: (typeId) => {
            // Option-click: only this type; again (when it is the only one) shows all
            const h = {}
            let others = 0
            for (const t of bar.types) if (t.id !== typeId && bar.hiddenTypes[t.id] !== true) ++others
            if (others > 0 || bar.hiddenTypes[typeId] === true) for (const t of bar.types) if (t.id !== typeId) h[t.id] = true
            bar.hiddenTypes = h
        }
    }

    Flickable {
        id: flick
        y: root.barHeight
        width: parent.width
        height: parent.height - root.barHeight
        contentWidth: strips.width + legend.width + Theme.spacing[4] * 2
        contentHeight: root.stripHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.AutoFlickIfNeeded
        // a click on the empty mixer takes the focus away from a name being edited, which confirms it
        TapHandler { onPressedChanged: if (pressed) root.forceActiveFocus() }

        // Swipe over M or S: a press on one button and a drag across the others sets them all to the state the first one took
        function swipeTo(kind, on, scenePos, from) {
            const hit = strips.childAt(strips.mapFromItem(null, scenePos).x, 4)
            if (!hit || hit === from || !hit.visible || !hit.muteButton) return
            const button = kind === "mute" ? hit.muteButton : hit.soloButton
            const local = button.mapFromItem(null, scenePos)
            if (local.x < 0 || local.y < 0 || local.x > button.width || local.y > button.height) return
            if (kind === "mute") { if (hit.mute !== on) root.project.setMute(hit.trackId, on) }
            else if (hit.solo !== on) root.project.setSolo(hit.trackId, on)
        }

        MixerLegend { id: legend; y: Theme.spacing[3]; height: root.stripHeight; visible: !bar.legendHidden && !root.compact }
        Row {
            id: strips
            x: (legend.visible ? legend.width : 0) + Theme.spacing[2]
            y: Theme.spacing[3]
            height: root.stripHeight - Theme.spacing[3] * 2
            spacing: Theme.spacing[2]

            // tracks first, the master strip last
            Repeater {
                model: root.project.mixer
                delegate: ProjectStrip {
                    id: strip
                    required property var model
                    visible: !model.isMaster && root.shown(model.info)
                    width: visible ? implicitWidth : 0
                    height: strips.height
                    project: root.project
                    info: model.info
                    longFader: bar.longFaders
                    onSwiped: (kind, on, p, from) => flick.swipeTo(kind, on, p, from)
                }
            }
            Repeater {
                model: root.project.mixer
                delegate: ProjectStrip {
                    required property var model
                    visible: model.isMaster && root.shown(model.info)
                    width: visible ? implicitWidth : 0
                    height: strips.height
                    project: root.project
                    info: model.info
                    longFader: bar.longFaders
                    peak: root.project.masterPeak
                    onSwiped: (kind, on, p, from) => flick.swipeTo(kind, on, p, from)
                }
            }
        }
    }
}
