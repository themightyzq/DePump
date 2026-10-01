#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <memory>
#include <vector>

#include "PluginProcessor.h"
#include "dsp/Envelope.h"
#include "dsp/GainCurve.h"
#include "dsp/PumpAnalysis.h"
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
// reportPosition = false models a host that gives no position at all.
struct TestPlayHead : public juce::AudioPlayHead
{
    int64_t sample = 0;
    bool reportPosition = true;

    juce::Optional<PositionInfo> getPosition() const override
    {
        if (!reportPosition)
            return {};
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

namespace
{
// Runs Learn on clean audio WITHOUT pumping the message loop, and returns once
// the background thread has finished analysing. The result lambda is then
// still queued on the message thread.
bool learnUntilAnalysisDoneWithoutDispatch(DePumpAudioProcessor& proc, TestPlayHead& playHead, double sr,
                                           int blockSize)
{
    auto* learnParam = proc.apvts.getParameter(ParamID::learn);
    if (learnParam == nullptr)
        return false;
    playHead.sample = 0;
    learnParam->setValueNotifyingHost(1.0f);
    runThroughProcessor(proc, playHead, makeCleanChord(sr, 6.0), blockSize);

    const auto deadline = juce::Time::getMillisecondCounter() + 30000u;
    while (proc.getLearnEngineForTest().getStatus() != PluginLearnEngine::Status::noPumpDetected &&
           juce::Time::getMillisecondCounter() < deadline)
        juce::Thread::sleep(10);
    return proc.getLearnEngineForTest().getStatus() == PluginLearnEngine::Status::noPumpDetected;
}
} // namespace

TEST_CASE("Learn result queued for the message thread is inert after the processor is destroyed")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    constexpr double sr = 48000.0;
    constexpr int blockSize = 512;
    {
        DePumpAudioProcessor proc;
        TestPlayHead playHead;
        proc.setPlayHead(&playHead);
        proc.setPlayConfigDetails(1, 1, sr, blockSize);
        proc.prepareToPlay(sr, blockSize);
        REQUIRE(learnUntilAnalysisDoneWithoutDispatch(proc, playHead, sr, blockSize));
    } // engine destroyed with its callAsync still pending
    pumpMessageLoop(200); // must not touch the destroyed engine
    SUCCEED();
}

TEST_CASE("prepare() cancels an in-flight Learn result instead of applying it later")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    constexpr double sr = 48000.0;
    constexpr int blockSize = 512;

    DePumpAudioProcessor proc;
    TestPlayHead playHead;
    proc.setPlayHead(&playHead);
    proc.setPlayConfigDetails(1, 1, sr, blockSize);
    proc.prepareToPlay(sr, blockSize);
    REQUIRE(learnUntilAnalysisDoneWithoutDispatch(proc, playHead, sr, blockSize));

    proc.prepareToPlay(44100.0, blockSize); // sample-rate change -> engine prepare()
    pumpMessageLoop(200);

    CHECK(proc.getLearnEngineForTest().getStatus() == PluginLearnEngine::Status::idle);
    // The stale result would have reset the Learn parameter to 0.
    CHECK(proc.apvts.getRawParameterValue(ParamID::learn)->load() >= 0.5f);
}

// ---------------------------------------------------------------------------------------------
// 2026-09-29 review backlog: safety clip, stopped transport, learn automation, thread exit.
// ---------------------------------------------------------------------------------------------
namespace
{
// Runs one block of `value`-filled mono samples; used to step the "engaged" detector.
void processSilenceBlock(DePumpAudioProcessor& proc, int blockSize)
{
    juce::AudioBuffer<float> buffer(1, blockSize);
    buffer.clear();
    juce::MidiBuffer midi;
    proc.processBlock(buffer, midi);
}

// Sets a parameter by its real (denormalised) value.
void setParam(DePumpAudioProcessor& proc, const char* id, float value)
{
    auto* p = proc.apvts.getParameter(id);
    REQUIRE(p != nullptr);
    p->setValueNotifyingHost(p->convertTo0to1(value));
}

// Builds a processor that is engaged (applies its correction path) with the given Amount and
// Output. Amount/Output are set BEFORE prepareToPlay so their smoothers start at the target.
std::unique_ptr<DePumpAudioProcessor> makeEngagedProcessor(double sr, int blockSize, float amountPercent,
                                                           float outputDb, TestPlayHead* playHead)
{
    auto proc = std::make_unique<DePumpAudioProcessor>();
    if (playHead != nullptr)
        proc->setPlayHead(playHead);
    setParam(*proc, ParamID::amount, amountPercent);
    setParam(*proc, ParamID::output, outputDb);
    setParam(*proc, ParamID::syncMode, 1.0f); // Free
    proc->setPlayConfigDetails(1, 1, sr, blockSize);
    proc->prepareToPlay(sr, blockSize);

    processSilenceBlock(*proc, blockSize);      // first block samples the "last seen" values
    setParam(*proc, ParamID::depth, 12.0f);     // a model edit engages the correction
    setParam(*proc, ParamID::freeRate, 4.0f);
    processSilenceBlock(*proc, blockSize);
    return proc;
}
} // namespace

TEST_CASE("Safety clip is bit-transparent below -1 dBFS when no correction is applied")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    for (double sr : {44100.0, 48000.0, 96000.0})
    {
        for (int blockSize : {64, 100, 1024})
        {
            INFO("sr=" << sr << " blockSize=" << blockSize);
            TestPlayHead playHead;
            auto proc = makeEngagedProcessor(sr, blockSize, 0.0f, 0.0f, &playHead);
            REQUIRE(proc->isEngagedForTest());

            // -6 dBFS sine: the old tanh clip turned this into about -6.4 dBFS.
            std::vector<float> sine(static_cast<size_t>(sr));
            for (size_t i = 0; i < sine.size(); ++i)
                sine[i] = 0.5012f * std::sin(2.0f * 3.14159265f * 440.0f * static_cast<float>(i) / static_cast<float>(sr));

            const auto out = runThroughProcessor(*proc, playHead, sine, blockSize);
            float maxError = 0.0f;
            for (size_t i = 0; i < sine.size(); ++i)
                maxError = std::max(maxError, std::abs(out[i] - sine[i]));
            CHECK(maxError < 1.0e-6f);
        }
    }
}

TEST_CASE("Safety clip still limits hot signals, monotonically and without a kink")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    constexpr double sr = 48000.0;
    constexpr int blockSize = 512;
    TestPlayHead playHead;
    auto proc = makeEngagedProcessor(sr, blockSize, 0.0f, 0.0f, &playHead);

    // A slow ramp through the knee and well past full scale, both signs.
    constexpr int n = 4 * blockSize * 8;
    std::vector<float> ramp(n);
    for (int i = 0; i < n; ++i)
        ramp[(size_t) i] = -1.5f + 3.0f * static_cast<float>(i) / static_cast<float>(n - 1);

    const auto out = runThroughProcessor(*proc, playHead, ramp, blockSize);
    constexpr float ceiling = 0.96605f;
    float previous = out[0];
    for (size_t i = 0; i < out.size(); ++i)
    {
        REQUIRE(std::isfinite(out[i]));
        REQUIRE(std::abs(out[i]) <= ceiling);
        if (std::abs(ramp[i]) <= 0.89f)
            REQUIRE(out[i] == ramp[i]);
        if (i > 0)
        {
            REQUIRE(out[i] >= previous);                                   // monotonic
            REQUIRE(out[i] - previous <= (ramp[i] - ramp[i - 1]) * 1.001f + 1.0e-7f); // slope <= 1: no kink up
        }
        previous = out[i];
    }
    CHECK(out.back() > 0.95f); // it limits to the ceiling, it does not flatten early
}

TEST_CASE("A stopped transport (frozen host position) behaves like a host with no position")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    constexpr double sr = 48000.0;
    constexpr int blockSize = 512;

    // B: a stopped transport, reporting position 0 forever. A: a host that reports nothing,
    // so the plugin free-runs. Before the fix B re-anchored the pump curve to position 0 every
    // block (a block-rate buzz that never reaches the dip); A and B must now be identical.
    TestPlayHead noPosition;
    noPosition.reportPosition = false;
    TestPlayHead stopped;
    stopped.sample = 0;

    auto a = makeEngagedProcessor(sr, blockSize, 100.0f, 0.0f, &noPosition);
    auto b = makeEngagedProcessor(sr, blockSize, 100.0f, 0.0f, &stopped);

    std::vector<float> tone(static_cast<size_t>(blockSize) * 64); // 0.68 s: several pump cycles
    for (size_t i = 0; i < tone.size(); ++i)
        tone[i] = 0.2f * std::sin(2.0f * 3.14159265f * 220.0f * static_cast<float>(i) / static_cast<float>(sr));

    juce::MidiBuffer midi;
    float maxDifference = 0.0f;
    float maxCorrection = 0.0f;
    for (size_t pos = 0; pos < tone.size(); pos += (size_t) blockSize)
    {
        juce::AudioBuffer<float> bufA(1, blockSize), bufB(1, blockSize);
        for (int i = 0; i < blockSize; ++i)
        {
            bufA.setSample(0, i, tone[pos + (size_t) i]);
            bufB.setSample(0, i, tone[pos + (size_t) i]);
        }
        a->processBlock(bufA, midi);
        b->processBlock(bufB, midi); // stopped.sample stays 0: the host does not advance
        for (int i = 0; i < blockSize; ++i)
        {
            maxDifference = std::max(maxDifference, std::abs(bufA.getSample(0, i) - bufB.getSample(0, i)));
            maxCorrection = std::max(maxCorrection, std::abs(bufA.getSample(0, i) - tone[pos + (size_t) i]));
        }
    }
    CHECK(maxCorrection > 0.05f); // the correction really was running in the free-running reference
    CHECK(maxDifference == 0.0f);
}

TEST_CASE("A playing transport still phase-locks, and a seek re-anchors at once")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    constexpr double sr = 48000.0;
    constexpr int blockSize = 512;

    // Same fixture, two hosts: one plays on from 0, one jumps to a later position mid-stream.
    // After the jump, the jumping host must equal a host that was simply there (timeline-locked).
    TestPlayHead reference;
    TestPlayHead jumping;
    auto ref = makeEngagedProcessor(sr, blockSize, 100.0f, 0.0f, &reference);
    auto jmp = makeEngagedProcessor(sr, blockSize, 100.0f, 0.0f, &jumping);

    constexpr int64_t jumpTo = 7 * 4800 + 131;
    std::vector<float> tone(static_cast<size_t>(blockSize) * 16);
    for (size_t i = 0; i < tone.size(); ++i)
        tone[i] = 0.2f * std::sin(2.0f * 3.14159265f * 220.0f * static_cast<float>(i) / static_cast<float>(sr));

    juce::MidiBuffer midi;
    reference.sample = jumpTo;
    jumping.sample = 0;
    float maxDifference = 0.0f;
    for (size_t pos = 0; pos < tone.size(); pos += (size_t) blockSize)
    {
        if (pos == 4 * (size_t) blockSize)
            jumping.sample = jumpTo + (int64_t) pos; // the seek: keep the two timelines equal from here
        juce::AudioBuffer<float> r(1, blockSize), j(1, blockSize);
        for (int i = 0; i < blockSize; ++i)
        {
            r.setSample(0, i, tone[pos + (size_t) i]);
            j.setSample(0, i, tone[pos + (size_t) i]);
        }
        ref->processBlock(r, midi);
        jmp->processBlock(j, midi);
        reference.sample += blockSize;
        jumping.sample += blockSize;
        if (pos >= 6 * (size_t) blockSize) // one-pole settle after the jump
            for (int i = 0; i < blockSize; ++i)
                maxDifference = std::max(maxDifference, std::abs(r.getSample(0, i) - j.getSample(0, i)));
    }
    // The gain state of the two hosts differs only through the one-pole memory, which has
    // settled by now; the timelines match, so the outputs must too.
    CHECK(maxDifference < 0.02f);
}

TEST_CASE("The Learn parameter is not automatable; the model parameters are")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    DePumpAudioProcessor proc;

    auto* learn = proc.apvts.getParameter(ParamID::learn);
    REQUIRE(learn != nullptr);
    CHECK_FALSE(learn->isAutomatable());
    CHECK(learn->isMetaParameter());

    for (auto* id : {ParamID::syncMode, ParamID::rate, ParamID::freeRate, ParamID::depth, ParamID::phase,
                     ParamID::attack, ParamID::release, ParamID::amount, ParamID::output, ParamID::hold})
    {
        auto* p = proc.apvts.getParameter(id);
        REQUIRE(p != nullptr);
        CHECK(p->isAutomatable());
    }
}

TEST_CASE("analyzePump reports progress and abandons promptly when cancelled")
{
    constexpr double sr = 48000.0;
    const depump::PumpProfile profile{4.0f, 9.0f, 10.0f, 60.0f, 150.0f, 0.25f};
    const auto pumped = makePumped(makeCleanChord(sr, 3.0), profile, sr);

    // Uncancelled: progress never goes backwards and ends near the top.
    {
        std::vector<float> seen;
        depump::AnalysisControl control;
        control.onProgress = [&seen](float p) { seen.push_back(p); };
        const auto analysis = depump::analyzePump(pumped, sr, &control);
        CHECK_FALSE(analysis.cancelled);
        REQUIRE(analysis.pumpDetected);
        REQUIRE(seen.size() > 5);
        CHECK(std::is_sorted(seen.begin(), seen.end()));
        CHECK(seen.back() >= 0.9f);
    }

    // Cancelled after a handful of polls: it must stop, say so, and not run the fit to the end.
    {
        int polls = 0;
        depump::AnalysisControl control;
        control.shouldCancel = [&polls] { return ++polls > 20; };
        const auto start = std::chrono::steady_clock::now();
        const auto analysis = depump::analyzePump(pumped, sr, &control);
        const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        CHECK(analysis.cancelled);
        CHECK_FALSE(analysis.pumpDetected);
        CHECK(polls <= 22); // no more than a candidate or two of overshoot
        INFO("cancelled run took " << ms << " ms");
    }
}

TEST_CASE("Destroying the processor in the middle of an analysis returns promptly and safely")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    constexpr double sr = 48000.0;
    constexpr int blockSize = 512;

    auto proc = std::make_unique<DePumpAudioProcessor>();
    TestPlayHead playHead;
    proc->setPlayHead(&playHead);
    proc->setPlayConfigDetails(1, 1, sr, blockSize);
    proc->prepareToPlay(sr, blockSize);

    const depump::PumpProfile profile{4.0f, 9.0f, 10.0f, 60.0f, 150.0f, 0.25f};
    const auto pumped = makePumped(makeCleanChord(sr, 6.0), profile, sr);

    playHead.sample = 0;
    proc->apvts.getParameter(ParamID::learn)->setValueNotifyingHost(1.0f);
    runThroughProcessor(*proc, playHead, pumped, blockSize);

    // Wait (without dispatching messages) until the background thread is analysing.
    const auto deadline = juce::Time::getMillisecondCounter() + 10000u;
    while (proc->getLearnEngineForTest().getStatus() != PluginLearnEngine::Status::analyzing &&
           juce::Time::getMillisecondCounter() < deadline)
        juce::Thread::sleep(1);
    REQUIRE(proc->getLearnEngineForTest().getStatus() == PluginLearnEngine::Status::analyzing);
    juce::Thread::sleep(50); // let it get properly into the fit

    const auto start = std::chrono::steady_clock::now();
    proc.reset(); // ~PluginLearnEngine joins the analysis thread
    const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    INFO("destruction took " << ms << " ms");
    CHECK(ms < 500.0);

    pumpMessageLoop(200); // the queued result, if any, must be inert
    SUCCEED();
}

TEST_CASE("Learn reports capture progress and a readable result")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    constexpr double sr = 48000.0;
    constexpr int blockSize = 512;

    DePumpAudioProcessor proc;
    TestPlayHead playHead;
    proc.setPlayHead(&playHead);
    proc.setPlayConfigDetails(1, 1, sr, blockSize);
    proc.prepareToPlay(sr, blockSize);
    auto& engine = proc.getLearnEngineForTest();

    CHECK(engine.getStatus() == PluginLearnEngine::Status::idle);
    CHECK(engine.getProgress() == 0.0f);
    CHECK(engine.getCaptureSeconds() == Catch::Approx(3.0));

    const depump::PumpProfile profile{4.0f, 9.0f, 10.0f, 60.0f, 150.0f, 0.25f};
    const auto pumped = makePumped(makeCleanChord(sr, 6.0), profile, sr);

    playHead.sample = 0;
    proc.apvts.getParameter(ParamID::learn)->setValueNotifyingHost(1.0f);
    const std::vector<float> firstHalf(pumped.begin(), pumped.begin() + (long) (1.5 * sr));
    runThroughProcessor(proc, playHead, firstHalf, blockSize);
    CHECK(engine.getStatus() == PluginLearnEngine::Status::capturing);
    CHECK(engine.getProgress() == Catch::Approx(0.5f).margin(0.02f));

    const std::vector<float> rest(pumped.begin() + (long) (1.5 * sr), pumped.end());
    auto status = feedWhileLearning(proc, playHead, rest, blockSize);
    if (status != PluginLearnEngine::Status::applied)
        status = waitForTerminalStatus(proc, 30000);
    REQUIRE(status == PluginLearnEngine::Status::applied);
    CHECK(engine.getProgress() == 1.0f);
    const auto message = engine.getStatusMessage();
    INFO("message: " << message);
    CHECK(message.startsWith("learned:"));
    CHECK(message.contains("Hz"));
    // Whole milliseconds, not "141.779" (juce::String(x, 0) does not round).
    CHECK(message.fromFirstOccurrenceOf("release ", false, false).upToFirstOccurrenceOf(" ms", false, false)
              .containsOnly("0123456789"));
}

// ---------------------------------------------------------------------------------------------
// Cancelling a Learn.
// ---------------------------------------------------------------------------------------------
TEST_CASE("Cancelling a Learn while it is capturing applies nothing, and Learn works again afterwards")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    constexpr double sr = 48000.0;
    constexpr int blockSize = 512;

    DePumpAudioProcessor proc;
    TestPlayHead playHead;
    proc.setPlayHead(&playHead);
    proc.setPlayConfigDetails(1, 1, sr, blockSize);
    proc.prepareToPlay(sr, blockSize);
    auto& engine = proc.getLearnEngineForTest();

    CHECK_FALSE(proc.cancelLearn()); // nothing to cancel while idle
    CHECK(engine.getStatus() == PluginLearnEngine::Status::idle);

    const depump::PumpProfile profile{4.0f, 9.0f, 10.0f, 60.0f, 150.0f, 0.25f};
    const auto pumped = makePumped(makeCleanChord(sr, 6.0), profile, sr);
    const auto half = static_cast<long>(1.5 * sr);

    playHead.sample = 0;
    proc.apvts.getParameter(ParamID::learn)->setValueNotifyingHost(1.0f);
    runThroughProcessor(proc, playHead, std::vector<float>(pumped.begin(), pumped.begin() + half), blockSize);
    REQUIRE(engine.getStatus() == PluginLearnEngine::Status::capturing);

    REQUIRE(proc.cancelLearn());
    CHECK(engine.getStatus() == PluginLearnEngine::Status::cancelled);
    CHECK(engine.getStatusMessage() == "cancelled");
    CHECK(engine.getProgress() == 0.0f);
    CHECK(proc.apvts.getRawParameterValue(ParamID::learn)->load() < 0.5f);
    CHECK_FALSE(proc.cancelLearn()); // already cancelled

    // The rest of the audio goes through: no capture completes, nothing is analysed or applied.
    runThroughProcessor(proc, playHead, std::vector<float>(pumped.begin() + half, pumped.end()), blockSize);
    pumpMessageLoop(400);
    CHECK(engine.getStatus() == PluginLearnEngine::Status::cancelled);
    CHECK_FALSE(proc.isEngagedForTest());

    // A fresh Learn is accepted and completes (a block ran with Learn at 0, so this is a new edge).
    playHead.sample = 0;
    proc.apvts.getParameter(ParamID::learn)->setValueNotifyingHost(1.0f);
    auto status = feedWhileLearning(proc, playHead, pumped, blockSize);
    if (status != PluginLearnEngine::Status::applied)
        status = waitForTerminalStatus(proc, 30000);
    CHECK(status == PluginLearnEngine::Status::applied);
}

TEST_CASE("Cancelling a Learn while it is analysing stops the analysis and nothing is applied")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    constexpr double sr = 48000.0;
    constexpr int blockSize = 512;

    DePumpAudioProcessor proc;
    TestPlayHead playHead;
    proc.setPlayHead(&playHead);
    proc.setPlayConfigDetails(1, 1, sr, blockSize);
    proc.prepareToPlay(sr, blockSize);
    auto& engine = proc.getLearnEngineForTest();

    const depump::PumpProfile profile{4.0f, 9.0f, 10.0f, 60.0f, 150.0f, 0.25f};
    const auto pumped = makePumped(makeCleanChord(sr, 6.0), profile, sr);

    std::array<float, 6> before{};
    const std::array<const char*, 6> ids{ParamID::freeRate, ParamID::depth, ParamID::phase,
                                          ParamID::attack,  ParamID::hold,  ParamID::release};
    for (size_t i = 0; i < ids.size(); ++i)
        before[i] = proc.apvts.getRawParameterValue(ids[i])->load();

    playHead.sample = 0;
    proc.apvts.getParameter(ParamID::learn)->setValueNotifyingHost(1.0f);
    runThroughProcessor(proc, playHead, pumped, blockSize);

    const auto deadline = juce::Time::getMillisecondCounter() + 10000u;
    while (engine.getStatus() != PluginLearnEngine::Status::analyzing && juce::Time::getMillisecondCounter() < deadline)
        juce::Thread::sleep(1);
    REQUIRE(engine.getStatus() == PluginLearnEngine::Status::analyzing);
    juce::Thread::sleep(30);

    const auto start = std::chrono::steady_clock::now();
    REQUIRE(proc.cancelLearn());
    const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    INFO("cancel() took " << ms << " ms");
    CHECK(ms < 100.0); // it signals the analysis thread, it does not wait for it
    CHECK(engine.getStatus() == PluginLearnEngine::Status::cancelled);

    // Give a non-cancelled analysis ample time to finish and apply: it must not.
    pumpMessageLoop(2500);
    CHECK(engine.getStatus() == PluginLearnEngine::Status::cancelled);
    processSilenceBlock(proc, blockSize);
    CHECK_FALSE(proc.isEngagedForTest());
    for (size_t i = 0; i < ids.size(); ++i)
        CHECK(proc.apvts.getRawParameterValue(ids[i])->load() == Catch::Approx(before[i]));
    CHECK(proc.apvts.getRawParameterValue(ParamID::learn)->load() < 0.5f);

    // The analysis thread is free again: a new Learn runs to completion.
    playHead.sample = 0;
    proc.apvts.getParameter(ParamID::learn)->setValueNotifyingHost(1.0f);
    auto status = feedWhileLearning(proc, playHead, pumped, blockSize);
    if (status != PluginLearnEngine::Status::applied)
        status = waitForTerminalStatus(proc, 30000);
    CHECK(status == PluginLearnEngine::Status::applied);
}

TEST_CASE("The editor size is saved in the plugin state (not as a parameter)")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    DePumpAudioProcessor first;
    const auto parameterCount = first.getParameters().size();
    CHECK(first.getEditorWidth() == 0); // nothing saved yet

    juce::MemoryBlock state;
    first.setEditorSize(800, 425);
    first.getStateInformation(state);
    REQUIRE(state.getSize() > 0);

    // The size travels as plain tree properties, next to "engaged".
    auto xml = juce::AudioProcessor::getXmlFromBinary(state.getData(), (int) state.getSize());
    REQUIRE(xml != nullptr);
    CHECK(xml->getIntAttribute("editor_width") == 800);
    CHECK(xml->getIntAttribute("editor_height") == 425);

    DePumpAudioProcessor second;
    CHECK(second.getParameters().size() == parameterCount);
    CHECK(second.apvts.getParameter("editor_width") == nullptr);
    second.setStateInformation(state.getData(), (int) state.getSize());
    CHECK(second.getEditorWidth() == 800);
    CHECK(second.getEditorHeight() == 425);

    // A session saved before this existed (no properties) restores as "unset", not as 0 x 0 windows.
    DePumpAudioProcessor third;
    third.setEditorSize(900, 478);
    juce::MemoryBlock oldState;
    DePumpAudioProcessor legacy;
    legacy.getStateInformation(oldState);
    if (auto legacyXml = juce::AudioProcessor::getXmlFromBinary(oldState.getData(), (int) oldState.getSize()))
    {
        legacyXml->removeAttribute("editor_width");
        legacyXml->removeAttribute("editor_height");
        juce::MemoryBlock stripped;
        juce::AudioProcessor::copyXmlToBinary(*legacyXml, stripped);
        third.setStateInformation(stripped.getData(), (int) stripped.getSize());
    }
    CHECK(third.getEditorWidth() == 0);
}
