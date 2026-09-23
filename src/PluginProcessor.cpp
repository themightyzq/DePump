#include "PluginProcessor.h"

#include <algorithm>
#include <array>
#include <cmath>

#include <juce_audio_utils/juce_audio_utils.h>

// --- Design note: "engaged" (do-nothing-by-default) ------------------------
// tests/DePumpTests.cpp requires bit-identical pass-through at construction,
// even though the default `depth` (6 dB) is a value that WOULD apply a real
// correction if the DSP ran unconditionally. So the plugin tracks a separate,
// non-parameter `engaged` flag: while false, processBlock does not touch the
// buffer at all (still capturing for Learn, just not correcting). It becomes
// true the moment either (a) a Learn completes successfully, or (b) the user
// (or the host) changes any of the six model parameters (freeRate, depth,
// phase, attack, hold, release) away from whatever they were a moment ago.
// Both cases are detected by the SAME mechanism: processBlock polls those six
// raw parameter values every block and compares them against a "last seen"
// snapshot; on Learn, the engine writes new values into those very
// parameters, so (a) falls out of (b) for free. This is done by polling
// (audio-thread-only comparisons + atomics) rather than an
// AudioProcessorValueTreeState::Listener, because a listener callback can be
// invoked from the audio thread when a host automates one of these ordinary
// continuous parameters, and the real-time rules forbid the allocation a
// ValueTree write would need there. `engaged` is persisted as an APVTS state
// tree property (not a parameter) so a saved, engaged session reopens
// engaged; see getStateInformation/setStateInformation.
namespace
{
constexpr double smoothingRampSeconds = 0.020;

// Sync-mode note divisions, in quarter-note beats, matching ParamID::rate's
// choice list order in createParameterLayout(). Dotted = x1.5, triplet = x2/3.
constexpr std::array<double, 13> rateDivisionBeats{
    4.0,            // 1/1
    2.0,            // 1/2
    2.0 * 1.5,      // 1/2.
    2.0 * 2.0 / 3.0,// 1/2T
    1.0,            // 1/4
    1.0 * 1.5,      // 1/4.
    1.0 * 2.0 / 3.0,// 1/4T
    0.5,            // 1/8
    0.5 * 1.5,      // 1/8.
    0.5 * 2.0 / 3.0,// 1/8T
    0.25,           // 1/16
    0.25 * 1.5,     // 1/16.
    0.25 * 2.0 / 3.0// 1/16T
};
} // namespace

DePumpAudioProcessor::DePumpAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout()),
      learnEngine(apvts, 3.0)
{
}

DePumpAudioProcessor::~DePumpAudioProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout DePumpAudioProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add(std::make_unique<AudioParameterChoice>(
        ParameterID{ParamID::syncMode, 1}, "Mode",
        StringArray{"Sync", "Free"}, 0));
    layout.add(std::make_unique<AudioParameterChoice>(
        ParameterID{ParamID::rate, 1}, "Rate",
        StringArray{"1/1", "1/2", "1/2.", "1/2T", "1/4", "1/4.", "1/4T",
                    "1/8", "1/8.", "1/8T", "1/16", "1/16.", "1/16T"},
        4));
    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID{ParamID::freeRate, 1}, "Free Rate",
        NormalisableRange<float>(0.25f, 8.0f, 0.01f, 0.5f), 2.0f,
        AudioParameterFloatAttributes().withLabel("Hz")));
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
        ParameterID{ParamID::amount, 1}, "Amount",
        NormalisableRange<float>(0.0f, 100.0f, 0.1f), 100.0f,
        AudioParameterFloatAttributes().withLabel("%")));
    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID{ParamID::output, 1}, "Output",
        NormalisableRange<float>(-24.0f, 12.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel("dB")));
    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID{ParamID::hold, 1}, "Hold",
        NormalisableRange<float>(0.0f, 400.0f, 0.1f), 60.0f,
        AudioParameterFloatAttributes().withLabel("ms")));
    layout.add(std::make_unique<AudioParameterBool>(
        ParameterID{ParamID::learn, 1}, "Learn", false,
        AudioParameterBoolAttributes().withMeta(true)));

    return layout;
}

float DePumpAudioProcessor::computeRateHz(double bpm, float currentFreeRateHz) const noexcept
{
    const int syncModeIndex = static_cast<int>(apvts.getRawParameterValue(ParamID::syncMode)->load());
    if (syncModeIndex == 0) // Sync
    {
        const int rateIndex = static_cast<int>(apvts.getRawParameterValue(ParamID::rate)->load());
        const size_t index = static_cast<size_t>(std::clamp(rateIndex, 0, (int) rateDivisionBeats.size() - 1));
        const double beats = rateDivisionBeats[index];
        const double cycleSeconds = beats * (60.0 / std::max(1.0, bpm));
        return cycleSeconds > 0.0 ? static_cast<float>(1.0 / cycleSeconds) : 2.0f;
    }
    return currentFreeRateHz;
}

float DePumpAudioProcessor::softClip(float x) noexcept
{
    // tanh soft clip asymptotic to -0.3 dBFS (10^(-0.3/20) ~= 0.96605):
    // allocation-free, bounded cost, leaves signals well under the ceiling
    // untouched to numeric precision (tanh(z) ~= z for small z).
    constexpr float ceiling = 0.96605f;
    return ceiling * std::tanh(x / ceiling);
}

void DePumpAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(samplesPerBlock);

    const bool sampleRateChanged = !(sampleRate == preparedSampleRate);
    preparedSampleRate = sampleRate;

    internalSampleCounter = 0;
    oscillator.reset(sampleRate);

    amountSmoothed.reset(sampleRate, smoothingRampSeconds);
    outputSmoothed.reset(sampleRate, smoothingRampSeconds);
    depthSmoothed.reset(sampleRate, smoothingRampSeconds);
    attackSmoothed.reset(sampleRate, smoothingRampSeconds);
    holdSmoothed.reset(sampleRate, smoothingRampSeconds);
    releaseSmoothed.reset(sampleRate, smoothingRampSeconds);
    freeRateSmoothed.reset(sampleRate, smoothingRampSeconds);
    phaseSmoothed.reset(sampleRate, smoothingRampSeconds);

    amountSmoothed.setCurrentAndTargetValue(apvts.getRawParameterValue(ParamID::amount)->load());
    outputSmoothed.setCurrentAndTargetValue(apvts.getRawParameterValue(ParamID::output)->load());
    depthSmoothed.setCurrentAndTargetValue(apvts.getRawParameterValue(ParamID::depth)->load());
    attackSmoothed.setCurrentAndTargetValue(apvts.getRawParameterValue(ParamID::attack)->load());
    holdSmoothed.setCurrentAndTargetValue(apvts.getRawParameterValue(ParamID::hold)->load());
    releaseSmoothed.setCurrentAndTargetValue(apvts.getRawParameterValue(ParamID::release)->load());
    freeRateSmoothed.setCurrentAndTargetValue(apvts.getRawParameterValue(ParamID::freeRate)->load());
    phaseSmoothed.setCurrentAndTargetValue(apvts.getRawParameterValue(ParamID::phase)->load());

    if (sampleRateChanged)
        learnEngine.prepare(sampleRate); // aborts any in-progress capture/analysis to idle

    setLatencySamples(0);
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

    const int numSamples = buffer.getNumSamples();
    const int numInputChannels = getTotalNumInputChannels();
    const int numOutputChannels = getTotalNumOutputChannels();

    for (int ch = numInputChannels; ch < numOutputChannels; ++ch)
        buffer.clear(ch, 0, numSamples);

    // --- Host position: phase reference for the oscillator, BPM for Sync
    // mode, and a timeline sample used both as the Learn arm anchor and as
    // this block's "current position" for engine bookkeeping.
    int64_t timelineSample = internalSampleCounter;
    bool havePosition = false;
    double bpm = 120.0;
    if (auto* playHead = getPlayHead())
    {
        if (auto position = playHead->getPosition())
        {
            if (auto samplesOpt = position->getTimeInSamples())
            {
                timelineSample = *samplesOpt;
                havePosition = true;
            }
            if (auto bpmOpt = position->getBpm())
                if (*bpmOpt > 0.0)
                    bpm = *bpmOpt;
        }
    }
    internalSampleCounter = havePosition ? timelineSample + numSamples : internalSampleCounter + numSamples;
    oscillator.setPhaseReferenceSample(timelineSample - captureStartSample.load());

    // --- Learn: edge-detect 0 -> 1 and arm the engine. Real-time safe:
    // armFromMessageThread() only does a handful of atomic stores plus an
    // AbstractFifo::reset() (see its header doc) — no allocation, no lock —
    // and it arms SYNCHRONOUSLY, so captureStartSample recorded right here
    // exactly matches the timeline position of the first sample the engine
    // will actually capture (this same block's samples, pushed below).
    const float learnRaw = apvts.getRawParameterValue(ParamID::learn)->load();
    const bool learnOn = learnRaw >= 0.5f;
    if (learnOn && !lastLearnOn)
    {
        captureStartSample.store(timelineSample);
        learnEngine.armFromMessageThread(timelineSample);
    }
    lastLearnOn = learnOn;

    // --- "engaged" detection: see the design note at the top of this file.
    if (rebaselineModelParams.exchange(false))
    {
        lastSeenFreeRate = apvts.getRawParameterValue(ParamID::freeRate)->load();
        lastSeenDepth = apvts.getRawParameterValue(ParamID::depth)->load();
        lastSeenPhase = apvts.getRawParameterValue(ParamID::phase)->load();
        lastSeenAttack = apvts.getRawParameterValue(ParamID::attack)->load();
        lastSeenHold = apvts.getRawParameterValue(ParamID::hold)->load();
        lastSeenRelease = apvts.getRawParameterValue(ParamID::release)->load();
    }
    else if (!engaged.load())
    {
        const float curFreeRate = apvts.getRawParameterValue(ParamID::freeRate)->load();
        const float curDepth = apvts.getRawParameterValue(ParamID::depth)->load();
        const float curPhase = apvts.getRawParameterValue(ParamID::phase)->load();
        const float curAttack = apvts.getRawParameterValue(ParamID::attack)->load();
        const float curHold = apvts.getRawParameterValue(ParamID::hold)->load();
        const float curRelease = apvts.getRawParameterValue(ParamID::release)->load();

        if (curFreeRate != lastSeenFreeRate || curDepth != lastSeenDepth || curPhase != lastSeenPhase ||
            curAttack != lastSeenAttack || curHold != lastSeenHold || curRelease != lastSeenRelease)
        {
            engaged.store(true);
        }
        lastSeenFreeRate = curFreeRate;
        lastSeenDepth = curDepth;
        lastSeenPhase = curPhase;
        lastSeenAttack = curAttack;
        lastSeenHold = curHold;
        lastSeenRelease = curRelease;
    }

    const bool isEngaged = engaged.load();

    amountSmoothed.setTargetValue(apvts.getRawParameterValue(ParamID::amount)->load());
    outputSmoothed.setTargetValue(apvts.getRawParameterValue(ParamID::output)->load());

    if (isEngaged)
    {
        depthSmoothed.setTargetValue(apvts.getRawParameterValue(ParamID::depth)->load());
        attackSmoothed.setTargetValue(apvts.getRawParameterValue(ParamID::attack)->load());
        holdSmoothed.setTargetValue(apvts.getRawParameterValue(ParamID::hold)->load());
        releaseSmoothed.setTargetValue(apvts.getRawParameterValue(ParamID::release)->load());
        freeRateSmoothed.setTargetValue(apvts.getRawParameterValue(ParamID::freeRate)->load());
        phaseSmoothed.setTargetValue(apvts.getRawParameterValue(ParamID::phase)->load());

        depump::PumpProfile profile;
        profile.depthDb = depthSmoothed.skip(numSamples);
        profile.attackMs = attackSmoothed.skip(numSamples);
        profile.holdMs = holdSmoothed.skip(numSamples);
        profile.releaseMs = releaseSmoothed.skip(numSamples);
        profile.phase01 = phaseSmoothed.skip(numSamples) * 0.01f + 0.5f;
        const float freeRateValue = freeRateSmoothed.skip(numSamples);
        profile.rateHz = std::clamp(computeRateHz(bpm, freeRateValue), 0.01f, 20.0f);

        oscillator.setProfile(profile);
    }

    for (int i = 0; i < numSamples; ++i)
    {
        float monoSample = 0.0f;
        for (int ch = 0; ch < numInputChannels; ++ch)
            monoSample += buffer.getSample(ch, i);
        if (numInputChannels > 0)
            monoSample /= static_cast<float>(numInputChannels);
        learnEngine.pushMonoSample(monoSample);

        if (isEngaged)
        {
            const float gain = oscillator.nextGain();
            const float amt = amountSmoothed.getNextValue() * 0.01f;
            const float outLin = juce::Decibels::decibelsToGain(outputSmoothed.getNextValue());

            for (int ch = 0; ch < numOutputChannels; ++ch)
            {
                const float in = ch < numInputChannels ? buffer.getSample(ch, i) : 0.0f;
                const float corrected = depump::invertSample(in, gain, amt) * outLin;
                buffer.setSample(ch, i, softClip(corrected));
            }
        }
        else
        {
            // Not engaged: the pump model is off, but the Output trim must still work or the
            // knob is dead until the first Learn. At its 0 dB default this multiplies by
            // exactly 1.0f, so the default state stays bit-identical pass-through.
            amountSmoothed.skip(1);
            const float outLin = juce::Decibels::decibelsToGain(outputSmoothed.getNextValue());
            if (outLin != 1.0f)
                for (int ch = 0; ch < numOutputChannels; ++ch)
                    buffer.setSample(ch, i, buffer.getSample(ch, i) * outLin);
        }
    }
}

void DePumpAudioProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused(midi);
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear(ch, 0, buffer.getNumSamples());
}

juce::AudioProcessorEditor* DePumpAudioProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor(*this);
}

void DePumpAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    apvts.state.setProperty("engaged", engaged.load(), nullptr);
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void DePumpAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
    {
        if (xml->hasTagName(apvts.state.getType()))
        {
            apvts.replaceState(juce::ValueTree::fromXml(*xml));

            engaged.store(static_cast<bool>(apvts.state.getProperty("engaged", false)));
            rebaselineModelParams.store(true); // don't treat the just-loaded values as a live edit

            if (auto* learnParam = apvts.getParameter(ParamID::learn))
                learnParam->setValueNotifyingHost(0.0f);
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DePumpAudioProcessor();
}
