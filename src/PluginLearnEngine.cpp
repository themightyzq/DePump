#include "PluginLearnEngine.h"

#include <algorithm>
#include <cmath>

#include "PluginProcessor.h"
#include "dsp/PumpAnalysis.h"
#include "dsp/PumpProfile.h"

// One clamped, ready-to-apply profile plus a human-readable note of which
// fields the plugin's parameter ranges clamped away from the raw fit.
// Declared at file scope (not in the anonymous namespace below) so it is
// the same type as the forward declaration in PluginLearnEngine.h.
struct ClampedProfile
{
    float freeRateHz = 2.0f;
    float depthDb = 6.0f;
    float attackMs = 10.0f;
    float holdMs = 60.0f;
    float releaseMs = 150.0f;
    float phasePercent = 0.0f;
    juce::String clampNote; // empty if nothing bit
};

namespace
{
constexpr int pollIntervalMs = 20;

float clampNoting(float value, float lo, float hi, const char* name, juce::String& note)
{
    const float clamped = std::clamp(value, lo, hi);
    if (clamped != value)
    {
        if (note.isNotEmpty())
            note << ", ";
        note << name << " clamped " << juce::String(value, 2) << " -> " << juce::String(clamped, 2);
    }
    return clamped;
}

ClampedProfile clampProfileToParameterRanges(const depump::PumpProfile& profile)
{
    ClampedProfile out;
    juce::String note;
    out.freeRateHz = clampNoting(profile.rateHz, 0.25f, 8.0f, "rate", note);
    out.depthDb = clampNoting(profile.depthDb, 0.0f, 24.0f, "depth", note);
    out.attackMs = clampNoting(profile.attackMs, 1.0f, 100.0f, "attack", note);
    out.releaseMs = clampNoting(profile.releaseMs, 20.0f, 500.0f, "release", note);
    out.holdMs = clampNoting(profile.holdMs, 0.0f, 400.0f, "hold", note);
    const float phasePercentRaw = (profile.phase01 - 0.5f) * 100.0f;
    out.phasePercent = clampNoting(phasePercentRaw, -50.0f, 50.0f, "phase", note);
    out.clampNote = note;
    return out;
}

void setNormalized(juce::AudioProcessorValueTreeState& apvts, const char* paramId, float value)
{
    if (auto* p = apvts.getParameter(paramId))
        p->setValueNotifyingHost(p->convertTo0to1(value));
}
} // namespace

PluginLearnEngine::PluginLearnEngine(juce::AudioProcessorValueTreeState& apvtsIn, double captureSecondsIn)
    : juce::Thread("DePump Learn"), apvts(apvtsIn), captureSeconds(captureSecondsIn)
{
    startThread();
}

PluginLearnEngine::~PluginLearnEngine()
{
    stopThread(2000);
}

void PluginLearnEngine::prepare(double sampleRateIn)
{
    sampleRate = sampleRateIn > 0.0 ? sampleRateIn : 44100.0;
    const int target = std::max(1, static_cast<int>(std::llround(captureSeconds * sampleRate)));

    armed.store(false);
    captureComplete.store(false);
    capturedCount.store(0);
    targetSamples.store(target);
    status.store(Status::idle);
    statusMessage = "idle";

    // juce::AbstractFifo always reserves one slot (getFreeSpace() ==
    // bufferSize - numReady - 1), so a FIFO sized to exactly `target` can
    // only ever hold target-1 items — size it one larger so `target`
    // samples are actually writable.
    const int fifoCapacity = target + 1;
    fifoBuffer.assign(static_cast<size_t>(fifoCapacity), 0.0f);
    fifo = std::make_unique<juce::AbstractFifo>(fifoCapacity);
}

void PluginLearnEngine::armFromMessageThread(int64_t captureStartTimelineSample) noexcept
{
    juce::ignoreUnused(captureStartTimelineSample); // see header doc: caller keeps this, not us

    const auto current = status.load();
    if (current == Status::capturing || current == Status::analyzing)
        return; // no-op while busy
    if (fifo == nullptr)
        return; // not prepared yet

    // Synchronous: an AbstractFifo::reset() and a few atomic stores, all
    // allocation-free and lock-free, so this is safe to run right here
    // (whichever thread called us, including the audio thread) rather than
    // deferring to the background thread's next poll — see the header doc
    // for why that matters. Deliberately does NOT touch `statusMessage`
    // (a juce::String) here: assigning one can allocate, which processBlock
    // may call this from and must never do.
    fifo->reset();
    capturedCount.store(0);
    captureComplete.store(false);
    status.store(Status::capturing);
    armed.store(true);
}

void PluginLearnEngine::pushMonoSample(float sample) noexcept
{
    if (!armed.load())
        return;
    if (fifo == nullptr)
        return;

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    fifo->prepareToWrite(1, start1, size1, start2, size2);
    if (size1 > 0)
        fifoBuffer[static_cast<size_t>(start1)] = sample;
    else if (size2 > 0)
        fifoBuffer[static_cast<size_t>(start2)] = sample;
    const int written = size1 + size2;
    fifo->finishedWrite(written);

    if (written <= 0)
        return;

    const int newCount = capturedCount.load() + 1;
    capturedCount.store(newCount);
    if (newCount >= targetSamples.load())
    {
        armed.store(false); // stop writing: capture buffer is exactly full
        captureComplete.store(true);
    }
}

void PluginLearnEngine::finishCaptureAndAnalyze()
{
    if (status.load() != Status::capturing || !captureComplete.load())
        return;

    status.store(Status::analyzing);
    statusMessage = "analyzing..."; // background thread only: safe to allocate here

    const int count = std::min(capturedCount.load(), targetSamples.load());
    std::vector<float> captured(static_cast<size_t>(std::max(0, count)));

    if (fifo != nullptr && count > 0)
    {
        int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
        fifo->prepareToRead(count, start1, size1, start2, size2);
        for (int i = 0; i < size1; ++i)
            captured[static_cast<size_t>(i)] = fifoBuffer[static_cast<size_t>(start1 + i)];
        for (int i = 0; i < size2; ++i)
            captured[static_cast<size_t>(size1 + i)] = fifoBuffer[static_cast<size_t>(start2 + i)];
        fifo->finishedRead(size1 + size2);
    }

    try
    {
        const auto analysis = depump::analyzePump(captured, sampleRate);
        if (!analysis.pumpDetected || !analysis.modelFitted)
        {
            statusMessage = analysis.pumpDetected
                                ? "pump detected but no compressor-model fit; nothing applied"
                                : "no pumping detected in the captured audio";
            status.store(Status::noPumpDetected);
            juce::MessageManager::callAsync([this] { resetLearnParameterOnMessageThread(); });
            return;
        }

        const auto clamped = clampProfileToParameterRanges(analysis.fittedProfile);
        juce::MessageManager::callAsync([this, clamped] { applyProfileOnMessageThread(clamped); });
    }
    catch (const std::exception& e)
    {
        statusMessage = juce::String("error: ") + e.what();
        status.store(Status::error);
        juce::MessageManager::callAsync([this] { resetLearnParameterOnMessageThread(); });
    }
}

void PluginLearnEngine::applyProfileOnMessageThread(const ClampedProfile& clamped)
{
    setNormalized(apvts, ParamID::syncMode, 1.0f); // Free
    setNormalized(apvts, ParamID::freeRate, clamped.freeRateHz);
    setNormalized(apvts, ParamID::depth, clamped.depthDb);
    setNormalized(apvts, ParamID::attack, clamped.attackMs);
    setNormalized(apvts, ParamID::hold, clamped.holdMs);
    setNormalized(apvts, ParamID::release, clamped.releaseMs);
    setNormalized(apvts, ParamID::phase, clamped.phasePercent);

    statusMessage = clamped.clampNote.isEmpty() ? juce::String("applied")
                                                 : juce::String("applied (") + clamped.clampNote + ")";
    resetLearnParameterOnMessageThread();
    status.store(Status::applied);
}

void PluginLearnEngine::resetLearnParameterOnMessageThread()
{
    if (auto* p = apvts.getParameter(ParamID::learn))
        p->setValueNotifyingHost(0.0f);
}

void PluginLearnEngine::run()
{
    while (!threadShouldExit())
    {
        finishCaptureAndAnalyze();
        wait(pollIntervalMs);
    }
}
