#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <vector>

#include "dsp/Envelope.h"
#include "dsp/GainCurve.h"
#include "dsp/PumpAnalysis.h"
#include "dsp/PumpProfile.h"

namespace
{
// Deterministic white-ish noise (no rand(): tests must be reproducible).
float cheapNoise(uint32_t& state)
{
    state = state * 1664525u + 1013904223u;
    return (static_cast<float>(state >> 8) / 8388608.0f - 1.0f);
}

// Program-varying "pad": chord with a slow musical level swell and a bed of
// noise — much closer to a real stem than a constant-amplitude tone.
std::vector<float> makeProgramMaterial(double sampleRate, double seconds, float swellDb)
{
    const auto n = static_cast<size_t>(sampleRate * seconds);
    std::vector<float> samples(n);
    constexpr double freqs[] = {220.0, 277.18, 329.63};
    uint32_t noiseState = 0x2E1u;
    for (size_t i = 0; i < n; ++i)
    {
        const double t = static_cast<double>(i) / sampleRate;
        double value = 0.0;
        for (double freq : freqs)
            value += 0.2 * std::sin(2.0 * 3.14159265358979 * freq * t);
        const double swell = std::pow(10.0, swellDb / 20.0 * std::sin(2.0 * 3.14159265358979 * 0.08 * t));
        samples[i] = static_cast<float>(value * swell) + 0.01f * cheapNoise(noiseState);
    }
    return samples;
}
} // namespace

TEST_CASE("trimToCeiling scales all channels identically and reports the trim")
{
    std::vector<std::vector<float>> channels(2, std::vector<float>(100, 0.5f));
    channels[0][10] = 2.0f; // hot peak in one channel only

    const float trimDb = depump::trimToCeiling(channels);

    CHECK(trimDb == Catch::Approx(20.0f * std::log10(0.999f / 2.0f)).margin(0.01));
    CHECK(channels[0][10] == Catch::Approx(0.999f).margin(1.0e-4));
    // The other channel must be scaled by the same factor (imaging intact).
    CHECK(channels[1][10] == Catch::Approx(0.5f * 0.999f / 2.0f).margin(1.0e-4));

    // Already-safe audio is untouched.
    std::vector<std::vector<float>> safe(1, std::vector<float>(100, 0.25f));
    CHECK(depump::trimToCeiling(safe) == 0.0f);
    CHECK(safe[0][0] == 0.25f);
}

TEST_CASE("Automatic recovery survives musical level swells and noise")
{
    const double sr = 48000.0;
    const auto clean = makeProgramMaterial(sr, 8.0, 3.0f); // +-3 dB swell
    const depump::PumpProfile profile{2.0f, 6.0f, 10.0f, 60.0f, 150.0f, 0.25f};

    auto audio = clean;
    const auto trueGain = depump::synthesizeGainCurve(profile, sr, audio.size());
    depump::applyGain(audio, trueGain);

    const auto analysis = depump::analyzePump(audio, sr);
    REQUIRE(analysis.pumpDetected);
    CHECK(analysis.periodSeconds == Catch::Approx(0.5).epsilon(0.01));

    const auto gain = depump::gainCurveFromAnalysis(analysis, sr, audio.size());
    depump::applyInverseGain(audio, gain);

    const auto deviation = depump::compareEnvelopes(depump::extractRmsEnvelopeDb(audio, sr),
                                                    depump::extractRmsEnvelopeDb(clean, sr));
    INFO("maxDev=" << deviation.maxAbsDb << " rms=" << deviation.rmsDb);
    // Program material is harder than static fixtures; the bar is "pumping
    // audibly gone" (from 6 dB baked in), not numerical perfection.
    CHECK(deviation.maxAbsDb <= 1.0);
    CHECK(deviation.rmsDb <= 0.25);
}

TEST_CASE("Do no harm: swelling, noisy, unpumped material is not corrected")
{
    const double sr = 48000.0;
    const auto clean = makeProgramMaterial(sr, 8.0, 3.0f);

    const auto analysis = depump::analyzePump(clean, sr);
    CHECK_FALSE(analysis.pumpDetected);
}
