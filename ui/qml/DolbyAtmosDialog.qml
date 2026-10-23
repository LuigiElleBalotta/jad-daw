import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import Jad

// Mix > Dolby Atmos: Spatial Audio mode of the project. Tracks are objects (they have a 3D Object Panner and are placed in the room) or part of the bed; the Monitoring Format says
// what the mix is rendered to (headphones, stereo, or 5.1 up to 7.1.4 speakers on the outputs of the interface); Downmix & Trim shapes the folding to the smaller layouts; the master
// is exported as an ADM BWF file, or bounced to the channels of the Monitoring Format; and a project can be made from an ADM BWF file.
Dialog {
    id: root
    required property ProjectController project
    property var tracks: []
    property var settings: ({ format: 1, downmix51: 0, downmix20: 0, trims: [] })
    property int layout: 3
    readonly property bool on: project.projectSettings.spatialAudio === "dolbyAtmos"
    readonly property string monitor: settings.format === 0 ? "stereo" : "binaural"
    readonly property var formatNames: [qsTr("Stereo pan"), qsTr("Binaural (headphones)"), qsTr("2.0 Direct"), qsTr("2.0 from 5.1 Downmix"), qsTr("5.1"), qsTr("7.1"), qsTr("5.1.2"), qsTr("5.1.4"), qsTr("7.1.4")]
    readonly property var layoutNames: [qsTr("2.0 Direct"), qsTr("5.1 & 2.0"), qsTr("5.1.2"), qsTr("5.1.4"), qsTr("7.1")]
    readonly property var downmix51Names: [qsTr("Lo/Ro - default"), qsTr("Dolby Pro Logic IIx"), qsTr("Direct render with room balance"), qsTr("Direct render")]
    readonly property var downmix20Names: [qsTr("Lo/Ro - default"), qsTr("Lt/Rt (Dolby Pro Logic II)"), qsTr("Lt/Rt (Dolby Pro Logic II) w/ Phase 90")]
    readonly property var trim: settings.trims !== undefined && settings.trims.length > layout ? settings.trims[layout] : ({ manual: false, surround: 0, height: 0, overhead: 0, plane: 0 })
    modal: false
    anchors.centerIn: parent
    width: 560
    height: Math.min(parent ? parent.height - 40 : 720, 620)
    function refresh() { tracks = project.atmosTracks(); settings = project.atmosSettings() }
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
    FileDialog {
        id: bounceDialog
        title: qsTr("Bounce Spatial Audio as")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("Wave file (*.wav)")]
        defaultSuffix: "wav"
        onAccepted: root.project.bounceAtmos(selectedFile)
    }
    component Slider2: RowLayout {
        id: sl
        property string label
        property string key
        property real from: -24
        property real to: 0
        property string unit: " dB"
        spacing: Theme.spacing[2]
        Text { text: sl.label; Layout.preferredWidth: 110; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeCaptionSize }
        Slider {
            objectName: "trim_" + sl.key
            Layout.fillWidth: true
            from: sl.from; to: sl.to
            enabled: root.trim.manual
            value: root.trim[sl.key] !== undefined ? root.trim[sl.key] : 0
            onMoved: root.project.setAtmosTrim(root.layout, sl.key, value)
        }
        Text { text: (root.trim[sl.key] !== undefined ? root.trim[sl.key] : 0).toFixed(1) + sl.unit; Layout.preferredWidth: 60; horizontalAlignment: Text.AlignRight; color: Theme.textValue; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeCaptionSize }
    }
    contentItem: ColumnLayout {
        spacing: Theme.spacing[2]
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
                choices: [0, 1, 2, 3, 4, 5, 6, 7, 8]
                format: (i) => root.formatNames[i]
                value: root.settings.format
                onChosen: (i) => root.project.setAtmosFormat(i)
            }
        }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: root.settings.format >= 4 ? qsTr("The speakers play on the outputs of the audio interface: L R on 1-2, then C and LFE on 3-4, the surrounds on 5-6 and so on; the bed plays in L and R. Set the outputs in Preferences > Audio.")
                                            : qsTr("An object track has its own place in the room (open the 3D Object Panner in its inserts); bed tracks are mixed together. A master holds at most 118 objects and a 7.1.2 bed.")
            color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeCaptionSize
        }
        ListView {
            objectName: "atmosTracks"
            Layout.fillWidth: true
            Layout.preferredHeight: 110
            clip: true
            enabled: root.on
            model: root.tracks
            delegate: RowLayout {
                required property var modelData
                width: ListView.view.width
                height: 28
                Text { Layout.fillWidth: true; text: modelData.name; elide: Text.ElideRight; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize }
                Text { text: modelData.object ? qsTr("Object") : qsTr("Bed"); color: modelData.object ? Theme.statePlay : Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize }
                IconButton {
                    implicitHeight: 22
                    label: modelData.object ? qsTr("Make Bed") : qsTr("Make Object")
                    onClicked: root.project.setAtmosObject(modelData.id, !modelData.object)
                }
            }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderSubtle }
        Text { text: qsTr("Downmix & Trim"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize; font.weight: Font.DemiBold }
        ColumnLayout {
            Layout.fillWidth: true
            enabled: root.on
            spacing: 4
            RowLayout {
                Text { text: qsTr("5.1 and 5.1.x"); Layout.preferredWidth: 110; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeCaptionSize }
                SelectField {
                    objectName: "downmix51"
                    implicitWidth: 250; implicitHeight: 22
                    choices: [0, 1, 2, 3]
                    format: (i) => root.downmix51Names[i]
                    value: root.settings.downmix51
                    onChosen: (i) => root.project.setAtmosDownmix("to51", i)
                }
            }
            RowLayout {
                Text { text: qsTr("5.1 to 2.0"); Layout.preferredWidth: 110; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeCaptionSize }
                SelectField {
                    objectName: "downmix20"
                    implicitWidth: 250; implicitHeight: 22
                    choices: [0, 1, 2]
                    format: (i) => root.downmix20Names[i]
                    value: root.settings.downmix20
                    onChosen: (i) => root.project.setAtmosDownmix("to20", i)
                }
            }
            RowLayout {
                spacing: 4
                Repeater {
                    model: root.layoutNames
                    delegate: IconButton {
                        required property int index
                        required property string modelData
                        implicitHeight: 22
                        label: modelData
                        active: root.layout === index
                        onClicked: root.layout = index
                    }
                }
                Item { Layout.fillWidth: true }
                Text { text: qsTr("Mode"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeCaptionSize }
                SelectField {
                    objectName: "trimMode"
                    implicitWidth: 100; implicitHeight: 22
                    choices: ["auto", "manual"]
                    format: (k) => k === "auto" ? qsTr("Automatic") : qsTr("Manual")
                    value: root.trim.manual ? "manual" : "auto"
                    onChosen: (k) => root.project.setAtmosTrimManual(root.layout, k === "manual")
                }
            }
            Slider2 { Layout.fillWidth: true; label: qsTr("Surround"); key: "surround" }
            Slider2 { Layout.fillWidth: true; label: qsTr("Height"); key: "height" }
            Slider2 { Layout.fillWidth: true; label: qsTr("Overhead balance"); key: "overhead"; from: -100; to: 100; unit: " %" }
            Slider2 { Layout.fillWidth: true; label: qsTr("Listener plane balance"); key: "plane"; from: -100; to: 100; unit: " %" }
        }
        Item { Layout.fillHeight: true }
        RowLayout {
            Layout.fillWidth: true
            enabled: root.on
            Text { text: qsTr("Export"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize }
            SelectField { id: rangeField; implicitWidth: 110; choices: [qsTr("Project"), qsTr("Cycle range")]; value: qsTr("Project"); onChosen: (c) => value = c }
            IconButton { objectName: "atmosExport"; implicitHeight: 26; label: qsTr("Export ADM BWF…"); onClicked: exportDialog.open() }
            IconButton { objectName: "atmosBounce"; implicitHeight: 26; label: qsTr("Bounce to Format…"); onClicked: bounceDialog.open() }
            Item { Layout.fillWidth: true }
            IconButton { implicitHeight: 26; label: qsTr("Close"); onClicked: root.close() }
        }
    }
}
