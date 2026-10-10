import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import Jad

// File > Bounce: the file format, the range, normalizing and the tail; Bounce asks for the file name and renders offline.
Dialog {
    id: root
    required property ProjectController project
    modal: true
    anchors.centerIn: parent
    width: 460
    property string format: "wav24"
    property string range: "project"
    property bool normalizeOn: false
    property real tail: 0.5
    readonly property var formats: [{ id: "wav16", label: "WAV 16 bit" }, { id: "wav24", label: "WAV 24 bit" }, { id: "wav32", label: "WAV 32 bit float" },
                                    { id: "aiff16", label: "AIFF 16 bit" }, { id: "aiff24", label: "AIFF 24 bit" }]
    background: Rectangle { color: Theme.surfacePanel; border.color: Theme.borderStrong; radius: Theme.radiusDialog }
    header: Item { height: 0 }

    FileDialog {
        id: fileDialog
        title: qsTr("Bounce the project")
        fileMode: FileDialog.SaveFile
        defaultSuffix: root.format.startsWith("aiff") ? "aif" : "wav"
        nameFilters: [root.format.startsWith("aiff") ? qsTr("AIFF audio (*.aif *.aiff)") : qsTr("WAV audio (*.wav)")]
        onAccepted: root.project.bounceProjectAs(selectedFile, { format: root.format, range: root.range, normalize: root.normalizeOn, tail: root.tail })
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacing[3]
        Text { text: qsTr("Bounce"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeTitleSize; font.weight: Theme.fontTypeTitleWeight }
        GridLayout {
            columns: 2
            columnSpacing: Theme.spacing[3]
            rowSpacing: Theme.spacing[2]
            Text { text: qsTr("Format"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeLabelSize }
            SelectField {
                objectName: "formatField"
                Layout.preferredWidth: 200
                choices: root.formats.map(f => f.id)
                value: root.format
                format: (id) => root.formats.find(f => f.id === id).label
                onChosen: (c) => root.format = c
            }
            Text { text: qsTr("Range"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeLabelSize }
            Row {
                spacing: Theme.spacing[1]
                IconButton { implicitHeight: 24; label: qsTr("Whole project"); active: root.range === "project"; fillActive: true; fillText: Theme.textPrimary; onClicked: root.range = "project" }
                IconButton { implicitHeight: 24; label: qsTr("Cycle area"); active: root.range === "cycle"; fillActive: true; fillText: Theme.textPrimary; onClicked: root.range = "cycle" }
            }
            Text { text: qsTr("Normalize"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeLabelSize }
            IconButton { implicitHeight: 24; implicitWidth: 120; label: root.normalizeOn ? qsTr("On (-0.3 dBFS)") : qsTr("Off"); active: root.normalizeOn; fillActive: true; fillText: Theme.textPrimary; onClicked: root.normalizeOn = !root.normalizeOn }
            Text { text: qsTr("Tail"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeLabelSize }
            Row {
                spacing: Theme.spacing[2]
                ParamSlider { width: 140; from: 0; to: 10; defaultValue: 0.5; value: root.tail; onMoved: (v) => root.tail = v }
                Text { anchors.verticalCenter: parent.verticalCenter; text: qsTr("%1 s").arg(root.tail.toFixed(1)); color: Theme.textValue; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize }
            }
        }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: qsTr("The render runs offline, without the audio device. 16-bit files are dithered. Plug-in inserts (VST3) are skipped; the built-in effects are included.")
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
