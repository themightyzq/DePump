#pragma once

#include <cstddef>
#include <vector>

#include "PumpProfile.h"

namespace depump
{

// Result of fully-automatic pump detection — no tempo/beat input.
struct PumpAnalysis
{
    bool pumpDetected = false;
    double confidence = 0.0;     // normalized autocorrelation peak, 0..1
    double periodSeconds = 0.0;  // estimated pump cycle length
    double dipTimeSeconds = 0.0; // gain-minimum position within [0, period)
    float depthDb = 0.0f;        // template peak-to-trough

    // One cycle of measured gain relative to the cycle's reference level,
    // in dB (<= 0). Bin 0 corresponds to absolute time 0 (mod period), so
    // the template carries its own phase alignment.
    std::vector<float> templateGainDb;

    // When one-pole compressor dynamics explain the template, the fitted
    // parameters (human-readable, and used for exact reconstruction —
    // including how far below unity the cycle top sits when the release
    // never completes). modelFitted=false means the raw template is used.
    bool modelFitted = false;
    PumpProfile fittedProfile;
};

// Estimate the pump profile from audio alone. Needs at least ~4 cycles of
// material to fold; returns pumpDetected=false when periodicity is weak,
// modulation is negligible, or the signal is too short.
PumpAnalysis analyzePump(const std::vector<float>& samples, double sampleRate);

// Expand the measured template into a per-sample gain curve (linear, <= 1)
// aligned to the analyzed file, ready for applyInverseGain().
std::vector<float> gainCurveFromAnalysis(const PumpAnalysis& analysis, double sampleRate, size_t numSamples);

} // namespace depump
