#include "AudioFileIO.h"

#include <stdexcept>

namespace depump
{

AudioFileData readAudioFile(const juce::File& file)
{
    juce::AudioFormatManager manager;
    manager.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(manager.createReaderFor(file));
    if (reader == nullptr)
        throw std::runtime_error("cannot read audio file: " + file.getFullPathName().toStdString());

    AudioFileData audio;
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

void writeWavFile(const juce::File& file, const AudioFileData& audio)
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

std::vector<float> monoMix(const AudioFileData& audio)
{
    std::vector<float> mono(audio.numSamples(), 0.0f);
    for (const auto& channel : audio.channels)
        for (size_t i = 0; i < channel.size(); ++i)
            mono[i] += channel[i] / static_cast<float>(audio.channels.size());
    return mono;
}

} // namespace depump
