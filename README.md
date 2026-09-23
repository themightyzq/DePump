# DePump

DePump removes baked-in sidechain pumping from audio stems. It estimates the
periodic gain envelope a compressor applied to a track and inverts it, fully
automatically, with no tempo or beat input needed. It is aimed at producers
and engineers cleaning up AI-separated or client-supplied stems that arrive
with audible pumping.

Three things ship: DePump.app, a standalone batch app and the main product;
depump_render, a command-line renderer; and a VST3/AU plugin. The plugin
has a Learn parameter: press it during playback to capture roughly 3 seconds
of audio, analyze it off-thread, and if a pump is detected, fit and apply
the model to freeRate, depth, attack, hold, release, and phase with no added
latency. If no pump is detected, the plugin remains a pass-through.

## Install

There are no packaged releases yet; build from source (below). The built
app and plugin are unsigned, so on first launch macOS will block them:
right-click (or Control-click) and choose Open, then confirm, to run them
the first time.

Requires macOS 11.0 or later.

## Use

Open DePump.app, drag in stems or a folder of stems, choose an output
folder, and start the batch. Each file gets a status readout as it
processes, and the output is written without touching your originals.

For scripting or batch jobs from the command line, depump_render does the
same recovery headlessly:

```
depump_render --batch IN_DIR --out-dir OUT_DIR
```

## Build from source

Requirements: CMake 3.25+, a C++20 compiler (Xcode command-line tools),
macOS only, Universal Binary (arm64 + x86_64). JUCE 8.0.14 and Catch2 are
fetched automatically by CMake; no manual install needed.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build --config Release -j"$(sysctl -n hw.ncpu)"
```

The first configure fetches JUCE and Catch2 and is slow. The app, plugin,
and CLI land under `build/{DePump,DePumpApp,depump_render}_artefacts/Release/`.

Run the unit tests with:

```bash
ctest --test-dir build --output-on-failure
```

The built plugin is ad-hoc signed by the build, which is enough for most
hosts on your own machine. If a host refuses it, re-sign it yourself:

```bash
codesign --force --deep -s - build/DePump_artefacts/Release/VST3/DePump.vst3
```

Soundminer scans the system-level plugin folder, which needs sudo:

```bash
sudo cp -R ~/Library/Audio/Plug-Ins/VST3/DePump.vst3 /Library/Audio/Plug-Ins/VST3/
```

## Licence

GPL-3.0-or-later. See LICENSE. Built with JUCE.

ZQ SFX, https://www.zq-sfx.com, connect@zq-sfx.com.
