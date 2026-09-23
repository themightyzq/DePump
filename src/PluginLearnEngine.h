#pragma once

#include <atomic>
#include <cstdint>
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
        error
    };

    // apvtsIn must outlive this engine (the owning AudioProcessor's apvts).
    explicit PluginLearnEngine(juce::AudioProcessorValueTreeState& apvtsIn, double captureSecondsIn = 3.0);
    ~PluginLearnEngine() override;

    // Sizes the capture ring buffer for the given sample rate and resets to
    // idle, aborting any in-progress capture/analysis. Message-thread /
    // prepareToPlay only — never call from processBlock.
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

    // --- Audio-thread API ---
    void pushMonoSample(float sample) noexcept;
    bool isCaptureComplete() const noexcept { return captureComplete.load(); }

    // --- Reporting (message thread; read status message only when
    // getStatus() is idle/applied/noPumpDetected/error, i.e. not while the
    // background thread may still be writing it) ---
    Status getStatus() const noexcept { return status.load(); }
    juce::String getStatusMessage() const { return statusMessage; }

private:
    void run() override;
    void finishCaptureAndAnalyze();
    void applyProfileOnMessageThread(const struct ClampedProfile& clamped);
    void resetLearnParameterOnMessageThread();

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
    juce::String statusMessage; // background/message thread only, see getStatusMessage()

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginLearnEngine)
};
