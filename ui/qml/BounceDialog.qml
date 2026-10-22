import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import Jad

// File > Bounce > Project or Section…: Logic's dialog. Destination is a checklist (Uncompressed is what is made here), the file type and bit depth,
// the sample rate of the file, an explicit start and end in bars, Normalize, the tail, the second cycle pass and Add to Project.
Dialog {
    id: root
    required property ProjectController project
    modal: true
    anchors.centerIn: parent
    width: 520
    property string format: "wav24"
    property string rangeKind: "project"     // project, cycle or bars
    property int startBar: 1
    property int endBar: 2
    property string normalizeMode: "off"
    property real tail: 0.5
    property int sampleRate: 0               // 0: the project's
    property bool secondPass: false
    property bool addToProject: false
    property bool destPcm: true
    property bool destMp3: false
    property bool destM4a: false
    readonly property bool mp3Available: project.compressedBounceAvailable("mp3")
    readonly property bool m4aAvailable: project.compressedBounceAvailable("m4a")
    readonly property var destinations: { const d = []; if (destPcm) d.push("uncompressed"); if (destMp3) d.push("mp3"); if (destM4a) d.push("m4a"); return d }
    readonly property var formats: [{ id: "wav16", label: "WAVE 16 bit" }, { id: "wav24", label: "WAVE 24 bit" }, { id: "wav32", label: "WAVE 32 bit float" },
                                    { id: "aiff16", label: "AIFF 16 bit" }, { id: "aiff24", label: "AIFF 24 bit" },
                                    { id: "caf16", label: "CAF 16 bit" }, { id: "caf24", label: "CAF 24 bit" }, { id: "caf32", label: "CAF 32 bit float" },
                                    { id: "flac16", label: "FLAC 16 bit" }, { id: "flac24", label: "FLAC 24 bit" }]
    readonly property var rates: [0, 44100, 48000, 88200, 96000, 176400, 192000]
    // what the dialog proposes: the whole project, or the cycle area, as bars
    function fillRange() {
        if (rangeKind === "project") {
            startBar = 1
            endBar = project.beatsToBar(project.projectEndBeats()) + 1
        } else if (rangeKind === "cycle") {
            startBar = project.beatsToBar(project.loopStartBeats)
            endBar = project.beatsToBar(Math.max(project.loopEndBeats - 1e-6, project.loopStartBeats)) + 1
        }
    }
    onAboutToShow: fillRange()
    background: Rectangle { color: Theme.surfacePanel; border.color: Theme.borderStrong; radius: Theme.radiusDialog }
    header: Item { height: 0 }

    FileDialog {
        id: fileDialog
        title: qsTr("Bounce the project")
        fileMode: FileDialog.SaveFile
        defaultSuffix: !root.destPcm ? (root.destMp3 ? "mp3" : "m4a") : (root.format.startsWith("aiff") ? "aif" : (root.format.startsWith("caf") ? "caf" : (root.format.startsWith("flac") ? "flac" : "wav")))
        nameFilters: [!root.destPcm ? (root.destMp3 ? qsTr("MP3 audio (*.mp3)") : qsTr("M4A audio (*.m4a)")) : (root.format.startsWith("aiff") ? qsTr("AIFF audio (*.aif *.aiff)") : (root.format.startsWith("caf") ? qsTr("CAF audio (*.caf)") : (root.format.startsWith("flac") ? qsTr("FLAC audio (*.flac)") : qsTr("WAVE audio (*.wav)"))))]
        onAccepted: {
            const o = { format: root.format, normalize: root.normalizeMode, tail: root.tail, sampleRate: root.sampleRate,
                        secondPass: root.secondPass && root.rangeKind !== "project", addToProject: root.addToProject, destinations: root.destinations }
            if (root.rangeKind === "project") o.range = "project"
            else if (root.rangeKind === "cycle") o.range = "cycle"
            else { o.range = "custom"; o.startBeats = root.project.barToBeats(root.startBar); o.endBeats = root.project.barToBeats(root.endBar) }
            root.project.bounceProjectAs(selectedFile, o)
        }
    }

    component Label: Text { color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeLabelSize }

    contentItem: ColumnLayout {
        spacing: Theme.spacing[3]
        Text { text: qsTr("Bounce \"Output 1-2\""); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeTitleSize; font.weight: Theme.fontTypeTitleWeight }
        GridLayout {
            columns: 2
            columnSpacing: Theme.spacing[3]
            rowSpacing: Theme.spacing[2]
            Label { text: qsTr("Destination") }
            ColumnLayout {
                spacing: 2
                CheckBox {
                    objectName: "destUncompressed"
                    checked: root.destPcm
                    enabled: root.destMp3 || root.destM4a   // at least one destination stays on
                    text: qsTr("Uncompressed")
                    onToggled: root.destPcm = checked
                    contentItem: Text { leftPadding: 24; text: parent.text; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize; verticalAlignment: Text.AlignVCenter }
                }
                CheckBox {
                    objectName: "destMp3"
                    checked: root.destMp3
                    enabled: root.mp3Available && (root.destPcm || root.destM4a || !root.destMp3)
                    text: root.mp3Available ? qsTr("MP3 (256 kbit/s)") : qsTr("MP3 (no encoder on this system)")
                    onToggled: root.destMp3 = checked
                    contentItem: Text { leftPadding: 24; text: parent.text; color: parent.enabled ? Theme.textPrimary : Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize; verticalAlignment: Text.AlignVCenter }
                }
                CheckBox {
                    objectName: "destM4a"
                    checked: root.destM4a
                    enabled: root.m4aAvailable && (root.destPcm || root.destMp3 || !root.destM4a)
                    text: root.m4aAvailable ? qsTr("M4A: AAC (192 kbit/s)") : qsTr("M4A: AAC (no encoder on this system)")
                    onToggled: root.destM4a = checked
                    contentItem: Text { leftPadding: 24; text: parent.text; color: parent.enabled ? Theme.textPrimary : Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize; verticalAlignment: Text.AlignVCenter }
                }
                CheckBox {
                    enabled: false
                    text: qsTr("Burn to CD/DVD (not offered)")
                    contentItem: Text { leftPadding: 24; text: parent.text; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize; verticalAlignment: Text.AlignVCenter }
                }
            }
            Label { text: qsTr("File type") }
            SelectField {
                objectName: "formatField"
                Layout.preferredWidth: 200
                choices: root.formats.map(f => f.id)
                value: root.format
                format: (id) => root.formats.find(f => f.id === id).label
                onChosen: (c) => root.format = c
            }
            Label { text: qsTr("Sample rate") }
            SelectField {
                objectName: "rateField"
                Layout.preferredWidth: 200
                choices: root.rates.map(r => String(r))
                value: String(root.sampleRate)
                format: (id) => id === "0" ? qsTr("Project rate") : qsTr("%1 Hz").arg(id)
                onChosen: (c) => root.sampleRate = parseInt(c)
            }
            Label { text: qsTr("Mode") }
            SelectField {
                objectName: "modeField"
                Layout.preferredWidth: 200
                choices: ["offline"]   // the render always runs offline, without the audio device
                value: "offline"
                format: (id) => qsTr("Offline")
                onChosen: (c) => {}
            }
            Label { text: qsTr("Range") }
            Row {
                spacing: Theme.spacing[1]
                IconButton { implicitHeight: 24; label: qsTr("Whole project"); active: root.rangeKind === "project"; fillActive: true; fillText: Theme.textPrimary; onClicked: { root.rangeKind = "project"; root.fillRange() } }
                IconButton { implicitHeight: 24; label: qsTr("Cycle area"); active: root.rangeKind === "cycle"; fillActive: true; fillText: Theme.textPrimary; onClicked: { root.rangeKind = "cycle"; root.fillRange() } }
                IconButton { implicitHeight: 24; label: qsTr("Bars"); active: root.rangeKind === "bars"; fillActive: true; fillText: Theme.textPrimary; onClicked: root.rangeKind = "bars" }
            }
            Label { text: qsTr("Start") }
            NumberField { objectName: "startBarField"; implicitWidth: 90; decimals: 0; from: 1; to: 99999; value: root.startBar; onCommitted: (v) => { root.rangeKind = "bars"; root.startBar = v; if (root.endBar <= v) root.endBar = v + 1 } }
            Label { text: qsTr("End") }
            NumberField { objectName: "endBarField"; implicitWidth: 90; decimals: 0; from: 2; to: 100000; value: root.endBar; onCommitted: (v) => { root.rangeKind = "bars"; root.endBar = Math.max(v, root.startBar + 1) } }
            Label { text: qsTr("Normalize") }
            SelectField {
                objectName: "normalizeField"
                Layout.preferredWidth: 200
                choices: ["off", "overall"]
                value: root.normalizeMode
                format: (id) => id === "off" ? qsTr("Off") : qsTr("Overall (-0.3 dBFS)")
                onChosen: (c) => root.normalizeMode = c
            }
            Label { text: qsTr("Tail") }
            Row {
                spacing: Theme.spacing[2]
                ParamSlider { width: 140; from: 0; to: 10; defaultValue: 0.5; value: root.tail; onMoved: (v) => root.tail = v }
                Text { anchors.verticalCenter: parent.verticalCenter; text: qsTr("%1 s").arg(root.tail.toFixed(1)); color: Theme.textValue; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize }
            }
        }
        CheckBox {
            objectName: "secondPassBox"
            enabled: root.rangeKind !== "project"
            checked: root.secondPass
            onToggled: root.secondPass = checked
            text: qsTr("Bounce 2nd Cycle Pass")
            contentItem: Text { leftPadding: 24; text: parent.text; color: parent.enabled ? Theme.textPrimary : Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize; verticalAlignment: Text.AlignVCenter }
        }
        CheckBox {
            objectName: "addToProjectBox"
            checked: root.addToProject
            onToggled: root.addToProject = checked
            text: qsTr("Add to Project")
            contentItem: Text { leftPadding: 24; text: parent.text; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize; verticalAlignment: Text.AlignVCenter }
        }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: qsTr("The render runs offline, without the audio device. 16-bit files are dithered. A different sample rate converts the finished mix. The 2nd cycle pass folds the tail of the range back onto its start, as a loop that plays twice.")
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTypeCaptionSize
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: Theme.spacing[2]
            IconButton { implicitHeight: 26; label: qsTr("Cancel"); onClicked: root.close() }
            IconButton { objectName: "bounceButton"; implicitHeight: 26; label: qsTr("Bounce…"); active: true; fillActive: true; fillText: Theme.textPrimary; onClicked: { root.close(); fileDialog.open() } }
        }
    }
}
