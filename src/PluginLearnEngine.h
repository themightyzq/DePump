#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>

// Owns the plugin's "Learn" workflow: capture a few seconds of the incoming
// mono mix into a preallocated ring buffer, then off the audio thread, run
// the offline pump analyzer on it and publish the fitted profile onto the
// ordinary APVTS parameters.
//
// Threading contract:
//  - prepare() and the constructor/destructor run on a non-realtime thread
//    (message thread / prepareToPlay), never concurrently with processBlock.
//  - pushMonoSample() and armFromMessageThread() are real-time safe: no
//    allocation, no locks, bounded cost. They may be called from the audio
//    thread. armFromMessageThread() resets the FIFO and flips the capture
//    state SYNCHRONOUSLY (an AbstractFifo::reset() and a handful of atomic
//    stores — no allocation, no locking, so this is safe even though the
//    name describes the logical caller — a parameter edit reaching the
//    message thread — rather than a hard requirement that the call
//    originate there). This matters: the caller anchors its own phase
//    reference to the same instant it calls armFromMessageThread, so
//    arming must take effect immediately, not on the next ~20 ms poll —
//    an earlier version deferred it, which left capture starting at an
//    unpredictable, scheduling-dependent sample offset from that instant.
//  - Everything else (analysis, applying parameters) runs on the background
//    juce::Thread and hands results to the message thread via
//    juce::MessageManager::callAsync.
class PluginLearnEngine : private juce::Thread
{
public:
    enum class Status
    {
        idle,
        capturing,
        analyzing,
        applied,
        noPumpDetected,
        error,
        cancelled // the user cancelled a capture or an analysis; nothing was applied
    };

    // apvtsIn must outlive this engine (the owning AudioProcessor's apvts).
    explicit PluginLearnEngine(juce::AudioProcessorValueTreeState& apvtsIn, double captureSecondsIn = 3.0);
    ~PluginLearnEngine() override;

    // Sizes the capture ring buffer for the given sample rate and resets to
    // idle, aborting any in-progress capture/analysis: a result from an
    // analysis that started before this call is discarded, never applied.
    // Message-thread / prepareToPlay only — never call from processBlock.
    void prepare(double sampleRate);

    // Starts a capture immediately (synchronously resets the FIFO and flips
    // status to capturing). captureStartTimelineSample (the host playhead
    // sample position at the moment Learn was pressed) is accepted for
    // interface symmetry with the caller's own bookkeeping but is not
    // needed by capture/analysis itself (which only cares about sample
    // COUNT, not absolute timeline position) — the caller (PluginProcessor)
    // is the one that remembers it, to re-anchor its GainOscillator's phase
    // reference on playback; because arming is synchronous, that recorded
    // instant exactly matches the first sample this call will accept.
    // Real-time safe. A no-op while already capturing or analyzing.
    void armFromMessageThread(int64_t captureStartTimelineSample) noexcept;

    // Abandons a capture or an analysis that is in progress: nothing is applied, the status becomes
    // `cancelled` and the Learn parameter goes back to 0. A no-op (returns false) in any other
    // state. Message thread only (it writes the status text and the parameter, which the audio
    // thread must never do). Cancellation rides the same generation counter as prepare() and
    // destruction: a running analysis polls it and stops within a candidate evaluation, and a
    // result already queued with callAsync goes inert. The liveness guard is unchanged.
    bool cancel();

    // --- Audio-thread API ---
    void pushMonoSample(float sample) noexcept;
    bool isCaptureComplete() const noexcept { return captureComplete.load(); }

    // --- Reporting (message thread; the editor polls these) ---
    Status getStatus() const noexcept { return status.load(); }

    // A copy of the latest status text. Safe from any non-real-time thread at any time: the
    // background thread and the message thread both write it, so it is guarded by a spin lock
    // that the audio thread never takes.
    juce::String getStatusMessage() const
    {
        const juce::SpinLock::ScopedLockType guard(messageLock);
        return statusMessage;
    }

    // 0..1: how far the current phase has got. capturing: captured / needed samples.
    // analyzing: the analyzer's coarse progress. idle: 0. A finished run (applied,
    // noPumpDetected, error): 1. Lock-free, so it is safe to poll from a timer.
    float getProgress() const noexcept
    {
        switch (status.load())
        {
            case Status::idle:
            case Status::cancelled:
                return 0.0f;
            case Status::capturing:
            {
                const int needed = targetSamples.load();
                return needed > 0 ? std::clamp(static_cast<float>(capturedCount.load()) / static_cast<float>(needed),
                                               0.0f, 1.0f)
                                  : 0.0f;
            }
            case Status::analyzing:
                return std::clamp(analysisProgress.load(), 0.0f, 1.0f);
            case Status::applied:
            case Status::noPumpDetected:
            case Status::error:
                return 1.0f;
        }
        return 0.0f;
    }

    // How much audio a Learn listens to, in seconds.
    double getCaptureSeconds() const noexcept { return captureSeconds; }

private:
    void run() override;
    void finishCaptureAndAnalyze();
    void postToMessageThread(uint32_t startedGeneration, std::function<void()> work);
    void applyProfileOnMessageThread(const struct ClampedProfile& clamped);
    void resetLearnParameterOnMessageThread();
    void setStatusMessage(const juce::String& text);
    void setStatusIfAnalyzing(Status next);

    juce::AudioProcessorValueTreeState& apvts;
    const double captureSeconds;

    double sampleRate = 44100.0;
    std::vector<float> fifoBuffer;
    std::unique_ptr<juce::AbstractFifo> fifo;

    std::atomic<bool> armed{false};           // audio thread may write while true
    std::atomic<bool> captureComplete{false}; // set by audio thread, consumed by background thread
    std::atomic<int> capturedCount{0};
    std::atomic<int> targetSamples{0};

    std::atomic<Status> status{Status::idle};
    juce::String statusMessage; // guarded by messageLock; never touched on the audio thread
    mutable juce::SpinLock messageLock;
    std::atomic<float> analysisProgress{0.0f};

    // Liveness/cancellation for callAsync results: queued lambdas may run after
    // this engine is destroyed (alive expired/false) or after prepare()
    // restarted the workflow (generation changed); both make them no-ops.
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>>(true);
    std::atomic<uint32_t> generation{0};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginLearnEngine)
};
