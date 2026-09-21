#pragma once

#include <memory>
#include <vector>

#include <juce_gui_extra/juce_gui_extra.h>
#include <zqsfx_ui/zqsfx_ui.h>

// The message thread owns the job list; the worker thread only reads the
// immutable file/output fields and posts status updates back via
// MessageManager::callAsync. No locks anywhere near the job data.
class MainComponent : public juce::Component, public juce::FileDragAndDropTarget, private juce::ListBoxModel
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    // For depump_ui_snapshot only: one row per JobState, no files touched.
    void populateForSnapshot();

    // Installs the ZQ SFX house LookAndFeel (zqsfx_ui) as the JUCE default for its lifetime.
    // Must outlive every component, so the application owns one ahead of its window.
    struct ScopedHouseLookAndFeel
    {
        ScopedHouseLookAndFeel() { juce::LookAndFeel::setDefaultLookAndFeel(&lookAndFeel); }
        ~ScopedHouseLookAndFeel() { juce::LookAndFeel::setDefaultLookAndFeel(nullptr); }
        zqsfx::ui::LookAndFeel lookAndFeel;
    };

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
    void showAbout();

    class Worker;

    std::vector<Job> jobs;
    juce::File outputDir;

    // House UI (zqsfx_ui): chassis behind everything, the job list sits in a titled panel, and
    // the company mark in the header doubles as the About button.
    zqsfx::ui::Panel stemsPanel{"Stems"};
    zqsfx::ui::LogoMark logo{"DePump"};
    juce::TooltipWindow tooltips{this, 500};

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
