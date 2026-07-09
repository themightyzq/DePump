#include "GainCurve.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace depump
{

namespace
{
float dbToLin(float db) { return std::pow(10.0f, db / 20.0f); }

float onePoleCoeff(float ms, double sampleRate)
{
    const double samples = std::max(1.0, ms * 0.001 * sampleRate);
    return static_cast<float>(1.0 - std::exp(-1.0 / samples));
}
} // namespace

std::vector<float> synthesizeGainCurve(const PumpProfile& profile, double sampleRate, size_t numSamples)
{
    assert(sampleRate > 0.0 && profile.rateHz > 0.0f);

    const double periodSamples = sampleRate / profile.rateHz;
    const double dipStart = static_cast<double>(profile.phase01) * periodSamples;
    const double dipWindowSamples = (profile.attackMs + profile.holdMs) * 0.001 * sampleRate;
    const float floorLin = dbToLin(-profile.depthDb);
    const float attackCoeff = onePoleCoeff(profile.attackMs, sampleRate);
    const float releaseCoeff = onePoleCoeff(profile.releaseMs, sampleRate);

    std::vector<float> gain(numSamples);
    float g = 1.0f;

    for (size_t n = 0; n < numSamples; ++n)
    {
        double posInCycle = std::fmod(static_cast<double>(n) - dipStart, periodSamples);
        if (posInCycle < 0.0)
            posInCycle += periodSamples;

        const bool inDip = posInCycle < dipWindowSamples;
        const float target = inDip ? floorLin : 1.0f;
        const float coeff = target < g ? attackCoeff : releaseCoeff;
        g += coeff * (target - g);
        gain[n] = g;
    }

    return gain;
}

void applyGain(std::vector<float>& samples, const std::vector<float>& gain)
{
    const size_t n = std::min(samples.size(), gain.size());
    for (size_t i = 0; i < n; ++i)
        samples[i] *= gain[i];
}

float trimToCeiling(std::vector<std::vector<float>>& channels, float ceilingLinear)
{
    float peak = 0.0f;
    for (const auto& channel : channels)
        for (float sample : channel)
            peak = std::max(peak, std::abs(sample));

    if (peak <= ceilingLinear || peak <= 0.0f)
        return 0.0f;

    const float scale = ceilingLinear / peak;
    for (auto& channel : channels)
        for (auto& sample : channel)
            sample *= scale;
    return 20.0f * std::log10(scale);
}

void applyInverseGain(std::vector<float>& samples, const std::vector<float>& gain, float amount)
{
    constexpr float minGain = 1.0e-4f; // -80 dB guard, far below the 24 dB param ceiling
    const size_t n = std::min(samples.size(), gain.size());
    for (size_t i = 0; i < n; ++i)
    {
        const float g = std::max(gain[i], minGain);
        samples[i] *= std::pow(g, -amount);
    }
}

} // namespace depump
