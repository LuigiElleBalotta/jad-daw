import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Jad

// Track > Convert Regions to New Sampler Track…: the selected audio regions become zones of a Sampler on a new track, one zone for every region or for every part
// of a region between two transients, and MIDI regions with the trigger notes are put where the sound was (the converted regions are muted).
Dialog {
    id: root
    required property ProjectController project
    property string source: "regions"
    property bool oneShot: false
    property bool mapEvenly: false
    property int low: 36
    property int high: 96
    modal: false
    anchors.centerIn: parent
    width: 420
    onAboutToShow: { source = "regions"; oneShot = false; mapEvenly = false; low = 36; high = 96 }
    background: Rectangle { color: Theme.surfacePanel; border.color: Theme.borderStrong; radius: Theme.radiusDialog }
    header: Item { height: 0 }
    // Logic names the middle C C3
    function noteName(n) { return ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"][n % 12] + (Math.floor(n / 12) - 2) }
    function noteNames() { const out = []; for (let n = 0; n < 128; ++n) out.push(noteName(n)); return out }
    contentItem: ColumnLayout {
        spacing: Theme.spacing[3]
        Text { text: qsTr("Convert Regions to New Sampler Track"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeTitleSize; font.weight: Theme.fontTypeTitleWeight }
        Text { text: qsTr("Create zones from"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize }
        Repeater {
            model: [{ id: "regions", label: qsTr("Regions") }, { id: "transients", label: qsTr("Transient Markers") }]
            delegate: RadioButton {
                required property var modelData
                objectName: "source_" + modelData.id
                text: modelData.label
                checked: root.source === modelData.id
                onClicked: root.source = modelData.id
                contentItem: Text { leftPadding: 24; text: parent.text; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize; verticalAlignment: Text.AlignVCenter }
            }
        }
        ThemedCheckBox { objectName: "oneShot"; text: qsTr("Create '1Shot' Zones"); checked: root.oneShot; onToggled: root.oneShot = checked }
        RowLayout {
            Layout.fillWidth: true
            Text { text: qsTr("Trigger Note Range"); Layout.fillWidth: true; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize }
            SelectField {
                objectName: "lowNote"
                Layout.preferredWidth: 80
                choices: root.noteNames()
                value: root.noteName(root.low)
                onChosen: (c) => { root.low = Math.max(0, root.noteNames().indexOf(c)); if (root.high < root.low) root.high = root.low }
            }
            Text { text: "–"; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize }
            SelectField {
                objectName: "highNote"
                Layout.preferredWidth: 80
                choices: root.noteNames()
                value: root.noteName(root.high)
                onChosen: (c) => { root.high = Math.max(0, root.noteNames().indexOf(c)); if (root.low > root.high) root.low = root.high }
            }
        }
        ThemedCheckBox { objectName: "mapEvenly"; text: qsTr("Map evenly across trigger note range"); checked: root.mapEvenly; onToggled: root.mapEvenly = checked }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            IconButton { implicitHeight: 26; label: qsTr("Cancel"); onClicked: root.close() }
            IconButton {
                objectName: "okButton"; implicitHeight: 26; label: qsTr("OK"); active: true; fillActive: true; fillText: Theme.textPrimary
                onClicked: {
                    root.project.convertRegionsToSampler({ source: root.source, oneShot: root.oneShot, mapEvenly: root.mapEvenly, low: root.low, high: root.high })
                    root.close()
                }
            }
        }
    }
}
