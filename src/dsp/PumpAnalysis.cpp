#include "PumpAnalysis.h"

#include <algorithm>
#include <cmath>

#include "Envelope.h"
#include "GainCurve.h"
#include "PumpProfile.h"

namespace depump
{

namespace
{
// Two envelope resolutions with one hop grid:
// - DETECTION uses a long window: tonal beating in real material (difference
//   tones of chords sit at tens of Hz) reads as huge "loudness modulation"
//   at short windows and must not trigger pump detection.
// - The correction TEMPLATE folds the short-window envelope: median folding
//   collapses beat ripple (incoherent with the pump period) to a constant
//   while keeping the dip's attack edge sharp.
constexpr double templateWindowMs = 10.0;
constexpr double detectionWindowMs = 50.0;
constexpr double analysisHopMs = 2.5;

// Pump-rate search range: 0.4 Hz .. 10 Hz.
constexpr double minPeriodSec = 0.1;
constexpr double maxPeriodSec = 2.5;

constexpr double detectConfidence = 0.5; // comb-verified autocorrelation floor
constexpr float detectDepthDb = 1.0f;    // modulation floor worth fixing
constexpr float maxCorrectionDb = 30.0f; // template clamp (safety)

std::vector<double> detrend(const std::vector<EnvelopePoint>& envelope, double windowSec, double hopSec)
{
    const size_t half = std::max<size_t>(1, static_cast<size_t>(windowSec / hopSec / 2.0));
    std::vector<double> detrended(envelope.size());

    for (size_t i = 0; i < envelope.size(); ++i)
    {
        const size_t lo = i > half ? i - half : 0;
        const size_t hi = std::min(envelope.size(), i + half + 1);
        double mean = 0.0;
        for (size_t j = lo; j < hi; ++j)
            mean += envelope[j].rmsDb;
        mean /= static_cast<double>(hi - lo);
        detrended[i] = envelope[i].rmsDb - mean;
    }
    return detrended;
}

// Normalized autocorrelation coefficient at one lag, over the overlap.
double correlationAtLag(const std::vector<double>& x, size_t lag)
{
    const size_t n = x.size() - lag;
    double sumXY = 0.0, sumXX = 0.0, sumYY = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        sumXY += x[i] * x[i + lag];
        sumXX += x[i] * x[i];
        sumYY += x[i + lag] * x[i + lag];
    }
    const double denom = std::sqrt(sumXX * sumYY);
    return denom > 0.0 ? sumXY / denom : 0.0;
}

struct LocalPeak
{
    double lagHops = 0.0;
    double value = -1.0;
};

// Highest correlation within +-radius of a lag, parabolic-refined.
LocalPeak localPeak(const std::vector<double>& corr, size_t center, size_t radius, size_t minLag, size_t maxLag)
{
    const size_t lo = std::max(minLag, center > radius ? center - radius : minLag);
    const size_t hi = std::min(maxLag, center + radius);
    if (lo > hi)
        return {};

    size_t best = lo;
    for (size_t lag = lo; lag <= hi; ++lag)
        if (corr[lag] > corr[best])
            best = lag;

    LocalPeak peak{static_cast<double>(best), corr[best]};
    if (best > minLag && best + 1 <= maxLag)
    {
        const double y0 = corr[best - 1], y1 = corr[best], y2 = corr[best + 1];
        const double denom = y0 - 2.0 * y1 + y2;
        if (std::abs(denom) > 1.0e-12)
            peak.lagHops += 0.5 * (y0 - y2) / denom;
    }
    return peak;
}

struct PeriodEstimate
{
    double lagHops = 0.0;
    double confidence = 0.0;
};

// Autocorrelation peaks at EVERY multiple of the true period, and dip-train
// peaks are broad, so the raw argmax is unreliable (it lands on multiples).
// Comb-score candidate fundamentals — a candidate is credible only if ALL
// its multiples peak — then refine precision against the largest reachable
// multiple, dividing the localization error by that factor.
PeriodEstimate estimatePeriod(const std::vector<double>& detrended, double hopSec)
{
    const auto minLag = static_cast<size_t>(minPeriodSec / hopSec);
    // Need at least ~4 cycles in the material to fold reliably.
    const size_t maxLagBySignal = detrended.size() / 4;
    const size_t maxLag = std::min(static_cast<size_t>(maxPeriodSec / hopSec), maxLagBySignal);
    if (minLag + 2 >= maxLag)
        return {};

    std::vector<double> corr(maxLag + 1, -1.0);
    size_t globalBest = minLag;
    for (size_t lag = minLag; lag <= maxLag; ++lag)
    {
        corr[lag] = correlationAtLag(detrended, lag);
        if (corr[lag] > corr[globalBest])
            globalBest = lag;
    }
    if (corr[globalBest] <= 0.0)
        return {};

    constexpr size_t peakRadius = 3;
    double bestScore = -1.0;
    std::vector<std::pair<double, double>> candidates; // {fundamental hops, comb score}
    for (size_t divisor = 1; divisor <= 8; ++divisor)
    {
        const double fundamental = static_cast<double>(globalBest) / static_cast<double>(divisor);
        if (fundamental < static_cast<double>(minLag))
            break;

        double sum = 0.0;
        size_t count = 0;
        for (double multiple = fundamental; multiple <= static_cast<double>(maxLag) + 0.5;
             multiple += fundamental)
        {
            sum += localPeak(corr, static_cast<size_t>(std::llround(multiple)), peakRadius, minLag, maxLag).value;
            ++count;
        }
        const double score = count > 0 ? sum / static_cast<double>(count) : -1.0;
        candidates.emplace_back(fundamental, score);
        bestScore = std::max(bestScore, score);
    }

    // Smallest fundamental whose multiples all score nearly as well as the
    // best candidate — i.e. the true fundamental, not a multiple of it.
    double chosen = 0.0, chosenScore = 0.0;
    for (auto it = candidates.rbegin(); it != candidates.rend(); ++it)
    {
        if (it->second >= 0.95 * bestScore)
        {
            chosen = it->first;
            chosenScore = it->second;
            break;
        }
    }
    if (chosen <= 0.0 || chosenScore <= 0.0)
        return {};

    // Precision: locate the peak at the largest reachable multiple and
    // divide — a fixed localization error shrinks by the multiple index.
    const auto largestMultiple = static_cast<size_t>(std::floor(static_cast<double>(maxLag) / chosen));
    if (largestMultiple >= 1)
    {
        const auto around = static_cast<size_t>(std::llround(chosen * static_cast<double>(largestMultiple)));
        const auto refined = localPeak(corr, around, peakRadius + 2, minLag, maxLag);
        if (refined.value > 0.0)
            chosen = refined.lagHops / static_cast<double>(largestMultiple);
    }

    return {chosen, std::max(0.0, chosenScore)};
}

// Autocorrelation localizes the period to ~a hop; over many cycles even a
// 0.3% error drifts the fold by tens of ms and smears the template. Refine
// by directly maximizing fold coherence: the period is right when folding
// concentrates the (detrended) envelope's energy into the bin means.
double refinePeriodByFolding(const std::vector<double>& detrended, double hopSec, double periodHops,
                             double searchSpan)
{
    constexpr size_t numBins = 128;
    constexpr int searchSteps = 41;

    auto coherence = [&](double candidateHops) {
        double sums[numBins] = {};
        size_t counts[numBins] = {};
        for (size_t i = 0; i < detrended.size(); ++i)
        {
            const double cyclePos = std::fmod(static_cast<double>(i), candidateHops);
            auto bin = static_cast<size_t>(cyclePos / candidateHops * numBins);
            bin = std::min(bin, numBins - 1);
            sums[bin] += detrended[i];
            ++counts[bin];
        }
        double energy = 0.0;
        for (size_t b = 0; b < numBins; ++b)
            if (counts[b] > 0)
                energy += sums[b] * sums[b] / static_cast<double>(counts[b]);
        return energy;
    };

    double bestPeriod = periodHops, bestEnergy = -1.0;
    double energies[searchSteps];
    double periods[searchSteps];
    int bestIndex = 0;
    for (int s = 0; s < searchSteps; ++s)
    {
        periods[s] = periodHops * (1.0 + searchSpan * (2.0 * s / (searchSteps - 1) - 1.0));
        energies[s] = coherence(periods[s]);
        if (energies[s] > bestEnergy)
        {
            bestEnergy = energies[s];
            bestPeriod = periods[s];
            bestIndex = s;
        }
    }

    if (bestIndex > 0 && bestIndex < searchSteps - 1)
    {
        const double y0 = energies[bestIndex - 1], y1 = energies[bestIndex], y2 = energies[bestIndex + 1];
        const double denom = y0 - 2.0 * y1 + y2;
        if (std::abs(denom) > 1.0e-12)
            bestPeriod += 0.5 * (y0 - y2) / denom * (periods[1] - periods[0]);
    }

    (void) hopSec;
    return bestPeriod;
}

// Fold an envelope at the period (window CENTERS, since timestamps mark
// window start); median per bin; return template relative to a robust
// high-quantile reference, clamped to [-maxCorrectionDb, 0].
std::vector<float> foldTemplate(const std::vector<EnvelopePoint>& envelope, double windowMs,
                                double periodSeconds, size_t numBins)
{
    std::vector<std::vector<double>> bins(numBins);
    const double halfWindowSec = windowMs * 0.001 / 2.0;
    for (const auto& point : envelope)
    {
        const double cyclePos = std::fmod(point.timeSec + halfWindowSec, periodSeconds);
        auto bin = static_cast<size_t>(cyclePos / periodSeconds * static_cast<double>(numBins));
        bins[std::min(bin, numBins - 1)].push_back(point.rmsDb);
    }

    std::vector<float> templateDb(numBins, 0.0f);
    for (size_t b = 0; b < numBins; ++b)
    {
        if (bins[b].empty())
        {
            templateDb[b] = b > 0 ? templateDb[b - 1] : 0.0f;
            continue;
        }
        auto& values = bins[b];
        std::nth_element(values.begin(), values.begin() + static_cast<long>(values.size() / 2), values.end());
        templateDb[b] = static_cast<float>(values[values.size() / 2]);
    }

    // Circular median FILTER across bins: rejects per-bin noise from tonal
    // beating (which the per-bin median can't fully kill with few cycles)
    // while preserving the dip's attack edge — a moving average would smear
    // it and the smear error scales with depth.
    constexpr int filterRadius = 2;
    std::vector<float> filtered(numBins);
    for (size_t b = 0; b < numBins; ++b)
    {
        float window[2 * filterRadius + 1];
        for (int offset = -filterRadius; offset <= filterRadius; ++offset)
        {
            const auto neighbor =
                static_cast<size_t>((static_cast<long>(b) + offset + static_cast<long>(numBins)) %
                                    static_cast<long>(numBins));
            window[offset + filterRadius] = templateDb[neighbor];
        }
        std::nth_element(window, window + filterRadius, window + 2 * filterRadius + 1);
        filtered[b] = window[filterRadius];
    }
    templateDb = std::move(filtered);

    auto sorted = templateDb;
    std::sort(sorted.begin(), sorted.end());
    const float referenceDb = sorted[static_cast<size_t>(0.95 * static_cast<double>(numBins - 1))];

    for (auto& value : templateDb)
        value = std::clamp(value - referenceDb, -maxCorrectionDb, 0.0f);
    return templateDb;
}

float templateDepthDb(const std::vector<float>& templateDb)
{
    float minDb = 0.0f;
    for (float value : templateDb)
        minDb = std::min(minDb, value);
    return -minDb;
}

// --- Parametric refinement -------------------------------------------------
// DAW sidechain pumping is one-pole compressor dynamics by construction, so
// the folded template usually IS a PumpProfile curve — but smeared by the
// RMS window (power-domain average) and dusted with fold noise. Fit the
// 5-parameter model THROUGH the known measurement operator (synthesize →
// smear like the window would → compare), then hand back the UNSMEARED
// fitted curve: noise-free and edge-exact. If the model can't explain the
// template (non-compressor pump shapes), keep the raw template.

// One steady-state cycle of the model at bin resolution, normalized so the
// cycle's loudest point is 0 dB. Evaluated at bin centers (hence the x4
// oversampling and half-bin offset).
std::vector<float> modelCycleDb(const PumpProfile& profile, double periodSeconds, size_t numBins)
{
    constexpr size_t oversample = 4;
    const double synthRate = static_cast<double>(numBins * oversample) / periodSeconds;
    const auto gain = synthesizeGainCurve(profile, synthRate, numBins * oversample * 3);

    std::vector<float> db(numBins);
    float maxDb = -1.0e9f;
    const size_t lastCycle = numBins * oversample * 2;
    for (size_t b = 0; b < numBins; ++b)
    {
        const auto index = lastCycle + b * oversample + oversample / 2;
        db[b] = 20.0f * std::log10(std::max(gain[index], 1.0e-6f));
        maxDb = std::max(maxDb, db[b]);
    }
    for (auto& value : db)
        value -= maxDb;
    return db;
}

// Apply the RMS window's power-domain smear at bin resolution.
std::vector<float> smearLikeWindow(const std::vector<float>& db, double windowMs, double hopMs)
{
    const int kernelBins = std::max(1, static_cast<int>(std::lround(windowMs / hopMs)));
    if (kernelBins <= 1)
        return db;
    const auto numBins = db.size();
    const int half = kernelBins / 2;

    std::vector<float> out(numBins);
    for (size_t b = 0; b < numBins; ++b)
    {
        double power = 0.0;
        for (int offset = -half; offset < kernelBins - half; ++offset)
        {
            const auto index = static_cast<size_t>(
                (static_cast<long>(b) + offset + static_cast<long>(numBins)) % static_cast<long>(numBins));
            power += std::pow(10.0, db[index] / 10.0);
        }
        out[b] = static_cast<float>(10.0 * std::log10(power / kernelBins));
    }
    return out;
}

double fitResidualRms(const std::vector<float>& templateDb, const PumpProfile& candidate,
                      double periodSeconds, double windowMs, double hopMs)
{
    const auto model = smearLikeWindow(modelCycleDb(candidate, periodSeconds, templateDb.size()), windowMs, hopMs);
    // Offset-invariant: template and model use slightly different level
    // references; fit the SHAPE and let the absolute level come from the
    // model's own one-pole structure at application time.
    const auto count = static_cast<double>(templateDb.size());
    double meanDiff = 0.0;
    for (size_t b = 0; b < templateDb.size(); ++b)
        meanDiff += templateDb[b] - model[b];
    meanDiff /= count;

    double sumSquares = 0.0;
    for (size_t b = 0; b < templateDb.size(); ++b)
    {
        const double diff = templateDb[b] - model[b] - meanDiff;
        sumSquares += diff * diff;
    }
    return std::sqrt(sumSquares / count);
}

struct ModelFit
{
    PumpProfile profile;
    double residualRmsDb = 1.0e9;
};

// The decisive objective: apply the candidate correction to the ACTUAL
// audio and measure how non-smooth the corrected loudness envelope is,
// with the same instrument the product is judged by. No envelope-domain
// forward model (whose smear approximations were limiting parameter
// precision), and pointwise — so period error can't hide by decohering
// the residual. This is literally "correct until the pumping is gone."
double correctedEnvelopeRoughness(const std::vector<float>& monoSamples, double sampleRate,
                                  const PumpProfile& profile, double periodSeconds)
{
    auto candidate = profile;
    candidate.rateHz = static_cast<float>(1.0 / periodSeconds);

    auto corrected = monoSamples;
    const auto gain = synthesizeGainCurve(candidate, sampleRate, corrected.size());
    applyInverseGain(corrected, gain);

    const auto envelope = extractRmsEnvelopeDb(corrected, sampleRate);
    if (envelope.size() < 8)
        return 1.0e9;

    // Deviation from a moving average longer than the pump cycle: residual
    // pumping shows up, genuine slow level changes don't.
    const double hopSec = envelope[1].timeSec - envelope[0].timeSec;
    const size_t half = std::max<size_t>(1, static_cast<size_t>(periodSeconds / hopSec));
    double sumSquares = 0.0;
    for (size_t i = 0; i < envelope.size(); ++i)
    {
        const size_t lo = i > half ? i - half : 0;
        const size_t hi = std::min(envelope.size(), i + half + 1);
        double mean = 0.0;
        for (size_t j = lo; j < hi; ++j)
            mean += envelope[j].rmsDb;
        mean /= static_cast<double>(hi - lo);
        const double deviation = envelope[i].rmsDb - mean;
        sumSquares += deviation * deviation;
    }
    return std::sqrt(sumSquares / static_cast<double>(envelope.size()));
}

// Joint polish of period + ALL model parameters against the corrected-audio
// objective.
void polishAlignment(const std::vector<float>& monoSamples, double sampleRate, PumpProfile& profile,
                     double& periodSeconds)
{
    double best = correctedEnvelopeRoughness(monoSamples, sampleRate, profile, periodSeconds);

    struct Param
    {
        float PumpProfile::* member;
        float span, lo, hi; // first-round +- span/2, clamped to [lo, hi]
    };
    const Param params[] = {
        {&PumpProfile::phase01, 0.04f, -1.0f, 2.0f}, // wrapped below
        {&PumpProfile::depthDb, 4.0f, 1.0f, 30.0f},
        {&PumpProfile::attackMs, 40.0f, 1.0f, 150.0f},
        {&PumpProfile::holdMs, 80.0f, 0.0f, 400.0f},
        {&PumpProfile::releaseMs, 160.0f, 10.0f, 800.0f},
    };

    constexpr int rounds = 4;
    constexpr int steps = 6;
    for (int round = 0; round < rounds; ++round)
    {
        const double shrink = std::pow(0.4, round);

        // Period: +-0.2% shrinking per round — wide enough to absorb any
        // residual error from the hop-grid estimates upstream.
        for (int s = -steps; s <= steps; ++s)
        {
            if (s == 0)
                continue;
            const double candidate = periodSeconds * (1.0 + 0.002 * shrink * s / steps);
            const double residual = correctedEnvelopeRoughness(monoSamples, sampleRate, profile, candidate);
            if (residual < best)
            {
                best = residual;
                periodSeconds = candidate;
            }
        }

        for (const auto& param : params)
        {
            for (int s = -steps; s <= steps; ++s)
            {
                if (s == 0)
                    continue;
                auto candidate = profile;
                float value = profile.*param.member +
                              param.span * static_cast<float>(shrink) * static_cast<float>(s) / (2.0f * steps);
                if (param.member == &PumpProfile::phase01)
                    value = static_cast<float>(std::fmod(static_cast<double>(value) + 1.0, 1.0));
                candidate.*param.member = std::clamp(value, param.lo, param.hi);

                const double residual =
                    correctedEnvelopeRoughness(monoSamples, sampleRate, candidate, periodSeconds);
                if (residual < best)
                {
                    best = residual;
                    profile = candidate;
                }
            }
        }
    }
}

// Coordinate descent with bracket shrinking; slices are smooth enough that
// dense sampling per round is reliable. Phase gets a full-range first round
// (it is the most multimodal parameter).
ModelFit fitPumpModel(const std::vector<float>& templateDb, double periodSeconds, double dipTimeSeconds,
                      float depthDb, double windowMs, double hopMs)
{
    ModelFit fit;
    fit.profile.rateHz = static_cast<float>(1.0 / periodSeconds);
    fit.profile.depthDb = depthDb;
    fit.profile.attackMs = 10.0f;
    fit.profile.holdMs = 60.0f;
    fit.profile.releaseMs = 150.0f;
    fit.profile.phase01 = static_cast<float>(std::fmod(dipTimeSeconds / periodSeconds + 0.8, 1.0));

    struct Param
    {
        float PumpProfile::* member;
        float lo, hi;
    };
    const Param params[] = {
        {&PumpProfile::phase01, 0.0f, 1.0f},
        {&PumpProfile::depthDb, 1.0f, 30.0f},
        {&PumpProfile::attackMs, 1.0f, 150.0f},
        {&PumpProfile::holdMs, 0.0f, 400.0f},
        {&PumpProfile::releaseMs, 10.0f, 800.0f},
    };

    fit.residualRmsDb = fitResidualRms(templateDb, fit.profile, periodSeconds, windowMs, hopMs);

    constexpr int rounds = 6;
    constexpr int samplesPerRound = 17;
    for (int round = 0; round < rounds; ++round)
    {
        for (const auto& param : params)
        {
            const float current = fit.profile.*param.member;
            const float fullSpan = param.hi - param.lo;
            const float span = round == 0 ? fullSpan : fullSpan * std::pow(0.35f, static_cast<float>(round));
            const float lo = std::max(param.lo, current - span / 2.0f);
            const float hi = std::min(param.hi, current + span / 2.0f);

            for (int s = 0; s < samplesPerRound; ++s)
            {
                auto candidate = fit.profile;
                candidate.*param.member =
                    lo + (hi - lo) * static_cast<float>(s) / static_cast<float>(samplesPerRound - 1);
                const double residual = fitResidualRms(templateDb, candidate, periodSeconds, windowMs, hopMs);
                if (residual < fit.residualRmsDb)
                {
                    fit.residualRmsDb = residual;
                    fit.profile = candidate;
                }
            }
        }
    }
    return fit;
}
} // namespace

PumpAnalysis analyzePump(const std::vector<float>& samples, double sampleRate)
{
    PumpAnalysis result;
    if (samples.empty() || sampleRate <= 0.0)
        return result;

    const auto detectionEnv = extractRmsEnvelopeDb(samples, sampleRate, detectionWindowMs, analysisHopMs);
    if (detectionEnv.size() < 2)
        return result;
    // The ACTUAL hop is quantized to whole samples (floor(hopMs*sr)/sr) —
    // at 44.1 kHz that is 2.4943 ms, not 2.5. Assuming the nominal value
    // bakes a systematic 0.23% error into every period estimate.
    const double hopSec = detectionEnv[1].timeSec - detectionEnv[0].timeSec;
    if (hopSec <= 0.0 || detectionEnv.size() < static_cast<size_t>(4.0 * minPeriodSec / hopSec))
        return result;

    const auto detrended = detrend(detectionEnv, 1.5 * maxPeriodSec, hopSec);
    const auto period = estimatePeriod(detrended, hopSec);
    result.confidence = period.confidence;
    if (period.lagHops <= 0.0)
        return result;

    const double refinedHops = refinePeriodByFolding(detrended, hopSec, period.lagHops, 0.01);
    result.periodSeconds = refinedHops * hopSec;
    const auto numBins = static_cast<size_t>(std::clamp(refinedHops, 16.0, 512.0));

    // Detection judged on the beat-immune long-window fold; the correction
    // template on the sharp short-window fold.
    const auto detectionFold = foldTemplate(detectionEnv, detectionWindowMs, result.periodSeconds, numBins);
    const float detectionDepth = templateDepthDb(detectionFold);

    result.pumpDetected = result.confidence >= detectConfidence && detectionDepth >= detectDepthDb;
    if (!result.pumpDetected)
        return result;

    const auto templateEnv = extractRmsEnvelopeDb(samples, sampleRate, templateWindowMs, analysisHopMs);
    result.templateGainDb = foldTemplate(templateEnv, templateWindowMs, result.periodSeconds, numBins);
    result.depthDb = templateDepthDb(result.templateGainDb);

    size_t minBin = 0;
    for (size_t b = 0; b < numBins; ++b)
        if (result.templateGainDb[b] < result.templateGainDb[minBin])
            minBin = b;
    result.dipTimeSeconds =
        (static_cast<double>(minBin) + 0.5) / static_cast<double>(numBins) * result.periodSeconds;

    // Parametric refinement: when one-pole compressor dynamics explain the
    // measured template, swap in the unsmeared fitted curve at high
    // resolution — noise-free and edge-exact. Otherwise the raw template
    // stands (non-compressor pump shapes).
    constexpr double acceptFitRmsDb = 0.6;
    const auto fit = fitPumpModel(result.templateGainDb, result.periodSeconds, result.dipTimeSeconds,
                                  result.depthDb, templateWindowMs, analysisHopMs);
    if (fit.residualRmsDb <= acceptFitRmsDb)
    {
        result.modelFitted = true;
        result.fittedProfile = fit.profile;

        // Final whole-file polish: folding-based estimates can't see slow
        // drift; this objective can, and residual timing error at deep
        // dips' attack edges costs ~1 dB/ms. Multi-start over depth: when
        // the release never completes within a cycle, the fold's
        // peak-to-trough understates true depth (the cycle top never
        // reaches unity), and depth/release compensate each other in a
        // valley that axis-aligned descent can't cross from a bad start.
        double bestResidual = 1.0e9;
        auto bestProfile = result.fittedProfile;
        double bestPeriod = result.periodSeconds;
        for (float depthScale : {1.0f, 1.4f, 2.0f})
        {
            auto candidate = result.fittedProfile;
            candidate.depthDb = std::clamp(candidate.depthDb * depthScale, 1.0f, 30.0f);
            double candidatePeriod = result.periodSeconds;
            polishAlignment(samples, sampleRate, candidate, candidatePeriod);
            const double residual =
                correctedEnvelopeRoughness(samples, sampleRate, candidate, candidatePeriod);
            if (residual < bestResidual)
            {
                bestResidual = residual;
                bestProfile = candidate;
                bestPeriod = candidatePeriod;
            }
        }
        result.fittedProfile = bestProfile;
        result.periodSeconds = bestPeriod;
        result.fittedProfile.rateHz = static_cast<float>(1.0 / result.periodSeconds);

        constexpr size_t modelBins = 1024;
        result.templateGainDb = modelCycleDb(result.fittedProfile, result.periodSeconds, modelBins);
        result.depthDb = templateDepthDb(result.templateGainDb);

        size_t modelMinBin = 0;
        for (size_t b = 0; b < modelBins; ++b)
            if (result.templateGainDb[b] < result.templateGainDb[modelMinBin])
                modelMinBin = b;
        result.dipTimeSeconds =
            (static_cast<double>(modelMinBin) + 0.5) / static_cast<double>(modelBins) * result.periodSeconds;
    }
    return result;
}

std::vector<float> gainCurveFromAnalysis(const PumpAnalysis& analysis, double sampleRate, size_t numSamples)
{
    std::vector<float> gain(numSamples, 1.0f);
    if (!analysis.pumpDetected || analysis.templateGainDb.empty() || analysis.periodSeconds <= 0.0)
        return gain;

    // With a fitted model, reconstruct exactly: absolute one-pole curve at
    // full sample rate — including the cycle top sitting below unity when
    // the release never completes within a cycle.
    if (analysis.modelFitted)
    {
        auto profile = analysis.fittedProfile;
        profile.rateHz = static_cast<float>(1.0 / analysis.periodSeconds);
        return synthesizeGainCurve(profile, sampleRate, numSamples);
    }

    const auto numBins = analysis.templateGainDb.size();
    const double binsPerSecond = static_cast<double>(numBins) / analysis.periodSeconds;

    for (size_t n = 0; n < numSamples; ++n)
    {
        const double cyclePos = std::fmod(static_cast<double>(n) / sampleRate, analysis.periodSeconds);
        // Bin centers carry the values; interpolate linearly in dB.
        const double position = cyclePos * binsPerSecond - 0.5;
        const double floorPos = std::floor(position);
        const double frac = position - floorPos;
        const auto index = static_cast<long>(floorPos);
        const size_t b0 = static_cast<size_t>((index % static_cast<long>(numBins) + static_cast<long>(numBins))) %
                          numBins;
        const size_t b1 = (b0 + 1) % numBins;

        const double db = (1.0 - frac) * analysis.templateGainDb[b0] + frac * analysis.templateGainDb[b1];
        gain[n] = static_cast<float>(std::pow(10.0, db / 20.0));
    }
    return gain;
}

} // namespace depump
