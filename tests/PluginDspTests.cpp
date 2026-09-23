#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "PluginProcessor.h"
#include "dsp/Envelope.h"
#include "dsp/GainCurve.h"
#include "dsp/PumpProfile.h"

namespace
{
// Copied from tests/AnalysisTests.cpp so this file has no cross-TU test
// dependency.
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

// Minimal AudioPlayHead stub: reports only a sample position, set by the
// test between blocks, so the plugin's phase-lock path is exercised.
struct TestPlayHead : public juce::AudioPlayHead
{
    int64_t sample = 0;

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setTimeInSamples(sample);
        return info;
    }
};

void pumpMessageLoop(int ms)
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil(ms);
}

// Feeds `signal` through the processor block-by-block, advancing the test
// playhead in step, and returns the processed signal.
std::vector<float> runThroughProcessor(DePumpAudioProcessor& proc, TestPlayHead& playHead,
                                       const std::vector<float>& signal, int blockSize)
{
    std::vector<float> processed(signal.size());
    juce::MidiBuffer midi;
    size_t pos = 0;
    while (pos < signal.size())
    {
        const auto n = (int) std::min<size_t>((size_t) blockSize, signal.size() - pos);
        juce::AudioBuffer<float> buffer(1, n);
        for (int i = 0; i < n; ++i)
            buffer.setSample(0, i, signal[pos + (size_t) i]);

        proc.processBlock(buffer, midi);

        for (int i = 0; i < n; ++i)
            processed[pos + (size_t) i] = buffer.getSample(0, i);

        playHead.sample += n;
        pos += (size_t) n;
    }
    return processed;
}

// Feeds `signal` through the processor while polling the learn engine's
// status after every block (and pumping the message loop so callAsync can
// land), stopping as soon as a terminal status is reached or the signal is
// exhausted.
PluginLearnEngine::Status feedWhileLearning(DePumpAudioProcessor& proc, TestPlayHead& playHead,
                                            const std::vector<float>& signal, int blockSize)
{
    juce::MidiBuffer midi;
    size_t pos = 0;
    auto status = proc.getLearnEngineForTest().getStatus();
    while (pos < signal.size())
    {
        const auto n = (int) std::min<size_t>((size_t) blockSize, signal.size() - pos);
        juce::AudioBuffer<float> buffer(1, n);
        for (int i = 0; i < n; ++i)
            buffer.setSample(0, i, signal[pos + (size_t) i]);

        proc.processBlock(buffer, midi);
        playHead.sample += n;
        pos += (size_t) n;

        pumpMessageLoop(5);
        status = proc.getLearnEngineForTest().getStatus();
        if (status == PluginLearnEngine::Status::applied ||
            status == PluginLearnEngine::Status::noPumpDetected ||
            status == PluginLearnEngine::Status::error)
            return status;
    }
    return status;
}

// After the fixture is exhausted, capture/analysis may still be running
// (analysis is pure CPU work, unrelated to how much audio has been fed) —
// keep pumping the message loop until a terminal status lands or we time out.
PluginLearnEngine::Status waitForTerminalStatus(DePumpAudioProcessor& proc, int timeoutMs)
{
    const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    auto status = proc.getLearnEngineForTest().getStatus();
    while (status != PluginLearnEngine::Status::applied && status != PluginLearnEngine::Status::noPumpDetected &&
           status != PluginLearnEngine::Status::error && juce::Time::getMillisecondCounter() < deadline)
    {
        pumpMessageLoop(20);
        status = proc.getLearnEngineForTest().getStatus();
    }
    return status;
}
} // namespace

TEST_CASE("Learn fits a profile and the phase-locked correction matches clean audio")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    constexpr int timeoutMs = 30000;

    for (double sr : {44100.0, 48000.0, 96000.0})
    {
        for (int blockSize : {64, 100, 1024})
        {
            INFO("sr=" << sr << " blockSize=" << blockSize);

            DePumpAudioProcessor proc;
            TestPlayHead playHead;
            proc.setPlayHead(&playHead);
            proc.setPlayConfigDetails(1, 1, sr, blockSize);
            proc.prepareToPlay(sr, blockSize);

            const depump::PumpProfile profile{4.0f, 9.0f, 10.0f, 60.0f, 150.0f, 0.25f};
            const auto clean = makeCleanChord(sr, 6.0);
            const auto pumped = makePumped(clean, profile, sr);

            auto* learnParam = proc.apvts.getParameter(ParamID::learn);
            REQUIRE(learnParam != nullptr);
            playHead.sample = 0;
            learnParam->setValueNotifyingHost(1.0f);

            auto status = feedWhileLearning(proc, playHead, pumped, blockSize);
            if (status != PluginLearnEngine::Status::applied)
                status = waitForTerminalStatus(proc, timeoutMs);

            INFO("learn status message: " << proc.getLearnEngineForTest().getStatusMessage());
            REQUIRE(status == PluginLearnEngine::Status::applied);
            // Note: "engaged" (see PluginProcessor.cpp) flips true the next
            // time processBlock runs after the engine writes the six model
            // parameters, not the instant the engine writes them — no
            // processBlock call has happened since applyProfileOnMessageThread
            // ran, so checking it here (before the replay pass below) would
            // always see the pre-Learn value. Checked after replay instead.

            // Replay from the start of the timeline (playhead reset) so the
            // correction must phase-lock to the captured profile, not just
            // continue a free-running oscillator.
            playHead.sample = 0;
            const auto processed = runThroughProcessor(proc, playHead, pumped, blockSize);
            REQUIRE(proc.isEngagedForTest());

            // Skip the first second: the six model parameters snap from
            // their pre-Learn defaults to the just-learned values right at
            // the top of this replay and ramp in over the required 20 ms
            // smoothing window, and the GainOscillator's one-pole state
            // starts fresh at unity — both are settle transients, the same
            // kind already excluded in tests/DspTests.cpp and
            // tests/AnalysisTests.cpp, not steady-state error.
            const auto skipSamples = static_cast<size_t>(sr * 1.0);
            REQUIRE(clean.size() > skipSamples);
            const std::vector<float> cleanTail(clean.begin() + (long) skipSamples, clean.end());
            const std::vector<float> processedTail(processed.begin() + (long) skipSamples, processed.end());

            const auto cleanEnv = depump::extractRmsEnvelopeDb(cleanTail, sr);
            const auto processedEnv = depump::extractRmsEnvelopeDb(processedTail, sr);
            const auto deviation = depump::compareEnvelopes(processedEnv, cleanEnv);
            CHECK(deviation.maxAbsDb <= 1.0);
        }
    }
}

TEST_CASE("Do no harm: Learn on clean audio changes no parameter and leaves audio untouched")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    constexpr int timeoutMs = 30000;
    const double sr = 48000.0;
    const int blockSize = 512;

    DePumpAudioProcessor proc;
    TestPlayHead playHead;
    proc.setPlayHead(&playHead);
    proc.setPlayConfigDetails(1, 1, sr, blockSize);
    proc.prepareToPlay(sr, blockSize);

    const auto clean = makeCleanChord(sr, 6.0);

    const std::array<const char*, 6> modelParamIds{ParamID::freeRate, ParamID::depth,   ParamID::phase,
                                                    ParamID::attack,  ParamID::hold,    ParamID::release};
    std::array<float, 6> before{};
    for (size_t i = 0; i < modelParamIds.size(); ++i)
        before[i] = proc.apvts.getRawParameterValue(modelParamIds[i])->load();

    auto* learnParam = proc.apvts.getParameter(ParamID::learn);
    REQUIRE(learnParam != nullptr);
    playHead.sample = 0;
    learnParam->setValueNotifyingHost(1.0f);

    auto status = feedWhileLearning(proc, playHead, clean, blockSize);
    if (status != PluginLearnEngine::Status::noPumpDetected)
        status = waitForTerminalStatus(proc, timeoutMs);

    INFO("learn status message: " << proc.getLearnEngineForTest().getStatusMessage());
    CHECK(status == PluginLearnEngine::Status::noPumpDetected);
    CHECK_FALSE(proc.isEngagedForTest());

    for (size_t i = 0; i < modelParamIds.size(); ++i)
        CHECK(proc.apvts.getRawParameterValue(modelParamIds[i])->load() == Catch::Approx(before[i]));

    // Pass-through must still hold: feed clean audio again and compare bit-for-bit.
    playHead.sample = 0;
    const auto processed = runThroughProcessor(proc, playHead, clean, blockSize);
    REQUIRE(processed.size() == clean.size());
    for (size_t i = 0; i < clean.size(); i += 97)
        REQUIRE(processed[i] == clean[i]);
}
