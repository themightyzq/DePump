// depump_render — headless driver of the DePump engine pipeline.
// Thin shell: all math lives in src/dsp/ (pure, unit-tested).

#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <juce_audio_formats/juce_audio_formats.h>

#include "dsp/Envelope.h"
#include "dsp/GainCurve.h"
#include "dsp/PumpAnalysis.h"
#include "dsp/PumpProfile.h"

namespace
{

struct Args
{
    std::map<std::string, std::string> named;
    bool has(const std::string& key) const { return named.count(key) > 0; }
    std::string get(const std::string& key, const std::string& fallback = {}) const
    {
        auto it = named.find(key);
        return it == named.end() ? fallback : it->second;
    }
    double getDouble(const std::string& key, double fallback) const
    {
        return has(key) ? std::stod(named.at(key)) : fallback;
    }
};

Args parseArgs(int argc, char* argv[])
{
    Args args;
    for (int i = 1; i < argc; ++i)
    {
        std::string token = argv[i];
        if (token.rfind("--", 0) != 0)
            throw std::runtime_error("unexpected argument: " + token);
        std::string key = token.substr(2);
        if (i + 1 < argc && std::string(argv[i + 1]).rfind("--", 0) != 0)
            args.named[key] = argv[++i];
        else
            args.named[key] = "1"; // boolean flag
    }
    return args;
}

depump::PumpProfile profileFromArgs(const Args& args)
{
    depump::PumpProfile profile;
    profile.rateHz = static_cast<float>(args.getDouble("rate", profile.rateHz));
    profile.depthDb = static_cast<float>(args.getDouble("depth", profile.depthDb));
    profile.attackMs = static_cast<float>(args.getDouble("attack", profile.attackMs));
    profile.holdMs = static_cast<float>(args.getDouble("hold", profile.holdMs));
    profile.releaseMs = static_cast<float>(args.getDouble("release", profile.releaseMs));
    profile.phase01 = static_cast<float>(args.getDouble("phase", profile.phase01));
    return profile;
}

struct AudioFile
{
    std::vector<std::vector<float>> channels;
    double sampleRate = 0.0;
    size_t numSamples() const { return channels.empty() ? 0 : channels.front().size(); }
};

AudioFile readWav(const juce::File& file)
{
    juce::AudioFormatManager manager;
    manager.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(manager.createReaderFor(file));
    if (reader == nullptr)
        throw std::runtime_error("cannot read audio file: " + file.getFullPathName().toStdString());

    AudioFile audio;
    audio.sampleRate = reader->sampleRate;
    const auto numChannels = static_cast<int>(reader->numChannels);
    const auto numSamples = static_cast<int>(reader->lengthInSamples);

    juce::AudioBuffer<float> buffer(numChannels, numSamples);
    reader->read(&buffer, 0, numSamples, 0, true, true);

    audio.channels.resize(static_cast<size_t>(numChannels));
    for (int ch = 0; ch < numChannels; ++ch)
        audio.channels[static_cast<size_t>(ch)].assign(buffer.getReadPointer(ch),
                                                       buffer.getReadPointer(ch) + numSamples);
    return audio;
}

void writeWav(const juce::File& file, const AudioFile& audio)
{
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    if (stream == nullptr)
        throw std::runtime_error("cannot open for writing: " + file.getFullPathName().toStdString());

    juce::WavAudioFormat format;
    auto writer = format.createWriterFor(stream, juce::AudioFormatWriterOptions{}
                                                     .withSampleRate(audio.sampleRate)
                                                     .withNumChannels(static_cast<int>(audio.channels.size()))
                                                     .withBitsPerSample(32)
                                                     .withSampleFormat(
                                                         juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
    if (writer == nullptr)
        throw std::runtime_error("cannot create WAV writer (32-bit float)");

    juce::AudioBuffer<float> buffer(static_cast<int>(audio.channels.size()),
                                    static_cast<int>(audio.numSamples()));
    for (size_t ch = 0; ch < audio.channels.size(); ++ch)
        buffer.copyFrom(static_cast<int>(ch), 0, audio.channels[ch].data(),
                        static_cast<int>(audio.channels[ch].size()));

    if (!writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples()))
        throw std::runtime_error("WAV write failed");
}

std::vector<float> monoMix(const AudioFile& audio)
{
    std::vector<float> mono(audio.numSamples(), 0.0f);
    for (const auto& channel : audio.channels)
        for (size_t i = 0; i < channel.size(); ++i)
            mono[i] += channel[i] / static_cast<float>(audio.channels.size());
    return mono;
}

// Constant-amplitude pad-like chord (A3, C#4, E4) — flat envelope by
// construction, which makes fixture assertions and eyeballing easy.
std::vector<float> makeCleanSignal(double sampleRate, double seconds)
{
    const auto numSamples = static_cast<size_t>(sampleRate * seconds);
    std::vector<float> samples(numSamples);
    constexpr double freqs[] = {220.0, 277.18, 329.63};
    for (size_t n = 0; n < numSamples; ++n)
    {
        double value = 0.0;
        for (double freq : freqs)
            value += 0.2 * std::sin(2.0 * juce::MathConstants<double>::pi * freq * static_cast<double>(n) / sampleRate);
        samples[n] = static_cast<float>(value);
    }
    return samples;
}

int runMakeFixture(const Args& args)
{
    const juce::File outDir(args.get("out-dir"));
    if (args.get("out-dir").empty())
        throw std::runtime_error("--make-fixture requires --out-dir");
    outDir.createDirectory();

    const double sampleRate = args.getDouble("sr", 48000.0);
    const double seconds = args.getDouble("seconds", 8.0);
    const auto profile = profileFromArgs(args);

    AudioFile clean;
    clean.sampleRate = sampleRate;
    clean.channels.push_back(makeCleanSignal(sampleRate, seconds));

    AudioFile pumped = clean;
    const auto gain = depump::synthesizeGainCurve(profile, sampleRate, pumped.numSamples());
    depump::applyGain(pumped.channels[0], gain);

    writeWav(outDir.getChildFile("clean.wav"), clean);
    writeWav(outDir.getChildFile("pumped.wav"), pumped);
    std::cout << "wrote " << outDir.getChildFile("clean.wav").getFullPathName()
              << " and pumped.wav (rate=" << profile.rateHz << "Hz depth=" << profile.depthDb
              << "dB attack=" << profile.attackMs << "ms hold=" << profile.holdMs
              << "ms release=" << profile.releaseMs
              << "ms phase=" << profile.phase01 << ")\n";
    return 0;
}

int runCompare(const Args& args)
{
    std::ifstream fileA(args.get("compare")), fileB(args.get("with"));
    if (!fileA || !fileB)
        throw std::runtime_error("--compare A --with B: cannot open input CSVs");

    std::stringstream bufA, bufB;
    bufA << fileA.rdbuf();
    bufB << fileB.rdbuf();

    const auto deviation =
        depump::compareEnvelopes(depump::envelopeFromCsv(bufA.str()), depump::envelopeFromCsv(bufB.str()));

    std::cout << "compared " << deviation.pointsCompared << " points: max " << deviation.maxAbsDb
              << " dB, rms " << deviation.rmsDb << " dB\n";

    if (args.has("tolerance") && deviation.maxAbsDb > args.getDouble("tolerance", 0.0))
    {
        std::cout << "FAIL: exceeds tolerance " << args.get("tolerance") << " dB\n";
        return 1;
    }
    return 0;
}

int runRender(const Args& args)
{
    auto audio = readWav(juce::File(args.get("in")));

    if (args.has("auto"))
    {
        const auto analysis = depump::analyzePump(monoMix(audio), audio.sampleRate);
        std::cout << "auto-analysis: " << (analysis.pumpDetected ? "PUMP DETECTED" : "no pump detected")
                  << " (confidence " << analysis.confidence << ")\n";
        if (analysis.pumpDetected)
        {
            std::cout << "  period " << analysis.periodSeconds << " s (" << 1.0 / analysis.periodSeconds
                      << " Hz), depth " << analysis.depthDb << " dB, dip at " << analysis.dipTimeSeconds
                      << " s into the cycle\n";
            if (analysis.modelFitted)
                std::cout << "  model fit: depth " << analysis.fittedProfile.depthDb << " dB, attack "
                          << analysis.fittedProfile.attackMs << " ms, hold " << analysis.fittedProfile.holdMs
                          << " ms, release " << analysis.fittedProfile.releaseMs << " ms, phase "
                          << analysis.fittedProfile.phase01 << "\n";
            else
                std::cout << "  model fit: rejected — using measured template\n";
            const auto gain = depump::gainCurveFromAnalysis(analysis, audio.sampleRate, audio.numSamples());
            const auto amount = static_cast<float>(args.getDouble("amount", 1.0));
            for (auto& channel : audio.channels)
                depump::applyInverseGain(channel, gain, amount);
        }
    }
    else if (args.has("apply") || args.has("invert"))
    {
        const auto profile = profileFromArgs(args);
        const auto gain = depump::synthesizeGainCurve(profile, audio.sampleRate, audio.numSamples());
        const auto amount = static_cast<float>(args.getDouble("amount", 1.0));
        for (auto& channel : audio.channels)
        {
            if (args.has("invert"))
                depump::applyInverseGain(channel, gain, amount);
            else
                depump::applyGain(channel, gain);
        }
    }

    if (args.has("out"))
        writeWav(juce::File(args.get("out")), audio);

    if (args.has("envelope"))
    {
        const auto envelope = depump::extractRmsEnvelopeDb(monoMix(audio), audio.sampleRate);
        std::ofstream csv(args.get("envelope"));
        if (!csv)
            throw std::runtime_error("cannot write envelope CSV: " + args.get("envelope"));
        csv << depump::envelopeToCsv(envelope);
        std::cout << "wrote " << envelope.size() << " envelope points to " << args.get("envelope") << "\n";
    }
    return 0;
}

void printUsage()
{
    std::cout << "depump_render — DePump engine pipeline, headless\n"
                 "  --in X.wav [--out Y.wav] [--envelope Z.csv]\n"
                 "      [--auto | --apply | --invert] [--amount 0..1]\n"
                 "      [--rate Hz --depth dB --attack ms --hold ms --release ms --phase 0..1]\n"
                 "  --make-fixture --out-dir DIR [--sr N] [--seconds S] [profile args]\n"
                 "  --compare A.csv --with B.csv [--tolerance dB]\n";
}

} // namespace

int main(int argc, char* argv[])
{
    try
    {
        const auto args = parseArgs(argc, argv);
        if (args.has("make-fixture"))
            return runMakeFixture(args);
        if (args.has("compare"))
            return runCompare(args);
        if (args.has("in"))
            return runRender(args);
        printUsage();
        return argc <= 1 ? 0 : 2;
    }
    catch (const std::exception& e)
    {
        std::cerr << "error: " << e.what() << "\n";
        return 2;
    }
}
