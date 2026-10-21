import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Jad

// Track > New Tracks: Logic's Create New Track window. Four categories with their kinds, a Details panel that follows the choice, the number of tracks;
// everything is created in one undo step.
Dialog {
    id: root
    required property ProjectController project
    modal: true
    anchors.centerIn: parent
    width: 820
    padding: 16
    property string category: "audio"
    property var subs: ({ midi: "software", pattern: "software", session: "drummer", audio: "mic" })
    property int count: 1
    // Details
    property string instrument: "builtin.sine"
    signal openEditorRequested()
    property int audioInput: 3            // 0 = stereo 1+2, n = input n
    property bool ascending: false
    property bool loadDefaultPatch: false
    property bool openLibrary: false
    property bool openEditor: true
    property bool inputMonitoring: true
    property bool recordEnable: true
    property string output: ""            // "" = Stereo Output
    property string midiDevice: ""
    property string style: "rock"

    readonly property var cards: [
        { id: "midi", label: qsTr("MIDI"), color: "#1fa84a", subs: [{ id: "software", label: qsTr("Software Instrument") }, { id: "external", label: qsTr("External MIDI") }] },
        { id: "pattern", label: qsTr("Pattern"), color: "#5b5bd6", subs: [{ id: "software", label: qsTr("Software Instrument") }, { id: "external", label: qsTr("External MIDI") }] },
        { id: "session", label: qsTr("Session Player"), color: "#c9a017", subs: [{ id: "drummer", label: qsTr("Drummer") }, { id: "bass", label: qsTr("Bass Player") }, { id: "keys", label: qsTr("Keyboard Player") }] },
        { id: "audio", label: qsTr("Audio"), color: "#1a73e8", subs: [{ id: "mic", label: qsTr("Mic or Line") }, { id: "guitar", label: qsTr("Guitar or Bass") }] }
    ]
    readonly property var styles: ({ drummer: ["rock", "pop", "funk", "ballad", "shuffle"], bass: ["root", "eighths", "walking", "funk"], keys: ["block", "arpeggio", "comp", "pad"] })
    readonly property string sub: subs[category]
    readonly property bool isAudio: category === "audio"
    readonly property bool isSession: category === "session"
    readonly property bool isExternal: (category === "midi" || category === "pattern") && sub === "external"
    readonly property var outputs: {
        const out = [{ id: "", name: qsTr("Stereo Output") }]
        for (const t of project.targetsFor("")) if (!String(t.id).startsWith("newOutput:") && t.name) out.push({ id: t.id, name: t.name })
        return out
    }
    readonly property var instruments: project.instrumentSpecs().filter(s => s.id !== "builtin.external").map(s => ({ id: s.id, name: s.name }))

    background: Rectangle { color: Theme.surfacePanel; border.color: Theme.borderStrong; radius: Theme.radiusDialog }
    header: Item { height: 0 }

    function choose(cardId, subId) {
        const next = Object.assign({}, subs)
        next[cardId] = subId
        subs = next
        category = cardId
        if (cardId === "audio") {
            // Mic or Line starts on Input 3 with Load Default Patch off, Guitar or Bass on Input 1 with it on (Logic's defaults)
            audioInput = subId === "mic" ? 3 : 1
            loadDefaultPatch = subId !== "mic"
            openLibrary = subId !== "mic"
            recordEnable = subId === "mic"
        } else if (cardId === "session") {
            style = styles[subId][0]
        } else if (cardId === "pattern" && instrument === "builtin.sine") {
            instrument = "builtin.drums"          // a pattern plays drums unless another instrument is chosen
        }
    }
    function create() {
        if (isSession) {
            project.newSessionPlayer({ kind: sub, style: style, chords: "Am F C G", repeats: 4, intensity: 60, swing: 0, seed: 1 })
            if (openLibrary) project.libraryVisible = true
        } else {
            project.createTracks({
                category: category, sub: sub, count: count, instrument: instrument, input: audioInput, ascending: ascending,
                output: output, monitoring: inputMonitoring, recordEnable: recordEnable, loadDefaultPatch: loadDefaultPatch, openLibrary: openLibrary, midiDevice: midiDevice
            })
        }
        if (category === "pattern" && openEditor) openEditorRequested()
        close()
    }
    onOpened: { count = 1; countField.text = "1"; choose(category, subs[category]) }

    component Check: RowLayout {
        id: check
        property string label
        property bool on
        signal flipped()
        spacing: Theme.spacing[2]
        FlagCheck { on: check.on; onFlipped: check.flipped() }
        Text { text: check.label; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize }
    }
    component Caption: Text { color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeLabelSize }

    contentItem: ColumnLayout {
        spacing: Theme.spacing[3]
        Text {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Create New Track")
            color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize; font.weight: 600
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing[2]
            Repeater {
                model: root.cards
                delegate: Rectangle {
                    id: card
                    required property var modelData
                    readonly property bool picked: root.category === modelData.id
                    Layout.fillWidth: true
                    Layout.preferredHeight: 200
                    radius: 6
                    color: Theme.surfaceRaised
                    border.width: picked ? 2 : 1
                    border.color: picked ? Theme.accentPrimary : Theme.borderSubtle
                    objectName: "newTrackCard_" + modelData.id
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 6
                        Rectangle {
                            Layout.alignment: Qt.AlignHCenter
                            width: 44; height: 44; radius: 22
                            color: card.modelData.color
                            Text { anchors.centerIn: parent; text: card.modelData.id === "midi" ? "♫" : (card.modelData.id === "pattern" ? "▦" : (card.modelData.id === "session" ? "♪" : "|||")); color: "white"; font.pixelSize: 20 }
                        }
                        Text { Layout.alignment: Qt.AlignHCenter; text: card.modelData.label; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize; font.weight: 600 }
                        Repeater {
                            model: card.modelData.subs
                            delegate: Rectangle {
                                required property var modelData
                                readonly property bool on: card.picked && root.subs[card.modelData.id] === modelData.id
                                Layout.fillWidth: true
                                Layout.preferredHeight: 24
                                radius: 3
                                color: on ? Theme.accentPrimary : (subArea.containsMouse ? Theme.surfaceRaisedHover : Theme.surfacePanel)
                                border.color: Theme.borderSubtle
                                objectName: "newTrackKind_" + card.modelData.id + "_" + modelData.id
                                Text { anchors.centerIn: parent; text: parent.modelData.label; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize }
                                MouseArea { id: subArea; anchors.fill: parent; hoverEnabled: true; onClicked: root.choose(card.modelData.id, parent.modelData.id) }
                            }
                        }
                        Item { Layout.fillHeight: true }
                    }
                }
            }
        }
        Text { text: "⌄  " + qsTr("Details"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 180
            color: Theme.surfaceApp
            radius: 4
            RowLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 24
                ColumnLayout {  // left: where the sound comes from
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    spacing: 6
                    Caption { text: root.isAudio ? qsTr("Audio Input:") : (root.isSession ? (root.sub === "drummer" ? qsTr("Drummer Style:") : (root.sub === "bass" ? qsTr("Bass Player Style:") : qsTr("Keyboard Player Style:"))) : (root.isExternal ? qsTr("MIDI Destination:") : qsTr("Instrument:"))) }
                    SelectField {
                        objectName: "newTrackMain"
                        Layout.fillWidth: true
                        choices: root.isAudio ? root.project.inputChoices().map((n, i) => i) : (root.isSession ? root.styles[root.sub] : (root.isExternal ? root.project.midiOutputNames() : root.instruments.map(s => s.id)))
                        value: root.isAudio ? root.audioInput : (root.isSession ? root.style : (root.isExternal ? root.midiDevice : root.instrument))
                        placeholder: root.isExternal ? qsTr("Off - All") : ""
                        // the Input menu: entry 0 is the stereo pair of inputs 1 and 2, then every input
                        format: (c) => root.isAudio ? root.project.inputChoices()[c] : (root.isSession || root.isExternal ? String(c) : (root.instruments.find(s => s.id === c)?.name ?? c))
                        onChosen: (c) => { if (root.isAudio) root.audioInput = c; else if (root.isSession) root.style = c; else if (root.isExternal) root.midiDevice = c; else root.instrument = c }
                    }
                    Check { visible: root.isAudio; label: qsTr("Ascending"); on: root.ascending; onFlipped: root.ascending = !root.ascending }
                    Check { visible: root.isAudio; label: qsTr("Load Default Patch"); on: root.loadDefaultPatch; onFlipped: root.loadDefaultPatch = !root.loadDefaultPatch }
                    Check { visible: !root.isExternal && !root.isAudio && !root.isSession; label: qsTr("Load Default Patch"); on: root.loadDefaultPatch; onFlipped: root.loadDefaultPatch = !root.loadDefaultPatch }
                    Check { visible: !root.isExternal; label: qsTr("Open Library"); on: root.openLibrary; onFlipped: root.openLibrary = !root.openLibrary }
                    Check { visible: root.category === "pattern"; label: qsTr("Open Editor"); on: root.openEditor; onFlipped: root.openEditor = !root.openEditor }
                }
                ColumnLayout {  // right: where it goes
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    spacing: 6
                    Caption { text: qsTr("Audio Output:") }
                    SelectField {
                        objectName: "newTrackOutput"
                        Layout.fillWidth: true
                        choices: root.outputs.map(o => o.id)
                        value: root.output
                        format: (c) => root.outputs.find(o => o.id === c)?.name ?? qsTr("Stereo Output")
                        onChosen: (c) => root.output = c
                    }
                    Check { visible: root.isAudio; label: qsTr("Input Monitoring"); on: root.inputMonitoring; onFlipped: root.inputMonitoring = !root.inputMonitoring }
                    Check { visible: root.isAudio; label: qsTr("Record Enable"); on: root.recordEnable; onFlipped: root.recordEnable = !root.recordEnable }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing[2]
            Item { Layout.fillWidth: true }
            Text { text: qsTr("Number of tracks to create:"); visible: !root.isSession; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize }
            Rectangle {
                visible: !root.isSession
                Layout.preferredWidth: 50; Layout.preferredHeight: 24
                color: Theme.surfaceRaised; radius: Theme.radiusControl - 2
                border.color: countField.activeFocus ? Theme.accentPrimary : Theme.borderSubtle
                TextInput {
                    id: countField
                    objectName: "newTrackCount"
                    anchors.fill: parent; anchors.margins: 5
                    verticalAlignment: TextInput.AlignVCenter
                    validator: IntValidator { bottom: 1; top: 64 }
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontTypeBodySize
                    onTextEdited: if (text !== "") root.count = Math.max(1, Math.min(64, parseInt(text)))
                    onAccepted: root.create()
                }
            }
            Item { Layout.fillWidth: true }
            IconButton { implicitHeight: 26; label: qsTr("Cancel"); onClicked: root.close() }
            IconButton { objectName: "newTrackCreate"; implicitHeight: 26; label: qsTr("Create"); active: true; fillActive: true; fillText: Theme.textPrimary; onClicked: root.create() }
        }
    }
}
