#pragma once

#include <cstddef>
#include <functional>
#include <vector>

#include "PumpProfile.h"

namespace depump
{

// Optional hooks for a caller that runs analyzePump on a worker thread (the plugin's Learn).
// Both are called on the analysing thread, never from a real-time thread. Pure std types, so
// the DSP core stays host-free.
struct AnalysisControl
{
    // Polled between work units (every candidate evaluation of the fit, so within a few ms).
    // Returning true abandons the analysis: the result has cancelled = true and nothing else
    // in it may be used.
    std::function<bool()> shouldCancel;

    // Coarse progress in 0..1, never decreasing. May be left empty.
    std::function<void(float)> onProgress;
};

// Result of fully-automatic pump detection — no tempo/beat input.
struct PumpAnalysis
{
    bool cancelled = false;      // AnalysisControl::shouldCancel fired; the rest is meaningless
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
PumpAnalysis analyzePump(const std::vector<float>& samples, double sampleRate,
                         const AnalysisControl* control = nullptr);

// Expand the measured template into a per-sample gain curve (linear, <= 1)
// aligned to the analyzed file, ready for applyInverseGain().
std::vector<float> gainCurveFromAnalysis(const PumpAnalysis& analysis, double sampleRate, size_t numSamples);

} // namespace depump
