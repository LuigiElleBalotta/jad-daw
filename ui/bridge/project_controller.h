#pragma once
#include <QList>
#include <QObject>
#include <QStringList>
#include <QHash>
#include <QSet>
#include <QSettings>
#include <QString>
#include <QVariantList>
#include <QTimer>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include <atomic>
#include <deque>
#include <functional>
#include <filesystem>
#include <memory>
#include <optional>
#include <thread>

#include <nlohmann/json_fwd.hpp>

#include "lpc/audio/click.h"
#include "lpc/audio/engine.h"
#include "lpc/device.h"
#include "bridge/inspector_model.h"
#include "bridge/library_model.h"
#include "bridge/mixer_model.h"
#include "bridge/plugins_model.h"
#include "bridge/region_model.h"
#include "bridge/snapshot.h"
#include "bridge/track_list_model.h"
#include "bridge/waveform_cache.h"

namespace lpc {
class MediaStore;
class PatchLibrary;
class ProjectHost;
struct InsertSlot;
#ifdef JAD_HAVE_JUCE
class JucePluginHost;
class PluginScanner;
#endif
class IAudioDevice;
class IAudioCallback;
namespace audio {
class AudioEngine;
}
}  // namespace lpc

namespace jad {

struct JuceInit;
class EngineDriver;

// The only door between QML and the Core. Owns the engine, media store, project host and the audio device.
// QML never changes data directly: it sends JSON commands (submit and the helpers built on it) and shows the
// models, which are rebuilt from a snapshot after every accepted change.
class ProjectController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool hasProject READ hasProject NOTIFY projectChanged)
    Q_PROPERTY(QString projectName READ projectName NOTIFY projectChanged)
    Q_PROPERTY(double bpm READ bpm NOTIFY projectChanged)
    Q_PROPERTY(int beatsPerBar READ beatsPerBar NOTIFY projectChanged)
    Q_PROPERTY(double barBeats READ barBeats NOTIFY projectChanged)  // quarter-note beats in a bar: numerator * 4 / denominator
    Q_PROPERTY(QString signatureText READ signatureText NOTIFY projectChanged)
    Q_PROPERTY(QString projectKey READ projectKey NOTIFY projectChanged)  // as the LCD shows it: "C maj", "A min"
    Q_PROPERTY(QString tool READ tool WRITE setTool NOTIFY toolChanged)
    Q_PROPERTY(QString snap READ snap WRITE setSnap NOTIFY snapChanged)
    // Mix > Automation Settings. Quick Access: one MIDI controller (a CC) writes the active automation parameter (volume or pan) of the selected track. Turning it
    // on waits for the controller to be moved (-2), which assigns it. Autoselect: moving a fader or knob in Read mode shows its lane while Show Automation is on.
    Q_PROPERTY(bool automationQuickAccess READ automationQuickAccess WRITE setAutomationQuickAccess NOTIFY automationSettingsChanged)
    Q_PROPERTY(int quickAccessController READ quickAccessController NOTIFY automationSettingsChanged)  // the CC number; -1 none, -2 waiting for a move
    Q_PROPERTY(bool autoselectAutomationParam READ autoselectAutomationParam WRITE setAutoselectAutomationParam NOTIFY automationSettingsChanged)
    // The Marquee tool: a range of time over a few tracks. Edit > Cut, Copy and Delete (and marqueeAction) act on that range: the regions are cut at its edges
    // and the pieces inside are what the command works on.
    Q_PROPERTY(bool hasMarquee READ hasMarquee NOTIFY marqueeChanged)
    Q_PROPERTY(QVariantMap marquee READ marquee NOTIFY marqueeChanged)  // {from, to (beats), row0, row1}
    Q_PROPERTY(bool automationFollowsRegions READ automationFollowsRegions WRITE setAutomationFollowsRegions NOTIFY dragModeChanged)  // Mix > Move Track Automation with Regions
    Q_PROPERTY(QString dragMode READ dragMode WRITE setDragMode NOTIFY dragModeChanged)  // "overlap", "noOverlap" or "xfade"
    Q_PROPERTY(double snapBeats READ snapBeats NOTIFY snapChanged)
    Q_PROPERTY(bool followPlayhead READ followPlayhead WRITE setFollowPlayhead NOTIFY followPlayheadChanged)
    Q_PROPERTY(QStringList selectedTrackIds READ selectedTrackIds NOTIFY selectionChanged)
    Q_PROPERTY(QStringList selectedRegionIds READ selectedRegionIds NOTIFY selectionChanged)
    Q_PROPERTY(bool selectedRecordArm READ selectedRecordArm NOTIFY trackTogglesChanged)  // of the first selected track
    Q_PROPERTY(bool selectedInputMonitor READ selectedInputMonitor NOTIFY trackTogglesChanged)
    Q_PROPERTY(bool selectedFrozen READ selectedFrozen NOTIFY trackFlagsChanged)  // the first selected track plays its frozen audio
    Q_PROPERTY(bool selectedShowInTracks READ selectedShowInTracks NOTIFY trackFlagsChanged)  // of the first selected track
    Q_PROPERTY(bool selectedCanHide READ selectedCanHide NOTIFY trackFlagsChanged)            // the first selected track is a bus or aux
    Q_PROPERTY(int trackHeightIndex READ trackHeightIndex WRITE setTrackHeightIndex NOTIFY trackHeightChanged)
    Q_PROPERTY(double masterGainDb READ masterGainDb NOTIFY projectChanged)
    Q_PROPERTY(bool anySolo READ anySolo NOTIFY projectChanged)  // a track is soloed: the S indicator and the yellow playhead
    Q_PROPERTY(bool mixerVisible READ mixerVisible WRITE setMixerVisible NOTIFY mixerVisibleChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(double positionSeconds READ positionSeconds NOTIFY positionChanged)
    Q_PROPERTY(double positionBeats READ positionBeats NOTIFY positionChanged)
    Q_PROPERTY(bool loopEnabled READ loopEnabled NOTIFY loopChanged)
    Q_PROPERTY(double loopStartBeats READ loopStartBeats NOTIFY loopChanged)  // the cycle area, also while the cycle is off
    Q_PROPERTY(double loopEndBeats READ loopEndBeats NOTIFY loopChanged)
    Q_PROPERTY(bool degraded READ degraded NOTIFY degradedChanged)
    Q_PROPERTY(QString deviceError READ deviceError NOTIFY deviceErrorChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(bool audioEnabled READ audioEnabled WRITE setAudioEnabled NOTIFY audioEnabledChanged)
    Q_PROPERTY(double masterPeak READ masterPeak NOTIFY peakChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY projectChanged)  // the project has changes that are not saved
    Q_PROPERTY(bool quickHelpVisible READ quickHelpVisible WRITE setQuickHelpVisible NOTIFY barsChanged)  // View > Quick Help
    Q_PROPERTY(bool controlBarVisible READ controlBarVisible WRITE setControlBarVisible NOTIFY barsChanged)  // View > Control Bar
    Q_PROPERTY(bool toolbarVisible READ toolbarVisible WRITE setToolbarVisible NOTIFY barsChanged)          // View > Toolbar
    Q_PROPERTY(bool globalTracksVisible READ globalTracksVisible WRITE setGlobalTracksVisible NOTIFY globalTracksVisibleChanged)  // the Marker, Tempo and Signature lanes
    Q_PROPERTY(bool automationVisible READ automationVisible WRITE setAutomationVisible NOTIFY automationViewChanged)  // Mix > Show Automation
    Q_PROPERTY(int automationRevision READ automationRevision NOTIFY automationViewChanged)  // follows the per-track choices of the lanes
    Q_PROPERTY(QString automationParam READ automationParam WRITE setAutomationParam NOTIFY automationViewChanged)   // "volume", "pan", "send1".."send4"
    Q_PROPERTY(bool metronomeOn READ metronomeOn NOTIFY metronomeChanged)  // the click while playing
    Q_PROPERTY(QString clickMode READ clickMode WRITE setClickMode NOTIFY clickSettingsChanged)          // "beats", "eighths", "sixteenths" or "grouped"
    Q_PROPERTY(QString clickGrouping READ clickGrouping WRITE setClickGrouping NOTIFY clickSettingsChanged)  // "3+2+2"
    Q_PROPERTY(int clickRevision READ clickRevision NOTIFY clickSettingsChanged)
    Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged)
    Q_PROPERTY(bool autoInputMonitoring READ autoInputMonitoring WRITE setAutoInputMonitoring NOTIFY recordingChanged)  // Record > Auto Input Monitoring
    Q_PROPERTY(bool countInEnabled READ countInEnabled WRITE setCountInEnabled NOTIFY recordingChanged)
    Q_PROPERTY(int countInChoice READ countInChoice WRITE setCountInChoice NOTIFY recordingChanged)  // the count-in before a recording: 1..6 bars, or -1..-3 for 1/4..3/4 of a bar in beats
    Q_PROPERTY(int sampleRateHz READ sampleRateHz NOTIFY projectChanged)  // the project's sample rate
    Q_PROPERTY(QString audioOutput READ audioOutput NOTIFY audioSettingsChanged)    // the chosen devices ("" = the system's default)
    Q_PROPERTY(QString audioInput READ audioInput NOTIFY audioSettingsChanged)
    Q_PROPERTY(int audioBufferSize READ audioBufferSize NOTIFY audioSettingsChanged)
    Q_PROPERTY(int inputChannels READ inputChannels NOTIFY audioSettingsChanged)    // of the open device
    Q_PROPERTY(double deviceRate READ deviceRate NOTIFY audioSettingsChanged)       // 0 when no device is open
    // Record > Overlapping Audio / MIDI Recordings > Cycle On: what a cycle recording makes of its passes. "takes": the passes are takes of one passage
    // on the armed track (the last plays); "tracks": each pass after the first goes to a new track made like the armed one; "tracksMute": the same and
    // the earlier tracks are muted. MIDI also has "merge": all the passes are one region.
    Q_PROPERTY(QString overlapAudio READ overlapAudio WRITE setOverlapAudio NOTIFY recordingChanged)
    Q_PROPERTY(QString overlapMidi READ overlapMidi WRITE setOverlapMidi NOTIFY recordingChanged)
    Q_PROPERTY(bool lowLatencyMonitoring READ lowLatencyMonitoring WRITE setLowLatencyMonitoring NOTIFY recordingChanged)  // Record > Low Latency Monitoring Mode
    Q_PROPERTY(bool useMusicalGrid READ useMusicalGrid WRITE setUseMusicalGrid NOTIFY recordingChanged)  // Record > Use Musical Grid: new audio takes follow the tempo (musical time)
    Q_PROPERTY(QString recordButtonMode READ recordButtonMode WRITE setRecordButtonMode NOTIFY recordingChanged)  // "toggle" (Record/Record Toggle) or "repeat" (Record/Record Repeat)
    Q_PROPERTY(bool allowQuickPunch READ allowQuickPunch WRITE setAllowQuickPunch NOTIFY punchChanged)  // Record while playing starts a take
    Q_PROPERTY(bool punchEnabled READ punchEnabled WRITE setPunchEnabled NOTIFY punchChanged)  // Autopunch: only the range is kept
    Q_PROPERTY(double punchStartBeats READ punchStartBeats NOTIFY punchChanged)
    Q_PROPERTY(double punchEndBeats READ punchEndBeats NOTIFY punchChanged)
    Q_PROPERTY(int recordingDelay READ recordingDelay WRITE setRecordingDelay NOTIFY audioSettingsChanged)  // samples
    Q_PROPERTY(QString effectEditorTrack READ effectEditorTrack NOTIFY effectEditorChanged)  // the insert whose editor is open ("" = none)
    Q_PROPERTY(int effectEditorIndex READ effectEditorIndex NOTIFY effectEditorChanged)
    Q_PROPERTY(QStringList midiInputsChosen READ midiInputsChosen NOTIFY midiChanged)  // the MIDI inputs in use (empty: all)
    Q_PROPERTY(int midiOpenCount READ midiOpenCount NOTIFY midiChanged)
    Q_PROPERTY(int waveformZoom READ waveformZoom WRITE setWaveformZoom NOTIFY waveformZoomChanged)  // vertical zoom of the waveforms: 1, 2, 4 or 8
    Q_PROPERTY(bool groupsActive READ groupsActive WRITE setGroupsActive NOTIFY groupsChanged)  // Mix > Groups Active (Shift-G)
    Q_PROPERTY(int groupsRevision READ groupsRevision NOTIFY groupsChanged)
    Q_PROPERTY(bool preFaderMetering READ preFaderMetering WRITE setPreFaderMetering NOTIFY meteringChanged)  // the meters read before the fader
    Q_PROPERTY(bool showHiddenTracks READ showHiddenTracks WRITE setShowHiddenTracks NOTIFY showHiddenTracksChanged)  // Track > Toggle Hide View
    Q_PROPERTY(int peaksRevision READ peaksRevision NOTIFY peaksChanged)  // bumps whenever a meter moved
    Q_PROPERTY(jad::TrackListModel* tracks READ tracks CONSTANT)
    Q_PROPERTY(jad::RegionModel* regions READ regions CONSTANT)
    Q_PROPERTY(jad::MixerModel* mixer READ mixer CONSTANT)
    Q_PROPERTY(jad::InspectorModel* inspector READ inspector CONSTANT)
    Q_PROPERTY(jad::LibraryModel* library READ library CONSTANT)
    Q_PROPERTY(jad::PluginsModel* plugins READ plugins CONSTANT)
    Q_PROPERTY(bool pluginManagerOpen READ pluginManagerOpen WRITE setPluginManagerOpen NOTIFY panelsChanged)
    Q_PROPERTY(bool inspectorVisible READ inspectorVisible WRITE setInspectorVisible NOTIFY panelsChanged)
    Q_PROPERTY(bool libraryVisible READ libraryVisible WRITE setLibraryVisible NOTIFY panelsChanged)
    Q_PROPERTY(bool smartControlsVisible READ smartControlsVisible WRITE setSmartControlsVisible NOTIFY panelsChanged)
    Q_PROPERTY(double leftColumnWidth READ leftColumnWidth WRITE setLeftColumnWidth NOTIFY panelsChanged)
    Q_PROPERTY(double smartControlsHeight READ smartControlsHeight WRITE setSmartControlsHeight NOTIFY panelsChanged)
    Q_PROPERTY(double mixerHeight READ mixerHeight WRITE setMixerHeight NOTIFY panelsChanged)       // the docked Mixer
    Q_PROPERTY(bool mixerDetached READ mixerDetached WRITE setMixerDetached NOTIFY panelsChanged)  // shown in its own window

public:
    explicit ProjectController(QObject* parent = nullptr);
    explicit ProjectController(bool openAudioDevice, QObject* parent = nullptr);
    ~ProjectController() override;

    bool hasProject() const { return host_ != nullptr; }
    bool selectedRecordArm() const { return selectedToggle(QStringLiteral("track.recordArm")); }
    bool selectedInputMonitor() const { return selectedToggle(QStringLiteral("track.inputMonitor")); }
    int routingRevision() const { return routingRevision_; }
    bool selectedFrozen() const {
        const TrackRow* t = selectedTracks_.isEmpty() ? nullptr : tracks_.find(selectedTracks_.first());
        return t && t->frozen;
    }
    bool selectedShowInTracks() const {
        const TrackRow* t = selectedTracks_.isEmpty() ? nullptr : tracks_.find(selectedTracks_.first());
        return !t || t->showInTracks;
    }
    bool selectedCanHide() const {  // any track but the master
        const TrackRow* t = selectedTracks_.isEmpty() ? nullptr : tracks_.find(selectedTracks_.first());
        return t && !t->master;
    }
    QString projectName() const { return name_; }
    double bpm() const { return bpm_; }
    int beatsPerBar() const { return beatsPerBar_; }
    double barBeats() const { return beatsPerBar_ * 4.0 / beatUnit_; }
    QString projectKey() const { return key_.endsWith(" major") ? key_.chopped(5) + "maj" : (key_.endsWith(" minor") ? key_.chopped(5) + "min" : key_); }
    QString signatureText() const { return QStringLiteral("%1/%2").arg(beatsPerBar_).arg(beatUnit_); }
    double masterGainDb() const { return masterGain_; }
    bool mixerVisible() const { return mixerVisible_; }
    QString tool() const { return tool_; }
    void setTool(const QString& tool);  // pointer, pencil, eraser, scissors or glue; anything else is ignored
    QString snap() const { return snap_; }
    QString dragMode() const { return dragMode_; }
    bool automationFollowsRegions() const { return automationFollows_; }
    void setAutomationFollowsRegions(bool on) { if (on == automationFollows_) return; automationFollows_ = on; QSettings().setValue("edit/automationFollows", on); emit dragModeChanged(); }
    void setDragMode(const QString& mode);
    void setSnap(const QString& snap);  // off, bar, half, quarter, eighth or sixteenth; anything else is ignored
    double snapBeats() const;           // the grid in beats (quarter notes); 0 when snapping is off
    bool followPlayhead() const { return followPlayhead_; }
    void setFollowPlayhead(bool on) {
        if (on == followPlayhead_) return;
        followPlayhead_ = on;
        emit followPlayheadChanged();
    }
    QStringList selectedTrackIds() const { return selectedTracks_; }
    QStringList selectedRegionIds() const { return selectedRegions_; }
    int trackHeightIndex() const { return trackHeightIndex_; }
    void setTrackHeightIndex(int index) {
        const int clamped = index < 0 ? 0 : (index > 3 ? 3 : index);
        if (clamped == trackHeightIndex_) return;
        trackHeightIndex_ = clamped;
        emit trackHeightChanged();
    }
    void setMixerVisible(bool visible) {
        if (visible == mixerVisible_) return;
        mixerVisible_ = visible;
        emit mixerVisibleChanged();
    }
    bool playing() const { return playing_; }
    double positionSeconds() const { return positionSeconds_; }
    double positionBeats() const { return positionBeats_; }
    bool loopEnabled() const { return loop_; }
    double loopStartBeats() const { return loopStartBeats_; }
    double loopEndBeats() const { return loopEndBeats_; }
    bool degraded() const;
    QString deviceError() const { return deviceError_; }
    QString lastError() const { return lastError_; }
    double masterPeak() const { return peak_; }
    int peaksRevision() const { return peaksRevision_; }
    bool metronomeOn() const { return metronome_; }
    bool showHiddenTracks() const { return showHidden_; }
    void setShowHiddenTracks(bool on);
    // Track > Hide Selected Track, Hide Unselected Tracks, Unhide All Tracks (one undo step each); hidden tracks still play and stay in the Mixer
    Q_INVOKABLE void hideSelectedTracks();
    Q_INVOKABLE void hideUnselectedTracks();
    Q_INVOKABLE void unhideAllTracks();
    Q_INVOKABLE void sortTracks(const QString& by);  // "name", "type" or "color": every track but the master is put in that order (one step)
    Q_INVOKABLE void setSelectedTracksColor(const QString& color);  // Track > Assign Track Color (one step)
    bool preFaderMetering() const { return preFader_; }
    void setPreFaderMetering(bool on) { if (on != preFader_) { preFader_ = on; emit meteringChanged(); } }
    bool groupsActive() const { return groupsActive_; }
    void setGroupsActive(bool on) { if (on != groupsActive_) { groupsActive_ = on; ++groupsRevision_; emit groupsChanged(); } }
    int groupsRevision() const { return groupsRevision_; }
    // Track groups: tracks whose volume, pan, mute and solo (and selection) move together; every change is one undo step
    Q_INVOKABLE QVariantList groups() const;                          // {id, name, members, volume, pan, mute, solo, selection}
    Q_INVOKABLE QVariantMap trackGroup(const QString& trackId) const;  // the group of a track ({} when none)
    Q_INVOKABLE void createGroup(const QStringList& trackIds, const QString& name = QString());
    Q_INVOKABLE void createGroupFromSelection();
    Q_INVOKABLE void setTrackGroup(const QString& trackId, const QString& groupId);  // "" takes it out; "new" starts a group with it
    Q_INVOKABLE void setGroupField(const QString& groupId, const QString& field, const QVariant& value);  // name, volume, pan, mute, solo, selection
    Q_INVOKABLE void deleteGroup(const QString& groupId);
    // Automation modes: "off" (the lanes are ignored), "read", and "touch" (fader moves are written while the fader is held), "latch" (from the
    // first touch until the transport stops), "write" (the fader is written all the time the project plays). Written moves replace the
    // lane between their first and last point and are one undo step.
    Q_INVOKABLE void setAutomationMode(const QString& trackId, const QString& mode);
    Q_INVOKABLE QString automationMode(const QString& trackId) const;
    Q_INVOKABLE void deleteAutomationOfSelected();  // Mix > Delete Automation: every lane of the selected tracks (one step)
    Q_INVOKABLE void createTrackAutomation();       // Mix > Create Track Automation: shows the lanes and takes the selected tracks out of Off
    // Group Settings window: for the group of this track ("" closes it)
    Q_PROPERTY(QString groupSettingsTrack READ groupSettingsTrack NOTIFY groupSettingsChanged)
    QString groupSettingsTrack() const { return groupSettingsTrack_; }
    Q_INVOKABLE void openGroupSettings(const QString& trackId) { groupSettingsTrack_ = trackId; emit groupSettingsChanged(); }
    int waveformZoom() const { return waveformZoom_; }
    void setWaveformZoom(int z) { if ((z == 1 || z == 2 || z == 4 || z == 8) && z != waveformZoom_) { waveformZoom_ = z; emit waveformZoomChanged(); } }
    QStringList midiInputsChosen() const { return midiChosen_; }
    int midiOpenCount() const;
    Q_INVOKABLE QStringList midiInputNames() const;   // the MIDI input devices of the system
    Q_INVOKABLE void setMidiInputs(const QStringList& names);
    // A note played from the screen (musical typing, the on-screen keyboard): it plays the live target and is recorded like a device's note
    Q_INVOKABLE void playNote(int note, int velocity, bool on);
    Q_INVOKABLE QString liveTargetTrack() const { return liveTarget_; }  // the instrument track that sounds the live MIDI
    void loadMidiSettings(QSettings& s);
    void loadIoSettings(QSettings& s);
    void saveIoSettings(QSettings& s) const;
    void saveMidiSettings(QSettings& s) const;
    QString effectEditorTrack() const { return effectEditorTrack_; }
    int effectEditorIndex() const { return effectEditorIndex_; }
    Q_INVOKABLE void openEffectEditor(const QString& trackId, int index);
    Q_INVOKABLE void closeEffectEditor();
    Q_INVOKABLE QVariantList effectSpecs() const;                    // the built-in effects and their parameters (effect_specs.h)
    Q_INVOKABLE QVariantList instrumentSpecs() const;                // the built-in instruments and their parameters
    // An instrument track's instrument: the choice (params start at their defaults) and one parameter of it (one undo step each)
    Q_INVOKABLE void setInstrument(const QString& trackId, const QString& processorId);
    Q_INVOKABLE void setInstrumentParam(const QString& trackId, const QString& param, double value);
    // A VST3 instrument for an instrument track (its window is openPluginEditor(trackId, -1)); label is the name shown in the slot
    Q_INVOKABLE void setInstrumentPlugin(const QString& trackId, const QString& pluginId, const QString& label);
    Q_INVOKABLE QVariantMap trackInstrument(const QString& trackId) const;  // {processorId, params}
    Q_INVOKABLE QVariantList trackInserts(const QString& trackId) const;  // the inserts of a track, with their parameters
    Q_INVOKABLE QString trackName(const QString& trackId) const;
    // the Channel EQ's response in dB at `points` frequencies from minHz to maxHz (log spaced) for the parameters in `values`
    Q_INVOKABLE QVariantList eqCurve(const QVariantMap& values, int points, double minHz, double maxHz) const;
    int sampleRateHz() const { return sampleRate_; }
    QString audioOutput() const { return audioOutput_; }
    QString audioInput() const { return audioInput_; }
    int audioBufferSize() const { return audioBuffer_; }
    int inputChannels() const;
    double deviceRate() const;
    // Preferences > Audio: the devices of the system and what the output offers: {outputs, inputs, rates, buffers, currentOutput, currentInput, ...}
    Q_INVOKABLE QVariantMap audioDevices(const QString& output, const QString& input) const;
    // Chooses the devices and the buffer size: remembered, and the open project's audio device is reopened with them.
    Q_INVOKABLE void applyAudioSettings(const QString& output, const QString& input, int bufferSize);
    void loadAudioSettings(QSettings& s);
    void saveAudioSettings(QSettings& s) const;
    // tests: replaces the list of devices (the real one asks the system)
    void setDeviceListerForTest(std::function<lpc::AudioDeviceChoices(const std::string&, const std::string&)> lister) { deviceLister_ = std::move(lister); }
    bool recording() const { return recording_; }
    bool punchEnabled() const { return punchEnabled_; }
    bool allowQuickPunch() const { return allowQuickPunch_; }
    QString overlapAudio() const { return overlapAudio_; }
    QString overlapMidi() const { return overlapMidi_; }
    void setOverlapAudio(const QString& mode);
    void setOverlapMidi(const QString& mode);
    bool lowLatencyMonitoring() const { return lowLatency_; }
    void setLowLatencyMonitoring(bool on);
    bool useMusicalGrid() const { return musicalGrid_; }
    void setUseMusicalGrid(bool on);
    QString recordButtonMode() const { return recordButtonMode_; }
    void setRecordButtonMode(const QString& mode);
    // Record > Record Button Options > Discard Recording and Return to Last Play Position: the take in progress is thrown away, the transport stops.
    Q_INVOKABLE void discardRecording();
    void setAllowQuickPunch(bool on) { if (on != allowQuickPunch_) { allowQuickPunch_ = on; emit punchChanged(); } }
    void setPunchEnabled(bool on);
    double punchStartBeats() const { return punchStartBeats_; }
    double punchEndBeats() const { return punchEndBeats_; }
    Q_INVOKABLE void setPunchRange(double startBeats, double endBeats);
    int recordingDelay() const { return recordingDelay_; }
    void setRecordingDelay(int samples);
    // The input a track records: 0 = inputs 1 and 2 as a stereo take, n = input n as a mono take (a command, with undo)
    Q_INVOKABLE int trackInput(const QString& trackId) const;
    Q_INVOKABLE void setTrackInput(const QString& trackId, int input);
    Q_INVOKABLE QStringList inputChoices() const;  // "Input 1", ... for the open device
    bool countInEnabled() const { return countIn_; }
    // On: an armed audio track plays its input while the project is stopped or recording, and its recorded audio while it plays. Off: only the I button monitors.
    bool autoInputMonitoring() const { return autoInput_; }
    void setAutoInputMonitoring(bool on);
    void setCountInEnabled(bool on);
    int countInChoice() const { return countInChoice_; }
    void setCountInChoice(int choice);
    // Records the input of the audio device into the first record-armed audio track from the playhead, after the count-in when it is on;
    // stop() ends it and the take becomes a region (one undo step) in that track.
    Q_INVOKABLE void startRecording();
    Q_INVOKABLE void toggleRecording();
    lpc::audio::AudioEngine* engineForTest() const { return engine_.get(); }  // the tests drive the engine by hand
    QString clickMode() const { return QString::fromStdString(clickSettings_.mode); }
    void setClickMode(const QString& mode);
    QString clickGrouping() const { return QString::fromStdString(clickSettings_.grouping); }
    void setClickGrouping(const QString& grouping);
    int clickRevision() const { return clickRevision_; }
    // The sound of one slot (1..32: the beat numbers, 33 e, 34 &, 35 a, 36 la, 37 li): a WAV file of the user's, or none (built-in click)
    Q_INVOKABLE QString clickSlotFile(int slot) const;
    Q_INVOKABLE void setClickSlotFile(int slot, const QUrl& file);
    Q_INVOKABLE void clearClickSlot(int slot);
    Q_INVOKABLE void setClickSlotGain(int slot, double gain);
    Q_INVOKABLE double clickSlotGain(int slot) const;
    Q_INVOKABLE static QString clickSlotName(int slot);
    void loadClickSettings(QSettings& s);
    void saveClickSettings(QSettings& s) const;
    Q_INVOKABLE void setMetronome(bool on);
    bool automationVisible() const { return automationVisible_; }
    void setAutomationVisible(bool on);
    QString automationParam() const { return automationParam_; }
    bool hasMarquee() const { return marqueeTo_ > marqueeFrom_; }
    QVariantMap marquee() const { return {{"from", marqueeFrom_}, {"to", marqueeTo_}, {"row0", marqueeRow0_}, {"row1", marqueeRow1_}}; }
    Q_INVOKABLE void setMarquee(double fromBeats, double toBeats, int row0, int row1);
    Q_INVOKABLE void clearMarquee();
    // what: "select" (cut at the edges, select the pieces inside), "split", "copy", "cut" or "delete"
    Q_INVOKABLE void marqueeAction(const QString& what);
    bool automationQuickAccess() const { return aqa_; }
    void setAutomationQuickAccess(bool on);
    int quickAccessController() const { return aqa_ && aqaCc_ < 0 ? -2 : aqaCc_; }
    bool autoselectAutomationParam() const { return autoselect_; }
    void setAutoselectAutomationParam(bool on);
    Q_INVOKABLE void learnQuickAccessController();             // the next controller that moves is the one
    Q_INVOKABLE void quickAccessMessage(int controller, int value);  // from the MIDI thread (queued)
    void setAutomationParam(const QString& param);
    // param: "volume", "pan" or "send1".."send4" (the first sends of the track, a send's level in dB)
    Q_INVOKABLE QVariantList automationPoints(const QString& trackId, const QString& param) const;  // {beats, value}; follows `revision`
    Q_INVOKABLE bool automationAvailable(const QString& trackId, const QString& param) const;
    int automationRevision() const { return automationRevision_; }
    // The lane a track shows: its own choice (a plug-in parameter, a send) or the one of the whole view
    Q_INVOKABLE QString automationParamFor(const QString& trackId) const;
    Q_INVOKABLE void setTrackAutomationParam(const QString& trackId, const QString& param);
    Q_INVOKABLE QString automationLabel(const QString& trackId, const QString& param) const;   // "Volume", "Send 1", "EQ: Gain" ...
    // What a track's lane can show: [{param, label}]: Volume, Pan, its sends and the automatable parameters of its VST3 inserts that are loaded
    Q_INVOKABLE QVariantList automationChoices(const QString& trackId) const;      // false for a send the track does not have
    Q_INVOKABLE void setAutomationPoints(const QString& trackId, const QString& param, const QVariantList& points);
    // Customize Control Bar and Display / Customize Toolbar: the parts of the bars that are shown ("cb.panels", "cb.transport", "cb.lcd", "cb.modes",
    // "cb.master", "tb.menus", "tb.tools", "tb.snap", "tb.heights", "tb.zoom", "tb.undo"); every part is on until the user turns it off. Kept in the settings.
    Q_PROPERTY(int barItemsRevision READ barItemsRevision NOTIFY barsChanged)
    int barItemsRevision() const { return barItemsRevision_; }
    Q_INVOKABLE bool barItem(const QString& key) const { return !barItemsOff_.contains(key); }
    Q_INVOKABLE void setBarItem(const QString& key, bool shown);
    Q_INVOKABLE void resetBarItems();
    bool quickHelpVisible() const { return quickHelp_; }
    bool dirty() const;
    // Autosave: every two minutes a project with unsaved changes is copied to project.autosave.json (not while recording or bouncing). A project that is opened
    // with a newer autosave beside it (the app stopped without saving) says so with autosaveFound; restoreAutosave puts it in place and opens the project again.
    Q_INVOKABLE void autosaveNow();
    Q_INVOKABLE bool restoreAutosave();
    Q_INVOKABLE void discardAutosave();   // the changes are not wanted (Don't Save): the autosave goes too
    void setAutosaveIntervalForTest(int ms) { autosaveTimer_.setInterval(ms); }
    void setQuickHelpVisible(bool on) { if (on == quickHelp_) return; quickHelp_ = on; QSettings().setValue("panels/quickHelp", on); ++barItemsRevision_; emit barsChanged(); }
    bool controlBarVisible() const { return controlBarVisible_; }
    void setControlBarVisible(bool on);
    bool toolbarVisible() const { return toolbarVisible_; }
    void setToolbarVisible(bool on);
    // Window > Open Project Audio: the audio files of the project, {name, path, seconds, sampleRate, channels, used (regions that play it)}
    Q_INVOKABLE QVariantList projectAudio() const;
    // View > Note Pads: free text kept with the project in notes.txt
    Q_INVOKABLE QString projectNotes() const;
    Q_INVOKABLE void setProjectNotes(const QString& text);
    bool globalTracksVisible() const { return globalTracksVisible_; }
    void setGlobalTracksVisible(bool on);
    // the global tracks; every list follows `revision`
    Q_INVOKABLE QVariantList markers() const;           // {id, beats, name}, sorted
    Q_INVOKABLE QVariantList tempoEvents() const;       // {beats, bpm}
    Q_INVOKABLE QVariantList signatureEvents() const;   // {beats, numerator, denominator}
    Q_INVOKABLE void addMarker(double beats, const QString& name = QString());
    Q_INVOKABLE void createMarkerAtPlayhead();
    Q_INVOKABLE void moveMarker(const QString& id, double beats);
    Q_INVOKABLE void renameMarker(const QString& id, const QString& name);
    Q_INVOKABLE void removeMarker(const QString& id);
    Q_INVOKABLE void setTempoAt(double beats, double bpm);
    Q_INVOKABLE void removeTempoAt(double beats);
    Q_INVOKABLE void setSignatureAt(double beats, int numerator, int denominator);
    Q_INVOKABLE void removeSignatureAt(double beats);
    // the level a strip's meter shows (falls back after a peak) and the highest level since the last reset (the peak field); linear
    Q_INVOKABLE double trackPeak(const QString& trackId) const { return meter_.value(trackId); }
    Q_INVOKABLE double trackHold(const QString& trackId) const { return hold_.value(trackId); }
    Q_INVOKABLE double trackReduction(const QString& trackId) const { return reduction_.value(trackId); }  // dB of gain reduction now
    Q_INVOKABLE void resetPeaks();
    bool anySolo() const {
        for (const TrackRow& t : allRows_)
            if (t.solo) return true;
        return false;
    }
    bool audioEnabled() const { return openAudioDevice_; }
    void setAudioEnabled(bool enabled) {
        if (enabled == openAudioDevice_) return;
        openAudioDevice_ = enabled;
        emit audioEnabledChanged();
    }
    TrackListModel* tracks() { return &tracks_; }
    RegionModel* regions() { return &regions_; }
    MixerModel* mixer() { return &mixer_; }
    InspectorModel* inspector() { return &inspector_; }
    LibraryModel* library() { return &library_; }
    PluginsModel* plugins() { return &plugins_; }
    bool pluginManagerOpen() const { return pluginManagerOpen_; }
    void setPluginManagerOpen(bool on) { if (on != pluginManagerOpen_) { pluginManagerOpen_ = on; emit panelsChanged(); } }
    bool inspectorVisible() const { return inspectorVisible_; }
    bool libraryVisible() const { return libraryVisible_; }
    bool smartControlsVisible() const { return smartControlsVisible_; }
    double leftColumnWidth() const { return leftColumnWidth_; }
    double smartControlsHeight() const { return smartControlsHeight_; }
    double mixerHeight() const { return mixerHeight_; }
    bool mixerDetached() const { return mixerDetached_; }
    void setInspectorVisible(bool on) { if (on != inspectorVisible_) { inspectorVisible_ = on; emit panelsChanged(); } }
    void setLibraryVisible(bool on) { if (on != libraryVisible_) { libraryVisible_ = on; emit panelsChanged(); } }
    void setSmartControlsVisible(bool on) { if (on != smartControlsVisible_) { smartControlsVisible_ = on; emit panelsChanged(); } }
    void loadPanelState(QSettings& settings);        // missing keys keep the defaults; sizes go through the clamps
    void savePanelState(QSettings& settings) const;
    void setLeftColumnWidth(double width);       // clamped to 200..320, NaN ignored
    void setSmartControlsHeight(double height);  // clamped to 120..320, NaN ignored
    void setMixerHeight(double height);          // clamped to 584..1400 (the strips with their legend), NaN ignored
    void setMixerDetached(bool detached);        // detaching also shows the Mixer

    Q_INVOKABLE bool openProject(const QUrl& folder);
    Q_INVOKABLE bool newProject(const QUrl& folder);
    Q_INVOKABLE bool saveProject();
    // Save As / Save a Copy As: the whole project folder (media included) is written to `folder`, which must be empty or new;
    // Save As then opens the copy, a copy leaves the current project open.
    Q_INVOKABLE bool saveProjectAs(const QUrl& folder, bool openCopy);
    // File > Import Audio File: into the selected audio track (else the first one) at the playhead
    // File > Bounce: the whole project is rendered offline, without the audio device, to a 24-bit WAV file (plug-in inserts are skipped)
    Q_INVOKABLE void bounceProject(const QUrl& file);
    // options: format "wav16" | "wav24" | "wav32" (float) | "aiff16" | "aiff24"; range "project" | "cycle"; normalize (bool, to -0.3 dBFS);
    // tail (seconds of tail after the last region, 0..30); dither (bool, for 16-bit)
    Q_INVOKABLE void bounceProjectAs(const QUrl& file, const QVariantMap& options);
    // Audio files dropped (or chosen) at a position: onto an audio track they go on that track, back to back; anywhere else each file
    // makes a new audio track named after the file. WAV, MP3, FLAC and AIFF; other rates are converted to the project's.
    Q_INVOKABLE void importAudioFilesAt(const QList<QUrl>& files, const QString& trackId, double startBeats);
    Q_INVOKABLE void importAudioFilesHere(const QList<QUrl>& files);
    // File > Import > MIDI File: one new instrument track with a region per channel of the file, at the playhead
    Q_INVOKABLE void importMidiFile(const QUrl& file);
    // File > Export > Selected Regions as MIDI File (the MIDI regions selected, or every instrument track when none is)
    Q_INVOKABLE void exportMidiFile(const QUrl& file);
    Q_INVOKABLE void submit(const QString& commandJson);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void play();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void locateBeats(double beats);
    Q_INVOKABLE void locateSeconds(double seconds);
    Q_INVOKABLE void setLoopBeats(double startBeats, double endBeats);
    // The cycle area: the range moves or resizes and the cycle keeps its on/off state; toggleLoop() flips it.
    Q_INVOKABLE void setLoopRange(double startBeats, double endBeats);
    Q_INVOKABLE void toggleLoop();
    // Navigate: the cycle from the selected regions (rounded: out to whole bars) with the cycle switched on; move it by its own length
    Q_INVOKABLE void setLocatorsBySelection(bool rounded);
    // Navigate > Auto Set Locators: the cycle is set to the selected regions, or to everything on the tracks when none is selected.
    Q_INVOKABLE void autoSetLocators();
    // Edit > Trim > Fill within Locators: the selected regions inside the locators are lengthened up to the next selected region of their track (the last one stays).
    Q_INVOKABLE void fillWithinLocators();
    // Edit > Cut/Insert Time: the section between the locators is cut out (the rest moves left) or made room for with silence (the rest moves right). The
    // selected regions only, or all of them when none is selected. Markers, tempo and automation stay where they are.
    Q_INVOKABLE void cutSectionBetweenLocators();
    Q_INVOKABLE void insertSilenceBetweenLocators();
    // Edit > Delete MIDI Events: kind "duplicates" (same position and pitch: one stays), "inside" or "outside" the locators; in the selected MIDI regions (all when none).
    Q_INVOKABLE void deleteMidiEvents(const QString& kind);
    // Edit > Separate MIDI Events > by Note Pitch: each pitch of the selected MIDI regions goes to a region of its own on a new track made like the first.
    Q_INVOKABLE void separateMidiByPitch();
    Q_INVOKABLE void moveSelectedToFocusedTrack();  // Edit > Move > To Focused Track: the selected track; the regions keep their time
    Q_INVOKABLE void joinPerTracks();
    // LCD > Key: "C", "c#", "Am", "F# min", "Bb major" ... (flats become sharps); false when the text is no key.
    Q_INVOKABLE bool setProjectKey(const QString& text);
    // File > Project Alternatives: named copies of the saved project. New saves the project first; Open saves it and the current state as "Before <name>", then
    // replaces the project by the alternative.
    Q_INVOKABLE QStringList projectAlternatives() const;
    Q_INVOKABLE bool newProjectAlternative(const QString& name);
    Q_INVOKABLE bool openProjectAlternative(const QString& name);
    Q_INVOKABLE void deleteProjectAlternative(const QString& name);
    // File > Project Management
    Q_INVOKABLE void showProjectFolder();
    Q_INVOKABLE QVariantMap unusedMedia() const;  // {count, bytes} of the audio files that no region and no frozen track uses
    Q_INVOKABLE void cleanUpProject();            // removes them from the project and from the disk; the undo history goes (the files would be missing)
    // Track > Assign Track Icon: the picture in the header of the selected tracks ("" removes it). The keys are those of trackIconChoices().
    Q_INVOKABLE void setSelectedTracksIcon(const QString& icon);
    Q_INVOKABLE QStringList trackIconChoices() const;
    Q_INVOKABLE void requestTrackIconDialog() { emit trackIconDialogRequested(); }  // the Inspector's icon button asks Main to show the dialog
    // Track > Show Output Track: the track the selected one plays into (a bus or aux that lives in the Mixer only is shown first) is selected.
    Q_INVOKABLE void showOutputTrack();
    // Window > Show Step Input Keyboard: a note is put at the playhead, `stepBeats` long, in the selected MIDI region that holds the playhead (a new one-bar region
    // on the selected or armed instrument track when none does); the playhead then moves on by the step unless `chord` (the next note joins this one).
    Q_INVOKABLE void stepInputNote(int note, int velocity, double stepBeats, bool chord);
    Q_INVOKABLE void stepInputMove(double stepBeats);  // a rest (positive) or a step back (negative)
    // Edit > Copy MIDI Events: the events between the locators of the selected MIDI regions (the MIDI regions of the selected track when none is selected) are
    // copied or moved to the playhead on `destTrackId` (empty: the selected track, else the same track). `mode`: copyMerge, copyReplace, copyInsert, moveMerge,
    // moveReplace or moveInsert. Merge blends them with what is there, Replace first clears the destination range, Insert pushes what follows to the right.
    Q_INVOKABLE void copyMidiEvents(const QString& mode, const QString& destTrackId = QString());
    Q_INVOKABLE QVariantList midiTrackChoices() const;  // [{id, name}] of the tracks that hold MIDI regions
    // Edit > Move > To Recorded Position: audio regions recorded in this project go back to where they were recorded.
    Q_INVOKABLE void moveSelectedToRecordedPosition();
    // Edit > Move > To Beat / First Transient to Nearest Beat: the first attack in each selected audio region is moved onto the nearest beat of the grid.
    Q_INVOKABLE void moveFirstTransientToNearestBeat();
    Q_INVOKABLE void openSelectedInExternalEditor();   // Edit > Open in External Sample Editor: the audio file in the program the system opens it with               // Edit > Bounce and Join > Join per Tracks
    Q_INVOKABLE void selectAllTracks();  // Edit > Select Tracks
    Q_INVOKABLE void moveLocators(int direction);
    Q_INVOKABLE void deleteMarkerAtPlayhead();
    Q_INVOKABLE void clearError();
    // The LCD: tempo (clamped to 20..999, NaN ignored) and time signature at tick 0, master volume, bar steps.
    Q_INVOKABLE void setTempo(double bpm);
    Q_INVOKABLE void setSignature(int numerator, int denominator);
    Q_INVOKABLE void setMasterGain(double db);
    Q_INVOKABLE void barBack();
    Q_INVOKABLE void barForward();
    // Selection lives here so that menus, shortcuts and the views agree. `mode` is "replace", "extend" or "toggle";
    // ids that do not exist are ignored. Selecting regions does not touch the track selection and vice versa.
    Q_INVOKABLE void selectTrack(const QString& id, const QString& mode);
    Q_INVOKABLE void selectRegion(const QString& id, const QString& mode);
    Q_INVOKABLE void selectRegions(const QStringList& ids, const QString& mode);
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void selectAll();  // every region
    // Track management: one command each (deleting several tracks is one transaction, one undo step).
    Q_INVOKABLE void addTrack(const QString& kind);  // "audio", "instrument" or "bus"
    Q_INVOKABLE void deleteSelectedTracks();
    Q_INVOKABLE void renameTrack(const QString& trackId, const QString& name);
    Q_INVOKABLE void setTrackColor(const QString& trackId, const QString& color);
    Q_INVOKABLE void toggleMuteSelected();
    // Region tools. Positions are in beats, snapped to the grid and clamped before a command is built; the Core's
    // refusals (a split outside the region, a join of regions that do not touch) show up in lastError.
    Q_INVOKABLE void createRegion(const QString& trackId, double startBeats, double lengthBeats);  // empty MIDI region
    Q_INVOKABLE void splitRegion(const QString& regionId, double atBeats);
    Q_INVOKABLE void joinRegions(const QStringList& regionIds);
    Q_INVOKABLE void resizeRegion(const QString& regionId, double startBeats, double lengthBeats);  // snapping is done by the caller
    Q_INVOKABLE void resizeSelectedRegions(const QString& regionId, double startBeats, double lengthBeats);
    Q_INVOKABLE void moveSelectedRegions(const QString& regionId, double startBeats);
    // Edit menu (project_controller_edit.cpp)
    Q_INVOKABLE void copySelectedRegions();
    Q_INVOKABLE void cutSelectedRegions();
    Q_INVOKABLE void pasteRegions(bool atOriginalPosition = false);
    Q_INVOKABLE void duplicateSelectedRegions();
    Q_INVOKABLE void repeatSelectedRegions(int copies);
    // Window > Open MIDI Transform: changes every note of the selected MIDI regions (one undo step). op: "transpose" (a = semitones), "velocityScale" (a = percent),
    // "velocityAdd" (a), "lengthScale" (a = percent), "humanize" (a = the largest timing change in 960ths of a beat, b = the largest velocity change; the same
    // choice every time for the same notes), "reverse" (the notes play backwards inside their region), "invert" (pitches mirrored around the first note).
    Q_INVOKABLE void transformNotes(const QString& op, double a, double b);
    Q_INVOKABLE void pasteReplace();                         // Edit > Paste Replace: the clipboard takes the place of the selected regions (one undo step)
    Q_INVOKABLE void shuffleSelectedRegion(int direction);   // Edit > Move > Shuffle Left (-1) / Right (+1): the region swaps places with its neighbour on the track
    Q_INVOKABLE void setSelectedRegionsLength(double beats);
    Q_INVOKABLE QVariantList trackList() const;  // {id, name, kind} of the tracks listed in the Tracks area (the master not included)
    // View > Browsers / Loop Browser: the folders and audio files of a folder, {path, parent, entries: [{name, path, dir, audio, size}]}, folders first.
    // An empty path is the user's home. standardLocations() is [{name, path}] for the sidebar (Home, Music, Desktop, Documents, the project, the project's audio).
    Q_INVOKABLE QVariantMap browseFolder(const QString& path) const;
    Q_INVOKABLE QVariantList standardLocations() const;
    Q_INVOKABLE void importAudioPath(const QString& path) { importAudioFilesHere({QUrl::fromLocalFile(path)}); }
    // Templates: projects kept in the application's config folder (Templates/<name>). saveAsTemplate keeps a copy of the open project,
    // newFromTemplate copies a template into an empty or new folder and opens it.
    Q_INVOKABLE QStringList templates() const;
    Q_INVOKABLE bool saveAsTemplate(const QString& name);
    Q_INVOKABLE bool newFromTemplate(const QString& name, const QUrl& folder);
    void setTemplatesFolderForTest(const QString& folder) { templatesFolder_ = folder; }
    // Mix > Search and Add Plug-in: what can go on the first selected track, {id, name, vendor, kind}: kind is "builtin" (an effect of the app), "effect" or
    // "instrument" (VST3). addSearchedPlugin puts it there: an effect at the end of the inserts, an instrument in the instrument slot of an instrument track.
    Q_INVOKABLE QVariantList searchablePlugins() const;
    Q_INVOKABLE void addSearchedPlugin(const QString& id, const QString& kind, const QString& name);
    Q_INVOKABLE void openAllPluginWindows();
    // Bounce in Place: each selected audio or instrument track (with its inserts, sends and the buses it feeds) is rendered to a new audio track
    // named after it, from its first region to its last plus a tail. The original stays as it is.
    Q_INVOKABLE void bounceInPlace();
    // Track > Freeze: the selected audio and instrument tracks are rendered (instrument, regions and inserts, before the fader) and play that audio instead
    // of making the sound again, which spares the processor; when they are all frozen already, they are unfrozen. One undo step.
    Q_INVOKABLE void toggleFreezeSelected();  // Mix > Show All Plug-in Windows
    Q_INVOKABLE void toggleMuteSelectedRegions();
    Q_INVOKABLE void toggleLoopSelectedRegions();
    // Takes: the regions of one take group on a track, in time order ({id, active, label}); one of them plays
    Q_INVOKABLE QVariantList regionTakes(const QString& regionId) const;
    Q_INVOKABLE void setActiveTake(const QString& regionId);        // this take plays, the others of its group are muted (one undo step)
    Q_INVOKABLE void deleteOtherTakes(const QString& regionId);     // keeps this take as an ordinary region and removes the others of its group
    Q_INVOKABLE void unpackTakes(const QString& regionId);
    Q_INVOKABLE void splitTakesAtPlayhead(const QString& regionId);  // every take of the region's passage is cut at the playhead; each side is a passage of its own (comping)          // the takes become ordinary regions (muted ones stay muted)
    // The Inspector's region fields for the selected MIDI regions (one undo step): what == "transpose" (semitones), "velocity" (added) or "quantize" (beats, 0 = off)
    Q_INVOKABLE void setSelectedRegionsMidi(const QString& what, double value);
    // The Inspector's track fields for an instrument track: what == "transpose", "velocity", "keyLow", "keyHigh", "velocityLow" or "velocityHigh"
    Q_INVOKABLE void setTrackMidi(const QString& trackId, const QString& what, int value);
    Q_INVOKABLE void setTrackDelay(const QString& trackId, double milliseconds);  // Track Delay of an audio or instrument track, -1000..1000 ms
    Q_INVOKABLE void selectFollowingRegions(bool sameTrackOnly);
    Q_INVOKABLE void selectOverlappedRegions();
    Q_INVOKABLE void selectSameColoredRegions();
    Q_INVOKABLE void selectMutedRegions();
    Q_INVOKABLE void selectInsideLocators();
    Q_INVOKABLE double projectEndBeats() const;  // the end of the last region
    Q_INVOKABLE void addTracks(const QString& kind, int count, const QString& name);  // Track > New Tracks: one undo step; names get a number
    // The fades of an audio region (beats from its start / to its end; 0 = none), one undo step
    Q_INVOKABLE void setRegionFades(const QString& regionId, double fadeInBeats, double fadeOutBeats);
    // Audio processing of the selected audio regions (the file is not changed: the result is a new file in the project, one undo step):
    // op = "normalize" (value: target dBFS), "reverse", "gain" (dB), "stretch" (value: the new length in percent, the pitch stays),
    // "pitch" (value: semitones, the length stays)
    Q_INVOKABLE void processSelectedRegions(const QString& op, double value);
    // Strip Silence: the selected audio regions are cut at the silences (below thresholdDb for at least minSilenceMs) and the silences go
    Q_INVOKABLE void stripSilence(double thresholdDb, double minSilenceMs);
    // The audio editor's Trim to Selection: the region keeps only frames [from, to) of its own part (a command)
    Q_INVOKABLE void trimRegionToFrames(const QString& regionId, double fromFrame, double toFrame);
    // Track > Create Track Stack: a new aux track that sums the selected tracks (their output goes to it), one undo step
    Q_INVOKABLE void createSummingStack(const QString& name = QString());
    // Mix > I/O Labels: your own names for the inputs of the interface ("Vocal mic" instead of "Input 3")
    Q_INVOKABLE QString inputLabel(int input) const;                 // input 1..; the name, or "Input n"
    Q_INVOKABLE void setInputLabel(int input, const QString& label);
    Q_PROPERTY(int inputLabelsRevision READ inputLabelsRevision NOTIFY inputLabelsChanged)
    int inputLabelsRevision() const { return inputLabelsRevision_; }
    // File: Close Project (the window stays, empty), Revert to Saved (the project is read again from its folder), the recent projects
    Q_INVOKABLE void closeProject();
    Q_INVOKABLE bool revertToSaved();
    Q_INVOKABLE QStringList recentProjects() const;  // folders that still hold a project, the latest first
    Q_INVOKABLE bool openRecent(const QString& folder);
    Q_INVOKABLE void setProjectName(const QString& name);
    Q_INVOKABLE QString projectFolder() const { return QString::fromStdU16String(dir_.u16string()); }
    void loadRecent(QSettings& s);
    void saveRecent(QSettings& s) const;
    // Edit > Undo History: {undo: [labels, oldest first], redo: [labels, next first]}; stepping goes to a point of it, clearing forgets it
    Q_INVOKABLE QVariantMap undoHistory() const;
    Q_INVOKABLE void undoSteps(int count);
    Q_INVOKABLE void redoSteps(int count);
    Q_INVOKABLE void clearUndoHistory();
    Q_INVOKABLE void deleteUnusedTracks();     // audio and instrument tracks without regions
    Q_INVOKABLE void deselectOutsideLocators();
    Q_INVOKABLE void selectSimilarRegions();   // the same audio file, or MIDI of the same length, as a selected region
    Q_INVOKABLE void selectEqualRegions();     // the same source, length and content as a selected region
    Q_INVOKABLE void moveSelectedToPlayhead(); // the group moves so that its first region starts at the playhead
    Q_INVOKABLE void splitAtLocators();        // the selected regions (all when none) are cut at the cycle's start and end
    Q_INVOKABLE void deleteSelectedAndMove();  // delete, and later regions on the same tracks close the gap
    Q_INVOKABLE void selectEmptyRegions();
    Q_INVOKABLE void invertRegionSelection();
    Q_INVOKABLE void selectNeighbourRegion(int direction);  // -1 previous, +1 next, on the track of the first selected region
    Q_INVOKABLE void removeOverlaps();
    Q_INVOKABLE void regionEndToNextRegion();
    Q_INVOKABLE void halveSelectedRegions();
    Q_INVOKABLE void doubleSelectedRegions();
    Q_INVOKABLE void nudgeSelectedRegions(int direction);  // by the nudge value
    Q_INVOKABLE void setNudgeBeats(double beats);
    // Edit > Move > Slip Left/Right (-1/+1): what the selected regions play moves by the nudge value while the regions stay where they are (audio: the part of the
    // file that plays; MIDI: the notes inside the region, those pushed out are dropped). Rotate Left/Right moves the notes of a MIDI region the same way but what
    // leaves at one end comes back at the other. One undo step.
    Q_INVOKABLE void slipSelectedRegions(int direction, bool rotate);
    // The Slip and Rotate tools: the same by an amount dragged (beats; positive = the content moves right).
    Q_INVOKABLE void slipRegions(const QStringList& ids, double deltaBeats, bool rotate);
    Q_INVOKABLE void renameRegion(const QString& regionId, const QString& name);  // empty: back to the track's name
    Q_INVOKABLE QString regionName(const QString& regionId) const;                // the name shown: its own, or the track's
    // Piano Roll: the notes of a MIDI region, in beats from the region start: [{start, length, note, velocity}]
    Q_INVOKABLE QVariantList regionNotes(const QString& regionId) const;
    Q_INVOKABLE void setRegionNotes(const QString& regionId, const QVariantList& notes);  // one undo step
    // Controller lanes of a MIDI region: lane is "cc<n>" (control change n, value 0..127), "bend" (pitch bend, -8192..8191) or "touch" (aftertouch,
    // 0..127). regionControls lists {beats, value}; setRegionControls replaces that lane's points (the other lanes stay), one undo step.
    Q_INVOKABLE QVariantList regionControls(const QString& regionId, const QString& lane) const;
    Q_INVOKABLE QVariantList regionControlEvents(const QString& regionId) const;  // every controller event of a MIDI region: {beats, status, data1, data2}
    Q_INVOKABLE void setRegionControls(const QString& regionId, const QString& lane, const QVariantList& points);
    Q_INVOKABLE QVariantMap regionInfo(const QString& regionId) const;  // {trackName, trackId, startBeats, lengthBeats, audio, found}
    Q_PROPERTY(int revision READ revision NOTIFY projectChanged)  // grows with every snapshot: bindings on regionNotes() follow it
    int revision() const { return static_cast<int>(snapshots_ & 0x7fffffff); }  // counts the snapshots applied, so that it changes even when the host's revision does not (a project just opened)
    Q_PROPERTY(double nudgeBeats READ nudgeBeats NOTIFY nudgeChanged)
    double nudgeBeats() const { return nudgeBeats_; }
    Q_INVOKABLE void splitSelectedAtPlayhead();  // every selected region that strictly contains the playhead, one undo step
    Q_INVOKABLE void joinSelected();             // the selected regions, which must touch one another
    // Selects the regions that overlap the rectangle (beats and timeline rows, both inclusive).
    Q_INVOKABLE void selectRegionsIn(double fromBeats, double toBeats, int fromRow, int toRow, const QString& mode);
    Q_INVOKABLE void joinWithNext(const QString& regionId);  // the region that starts where this one ends, same track
    Q_INVOKABLE void toggleSoloSelected();
    Q_INVOKABLE void setSelectedColor(const QString& color);  // every selected track, one undo step
    // The R and I stubs keep their state per track here, so it survives track changes. Other action ids are ignored.
    Q_INVOKABLE void setTrackToggle(const QString& actionId, const QString& trackId, bool on);
    // Test seam (tests have no audio device): sets the transport as the engine would report it, so that the views that
    // follow the playhead can be driven from QML. The engine is no longer polled afterwards.
    Q_INVOKABLE void simulatePlaybackForTest(bool playing, double positionBeats);
    Q_INVOKABLE bool newProjectInTempForTest();  // a new project in a fresh folder of the temp directory
    // Strip edits: one set_strip command each; values are clamped, NaN is ignored.
    Q_INVOKABLE void setGain(const QString& trackId, double db);
    Q_INVOKABLE void setPan(const QString& trackId, double pan);
    Q_INVOKABLE void setMute(const QString& trackId, bool on);
    Q_INVOKABLE void setSolo(const QString& trackId, bool on);
    Q_INVOKABLE void toggleMute(const QString& trackId);
    Q_INVOKABLE void toggleSolo(const QString& trackId);
    // Panels. Every edit is one command (or one transaction); values are clamped, NaN is ignored.
    Q_INVOKABLE void applyPatch(const QString& patchId);  // to the track the panels show; a notice when it does not fit
    Q_INVOKABLE void revertPatch();                       // applies the patch of that track again
    Q_INVOKABLE void addInsert(const QString& trackId, const QString& processorId);
    Q_INVOKABLE void removeInsert(const QString& trackId, int index);
    Q_INVOKABLE void addPlugin(const QString& trackId, const QString& pluginId, const QString& label);
    Q_INVOKABLE void openPluginEditor(const QString& trackId, int index);
    Q_INVOKABLE void setInsertState(const QString& trackId, int index, const QString& state);
    Q_INVOKABLE void setInsertParam(const QString& trackId, int index, const QString& param, double value);
    Q_INVOKABLE void addSend(const QString& trackId, const QString& targetId);
    Q_INVOKABLE void removeSend(const QString& sendId);
    Q_INVOKABLE void setSendLevel(const QString& sendId, double db);
    Q_INVOKABLE void setSendPreFader(const QString& sendId, bool on);
    Q_INVOKABLE void setOutput(const QString& trackId, const QString& outputId);  // "" = master
    // One undo step: a new bus that is hidden from the Tracks area, and a send (role "send") or the output (role "output") to it.
    // Starts plug-in hosting and the first scan when audio output is on (once). The application calls it at start-up; opening a
    // project calls it too, so a controller made without it still hosts plug-ins.
    Q_INVOKABLE void startPlugins();
    // Insert edits: one undo step each. `toTrackId` empty: inside the track; else the insert goes to that track at `to`.
    Q_INVOKABLE void moveInsert(const QString& trackId, int from, int to, const QString& toTrackId = QString());
    Q_INVOKABLE void setInsertBypass(const QString& trackId, int index, bool on);
    // The right strip of the Inspector shows this bus (or the master) instead of the output of the shown track; "" or the
    // output of the shown track puts it back. View state only: not in the project, not undoable.
    Q_INVOKABLE void showBus(const QString& busId);
    // The buses and auxes the Core accepts as a send target or output of `trackId` (no cycle, not itself): [{id, name}].
    Q_INVOKABLE QVariantList targetsFor(const QString& trackId) const;
    Q_PROPERTY(int routingRevision READ routingRevision NOTIFY routingRevisionChanged)  // changes with every snapshot
    Q_INVOKABLE void newBusFor(const QString& trackId, const QString& role);
    Q_INVOKABLE void setShowInTracks(const QString& trackId, bool on);  // buses and auxes only
    Q_INVOKABLE void setRegionGain(const QString& regionId, double db);
    Q_INVOKABLE void setSmartControl(const QString& trackId, const QString& controlId, double value);
    Q_INVOKABLE void soloExclusive(const QString& trackId);
    Q_INVOKABLE void muteAll(bool on);
    // A drag of a fader or knob: beginGesture() first, then setGainLive/setPanLive as the pointer moves (sent at most every 33 ms,
    // so that every strip, header and field follows and the sound changes at once), then the final setGain/setPan and
    // endGesture(): the whole drag is one undo step.
    Q_INVOKABLE void beginGesture();
    Q_INVOKABLE void endGesture();
    Q_INVOKABLE void setGainLive(const QString& trackId, double db);
    Q_INVOKABLE void setPanLive(const QString& trackId, double pan);
    Q_INVOKABLE void soloAll(bool on);
    Q_INVOKABLE void clearSolo();
    Q_INVOKABLE void announceStub(const QString& label);  // a visual-only control was used: the usual notice
    Q_INVOKABLE void moveRegion(const QString& regionId, double startBeats);
    Q_INVOKABLE void deleteRegions(const QStringList& regionIds);
    // Copies a 1-2 channel WAV of the project sample rate into <project>/audio and adds it as media plus a region
    // on `trackId` at `startBeats`, as one undo step. Errors go to lastError.
    Q_INVOKABLE void importAudio(const QUrl& file, const QString& trackId, double startBeats);
    // Several files: imported one after the other, back to back from `startBeats`. A bad file is reported and skipped.
    Q_INVOKABLE void importAudioFiles(const QList<QUrl>& files, const QString& trackId, double startBeats);
    // Peaks of an audio file of the project, `buckets` values in [0,1]. Empty until computed (on a worker);
    // waveformReady(mediaId) fires when the call can be repeated to get them.
    // The waveform of one region: only its own part of the media, `buckets` peaks over its length (empty while it is being read:
    // regionPeaksReady follows)
    Q_INVOKABLE QVariantList regionPeaks(const QString& regionId, int buckets);
    Q_INVOKABLE QVariantList waveformPeaks(const QString& mediaId, int buckets);

    // Same code path the change listener uses; lets tests feed snapshots in any order.
    void applySnapshotForTest(Snapshot snapshot) { applySnapshot(std::move(snapshot), generation_); }
    void applySnapshotForTest(Snapshot snapshot, std::uint64_t generation) { applySnapshot(std::move(snapshot), generation); }
    std::uint64_t generationForTest() const { return generation_; }
    void forceDegradedForTest(bool on) { forcedDegraded_ = on; }

signals:
    void projectChanged();
    void playingChanged();
    void positionChanged();
    void loopChanged();
    void degradedChanged();
    void deviceErrorChanged();
    void lastErrorChanged();
    void peakChanged();
    void peaksChanged();
    void globalTracksVisibleChanged();
    void automationViewChanged();
    void metronomeChanged();
    void barsChanged();
    void showHiddenTracksChanged();
    void meteringChanged();
    void groupsChanged();
    void recentChanged();
    void inputLabelsChanged();
    void groupSettingsChanged();
    void waveformZoomChanged();
    void midiChanged();
    void effectEditorChanged();
    void audioSettingsChanged();
    void recordingChanged();
    void punchChanged();
    void clickSettingsChanged();
    void commandSent(const QString& type);
    // A tool had nothing to do (no MIDI track, nothing selected, nothing to join): a toast, not an error.
    void notice(const QString& message);
    void audioEnabledChanged();
    void mixerVisibleChanged();
    void panelsChanged();
    void selectionChanged();
    void trackFlagsChanged();
    void routingRevisionChanged();  // the first selected track, or one of its flags, changed
    void trackTogglesChanged();
    void nudgeChanged();
    void toolChanged();
    void snapChanged();
    void dragModeChanged();
    void automationSettingsChanged();
    void marqueeChanged();
    void trackIconDialogRequested();
    void autosaveFound();  // the project that was just opened has an autosave newer than its saved file
    void followPlayheadChanged();
    void trackHeightChanged();
    // The loader refused a folder; the message is the loader's. The previous project stays open.
    void projectOpenFailed(const QString& message);
    void regionPeaksReady(const QString& regionId);
    void waveformReady(const QString& mediaId);

private:
    // `done(accepted)` runs on the Qt thread once the project thread has answered, or at once when nothing could be sent.
    void setStripField(const QString& trackId, const char* field, nlohmann::json value);
    struct PendingImport {
        QUrl file;
        QString trackId;
        double startBeats;  // NaN: right after the previous clip
        bool temporary = false;  // a file made for the import (a take): removed when it has been taken in
        QString takeGroup;       // set for the passes of a cycle recording: the regions share it
        bool muted = false;      // a take that does not play
        bool musical = false;    // the region follows the tempo (Use Musical Grid)
        bool recorded = false;   // a take: the file remembers when it was recorded (Move > To Recorded Position)
    };
    void startNextImport();
    // `done(ok, endBeats)` runs on the Qt thread when the file is in the project or has failed.
    void runImport(const QUrl& file, const QString& trackId, double startBeats, std::function<void(bool, double)> done, const QString& takeGroup = {}, bool muted = false, bool musical = false, bool recorded = false);
    void sendCommand(const nlohmann::json& command, std::function<void(bool)> done = {});
    QString inspectorBusId_, pinOwner_;  // the pinned bus of the right strip and the track it was pinned for
    int routingRevision_ = 0;
    QString selectWhenListed_;  // a track made by a command: selected as soon as a snapshot lists it
    void refresh(std::uint64_t revision);
    void applySnapshot(Snapshot snapshot, std::uint64_t generation);
    void tick();
    void pruneSelection();
    void refreshPanels();
    const TrackRow* rowOf(const QString& trackId) const;
    bool selectedToggle(const QString& actionId) const;
    void toggleSelectedFlag(const char* field, bool TrackRow::*flag);
    std::int64_t regionPosition(const RegionRow& row, double beats) const;  // beats -> the region's own unit
    // What the Drag mode asks when regions land on others: No Overlap trims or removes what lies under them, X-Fade turns the overlap of two audio
    // regions into a crossfade; `moved` is (region, new start in beats). Empty in the Overlap mode.
    nlohmann::json overlapCommands(const std::vector<std::pair<const RegionRow*, double>>& moved) const;
    QString dragMode_ = QStringLiteral("overlap");
    bool automationFollows_ = false;
    // set_automation commands that carry the automation of the moved regions' spans with them (Mix > Move Track Automation with Regions)
    nlohmann::json automationFollowCommands(const std::vector<std::pair<const RegionRow*, double>>& moved) const;
    nlohmann::json resizeCommand(const RegionRow& row, double startBeats, double lengthBeats) const;
    nlohmann::json moveCommand(const RegionRow& row, double startBeats) const;
    void setError(const QString& message);
    void setUpPlugins();       // creates the plug-in host and starts the scan; once per controller
    void refreshPluginRows();  // catalogue to the model and to the host
    void commitPluginState(const lpc::InsertSlot& slot);
    void commitPluginStates();
    void teardown();
    static std::filesystem::path toPath(const QUrl& url);

    bool openAudioDevice_;
    TrackListModel tracks_;
    RegionModel regions_;
    MixerModel mixer_;
    InspectorModel inspector_;
    LibraryModel library_;
    PluginsModel plugins_;
    bool pluginManagerOpen_ = false;
    std::shared_ptr<const lpc::PatchLibrary> patches_;  // the built-in catalogue; also read on the project thread
    std::vector<TrackRow> allRows_;                     // every track of the last snapshot, the master included
    std::vector<RegionRow> regionRows_;
    struct ClipRegion {
        RegionRow row;  // where it came from (track, position, unit)
        std::string json;
    };
    std::vector<ClipRegion> clipboard_;   // the copied regions
    QStringList pendingRegionSelection_;  // ids of regions just pasted: selected when the next snapshot has them
    double nudgeBeats_ = 0.25;
    std::vector<const RegionRow*> selectedRegionRows() const;
    void setAllStrips(const char* field, bool on);
    void flushLive();
    QTimer liveTimer_;
    bool liveWired_ = false;
    QHash<QString, double> liveGain_, livePan_;  // the latest value of each strip, waiting for the next tick
    void pasteClipboard(double offsetBeats, bool keepTrack, int copies = 1);
    bool inspectorVisible_ = true, libraryVisible_ = false, smartControlsVisible_ = false;
    double leftColumnWidth_ = 240.0, smartControlsHeight_ = 180.0, mixerHeight_ = 600.0;
    bool mixerDetached_ = false;

    std::unique_ptr<JuceInit> juce_;
#ifdef JAD_HAVE_JUCE
    std::shared_ptr<lpc::JucePluginHost> pluginHost_;  // after juce_: destroyed first
    std::shared_ptr<std::atomic<bool>> bouncing_ = std::make_shared<std::atomic<bool>>(false);  // a bounce is rendering with the live plug-in instances: nothing may play meanwhile
    std::unique_ptr<lpc::PluginScanner> scanner_;
#endif
    std::unique_ptr<lpc::audio::AudioEngine> engine_;
    std::unique_ptr<lpc::MediaStore> media_;
    std::unique_ptr<lpc::IAudioCallback> callback_;
    std::unique_ptr<lpc::IAudioDevice> device_;
    std::unique_ptr<EngineDriver> driver_;  // keeps the engine draining when there is no device
    std::unique_ptr<lpc::ProjectHost> host_;
    std::filesystem::path dir_;
    QTimer timer_;

    std::deque<PendingImport> importQueue_;
    bool importRunning_ = false;
    double lastImportEnd_ = 0.0;
    QHash<QString, QString> mediaPaths_;
    QHash<QString, QVariantList> peaksCache_;  // key: mediaId + "#" + buckets
    QSet<QString> peaksPending_;
    QHash<QString, QVariantList> regionPeaksCache_;  // regionId # offset # frames # buckets
    QSet<QString> regionPeaksPending_;

    // Bumped on every open: reads and notifications started for a previous project must not touch the new one.
    std::uint64_t generation_ = 0;
    bool forcedDegraded_ = false;
    std::uint64_t shownRevision_ = 0;
    lpc::TempoMap tempoMap_;
    int sampleRate_ = 48000;
    QString name_;
    QString key_ = QStringLiteral("C major");
    double bpm_ = 120.0;
    int beatsPerBar_ = 4;
    int beatUnit_ = 4;
    double masterGain_ = 0.0;
    QString masterId_;
    bool mixerVisible_ = true;
    QString tool_ = QStringLiteral("pointer");
    QString snap_ = QStringLiteral("quarter");
    bool followPlayhead_ = true;
    QStringList selectedTracks_;
    QHash<QString, QSet<QString>> trackToggles_;  // action id -> the tracks it is on for
    QStringList selectedRegions_;
    int trackHeightIndex_ = 1;
    bool playing_ = false;
    bool loop_ = false;
    double loopStartBeats_ = 0.0, loopEndBeats_ = 0.0;
    bool degraded_ = false;
    double positionSeconds_ = 0.0;
    double positionBeats_ = 0.0;
    double peak_ = 0.0;
    int peaksRevision_ = 0;
    bool globalTracksVisible_ = false;
    bool controlBarVisible_ = true, toolbarVisible_ = true, quickHelp_ = false;
    QSet<QString> barItemsOff_;
    QString templatesFolder_;  // empty: the default one
    std::uint64_t savedRevision_ = 0;  // the revision of the project when it was opened or last saved
    QTimer autosaveTimer_;
    QStringList replaceIds_;   // regions that the paste in progress replaces
    std::filesystem::path templatesDir() const;
    int barItemsRevision_ = 0;
    bool automationVisible_ = false;
    bool metronome_ = false;
    bool autoInput_ = true;
    bool showHidden_ = false;
    bool preFader_ = false;
    bool groupsActive_ = true;
    QStringList recent_;
    void addRecent();
    QStringList inputLabels_;
    int inputLabelsRevision_ = 0;
    struct AutoCapture {
        std::vector<std::pair<double, double>> gain, pan;  // (beats, value)
        bool latched = false;                              // a latch take: it goes on after the fader is let go
    };
    QHash<QString, AutoCapture> autoCapture_;               // by track, while moves are being written
    QString automationTarget(const QString& trackId, const QString& param) const;
    QHash<QString, QString> trackAutomationParams_;   // track id -> its lane's parameter
    mutable QHash<QString, QString> paramLabels_;     // plug-in parameter target -> label, filled when the choices are listed
    int automationRevision_ = 0;
    void captureAutomation(const QString& trackId, bool isPan, double value);
    void finishAutomationCapture(const QString& trackId);
    void finishAutomationCaptures(bool latchedToo);
    void tickAutomationWrite();
    QString groupSettingsTrack_;
    int groupsRevision_ = 0;
    std::vector<GroupRow> groupRows_;
    bool gestureActive_ = false;  // a fader or knob drag is running: the group fan-out works from where it started
    QHash<QString, QHash<QString, double>> groupBase_;  // field -> track -> value at the start of the drag
    void sendGroups(const std::vector<GroupRow>& rows);
    QStringList groupPeers(const QString& trackId, const char* field) const;
    double stripValue(const QString& trackId, const char* field) const;
    int waveformZoom_ = 1;
    QStringList midiChosen_;
    QString liveTarget_;
    std::unique_ptr<lpc::IMidiSink> midiSink_;
    std::shared_ptr<void> midiInputs_;  // the open MIDI inputs (platform object, destroyed before the engine)
    QStringList recMidiTracks_;         // the armed instrument tracks of the recording in progress
    std::vector<lpc::audio::AudioEngine::MidiRecEvent> recMidi_;
    void applyLiveTarget();
    void openMidi();
    void drainMidiRecording();
    // `passes`: the first and last frame of each pass of the take (a cycle that wraps makes several)
    void finishMidiRecording(const std::vector<std::pair<std::int64_t, std::int64_t>>& passes);
    QString effectEditorTrack_;
    int effectEditorIndex_ = -1;
    QString audioOutput_, audioInput_;
    int audioBuffer_ = 256;
    std::function<lpc::AudioDeviceChoices(const std::string&, const std::string&)> deviceLister_;
    void openDevice();  // opens the audio device (or the driver without one) for the current engine with the chosen settings
    QSet<QString> pendingAudioTracks_;  // tracks just created for an import: the models do not list them yet
    QString addAudioTrackNamed(const QString& name);
    bool recording_ = false, recFinishing_ = false, countIn_ = false;
    int countInChoice_ = 1;
    // A recording in progress: the raw input channels of the playing blocks, with where each block lay on the timeline. When it
    // ends every armed track gets its channels (its Input setting), cut at the cycle wraps (one take per pass), shifted by the
    // latency and cropped to the punch range.
    struct RecChunkInfo { std::int64_t position; int frames; std::size_t offset; };
    std::vector<std::vector<float>> recChannels_;
    std::vector<RecChunkInfo> recChunks_;
    QStringList recTracks_;
    QHash<QString, int> recInputs_;       // each armed track's input at the start
    bool punchStopSent_ = false;
    bool punchEnabled_ = false;
    bool allowQuickPunch_ = true;
    bool musicalGrid_ = false;
    bool lowLatency_ = false;
    QString overlapAudio_ = QStringLiteral("takes"), overlapMidi_ = QStringLiteral("takes");
    // The track that pass `pass` (0 = the first) of a cycle recording on `base` is put on: `base` itself, or a track made like it (commands are added to make it).
    QString cloneTrackCommand(const QString& base, const QString& name, int after, nlohmann::json& commands);
    QString trackForPass(const QString& base, int pass, QMap<QString, QStringList>& made, nlohmann::json& commands);
    // Create Tracks and Mute: the tracks of the earlier passes do not play.
    void mutePreviousPasses(const QMap<QString, QStringList>& made, nlohmann::json& commands) const;
    QString recordButtonMode_ = QStringLiteral("toggle");
    bool discardOnFinish_ = false, repeatOnFinish_ = false;  // how the recording in progress ends
    double recStartBeats_ = 0;                               // where it started
    void afterRecording();
    double punchStartBeats_ = 0.0, punchEndBeats_ = 0.0;
    int recordingDelay_ = 0;              // samples: added to the device's own latency when a take is placed
    void applyMonitoring();
    void sendRecordingStop();
    void drainRecording();
    void finishRecording();
    lpc::audio::ClickSettings clickSettings_;
    int clickRevision_ = 0;
    void clickSettingsEdited();
    std::uint64_t snapshots_ = 0;
    QString automationParam_ = QStringLiteral("volume");
    bool aqa_ = false, autoselect_ = false, aqaGesture_ = false;
    double marqueeFrom_ = 0, marqueeTo_ = 0;
    int marqueeRow0_ = 0, marqueeRow1_ = 0;
    void whenRegionsExist(const QStringList& ids, std::function<void()> then, int tries = 100);
    int aqaCc_ = -1;
    std::atomic<int> aqaWatch_{-1};  // read by the MIDI thread: -1 off, -2 any controller, else the one
    QTimer aqaRelease_;
    void autoselectLane(const QString& trackId, const QString& param);  // Autoselect Automation Parameter in Read Mode
    std::vector<MarkerRow> markerRows_;
    void sendMarkers(const std::vector<MarkerRow>& rows);
    QHash<QString, double> meter_, hold_, reduction_;
    QString deviceError_;
    QString lastError_;
};

}  // namespace jad
