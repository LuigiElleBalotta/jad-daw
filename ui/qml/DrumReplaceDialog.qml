import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import Jad

// Track > Replace or Double Drum Track: the hits of the selected audio regions get a drum sound, instead of the original (Replace) or with it (Double).
Dialog {
    id: root
    required property ProjectController project
    modal: true
    anchors.centerIn: parent
    width: 460
    property string mode: "replace"
    property string sound: "kick"
    property url file
    property real mix: 100
    property real levelDb: 0
    property real velocity: 70
    property real sensitivity: 50
    readonly property var sounds: [{ id: "kick", label: qsTr("Kick") }, { id: "snare", label: qsTr("Snare") }, { id: "tom", label: qsTr("Tom") },
                                   { id: "hihat", label: qsTr("Hi-Hat") }, { id: "clap", label: qsTr("Clap") }, { id: "file", label: qsTr("Audio file…") }]
    background: Rectangle { color: Theme.surfacePanel; border.color: Theme.borderStrong; radius: Theme.radiusDialog }
    header: Item { height: 0 }

    FileDialog {
        id: fileDialog
        title: qsTr("Choose the drum sound")
        nameFilters: [qsTr("Audio (*.wav *.aif *.aiff *.flac)")]
        onAccepted: { root.file = selectedFile; root.sound = "file" }
    }
    function apply() {
        project.drumReplace({ mode: mode, sound: sound, file: String(file), mix: mix, levelDb: levelDb, velocity: velocity, sensitivity: sensitivity })
        close()
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacing[3]
        Text { text: qsTr("Replace or Double Drum Track"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeTitleSize; font.weight: Theme.fontTypeTitleWeight }
        RowLayout {
            spacing: Theme.spacing[2]
            Text { text: qsTr("Mode"); Layout.preferredWidth: 110; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeLabelSize }
            IconButton { objectName: "drumReplaceMode"; implicitHeight: 24; label: qsTr("Replace"); active: root.mode === "replace"; onClicked: root.mode = "replace" }
            IconButton { objectName: "drumDoubleMode"; implicitHeight: 24; label: qsTr("Double"); active: root.mode === "double"; onClicked: root.mode = "double" }
        }
        RowLayout {
            spacing: Theme.spacing[2]
            Text { text: qsTr("Sound"); Layout.preferredWidth: 110; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeLabelSize }
            SelectField {
                Layout.preferredWidth: 220
                choices: root.sounds.map(s => s.id)
                value: root.sound
                format: (c) => c === "file" && root.file.toString() !== "" ? decodeURIComponent(root.file.toString().split("/").pop()) : root.sounds.find(s => s.id === c).label
                onChosen: (c) => { if (c === "file") fileDialog.open(); else root.sound = c }
            }
        }
        Repeater {
            model: [{ key: "sensitivity", label: qsTr("Sensitivity"), from: 0, to: 100, def: 50, unit: "%" },
                    { key: "levelDb", label: qsTr("Sound level"), from: -24, to: 12, def: 0, unit: " dB" },
                    { key: "velocity", label: qsTr("Velocity response"), from: 0, to: 100, def: 70, unit: "%" },
                    { key: "mix", label: qsTr("Original removed"), from: 0, to: 100, def: 100, unit: "%" }]
            delegate: RowLayout {
                id: row
                required property var modelData
                visible: modelData.key !== "mix" || root.mode === "replace"
                spacing: Theme.spacing[2]
                Text { text: row.modelData.label; Layout.preferredWidth: 110; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeLabelSize }
                ParamSlider {
                    objectName: "drumSlider_" + row.modelData.key
                    Layout.fillWidth: true
                    from: row.modelData.from
                    to: row.modelData.to
                    defaultValue: row.modelData.def
                    value: root[row.modelData.key]
                    onMoved: (v) => root[row.modelData.key] = v
                    onReleased: (v) => root[row.modelData.key] = v
                }
                Text {
                    Layout.preferredWidth: 60
                    horizontalAlignment: Text.AlignRight
                    text: Math.round(root[row.modelData.key]) + row.modelData.unit
                    color: Theme.textValue
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontTypeLabelSize
                }
            }
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: Theme.spacing[2]
            IconButton { implicitHeight: 26; label: qsTr("Cancel"); onClicked: root.close() }
            IconButton { objectName: "applyButton"; implicitHeight: 26; label: qsTr("Apply"); active: true; fillActive: true; fillText: Theme.textPrimary; onClicked: root.apply() }
        }
    }
}
