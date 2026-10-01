#pragma once

#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"

// DePump's plugin editor, built on the ZQ SFX house UI (zqsfx_ui): the eleven parameters through
// APVTS attachments, a Learn button, and a status line that follows the Learn engine from
// "ready" through listening and analysing to learned or failed.
//
// Layout is drawn once at kDesignWidth x kDesignHeight and uniformly scaled to whatever size the
// host gives the window (fixed aspect ratio, kMinScale..kMaxScale), so every control keeps its
// proportions on a small laptop screen or a large monitor.
//
// The editor reads the engine only by polling (Learn status is an atomic plus a lock-guarded
// string) from a message-thread timer, so there is no callback that could outlive it.
class DePumpAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit DePumpAudioProcessorEditor(DePumpAudioProcessor& processorIn);
    ~DePumpAudioProcessorEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    static constexpr int kDesignWidth = 640;
    static constexpr int kDesignHeight = 340;
    static constexpr float kMinScale = 0.9f;
    static constexpr float kMaxScale = 1.5f;

    // What the status line currently shows, for tests and screen readers.
    juce::String getStatusTextForTest() const;

private:
    class Content;

    void timerCallback() override;

    DePumpAudioProcessor& processor;
    std::unique_ptr<Content> content;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DePumpAudioProcessorEditor)
};
