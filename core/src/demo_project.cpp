#include "lpc/demo_project.h"

#include <cmath>
#include <random>

#include "lpc/wav.h"

namespace lpc {

Project makeDemoProject(const std::filesystem::path& dir) {
    std::mt19937_64 rng(42);
    Project p(Uuid::random(rng));
    p.name = "Demo";
    p.sampleRate = 48000;

    // 2 s of 220 Hz at -10 dBFS-ish amplitude, stereo
    MediaItem tone{Uuid::random(rng), "audio/tone.wav", "demo-tone-220hz", 48000, 2, 96000};
    if (!dir.empty()) {
        std::vector<float> samples(96000 * 2);
        for (int i = 0; i < 96000; ++i) {
            const float v = 0.3f * static_cast<float>(std::sin(2.0 * 3.14159265358979323846 * 220.0 * i / 48000.0));
            samples[static_cast<std::size_t>(2 * i)] = v;
            samples[static_cast<std::size_t>(2 * i + 1)] = v;
        }
        std::filesystem::create_directories(dir / "audio");
        writeWav(dir / "audio" / "tone.wav", 48000, 2, samples);
    }
    p.mediaPool.push_back(tone);

    Track bus;
    bus.id = Uuid::random(rng);
    bus.kind = TrackKind::Bus;
    bus.name = "Reverb Bus";
    bus.color = "purple";
    bus.strip.gainDb = -3.0f;
    bus.strip.inserts.push_back({"builtin.gain", {{"gainDb", -3.0}}, ""});

    Track keys;
    keys.id = Uuid::random(rng);
    keys.kind = TrackKind::Instrument;
    keys.name = "Keys";
    keys.color = "indigo";
    keys.instrument = ProcessorRef{"builtin.sine", {}, ""};
    keys.strip.pan = -0.3f;
    keys.strip.sends.push_back({Uuid::random(rng), bus.id, -6.0f, false});
    Region chords;
    chords.id = Uuid::random(rng);
    chords.start = 0;
    chords.length = 8 * kPPQ;
    const std::uint8_t progression[4][3] = {{60, 64, 67}, {65, 69, 72}, {67, 71, 74}, {60, 64, 67}};  // C F G C
    for (int chord = 0; chord < 4; ++chord)
        for (const std::uint8_t note : progression[chord]) chords.notes.push_back({chord * 2 * kPPQ, 2 * kPPQ, note, 90});
    keys.regions.push_back(chords);

    Track audio;
    audio.id = Uuid::random(rng);
    audio.kind = TrackKind::Audio;
    audio.name = "Tone";
    audio.color = "teal";
    audio.strip.gainDb = -6.0f;
    audio.strip.pan = 0.3f;
    Region toneRegion;
    toneRegion.id = Uuid::random(rng);
    toneRegion.start = 0;
    toneRegion.length = 4 * kPPQ;  // 2 s at 120 bpm
    toneRegion.mediaId = tone.id;
    audio.regions.push_back(toneRegion);

    p.tracks.push_back(bus);
    p.tracks.push_back(keys);
    p.tracks.push_back(audio);
    return p;
}

}  // namespace lpc
