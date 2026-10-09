import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Jad

// Preferences: a list of sections on the left and the section on the right. Audio is the one that exists so far; the other
// sections say so.
Dialog {
    id: root
    required property ProjectController project
    modal: true
    anchors.centerIn: parent
    width: 640
    height: 420
    property int section: 1
    readonly property var sections: [qsTr("General"), qsTr("Audio"), qsTr("MIDI"), qsTr("Display"), qsTr("Advanced")]

    // the choices being edited (applied with the Apply button)
    property string output: ""
    property string input: ""
    property int buffer: 256
    property var devices: ({ outputs: [], inputs: [], rates: [], buffers: [], currentOutput: "", currentInput: "", inputChannels: 0, outputChannels: 0 })

    function refresh() { devices = project.audioDevices(output, input) }
    onAboutToShow: {
        output = project.audioOutput
        input = project.audioInput
        buffer = project.audioBufferSize
        refresh()
    }

    background: Rectangle { color: Theme.surfacePanel; border.color: Theme.borderStrong; radius: Theme.radiusDialog }
    header: Item { height: 0 }

    contentItem: RowLayout {
        spacing: 0
        Rectangle {
            Layout.preferredWidth: 150
            Layout.fillHeight: true
            color: Theme.surfaceCanvas
            Column {
                anchors.fill: parent
                anchors.topMargin: Theme.spacing[3]
                Repeater {
                    model: root.sections
                    delegate: Rectangle {
                        required property string modelData
                        required property int index
                        width: parent.width
                        height: 28
                        color: root.section === index ? Theme.accentPrimary : "transparent"
                        Text {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.spacing[3]
                            verticalAlignment: Text.AlignVCenter
                            text: parent.modelData
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontTypeBodySize
                        }
                        MouseArea { anchors.fill: parent; onClicked: root.section = parent.index }
                    }
                }
            }
        }
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            Text {  // the sections that do not exist yet
                visible: root.section !== 1
                anchors.centerIn: parent
                text: qsTr("%1 settings are not implemented yet").arg(root.sections[root.section])
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTypeBodySize
            }

            ColumnLayout {
                id: audio
                visible: root.section === 1
                anchors.fill: parent
                anchors.margins: Theme.spacing[4]
                spacing: Theme.spacing[3]
                Text {
                    text: qsTr("Audio")
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontTypeTitleSize
                    font.weight: Theme.fontTypeTitleWeight
                }
                GridLayout {
                    columns: 2
                    columnSpacing: Theme.spacing[3]
                    rowSpacing: Theme.spacing[3]
                    Text { text: qsTr("Output device"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeLabelSize }
                    SelectField {
                        objectName: "outputField"
                        Layout.preferredWidth: 300
                        choices: root.devices.outputs
                        value: root.output !== "" ? root.output : root.devices.currentOutput
                        placeholder: qsTr("No output device")
                        onChosen: (c) => { root.output = c; root.refresh() }
                    }
                    Text { text: qsTr("Input device"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeLabelSize }
                    SelectField {
                        objectName: "inputField"
                        Layout.preferredWidth: 300
                        choices: root.devices.inputs
                        value: root.input !== "" ? root.input : root.devices.currentInput
                        placeholder: qsTr("No input device")
                        onChosen: (c) => { root.input = c; root.refresh() }
                    }
                    Text { text: qsTr("I/O buffer size"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeLabelSize }
                    SelectField {
                        objectName: "bufferField"
                        Layout.preferredWidth: 140
                        choices: root.devices.buffers
                        value: root.buffer
                        format: (b) => qsTr("%1 samples").arg(b)
                        onChosen: (c) => root.buffer = c
                    }
                    Text { text: qsTr("Sample rate"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeLabelSize }
                    Text {
                        objectName: "rateText"
                        readonly property bool supported: root.devices.rates.indexOf(root.project.deviceRate > 0 ? root.project.deviceRate : 48000) >= 0 || root.devices.rates.length === 0
                        text: qsTr("The project runs at %1 Hz").arg(root.project.sampleRateHz)
                        color: Theme.textValue
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontTypeBodySize
                    }
                    Text { text: qsTr("Recording delay"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeLabelSize }
                    RowLayout {
                        spacing: Theme.spacing[2]
                        Rectangle {
                            Layout.preferredWidth: 70; Layout.preferredHeight: 24
                            color: Theme.surfaceRaised; radius: Theme.radiusControl - 2
                            border.color: delayField.activeFocus ? Theme.accentPrimary : Theme.borderSubtle
                            TextInput {
                                id: delayField
                                objectName: "delayField"
                                anchors.fill: parent; anchors.margins: 5
                                verticalAlignment: TextInput.AlignVCenter
                                validator: IntValidator { bottom: -4800; top: 48000 }
                                text: String(root.project.recordingDelay)
                                color: Theme.textPrimary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontTypeBodySize
                                onEditingFinished: root.project.recordingDelay = parseInt(text)
                            }
                        }
                        Text { text: qsTr("samples (added to the interface's own latency)"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeCaptionSize }
                    }
                    Text { text: qsTr("Latency"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeLabelSize }
                    Text {
                        objectName: "latencyText"
                        text: qsTr("%1 ms round trip, at best").arg((2 * root.buffer / root.project.sampleRateHz * 1000).toFixed(1))
                        color: Theme.textValue
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontTypeBodySize
                    }
                }
                Text {
                    objectName: "deviceStatus"
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: root.project.deviceError !== "" ? root.project.deviceError
                          : (root.project.deviceRate > 0 ? qsTr("Open: %1 Hz, %2 input channels").arg(root.project.deviceRate).arg(root.project.inputChannels) : qsTr("No project is open"))
                    color: root.project.deviceError !== "" ? Theme.stateClip : Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontTypeLabelSize
                }
                Item { Layout.fillHeight: true }
                RowLayout {
                    Layout.alignment: Qt.AlignRight
                    spacing: Theme.spacing[2]
                    IconButton { implicitHeight: 26; label: qsTr("Close"); onClicked: root.close() }
                    IconButton {
                        objectName: "applyButton"
                        implicitHeight: 26
                        label: qsTr("Apply")
                        active: true
                        fillActive: true
                        fillText: Theme.textPrimary
                        onClicked: root.project.applyAudioSettings(root.output, root.input, root.buffer)
                    }
                }
            }
        }
    }
}
