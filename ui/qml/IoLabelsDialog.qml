import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Jad

// Mix > I/O Labels: one table for every channel of the interface and the buses (Logic's window). A row has the name the driver gives, a switch for the
// User name, and the Long and Short names that the Input and Output menus and the strips show.
Dialog {
    id: root
    required property ProjectController project
    readonly property int inputCount: Math.max(project.inputChannels, 4)
    readonly property int outputCount: Math.max(project.outputChannels, 4)
    readonly property var rows: {
        const r = []
        for (let i = 1; i <= inputCount; ++i) r.push({ key: "Input " + i, driver: "Input " + i, enabled: true })
        for (let i = 1; i <= outputCount; ++i) r.push({ key: "Output " + i, driver: "Output " + i, enabled: true })
        for (let i = 1; i + 1 <= inputCount; i += 2) r.push({ key: "Input " + i + "-" + (i + 1), driver: "Input " + i + "/" + (i + 1), enabled: true })
        for (let i = 1; i + 1 <= outputCount; i += 2) r.push({ key: "Output " + i + "-" + (i + 1), driver: "Output " + i + "/" + (i + 1), enabled: i !== 1 })
        for (let i = 1; i <= 256; ++i) r.push({ key: "Bus " + i, driver: "-", enabled: true })
        return r
    }
    readonly property var columnWidths: [150, 170, 70, 170, 150]
    modal: true
    anchors.centerIn: parent
    width: Math.min(parent ? parent.width - 60 : 760, 760)
    height: Math.min(parent ? parent.height - 60 : 560, 560)
    padding: 12
    background: Rectangle { color: Theme.surfacePanel; border.color: Theme.borderStrong; radius: Theme.radiusDialog }
    header: Item { height: 0 }

    contentItem: ColumnLayout {
        spacing: Theme.spacing[2]
        RowLayout {
            Layout.fillWidth: true
            TrackIcon { size: 20; glyphFile: "icon-io-labels"; opacity: 0.6 }
            IconButton { implicitHeight: 24; label: qsTr("Reset"); objectName: "ioReset"; onClicked: root.project.resetIoLabels() }
            Item { Layout.fillWidth: true }
            IconButton { implicitHeight: 24; label: qsTr("Close"); onClicked: root.close() }
        }
        Row {
            Layout.fillWidth: true
            Repeater {
                model: [qsTr("Channel"), qsTr("Provided by Driver"), qsTr("User"), qsTr("Long"), qsTr("Short")]
                delegate: Text {
                    required property int index
                    required property string modelData
                    width: root.columnWidths[index]
                    leftPadding: 6
                    text: modelData
                    color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeLabelSize; font.weight: 600
                }
            }
        }
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.rows
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            delegate: Rectangle {
                id: row
                required property int index
                required property var modelData
                readonly property var label: { const rev = root.project.inputLabelsRevision; return [rev, root.project.ioLabel(modelData.key)][1] }
                width: ListView.view.width - 10
                height: 24
                color: index % 2 ? Theme.surfaceRaised : "transparent"
                opacity: modelData.enabled ? 1 : 0.45
                Row {
                    anchors.fill: parent
                    Item {
                        width: root.columnWidths[0]; height: parent.height
                        Rectangle { x: 6; anchors.verticalCenter: parent.verticalCenter; width: 10; height: 10; radius: 5; color: row.modelData.enabled ? Theme.accentPrimary : Theme.borderSubtle }
                        Text { x: 24; anchors.verticalCenter: parent.verticalCenter; text: row.modelData.key; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize }
                    }
                    Item {
                        width: root.columnWidths[1]; height: parent.height
                        Rectangle { x: 6; anchors.verticalCenter: parent.verticalCenter; width: 10; height: 10; radius: 5; color: Theme.borderSubtle }
                        Text { x: 24; anchors.verticalCenter: parent.verticalCenter; text: row.modelData.driver; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize }
                    }
                    Item {
                        width: root.columnWidths[2]; height: parent.height
                        Rectangle {
                            id: userDot
                            objectName: "ioUser_" + row.modelData.key
                            anchors.centerIn: parent
                            width: 10; height: 10; radius: 5
                            color: row.label.user ? Theme.accentPrimary : Theme.borderSubtle
                            MouseArea {
                                anchors.fill: parent; anchors.margins: -4
                                enabled: row.modelData.enabled
                                onClicked: root.project.setIoLabel(row.modelData.key, !row.label.user, row.label.longName, row.label.shortName)
                            }
                        }
                    }
                    Repeater {
                        model: 2
                        delegate: TextInput {
                            id: field
                            required property int index
                            objectName: (index === 0 ? "ioLong_" : "ioShort_") + row.modelData.key
                            width: root.columnWidths[3 + index]; height: parent.height
                            leftPadding: 6
                            verticalAlignment: TextInput.AlignVCenter
                            maximumLength: index === 0 ? 32 : 16
                            enabled: row.modelData.enabled
                            readonly property string stored: index === 0 ? row.label.longName : row.label.shortName
                            text: stored
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize
                            Text { visible: !field.text && !field.activeFocus; leftPadding: 6; anchors.verticalCenter: parent.verticalCenter; text: "-"; color: Theme.textSecondary; font: field.font }
                            onEditingFinished: {
                                const l = index === 0 ? text : row.label.longName, s = index === 1 ? text : row.label.shortName
                                // typing a name switches the User name on, as in Logic
                                root.project.setIoLabel(row.modelData.key, row.label.user || text.length > 0, l, s)
                            }
                        }
                    }
                }
            }
        }
    }
}
