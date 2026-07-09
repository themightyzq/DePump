// depump_render — headless driver of the DePump engine pipeline.
// Thin shell: all math lives in src/dsp/ (pure, unit-tested); file I/O in
// src/io/ (shared with the GUI app).

#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "dsp/Envelope.h"
#include "dsp/GainCurve.h"
#include "dsp/PumpProfile.h"
#include "dsp/Recovery.h"
#include "io/AudioFileIO.h"

namespace
{

using depump::AudioFileData;

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

void reportOutcome(const depump::RecoveryOutcome& outcome, std::ostream& log)
{
    const auto& analysis = outcome.analysis;
    log << "auto-analysis: " << (analysis.pumpDetected ? "PUMP DETECTED" : "no pump detected")
        << " (confidence " << analysis.confidence << ")\n";
    if (!analysis.pumpDetected)
        return;

    log << "  period " << analysis.periodSeconds << " s (" << 1.0 / analysis.periodSeconds << " Hz), depth "
        << analysis.depthDb << " dB, dip at " << analysis.dipTimeSeconds << " s into the cycle\n";
    if (analysis.modelFitted)
        log << "  model fit: depth " << analysis.fittedProfile.depthDb << " dB, attack "
            << analysis.fittedProfile.attackMs << " ms, hold " << analysis.fittedProfile.holdMs
            << " ms, release " << analysis.fittedProfile.releaseMs << " ms, phase "
            << analysis.fittedProfile.phase01 << "\n";
    else
        log << "  model fit: rejected — using measured template\n";
    if (outcome.trimDb < 0.0f)
        log << "  output trimmed " << outcome.trimDb << " dB to keep peaks below full scale\n";
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

    AudioFileData clean;
    clean.sampleRate = sampleRate;
    clean.channels.push_back(makeCleanSignal(sampleRate, seconds));

    AudioFileData pumped = clean;
    const auto gain = depump::synthesizeGainCurve(profile, sampleRate, pumped.numSamples());
    depump::applyGain(pumped.channels[0], gain);

    depump::writeWavFile(outDir.getChildFile("clean.wav"), clean);
    depump::writeWavFile(outDir.getChildFile("pumped.wav"), pumped);
    std::cout << "wrote " << outDir.getChildFile("clean.wav").getFullPathName()
              << " and pumped.wav (rate=" << profile.rateHz << "Hz depth=" << profile.depthDb
              << "dB attack=" << profile.attackMs << "ms hold=" << profile.holdMs
              << "ms release=" << profile.releaseMs << "ms phase=" << profile.phase01 << ")\n";
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

int runBatch(const Args& args)
{
    const juce::File inDir(args.get("batch"));
    const juce::File outDir(args.get("out-dir"));
    if (!inDir.isDirectory())
        throw std::runtime_error("--batch requires an existing input directory");
    if (args.get("out-dir").empty())
        throw std::runtime_error("--batch requires --out-dir (output is never written in place)");
    if (outDir == inDir)
        throw std::runtime_error("--out-dir must differ from the input directory (non-destructive)");
    outDir.createDirectory();

    const auto amount = static_cast<float>(args.getDouble("amount", 1.0));
    auto files = inDir.findChildFiles(juce::File::findFiles, false, "*.wav;*.aif;*.aiff");
    files.sort();

    int recovered = 0, untouched = 0, failed = 0;
    for (const auto& file : files)
    {
        std::cout << "== " << file.getFileName() << "\n";
        try
        {
            auto audio = depump::readAudioFile(file);
            const auto outcome = depump::analyzeAndRecover(audio.channels, audio.sampleRate, amount);
            reportOutcome(outcome, std::cout);
            depump::writeWavFile(outDir.getChildFile(file.getFileName()), audio);
            outcome.analysis.pumpDetected ? ++recovered : ++untouched;
        }
        catch (const std::exception& e)
        {
            std::cout << "  ERROR: " << e.what() << " — skipped\n";
            ++failed;
        }
    }

    std::cout << "batch done: " << recovered << " recovered, " << untouched
              << " passed through unpumped, " << failed << " failed, into "
              << outDir.getFullPathName() << "\n";
    return failed == 0 ? 0 : 1;
}

int runRender(const Args& args)
{
    auto audio = depump::readAudioFile(juce::File(args.get("in")));

    if (args.has("auto"))
    {
        const auto outcome = depump::analyzeAndRecover(
            audio.channels, audio.sampleRate, static_cast<float>(args.getDouble("amount", 1.0)));
        reportOutcome(outcome, std::cout);
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
        depump::writeWavFile(juce::File(args.get("out")), audio);

    if (args.has("envelope"))
    {
        const auto envelope = depump::extractRmsEnvelopeDb(depump::monoMix(audio), audio.sampleRate);
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
                 "  --batch IN_DIR --out-dir OUT_DIR [--amount 0..1]\n"
                 "      auto-recover every wav/aiff, non-destructive\n"
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
        if (args.has("batch"))
            return runBatch(args);
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
