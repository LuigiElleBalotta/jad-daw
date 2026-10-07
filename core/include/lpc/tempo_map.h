#pragma once
#include <cstdint>
#include <optional>
#include <vector>

namespace lpc {

using Ticks = std::int64_t;
inline constexpr Ticks kPPQ = 960;
inline constexpr double kMinBpm = 20.0;
inline constexpr double kMaxBpm = 999.0;

class TempoMap {
public:
    struct TempoEvent {
        Ticks tick = 0;
        double bpm = 120.0;
        bool operator==(const TempoEvent&) const = default;
    };
    struct SigEvent {
        Ticks tick = 0;
        int numerator = 4;
        int denominator = 4;
        bool operator==(const SigEvent&) const = default;
    };

    TempoMap();
    static TempoMap fromEvents(std::vector<TempoEvent> tempos, std::vector<SigEvent> sigs);

    bool setTempo(Ticks tick, double bpm);
    bool removeTempo(Ticks tick);
    std::optional<double> tempoEventAt(Ticks tick) const;
    bool setSignature(Ticks tick, int numerator, int denominator);

    const std::vector<TempoEvent>& tempos() const { return tempos_; }
    const std::vector<SigEvent>& signatures() const { return sigs_; }

    double bpmAt(Ticks tick) const;
    double ticksToSamples(Ticks tick, double sampleRate) const;
    Ticks samplesToTicks(double samples, double sampleRate) const;

    bool operator==(const TempoMap&) const = default;

private:
    std::vector<TempoEvent> tempos_;
    std::vector<SigEvent> sigs_;
};

}  // namespace lpc
