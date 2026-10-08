#include <QtTest>
#include <nlohmann/json.hpp>

#include "bridge/inspector_model.h"
#include "bridge/library_model.h"
#include "bridge/row_maps.h"
#include "lpc/patch_library.h"

namespace {
jad::TrackRow row(const char* id, const char* kind, const char* name, bool master = false) {
    jad::TrackRow r;
    r.id = id;
    r.kind = kind;
    r.name = name;
    r.master = master;
    r.color = "blue";
    return r;
}
jad::RegionRow region(const char* id, const char* trackId) {
    jad::RegionRow r;
    r.id = id;
    r.trackId = trackId;
    r.lengthBeats = 4;
    return r;
}
const char* kDoc = R"({"patches":[
 {"id":"a.one","category":"01 A","name":"Alpha","kind":"audio","instrument":null,"strip":{"gainDb":-3},"inserts":[],"smartControls":[]},
 {"id":"a.two","category":"01 A","name":"Beta","kind":"audio","instrument":null,"strip":{"gainDb":-4},"inserts":[],"smartControls":[]},
 {"id":"a.three","category":"02 B","name":"Gamma","kind":"audio","instrument":null,"strip":{"gainDb":-5},"inserts":[],"smartControls":[]},
 {"id":"i.one","category":"01 Leads","name":"Lead","kind":"instrument","instrument":{"processorId":"builtin.sine"},"strip":{},"inserts":[],"smartControls":[]}]})";
}  // namespace

class PanelModelsTest : public QObject {
    Q_OBJECT
private slots:
    void inspectorShowsTheFirstSelectedTrackAndItsOutput() {
        jad::TrackRow master = row("m", "master", "Stereo Out", true);
        jad::TrackRow a = row("a", "audio", "Vox");
        a.outputId = "m";
        a.outputName = "Stereo Out";
        a.inserts.push_back({"builtin.gain", 3.0});
        jad::InspectorModel model;
        model.update({master, a}, {}, {"a"}, {});
        QVERIFY(model.hasTrack());
        QVERIFY(!model.hasRegion());
        QCOMPARE(model.track().value("name").toString(), QStringLiteral("Vox"));
        QCOMPARE(model.track().value("inserts").toList().size(), 1);
        QCOMPARE(model.output().value("trackId").toString(), QStringLiteral("m"));
    }
    void inspectorFollowsTheRegionWhenNoTrackIsSelected() {
        jad::InspectorModel model;
        model.update({row("m", "master", "Out", true), row("a", "audio", "Vox")}, {region("r", "a")}, {}, {"r"});
        QVERIFY(model.hasRegion());
        QVERIFY(model.hasTrack());
        QCOMPARE(model.trackId(), QStringLiteral("a"));
        QCOMPARE(model.region().value("trackName").toString(), QStringLiteral("Vox"));
    }
    void inspectorIsNeutralForStaleIds() {
        jad::InspectorModel model;
        model.update({row("m", "master", "Out", true), row("a", "audio", "Vox")}, {}, {"gone"}, {"gone-too"});
        QVERIFY(!model.hasTrack());
        QVERIFY(!model.hasRegion());
        QVERIFY(model.track().isEmpty());
        QVERIFY(model.smartControls().isEmpty());
    }
    void inspectorListsBusTargetsAndSmartControls() {
        jad::TrackRow bus = row("b", "bus", "Reverb");
        jad::TrackRow a = row("a", "audio", "Vox");
        jad::SmartRow s;
        s.id = "level";
        s.label = "Level";
        s.group = "Main";
        s.min = -24;
        s.max = 6;
        s.value = -3;
        a.smart.push_back(s);
        jad::InspectorModel model;
        model.update({row("m", "master", "Out", true), bus, a}, {}, {"a"}, {});
        QCOMPARE(model.busTargets().size(), 1);
        QCOMPARE(model.smartControls().size(), 1);
        QCOMPARE(model.smartControls().first().toMap().value("label").toString(), QStringLiteral("Level"));
    }
    void inspectorEmitsChangedOnlyWhenSomethingChanged() {
        jad::InspectorModel model;
        const std::vector<jad::TrackRow> rows{row("m", "master", "Out", true), row("a", "audio", "Vox")};
        QSignalSpy spy(&model, &jad::InspectorModel::changed);
        model.update(rows, {}, {"a"}, {});
        QCOMPARE(spy.count(), 1);
        model.update(rows, {}, {"a"}, {});
        QCOMPARE(spy.count(), 1);
    }
    void libraryListsCategoriesAndPatchesOfTheTrackKind() {
        const auto lib = lpc::PatchLibrary::fromJson(nlohmann::json::parse(kDoc));
        jad::LibraryModel model;
        model.setLibrary(&lib);
        model.setTrack("audio", "");
        QCOMPARE(model.categories(), (QStringList{"01 A", "02 B"}));
        QCOMPARE(model.category(), QStringLiteral("01 A"));  // the first one when the track has no patch
        QCOMPARE(model.patches().size(), 2);
        model.setCategory("02 B");
        QCOMPARE(model.patches().size(), 1);
        model.setTrack("instrument", "");
        QCOMPARE(model.categories(), (QStringList{"01 Leads"}));
        model.setTrack("midi", "");
        QVERIFY(model.categories().isEmpty());
        QVERIFY(model.patches().isEmpty());
    }
    void libraryOpensOnTheCategoryOfTheCurrentPatch() {
        const auto lib = lpc::PatchLibrary::fromJson(nlohmann::json::parse(kDoc));
        jad::LibraryModel model;
        model.setLibrary(&lib);
        model.setTrack("audio", "a.three");
        QCOMPARE(model.category(), QStringLiteral("02 B"));
        QCOMPARE(model.currentPatchId(), QStringLiteral("a.three"));
        model.setTrack("audio", "removed.from.catalogue");  // an id the catalogue no longer has: no selection, no error
        QCOMPARE(model.currentPatchId(), QStringLiteral("removed.from.catalogue"));
        QVERIFY(!model.neighbour(+1).isEmpty());  // still steps from the start of the shown list
    }
    void librarySearchCrossesCategories() {
        const auto lib = lpc::PatchLibrary::fromJson(nlohmann::json::parse(kDoc));
        jad::LibraryModel model;
        model.setLibrary(&lib);
        model.setTrack("audio", "");
        model.setSearch("a");  // Alpha, Beta, Gamma all contain an a
        QCOMPARE(model.patches().size(), 3);
        model.setSearch("gam");
        QCOMPARE(model.patches().size(), 1);
        model.setSearch("zzz");
        QVERIFY(model.patches().isEmpty());
        model.setSearch("");
        QCOMPARE(model.patches().size(), 2);
    }
    void libraryNeighbourStepsWithoutWrapping() {
        const auto lib = lpc::PatchLibrary::fromJson(nlohmann::json::parse(kDoc));
        jad::LibraryModel model;
        model.setLibrary(&lib);
        model.setTrack("audio", "a.one");
        QCOMPARE(model.neighbour(+1), QStringLiteral("a.two"));
        QCOMPARE(model.neighbour(-1), QStringLiteral("a.one"));  // already the first: stays
        model.setTrack("audio", "a.two");
        QCOMPARE(model.neighbour(+1), QStringLiteral("a.two"));  // already the last of the category: stays
        model.setTrack("audio", "");
        QCOMPARE(model.neighbour(+1), QStringLiteral("a.one"));  // nothing current: the first (or the last going back)
        QCOMPARE(model.neighbour(-1), QStringLiteral("a.two"));
    }
    void libraryReportsCatalogueProblems() {
        const auto lib = lpc::PatchLibrary::fromJson(nlohmann::json::parse(R"({"patches":[{"id":"bad"}]})"));
        jad::LibraryModel model;
        model.setLibrary(&lib);
        QCOMPARE(model.problems().size(), 1);
        QCOMPARE(model.patchCount(), 0);
    }
};

QTEST_MAIN(PanelModelsTest)
#include "tst_panel_models.moc"
