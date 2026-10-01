# DePump

DePump removes baked-in sidechain pumping from audio stems. It estimates the
periodic gain envelope a compressor applied to a track and inverts it, fully
automatically, with no tempo or beat input needed. It is aimed at producers
and engineers cleaning up AI-separated or client-supplied stems that arrive
with audible pumping.

Three things ship: DePump.app, a standalone batch app and the main product;
depump_render, a command-line renderer; and a VST3/AU plugin. The plugin
has a Learn button: press it during playback to capture roughly 3 seconds
of audio, analyze it off-thread, and if a pump is detected, fit and apply
the model to freeRate, depth, attack, hold, release, and phase with no added
latency. If no pump is detected, the plugin remains a pass-through. The
plugin window shows the Learn status as it goes (listening, analysing with a
progress bar, learned, or the reason it failed). Press the button again while it
is listening or analysing to cancel: nothing is applied and the status line says
cancelled. Learn is not automatable, so host automation cannot start it.

## Install

Download the latest build from the
[Releases page](https://github.com/themightyzq/DePump/releases/latest). Each
platform has the plugin (VST3; on macOS also AU and a Standalone) and the
DePump desktop app. On macOS, copy the `.vst3` to
`~/Library/Audio/Plug-Ins/VST3/` and the `.component` to
`~/Library/Audio/Plug-Ins/Components/`. The macOS builds are universal
(Apple Silicon and Intel).

The builds are unsigned, so on first launch macOS will block the apps:
right-click (or Control-click) and choose Open, then confirm, to run them
the first time. Some hosts need plugins signed locally.

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

## Plugin behaviour

- With Amount at 0 and Output at 0 dB, audio below -1 dBFS passes through
  unchanged, bit for bit. A safety limiter only acts above -1 dBFS and keeps
  the output under -0.3 dBFS.
- When the host transport is stopped and keeps reporting the same position,
  the correction runs on its own clock instead of restarting every block. A
  host that reports no position is treated the same way.
- The plugin window scales uniformly from 90 to 150 percent of its default
  size, and reopens at the size it was saved with (the size is stored in the
  plugin state, not as a parameter).

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

The window and app use the ZQ SFX house UI. It embeds three typefaces under
the SIL Open Font License 1.1: Barlow Condensed, VT323 and IBM Plex Mono. The
licence texts are in the licenses folder and inside each built macOS bundle,
under Contents/Resources/licenses. The knob artwork is CC0 (KnobGallery,
by SolurOathLabs, dh96 and C. Anders).

ZQ SFX, https://www.zq-sfx.com, connect@zq-sfx.com.
