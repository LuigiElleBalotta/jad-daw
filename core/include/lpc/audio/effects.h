#pragma once
#include <memory>

#include "lpc/audio/processors.h"

namespace lpc::audio {

// The built-in effects other than the gain (EQ, compressor, limiter, gate, delay, reverb). Returns null for an unknown id. Parameters
// missing from the ref take the default of the effect's spec; the processor runs at `sampleRate`.
std::unique_ptr<IProcessor> makeBuiltinEffect(const ProcessorRef& ref, double sampleRate);

// The magnitude (dB) of the channel EQ's response at `freq`, from its parameters; for the editor's curve and for tests.
double eqResponseDb(const ProcessorRef& ref, double sampleRate, double freq);

}  // namespace lpc::audio
