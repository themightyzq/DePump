// depump_ui_snapshot: render the app window's content headlessly to a PNG.
//
//   depump_ui_snapshot <out.png> [scale]
//
// The look-and-feel regression gate: render before a UI change, render after, compare. It
// fills the job list with one row per status so every state colour is in the picture.

#include "app/MainComponent.h"

#include <iostream>

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::cerr << "usage: depump_ui_snapshot <out.png> [scale]\n";
        return 2;
    }

    juce::ScopedJuceInitialiser_GUI gui;
    const auto out = juce::File::getCurrentWorkingDirectory().getChildFile(juce::String(argv[1]));
    const float scale = argc > 2 ? juce::String(argv[2]).getFloatValue() : 2.0f;

    int result = 0;
    {
        MainComponent::ScopedHouseLookAndFeel look;
        MainComponent content;
        content.populateForSnapshot();

        const auto image = content.createComponentSnapshot(content.getLocalBounds(), true, scale);
        out.getParentDirectory().createDirectory();
        out.deleteFile();
        juce::FileOutputStream stream(out);
        juce::PNGImageFormat png;
        if (!stream.openedOk() || !png.writeImageToStream(image, stream))
        {
            std::cerr << "could not write " << out.getFullPathName() << "\n";
            result = 1;
        }
        else
        {
            std::cout << out.getFullPathName() << "  " << image.getWidth() << "x" << image.getHeight() << "\n";
        }
    }
    return result;
}
