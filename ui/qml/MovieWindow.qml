import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtMultimedia
import Jad

// View > Movie Window and File > Movie: a floating video window that follows the transport. The movie file and the project time of its first frame are kept
// with the project (settings moviePath, movieStart); the sound of the movie is not played, the project's audio is the sound.
Window {
    id: root
    required property ProjectController project
    readonly property string path: project.projectSettings.moviePath === undefined ? "" : project.projectSettings.moviePath
    readonly property real startSeconds: project.projectSettings.movieStart === undefined ? 0 : Number(project.projectSettings.movieStart)
    title: qsTr("Movie")
    width: 480
    height: 300
    minimumWidth: 200
    minimumHeight: 120
    color: "black"
    flags: Qt.Tool | Qt.WindowStaysOnTopHint | Qt.WindowTitleHint | Qt.WindowCloseButtonHint | Qt.WindowMinMaxButtonsHint

    function openMovie(url) {
        project.setProjectSetting("moviePath", url.toString())
        project.setProjectSetting("movieStart", "0")
        root.show()
    }
    function closeMovie() {
        project.setProjectSetting("moviePath", "")
        project.setProjectSetting("movieStart", "0")
        root.hide()
    }
    function setStartToPlayhead() { project.setProjectSetting("movieStart", String(project.positionSeconds)) }
    function toggle() {
        if (root.visible) root.hide()
        else root.show()
    }
    onVisibleChanged: if (visible) syncTimer.restart()

    MediaPlayer {
        id: player
        source: root.path
        videoOutput: video
        audioOutput: AudioOutput { volume: 0 }
    }
    VideoOutput { id: video; anchors.fill: parent; fillMode: VideoOutput.PreserveAspectFit }
    Text {
        anchors.centerIn: parent
        visible: root.path === ""
        text: qsTr("No movie. Use File > Movie > Open Movie.")
        color: "#aaaaaa"
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontTypeBodySize
    }

    // where the movie should be, in ms, for the project time now (before the movie starts, the first frame)
    readonly property real wantedMs: Math.max(0, (project.positionSeconds - startSeconds) * 1000)
    Timer {
        id: syncTimer
        interval: 60
        repeat: true
        running: root.visible && root.path !== ""
        onTriggered: {
            if (root.project.playing && root.project.positionSeconds >= root.startSeconds) {
                if (Math.abs(player.position - root.wantedMs) > 180) player.position = root.wantedMs
                if (player.playbackState !== MediaPlayer.PlayingState) player.play()
            } else {
                if (player.playbackState === MediaPlayer.PlayingState) player.pause()
                if (Math.abs(player.position - root.wantedMs) > 20) player.position = root.wantedMs
            }
        }
    }
}
