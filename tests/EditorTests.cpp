#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <functional>
#include <memory>

#include <zqsfx_ui/zqsfx_ui.h>

#include "PluginEditor.h"
#include "dsp/GainCurve.h"
#include "PluginProcessor.h"

namespace
{
namespace ui = zqsfx::ui;

// WCAG 2.x contrast ratio between two opaque colours.
double luminance(juce::Colour c)
{
    auto lin = [](float v) { return v <= 0.03928f ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
    return 0.2126 * lin(c.getFloatRed()) + 0.7152 * lin(c.getFloatGreen()) + 0.0722 * lin(c.getFloatBlue());
}

double contrast(juce::Colour a, juce::Colour b)
{
    const double la = luminance(a), lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

void forEachDescendant(juce::Component& root, const std::function<void(juce::Component&)>& fn)
{
    for (auto* child : root.getChildren())
    {
        fn(*child);
        forEachDescendant(*child, fn);
    }
}

struct TestPlayHead : public juce::AudioPlayHead
{
    int64_t sample = 0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setTimeInSamples(sample);
        return info;
    }
};

void pumpMessages(int ms)
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil(ms);
}

std::vector<float> makePumpedChord(double sr, double seconds, bool pumped)
{
    std::vector<float> samples(static_cast<size_t>(sr * seconds));
    for (size_t i = 0; i < samples.size(); ++i)
    {
        double v = 0.0;
        for (double f : {220.0, 277.18, 329.63})
            v += 0.2 * std::sin(2.0 * 3.14159265358979 * f * static_cast<double>(i) / sr);
        samples[i] = static_cast<float>(v);
    }
    if (pumped)
    {
        const depump::PumpProfile profile{4.0f, 9.0f, 10.0f, 60.0f, 150.0f, 0.25f};
        const auto gain = depump::synthesizeGainCurve(profile, sr, samples.size());
        depump::applyGain(samples, gain);
    }
    return samples;
}
} // namespace

TEST_CASE("Editor: every control is named, described, focusable and at least 22 px")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    DePumpAudioProcessor proc;
    std::unique_ptr<juce::AudioProcessorEditor> editor(proc.createEditor());
    REQUIRE(editor != nullptr);

    for (float scale : {0.9f, 1.0f, 1.5f})
    {
        const int w = juce::roundToInt(DePumpAudioProcessorEditor::kDesignWidth * scale);
        const int h = juce::roundToInt(DePumpAudioProcessorEditor::kDesignHeight * scale);
        editor->setSize(w, h);

        int controls = 0;
        forEachDescendant(*editor, [&](juce::Component& c) {
            const bool isControl = dynamic_cast<juce::Slider*>(&c) != nullptr || dynamic_cast<juce::ComboBox*>(&c) != nullptr ||
                                   dynamic_cast<juce::Button*>(&c) != nullptr;
            if (!isControl)
                return;
            ++controls;
            INFO("scale " << scale << " control '" << c.getTitle() << "'");
            CHECK(c.getTitle().isNotEmpty());
            CHECK(c.getDescription().isNotEmpty());
            CHECK(c.getWantsKeyboardFocus());
            if (auto* tip = dynamic_cast<juce::TooltipClient*>(&c))
                CHECK(tip->getTooltip().isNotEmpty());
            else
                FAIL("control has no tooltip client");

            // Size on screen: the component's own size times the uniform scale of the content.
            const auto onScreen = editor->getLocalArea(&c, c.getLocalBounds());
            CHECK(onScreen.getWidth() >= 22);
            CHECK(onScreen.getHeight() >= 22);
        });
        CHECK(controls >= 12); // 8 knobs + 2 combos + Learn + the About mark

        // The knobs' value boxes are editable (click and type), so they are controls too.
        int valueBoxes = 0;
        forEachDescendant(*editor, [&](juce::Component& c) {
            auto* label = dynamic_cast<juce::Label*>(&c);
            if (label == nullptr || dynamic_cast<juce::Slider*>(label->getParentComponent()) == nullptr)
                return;
            ++valueBoxes; // includes the dimmed Free Rate box: it becomes editable again in Free mode
            const auto onScreen = editor->getLocalArea(label, label->getLocalBounds());
            INFO("scale " << scale << " value box of '" << label->getParentComponent()->getTitle() << "' is "
                          << onScreen.getWidth() << " x " << onScreen.getHeight());
            CHECK(onScreen.getWidth() >= 22);
            CHECK(onScreen.getHeight() >= 22);
        });
        CHECK(valueBoxes == 8);
    }
}

TEST_CASE("Editor: knob readouts show real units, whole numbers where the parameter is whole")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    DePumpAudioProcessor proc;
    auto setReal = [&](const char* id, float value) {
        auto* p = proc.apvts.getParameter(id);
        REQUIRE(p != nullptr);
        p->setValueNotifyingHost(p->convertTo0to1(value));
    };
    setReal(ParamID::release, 141.779f);
    setReal(ParamID::hold, 59.6f);
    setReal(ParamID::depth, 9.0f);
    setReal(ParamID::output, -3.0f);
    setReal(ParamID::phase, 12.5f);

    std::unique_ptr<juce::AudioProcessorEditor> editor(proc.createEditor());
    auto readout = [&](const juce::String& title) {
        juce::String text;
        forEachDescendant(*editor, [&](juce::Component& c) {
            if (auto* slider = dynamic_cast<juce::Slider*>(&c))
                if (slider->getTitle() == title)
                    text = slider->getTextFromValue(slider->getValue());
        });
        return text;
    };
    CHECK(readout("Release") == "142 ms");
    CHECK(readout("Hold") == "60 ms");
    CHECK(readout("Depth") == "9.0 dB");
    CHECK(readout("Output") == "-3.0 dB");
    CHECK(readout("Phase") == "+12.5 %");
}

TEST_CASE("Editor: the widest knob readouts fit inside their value boxes")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    ui::LookAndFeel look;
    const auto font = look.lcdFont(17.0f);
    // Widest text each readout can show at its parameter's range ends. The box is 68 px wide;
    // leave 6 px for the screen border.
    for (auto text : {"-24.0 dB", "+12.0 dB", "24.0 dB", "8.00 Hz", "+50.0 %", "-50.0 %", "100.0 ms", "500 ms", "400 ms", "100 %"})
    {
        INFO(text);
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText(font, text, 0.0f, 0.0f);
        CHECK(glyphs.getBoundingBox(0, -1, true).getWidth() <= 62.0f);
    }
}

TEST_CASE("Editor: the Free Rate knob is dimmed in Sync mode and the Rate box in Free mode")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    DePumpAudioProcessor proc;
    std::unique_ptr<juce::AudioProcessorEditor> editor(proc.createEditor());

    auto alphaOf = [&](const juce::String& title) {
        float alpha = -1.0f;
        forEachDescendant(*editor, [&](juce::Component& c) {
            if (c.getTitle() == title && (dynamic_cast<juce::Slider*>(&c) != nullptr || dynamic_cast<juce::ComboBox*>(&c) != nullptr))
                alpha = c.getAlpha();
        });
        return alpha;
    };

    pumpMessages(120); // default is Sync
    CHECK(alphaOf("Free Rate") < 0.5f);
    CHECK(alphaOf("Rate") == 1.0f);

    auto* mode = proc.apvts.getParameter(ParamID::syncMode);
    mode->setValueNotifyingHost(1.0f); // Free
    pumpMessages(150);
    CHECK(alphaOf("Free Rate") == 1.0f);
    CHECK(alphaOf("Rate") < 0.5f);
}

TEST_CASE("Editor: sizes uniformly and keeps all content inside the window")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    DePumpAudioProcessor proc;
    std::unique_ptr<juce::AudioProcessorEditor> editor(proc.createEditor());
    REQUIRE(editor != nullptr);

    CHECK(editor->getWidth() == DePumpAudioProcessorEditor::kDesignWidth);
    CHECK(editor->getHeight() == DePumpAudioProcessorEditor::kDesignHeight);
    CHECK(editor->isResizable());
    REQUIRE(editor->getConstrainer() != nullptr);
    CHECK(editor->getConstrainer()->getMinimumWidth() < DePumpAudioProcessorEditor::kDesignWidth);
    CHECK(editor->getConstrainer()->getMaximumWidth() > DePumpAudioProcessorEditor::kDesignWidth);

    for (float scale : {0.9f, 1.5f})
    {
        editor->setSize(juce::roundToInt(DePumpAudioProcessorEditor::kDesignWidth * scale),
                        juce::roundToInt(DePumpAudioProcessorEditor::kDesignHeight * scale));
        forEachDescendant(*editor, [&](juce::Component& c) {
            if (c.getWidth() == 0 || c.getHeight() == 0 || !c.isVisible())
                return;
            const auto inEditor = editor->getLocalArea(&c, c.getLocalBounds());
            INFO("scale " << scale << " component '" << c.getTitle() << "' " << inEditor.toString());
            CHECK(editor->getLocalBounds().expanded(1).contains(inEditor));
        });
    }
}

TEST_CASE("Editor: the status line follows Learn from ready to learned, and reports a clean pass")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    constexpr double sr = 48000.0;
    constexpr int blockSize = 512;

    DePumpAudioProcessor proc;
    TestPlayHead playHead;
    proc.setPlayHead(&playHead);
    proc.setPlayConfigDetails(1, 1, sr, blockSize);
    proc.prepareToPlay(sr, blockSize);

    auto* editor = dynamic_cast<DePumpAudioProcessorEditor*>(proc.createEditor());
    REQUIRE(editor != nullptr);
    std::unique_ptr<juce::AudioProcessorEditor> owner(editor);

    pumpMessages(150);
    CHECK(editor->getStatusTextForTest().startsWith("Ready"));

    auto feed = [&](const std::vector<float>& signal, size_t from, size_t to) {
        juce::MidiBuffer midi;
        for (size_t pos = from; pos < to; pos += (size_t) blockSize)
        {
            const int n = (int) std::min<size_t>((size_t) blockSize, to - pos);
            juce::AudioBuffer<float> buffer(1, n);
            for (int i = 0; i < n; ++i)
                buffer.setSample(0, i, signal[pos + (size_t) i]);
            proc.processBlock(buffer, midi);
            playHead.sample += n;
        }
    };

    for (bool pumped : {true, false})
    {
        const auto signal = makePumpedChord(sr, 6.0, pumped);
        playHead.sample = 0;
        // A host keeps running blocks, so the processor sees Learn fall back to 0 before the
        // next press; step one block here so the second press is a fresh 0 -> 1 edge.
        feed(std::vector<float>((size_t) blockSize, 0.0f), 0, (size_t) blockSize);
        playHead.sample = 0;
        proc.apvts.getParameter(ParamID::learn)->setValueNotifyingHost(1.0f);

        feed(signal, 0, (size_t) (1.5 * sr));
        pumpMessages(150);
        const auto listening = editor->getStatusTextForTest();
        INFO("while capturing: " << listening);
        CHECK(listening.startsWith("Listening"));

        feed(signal, (size_t) (1.5 * sr), signal.size());
        const auto deadline = juce::Time::getMillisecondCounter() + 30000u;
        auto terminal = [&] {
            const auto t = editor->getStatusTextForTest();
            return t.startsWith("Learned") || t.startsWith("Nothing learned") || t.startsWith("Learn failed");
        };
        while (!terminal() && juce::Time::getMillisecondCounter() < deadline)
            pumpMessages(30);

        const auto finished = editor->getStatusTextForTest();
        INFO("after analysis (pumped=" << (int) pumped << "): " << finished);
        CHECK(finished.startsWith(pumped ? "Learned" : "Nothing learned"));
    }
}

TEST_CASE("Editor: closing it in the middle of a Learn is safe")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    constexpr double sr = 48000.0;
    constexpr int blockSize = 512;

    DePumpAudioProcessor proc;
    TestPlayHead playHead;
    proc.setPlayHead(&playHead);
    proc.setPlayConfigDetails(1, 1, sr, blockSize);
    proc.prepareToPlay(sr, blockSize);

    {
        std::unique_ptr<juce::AudioProcessorEditor> editor(proc.createEditor());
        const auto signal = makePumpedChord(sr, 4.0, true);
        proc.apvts.getParameter(ParamID::learn)->setValueNotifyingHost(1.0f);
        juce::MidiBuffer midi;
        for (size_t pos = 0; pos + (size_t) blockSize <= signal.size(); pos += (size_t) blockSize)
        {
            juce::AudioBuffer<float> buffer(1, blockSize);
            for (int i = 0; i < blockSize; ++i)
                buffer.setSample(0, i, signal[pos + (size_t) i]);
            proc.processBlock(buffer, midi);
            playHead.sample += blockSize;
        }
        pumpMessages(100);
    } // editor destroyed while the analysis thread may still be running
    pumpMessages(300);
    SUCCEED();
}

TEST_CASE("Editor: the colours it draws text and markers with meet WCAG AA on their backgrounds")
{
    // The status screen: text on the LCD glass.
    for (auto text : {ui::colour::lcdDim, ui::colour::lcdText, ui::comp::yellow})
        CHECK(contrast(text, ui::colour::lcdBg) >= 4.5);
    // Title and caption on the chassis.
    for (auto text : {ui::colour::silkTitle, ui::colour::silkCaption})
        CHECK(contrast(text, ui::colour::chassisMid) >= 4.5);
    // Markers on the glass need 3:1 (non-text).
    for (auto marker : {ui::colour::accent, ui::comp::sky, ui::comp::green, ui::colour::lcdDim})
        CHECK(contrast(marker, ui::colour::lcdBg) >= 3.0);
}

TEST_CASE("Editor snapshot (only when DEPUMP_EDITOR_SNAPSHOT names a PNG to write)")
{
    const auto path = juce::SystemStats::getEnvironmentVariable("DEPUMP_EDITOR_SNAPSHOT", {});
    if (path.isEmpty())
        SUCCEED("skipped");
    else
    {
        juce::ScopedJuceInitialiser_GUI juceInit;
        DePumpAudioProcessor proc;
        std::unique_ptr<juce::AudioProcessorEditor> editor(proc.createEditor());
        pumpMessages(100);
        const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 2.0f);
        juce::File out(path);
        out.deleteFile();
        juce::FileOutputStream stream(out);
        juce::PNGImageFormat png;
        REQUIRE(stream.openedOk());
        CHECK(png.writeImageToStream(image, stream));
    }
}

TEST_CASE("Editor: the window size is restored from the saved state and clamped to the limits")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    // The editor writes its size back to the processor whenever it is resized.
    {
        DePumpAudioProcessor proc;
        std::unique_ptr<juce::AudioProcessorEditor> editor(proc.createEditor());
        CHECK(proc.getEditorWidth() == DePumpAudioProcessorEditor::kDesignWidth);
        editor->setSize(800, 425);
        CHECK(proc.getEditorWidth() == 800);
        CHECK(proc.getEditorHeight() == 425);
    }

    // A fresh instance restored from that state reopens at that size.
    DePumpAudioProcessor saved;
    saved.setEditorSize(800, 425);
    juce::MemoryBlock state;
    saved.getStateInformation(state);

    DePumpAudioProcessor restored;
    restored.setStateInformation(state.getData(), (int) state.getSize());
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor(restored.createEditor());
        CHECK(editor->getWidth() == 800);
        CHECK(editor->getHeight() == 425);
    }
    CHECK(restored.getEditorWidth() == 800); // opening did not reset what was saved

    // Absurd saved sizes are clamped; the aspect ratio follows the width.
    DePumpAudioProcessor huge;
    huge.setEditorSize(9000, 100);
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor(huge.createEditor());
        CHECK(editor->getWidth() == juce::roundToInt(DePumpAudioProcessorEditor::kDesignWidth * DePumpAudioProcessorEditor::kMaxScale));
        CHECK(editor->getHeight() == juce::roundToInt(editor->getWidth() * (double) DePumpAudioProcessorEditor::kDesignHeight / DePumpAudioProcessorEditor::kDesignWidth));
    }
    DePumpAudioProcessor tiny;
    tiny.setEditorSize(10, 10);
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor(tiny.createEditor());
        CHECK(editor->getWidth() == juce::roundToInt(DePumpAudioProcessorEditor::kDesignWidth * DePumpAudioProcessorEditor::kMinScale));
    }

    // Nothing saved: the default size.
    DePumpAudioProcessor fresh;
    std::unique_ptr<juce::AudioProcessorEditor> editor(fresh.createEditor());
    CHECK(editor->getWidth() == DePumpAudioProcessorEditor::kDesignWidth);
    CHECK(editor->getHeight() == DePumpAudioProcessorEditor::kDesignHeight);
}

TEST_CASE("Editor: the Learn button is Cancel while Learn is busy, and the status line says cancelled")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    constexpr double sr = 48000.0;
    constexpr int blockSize = 512;

    DePumpAudioProcessor proc;
    TestPlayHead playHead;
    proc.setPlayHead(&playHead);
    proc.setPlayConfigDetails(1, 1, sr, blockSize);
    proc.prepareToPlay(sr, blockSize);

    std::unique_ptr<juce::AudioProcessorEditor> owner(proc.createEditor());
    auto* editor = dynamic_cast<DePumpAudioProcessorEditor*>(owner.get());
    REQUIRE(editor != nullptr);

    juce::TextButton* learn = nullptr;
    forEachDescendant(*editor, [&](juce::Component& c) {
        if (auto* b = dynamic_cast<juce::TextButton*>(&c))
            learn = b;
    });
    REQUIRE(learn != nullptr);
    CHECK(learn->getButtonText() == "Learn");

    const auto signal = makePumpedChord(sr, 6.0, true);
    auto feed = [&](size_t from, size_t to) {
        juce::MidiBuffer midi;
        for (size_t pos = from; pos < to; pos += (size_t) blockSize)
        {
            const int n = (int) std::min<size_t>((size_t) blockSize, to - pos);
            juce::AudioBuffer<float> buffer(1, n);
            for (int i = 0; i < n; ++i)
                buffer.setSample(0, i, signal[pos + (size_t) i]);
            proc.processBlock(buffer, midi);
            playHead.sample += n;
        }
    };

    learn->triggerClick(); // press Learn (delivered asynchronously)
    pumpMessages(50);
    feed(0, (size_t) (1.5 * sr));
    pumpMessages(150);
    CHECK(editor->getStatusTextForTest().startsWith("Listening"));
    CHECK(learn->getButtonText() == "Cancel");
    CHECK(learn->getTitle() == "Cancel Learn");

    learn->triggerClick(); // press it again: Cancel
    pumpMessages(200);
    CHECK(editor->getStatusTextForTest().startsWith("Cancelled"));
    CHECK(learn->getButtonText() == "Learn");
    CHECK(learn->getTitle() == "Learn");
    CHECK(proc.apvts.getRawParameterValue(ParamID::learn)->load() < 0.5f);

    feed((size_t) (1.5 * sr), signal.size());
    pumpMessages(400);
    CHECK(editor->getStatusTextForTest().startsWith("Cancelled")); // no late result
    CHECK_FALSE(proc.isEngagedForTest());
}
