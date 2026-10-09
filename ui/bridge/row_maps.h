#pragma once
#include <QVariantList>
#include <QVariantMap>

#include "bridge/snapshot.h"

namespace jad {

// The strip view of a track as a plain map for QML (the Inspector, the mixer): see the keys below.
inline QVariantMap trackToMap(const TrackRow& t) {
    QVariantList inserts, sends;
    for (const InsertRow& i : t.inserts)
        inserts.append(QVariantMap{{"processorId", i.processorId}, {"gainDb", i.gainDb}, {"label", i.label}, {"plugin", i.plugin}, {"bypass", i.bypass}, {"params", i.params}});
    for (const SendRow& s : t.sends)
        sends.append(QVariantMap{{"id", s.id}, {"targetId", s.targetId}, {"targetName", s.targetName}, {"levelDb", s.levelDb}, {"preFader", s.preFader}});
    return {{"instrumentParams", t.instrumentParams}, {"trackId", t.id},         {"name", t.name},           {"color", t.color},         {"kind", t.kind},
            {"master", t.master},      {"patchId", t.patchId},     {"patchName", t.patchName}, {"instrument", t.instrument},
            {"gainDb", t.gainDb},      {"pan", t.pan},             {"mute", t.mute},           {"solo", t.solo},
            {"outputId", t.outputId},  {"outputName", t.outputName}, {"inserts", inserts},     {"sends", sends},
            {"recordArm", t.recordArm}, {"inputMonitor", t.inputMonitor}, {"soloSafe", t.soloSafe}};
}

}  // namespace jad
