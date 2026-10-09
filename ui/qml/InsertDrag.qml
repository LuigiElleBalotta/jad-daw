pragma Singleton
import QtQuick

// The insert being dragged from one channel strip to a place in the same or another strip. Strips register themselves; the
// slot being dragged reports scene positions here, and the strip under the pointer shows where the insert would land.
QtObject {
    id: root
    property var strips: []
    property var source: null
    property int sourceIndex: -1
    property var target: null

    function register(strip) { strips = strips.concat([strip]) }
    function unregister(strip) {
        if (source === strip) cancel()
        strips = strips.filter((s) => s !== strip)
    }
    function begin(strip, index) { clearLine(); source = strip; sourceIndex = index; target = null }
    function cancel() { clearLine(); source = null; sourceIndex = -1; target = null }
    function cancelIf(strip) { if (source === strip) cancel() }
    function clearLine() { if (target) target.hideDropLine() }
    function stripAt(sx, sy) {
        for (let i = strips.length - 1; i >= 0; --i) {
            const s = strips[i]
            if (!s.acceptsInserts) continue
            const p = s.mapFromItem(null, sx, sy)
            if (p.x >= 0 && p.y >= 0 && p.x <= s.width && p.y <= s.height) return { strip: s, y: p.y }
        }
        return null
    }
    // Moves the drop line to the strip under the point; returns the place {trackId, to} or null (over no strip)
    function update(sx, sy) {
        if (!source) return null
        const hit = stripAt(sx, sy)
        const next = hit ? hit.strip : null
        if (target && target !== next) target.hideDropLine()
        target = next
        if (!hit) return null
        const same = hit.strip.trackId === source.trackId
        const to = hit.strip.dropIndexAt(hit.y, same ? sourceIndex : -1)
        hit.strip.showDropLine(to, same ? sourceIndex : -1)
        return { trackId: hit.strip.trackId, to: to }
    }
    function end(sx, sy) {
        const place = update(sx, sy)
        cancel()
        return place
    }
}
