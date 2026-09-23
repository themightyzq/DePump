#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "PumpProfile.h"

namespace depump
{

// Stateful, allocation-free, per-sample source for the one-pole pump gain
// curve. This is the real-time-safe core that synthesizeGainCurve() (below)
// wraps for offline/whole-buffer use, and that the plugin's processBlock
// drives directly, one sample at a time, so it can resync to the host
// timeline every block without re-synthesizing a whole buffer.
//
// Not thread-safe: an instance belongs to one thread (the audio thread in
// the plugin, or a single offline call) for its whole lifetime.
class GainOscillator
{
public:
    // (Re)starts the internal one-pole state at unity and adopts a new
    // sample rate. Safe to call from a non-realtime thread only (it is
    // meant for prepareToPlay-style setup), never from processBlock.
    void reset(double sampleRate) noexcept;

    // Adopts a new pump profile. Cheap (a handful of pow/exp calls to cache
    // derived coefficients) — call it at block rate, not per sample.
    void setProfile(const PumpProfile& profile) noexcept;

    // Re-anchors the internal sample counter that phase is computed from,
    // e.g. to (hostPlayheadSample - captureStartSample) so the curve tracks
    // the host timeline instead of free-running. Cheap; call every block.
    void setPhaseReferenceSample(int64_t timelineSample) noexcept;

    // Advances one sample and returns the resulting gain (0 < g <= 1).
    float nextGain() noexcept;

private:
    void cacheDerivedCoefficients() noexcept;

    double sampleRate = 44100.0;
    PumpProfile profile{};
    int64_t sampleIndex = 0;
    float g = 1.0f;

    // Derived from `profile`/`sampleRate` by cacheDerivedCoefficients().
    double periodSamples = 44100.0;
    double dipStartSamples = 0.0;
    double dipWindowSamples = 0.0;
    float floorLin = 1.0f;
    float attackCoeff = 0.0f;
    float releaseCoeff = 0.0f;
};

// Undoes one sample of gain: out = in * gain^(-amount). amount 0..1 scales
// the correction (1 = full inverse). gain is clamped away from zero before
// inversion. The scalar core applyInverseGain() (below) wraps for buffers.
float invertSample(float sample, float gain, float amount) noexcept;

// Synthesize the per-sample gain curve g[n] (0 < g <= 1) a sidechain
// compressor with this profile would have applied. Deterministic; the
// one-pole state starts at unity, so the first cycle carries a settle
// transient — harmless for fixtures because pump and inverse use the
// identical curve. A thin wrapper over GainOscillator.
std::vector<float> synthesizeGainCurve(const PumpProfile& profile, double sampleRate, size_t numSamples);

// out[n] = in[n] * g[n] — bake pumping into a clean signal (fixtures).
void applyGain(std::vector<float>& samples, const std::vector<float>& gain);

// out[n] = in[n] * g[n]^(-amount) — undo pumping. amount 0..1 scales the
// correction (1 = full inverse). Gains are clamped away from zero before
// inversion. A thin wrapper over invertSample().
void applyInverseGain(std::vector<float>& samples, const std::vector<float>& gain, float amount = 1.0f);

// Headroom management: recovery gain can push peaks past full scale, and a
// batch tool must hand back files safe to bounce anywhere. Returns the trim
// applied in dB (<= 0; 0 when the peak already fits the ceiling) and scales
// every channel identically so imaging is untouched.
float trimToCeiling(std::vector<std::vector<float>>& channels, float ceilingLinear = 0.999f);

} // namespace depump
