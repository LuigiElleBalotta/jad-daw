#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <unordered_set>
#include "lpc/audio/render_graph.h"
#include "lpc/commands.h"
#include "lpc/graph_builder.h"
#include "lpc/model_json.h"
#include "lpc/undo_stack.h"
#include "random_commands.h"

using namespace lpc;
using namespace lpc::audio;

namespace {

std::mt19937_64 gRng(2024);

Track track(TrackKind kind, const char* name) {
    Track t;
    t.id = Uuid::random(gRng);
    t.kind = kind;
    t.name = name;
    if (kind == TrackKind::Instrument) t.instrument = ProcessorRef{"builtin.sine", {}, ""};
    return t;
}

void add(Project& p, const Track& t) { REQUIRE(makeAddTrack(t)->apply(p).ok()); }

std::shared_ptr<MemorySource> silence(int frames = 4800) {
    return std::make_shared<MemorySource>(48000, 2, std::vector<float>(static_cast<std::size_t>(frames) * 2, 0.1f));
}

void applyAll(RenderGraph& g, std::vector<AudioMsg> msgs) {
    for (AudioMsg& m : msgs) g.apply(m).destroy();
}

// Builds a graph by applying the initial messages; every media item gets a registered source.
struct Scene {
    Project p;
    MediaStore media;
    std::unordered_set<Uuid> registered;

    explicit Scene(Project project) : p(std::move(project)) { registerMedia(); }
    void registerMedia() {
        for (const MediaItem& m : p.mediaPool)
            if (registered.insert(m.id).second) media.registerSource(m.id, silence());
    }
};

const AudioMsg* firstOfKind(const std::vector<AudioMsg>& v, MsgKind k) {
    for (const AudioMsg& m : v)
        if (m.kind == k) return &m;
    return nullptr;
}

int countOfKind(const std::vector<AudioMsg>& v, MsgKind k) {
    int n = 0;
    for (const AudioMsg& m : v) n += m.kind == k;
    return n;
}

void destroyAll(std::vector<AudioMsg>& msgs) {
    // messages that were never applied still own their objects
    for (AudioMsg& m : msgs) m.obj.destroy();
}

}  // namespace

TEST_CASE("builder: processing order puts sources before buses and the master last", "[builder]") {
    Project p(Uuid::random(gRng));
    Track bus2 = track(TrackKind::Bus, "Bus2");
    Track bus1 = track(TrackKind::Bus, "Bus1");
    bus1.strip.output = bus2.id;  // bus1 -> bus2
    Track src = track(TrackKind::Audio, "Src");
    src.strip.output = bus1.id;   // src -> bus1
    add(p, bus2);                 // declared in the "wrong" order on purpose
    add(p, bus1);
    add(p, src);
    const auto order = processingOrder(p);
    REQUIRE(order.size() == 4);
    REQUIRE(order[0] == src.id);
    REQUIRE(order[1] == bus1.id);
    REQUIRE(order[2] == bus2.id);
    REQUIRE(order[3] == p.master()->id);
}

TEST_CASE("builder: sends count as routing edges and independent tracks keep their order", "[builder]") {
    Project p(Uuid::random(gRng));
    const Track bus = track(TrackKind::Aux, "Aux");
    const Track a = track(TrackKind::Audio, "A");
    const Track b = track(TrackKind::Instrument, "B");
    add(p, bus);
    add(p, a);
    add(p, b);
    REQUIRE(makeAddSend(a.id, Send{Uuid::random(gRng), bus.id, 0.0f, false})->apply(p).ok());
    const auto order = processingOrder(p);
    REQUIRE(order[0] == a.id);
    REQUIRE(order[1] == b.id);
    REQUIRE(order[2] == bus.id);
    REQUIRE(order[3] == p.master()->id);
}

TEST_CASE("builder: regions become absolute frames through the tempo map", "[builder]") {
    Scene s(Project(Uuid::random(gRng)));
    const MediaItem media{Uuid::random(gRng), "audio/a.wav", "h", 48000, 2, 4800};
    REQUIRE(makeAddMedia(media)->apply(s.p).ok());
    s.registerMedia();

    Track audio = track(TrackKind::Audio, "A");
    Region musical;
    musical.id = Uuid::random(gRng);
    musical.start = kPPQ;
    musical.length = 2 * kPPQ;
    musical.mediaId = media.id;
    musical.sourceOffsetFrames = 77;
    musical.gainDb = -6.0206f;
    Region absolute;
    absolute.id = Uuid::random(gRng);
    absolute.timeBase = TimeBase::Absolute;
    absolute.start = 500000;   // 0.5 s
    absolute.length = 1000000;  // 1 s
    absolute.mediaId = media.id;
    audio.regions = {musical, absolute};
    add(s.p, audio);

    auto cfg = buildConfig(s.p, *s.p.findTrack(audio.id), s.media);
    REQUIRE(cfg->regions.size() == 2);
    REQUIRE(cfg->regions[0].startFrame == 24000);  // beat 1 at 120 bpm
    REQUIRE(cfg->regions[0].endFrame == 72000);    // beat 3
    REQUIRE(cfg->regions[0].sourceOffsetFrames == 77);
    REQUIRE(cfg->regions[0].gain == Catch::Approx(0.5f).margin(1e-4));
    REQUIRE(cfg->regions[1].startFrame == 24000);
    REQUIRE(cfg->regions[1].endFrame == 72000);
    REQUIRE(cfg->keepAlive.size() == 2);

    // a tempo change moves the musical region but not the absolute one
    REQUIRE(makeSetTempo(kPPQ, 60.0)->apply(s.p).ok());
    cfg = buildConfig(s.p, *s.p.findTrack(audio.id), s.media);
    REQUIRE(cfg->regions[0].startFrame == 24000);
    REQUIRE(cfg->regions[0].endFrame == 24000 + 2 * 48000);  // two beats at 60 bpm
    REQUIRE(cfg->regions[1].startFrame == 24000);
}

TEST_CASE("builder: MIDI notes become sorted frame spans", "[builder]") {
    Scene s(Project(Uuid::random(gRng)));
    Track inst = track(TrackKind::Instrument, "Keys");
    Region r;
    r.id = Uuid::random(gRng);
    r.start = kPPQ;  // beat 1 = frame 24000
    r.length = 4 * kPPQ;
    r.notes.push_back({kPPQ, kPPQ, 64, 90});  // second note listed first on purpose
    r.notes.push_back({0, kPPQ, 60, 100});
    inst.regions.push_back(r);
    add(s.p, inst);

    const auto cfg = buildConfig(s.p, *s.p.findTrack(inst.id), s.media);
    REQUIRE(cfg->regions[0].notes.size() == 2);
    REQUIRE(cfg->regions[0].notes[0].onFrame == 24000);
    REQUIRE(cfg->regions[0].notes[0].offFrame == 48000);
    REQUIRE(cfg->regions[0].notes[0].note == 60);
    REQUIRE(cfg->regions[0].notes[1].onFrame == 48000);
    REQUIRE(cfg->regions[0].notes[1].note == 64);
}

TEST_CASE("builder: muted MIDI notes stay in the model but are not played", "[builder]") {
    Scene s(Project(Uuid::random(gRng)));
    Track inst = track(TrackKind::Instrument, "Keys");
    Region r;
    r.id = Uuid::random(gRng);
    r.length = 4 * kPPQ;
    MidiNote quiet{0, kPPQ, 60, 100};
    quiet.muted = true;
    r.notes.push_back(quiet);
    r.notes.push_back({kPPQ, kPPQ, 64, 90});
    inst.regions.push_back(r);
    add(s.p, inst);
    const auto cfg = buildConfig(s.p, *s.p.findTrack(inst.id), s.media);
    REQUIRE(cfg->regions[0].notes.size() == 1);
    REQUIRE(cfg->regions[0].notes[0].note == 64);
    nlohmann::json j = quiet;
    REQUIRE(j.at("muted") == true);
    REQUIRE(j.get<MidiNote>().muted);
    nlohmann::json plain = MidiNote{0, kPPQ, 60, 100};
    REQUIRE_FALSE(plain.contains("muted"));
}

TEST_CASE("builder: a region whose media is missing is skipped and reported", "[builder]") {
    Project p(Uuid::random(gRng));
    const MediaItem media{Uuid::random(gRng), "audio/gone.wav", "h", 48000, 2, 100};
    REQUIRE(makeAddMedia(media)->apply(p).ok());
    Track audio = track(TrackKind::Audio, "A");
    Region r;
    r.id = Uuid::random(gRng);
    r.length = kPPQ;
    r.mediaId = media.id;
    audio.regions.push_back(r);
    add(p, audio);

    MediaStore media_store;  // knows nothing about the file
    const auto cfg = buildConfig(p, *p.findTrack(audio.id), media_store);
    REQUIRE(cfg->regions.empty());
    REQUIRE(media_store.missingCount() == 1);
}

TEST_CASE("builder: diff produces the smallest sensible message set", "[builder][diff]") {
    Scene s(Project(Uuid::random(gRng)));
    const Track a = track(TrackKind::Audio, "A");
    add(s.p, a);

    // nothing changed
    REQUIRE(diffToMessages(s.p, s.p, s.media).empty());

    // strip-only change: exactly one SetStrip with the linear gain
    Project after = s.p;
    StripPatch patch;
    patch.gainDb = -6.0206f;
    patch.mute = true;
    REQUIRE(makeSetStrip(a.id, patch)->apply(after).ok());
    auto msgs = diffToMessages(s.p, after, s.media);
    REQUIRE(msgs.size() == 1);
    REQUIRE(msgs[0].kind == MsgKind::SetStrip);
    REQUIRE(msgs[0].track == a.id);
    REQUIRE(msgs[0].strip.gain == Catch::Approx(0.5f).margin(1e-4));
    REQUIRE(msgs[0].strip.mute);

    // new track: AddTrack then Reorder
    after = s.p;
    const Track b = track(TrackKind::Instrument, "B");
    add(after, b);
    msgs = diffToMessages(s.p, after, s.media);
    REQUIRE(msgs.size() == 2);
    REQUIRE(msgs[0].kind == MsgKind::AddTrack);
    REQUIRE(msgs[1].kind == MsgKind::Reorder);
    destroyAll(msgs);

    // removed track: RemoveTrack then Reorder
    msgs = diffToMessages(after, s.p, s.media);
    REQUIRE(msgs.size() == 2);
    REQUIRE(msgs[0].kind == MsgKind::RemoveTrack);
    REQUIRE(msgs[0].track == b.id);
    REQUIRE(msgs[1].kind == MsgKind::Reorder);
    destroyAll(msgs);

    // tempo change: SetConfig for every track that exists on both sides (master + A)
    after = s.p;
    REQUIRE(makeSetTempo(kPPQ, 90.0)->apply(after).ok());
    msgs = diffToMessages(s.p, after, s.media);
    REQUIRE(countOfKind(msgs, MsgKind::SetConfig) == 2);
    REQUIRE(firstOfKind(msgs, MsgKind::Reorder) == nullptr);
    destroyAll(msgs);

    // inserts change: a single SetConfig
    after = s.p;
    REQUIRE(makeSetInserts(a.id, {{"builtin.gain", {{"gainDb", -3.0}}, ""}})->apply(after).ok());
    msgs = diffToMessages(s.p, after, s.media);
    REQUIRE(msgs.size() == 1);
    REQUIRE(msgs[0].kind == MsgKind::SetConfig);
    destroyAll(msgs);
}

TEST_CASE("builder: initial messages build a graph in processing order", "[builder]") {
    Scene s(Project(Uuid::random(gRng)));
    Track bus = track(TrackKind::Bus, "Bus");
    Track src = track(TrackKind::Audio, "Src");
    src.strip.output = bus.id;
    add(s.p, bus);
    add(s.p, src);
    RenderGraph g(48000.0);
    applyAll(g, initialMessages(s.p, s.media));
    const auto d = g.describe();
    REQUIRE(d["tracks"].size() == 3);
    REQUIRE(d["tracks"][0]["id"] == src.id.toString());
    REQUIRE(d["tracks"][1]["id"] == bus.id.toString());
    REQUIRE(d["tracks"][2]["id"] == s.p.master()->id.toString());
}

TEST_CASE("builder: messages keep the live graph identical to a graph built from scratch", "[builder][property]") {
    for (std::uint64_t seed = 1; seed <= 15; ++seed) {
        CAPTURE(seed);
        std::mt19937_64 rng(seed);
        Scene s(Project(Uuid::random(rng)));
        UndoStack stack;
        RenderGraph live(48000.0);
        applyAll(live, initialMessages(s.p, s.media));

        int steps = 0;
        for (int i = 0; i < 150; ++i) {
            const Project before = s.p;
            if (rng() % 100 < 25 && stack.canUndo()) {
                REQUIRE_FALSE(stack.undo(s.p).has_value());
            } else {
                CommandPtr c = test::randomCommand(s.p, rng);
                if (!c || stack.execute(s.p, std::move(c)).has_value()) continue;
            }
            s.registerMedia();
            applyAll(live, diffToMessages(before, s.p, s.media));

            RenderGraph fresh(48000.0);
            applyAll(fresh, initialMessages(s.p, s.media));
            REQUIRE(live.describe() == fresh.describe());
            ++steps;
        }
        REQUIRE(steps > 30);
    }
}
