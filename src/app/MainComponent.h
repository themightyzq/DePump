#pragma once

#include <memory>
#include <vector>

#include <juce_gui_extra/juce_gui_extra.h>

// The message thread owns the job list; the worker thread only reads the
// immutable file/output fields and posts status updates back via
// MessageManager::callAsync. No locks anywhere near the job data.
class MainComponent : public juce::Component,
                      public juce::FileDragAndDropTarget,
                      private juce::ListBoxModel
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    enum class JobState
    {
        pending,
        analyzing,
        recovered,
        clean,
        error
    };

    struct Job
    {
        juce::File file;
        JobState state = JobState::pending;
        juce::String detail{"pending"};
    };

    // ListBoxModel
    int getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override;

    void addFiles(const juce::Array<juce::File>& files);
    void chooseOutputFolder();
    void startProcessing();
    void setJobStatus(int index, JobState state, const juce::String& detail);
    void updateButtonStates();

    class Worker;

    std::vector<Job> jobs;
    juce::File outputDir;

    juce::Label titleLabel, outputLabel;
    juce::TextButton addFilesButton{"Add Files..."};
    juce::TextButton outputButton{"Output Folder..."};
    juce::TextButton processButton{"Process All"};
    juce::TextButton clearButton{"Clear"};
    juce::ListBox jobList{"jobs", this};
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<Worker> worker;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
