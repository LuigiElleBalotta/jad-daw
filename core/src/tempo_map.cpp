#include "lpc/tempo_map.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace lpc {

namespace {

double samplesPerTick(double bpm, double sampleRate) { return sampleRate * 60.0 / (bpm * double(kPPQ)); }

bool validSignature(int n, int d) {
    return n >= 1 && n <= 64 && (d == 1 || d == 2 || d == 4 || d == 8 || d == 16 || d == 32);
}

}  // namespace

TempoMap::TempoMap() : tempos_{{0, 120.0}}, sigs_{{0, 4, 4}} {}

TempoMap TempoMap::fromEvents(std::vector<TempoEvent> tempos, std::vector<SigEvent> sigs) {
    TempoMap m;
    m.tempos_.clear();
    m.sigs_.clear();
    for (const auto& e : tempos)
        if (!m.setTempo(e.tick, e.bpm)) throw std::invalid_argument("invalid tempo event");
    for (const auto& e : sigs)
        if (!m.setSignature(e.tick, e.numerator, e.denominator)) throw std::invalid_argument("invalid signature event");
    if (m.sigs_.empty()) m.sigs_.push_back({0, 4, 4});
    if (m.tempos_.empty() || m.tempos_.front().tick != 0)
        throw std::invalid_argument("tempo map needs a tempo event at tick 0");
    if (m.sigs_.front().tick != 0) throw std::invalid_argument("tempo map needs a signature event at tick 0");
    return m;
}

bool TempoMap::setTempo(Ticks tick, double bpm) {
    if (tick < 0 || !(bpm >= kMinBpm && bpm <= kMaxBpm)) return false;
    auto it = std::lower_bound(tempos_.begin(), tempos_.end(), tick,
                               [](const TempoEvent& e, Ticks t) { return e.tick < t; });
    if (it != tempos_.end() && it->tick == tick) it->bpm = bpm;
    else tempos_.insert(it, {tick, bpm});
    return true;
}

bool TempoMap::removeTempo(Ticks tick) {
    if (tick <= 0) return false;
    auto it = std::find_if(tempos_.begin(), tempos_.end(), [&](const TempoEvent& e) { return e.tick == tick; });
    if (it == tempos_.end()) return false;
    tempos_.erase(it);
    return true;
}

std::optional<double> TempoMap::tempoEventAt(Ticks tick) const {
    for (const auto& e : tempos_)
        if (e.tick == tick) return e.bpm;
    return std::nullopt;
}

bool TempoMap::setSignature(Ticks tick, int numerator, int denominator) {
    if (tick < 0 || !validSignature(numerator, denominator)) return false;
    auto it = std::lower_bound(sigs_.begin(), sigs_.end(), tick,
                               [](const SigEvent& e, Ticks t) { return e.tick < t; });
    if (it != sigs_.end() && it->tick == tick) {
        it->numerator = numerator;
        it->denominator = denominator;
    } else {
        sigs_.insert(it, {tick, numerator, denominator});
    }
    return true;
}

std::optional<std::pair<int, int>> TempoMap::signatureEventAt(Ticks tick) const {
    for (const SigEvent& e : sigs_)
        if (e.tick == tick) return std::make_pair(e.numerator, e.denominator);
    return std::nullopt;
}

bool TempoMap::removeSignature(Ticks tick) {
    if (tick <= 0) return false;
    const auto it = std::find_if(sigs_.begin(), sigs_.end(), [&](const SigEvent& e) { return e.tick == tick; });
    if (it == sigs_.end()) return false;
    sigs_.erase(it);
    return true;
}

double TempoMap::bpmAt(Ticks tick) const {
    double bpm = tempos_.front().bpm;
    for (const auto& e : tempos_) {
        if (e.tick > tick) break;
        bpm = e.bpm;
    }
    return bpm;
}

double TempoMap::ticksToSamples(Ticks tick, double sampleRate) const {
    if (tick <= 0) return double(tick) * samplesPerTick(tempos_.front().bpm, sampleRate);
    double acc = 0.0;
    for (std::size_t i = 0; i < tempos_.size(); ++i) {
        const bool last = i + 1 == tempos_.size();
        const Ticks segEnd = last ? tick : std::min(tick, tempos_[i + 1].tick);
        acc += double(segEnd - tempos_[i].tick) * samplesPerTick(tempos_[i].bpm, sampleRate);
        if (last || tick <= tempos_[i + 1].tick) break;
    }
    return acc;
}

Ticks TempoMap::samplesToTicks(double samples, double sampleRate) const {
    if (samples <= 0.0) return Ticks(std::floor(samples / samplesPerTick(tempos_.front().bpm, sampleRate)));
    double remaining = samples;
    for (std::size_t i = 0; i < tempos_.size(); ++i) {
        const double spt = samplesPerTick(tempos_[i].bpm, sampleRate);
        if (i + 1 == tempos_.size()) return tempos_[i].tick + Ticks(std::floor(remaining / spt));
        const double segSamples = double(tempos_[i + 1].tick - tempos_[i].tick) * spt;
        if (remaining < segSamples) return tempos_[i].tick + Ticks(std::floor(remaining / spt));
        remaining -= segSamples;
    }
    return 0;  // unreachable: the loop always returns on the last event
}

}  // namespace lpc
