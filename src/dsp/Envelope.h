#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace depump
{

struct EnvelopePoint
{
    double timeSec = 0.0;
    double rmsDb = 0.0;
};

// Short-term RMS envelope in dBFS. Windows shorter than windowMs at the
// tail are dropped rather than padded.
std::vector<EnvelopePoint> extractRmsEnvelopeDb(const std::vector<float>& samples, double sampleRate,
                                                double windowMs = 20.0, double hopMs = 5.0);

struct EnvelopeDeviation
{
    double maxAbsDb = 0.0;
    double rmsDb = 0.0;
    size_t pointsCompared = 0;
};

// Compare two envelopes point-by-point over their common length.
EnvelopeDeviation compareEnvelopes(const std::vector<EnvelopePoint>& a, const std::vector<EnvelopePoint>& b);

// CSV round-trip ("time_sec,rms_db" header + rows). Parse throws
// std::runtime_error on malformed input.
std::string envelopeToCsv(const std::vector<EnvelopePoint>& envelope);
std::vector<EnvelopePoint> envelopeFromCsv(const std::string& csv);

} // namespace depump
