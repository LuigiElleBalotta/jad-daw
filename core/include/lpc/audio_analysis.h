#pragma once
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "lpc/loudness.h"
#include "lpc/wav.h"

namespace lpc {

// What an engineer (or an AI assistant) wants to know about a piece of audio, measured in one pass: level, loudness, balance, stereo image,
// problems (clipping, DC, silence) and the tempo. All levels are dB; the spectrum is in third-octave bands as RMS in dBFS.
inline constexpr int kThirdOctaveBands = 31;  // 20 Hz ... 20 kHz

struct AudioReport {
    double seconds = 0.0;
    int sampleRate = 0, channels = 0;
    // level
    double peakDb = -200.0, rmsDb = -200.0, crestDb = 0.0;
    LoudnessStats loudness;
    // problems
    double dcOffset = 0.0;            // mean value, -1..1
    std::int64_t clippedSamples = 0;  // |x| >= 0.999
    double clippedPercent = 0.0;
    double silencePercent = 0.0;      // 100 ms blocks under -60 dBFS
    double leadingSilenceSeconds = 0.0, trailingSilenceSeconds = 0.0;
    // balance
    std::vector<double> bandCentres;  // Hz
    std::vector<double> bandDb;       // RMS in each third-octave band, dBFS
    double centroidHz = 0.0, rolloffHz = 0.0;   // where the spectrum is centred; below which 85 % of the energy lies
    double lowPercent = 0.0, lowMidPercent = 0.0, midPercent = 0.0, highMidPercent = 0.0, highPercent = 0.0;  // of the energy: <120, 120-500, 500-2k, 2k-6k, >6k Hz
    // stereo
    double correlation = 1.0;         // -1 (out of phase) .. 1 (mono)
    double widthPercent = 0.0;        // side energy of the whole (0 mono, 50 uncorrelated)
    double balanceDb = 0.0;           // left minus right
    // time
    double bpm = 0.0, tempoConfidence = 0.0;
    std::vector<double> shortTermTimeline;  // LUFS-S about once a second (at most 240 values), to see the dynamics over time
};

struct AnalysisOptions {
    bool tempo = true;      // the tempo analysis is the slowest part
    bool timeline = true;
};

AudioReport analyseAudio(const WavData& audio, const AnalysisOptions& options = {});
nlohmann::json toJson(const AudioReport& r);
// A short plain-language reading of the report: what stands out (too quiet, clipping, boomy, thin, narrow...), for a person or an assistant.
std::vector<std::string> describeReport(const AudioReport& r);

// Two sounds that play together: where their spectra pile up (masking). `overlap` is 0..1 per third-octave band: 1 when both are strong there
// and about equally loud. `worst` lists the bands with the most overlap.
struct MaskingReport {
    std::vector<double> bandCentres, overlap;
    double score = 0.0;  // 0..1: the average overlap of the bands in which at least one of them is strong
    std::vector<double> worstHz;
};
MaskingReport compareMasking(const AudioReport& a, const AudioReport& b);

}  // namespace lpc
