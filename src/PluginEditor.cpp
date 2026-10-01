#include "PluginEditor.h"

#include <array>
#include <cmath>

#include <zqsfx_ui/zqsfx_ui.h>

namespace
{
namespace ui = zqsfx::ui;

// What the Learn status line is telling the user. Drives the marker shape and the text colour;
// the wording always names the state too, so colour is never the only carrier.
enum class StatusKind
{
    idle,     // hollow ring
    busy,     // filled circle, accent
    learned,  // filled circle, green
    nothing,  // filled circle, sky: ran fine, found no pump
    failed,   // filled square, warn
    cancelled // hollow square
};

juce::String formatValue(double value, int decimals, const juce::String& unit, bool showPlus = false)
{
    if (std::abs(value) < 0.5 * std::pow(10.0, -decimals)) // never "-0.0"
        value = 0.0;
    // juce::String(double, 0) does not mean "no decimals"; whole numbers are written as ints.
    const juce::String number = decimals == 0 ? juce::String(juce::roundToInt(value)) : juce::String(value, decimals);
    return (showPlus && value > 0.0 ? "+" : "") + number + " " + unit;
}

std::function<juce::String(double)> valueFormatter(int decimals, const juce::String& unit, bool showPlus = false)
{
    return [decimals, unit, showPlus](double v) { return formatValue(v, decimals, unit, showPlus); };
}

juce::StringArray choicesOf(juce::AudioProcessorValueTreeState& apvts, const char* id)
{
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(id)))
        return choice->choices;
    jassertfalse;
    return {};
}

//==============================================================================
// The house look, with one change: the knobs' value boxes are 25 px tall (a real hit target),
// and the module sizes their text from the box height, which would make it too big to fit
// "10.0 ms". Draw those readouts at a fixed LCD size instead.
class EditorLookAndFeel : public ui::LookAndFeel
{
public:
    void drawLabel(juce::Graphics& g, juce::Label& label) override
    {
        if (dynamic_cast<juce::Slider*>(label.getParentComponent()) != nullptr)
        {
            ui::LookAndFeel::drawScreen(g, label.getLocalBounds().toFloat(), false);
            if (!label.isBeingEdited())
                drawLcdText(g, label.getText(), label.getLocalBounds(), 17.0f, juce::Justification::centred);
            return;
        }
        ui::LookAndFeel::drawLabel(g, label);
    }
};

//==============================================================================
// The screen under the Learn button: a phosphor-glass readout with a marker, up to two lines of
// text and a thin progress bar while Learn is working. A Label so a screen reader reads its text.
class StatusScreen : public juce::Label
{
public:
    StatusScreen()
    {
        setTitle("Learn status");
        setDescription("Shows whether Learn is ready, listening, analysing, finished or failed.");
        setInterceptsMouseClicks(false, false);
        setWantsKeyboardFocus(false);
    }

    void setState(StatusKind newKind, const juce::String& text, float newProgress, bool newShowBar)
    {
        newProgress = juce::jlimit(0.0f, 1.0f, newProgress);
        if (newKind == kind && text == getText() && newShowBar == showBar && std::abs(newProgress - progress) < 0.004f)
            return;
        kind = newKind;
        progress = newProgress;
        showBar = newShowBar;
        setText(text, juce::dontSendNotification);
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        ui::LookAndFeel::drawScreen(g, bounds, false);

        const auto* house = dynamic_cast<ui::LookAndFeel*>(&getLookAndFeel());
        auto area = getLocalBounds().reduced(8, 5);

        // marker
        auto markerBox = area.removeFromLeft(14).withSizeKeepingCentre(10, 10).toFloat();
        switch (kind)
        {
            case StatusKind::idle:
                g.setColour(ui::colour::lcdDim);
                g.drawEllipse(markerBox.reduced(0.5f), 1.5f);
                break;
            case StatusKind::busy:
                g.setColour(ui::colour::accent);
                g.fillEllipse(markerBox);
                break;
            case StatusKind::learned:
                g.setColour(ui::comp::green);
                g.fillEllipse(markerBox);
                break;
            case StatusKind::nothing:
                g.setColour(ui::comp::sky);
                g.fillEllipse(markerBox);
                break;
            case StatusKind::failed:
                g.setColour(ui::colour::warn);
                g.fillRect(markerBox);
                break;
            case StatusKind::cancelled:
                g.setColour(ui::colour::lcdText);
                g.drawRect(markerBox.reduced(0.5f), 1.5f);
                break;
        }
        area.removeFromLeft(8);

        juce::Rectangle<int> barArea;
        if (showBar)
        {
            barArea = area.removeFromBottom(6);
            area.removeFromBottom(4);
        }

        g.setColour(textColour());
        g.setFont(house != nullptr ? house->lcdFont(17.0f) : juce::Font(juce::FontOptions(15.0f)));
        g.drawFittedText(getText(), area, juce::Justification::centredLeft, 3, 0.9f);

        if (showBar)
        {
            auto track = barArea.toFloat();
            g.setColour(ui::colour::lcdScreenDark);
            g.fillRect(track);
            g.setColour(ui::colour::lcdBorder);
            g.drawRect(track, 1.0f);
            g.setColour(ui::colour::accent);
            g.fillRect(track.reduced(1.0f).withWidth((track.getWidth() - 2.0f) * progress));
        }
    }

private:
    juce::Colour textColour() const
    {
        switch (kind)
        {
            case StatusKind::idle:    return ui::colour::lcdDim;
            case StatusKind::failed:  return ui::comp::yellow;
            default:                  return ui::colour::lcdText;
        }
    }

    StatusKind kind = StatusKind::idle;
    float progress = 0.0f;
    bool showBar = false;
};
} // namespace

//==============================================================================
// Everything the user sees, laid out at the design size. The editor scales it as one piece.
class DePumpAudioProcessorEditor::Content : public juce::Component
{
public:
    explicit Content(DePumpAudioProcessor& p)
        : processor(p),
          learnButton(p.apvts, ParamID::learn, "Learn",
                      "Listens to the next few seconds of audio, finds the pumping and sets the model controls "
                      "to match. Start playback first. While it is listening or analysing, this button "
                      "becomes Cancel.",
                      false, "Cancel"),
          modeCombo(p.apvts, ParamID::syncMode, choicesOf(p.apvts, ParamID::syncMode), "Mode",
                    "Sync follows the host tempo and the Rate note value. Free uses the Free Rate in Hz, "
                    "which hosts without a tempo need."),
          rateCombo(p.apvts, ParamID::rate, choicesOf(p.apvts, ParamID::rate), "Rate",
                    "Length of one pump cycle as a note value. Used in Sync mode."),
          freeRateKnob(p.apvts, ParamID::freeRate, "Free Rate",
                       "Length of one pump cycle in Hz. Used in Free mode. Learn sets it.", false, true, "Hz",
                       valueFormatter(2, "Hz")),
          depthKnob(p.apvts, ParamID::depth, "Depth",
                    "How deep the original pump dipped, in dB. The correction boosts by this much at the "
                    "bottom of each dip.",
                    false, true, "dB", valueFormatter(1, "dB")),
          phaseKnob(p.apvts, ParamID::phase, "Phase",
                    "Moves the correction earlier or later within the cycle, as a percent of one cycle.", false,
                    true, "%", valueFormatter(1, "%", true)),
          attackKnob(p.apvts, ParamID::attack, "Attack", "How fast the original dip fell, in milliseconds.",
                     false, true, "ms", valueFormatter(1, "ms")),
          holdKnob(p.apvts, ParamID::hold, "Hold",
                   "How long the original dip stayed at its bottom, in milliseconds.", false, true, "ms",
                   valueFormatter(0, "ms")),
          releaseKnob(p.apvts, ParamID::release, "Release",
                      "How fast the original dip recovered, in milliseconds.", false, true, "ms",
                      valueFormatter(0, "ms")),
          amountKnob(p.apvts, ParamID::amount, "Amount",
                     "How much of the correction is applied, 0 to 100 percent. This is not a dry/wet mix.", false,
                     true, "%", valueFormatter(0, "%")),
          outputKnob(p.apvts, ParamID::output, "Output",
                     "Output trim in dB. The correction adds gain, so you may need to turn this down.", false,
                     true, "dB", valueFormatter(1, "dB", true))
    {
        namespace colour = ui::colour;

        addAndMakeVisible(learnPanel);
        addAndMakeVisible(outputPanel);
        addAndMakeVisible(modelPanel);

        titleLabel.setText("DePump", juce::dontSendNotification);
        titleLabel.setFont(juce::FontOptions(24.0f, juce::Font::bold));
        titleLabel.setColour(juce::Label::textColourId, colour::silkTitle);
        titleLabel.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(titleLabel);

        captionLabel.setText("Removes baked-in sidechain pumping from a stem", juce::dontSendNotification);
        captionLabel.setFont(juce::FontOptions(13.0f));
        captionLabel.setColour(juce::Label::textColourId, colour::silkCaption);
        captionLabel.setJustificationType(juce::Justification::centredLeft);
        captionLabel.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(captionLabel);

        logo.onClick = [this] { showAbout(); };
        addAndMakeVisible(logo);

        // The attachment turns the button into the Learn parameter. While Learn is busy the same
        // press is a Cancel: the engine abandons the work, nothing is applied, the parameter is
        // put back to 0 and the status line says so. When Learn is idle this is a no-op.
        learnButton.button.onClick = [this] { processor.cancelLearn(); };
        addAndMakeVisible(learnButton);
        addAndMakeVisible(status);

        for (auto* combo : {&modeCombo, &rateCombo})
            addAndMakeVisible(*combo);

        for (auto* knob : allKnobs())
        {
            // A rotary slider does not take keyboard focus by default; without this the focus
            // ring never reaches the knobs and they cannot be set from the keyboard.
            knob->slider.setWantsKeyboardFocus(true);
            // The value box is an editable control (click to type a value), so it gets a real hit
            // target: the module's default is 16 px tall, under the 22 px floor, and the editor can
            // be scaled down to 0.9x, so draw it 25 px tall.
            knob->slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 68, kValueBoxHeight);
            addAndMakeVisible(*knob);
        }

        refresh();
    }

    // Called by the editor's timer on the message thread.
    void refresh()
    {
        const auto& engine = processor.getLearnEngine();
        const auto engineStatus = engine.getStatus();
        const float progress = engine.getProgress();

        // Learn button: Cancel while there is something to cancel.
        const bool busy = engineStatus == PluginLearnEngine::Status::capturing
                          || engineStatus == PluginLearnEngine::Status::analyzing;
        if (static_cast<int>(busy) != lastBusy)
        {
            lastBusy = static_cast<int>(busy);
            learnButton.button.setTitle(busy ? "Cancel Learn" : "Learn");
        }

        switch (engineStatus)
        {
            case PluginLearnEngine::Status::idle:
                status.setState(StatusKind::idle,
                                "Ready. Start playback, press Learn, and keep the audio playing for about "
                                    + juce::String(engine.getCaptureSeconds(), 0) + " seconds.",
                                0.0f, false);
                break;

            case PluginLearnEngine::Status::capturing:
            {
                const double heard = static_cast<double>(progress) * engine.getCaptureSeconds();
                const juce::String text = progress <= 0.0f
                                              ? juce::String("Listening: waiting for audio. Start playback.")
                                              : "Listening: " + juce::String(heard, 1) + " of "
                                                    + juce::String(engine.getCaptureSeconds(), 1) + " seconds.";
                status.setState(StatusKind::busy, text, progress, true);
                break;
            }

            case PluginLearnEngine::Status::analyzing:
                status.setState(StatusKind::busy,
                                "Analysing the pump: " + juce::String(juce::roundToInt(progress * 100.0f)) + " percent.",
                                progress, true);
                break;

            case PluginLearnEngine::Status::applied:
                status.setState(StatusKind::learned,
                                "Learned. " + engine.getStatusMessage().fromFirstOccurrenceOf("learned: ", false, false),
                                1.0f, false);
                break;

            case PluginLearnEngine::Status::noPumpDetected:
                status.setState(StatusKind::nothing, "Nothing learned: " + engine.getStatusMessage() + ".", 1.0f, false);
                break;

            case PluginLearnEngine::Status::cancelled:
                status.setState(StatusKind::cancelled, "Cancelled. Nothing was changed. Press Learn to start again.",
                                0.0f, false);
                break;

            case PluginLearnEngine::Status::error:
                status.setState(StatusKind::failed,
                                "Learn failed: " + engine.getStatusMessage().fromFirstOccurrenceOf("error: ", false, false),
                                1.0f, false);
                break;
        }

        // Sync uses the note value, Free uses the Hz knob: grey out whichever one is ignored.
        const bool syncMode = static_cast<int>(processor.apvts.getRawParameterValue(ParamID::syncMode)->load()) == 0;
        if (static_cast<int>(syncMode) != lastSyncMode)
        {
            lastSyncMode = static_cast<int>(syncMode);
            rateCombo.setActive(syncMode);
            freeRateKnob.setActive(!syncMode);
        }
    }

    juce::String getStatusText() const { return status.getText(); }
    juce::LookAndFeel* getHouseLookAndFeel() { return &houseLookAndFeel.get(); }

    void resized() override
    {
        constexpr int margin = 12;
        constexpr int knobW = 72, knobH = 96;

        auto area = getLocalBounds().reduced(margin, 8);

        auto header = area.removeFromTop(34);
        logo.setBounds(header.removeFromRight(40).withSizeKeepingCentre(36, 30)); // mark never under 24 px tall
        titleLabel.setBounds(header.removeFromLeft(112));
        captionLabel.setBounds(header);
        area.removeFromTop(6);

        // Row 1: Learn (left) and Output (right).
        auto row1 = area.removeFromTop(136);
        auto learnArea = row1.removeFromLeft(404);
        row1.removeFromLeft(10);
        auto outputArea = row1;
        learnPanel.setBounds(learnArea);
        outputPanel.setBounds(outputArea);

        auto learnInner = learnArea.reduced(16, 0);
        learnInner.removeFromTop(34);
        learnButton.setBounds(learnInner.removeFromTop(34).removeFromLeft(150));
        learnInner.removeFromTop(8);
        status.setBounds(learnInner.removeFromTop(52));

        auto outputInner = outputArea.withTrimmedTop(34);
        const int outputPad = (outputInner.getWidth() - 2 * knobW - 16) / 2;
        outputInner = outputInner.withTrimmedLeft(outputPad);
        amountKnob.setBounds(outputInner.removeFromLeft(knobW).withHeight(knobH));
        outputInner.removeFromLeft(16);
        outputKnob.setBounds(outputInner.removeFromLeft(knobW).withHeight(knobH));

        area.removeFromTop(10);

        // Row 2: the pump model.
        auto row2 = area.removeFromTop(138);
        modelPanel.setBounds(row2);
        auto modelInner = row2.withTrimmedTop(34).withTrimmedLeft(16);
        auto combos = modelInner.removeFromLeft(112);
        modeCombo.setBounds(combos.removeFromTop(41));
        combos.removeFromTop(14);
        rateCombo.setBounds(combos.removeFromTop(41));
        modelInner.removeFromLeft(22);

        for (auto* knob : {&freeRateKnob, &depthKnob, &phaseKnob, &attackKnob, &holdKnob, &releaseKnob})
        {
            knob->setBounds(modelInner.removeFromLeft(knobW).withHeight(knobH));
            modelInner.removeFromLeft(4);
        }
    }

private:
    std::array<ui::Knob*, 8> allKnobs()
    {
        return {&freeRateKnob, &depthKnob, &phaseKnob, &attackKnob, &holdKnob, &releaseKnob, &amountKnob, &outputKnob};
    }

    void showAbout()
    {
        juce::String version;
#ifdef JucePlugin_VersionString
        version = juce::String(" ") + JucePlugin_VersionString;
#endif
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "About DePump",
                                               "DePump" + version
                                                   + "\n\nRemoves baked-in sidechain pumping from stems.\n\n"
                                                     "ZQ SFX  -  https://www.zq-sfx.com  -  connect@zq-sfx.com\n"
                                                     "Free software under GPL-3.0-or-later. Built with JUCE.\n"
                                                     "Fonts: Barlow Condensed, VT323, IBM Plex Mono (SIL OFL).",
                                               "Close", this);
    }

    DePumpAudioProcessor& processor;

    // One shared instance for every open editor in this process, kept alive by the editors
    // that use it. Never the process-wide default look and feel.
    juce::SharedResourcePointer<EditorLookAndFeel> houseLookAndFeel;

    ui::Panel learnPanel{"Learn"};
    ui::Panel outputPanel{"Output"};
    ui::Panel modelPanel{"Pump model"};
    ui::LogoMark logo{"DePump"};
    juce::Label titleLabel, captionLabel;

    ui::TextToggle learnButton;
    StatusScreen status;

    ui::Combo modeCombo, rateCombo;
    ui::Knob freeRateKnob, depthKnob, phaseKnob, attackKnob, holdKnob, releaseKnob;
    ui::Knob amountKnob, outputKnob;

    static constexpr int kValueBoxHeight = 25;
    int lastBusy = -1;
    int lastSyncMode = -1; // -1 until the first refresh() has applied the Sync/Free dimming
    // Declared last so it goes first: nothing above can still be showing a tooltip.
    juce::TooltipWindow tooltips{this, 500};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Content)
};

//==============================================================================
DePumpAudioProcessorEditor::DePumpAudioProcessorEditor(DePumpAudioProcessor& processorIn)
    : juce::AudioProcessorEditor(processorIn), processor(processorIn)
{
    setTitle("DePump");
    setDescription("DePump plugin window");

    content = std::make_unique<Content>(processor);
    // The house look is installed on this editor and its children only (never as the
    // process-wide default: a host runs other plugins and other DePump instances in the same
    // process).
    setLookAndFeel(content->getHouseLookAndFeel());
    addAndMakeVisible(*content);

    setResizable(true, true);
    setResizeLimits(juce::roundToInt(kDesignWidth * kMinScale), juce::roundToInt(kDesignHeight * kMinScale),
                    juce::roundToInt(kDesignWidth * kMaxScale), juce::roundToInt(kDesignHeight * kMaxScale));
    getConstrainer()->setFixedAspectRatio(static_cast<double>(kDesignWidth) / static_cast<double>(kDesignHeight));

    // Reopen at the size the session was saved with (clamped to the limits; the height follows
    // the width so the aspect ratio holds). Nothing is written back until this is done.
    int startWidth = kDesignWidth;
    if (processor.getEditorWidth() > 0)
        startWidth = juce::jlimit(juce::roundToInt(kDesignWidth * kMinScale), juce::roundToInt(kDesignWidth * kMaxScale),
                                  processor.getEditorWidth());
    setSize(startWidth, juce::roundToInt(static_cast<double>(startWidth) * kDesignHeight / kDesignWidth));
    persistSize = true;
    processor.setEditorSize(getWidth(), getHeight()); // the window now is what the session will save

    startTimerHz(15);
}

DePumpAudioProcessorEditor::~DePumpAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
    content.reset();
}

void DePumpAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.setGradientFill(ui::gradients::chassis(getLocalBounds().toFloat()));
    g.fillAll();
}

void DePumpAudioProcessorEditor::resized()
{
    const float scale = juce::jmin(static_cast<float>(getWidth()) / static_cast<float>(kDesignWidth),
                                   static_cast<float>(getHeight()) / static_cast<float>(kDesignHeight));
    content->setTransform(juce::AffineTransform::scale(scale));
    content->setBounds(0, 0, kDesignWidth, kDesignHeight);

    if (persistSize && getWidth() > 0 && getHeight() > 0)
        processor.setEditorSize(getWidth(), getHeight());
}

void DePumpAudioProcessorEditor::timerCallback()
{
    content->refresh();
}

juce::String DePumpAudioProcessorEditor::getStatusTextForTest() const
{
    return content->getStatusText();
}
