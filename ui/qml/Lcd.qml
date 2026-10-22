import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Jad

// The LCD: position (bars or time), tempo, time signature and key. Double click a cell to type a value; Enter applies it
// (an invalid value restores the old one and emits `message`), Escape cancels. A single click on the position switches
// between bars and time; a single click on the signature or the key opens a list to choose from, as in Logic (Custom… types a signature).
Rectangle {
    id: root
    required property ProjectController project
    property bool showTime: false
    readonly property alias positionCell: posCell
    readonly property alias tempoCell: tempoCell
    readonly property alias signatureCell: sigCell
    readonly property alias keyCell: keyCell
    signal message(string text)

    function editPosition() { posCell.begin() }
    function editKey() { keyCell.begin() }

    // the lists of Logic's LCD: common signatures, and the keys in the order of the circle of fifths (majors, then minors)
    readonly property var commonSignatures: [[3, 4], [4, 4], [5, 4], [6, 8], [7, 8], [12, 8]]
    readonly property var otherSignatures: [[2, 4], [6, 4], [1, 4], [22, 1], [20, 1], [8, 1], [9, 1]]
    readonly property var majorKeys: [["B", "B"], ["E", "E"], ["A", "A"], ["D", "D"], ["G", "G"], ["C", "C"], ["F", "F"], ["B♭", "A#"], ["E♭", "D#"], ["A♭", "G#"], ["D♭", "C#"], ["G♭", "F#"]]
    readonly property var minorKeys: [["G♯m", "G#"], ["C♯m", "C#"], ["F♯m", "F#"], ["Bm", "B"], ["Em", "E"], ["Am", "A"], ["Dm", "D"], ["Gm", "G"], ["Cm", "C"], ["Fm", "F"], ["B♭m", "A#"], ["E♭m", "D#"]]
    // sharps are how the project stores a key, so its text is compared with `<sharp name> maj` or `min`
    function isCurrentKey(name, minor) { return root.project.projectKey === name + (minor ? " min" : " maj") }
    function isCurrentSignature(n, d) { return root.project.signatureText === n + "/" + d }
    // a signature picked from the list: Logic asks whether it replaces the project's signature or starts a new one at the playhead
    property var pendingSignature: null
    function chooseSignature(n, d) {
        if (root.project.projectSetting("lcd.signatureAsk", "1") === "0") { root.project.setSignature(n, d); return }
        pendingSignature = { numerator: n, denominator: d }
        signatureAsk.open()
    }

    implicitWidth: row.implicitWidth + Theme.spacing[6] * 2
    implicitHeight: Theme.sizeControlLarge
    radius: Theme.radiusControl
    color: Theme.surfaceLcd
    border.color: Theme.surfaceLcdBezel
    border.width: 1

    component LcdCell: Item {
        id: cell
        property string shown
        property string caption
        property int minWidth: 56
        property bool editing: false
        readonly property alias input: field
        signal commit(string value)
        signal singleClicked()

        function begin() {
            editing = true
            field.text = shown
            field.forceActiveFocus()
            field.selectAll()
        }

        implicitWidth: Math.max(minWidth, value.implicitWidth + 4)
        implicitHeight: 30
        Column {
            anchors.centerIn: parent
            Text {
                id: value
                anchors.horizontalCenter: parent.horizontalCenter
                visible: !cell.editing
                text: cell.shown
                color: Theme.textValue
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTypeBodySize + 3
                font.weight: Theme.fontTypeLcdWeight
                font.features: { "tnum": 1 }
            }
            TextInput {
                id: field
                anchors.horizontalCenter: parent.horizontalCenter
                visible: cell.editing
                width: Math.max(cell.minWidth, contentWidth + 8)
                color: Theme.textValue
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTypeBodySize + 3
                selectByMouse: true
                onAccepted: {
                    cell.editing = false
                    cell.commit(text)
                }
                // Return is also a global shortcut (go to beginning): it must confirm the field, not run the shortcut
                Keys.onShortcutOverride: (event) => { if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) event.accepted = true }
                Keys.onEscapePressed: cell.editing = false
                onActiveFocusChanged: if (!activeFocus) cell.editing = false
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: cell.caption
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTypeCaptionSize
            }
        }
        // a single click waits out the double-click interval, so that a double click edits instead of switching first
        Timer {
            id: clickTimer
            interval: Qt.styleHints.mouseDoubleClickInterval
            onTriggered: cell.singleClicked()
        }
        MouseArea {
            anchors.fill: parent
            enabled: !cell.editing
            onClicked: clickTimer.restart()
            onDoubleClicked: { clickTimer.stop(); cell.begin() }
        }
    }

    RowLayout {
        id: row
        anchors.centerIn: parent
        spacing: Theme.spacing[5]

        LcdCell {
            id: posCell
            minWidth: 96
            shown: root.showTime ? LcdParser.formatTime(root.project.positionSeconds)
                                 : LcdParser.formatPosition(root.project.positionBeats, root.project.barBeats)
            caption: root.showTime ? qsTr("TIME") : qsTr("BAR BEAT DIV TICK")
            onSingleClicked: root.showTime = !root.showTime
            onCommit: (text) => {
                if (root.showTime) {
                    const s = LcdParser.timeSeconds(text)
                    if (s < 0) { root.message(qsTr("Invalid time")); return }
                    root.project.locateSeconds(s)
                } else {
                    const b = LcdParser.positionBeats(text, root.project.barBeats)
                    if (b < 0) { root.message(qsTr("Invalid position")); return }
                    root.project.locateBeats(b)
                }
            }
        }
        LcdCell {
            id: tempoCell
            shown: String(Number(root.project.bpm.toFixed(2)))
            caption: qsTr("BPM")
            onCommit: (text) => {
                const v = LcdParser.bpm(text)
                if (v < 0) { root.message(qsTr("Invalid tempo (20 to 999)")); return }
                root.project.setTempo(v)
            }
        }
        LcdCell {
            id: sigCell
            minWidth: 40
            shown: root.project.signatureText
            caption: qsTr("SIGNATURE")
            onSingleClicked: signatureMenu.popup(sigCell, 0, sigCell.height)
            onCommit: (text) => {
                const s = LcdParser.signature(text)
                if (s.numerator === undefined) { root.message(qsTr("Invalid time signature")); return }
                root.project.setSignature(s.numerator, s.denominator)
            }
        }
        LcdCell {
            minWidth: 40
            id: keyCell
            shown: root.project.projectKey
            caption: qsTr("KEY")
            onSingleClicked: keyMenu.popup(keyCell, 0, keyCell.height)
            onCommit: (text) => { if (!root.project.setProjectKey(text)) root.message(qsTr("Invalid key (for example C, F# min, Bb maj)")) }
        }
    }

    component SignatureItems: Repeater {
        delegate: ThemedMenuItem {
            required property var modelData
            text: modelData[0] + "/" + modelData[1]
            checkable: true
            checked: root.isCurrentSignature(modelData[0], modelData[1])
            onTriggered: root.chooseSignature(modelData[0], modelData[1])
        }
    }
    ThemedMenu {
        id: signatureMenu
        objectName: "signatureMenu"
        SignatureItems { model: root.commonSignatures }
        MenuSeparator { }
        SignatureItems { model: root.otherSignatures }
        MenuSeparator { }
        ThemedMenuItem { text: qsTr("Custom…"); onTriggered: sigCell.begin() }
    }
    component KeyItems: Repeater {
        property bool minor: false
        delegate: ThemedMenuItem {
            required property var modelData
            text: modelData[0]
            checkable: true
            checked: root.isCurrentKey(modelData[1], minor)
            onTriggered: root.project.setProjectKey(modelData[1] + (minor ? " min" : " maj"))
        }
    }
    ThemedMenu {
        id: keyMenu
        objectName: "keyMenu"
        KeyItems { minor: false; model: root.majorKeys }
        MenuSeparator { }
        KeyItems { minor: true; model: root.minorKeys }
    }
    Dialog {
        id: signatureAsk
        objectName: "signatureAsk"
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 460
        padding: 16
        background: Rectangle { color: Theme.surfacePanel; border.color: Theme.borderStrong; radius: Theme.radiusDialog }
        header: Item { height: 0 }
        property bool dontAsk: false
        function answer(insert) {
            const sig = root.pendingSignature
            if (dontAsk) root.project.setProjectSetting("lcd.signatureAsk", "0")
            if (sig) {
                if (insert) root.project.setSignatureAt(root.project.barToBeats(1 + Math.floor(root.project.positionBeats / root.project.barBeats)), sig.numerator, sig.denominator)
                else root.project.setSignature(sig.numerator, sig.denominator)
            }
            root.pendingSignature = null
            close()
        }
        contentItem: ColumnLayout {
            spacing: Theme.spacing[3]
            Text { text: qsTr("Do you want to change the project time signature?"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeTitleSize; font.weight: Theme.fontTypeTitleWeight; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Text { text: qsTr("You can change the signature of the whole project, or insert a new signature change at the playhead."); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            ThemedCheckBox { objectName: "signatureDontAsk"; text: qsTr("Don't ask again"); checked: signatureAsk.dontAsk; onToggled: signatureAsk.dontAsk = checked }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: Theme.spacing[2]
                IconButton { objectName: "signatureCancel"; implicitHeight: 26; label: qsTr("Cancel"); onClicked: { root.pendingSignature = null; signatureAsk.close() } }
                IconButton { objectName: "signatureInsert"; implicitHeight: 26; label: qsTr("Insert new signature"); onClicked: signatureAsk.answer(true) }
                IconButton { objectName: "signatureChange"; implicitHeight: 26; label: qsTr("Change project signature"); active: true; fillActive: true; fillText: Theme.textPrimary; onClicked: signatureAsk.answer(false) }
            }
        }
    }
}
