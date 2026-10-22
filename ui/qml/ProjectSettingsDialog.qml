import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Jad

// File > Project Settings: Logic's one window with 11 tabs (General, Audio, Recording, Smart Tempo, MIDI, Score, Movie, Synchronization, Metronome,
// Tuning, Assets). The choices that are plain values are saved with the project (`projectSetting`); the ones the engine already has (pan law, count-in,
// musical grid, auto input monitoring) go to their own properties. Tabs of features this app does not have say so.
Dialog {
    id: root
    required property ProjectController project
    signal preferencesRequested()
    property int tab: 0
    readonly property var tabs: [qsTr("General"), qsTr("Audio"), qsTr("Recording"), qsTr("Smart Tempo"), qsTr("MIDI"), qsTr("Score"), qsTr("Movie"),
                                 qsTr("Synchronization"), qsTr("Metronome"), qsTr("Tuning"), qsTr("Assets")]
    readonly property var values: project.projectSettings    // re-read when the project changes
    function flag(key, fallback) { const v = values[key]; return v === undefined ? fallback : v === "1" }
    function text(key, fallback) { const v = values[key]; return v === undefined ? fallback : v }
    function setFlag(key, on) { project.setProjectSetting(key, on ? "1" : "0") }
    function showTab(index) { tab = index; open() }
    modal: true
    anchors.centerIn: parent
    width: 640
    height: Math.min(parent ? parent.height - 40 : 560, 560)
    background: Rectangle { color: Theme.surfacePanel; border.color: Theme.borderStrong; radius: Theme.radiusDialog }
    header: Item { height: 0 }
    onAboutToShow: { nameField.text = project.projectName }

    component Row2: RowLayout {
        property string label
        default property alias content: slot.data
        Layout.fillWidth: true
        spacing: Theme.spacing[3]
        Text { Layout.preferredWidth: 190; text: parent.label; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeLabelSize; wrapMode: Text.WordWrap }
        RowLayout { id: slot; Layout.fillWidth: true; spacing: Theme.spacing[2] }
    }
    component Group: Text { color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize; font.weight: Font.DemiBold; Layout.topMargin: Theme.spacing[2] }
    component Note: Text {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeCaptionSize
    }
    component Value: Text { color: Theme.textValue; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize }
    // a checkbox kept in the project: key, default and the label next to it
    component Flag: RowLayout {
        property string key
        property bool fallback: false
        property string label
        property bool enabledByEngine: true
        spacing: Theme.spacing[2]
        FlagCheck { objectName: "flag_" + parent.key; on: root.flag(parent.key, parent.fallback); onFlipped: root.setFlag(parent.key, !on) }
        Value { text: parent.label }
    }
    component Choice: SelectField {
        property string key
        property string fallback
        property var labels: ({})
        Layout.preferredWidth: 240
        value: root.text(key, fallback)
        format: (id) => labels[id] ?? id
        onChosen: (c) => root.project.setProjectSetting(key, c)
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacing[2]
        Text { text: root.project.projectName + qsTr(" - Project Settings"); elide: Text.ElideMiddle; Layout.fillWidth: true; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeTitleSize; font.weight: Theme.fontTypeTitleWeight }
        Flow {
            Layout.fillWidth: true
            spacing: Theme.spacing[1]
            Repeater {
                model: root.tabs
                delegate: IconButton {
                    required property int index
                    required property string modelData
                    objectName: "tab_" + index
                    implicitHeight: 24
                    label: modelData
                    active: root.tab === index
                    fillActive: true
                    fillText: Theme.textPrimary
                    onClicked: root.tab = index
                }
            }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderSubtle }
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.tab

            // General
            ColumnLayout {
                spacing: Theme.spacing[2]
                Row2 {
                    label: qsTr("Name")
                    Rectangle {
                        Layout.fillWidth: true; Layout.preferredHeight: 24
                        color: Theme.surfaceRaised; radius: Theme.radiusControl - 2
                        border.color: nameField.activeFocus ? Theme.accentPrimary : Theme.borderSubtle
                        TextInput {
                            id: nameField
                            objectName: "projectName"
                            anchors.fill: parent; anchors.margins: 5
                            verticalAlignment: TextInput.AlignVCenter
                            maximumLength: 128
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontTypeBodySize
                            onEditingFinished: root.project.setProjectName(text)
                        }
                    }
                }
                Row2 { label: qsTr("Folder"); Value { Layout.fillWidth: true; text: root.project.projectFolder(); elide: Text.ElideMiddle } }
                Row2 { label: qsTr("Tempo"); Value { text: qsTr("%1 BPM, %2 (change them in the LCD)").arg(root.project.bpm).arg(root.project.signatureText) } }
                Group { text: qsTr("Project Type") }
                Row2 { label: qsTr("Use musical grid"); FlagCheck { objectName: "useMusicalGrid"; on: root.project.useMusicalGrid; onFlipped: root.project.useMusicalGrid = !on } }
                Group { text: qsTr("Opening Project") }
                Flag { key: "loadOnlyNeeded"; label: qsTr("Only load plug-ins needed for playback") }
                Flag { key: "resetRecordEnable"; fallback: true; label: qsTr("Reset record enable state of Software Instrument tracks") }
                Group { text: qsTr("Automation") }
                Flag { key: "regionAutomationPriority"; fallback: true; label: qsTr("Region automation takes priority over track automation") }
                Flag { key: "presetValueForRegions"; label: qsTr("Use preset parameter value for regions without region automation") }
                Item { Layout.fillHeight: true }
            }

            // Audio
            ColumnLayout {
                spacing: Theme.spacing[2]
                Row2 { label: qsTr("Sample rate"); Value { text: qsTr("%1 Hz (set when the project was made)").arg(root.project.sampleRateHz) } }
                Row2 {
                    label: qsTr("Pan Law")
                    SelectField {
                        objectName: "panLawField"
                        Layout.preferredWidth: 240
                        choices: ["0db", "minus3", "minus3comp", "minus45comp", "minus6", "minus6comp"]
                        value: root.project.panLaw
                        format: (id) => ({ "0db": qsTr("0 dB (balanced)"), "minus3": qsTr("-3 dB"), "minus3comp": qsTr("-3 dB Compensated"), "minus45comp": qsTr("-4.5 dB Compensated"),
                                           "minus6": qsTr("-6 dB"), "minus6comp": qsTr("-6 dB Compensated") })[id]
                        onChosen: (c) => root.project.panLaw = c
                    }
                }
                Flag { key: "balancerPanLaw"; label: qsTr("Apply Pan Law compensation to stereo balancers") }
                Note { text: qsTr("The pan law sets how much quieter a source is at the centre than at one side. A Compensated law raises the whole mix so that a source panned to a side keeps its level.") }
                Group { text: qsTr("Spatial Audio and Surround") }
                Note { text: qsTr("The mix is stereo: Spatial Audio and surround formats are not part of this app.") }
                Item { Layout.fillHeight: true }
            }

            // Recording
            ColumnLayout {
                spacing: Theme.spacing[2]
                Row2 {
                    label: qsTr("When beginning")
                    Choice { objectName: "recordStart"; key: "recordStart"; fallback: "countin"; choices: ["countin", "preroll"]; labels: ({ "countin": qsTr("Count-in"), "preroll": qsTr("Pre-roll") }) }
                }
                Row2 {
                    label: qsTr("Count-in")
                    Value { text: root.project.countInEnabled ? qsTr("On, %1").arg(root.project.countInChoice > 0 ? qsTr("%1 bar(s)").arg(root.project.countInChoice) : qsTr("%1/4").arg(-root.project.countInChoice)) : qsTr("Off") }
                }
                Row2 { label: qsTr("Pre-roll bars"); NumberField { objectName: "preRollBars"; implicitWidth: 80; decimals: 0; from: 1; to: 16; value: parseInt(root.text("preRollBars", "2")); onCommitted: (v) => root.project.setProjectSetting("preRollBars", String(Math.round(v))) } }
                Flag { key: "allowTempoRecording"; label: qsTr("Allow tempo change recording") }
                Flag { key: "colorizeTakes"; fallback: true; label: qsTr("Automatically colorize takes") }
                Row2 { label: qsTr("Monitoring"); FlagCheck { objectName: "autoInputMonitoring"; on: root.project.autoInputMonitoring; onFlipped: root.project.autoInputMonitoring = !on }
                       Value { text: qsTr("Auto input monitoring") } }
                Group { text: qsTr("MIDI recording") }
                Flag { key: "midiDataReduction"; label: qsTr("MIDI data reduction") }
                Flag { key: "eraseDuplicates"; label: qsTr("Erase duplicate notes and controller data") }
                Group { text: qsTr("Audio Recording Path") }
                Value { Layout.fillWidth: true; text: root.project.projectFolder() + "/Audio"; elide: Text.ElideMiddle }
                Item { Layout.fillHeight: true }
            }

            // Smart Tempo
            ColumnLayout {
                spacing: Theme.spacing[2]
                Row2 {
                    label: qsTr("Project Tempo Mode")
                    Choice { objectName: "tempoMode"; key: "tempoMode"; fallback: "adapt"; choices: ["keep", "adapt", "automatic"]; labels: ({ "keep": qsTr("Keep"), "adapt": qsTr("Adapt"), "automatic": qsTr("Automatic") }) }
                }
                Group { text: qsTr("Flex & Follow") }
                Row2 {
                    label: qsTr("New recordings")
                    Choice { key: "followNewRecordings"; fallback: "off"; choices: ["off", "follow"]; labels: ({ "off": qsTr("Off"), "follow": qsTr("Flex & Follow") }) }
                }
                Flag { key: "trimNewRecordings"; fallback: true; label: qsTr("Trim start of new regions") }
                Row2 {
                    label: qsTr("Imported files")
                    Choice { key: "followImports"; fallback: "off"; choices: ["off", "follow"]; labels: ({ "off": qsTr("Off"), "follow": qsTr("Flex & Follow") }) }
                }
                Flag { key: "trimImports"; fallback: true; label: qsTr("Trim start of new regions") }
                Group { text: qsTr("Free Tempo Recording") }
                Row2 {
                    label: qsTr("Default tempo behavior")
                    Choice { key: "freeTempoBehavior"; fallback: "ask"; choices: ["ask", "recorded", "average", "keep"]
                             labels: ({ "ask": qsTr("Ask"), "recorded": qsTr("Apply recorded tempo"), "average": qsTr("Apply average tempo"), "keep": qsTr("Keep project tempo") }) }
                }
                Note { text: qsTr("Keep: the project tempo is never changed by a recording. Adapt: a recording's tempo is followed where it differs. Automatic: the tempo of the first recording sets the project tempo.") }
                Item { Layout.fillHeight: true }
            }

            // MIDI
            ColumnLayout {
                spacing: Theme.spacing[2]
                Group { text: qsTr("General") }
                Flag { key: "midiSendAfterLoad"; label: qsTr("Send settings to the MIDI devices after loading the project") }
                Flag { key: "midiThru"; fallback: true; label: qsTr("MIDI Thru: pass the incoming notes to the selected track") }
                Note { text: qsTr("The input filter, chase and clip length pages of Logic are about older MIDI hardware and are not offered.") }
                Item { Layout.fillHeight: true }
            }

            // Score
            ColumnLayout {
                spacing: Theme.spacing[2]
                Note { text: qsTr("The Score shows the notes of the selected MIDI regions and is not an engraving editor, so the notation settings of Logic (margins, spacing, beaming, symbol fonts, chord grids) are not offered.") }
                Item { Layout.fillHeight: true }
            }

            // Movie
            ColumnLayout {
                spacing: Theme.spacing[2]
                Note { text: qsTr("Video is not part of this app: there are no movie settings.") }
                Item { Layout.fillHeight: true }
            }

            // Synchronization
            ColumnLayout {
                spacing: Theme.spacing[2]
                Row2 { label: qsTr("Sync mode"); Value { text: qsTr("Internal") } }
                Note { text: qsTr("The project always runs on its own clock; chasing external timecode (MTC, SMPTE) is not offered.") }
                Item { Layout.fillHeight: true }
            }

            // Metronome
            Flickable {
                clip: true
                contentWidth: width
                contentHeight: metronomePane.implicitHeight
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                MetronomeSettings { id: metronomePane; width: parent.width - 12; project: root.project }
            }

            // Tuning
            ColumnLayout {
                spacing: Theme.spacing[2]
                Row2 {
                    label: qsTr("Software instruments tuning")
                    NumberField { objectName: "tuningCents"; implicitWidth: 80; decimals: 0; from: -100; to: 100; suffix: qsTr(" ct"); value: parseInt(root.text("tuningCents", "0")); onCommitted: (v) => root.project.setProjectSetting("tuningCents", String(Math.round(v))) }
                }
                Row2 {
                    label: qsTr("Tuning system")
                    Choice { key: "tuningSystem"; fallback: "equal"; choices: ["equal"]; labels: ({ "equal": qsTr("Equal tempered") }) }
                }
                Note { text: qsTr("Microtonal tables and Hermode tuning are not offered.") }
                Item { Layout.fillHeight: true }
            }

            // Assets
            ColumnLayout {
                spacing: Theme.spacing[2]
                Group { text: qsTr("Project Package: copy into the project folder") }
                Flag { key: "copyAudioFiles"; fallback: true; label: qsTr("Audio files") }
                Flag { key: "copySamplerAudio"; fallback: true; label: qsTr("Sampler instruments audio data") }
                Flag { key: "copyImpulseResponses"; fallback: true; label: qsTr("Impulse responses of convolution reverbs") }
                Flag { key: "copyMovie"; label: qsTr("Movie file") }
                Flag { key: "copySoundLibrary"; label: qsTr("Sound Library content") }
                Note { text: qsTr("Imported audio is always copied into the project today; the choices are saved with the project for when other kinds of files are taken in.") }
                Item { Layout.fillHeight: true }
            }
        }
        RowLayout {
            spacing: Theme.spacing[2]
            IconButton { implicitHeight: 24; label: qsTr("Audio and MIDI Preferences…"); onClicked: { root.close(); root.preferencesRequested() } }
            Item { Layout.fillWidth: true }
            IconButton { implicitHeight: 26; label: qsTr("Close"); onClicked: root.close() }
        }
    }
}
