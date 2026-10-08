import QtQuick
import QtQuick.Layouts
import Jad

// The Smart Controls pane: the controls of the selected track's patch as labelled knobs, grouped in panels. One knob
// can move several parameters (the patch decides); a release is one undo step.
Rectangle {
    id: root
    required property ProjectController project
    readonly property var insp: project.inspector
    readonly property var controls: insp.smartControls
    readonly property var groups: {
        const order = [], byName = {}
        for (const c of controls) {
            const g = c.group !== "" ? c.group : qsTr("Controls")
            if (!(g in byName)) { byName[g] = []; order.push(g) }
            byName[g].push(c)
        }
        return order.map((g) => ({ name: g, controls: byName[g] }))
    }
    readonly property int knobCount: controls.length
    readonly property alias groupList: groupRepeater
    readonly property alias emptyLabel: empty
    readonly property alias compareButton: compare
    readonly property alias eqTab: eq
    function knobFor(controlId) {
        for (let g = 0; g < groupRepeater.count; ++g) {
            const grp = groupRepeater.itemAt(g)
            if (!grp) continue
            for (let k = 0; k < grp.knobs.count; ++k) {
                const item = grp.knobs.itemAt(k)
                if (item && item.controlId === controlId) return item
            }
        }
        return null
    }
    color: Theme.surfaceCanvas
    clip: true

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 28
            color: Theme.surfacePanel
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacing[3]
                anchors.rightMargin: Theme.spacing[3]
                spacing: Theme.spacing[3]
                Rectangle {
                    implicitWidth: 56
                    implicitHeight: 20
                    radius: Theme.radiusControl
                    color: Theme.accentPrimary
                    Text { anchors.centerIn: parent; text: qsTr("Track"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeLabelSize }
                }
                Text {
                    text: root.insp.track.patchName && root.insp.track.patchName !== "" ? root.insp.track.patchName : qsTr("No patch")
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontTypeLabelSize
                }
                IconButton { id: compare; implicitWidth: 64; implicitHeight: 20; label: qsTr("Compare"); opacity: 0.6; onClicked: root.project.announceStub(qsTr("Compare")) }
                Item { Layout.fillWidth: true }
                IconButton { implicitWidth: 64; implicitHeight: 20; label: qsTr("Controls"); active: true }
                IconButton { id: eq; implicitWidth: 40; implicitHeight: 20; label: qsTr("EQ"); opacity: 0.6; onClicked: root.project.announceStub(qsTr("Smart Controls EQ")) }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0
            Rectangle {
                Layout.preferredWidth: 190
                Layout.fillHeight: true
                color: Theme.surfacePanel
                Column {
                    anchors.fill: parent
                    Text {
                        x: Theme.spacing[3]
                        height: 26
                        verticalAlignment: Text.AlignVCenter
                        text: qsTr("Automatic Smart Controls")
                        color: Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontTypeLabelSize
                    }
                    PanelHeader { width: parent.width; title: qsTr("Parameter Mapping"); expanded: false; onToggled: if (expanded) root.project.announceStub(qsTr("Parameter Mapping")) }
                    PanelHeader { width: parent.width; title: qsTr("External Assignment"); expanded: false; onToggled: if (expanded) root.project.announceStub(qsTr("External Assignment")) }
                }
            }
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Text {
                    id: empty
                    anchors.centerIn: parent
                    visible: root.knobCount === 0
                    text: root.insp.hasTrack ? qsTr("This track has no patch with Smart Controls: choose one in the Library") : qsTr("Select a track")
                    color: Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontTypeBodySize
                }
                Row {
                    anchors.centerIn: parent
                    spacing: Theme.spacing[5]
                    Repeater {
                        id: groupRepeater
                        model: root.groups
                        delegate: Rectangle {
                            id: panel
                            required property var modelData
                            readonly property alias knobs: knobRepeater
                            width: row.width + Theme.spacing[5] * 2
                            height: 108
                            radius: Theme.radiusCard
                            color: Theme.surfacePanel
                            border.color: Theme.borderStrong
                            Row {
                                id: row
                                anchors.centerIn: parent
                                spacing: Theme.spacing[3]
                                Repeater {
                                    id: knobRepeater
                                    model: panel.modelData.controls
                                    delegate: ScreenKnob {
                                        required property var modelData
                                        readonly property string controlId: modelData.id
                                        label: modelData.label
                                        from: modelData.min
                                        to: modelData.max
                                        resetValue: modelData.def
                                        value: modelData.value
                                        onReleased: (v) => root.project.setSmartControl(root.insp.track.trackId, modelData.id, v)
                                    }
                                }
                            }
                            Text {
                                anchors.top: parent.top
                                anchors.topMargin: 2
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: panel.modelData.name
                                color: Theme.textDisabled
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontTypeCaptionSize
                            }
                        }
                    }
                }
            }
        }
    }
}
