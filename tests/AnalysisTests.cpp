#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

#include "dsp/Envelope.h"
#include "dsp/GainCurve.h"
#include "dsp/PumpAnalysis.h"
#include "dsp/PumpProfile.h"

namespace
{
// Constant-amplitude chord, like the CLI fixture generator.
std::vector<float> makeCleanChord(double sampleRate, double seconds)
{
    const auto n = static_cast<size_t>(sampleRate * seconds);
    std::vector<float> samples(n);
    constexpr double freqs[] = {220.0, 277.18, 329.63};
    for (size_t i = 0; i < n; ++i)
    {
        double value = 0.0;
        for (double freq : freqs)
            value += 0.2 * std::sin(2.0 * 3.14159265358979 * freq * static_cast<double>(i) / sampleRate);
        samples[i] = static_cast<float>(value);
    }
    return samples;
}

std::vector<float> makePumped(const std::vector<float>& clean, const depump::PumpProfile& profile,
                              double sampleRate)
{
    auto pumped = clean;
    const auto gain = depump::synthesizeGainCurve(profile, sampleRate, pumped.size());
    depump::applyGain(pumped, gain);
    return pumped;
}

// Circular distance between two positions on a cycle.
double circularDistance(double a, double b, double period)
{
    const double diff = std::abs(std::fmod(std::abs(a - b), period));
    return std::min(diff, period - diff);
}
} // namespace

TEST_CASE("Analysis estimates the pump period within 1 percent, no tempo input")
{
    for (double sr : {44100.0, 96000.0})
    {
        for (float rate : {1.0f, 2.0f, 4.0f})
        {
            const depump::PumpProfile profile{rate, 9.0f, 10.0f, 60.0f, 150.0f, 0.3f};
            const auto pumped = makePumped(makeCleanChord(sr, 8.0), profile, sr);

            const auto analysis = depump::analyzePump(pumped, sr);

            INFO("sr=" << sr << " rate=" << rate);
            REQUIRE(analysis.pumpDetected);
            CHECK(analysis.periodSeconds ==
                  Catch::Approx(1.0 / static_cast<double>(rate)).epsilon(0.01));
        }
    }
}

// Phase accuracy is judged at the attack EDGE (half-depth downward
// crossing): the dip bottom is a plateau — the one-pole sits at the floor
// for the hold duration — so an argmin position is ambiguous by several ms
// between audibly identical curves. The edge is the recovery-relevant,
// well-posed phase feature.
namespace
{
double halfDepthFallTime(const std::vector<float>& gain, double sr, double periodSec)
{
    float minGain = 1.0f;
    const auto start = static_cast<size_t>(sr); // skip settle transient
    const auto end = start + static_cast<size_t>(periodSec * sr * 2.0);
    for (size_t i = start; i < end; ++i)
        minGain = std::min(minGain, gain[i]);

    const float halfDepth = (1.0f + minGain) / 2.0f;
    for (size_t i = start + 1; i < end; ++i)
        if (gain[i - 1] >= halfDepth && gain[i] < halfDepth)
            return std::fmod(static_cast<double>(i) / sr, periodSec);
    return -1.0;
}
} // namespace

TEST_CASE("Analysis locates the dip's attack edge within 5 ms")
{
    const double sr = 48000.0;
    for (float phase : {0.0f, 0.25f, 0.6f})
    {
        const depump::PumpProfile profile{2.0f, 9.0f, 10.0f, 60.0f, 150.0f, phase};
        const auto pumped = makePumped(makeCleanChord(sr, 8.0), profile, sr);

        const auto analysis = depump::analyzePump(pumped, sr);
        REQUIRE(analysis.pumpDetected);

        const auto trueGain = depump::synthesizeGainCurve(profile, sr, static_cast<size_t>(sr * 4.0));
        const auto estGain = depump::gainCurveFromAnalysis(analysis, sr, static_cast<size_t>(sr * 4.0));

        const double trueEdge = halfDepthFallTime(trueGain, sr, 0.5);
        const double estEdge = halfDepthFallTime(estGain, sr, analysis.periodSeconds);
        REQUIRE(trueEdge >= 0.0);
        REQUIRE(estEdge >= 0.0);

        INFO("phase=" << phase);
        CHECK(circularDistance(estEdge, trueEdge, 0.5) < 0.005);
    }
}

TEST_CASE("Automatic recovery brings fixtures within 0.5 dB of clean")
{
    const double sr = 48000.0;
    for (float depth : {3.0f, 6.0f, 12.0f})
    {
        const depump::PumpProfile profile{2.0f, depth, 10.0f, 60.0f, 150.0f, 0.25f};
        const auto clean = makeCleanChord(sr, 8.0);
        auto audio = makePumped(clean, profile, sr);

        const auto analysis = depump::analyzePump(audio, sr);
        REQUIRE(analysis.pumpDetected);
        const auto gain = depump::gainCurveFromAnalysis(analysis, sr, audio.size());
        depump::applyInverseGain(audio, gain);

        const auto deviation = depump::compareEnvelopes(depump::extractRmsEnvelopeDb(audio, sr),
                                                        depump::extractRmsEnvelopeDb(clean, sr));
        INFO("depth=" << depth << " maxDev=" << deviation.maxAbsDb);
        CHECK(deviation.maxAbsDb <= 0.5);
    }
}

TEST_CASE("Do no harm: clean audio is detected as unpumped and left untouched")
{
    const double sr = 48000.0;
    const auto clean = makeCleanChord(sr, 8.0);

    const auto analysis = depump::analyzePump(clean, sr);
    CHECK_FALSE(analysis.pumpDetected);

    // Even if applied, the curve for an undetected pump must be identity.
    auto audio = clean;
    const auto gain = depump::gainCurveFromAnalysis(analysis, sr, audio.size());
    depump::applyInverseGain(audio, gain);

    const auto deviation = depump::compareEnvelopes(depump::extractRmsEnvelopeDb(audio, sr),
                                                    depump::extractRmsEnvelopeDb(clean, sr));
    CHECK(deviation.maxAbsDb <= 0.1);
}

TEST_CASE("Too-short input reports no detection instead of guessing")
{
    const double sr = 48000.0;
    const depump::PumpProfile profile{2.0f, 9.0f, 10.0f, 60.0f, 150.0f, 0.0f};
    // Half a second: a single pump cycle — not enough to fold.
    const auto pumped = makePumped(makeCleanChord(sr, 0.5), profile, sr);

    const auto analysis = depump::analyzePump(pumped, sr);
    CHECK_FALSE(analysis.pumpDetected);
}
