// The track colours: the ten named colours of the theme and the 24 x 4 swatches of Logic's Colors window (sampled from Logic Pro 11.2).
// A track colour is a name ("purple") or "#rrggbb"; the empty colour means the default one.
pragma Singleton
import QtQuick

QtObject {
    // row 0 is the pure set, rows 1 to 3 get darker; the columns sweep the hue wheel from red-orange to magenta
    readonly property var palette: [
        ["#c93b10", "#c95a10", "#c97c10", "#c9a410", "#c9c910", "#a4c910", "#7fc910", "#5ac910", "#10c92f", "#10c95a", "#10c97f", "#10c9a4", "#10c9c9", "#10a4c9", "#2596cf", "#3582d4", "#3e67d6", "#4643d9", "#5e40d6", "#7739d4", "#902ed1", "#a410c9", "#c910c9", "#c910a4"],
        ["#a33a1a", "#a3511a", "#a36a1a", "#a3881a", "#a3a31a", "#88a31a", "#6ca31a", "#51a31a", "#1aa331", "#1aa351", "#1aa36c", "#1aa388", "#1aa3a3", "#1a88a3", "#297ca6", "#376fab", "#3d5bad", "#4341b0", "#553ead", "#6638ab", "#7a32ab", "#881aa3", "#a31aa3", "#a31a88"],
        ["#7d341e", "#7d441e", "#7d551e", "#7d6a1e", "#7d7d1e", "#6a7d1e", "#577d1e", "#447d1e", "#1e7d2e", "#1e7d44", "#1e7d57", "#1e7d6a", "#1e7d7d", "#1e6a7d", "#296380", "#315882", "#364b85", "#3a3985", "#473885", "#533482", "#612f82", "#6a1e7d", "#7d1e7d", "#7d1e6a"],
        ["#572a1c", "#57331c", "#573e1c", "#574b1c", "#57571c", "#4b571c", "#3f571c", "#33571c", "#1c5726", "#1c5733", "#1c573f", "#1c574b", "#1c5757", "#1c4b57", "#234759", "#29425c", "#2b385c", "#2e2d5c", "#362c5c", "#3e2a5c", "#452659", "#4b1c57", "#571c57", "#571c4b"]
    ]

    function isHex(name) { return typeof name === "string" && name.length === 7 && name.charAt(0) === "#" }
    function named(name, suffix) {
        const key = "track" + name.charAt(0).toUpperCase() + name.slice(1) + suffix
        return Theme[key] !== undefined ? Theme[key] : Theme.trackPurpleSolid
    }
    // the colour of the track header, the icon and the region outline
    function solid(name) { return isHex(name) ? Qt.color(name) : named(name ?? "purple", "Solid") }
    // the region body: a swatch gets its own darker shade, a named colour its theme fill
    function fill(name) { return isHex(name) ? Qt.darker(Qt.color(name), 2.6) : named(name ?? "purple", "Fill") }
}
