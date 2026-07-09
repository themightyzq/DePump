#pragma once

#include <vector>

#include "PumpAnalysis.h"

namespace depump
{

struct RecoveryOutcome
{
    PumpAnalysis analysis;
    float trimDb = 0.0f; // headroom trim applied after correction (<= 0)
};

// The product's core operation on one file's worth of audio, in memory:
// analyze the mono mix, and if pumping is detected apply the identical
// inverse gain to every channel (imaging intact) plus a headroom trim.
// Undetected audio is left untouched.
RecoveryOutcome analyzeAndRecover(std::vector<std::vector<float>>& channels, double sampleRate,
                                  float amount = 1.0f);

} // namespace depump
