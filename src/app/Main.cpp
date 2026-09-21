#include "MainComponent.h"

class DePumpApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "DePump"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise(const juce::String&) override { mainWindow = std::make_unique<MainWindow>(getApplicationName()); }

    void shutdown() override { mainWindow = nullptr; }

private:
    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow(const juce::String& name) : DocumentWindow(name, zqsfx::ui::colour::chassisMid, allButtons)
        {
            setUsingNativeTitleBar(true);
            setContentOwned(new MainComponent(), true);
            setResizable(true, true);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);
        }

        void closeButtonPressed() override { JUCEApplication::getInstance()->systemRequestedQuit(); }
    };

    // declared first so it outlives the window: the house LookAndFeel is the JUCE default
    MainComponent::ScopedHouseLookAndFeel houseLookAndFeel;
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(DePumpApplication)
