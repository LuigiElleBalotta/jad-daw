import QtQuick
import Jad

// Mirrored peak bars; `peaks` holds values in [0,1], one per two pixels.
Canvas {
    id: root
    property var peaks: []
    property color color: Theme.textPrimary
    property real zoom: 1          // vertical zoom: the bars are drawn this many times taller (View > Waveform Zoom)

    onPeaksChanged: requestPaint()
    onColorChanged: requestPaint()
    onZoomChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()

    onPaint: {
        const ctx = getContext("2d")
        ctx.clearRect(0, 0, width, height)
        const n = peaks.length
        if (n === 0) return
        ctx.globalAlpha = Theme.waveformStrokeOpacity
        ctx.fillStyle = color
        const mid = height / 2
        const barW = width / n
        for (let i = 0; i < n; ++i) {
            const h = Math.max(1, Math.min(1, peaks[i] * zoom) * height)
            ctx.fillRect(i * barW, mid - h / 2, Math.max(1, barW - 0.5), h)
        }
    }
}
