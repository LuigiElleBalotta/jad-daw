#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace lpc {

// Dolby Atmos masters are ADM BWF files (ITU-R BS.2076 inside a BW64/RIFF wave file, EBU Tech 3285/3364): the bed's channels and one mono channel per object
// are interleaved in the `data` chunk, `chna` says which audio track is which track UID and `axml` holds the Audio Definition Model with the position of every object
// over time. This is the file as Logic writes and reads it, without the Dolby-specific `dbmd` chunk.

// A position of an object in the room, in the cartesian coordinates of the ADM: x from the left (-1) to the right (1), y from the back (-1) to the front (1), z from the floor (0) to the ceiling (1).
struct AdmPosition {
    double time = 0.0;  // seconds from the start of the file
    double x = 0.0, y = 1.0, z = 0.0;
    double size = 0.0;
    bool operator==(const AdmPosition&) const = default;
};

struct AdmObject {
    std::string name;
    std::vector<float> samples;         // mono
    std::vector<AdmPosition> positions;  // sorted by time; the first one holds from the start
};

// The 7.1.2 bed that Dolby Atmos masters carry: L R C LFE Lss Rss Lrs Rrs Lts Rts.
constexpr int kAdmBedChannels = 10;

struct AdmFile {
    int sampleRate = 48000;
    int bitDepth = 24;
    std::vector<std::vector<float>> bed;  // `kAdmBedChannels` channels of the same length (or none: the file has no bed)
    std::vector<AdmObject> objects;
    std::int64_t frames() const;
};

// Writes the file (16 or 24 bit PCM, or 32 bit float is not allowed by the format). Throws std::runtime_error when it cannot.
void writeAdmBwf(const std::filesystem::path& path, const AdmFile& file);

// Reads a file that has `chna` and `axml` chunks: the bed channels are the audio tracks of a DirectSpeakers pack, the objects those of an Objects pack. Throws when the file is not an ADM BWF.
AdmFile readAdmBwf(const std::filesystem::path& path);

// The ADM document and the chna chunk for a file, for tests.
std::string admXml(const AdmFile& file);

}  // namespace lpc
