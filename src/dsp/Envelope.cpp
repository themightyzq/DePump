#include "Envelope.h"

#include <cmath>
#include <sstream>
#include <stdexcept>

namespace depump
{

std::vector<EnvelopePoint> extractRmsEnvelopeDb(const std::vector<float>& samples, double sampleRate,
                                                double windowMs, double hopMs)
{
    std::vector<EnvelopePoint> envelope;
    if (samples.empty() || sampleRate <= 0.0)
        return envelope;

    const size_t window = std::max<size_t>(1, static_cast<size_t>(windowMs * 0.001 * sampleRate));
    const size_t hop = std::max<size_t>(1, static_cast<size_t>(hopMs * 0.001 * sampleRate));

    for (size_t start = 0; start + window <= samples.size(); start += hop)
    {
        double sumSquares = 0.0;
        for (size_t i = start; i < start + window; ++i)
            sumSquares += static_cast<double>(samples[i]) * samples[i];

        const double rms = std::sqrt(sumSquares / static_cast<double>(window));
        constexpr double silenceFloor = 1.0e-10; // -200 dB
        envelope.push_back({static_cast<double>(start) / sampleRate, 20.0 * std::log10(rms + silenceFloor)});
    }

    return envelope;
}

EnvelopeDeviation compareEnvelopes(const std::vector<EnvelopePoint>& a, const std::vector<EnvelopePoint>& b)
{
    EnvelopeDeviation result;
    result.pointsCompared = std::min(a.size(), b.size());
    if (result.pointsCompared == 0)
        return result;

    double sumSquares = 0.0;
    for (size_t i = 0; i < result.pointsCompared; ++i)
    {
        const double diff = std::abs(a[i].rmsDb - b[i].rmsDb);
        result.maxAbsDb = std::max(result.maxAbsDb, diff);
        sumSquares += diff * diff;
    }
    result.rmsDb = std::sqrt(sumSquares / static_cast<double>(result.pointsCompared));
    return result;
}

std::string envelopeToCsv(const std::vector<EnvelopePoint>& envelope)
{
    std::ostringstream out;
    out << "time_sec,rms_db\n";
    out.precision(9);
    for (const auto& point : envelope)
        out << point.timeSec << ',' << point.rmsDb << '\n';
    return out.str();
}

std::vector<EnvelopePoint> envelopeFromCsv(const std::string& csv)
{
    std::vector<EnvelopePoint> envelope;
    std::istringstream in(csv);
    std::string line;

    if (!std::getline(in, line) || line.rfind("time_sec,rms_db", 0) != 0)
        throw std::runtime_error("envelope CSV missing 'time_sec,rms_db' header");

    while (std::getline(in, line))
    {
        if (line.empty())
            continue;
        const auto comma = line.find(',');
        if (comma == std::string::npos)
            throw std::runtime_error("envelope CSV row missing comma: " + line);
        envelope.push_back({std::stod(line.substr(0, comma)), std::stod(line.substr(comma + 1))});
    }
    return envelope;
}

} // namespace depump
