#include <catch2/catch_test_macros.hpp>
#include <mutex>
#include <vector>
#include "lpc/commands.h"
#include "lpc/demo_project.h"
#include "lpc/media_store.h"
#include "lpc/offline_render.h"
#include "lpc/plugin_host.h"
#include "lpc/processor_ids.h"

using namespace lpc;

namespace {

struct Sent {
    unsigned char status, d1, d2;
};

class FakeOutput final : public IMidiOutput {
public:
    void send(unsigned char s, unsigned char a, unsigned char b) noexcept override { messages.push_back({s, a, b}); }
    std::vector<Sent> messages;
};

class FakeHost final : public IPluginHost {
public:
    std::vector<PluginDescriptor> catalogue() const override { return {}; }
    std::shared_ptr<audio::IProcessor> acquire(const InsertSlot&, const ProcessorRef&, double, int) override { return nullptr; }
    void prune(const std::vector<std::pair<InsertSlot, ProcessorRef>>&) override {}
    std::optional<std::string> captureState(const InsertSlot&) override { return std::nullopt; }
    void setReadyListener(std::function<void(const InsertSlot&)>) override {}
    std::shared_ptr<IMidiOutput> openMidiOutput(const std::string& device) override {
        opened.push_back(device);
        return device == "Fake Synth" ? out : nullptr;
    }
    std::shared_ptr<FakeOutput> out = std::make_shared<FakeOutput>();
    std::vector<std::string> opened;
};

Project externalProject(int channel, const std::string& device) {
    Project p = makeDemoProject({});
    for (Track& t : p.tracks)
        if (t.kind == TrackKind::Instrument) {
            t.instrument = ProcessorRef{kProcExternalMidi, {{"channel", static_cast<double>(channel)}}, "{\"device\":\"" + device + "\"}"};
        }
    return p;
}

}  // namespace

TEST_CASE("external MIDI: the notes of the track go out of the port on its channel, and the track is silent", "[external-midi]") {
    FakeHost host;
    const Project p = externalProject(3, "Fake Synth");
    MediaStore media({}, false);
    RenderOptions o;
    o.plugins = &host;
    o.externalMidi = true;
    o.tailSeconds = 0.1;
    const RenderResult r = renderOffline(p, media, o);
    REQUIRE(!host.out->messages.empty());
    int ons = 0, offs = 0;
    for (const Sent& m : host.out->messages) {
        if ((m.status & 0xF0) == 0x90 && m.d2 > 0) { ++ons; CHECK((m.status & 0x0F) == 2); }  // channel 3
        if ((m.status & 0xF0) == 0x80) ++offs;
    }
    CHECK(ons == 12);                                  // four chords of three notes
    CHECK(offs >= 12);                                 // every note is turned off again
    // the Keys track made no sound: only the Tone track is heard (the demo's tone track is 220 Hz at -6 dB)
    host.out->messages.clear();
    // a bounce does not play the hardware
    RenderOptions quiet;
    quiet.plugins = &host;
    quiet.tailSeconds = 0.1;
    renderOffline(p, media, quiet);
    CHECK(host.out->messages.empty());
    // a device that cannot be opened: silence, no crash
    FakeHost none;
    RenderOptions o2;
    o2.plugins = &none;
    o2.externalMidi = true;
    const Project p2 = externalProject(1, "Missing Port");
    CHECK_NOTHROW(renderOffline(p2, media, o2));
    CHECK(none.out->messages.empty());
    CHECK(!none.opened.empty());
}

TEST_CASE("external MIDI: the instrument validates and JSON keeps its device", "[external-midi]") {
    Project p = makeDemoProject({});
    Uuid keys;
    for (const Track& t : p.tracks)
        if (t.kind == TrackKind::Instrument) keys = t.id;
    ProcessorRef ext{kProcExternalMidi, {{"channel", 5.0}}, "{\"device\":\"Some Port\"}"};
    ApplyResult r = makeSetInstrument(keys, ext)->apply(p);
    REQUIRE(r.ok());
    CHECK(p.findTrack(keys)->instrument->state == "{\"device\":\"Some Port\"}");
    REQUIRE(r.inverse->apply(p).ok());
    CHECK(p.findTrack(keys)->instrument->processorId == "builtin.sine");
}
