import QtQuick
import QtQuick.Layouts
import Jad

// The Library: the patches of the selected track's kind. Categories on the left, patches on the right; clicking a
// patch or stepping with Up/Down applies it to the track; Revert applies the track's patch again.
Rectangle {
    id: root
    required property ProjectController project
    readonly property var lib: project.library
    readonly property string colorName: project.inspector.track.color ?? "purple"
    readonly property alias emptyLabel: empty
    readonly property alias searchField: search
    readonly property alias categoryList: categories
    readonly property alias patchList: patches
    readonly property alias revertButton: revert
    readonly property alias saveButton: save
    readonly property alias deleteButton: del
    function step(n) {
        const id = lib.neighbour(n)
        if (id !== "") project.applyPatch(id)
    }
    color: Theme.surfacePanel
    clip: true
    focus: true
    activeFocusOnTab: true
    Keys.onUpPressed: step(-1)
    Keys.onDownPressed: step(1)

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 24
            color: Theme.surfaceRaised
            Text {
                anchors.centerIn: parent
                text: qsTr("Library")
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTypeLabelSize
                font.weight: Theme.fontTypeLabelWeight
            }
        }
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 84
            visible: root.project.inspector.hasTrack
            TrackIcon {
                anchors.horizontalCenter: parent.horizontalCenter
                y: Theme.spacing[3]
                size: 36
                kind: root.project.inspector.track.kind ?? "audio"
                tint: Theme["track" + root.colorName.charAt(0).toUpperCase() + root.colorName.slice(1) + "Solid"]
            }
            Text {
                anchors.bottom: parent.bottom
                anchors.bottomMargin: Theme.spacing[2]
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: root.project.inspector.track.patchName && root.project.inspector.track.patchName !== ""
                      ? root.project.inspector.track.patchName : (root.project.inspector.track.name ?? "")
                elide: Text.ElideRight
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTypeLabelSize
            }
        }
        Text {
            id: empty
            visible: !root.project.inspector.hasTrack
            Layout.fillWidth: true
            Layout.preferredHeight: 80
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            text: qsTr("Select a track to see its patches")
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTypeBodySize
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.margins: Theme.spacing[2]
            Layout.preferredHeight: 22
            visible: root.project.inspector.hasTrack
            radius: Theme.radiusControl
            color: Theme.surfaceRaised
            border.color: search.activeFocus ? Theme.accentPrimary : Theme.borderSubtle
            TextInput {
                id: search
                anchors.fill: parent
                anchors.leftMargin: Theme.spacing[3]
                anchors.rightMargin: Theme.spacing[3]
                verticalAlignment: TextInput.AlignVCenter
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTypeLabelSize
                selectByMouse: true
                onTextChanged: root.lib.search = text
                Text {
                    visible: !search.text && !search.activeFocus
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Search Patches")
                    color: Theme.textDisabled
                    font: search.font
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.project.inspector.hasTrack
            spacing: 0
            ListView {
                id: categories
                Layout.preferredWidth: root.width * 0.42
                Layout.fillHeight: true
                clip: true
                model: root.lib.categories
                delegate: Rectangle {
                    required property string modelData
                    width: ListView.view.width
                    height: 22
                    color: modelData === root.lib.category && root.lib.search === "" ? Theme.surfaceRaisedHover : "transparent"
                    Text {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.spacing[3]
                        verticalAlignment: Text.AlignVCenter
                        text: modelData
                        elide: Text.ElideRight
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontTypeLabelSize
                    }
                    MouseArea { anchors.fill: parent; onClicked: { search.text = ""; root.lib.category = modelData } }
                }
            }
            Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: Theme.borderSubtle }
            ListView {
                id: patches
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: root.lib.patches
                delegate: Rectangle {
                    id: row
                    required property var modelData
                    signal clicked()
                    width: ListView.view.width
                    height: 22
                    color: modelData.id === root.lib.currentPatchId ? Theme.accentPrimary : (area.containsMouse ? Theme.surfaceRaisedHover : "transparent")
                    onClicked: { root.forceActiveFocus(); root.project.applyPatch(modelData.id) }
                    Text {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.spacing[3]
                        verticalAlignment: Text.AlignVCenter
                        text: row.modelData.name
                        elide: Text.ElideRight
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontTypeLabelSize
                    }
                    MouseArea { id: area; anchors.fill: parent; hoverEnabled: true; onClicked: row.clicked() }
                }
                Text {
                    anchors.centerIn: parent
                    visible: patches.count === 0
                    text: root.lib.problems.length > 0 ? qsTr("Catalogue problems: %1").arg(root.lib.problems.length) : qsTr("No patches match")
                    color: Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontTypeLabelSize
                }
            }
        }
        Item { Layout.fillHeight: true; visible: !root.project.inspector.hasTrack }
        Text {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacing[3]
            text: qsTr("Built-in patches: %1").arg(root.lib.patchCount)
            color: Theme.textDisabled
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTypeCaptionSize
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.spacing[2]
            spacing: Theme.spacing[2]
            IconButton { implicitWidth: 24; implicitHeight: 22; label: "⋯"; enabled: false }
            IconButton { id: revert; implicitWidth: 50; implicitHeight: 22; label: qsTr("Revert"); onClicked: root.project.revertPatch() }
            Item { Layout.fillWidth: true }
            IconButton { id: del; implicitWidth: 50; implicitHeight: 22; label: qsTr("Delete"); opacity: 0.5; onClicked: root.project.announceStub(qsTr("Delete Patch")) }
            IconButton { id: save; implicitWidth: 50; implicitHeight: 22; label: qsTr("Save"); opacity: 0.5; onClicked: root.project.announceStub(qsTr("Save Patch")) }
        }
    }
}
