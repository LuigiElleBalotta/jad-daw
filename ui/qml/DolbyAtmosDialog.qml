import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import Jad

// Mix > Dolby Atmos: Spatial Audio mode of the project. Tracks are objects (they have a 3D Object Panner and are placed in the room) or part of the bed; the objects are
// heard binaurally over headphones or as a stereo pan; the master is exported as an ADM BWF file, and a project can be made from such a file.
Dialog {
    id: root
    required property ProjectController project
    property var tracks: []
    readonly property bool on: project.projectSettings.spatialAudio === "dolbyAtmos"
    readonly property string monitor: project.projectSettings.atmosMonitor === "stereo" ? "stereo" : "binaural"
    modal: false
    anchors.centerIn: parent
    width: 480
    height: Math.min(parent ? parent.height - 40 : 560, 330 + Math.min(tracks.length, 9) * 30)
    function refresh() { tracks = project.atmosTracks() }
    onAboutToShow: refresh()
    Connections { target: root.project; function onProjectChanged() { if (root.visible) root.refresh() } }
    background: Rectangle { color: Theme.surfacePanel; border.color: Theme.borderStrong; radius: Theme.radiusDialog }
    header: Item { height: 0 }
    FileDialog {
        id: exportDialog
        title: qsTr("Export Spatial Audio File as")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("Dolby Atmos ADM BWF (*.wav)")]
        defaultSuffix: "wav"
        onAccepted: root.project.exportAdmBwf(selectedFile, { range: rangeField.value === qsTr("Cycle range") ? "cycle" : "project", bitDepth: 24 })
    }
    contentItem: ColumnLayout {
        spacing: Theme.spacing[3]
        Text { text: qsTr("Dolby Atmos"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeTitleSize; font.weight: Theme.fontTypeTitleWeight }
        ThemedCheckBox { objectName: "atmosOn"; text: qsTr("Spatial Audio with Dolby Atmos"); checked: root.on; onToggled: root.project.setAtmosMode(checked) }
        RowLayout {
            Layout.fillWidth: true
            enabled: root.on
            Text { text: qsTr("Monitoring Format"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize }
            Item { Layout.fillWidth: true }
            SelectField {
                objectName: "atmosMonitor"
                implicitWidth: 230
                choices: [qsTr("Binaural (headphones)"), qsTr("2.0 Direct")]
                value: root.monitor === "stereo" ? qsTr("2.0 Direct") : qsTr("Binaural (headphones)")
                onChosen: (choice) => root.project.setAtmosMonitor(choice === qsTr("2.0 Direct") ? "stereo" : "binaural")
            }
        }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: qsTr("An object track has its own place in the room (open the 3D Object Panner in its inserts); bed tracks are mixed together. A master holds at most 118 objects and a 7.1.2 bed; this app plays the bed in stereo.")
            color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeCaptionSize
        }
        ListView {
            objectName: "atmosTracks"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            enabled: root.on
            model: root.tracks
            delegate: RowLayout {
                required property var modelData
                width: ListView.view.width
                height: 30
                Text { Layout.fillWidth: true; text: modelData.name; elide: Text.ElideRight; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize }
                Text { text: modelData.object ? qsTr("Object") : qsTr("Bed"); color: modelData.object ? Theme.statePlay : Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize }
                IconButton {
                    implicitHeight: 24
                    label: modelData.object ? qsTr("Make Bed") : qsTr("Make Object")
                    onClicked: root.project.setAtmosObject(modelData.id, !modelData.object)
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            enabled: root.on
            Text { text: qsTr("Export"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize }
            SelectField { id: rangeField; implicitWidth: 120; choices: [qsTr("Project"), qsTr("Cycle range")]; value: qsTr("Project"); onChosen: (c) => value = c }
            IconButton { objectName: "atmosExport"; implicitHeight: 26; label: qsTr("Export ADM BWF…"); onClicked: exportDialog.open() }
            Item { Layout.fillWidth: true }
            IconButton { implicitHeight: 26; label: qsTr("Close"); onClicked: root.close() }
        }
    }
}
