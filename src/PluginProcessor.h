#pragma once

#include <atomic>
#include <cstdint>

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginLearnEngine.h"
#include "dsp/GainCurve.h"

// Parameter IDs — unique and stable across releases (automation depends on it).
namespace ParamID
{
inline constexpr auto syncMode = "syncMode"; // Sync (host tempo) or Free (Hz) — Free is required for
                                             // tempo-less hosts like Soundminer (REVIEW-UX.md #1)
inline constexpr auto rate = "rate";         // pump cycle: note division incl. dotted/triplet (Sync mode)
inline constexpr auto freeRate = "freeRate"; // pump cycle in Hz (Free mode)
inline constexpr auto depth = "depth";       // max recovery boost at the bottom of the dip, dB
inline constexpr auto phase = "phase";       // offset of the inverse envelope within the cycle, %
inline constexpr auto attack = "attack";     // how fast the original dip fell, ms
inline constexpr auto release = "release";   // how fast the original dip recovered, ms
inline constexpr auto amount = "amount";     // scales the inverse envelope 0-100% (deliberately NOT a
                                             // dry/wet mix — that would blend the artifact back in)
inline constexpr auto output = "output";     // output trim, dB (recovery gain needs headroom management)
inline constexpr auto hold = "hold";         // dip hold duration, ms — matches depump::PumpProfile::holdMs
inline constexpr auto learn = "learn";       // momentary: capture ~captureSeconds of audio and fit a
                                             // profile from it. Meta parameter; the engine resets it to 0.
} // namespace ParamID

class DePumpAudioProcessor : public juce::AudioProcessor
{
public:
    DePumpAudioProcessor();
    ~DePumpAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    void processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "DePump"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // Exposed for tests only: whether the plugin currently applies any
    // correction (see the "engaged" design note in PluginProcessor.cpp).
    bool isEngagedForTest() const noexcept { return engaged.load(); }
    PluginLearnEngine& getLearnEngineForTest() noexcept { return learnEngine; }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    float computeRateHz(double bpm, float currentFreeRateHz) const noexcept;
    static float softClip(float x) noexcept;

    PluginLearnEngine learnEngine;

    // --- Real-time state (audio thread only unless noted) ---
    depump::GainOscillator oscillator;
    int64_t internalSampleCounter = 0; // fallback free-running clock when the host reports no position

    juce::SmoothedValue<float> amountSmoothed, outputSmoothed;
    juce::SmoothedValue<float> depthSmoothed, attackSmoothed, holdSmoothed, releaseSmoothed;
    juce::SmoothedValue<float> freeRateSmoothed, phaseSmoothed;

    // "engaged": the plugin applies zero DSP (bit-identical pass-through)
    // until either the first successful Learn or the first user edit of one
    // of the six model parameters (freeRate, depth, phase, attack, hold,
    // release) — see the design note in PluginProcessor.cpp for why. Purely
    // atomic so processBlock can read/update it lock-free; persisted as a
    // non-parameter APVTS state property ("engaged") so a saved session
    // that was engaged reopens engaged.
    std::atomic<bool> engaged{false};
    std::atomic<bool> rebaselineModelParams{true}; // audio thread re-samples "last seen" values when true
    float lastSeenFreeRate = 0.0f, lastSeenDepth = 0.0f, lastSeenPhase = 0.0f;
    float lastSeenAttack = 0.0f, lastSeenHold = 0.0f, lastSeenRelease = 0.0f;

    bool lastLearnOn = false; // audio-thread-only edge detector for ParamID::learn
    // The timeline sample at which the current/last capture began. Recorded
    // at the same instant armFromMessageThread() is called (which arms
    // synchronously — see PluginLearnEngine.h), so this exactly matches the
    // first sample the engine actually captures.
    std::atomic<int64_t> captureStartSample{0};

    double preparedSampleRate = -1.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DePumpAudioProcessor)
};
