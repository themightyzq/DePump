#pragma once

#include <vector>

#include <juce_audio_formats/juce_audio_formats.h>

namespace depump
{

struct AudioFileData
{
    std::vector<std::vector<float>> channels;
    double sampleRate = 0.0;
    size_t numSamples() const { return channels.empty() ? 0 : channels.front().size(); }
};

// Both throw std::runtime_error with a user-readable message.
AudioFileData readAudioFile(const juce::File& file);
void writeWavFile(const juce::File& file, const AudioFileData& audio); // 32-bit float

std::vector<float> monoMix(const AudioFileData& audio);

} // namespace depump
