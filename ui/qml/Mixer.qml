import QtQuick
import QtQuick.Layouts
import Jad

Rectangle {
    id: root
    required property ProjectController project
    property bool expanded: true
    readonly property real headerHeight: 24
    readonly property real filterHeight: 24
    // the strips take what the window can spare (a short window gets compact strips, without the legend)
    property real stripHeight: Math.min(header2.longFaders ? 640 : 500, Math.max(380, (Window.height - 300) * 0.55))
    readonly property bool compact: stripHeight - Theme.spacing[3] * 2 < 470
    // the type filter id of a strip: Logic's aux is our bus kind
    function typeOf(info) { return info.master ? "master" : (info.kind === "bus" ? "aux" : info.kind) }
    // does the strip pass the Single | Tracks | All choice and the type filters
    function shown(info) {
        if (header2.hiddenTypes[typeOf(info)] === true) return false
        if (header2.scope === "all") return true
        if (header2.scope === "single") return project.selectedTrackIds.indexOf(info.trackId) >= 0
        return info.master || info.kind !== "bus"  // Tracks: the track strips and the master
    }

    color: Theme.surfaceCanvas
    implicitHeight: expanded ? headerHeight + filterHeight + stripHeight : headerHeight
    clip: true

    Rectangle {
        id: header
        width: parent.width
        height: root.headerHeight
        color: Theme.surfacePanel
        Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.borderSubtle }
        Row {
            anchors.verticalCenter: parent.verticalCenter
            x: Theme.spacing[3]
            spacing: Theme.spacing[3]
            IconButton {
                implicitWidth: 20
                implicitHeight: 20
                source: root.expanded ? "icons/chevron-down.svg" : "icons/chevron-right.svg"
                onClicked: root.expanded = !root.expanded
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Mixer")
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTypeLabelSize
                font.weight: Theme.fontTypeLabelWeight
            }
        }
    }

    MixerHeader {
        id: header2
        visible: root.expanded
        y: root.headerHeight
        width: parent.width
        height: root.filterHeight
        project: root.project
        onScopeSelected: (scope) => { header2.scope = scope }
        onTypeToggled: (typeId, on) => {
            const h = Object.assign({}, header2.hiddenTypes)
            if (on) delete h[typeId]; else h[typeId] = true
            header2.hiddenTypes = h
        }
        onOnlyTypeRequested: (typeId) => {
            // Option-click: only this type; again (when it is the only one) shows all
            const h = {}
            let others = 0
            for (const t of header2.types) if (t.id !== typeId && header2.hiddenTypes[t.id] !== true) ++others
            if (others > 0 || header2.hiddenTypes[typeId] === true) for (const t of header2.types) if (t.id !== typeId) h[t.id] = true
            header2.hiddenTypes = h
        }
    }

    Flickable {
        visible: root.expanded
        y: root.headerHeight + root.filterHeight
        width: parent.width
        height: root.stripHeight
        contentWidth: strips.width + legend.width + Theme.spacing[4] * 2
        contentHeight: height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        // a click on the empty mixer takes the focus away from a name being edited, which confirms it
        TapHandler { onPressedChanged: if (pressed) root.forceActiveFocus() }

        MixerLegend { id: legend; y: Theme.spacing[3]; height: parent.height; visible: !header2.legendHidden && !root.compact }
        Row {
            id: strips
            x: (legend.visible ? legend.width : 0) + Theme.spacing[2]
            y: Theme.spacing[3]
            height: parent.height - Theme.spacing[3] * 2
            spacing: Theme.spacing[2]

            // tracks first, the master strip last
            Repeater {
                model: root.project.mixer
                delegate: ProjectStrip {
                    required property var model
                    visible: !model.isMaster && root.shown(model.info)
                    width: visible ? implicitWidth : 0
                    height: strips.height
                    project: root.project
                    info: model.info
                    longFader: header2.longFaders
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
                    longFader: header2.longFaders
                    peak: root.project.masterPeak
                }
            }
        }
    }
}
