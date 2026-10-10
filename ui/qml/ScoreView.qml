import QtQuick
import QtQuick.Controls.Basic
import Jad

// The Score of the Editors area: the notes of the selected MIDI region on a grand staff (treble above bass), read-only. Note heads are filled up to a
// beat and hollow from two beats, black keys are written as sharps, ledger lines and bar lines are drawn; there are no beams, rests or ties.
Item {
    id: root
    required property ProjectController project
    property string regionId: ""
    readonly property var info: { project.revision; return regionId !== "" ? project.regionInfo(regionId) : ({ found: false }) }
    readonly property bool hasMidi: info.found === true && info.audio !== true
    readonly property var notes: { project.revision; return hasMidi ? project.regionNotes(regionId) : [] }
    readonly property real lineGap: 8              // between two staff lines
    readonly property real pixelsPerBeat: 46
    readonly property real leftMargin: 70                // clefs and the time signature
    readonly property var steps: [0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6]
    readonly property var sharp: [false, true, false, true, false, false, true, false, true, false, true, false]
    function diatonic(n) { return (Math.floor(n / 12) - 1) * 7 + steps[n % 12] }

    Rectangle { anchors.fill: parent; color: Theme.surfaceCanvas }
    Text {
        anchors.centerIn: parent
        visible: !root.hasMidi
        text: qsTr("Select a MIDI region to see its notes")
        color: Theme.textSecondary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontTypeBodySize
    }
    Flickable {
        id: flick
        visible: root.hasMidi
        anchors.fill: parent
        contentWidth: root.leftMargin + (root.hasMidi ? root.info.lengthBeats : 0) * root.pixelsPerBeat + 40
        contentHeight: height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.horizontal: ScrollBar {}
        Canvas {
            id: canvas
            objectName: "scoreCanvas"
            width: flick.contentWidth
            height: flick.height
            property var shownNotes: root.notes
            onShownNotesChanged: requestPaint()
            Connections { target: root; function onHasMidiChanged() { canvas.requestPaint() } }
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            onPaint: {
                const ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)
                if (!root.hasMidi) return
                const sp = root.lineGap
                const trebleBottom = Math.max(70, height / 2 - 30)              // the bottom line of the treble staff
                const bassTop = trebleBottom + 60                                // the top line of the bass staff
                const bassBottom = bassTop + 4 * sp
                const bottomLine = { treble: trebleBottom, bass: bassBottom }
                const bottomIndex = { treble: 30, bass: 18 }                     // E4 and G2 as diatonic steps
                const right = root.leftMargin + root.info.lengthBeats * root.pixelsPerBeat
                ctx.strokeStyle = "#a0a0a0"
                ctx.fillStyle = "#e8e8e8"
                ctx.lineWidth = 1
                for (const clef of ["treble", "bass"])
                    for (let i = 0; i < 5; ++i) {
                        const y = bottomLine[clef] - i * sp + 0.5
                        ctx.beginPath(); ctx.moveTo(8, y); ctx.lineTo(right, y); ctx.stroke()
                    }
                ctx.beginPath(); ctx.moveTo(8, trebleBottom - 4 * sp); ctx.lineTo(8, bassBottom); ctx.stroke()
                ctx.font = "34px 'Segoe UI Symbol', 'Apple Symbols', 'Noto Music', sans-serif"
                ctx.fillText("𝄞", 14, trebleBottom - sp)               // the G clef
                ctx.fillText("𝄢", 14, bassTop + 3 * sp)                // the F clef
                ctx.font = "bold 14px sans-serif"
                const num = root.project.beatsPerBar, den = Math.round(4 * root.project.beatsPerBar / root.project.barBeats)
                ctx.fillText(String(num), 46, trebleBottom - 2 * sp - 2); ctx.fillText(String(den), 46, trebleBottom - 2)
                ctx.fillText(String(num), 46, bassBottom - 2 * sp - 2); ctx.fillText(String(den), 46, bassBottom - 2)
                // bar lines
                ctx.strokeStyle = "#707070"
                for (let b = root.project.barBeats; b < root.info.lengthBeats + 1e-6; b += root.project.barBeats) {
                    const x = root.leftMargin + b * root.pixelsPerBeat
                    ctx.beginPath(); ctx.moveTo(x, trebleBottom - 4 * sp); ctx.lineTo(x, bassBottom); ctx.stroke()
                }
                // the notes: the treble staff from the middle C up, the bass staff below
                for (const n of root.notes) {
                    const clef = n.note >= 60 ? "treble" : "bass"
                    const dia = root.diatonic(n.note)
                    const y = bottomLine[clef] - (dia - bottomIndex[clef]) * sp / 2
                    const x = root.leftMargin + n.start * root.pixelsPerBeat + 6
                    ctx.strokeStyle = "#a0a0a0"
                    // ledger lines above and below the staff
                    const step = dia - bottomIndex[clef]
                    for (let s = -2; s >= step; s -= 2) { const ly = bottomLine[clef] - s * sp / 2 + 0.5; ctx.beginPath(); ctx.moveTo(x - 7, ly); ctx.lineTo(x + 7, ly); ctx.stroke() }
                    for (let s = 10; s <= step; s += 2) { const ly = bottomLine[clef] - s * sp / 2 + 0.5; ctx.beginPath(); ctx.moveTo(x - 7, ly); ctx.lineTo(x + 7, ly); ctx.stroke() }
                    // the note head
                    ctx.fillStyle = "#e8e8e8"
                    ctx.strokeStyle = "#e8e8e8"
                    ctx.save()
                    ctx.translate(x, y)
                    ctx.rotate(-0.35)
                    ctx.scale(1, 0.7)
                    ctx.beginPath()
                    ctx.arc(0, 0, 5.5, 0, Math.PI * 2)
                    if (n.length >= 2) ctx.stroke(); else ctx.fill()
                    ctx.restore()
                    if (root.sharp[n.note % 12]) { ctx.font = "12px sans-serif"; ctx.fillText("♯", x - 17, y + 4) }
                    // the stem: up on the lower half of the staff, down on the upper half
                    if (n.length < 4) {
                        const up = step < 4
                        ctx.beginPath()
                        if (up) { ctx.moveTo(x + 5, y); ctx.lineTo(x + 5, y - 3 * sp) } else { ctx.moveTo(x - 5, y); ctx.lineTo(x - 5, y + 3 * sp) }
                        ctx.stroke()
                    }
                }
            }
        }
    }
}
