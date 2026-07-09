#pragma once

#include <cstddef>
#include <vector>

#include "PumpProfile.h"

namespace depump
{

// Synthesize the per-sample gain curve g[n] (0 < g <= 1) a sidechain
// compressor with this profile would have applied. Deterministic; the
// one-pole state starts at unity, so the first cycle carries a settle
// transient — harmless for fixtures because pump and inverse use the
// identical curve.
std::vector<float> synthesizeGainCurve(const PumpProfile& profile, double sampleRate, size_t numSamples);

// out[n] = in[n] * g[n] — bake pumping into a clean signal (fixtures).
void applyGain(std::vector<float>& samples, const std::vector<float>& gain);

// out[n] = in[n] * g[n]^(-amount) — undo pumping. amount 0..1 scales the
// correction (1 = full inverse). Gains are clamped away from zero before
// inversion.
void applyInverseGain(std::vector<float>& samples, const std::vector<float>& gain, float amount = 1.0f);

// Headroom management: recovery gain can push peaks past full scale, and a
// batch tool must hand back files safe to bounce anywhere. Returns the trim
// applied in dB (<= 0; 0 when the peak already fits the ceiling) and scales
// every channel identically so imaging is untouched.
float trimToCeiling(std::vector<std::vector<float>>& channels, float ceilingLinear = 0.999f);

} // namespace depump
