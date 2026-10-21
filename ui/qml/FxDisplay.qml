import QtQuick
import Jad

// The dark screen of the stock effect windows (curves, meters), with the thin inner frame Logic draws.
Rectangle {
    color: "#101012"
    radius: 6
    border.color: "#2c2c30"
    border.width: 1
    Rectangle { anchors.fill: parent; anchors.margins: 1; radius: 5; color: "transparent"; border.color: "#050506"; border.width: 1 }
}
