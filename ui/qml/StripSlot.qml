import QtQuick
import Jad

// One slot of a channel strip: a bar with a label, an optional value, a cross on hover when removable, and a
// horizontal drag (used for insert gain).
Rectangle {
    id: root
    property string text
    property string value
    property bool filled: false
    property color fillColor: Theme.accentPrimary
    property bool dim: false
    property bool removable: false
    property bool missing: false  // a plug-in that is not installed
    property bool movable: false          // an insert: it can be dragged to another place
    property bool horizontalDrag: true    // a horizontal drag reports `dragged` (the gain of a Gain insert); false: any drag moves
    property bool showBypass: false       // an insert: a power toggle on the left
    property bool bypassed: false
    property Item boundsItem: null        // the strip: a gain drag that leaves it becomes a move
    property real moveDy: 0               // while it is being moved: how far it follows the pointer
    readonly property bool hovered: area.containsMouse
    signal clicked(int modifiers)
    signal rightClicked()
    signal removeRequested()
    signal dragged(real dx)
    signal dragReleased()
    signal dragAborted()  // a gain drag turned into a move: the gain it was showing is dropped
    signal moveStarted()
    signal moved(real sceneX, real sceneY)
    signal moveReleased(real sceneX, real sceneY)
    signal moveCancelled()
    signal bypassToggled(bool on)
    signal doubleClicked()

    implicitHeight: 18
    implicitWidth: 84
    radius: Theme.radiusControl - 2
    color: filled ? fillColor : (area.containsMouse ? Theme.surfaceRaisedHover : Theme.surfaceRaised)
    border.color: Theme.borderSubtle
    opacity: bypassed ? 0.55 : (dim ? 0.7 : 1)
    z: moveDy !== 0 ? 100 : 0
    transform: Translate { y: root.moveDy }
    Rectangle {  // the power toggle
        visible: root.showBypass
        x: 4
        anchors.verticalCenter: parent.verticalCenter
        width: 8
        height: 8
        radius: 4
        color: "transparent"
        border.color: root.bypassed ? Theme.textDisabled : Theme.textPrimary
        border.width: 1
        Rectangle { anchors.centerIn: parent; width: 4; height: 4; radius: 2; visible: !root.bypassed; color: Theme.textPrimary }
    }

    Text {
        anchors.left: parent.left
        anchors.leftMargin: root.showBypass ? 16 : Theme.spacing[2]
        anchors.right: valueText.visible ? valueText.left : parent.right
        anchors.rightMargin: Theme.spacing[1]
        anchors.verticalCenter: parent.verticalCenter
        text: root.text
        elide: Text.ElideRight
        color: root.missing ? Theme.stateClip : (root.filled ? Theme.textPrimary : Theme.textSecondary)
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontTypeLabelSize
        font.weight: Theme.fontTypeLabelWeight
        font.strikeout: root.bypassed
    }
    Text {
        id: valueText
        visible: root.value !== "" && !(root.removable && area.containsMouse)
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacing[2]
        anchors.verticalCenter: parent.verticalCenter
        text: root.value
        color: Theme.textValue
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontTypeLabelSize
    }
    Text {
        visible: root.removable && area.containsMouse
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacing[2]
        anchors.verticalCenter: parent.verticalCenter
        text: "×"
        color: Theme.textPrimary
        font.pixelSize: Theme.fontTypeBodySize
    }
    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        property real pressX: 0  // scene coordinates: the slot follows the pointer, so item coordinates would stand still
        property real pressY: 0
        property bool moved: false
        property int axis: 0  // 0 undecided, 1 horizontal (gain), 2 move
        onPressed: (m) => { const p = mapToItem(null, m.x, m.y); pressX = p.x; pressY = p.y; moved = false; axis = 0; root.moveDy = 0 }
        onPositionChanged: (m) => {
            if (!pressed || pressedButtons !== Qt.LeftButton) return
            const p = mapToItem(null, m.x, m.y)
            const dx = p.x - pressX, dy = p.y - pressY
            if (axis === 0) {  // the axis is decided once per gesture, after 4 px
                if (Math.max(Math.abs(dx), Math.abs(dy)) <= 4) return
                if (root.movable && (!root.horizontalDrag || Math.abs(dy) > Math.abs(dx))) { axis = 2; root.moveStarted() }
                else if (root.horizontalDrag) axis = 1
                else return
                moved = true
            }
            if (axis === 1 && root.movable && root.boundsItem) {
                const inside = root.boundsItem.mapFromItem(null, p.x, p.y)
                if (inside.x < 0 || inside.x > root.boundsItem.width) { axis = 2; root.dragAborted(); root.moveStarted() }
            }
            if (axis === 1) {
                root.dragged(dx)
            } else {
                root.moveDy = dy
                root.moved(p.x, p.y)
            }
        }
        onReleased: (m) => {
            if (axis === 1) {
                root.dragReleased()
            } else if (axis === 2) {
                const p = mapToItem(null, m.x, m.y)
                root.moveDy = 0
                root.moveReleased(p.x, p.y)
            }
            axis = 0
        }
        onCanceled: { if (axis === 2) { root.moveDy = 0; root.moveCancelled() } axis = 0 }
        onDoubleClicked: root.doubleClicked()
        onClicked: (m) => {
            if (m.button === Qt.RightButton) { root.rightClicked(); return }
            if (moved) return
            if (root.showBypass && (m.x < 16 || (m.modifiers & Qt.AltModifier))) { root.bypassToggled(!root.bypassed); return }
            if (root.removable && m.x > root.width - 16) root.removeRequested()
            else root.clicked(m.modifiers)
        }
    }
}
