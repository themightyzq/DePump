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
                    detail << "recovered: " << juce::String(1.0 / outcome.analysis.periodSeconds, 2) << " Hz, "
                           << juce::String(outcome.analysis.depthDb, 1) << " dB dips";
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
        juce::MessageManager::callAsync(
            [safeOwner = juce::Component::SafePointer(&owner)]
            {
                if (safeOwner != nullptr)
                    safeOwner->updateButtonStates();
            });
    }

private:
    void post(int index, JobState state, juce::String detail)
    {
        juce::MessageManager::callAsync(
            [safeOwner = juce::Component::SafePointer(&owner), index, state, detailCopy = std::move(detail)]
            {
                if (safeOwner != nullptr)
                    safeOwner->setJobStatus(index, state, detailCopy);
            });
    }

    MainComponent& owner;
    std::vector<Task> tasks;
};

MainComponent::MainComponent()
{
    namespace colour = zqsfx::ui::colour;

    titleLabel.setText("DePump: drop pumped stems below, pick an output folder, process.", juce::dontSendNotification);
    titleLabel.setFont(juce::FontOptions(16.0f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, colour::silkTitle);
    addAndMakeVisible(titleLabel);

    logo.onClick = [this] { showAbout(); };
    addAndMakeVisible(logo);

    outputLabel.setText("Output folder: (not set)", juce::dontSendNotification);
    outputLabel.setFont(juce::FontOptions(13.0f));
    outputLabel.setColour(juce::Label::textColourId, colour::silkCaption);
    addAndMakeVisible(outputLabel);

    addAndMakeVisible(stemsPanel);

    // every interactive control: tooltip + accessible name + description (house accessibility floor)
    auto describe = [](juce::Button& b, const juce::String& tip)
    {
        b.setTooltip(tip);
        b.setTitle(b.getButtonText().upToFirstOccurrenceOf(".", false, false));
        b.setDescription(tip);
    };
    describe(addFilesButton, "Add .wav or .aiff stems. You can also drop files or a folder anywhere in the window.");
    describe(outputButton, "Choose where recovered files are written. Originals are never overwritten.");
    describe(processButton, "Analyze every stem and write a recovered copy of each one that pumps.");
    describe(clearButton, "Remove every stem from the list. Files on disk are not touched.");
    jobList.setTitle("Stems");
    jobList.setDescription("Each stem with its status: pending, analyzing, recovered, clean, or error.");

    addFilesButton.onClick = [this]
    {
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

    clearButton.onClick = [this]
    {
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
    g.setGradientFill(zqsfx::ui::gradients::chassis(getLocalBounds().toFloat()));
    g.fillAll();
}

// The empty-state hint is drawn over the panel, so it has to come after the children.
void MainComponent::paintOverChildren(juce::Graphics& g)
{
    if (!jobs.empty())
        return;
    g.setColour(zqsfx::ui::colour::silkCaption);
    g.setFont(juce::FontOptions(15.0f));
    g.drawText("Drop .wav / .aiff stems anywhere in this window", jobList.getBounds(), juce::Justification::centred);
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced(18, 12); // 18 px window padding (style guide section 7)
    auto header = area.removeFromTop(32);
    logo.setBounds(header.removeFromRight(36).withSizeKeepingCentre(32, 28)); // mark never under 24 px tall
    titleLabel.setBounds(header);
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
    stemsPanel.setBounds(area);
    jobList.setBounds(area.reduced(8, 0).withTrimmedTop(30).withTrimmedBottom(8)); // below the panel's title rule
}

bool MainComponent::isInterestedInFileDrag(const juce::StringArray&)
{
    return true;
}

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

int MainComponent::getNumRows()
{
    return static_cast<int>(jobs.size());
}

void MainComponent::paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool)
{
    if (row < 0 || row >= getNumRows())
        return;
    const auto& job = jobs[static_cast<size_t>(row)];

    namespace colour = zqsfx::ui::colour;
    namespace comp = zqsfx::ui::comp;

    if (row % 2 == 0)
        g.fillAll(juce::Colours::white.withAlpha(0.04f));

    // House colours. The state is never signalled by colour alone: the detail text beside the
    // dot always names it ("recovered: ...", "clean: ...", "error: ...").
    auto colourFor = [](JobState state)
    {
        switch (state)
        {
        case JobState::recovered:
            return comp::green; // colour-blind-safe channel
        case JobState::clean:
            return comp::sky; // colour-blind-safe channel
        case JobState::error:
            return colour::warn; // red is reserved for errors
        case JobState::analyzing:
            return colour::accent; // orange means "active"
        case JobState::pending:
            break;
        }
        return colour::ledOffRim;
    };

    auto bounds = juce::Rectangle<int>(0, 0, width, height).reduced(6, 4);
    // Shape backs up colour (orange "analyzing" and red "error" are close for colour-blind
    // users): error is a square, pending a hollow ring, everything else a filled dot.
    const auto marker = bounds.removeFromLeft(10).withSizeKeepingCentre(8, 8).toFloat();
    g.setColour(colourFor(job.state));
    if (job.state == JobState::error)
        g.fillRect(marker);
    else if (job.state == JobState::pending)
        g.drawEllipse(marker.reduced(0.75f), 1.5f);
    else
        g.fillEllipse(marker);
    bounds.removeFromLeft(8);

    g.setColour(colour::btnText);
    g.setFont(juce::FontOptions(14.0f));
    g.drawText(job.file.getFileName(), bounds.removeFromLeft(width / 3), juce::Justification::centredLeft, true);
    g.setColour(colour::silkLabel);
    g.drawText(job.detail, bounds, juce::Justification::centredLeft, true);
}

void MainComponent::addFiles(const juce::Array<juce::File>& files)
{
    for (const auto& file : files)
    {
        if (!file.existsAsFile())
            continue;
        const bool already = std::any_of(jobs.begin(), jobs.end(), [&](const Job& job) { return job.file == file; });
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
                         [this](const juce::FileChooser& fc)
                         {
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

void MainComponent::showAbout()
{
    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "About DePump",
                                           "DePump " + juce::JUCEApplication::getInstance()->getApplicationVersion() +
                                               "\n\nRemoves baked-in sidechain pumping from stems.\n\n"
                                               "ZQ SFX  -  https://www.zq-sfx.com  -  connect@zq-sfx.com\n"
                                               "Free software under GPL-3.0-or-later. Built with JUCE.\n"
                                               "Fonts: Barlow Condensed, VT323, IBM Plex Mono (SIL OFL).",
                                           "Close", this);
}

void MainComponent::populateForSnapshot()
{
    outputDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    outputLabel.setText("Output folder: ~/Music/DePump out", juce::dontSendNotification);
    jobs.clear();
    jobs.push_back({juce::File("/stems/drums_bus.wav"), JobState::recovered, "recovered: 4.1 dB of pumping removed"});
    jobs.push_back({juce::File("/stems/vocal_lead.wav"), JobState::clean, "clean: no pumping found, copied unchanged"});
    jobs.push_back({juce::File("/stems/bass_di.wav"), JobState::analyzing, "analyzing..."});
    jobs.push_back({juce::File("/stems/pad_wide.wav"), JobState::pending, "queued"});
    jobs.push_back({juce::File("/stems/fx_riser.aif"), JobState::error, "error: unsupported bit depth"});
    jobList.updateContent();
    updateButtonStates();
}
