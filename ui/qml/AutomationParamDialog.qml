import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Jad

// The parameter an automation lane shows: Volume, Pan, a send or, for a loaded VST3 insert, one of its parameters. Type to search.
Dialog {
    id: root
    required property ProjectController project
    property string trackId: ""
    modal: true
    anchors.centerIn: parent
    width: 460
    height: 420
    property var all: []
    readonly property var matches: {
        const q = entry.text.trim().toLowerCase()
        return all.filter(c => q === "" || c.label.toLowerCase().indexOf(q) >= 0)
    }
    onAboutToShow: { all = project.automationChoices(trackId); entry.text = ""; entry.input.forceActiveFocus() }
    function choose(c) { project.setTrackAutomationParam(trackId, c.param); close() }
    background: Rectangle { color: Theme.surfacePanel; border.color: Theme.borderStrong; radius: Theme.radiusDialog }
    header: Item { height: 0 }
    contentItem: ColumnLayout {
        spacing: Theme.spacing[3]
        Text { text: qsTr("Automation Parameter"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeTitleSize; font.weight: Theme.fontTypeTitleWeight }
        Text {
            visible: root.all.length > 0 && root.all.length <= 4
            text: qsTr("Plug-in parameters appear here once the plug-ins of the track have loaded.")
            color: Theme.textSecondary; wrapMode: Text.Wrap; Layout.fillWidth: true; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeCaptionSize
        }
        TextEntry {
            id: entry
            Layout.fillWidth: true
            objectName: "paramEntry"
            onEdited: (t) => { const first = root.matches[0]; if (first) root.choose(first) }
        }
        ListView {
            objectName: "paramResults"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.matches
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            delegate: Rectangle {
                required property var modelData
                width: ListView.view.width - 10
                height: 24
                radius: 3
                color: area.containsMouse ? Theme.surfaceRaisedHover : "transparent"
                Text { anchors.fill: parent; anchors.leftMargin: Theme.spacing[2]; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight; text: modelData.label; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize }
                MouseArea { id: area; anchors.fill: parent; hoverEnabled: true; onClicked: root.choose(modelData) }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            IconButton { implicitHeight: 26; label: qsTr("Close"); onClicked: root.close() }
        }
    }
}
