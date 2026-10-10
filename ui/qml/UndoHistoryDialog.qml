import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Jad

// Edit > Undo History: every step of the project, the latest on top; a click goes back (or forward) to that point.
Dialog {
    id: root
    required property ProjectController project
    modal: true
    anchors.centerIn: parent
    width: 420
    height: 460
    property var undo: []
    property var redo: []
    function reload() { const h = project.undoHistory(); undo = h.undo; redo = h.redo }
    onAboutToShow: reload()
    Connections { target: root.project; function onProjectChanged() { if (root.visible) root.reload() } }
    background: Rectangle { color: Theme.surfacePanel; border.color: Theme.borderStrong; radius: Theme.radiusDialog }
    header: Item { height: 0 }

    // the list: the steps that can be redone (faded) above, then the ones that were done, newest first
    readonly property var rows: {
        const out = []
        for (let i = redo.length - 1; i >= 0; --i) out.push({ label: redo[i], done: false, steps: redo.length - i })
        for (let i = undo.length - 1; i >= 0; --i) out.push({ label: undo[i], done: true, steps: undo.length - 1 - i })
        return out
    }
    contentItem: ColumnLayout {
        spacing: Theme.spacing[3]
        Text { text: qsTr("Undo History"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeTitleSize; font.weight: Theme.fontTypeTitleWeight }
        Text { visible: root.rows.length === 0; text: qsTr("Nothing has been done yet."); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize }
        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.rows
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            delegate: Rectangle {
                required property var modelData
                required property int index
                width: ListView.view.width - 10
                height: 24
                radius: 3
                color: area.containsMouse ? Theme.surfaceRaisedHover : (modelData.done && modelData.steps === 0 ? Theme.accentPrimary : "transparent")
                Text {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spacing[2]
                    verticalAlignment: Text.AlignVCenter
                    text: modelData.label
                    color: modelData.done ? Theme.textPrimary : Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontTypeBodySize
                    font.italic: !modelData.done
                }
                MouseArea {
                    id: area
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        if (modelData.done) root.project.undoSteps(modelData.steps)
                        else root.project.redoSteps(modelData.steps)
                        refresh.start()
                    }
                }
            }
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: Theme.spacing[2]
            IconButton { objectName: "clearHistory"; implicitHeight: 26; label: qsTr("Delete History"); onClicked: { root.project.clearUndoHistory(); root.reload() } }
            IconButton { implicitHeight: 26; label: qsTr("Close"); onClicked: root.close() }
        }
    }
    Timer { id: refresh; interval: 250; onTriggered: root.reload() }
}
