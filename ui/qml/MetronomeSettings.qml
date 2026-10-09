import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import Jad

// Record > Metronome Settings: how a bar is counted and the sound of every count. A slot without a file plays the built-in click.
// The files are the user's own WAV files (a spoken count, a cowbell, ...): nothing is shipped with the app.
Dialog {
    id: root
    required property ProjectController project
    modal: true
    title: qsTr("Metronome Settings")
    anchors.centerIn: parent
    width: 520
    height: Math.min(parent ? parent.height - 40 : 600, 640)

    readonly property var modes: [
        { id: "beats", label: qsTr("Beats"), hint: "1  2  3  4" },
        { id: "eighths", label: qsTr("Eighths"), hint: "1 &  2 &" },
        { id: "sixteenths", label: qsTr("Sixteenths"), hint: "1 e & a  2 e & a" },
        { id: "grouped", label: qsTr("Grouped"), hint: "1 la li  2 la li" }
    ]
    // the slots shown: the beat numbers up to the longest bar in use, then the subdivisions
    readonly property var slots: {
        const list = []
        for (let i = 1; i <= 12; ++i) list.push(i)
        for (const s of [33, 34, 35, 36, 37]) list.push(s)
        return list
    }

    background: Rectangle {
        color: Theme.surfacePanel
        border.color: Theme.borderStrong
        radius: Theme.radiusDialog
    }
    header: Item { height: 0 }
    standardButtons: Dialog.Close

    FileDialog {
        id: sampleDialog
        property int slot: 1
        title: qsTr("Choose a WAV file for this count")
        nameFilters: [qsTr("WAV audio (*.wav)")]
        onAccepted: root.project.setClickSlotFile(slot, selectedFile)
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacing[3]
        Text {
            text: qsTr("Metronome Settings")
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTypeTitleSize
            font.weight: Theme.fontTypeTitleWeight
        }
        Text {
            text: qsTr("Counting")
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTypeLabelSize
        }
        Row {
            spacing: Theme.spacing[1]
            Repeater {
                model: root.modes
                delegate: IconButton {
                    required property var modelData
                    implicitHeight: 24
                    label: modelData.label
                    active: root.project.clickMode === modelData.id
                    fillActive: true
                    fillText: Theme.textPrimary
                    onClicked: root.project.clickMode = modelData.id
                }
            }
        }
        Text {
            text: {
                for (const m of root.modes) if (m.id === root.project.clickMode) return m.hint
                return ""
            }
            color: Theme.textValue
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTypeBodySize
        }
        RowLayout {
            visible: root.project.clickMode === "grouped"
            spacing: Theme.spacing[2]
            Text { text: qsTr("Groups"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeLabelSize }
            Rectangle {
                Layout.preferredWidth: 140
                Layout.preferredHeight: 22
                color: Theme.surfaceRaised
                radius: Theme.radiusControl - 2
                border.color: groupField.activeFocus ? Theme.accentPrimary : Theme.borderSubtle
                TextInput {
                    id: groupField
                    anchors.fill: parent
                    anchors.leftMargin: 6
                    anchors.rightMargin: 6
                    verticalAlignment: TextInput.AlignVCenter
                    text: root.project.clickGrouping
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontTypeBodySize
                    inputMask: ""
                    onEditingFinished: root.project.clickGrouping = text
                }
            }
            Text {
                text: qsTr("for example 3+2+2 in 7/8; empty: groups of three in 6/8, 9/8 and 12/8")
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTypeCaptionSize
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
        }
        Text {
            text: qsTr("Sounds: each count can play a WAV file of your own (a voice saying the number, a cowbell...). Empty counts use the built-in click.")
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTypeCaptionSize
        }
        ListView {
            id: table
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.slots
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            delegate: Item {
                id: row
                required property int modelData
                readonly property string file: { root.project.clickRevision; return root.project.clickSlotFile(modelData) }
                width: table.width - 10
                height: 28
                RowLayout {
                    anchors.fill: parent
                    spacing: Theme.spacing[2]
                    Text {
                        Layout.preferredWidth: 36
                        text: root.project.clickSlotName(row.modelData)
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontTypeBodySize
                        font.weight: Theme.fontTypeLabelWeight
                    }
                    Text {
                        Layout.fillWidth: true
                        text: row.file === "" ? qsTr("built-in click") : row.file.split(/[\\/]/).pop()
                        color: row.file === "" ? Theme.textSecondary : Theme.textValue
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontTypeLabelSize
                        elide: Text.ElideMiddle
                    }
                    Slider {
                        Layout.preferredWidth: 70
                        from: 0; to: 2
                        enabled: row.file !== ""
                        value: { root.project.clickRevision; return root.project.clickSlotGain(row.modelData) }
                        onMoved: root.project.setClickSlotGain(row.modelData, value)
                    }
                    IconButton {
                        implicitHeight: 22
                        label: qsTr("Choose…")
                        onClicked: { sampleDialog.slot = row.modelData; sampleDialog.open() }
                    }
                    IconButton {
                        implicitHeight: 22
                        label: qsTr("Clear")
                        enabled: row.file !== ""
                        onClicked: root.project.clearClickSlot(row.modelData)
                    }
                }
            }
        }
    }
}
