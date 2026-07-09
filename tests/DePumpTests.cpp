#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "PluginProcessor.h"

namespace
{
// JUCE needs its runtime (MessageManager etc.) initialised in a console app.
struct JuceEnv
{
    juce::ScopedJuceInitialiser_GUI init;
};
} // namespace

TEST_CASE("Processor reports effect-plugin basics")
{
    JuceEnv env;
    DePumpAudioProcessor proc;

    CHECK(proc.getName() == juce::String("DePump"));
    CHECK_FALSE(proc.acceptsMidi());
    CHECK_FALSE(proc.producesMidi());
    CHECK_FALSE(proc.isMidiEffect());
    CHECK(proc.getTailLengthSeconds() == 0.0);
    for (auto* id : {ParamID::syncMode, ParamID::rate, ParamID::freeRate, ParamID::depth,
                     ParamID::phase, ParamID::attack, ParamID::release, ParamID::amount,
                     ParamID::output})
        CHECK(proc.apvts.getParameter(id) != nullptr);

    // Guard the REVIEW-UX decisions: no Mix param may reappear, and Free
    // mode must exist for tempo-less hosts (Soundminer).
    CHECK(proc.apvts.getParameter("mix") == nullptr);
}

TEST_CASE("Pass-through leaves audio bit-identical and finite")
{
    JuceEnv env;
    DePumpAudioProcessor proc;

    // Never assume one buffer size or sample rate.
    for (double sr : {44100.0, 48000.0, 96000.0})
    {
        for (int blockSize : {64, 512, 1024})
        {
            proc.setPlayConfigDetails(2, 2, sr, blockSize);
            proc.prepareToPlay(sr, blockSize);

            juce::AudioBuffer<float> buffer(2, blockSize);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < blockSize; ++i)
                    buffer.setSample(ch, i, std::sin(0.02f * (float) i + (float) ch));

            juce::AudioBuffer<float> original;
            original.makeCopyOf(buffer);

            juce::MidiBuffer midi;
            proc.processBlock(buffer, midi);

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < blockSize; ++i)
                {
                    const float out = buffer.getSample(ch, i);
                    REQUIRE(std::isfinite(out));
                    REQUIRE(out == original.getSample(ch, i));
                }

            proc.releaseResources();
        }
    }
}

TEST_CASE("State save/restore roundtrips parameter values")
{
    JuceEnv env;
    DePumpAudioProcessor proc;

    auto* depth = proc.apvts.getParameter(ParamID::depth);
    REQUIRE(depth != nullptr);
    depth->setValueNotifyingHost(0.75f);

    juce::MemoryBlock state;
    proc.getStateInformation(state);

    depth->setValueNotifyingHost(0.1f);
    REQUIRE(depth->getValue() == Catch::Approx(0.1f));

    proc.setStateInformation(state.getData(), (int) state.getSize());
    CHECK(depth->getValue() == Catch::Approx(0.75f));
}
