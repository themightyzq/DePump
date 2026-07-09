#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

#include "dsp/Envelope.h"
#include "dsp/GainCurve.h"
#include "dsp/PumpProfile.h"

namespace
{
std::vector<float> makeSineMix(double sampleRate, double seconds)
{
    const auto n = static_cast<size_t>(sampleRate * seconds);
    std::vector<float> samples(n);
    for (size_t i = 0; i < n; ++i)
        samples[i] = 0.3f * std::sin(2.0f * 3.14159265f * 220.0f * (float) i / (float) sampleRate) +
                     0.2f * std::sin(2.0f * 3.14159265f * 331.0f * (float) i / (float) sampleRate);
    return samples;
}
} // namespace

TEST_CASE("Gain curve reaches the dip floor and never exceeds unity")
{
    for (double sr : {44100.0, 96000.0})
    {
        depump::PumpProfile profile{2.0f, 12.0f, 10.0f, 60.0f, 150.0f, 0.0f};
        const auto gain = depump::synthesizeGainCurve(profile, sr, (size_t) (sr * 4.0));

        float minGain = 1.0f, maxGain = 0.0f;
        // Skip the first cycle: the one-pole state settles from unity.
        for (size_t i = (size_t) (sr / 2); i < gain.size(); ++i)
        {
            minGain = std::min(minGain, gain[i]);
            maxGain = std::max(maxGain, gain[i]);
        }

        const float floorLin = std::pow(10.0f, -12.0f / 20.0f);
        CHECK(maxGain <= 1.0f);
        CHECK(minGain == Catch::Approx(floorLin).margin(0.02));
        CHECK(minGain > 0.0f);
    }
}

TEST_CASE("Gain curve is periodic at the pump rate after settling")
{
    const double sr = 48000.0;
    depump::PumpProfile profile{2.0f, 6.0f, 10.0f, 60.0f, 150.0f, 0.25f};
    const auto gain = depump::synthesizeGainCurve(profile, sr, (size_t) (sr * 4.0));

    const auto period = (size_t) (sr / 2.0); // 2 Hz
    for (size_t i = (size_t) sr; i < (size_t) (sr * 2.5); i += 97)
        CHECK(gain[i] == Catch::Approx(gain[i + period]).margin(1.0e-3));
}

TEST_CASE("Inverse application recovers the clean signal from a known profile")
{
    const double sr = 48000.0;
    depump::PumpProfile profile{2.0f, 9.0f, 15.0f, 60.0f, 120.0f, 0.1f};

    const auto clean = makeSineMix(sr, 4.0);
    auto processed = clean;

    const auto gain = depump::synthesizeGainCurve(profile, sr, processed.size());
    depump::applyGain(processed, gain);      // bake the pumping in
    depump::applyInverseGain(processed, gain); // undo it

    float maxError = 0.0f;
    for (size_t i = 0; i < clean.size(); ++i)
        maxError = std::max(maxError, std::abs(processed[i] - clean[i]));

    // Full recovery to numeric precision — the whole point of the product.
    CHECK(maxError < 1.0e-5f);
}

TEST_CASE("Amount 0 leaves the pumped signal untouched")
{
    const double sr = 44100.0;
    depump::PumpProfile profile{4.0f, 6.0f, 10.0f, 40.0f, 100.0f, 0.0f};
    auto samples = makeSineMix(sr, 1.0);
    const auto pumped = samples;

    const auto gain = depump::synthesizeGainCurve(profile, sr, samples.size());
    depump::applyInverseGain(samples, gain, 0.0f);

    for (size_t i = 0; i < samples.size(); i += 31)
        REQUIRE(samples[i] == pumped[i]);
}

TEST_CASE("RMS envelope of a constant sine is flat at the predicted level")
{
    const double sr = 48000.0;
    const float amplitude = 0.5f;
    std::vector<float> sine((size_t) (sr * 2.0));
    for (size_t i = 0; i < sine.size(); ++i)
        sine[i] = amplitude * std::sin(2.0f * 3.14159265f * 440.0f * (float) i / (float) sr);

    const auto envelope = depump::extractRmsEnvelopeDb(sine, sr);
    REQUIRE(envelope.size() > 100);

    const double expectedDb = 20.0 * std::log10(amplitude / std::sqrt(2.0));
    for (const auto& point : envelope)
        REQUIRE(point.rmsDb == Catch::Approx(expectedDb).margin(0.1));
}

TEST_CASE("Envelope comparison reports exact known deviations")
{
    std::vector<depump::EnvelopePoint> a, b;
    for (int i = 0; i < 100; ++i)
    {
        a.push_back({i * 0.005, -20.0});
        b.push_back({i * 0.005, i == 50 ? -23.0 : -20.0});
    }

    const auto identical = depump::compareEnvelopes(a, a);
    CHECK(identical.maxAbsDb == 0.0);
    CHECK(identical.rmsDb == 0.0);

    const auto deviation = depump::compareEnvelopes(a, b);
    CHECK(deviation.maxAbsDb == Catch::Approx(3.0));
    CHECK(deviation.pointsCompared == 100);
}

TEST_CASE("Envelope CSV round-trips and rejects malformed input")
{
    std::vector<depump::EnvelopePoint> envelope{{0.0, -18.5}, {0.005, -19.25}, {0.010, -60.0}};

    const auto csv = depump::envelopeToCsv(envelope);
    const auto parsed = depump::envelopeFromCsv(csv);

    REQUIRE(parsed.size() == envelope.size());
    for (size_t i = 0; i < parsed.size(); ++i)
    {
        CHECK(parsed[i].timeSec == Catch::Approx(envelope[i].timeSec));
        CHECK(parsed[i].rmsDb == Catch::Approx(envelope[i].rmsDb));
    }

    CHECK_THROWS(depump::envelopeFromCsv("bogus header\n1,2\n"));
    CHECK_THROWS(depump::envelopeFromCsv("time_sec,rms_db\nno-comma-here\n"));
}
