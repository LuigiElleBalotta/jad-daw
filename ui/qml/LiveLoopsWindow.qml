import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Jad

// View > Live Loops: the grid of cells, scenes down and tracks across. A cell is a loop that is launched with its play button, a scene launches the row, and
// everything starts at the next multiple of the quantization. Cells are made with Edit > Copy to Live Loops Scene, or from the selected regions of a track.
Dialog {
    id: root
    required property ProjectController project
    property var grid: ({ tracks: [], cells: [], scenes: 8, sceneNames: [] })
    readonly property int cellW: 110
    readonly property int cellH: 34
    readonly property var quantizeChoices: [
        { label: qsTr("1/4 note"), beats: 1 }, { label: qsTr("1/2 note"), beats: 2 }, { label: qsTr("1 bar"), beats: 0 },
        { label: qsTr("2 bars"), beats: -2 }, { label: qsTr("4 bars"), beats: -4 }, { label: qsTr("8 bars"), beats: -8 }]
    modal: false
    anchors.centerIn: parent
    width: Math.min(parent ? parent.width - 40 : 760, Math.max(420, 150 + grid.tracks.length * (cellW + 4) + 24))
    height: Math.min(parent ? parent.height - 40 : 460, 130 + grid.scenes * (cellH + 4) + 24)
    function refresh() { grid = project.liveGrid() }
    function cellAt(trackId, scene) {
        for (let i = 0; i < grid.cells.length; ++i) {
            const c = grid.cells[i]
            if (c.trackId === trackId && c.scene === scene) return c
        }
        return null
    }
    function quantizeBeats(choice) { return choice.beats > 0 ? choice.beats : choice.beats === 0 ? project.barBeats : -choice.beats * project.barBeats }
    onAboutToShow: refresh()
    Connections { target: root.project; function onLiveLoopsChanged() { if (root.visible) root.refresh() } function onProjectChanged() { if (root.visible) root.refresh() } }
    background: Rectangle { color: Theme.surfacePanel; border.color: Theme.borderStrong; radius: Theme.radiusDialog }
    header: Item { height: 0 }
    contentItem: ColumnLayout {
        spacing: Theme.spacing[2]
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing[2]
            Text { text: qsTr("Live Loops"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeTitleSize; font.weight: Theme.fontTypeTitleWeight }
            Item { Layout.fillWidth: true }
            Text { text: qsTr("Quantization"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize }
            SelectField {
                objectName: "liveQuantize"
                implicitWidth: 100
                choices: root.quantizeChoices.map(c => c.label)
                value: {
                    const stored = Number(root.project.projectSettings.liveQuantize)
                    for (let i = 0; i < root.quantizeChoices.length; ++i)
                        if (Math.abs(root.quantizeBeats(root.quantizeChoices[i]) - (stored > 0 ? stored : root.project.barBeats)) < 1e-6) return root.quantizeChoices[i].label
                    return root.quantizeChoices[2].label
                }
                onChosen: (choice) => { for (const c of root.quantizeChoices) if (c.label === choice) root.project.setProjectSetting("liveQuantize", String(root.quantizeBeats(c))) }
            }
            IconButton { objectName: "liveStopAll"; implicitHeight: 24; label: qsTr("Stop All"); onClicked: root.project.stopAllCells() }
            IconButton { objectName: "liveAddScene"; implicitHeight: 24; label: qsTr("Add Scene"); onClicked: root.project.addLiveScene() }
        }
        Text {
            Layout.fillWidth: true
            visible: root.grid.tracks.length === 0
            wrapMode: Text.Wrap
            text: qsTr("There is no audio, instrument or MIDI track yet.")
            color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize
        }
        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: gridColumn.width
            contentHeight: gridColumn.height
            ScrollBar.vertical: ScrollBar { }
            ScrollBar.horizontal: ScrollBar { }
            Column {
                id: gridColumn
                spacing: 4
                Row {
                    spacing: 4
                    Item { width: 146; height: 22 }
                    Repeater {
                        model: root.grid.tracks
                        delegate: Rectangle {
                            required property var modelData
                            width: root.cellW; height: 22; radius: 3
                            color: TrackColors.solid(modelData.color)
                            Text { anchors.fill: parent; anchors.margins: 4; text: modelData.name; elide: Text.ElideRight; color: "#101010"; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeCaptionSize; verticalAlignment: Text.AlignVCenter }
                            MouseArea { anchors.fill: parent; onClicked: root.project.stopCell(modelData.id); ToolTip.visible: containsMouse; ToolTip.text: qsTr("Click to stop the cell"); hoverEnabled: true }
                        }
                    }
                }
                Repeater {
                    model: root.grid.scenes
                    delegate: Row {
                        id: sceneRow
                        required property int index
                        spacing: 4
                        Rectangle {
                            width: 142; height: root.cellH; radius: 3
                            color: Theme.surfaceRaised
                            border.color: Theme.borderStrong
                            RowLayout {
                                anchors.fill: parent; anchors.margins: 4; spacing: 4
                                IconButton { objectName: "launchScene" + sceneRow.index; implicitHeight: 24; implicitWidth: 24; label: "▶"; onClicked: root.project.launchScene(sceneRow.index) }
                                TextInput {
                                    Layout.fillWidth: true
                                    text: root.grid.sceneNames[sceneRow.index] === "" || root.grid.sceneNames[sceneRow.index] === undefined ? qsTr("Scene %1").arg(sceneRow.index + 1) : root.grid.sceneNames[sceneRow.index]
                                    color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize
                                    selectByMouse: true; clip: true
                                    onEditingFinished: root.project.renameLiveScene(sceneRow.index, text === qsTr("Scene %1").arg(sceneRow.index + 1) ? "" : text)
                                }
                            }
                        }
                        Repeater {
                            model: root.grid.tracks
                            delegate: Rectangle {
                                id: cell
                                required property var modelData
                                readonly property var info: root.cellAt(modelData.id, sceneRow.index)
                                readonly property string state: info ? info.state : "empty"
                                width: root.cellW; height: root.cellH; radius: 3
                                color: info ? TrackColors.solid(modelData.color) : Theme.surfaceCanvas
                                opacity: state === "stopped" ? 0.55 : 1
                                border.width: state === "queued" || state === "stopping" ? 2 : 1
                                border.color: state === "playing" ? Theme.textPrimary : state === "queued" || state === "stopping" ? Theme.stateRecord : Theme.borderStrong
                                Text {
                                    anchors.fill: parent; anchors.leftMargin: 26; anchors.rightMargin: 4
                                    visible: cell.info !== null
                                    text: cell.info ? (cell.info.name !== "" ? cell.info.name : modelData.name) : ""
                                    elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter
                                    color: "#101010"; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeCaptionSize
                                }
                                Text {
                                    x: 8; anchors.verticalCenter: parent.verticalCenter
                                    visible: cell.info !== null
                                    text: cell.state === "playing" || cell.state === "stopping" ? "■" : "▶"
                                    color: "#101010"; font.pixelSize: 12
                                }
                                MouseArea {
                                    objectName: "cell_" + modelData.id + "_" + sceneRow.index
                                    anchors.fill: parent
                                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                                    onClicked: (mouse) => {
                                        if (mouse.button === Qt.RightButton || cell.info === null) cellMenu.popup()
                                        else if (cell.state === "playing" || cell.state === "queued") root.project.stopCell(modelData.id)
                                        else root.project.launchCell(modelData.id, sceneRow.index)
                                    }
                                }
                                Menu {
                                    id: cellMenu
                                    MenuItem { text: qsTr("Set from Selected Regions"); onTriggered: root.project.setCellFromSelection(cell.modelData.id, sceneRow.index) }
                                    MenuItem { text: qsTr("Clear Cell"); enabled: cell.info !== null; onTriggered: root.project.clearCell(cell.modelData.id, sceneRow.index) }
                                }
                            }
                        }
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            IconButton { implicitHeight: 26; label: qsTr("Close"); onClicked: root.close() }
        }
    }
}
