import QtQuick
import QtQuick.Layouts
import Jad

// The LCD: position (bars or time), tempo, time signature and key. Double click a cell to type a value; Enter applies it
// (an invalid value restores the old one and emits `message`), Escape cancels. A single click on the position switches
// between bars and time.
Rectangle {
    id: root
    required property ProjectController project
    property bool showTime: false
    readonly property alias positionCell: posCell
    readonly property alias tempoCell: tempoCell
    readonly property alias signatureCell: sigCell
    signal message(string text)

    function editPosition() { posCell.begin() }

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
        MouseArea {
            anchors.fill: parent
            enabled: !cell.editing
            onClicked: cell.singleClicked()
            onDoubleClicked: cell.begin()
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
                                 : LcdParser.formatPosition(root.project.positionBeats, root.project.beatsPerBar)
            caption: root.showTime ? qsTr("TIME") : qsTr("BAR BEAT DIV TICK")
            onSingleClicked: root.showTime = !root.showTime
            onCommit: (text) => {
                if (root.showTime) {
                    const s = LcdParser.timeSeconds(text)
                    if (s < 0) { root.message(qsTr("Invalid time")); return }
                    root.project.locateSeconds(s)
                } else {
                    const b = LcdParser.positionBeats(text, root.project.beatsPerBar)
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
            onCommit: (text) => {
                const s = LcdParser.signature(text)
                if (s.numerator === undefined) { root.message(qsTr("Invalid time signature")); return }
                root.project.setSignature(s.numerator, s.denominator)
            }
        }
        LcdCell {
            minWidth: 40
            shown: "C maj"
            caption: qsTr("KEY")
            onSingleClicked: if (ActionHub.registry) ActionHub.registry.stubTriggered("lcd.key", false)
        }
    }
}
