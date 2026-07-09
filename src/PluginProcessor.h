#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

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
} // namespace ParamID

class DePumpAudioProcessor : public juce::AudioProcessor
{
public:
    DePumpAudioProcessor();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

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

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DePumpAudioProcessor)
};
