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

void GainOscillator::cacheDerivedCoefficients() noexcept
{
    const float rateHz = profile.rateHz > 0.0f ? profile.rateHz : 0.0001f; // guard div-by-zero
    periodSamples = sampleRate / static_cast<double>(rateHz);
    dipStartSamples = static_cast<double>(profile.phase01) * periodSamples;
    dipWindowSamples = (static_cast<double>(profile.attackMs) + static_cast<double>(profile.holdMs)) * 0.001 * sampleRate;
    floorLin = dbToLin(-profile.depthDb);
    attackCoeff = onePoleCoeff(profile.attackMs, sampleRate);
    releaseCoeff = onePoleCoeff(profile.releaseMs, sampleRate);
}

void GainOscillator::reset(double sampleRateIn) noexcept
{
    sampleRate = sampleRateIn > 0.0 ? sampleRateIn : 44100.0;
    sampleIndex = 0;
    g = 1.0f;
    cacheDerivedCoefficients();
}

void GainOscillator::setProfile(const PumpProfile& profileIn) noexcept
{
    profile = profileIn;
    cacheDerivedCoefficients();
}

void GainOscillator::setPhaseReferenceSample(int64_t timelineSample) noexcept
{
    sampleIndex = timelineSample;
}

float GainOscillator::nextGain() noexcept
{
    double posInCycle = std::fmod(static_cast<double>(sampleIndex) - dipStartSamples, periodSamples);
    if (posInCycle < 0.0)
        posInCycle += periodSamples;

    const bool inDip = posInCycle < dipWindowSamples;
    const float target = inDip ? floorLin : 1.0f;
    const float coeff = target < g ? attackCoeff : releaseCoeff;
    g += coeff * (target - g);
    ++sampleIndex;
    return g;
}

float invertSample(float sample, float gain, float amount) noexcept
{
    constexpr float minGain = 1.0e-4f; // -80 dB guard, far below the 24 dB param ceiling
    const float g = std::max(gain, minGain);
    return sample * std::pow(g, -amount);
}

std::vector<float> synthesizeGainCurve(const PumpProfile& profile, double sampleRate, size_t numSamples)
{
    assert(sampleRate > 0.0 && profile.rateHz > 0.0f);

    GainOscillator osc;
    osc.reset(sampleRate);
    osc.setProfile(profile);

    std::vector<float> gain(numSamples);
    for (size_t n = 0; n < numSamples; ++n)
        gain[n] = osc.nextGain();
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
    const size_t n = std::min(samples.size(), gain.size());
    for (size_t i = 0; i < n; ++i)
        samples[i] = invertSample(samples[i], gain[i], amount);
}

} // namespace depump
