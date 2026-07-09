#include "Recovery.h"

#include "GainCurve.h"

namespace depump
{

namespace
{
std::vector<float> monoMixOf(const std::vector<std::vector<float>>& channels)
{
    if (channels.empty())
        return {};
    std::vector<float> mono(channels.front().size(), 0.0f);
    for (const auto& channel : channels)
        for (size_t i = 0; i < channel.size() && i < mono.size(); ++i)
            mono[i] += channel[i] / static_cast<float>(channels.size());
    return mono;
}
} // namespace

RecoveryOutcome analyzeAndRecover(std::vector<std::vector<float>>& channels, double sampleRate, float amount)
{
    RecoveryOutcome outcome;
    if (channels.empty() || channels.front().empty())
        return outcome;

    outcome.analysis = analyzePump(monoMixOf(channels), sampleRate);
    if (!outcome.analysis.pumpDetected)
        return outcome;

    const auto gain = gainCurveFromAnalysis(outcome.analysis, sampleRate, channels.front().size());
    for (auto& channel : channels)
        applyInverseGain(channel, gain, amount);

    outcome.trimDb = trimToCeiling(channels);
    return outcome;
}

} // namespace depump
