#include "MainComponent.h"

#include "dsp/Recovery.h"
#include "io/AudioFileIO.h"

// Processes the job queue sequentially off the message thread. Each job's
// result is posted back with callAsync; the worker never touches the jobs
// vector itself.
class MainComponent::Worker : public juce::Thread
{
public:
    struct Task
    {
        int index;
        juce::File input;
        juce::File output;
    };

    Worker(MainComponent& ownerIn, std::vector<Task> tasksIn)
        : juce::Thread("DePump batch"), owner(ownerIn), tasks(std::move(tasksIn))
    {
    }

    void run() override
    {
        for (const auto& task : tasks)
        {
            if (threadShouldExit())
                return;
            post(task.index, JobState::analyzing, "analyzing...");

            try
            {
                auto audio = depump::readAudioFile(task.input);
                const auto outcome = depump::analyzeAndRecover(audio.channels, audio.sampleRate);
                depump::writeWavFile(task.output, audio);

                if (outcome.analysis.pumpDetected)
                {
                    juce::String detail;
                    detail << "recovered: " << juce::String(1.0 / outcome.analysis.periodSeconds, 2)
                           << " Hz, " << juce::String(outcome.analysis.depthDb, 1) << " dB dips";
                    if (outcome.trimDb < 0.0f)
                        detail << ", trimmed " << juce::String(outcome.trimDb, 1) << " dB";
                    post(task.index, JobState::recovered, detail);
                }
                else
                {
                    post(task.index, JobState::clean, "no pumping detected - passed through");
                }
            }
            catch (const std::exception& e)
            {
                post(task.index, JobState::error, juce::String("error: ") + e.what());
            }
        }
        juce::MessageManager::callAsync([safeOwner = juce::Component::SafePointer(&owner)] {
            if (safeOwner != nullptr)
                safeOwner->updateButtonStates();
        });
    }

private:
    void post(int index, JobState state, juce::String detail)
    {
        juce::MessageManager::callAsync(
            [safeOwner = juce::Component::SafePointer(&owner), index, state,
             detailCopy = std::move(detail)] {
                if (safeOwner != nullptr)
                    safeOwner->setJobStatus(index, state, detailCopy);
            });
    }

    MainComponent& owner;
    std::vector<Task> tasks;
};

MainComponent::MainComponent()
{
    titleLabel.setText("DePump: drop pumped stems below, pick an output folder, process.",
                       juce::dontSendNotification);
    titleLabel.setFont(juce::FontOptions(16.0f, juce::Font::bold));
    addAndMakeVisible(titleLabel);

    outputLabel.setText("Output folder: (not set)", juce::dontSendNotification);
    addAndMakeVisible(outputLabel);

    addFilesButton.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser>("Add stems", juce::File(), "*.wav;*.aif;*.aiff");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles |
                                 juce::FileBrowserComponent::canSelectMultipleItems,
                             [this](const juce::FileChooser& fc) { addFiles(fc.getResults()); });
    };
    addAndMakeVisible(addFilesButton);

    outputButton.onClick = [this] { chooseOutputFolder(); };
    addAndMakeVisible(outputButton);

    processButton.onClick = [this] { startProcessing(); };
    addAndMakeVisible(processButton);

    clearButton.onClick = [this] {
        if (worker == nullptr || !worker->isThreadRunning())
        {
            jobs.clear();
            jobList.updateContent();
            updateButtonStates();
        }
    };
    addAndMakeVisible(clearButton);

    jobList.setRowHeight(26);
    jobList.setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    addAndMakeVisible(jobList);

    updateButtonStates();
    setSize(760, 480);
}

MainComponent::~MainComponent()
{
    if (worker != nullptr)
        worker->stopThread(5000);
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
    if (jobs.empty())
    {
        g.setColour(juce::Colours::grey);
        g.setFont(juce::FontOptions(15.0f));
        g.drawText("Drop .wav / .aiff stems anywhere in this window",
                   jobList.getBounds(), juce::Justification::centred);
    }
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced(12);
    titleLabel.setBounds(area.removeFromTop(28));
    area.removeFromTop(6);

    auto buttonRow = area.removeFromTop(30);
    addFilesButton.setBounds(buttonRow.removeFromLeft(110));
    buttonRow.removeFromLeft(8);
    outputButton.setBounds(buttonRow.removeFromLeft(130));
    buttonRow.removeFromLeft(8);
    processButton.setBounds(buttonRow.removeFromLeft(110));
    buttonRow.removeFromLeft(8);
    clearButton.setBounds(buttonRow.removeFromLeft(80));

    area.removeFromTop(6);
    outputLabel.setBounds(area.removeFromTop(24));
    area.removeFromTop(6);
    jobList.setBounds(area);
}

bool MainComponent::isInterestedInFileDrag(const juce::StringArray&) { return true; }

void MainComponent::filesDropped(const juce::StringArray& files, int, int)
{
    juce::Array<juce::File> audioFiles;
    for (const auto& path : files)
    {
        const juce::File file(path);
        if (file.isDirectory())
            audioFiles.addArray(file.findChildFiles(juce::File::findFiles, false, "*.wav;*.aif;*.aiff"));
        else if (file.hasFileExtension("wav;aif;aiff"))
            audioFiles.add(file);
    }
    addFiles(audioFiles);
}

int MainComponent::getNumRows() { return static_cast<int>(jobs.size()); }

void MainComponent::paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool)
{
    if (row < 0 || row >= getNumRows())
        return;
    const auto& job = jobs[static_cast<size_t>(row)];

    if (row % 2 == 0)
        g.fillAll(juce::Colours::white.withAlpha(0.04f));

    auto colourFor = [](JobState state) {
        switch (state)
        {
            case JobState::recovered: return juce::Colours::mediumseagreen;
            case JobState::clean: return juce::Colours::cornflowerblue;
            case JobState::error: return juce::Colours::indianred;
            case JobState::analyzing: return juce::Colours::orange;
            case JobState::pending: break;
        }
        return juce::Colours::grey;
    };

    auto bounds = juce::Rectangle<int>(0, 0, width, height).reduced(6, 4);
    g.setColour(colourFor(job.state));
    g.fillEllipse(bounds.removeFromLeft(10).withSizeKeepingCentre(8, 8).toFloat());
    bounds.removeFromLeft(8);

    g.setColour(getLookAndFeel().findColour(juce::Label::textColourId));
    g.setFont(juce::FontOptions(14.0f));
    g.drawText(job.file.getFileName(), bounds.removeFromLeft(width / 3), juce::Justification::centredLeft, true);
    g.setColour(getLookAndFeel().findColour(juce::Label::textColourId).withAlpha(0.75f));
    g.drawText(job.detail, bounds, juce::Justification::centredLeft, true);
}

void MainComponent::addFiles(const juce::Array<juce::File>& files)
{
    for (const auto& file : files)
    {
        if (!file.existsAsFile())
            continue;
        const bool already = std::any_of(jobs.begin(), jobs.end(),
                                         [&](const Job& job) { return job.file == file; });
        if (!already)
            jobs.push_back({file});
    }
    jobList.updateContent();
    updateButtonStates();
    repaint();
}

void MainComponent::chooseOutputFolder()
{
    chooser = std::make_unique<juce::FileChooser>("Choose output folder (originals are never overwritten)");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                         [this](const juce::FileChooser& fc) {
                             const auto result = fc.getResult();
                             if (result.isDirectory())
                             {
                                 outputDir = result;
                                 outputLabel.setText("Output folder: " + outputDir.getFullPathName(),
                                                     juce::dontSendNotification);
                                 updateButtonStates();
                             }
                         });
}

void MainComponent::startProcessing()
{
    if (jobs.empty() || !outputDir.isDirectory() || (worker != nullptr && worker->isThreadRunning()))
        return;

    std::vector<Worker::Task> tasks;
    for (size_t i = 0; i < jobs.size(); ++i)
    {
        if (jobs[i].file.getParentDirectory() == outputDir)
        {
            setJobStatus(static_cast<int>(i), JobState::error,
                         "error: output folder equals the file's folder (non-destructive rule)");
            continue;
        }
        setJobStatus(static_cast<int>(i), JobState::pending, "queued");
        tasks.push_back({static_cast<int>(i), jobs[i].file, outputDir.getChildFile(jobs[i].file.getFileName())});
    }
    if (tasks.empty())
        return;

    worker = std::make_unique<Worker>(*this, std::move(tasks));
    worker->startThread();
    updateButtonStates();
}

void MainComponent::setJobStatus(int index, JobState state, const juce::String& detail)
{
    if (index < 0 || index >= getNumRows())
        return;
    jobs[static_cast<size_t>(index)].state = state;
    jobs[static_cast<size_t>(index)].detail = detail;
    jobList.repaintRow(index);
}

void MainComponent::updateButtonStates()
{
    const bool busy = worker != nullptr && worker->isThreadRunning();
    processButton.setEnabled(!busy && !jobs.empty() && outputDir.isDirectory());
    addFilesButton.setEnabled(!busy);
    clearButton.setEnabled(!busy && !jobs.empty());
    outputButton.setEnabled(!busy);
}
