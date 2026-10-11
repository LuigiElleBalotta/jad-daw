import QtQuick
import QtQuick.Window
import Jad

// The Flex editor: the selected audio region large, with its transients and flex markers (Flex Time) or its notes (Flex Pitch), the mode,
// and the actions that go with them. It is the Audio Track Editor of the Editors area in a window of its own (Edit > Flex > Open Flex Editor,
// or a double click on a region while the Flex view is on).
Window {
    id: root
    required property ProjectController project
    readonly property string regionId: {
        if (project.revision < 0) return ""
        for (const id of project.selectedRegionIds) {
            const info = project.regionInfo(id)
            if (info.found === true && info.audio === true) return id
        }
        return ""
    }
    readonly property alias editor: editor

    width: 1180
    height: 620
    minimumWidth: 640
    minimumHeight: 360
    title: qsTr("Flex Editor")
    color: Theme.surfaceCanvas

    AudioEditor {
        id: editor
        objectName: "flexEditorContent"
        anchors.fill: parent
        project: root.project
        regionId: root.regionId
    }
}
