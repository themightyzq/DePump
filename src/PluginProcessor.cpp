#include "PluginProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>

DePumpAudioProcessor::DePumpAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout DePumpAudioProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add(std::make_unique<AudioParameterChoice>(
        ParameterID{ParamID::rate, 1}, "Rate",
        StringArray{"1/1", "1/2", "1/4", "1/8", "1/16"}, 2));
    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID{ParamID::depth, 1}, "Depth",
        NormalisableRange<float>(0.0f, 24.0f, 0.1f), 6.0f,
        AudioParameterFloatAttributes().withLabel("dB")));
    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID{ParamID::phase, 1}, "Phase",
        NormalisableRange<float>(-50.0f, 50.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel("%")));
    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID{ParamID::attack, 1}, "Attack",
        NormalisableRange<float>(1.0f, 100.0f, 0.1f, 0.4f), 10.0f,
        AudioParameterFloatAttributes().withLabel("ms")));
    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID{ParamID::release, 1}, "Release",
        NormalisableRange<float>(20.0f, 500.0f, 0.1f, 0.4f), 150.0f,
        AudioParameterFloatAttributes().withLabel("ms")));
    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID{ParamID::mix, 1}, "Mix",
        NormalisableRange<float>(0.0f, 100.0f, 0.1f), 100.0f,
        AudioParameterFloatAttributes().withLabel("%")));

    return layout;
}

void DePumpAudioProcessor::prepareToPlay(double, int)
{
    // DSP state allocation happens here (never in processBlock).
}

bool DePumpAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& in = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    if (in != out)
        return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void DePumpAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (auto ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear(ch, 0, buffer.getNumSamples());

    // Pass-through: the inverse-envelope DSP lands with the MVP
    // (user-guided de-pump; see ROADMAP.md). Parameters above define
    // the MVP surface and are exercised by hosts/pluginval already.
}

juce::AudioProcessorEditor* DePumpAudioProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor(*this);
}

void DePumpAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void DePumpAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DePumpAudioProcessor();
}
