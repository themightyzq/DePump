# DePump

DePump removes baked-in sidechain pumping from audio stems by estimating the
periodic gain envelope a sidechain compressor applied to a track and
inverting it. It targets producers and engineers working with AI-separated
stems or client-supplied stems that arrive with audible pumping artifacts,
with the goal of producing pristine stems that load cleanly into a DAW.

Recovery is fully automatic: the analysis core finds the pump period and
fits a 5-parameter one-pole model with no tempo or beat input from the user
(`src/dsp/PumpAnalysis.cpp`).

## Status

The v1 engine (harness -> analysis -> recovery -> batch CLI -> GUI) is
complete and usable per `HANDOFF.md`: 18 Catch2 test cases pass, pluginval
strictness 10 succeeds on VST3 + AU, and the recovery-quality regression
(`tools/render/matrix_sweep.sh`, 81 synthetic fixtures) meets the
owner-approved bar. See `HANDOFF.md` for the current "what's next" list —
real-world validation on actual stems is the open item.

## What's here — three deliverables (CMakeLists.txt)

1. **`DePump` plugin** — VST3 + AU + Standalone (`FORMATS VST3 AU
   Standalone`), bundle id `com.zqsfx.depump`. Companion product on the
   same DSP core; currently a thin APVTS pass-through that does not yet
   call the analysis core.
2. **`DePumpApp`** — the primary product: a standalone macOS GUI app
   (`juce_add_gui_app`), bundle id `com.zqsfx.depump.app`. Drag-drop stems
   or folders, pick an output folder, batch-process non-destructively with
   per-file status and analysis readouts.
3. **`depump_render`** — a headless console renderer (`juce_add_console_app`)
   that drives the same engine: fixture generation, batch/auto recovery,
   envelope extraction, numeric comparison. The dev/test workhorse.

All DSP is pure, host-free C++ in `src/dsp/` (no JUCE plugin/GUI types, no
APVTS) so the plugin, app, and CLI share one engine — see `TESTING.md`'s
purity rule.

## Requirements

- CMake 3.25+
- C++20, Xcode/AppleClang (no Projucer)
- macOS only, Universal Binary (arm64 + x86_64)
- JUCE 8.0.14 and Catch2 v3.8.1, both fetched automatically via CMake
  `FetchContent` — no manual JUCE install needed

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build --config Release -j"$(sysctl -n hw.ncpu)"
```

First configure fetches JUCE and Catch2 and is slow. Artifacts land in
`build/{DePump,DePumpApp,depump_render,DePumpTests}_artefacts/Release/`.

## Tests

Per `TESTING.md`: Catch2 v3 unit tests (`DePumpTests`) cover DSP math,
smoothing, state round-trip, and denormal/NaN handling; pluginval
strictness 10 and DAW ear-checks are the integration layer.

```bash
ctest --test-dir build --output-on-failure
# or, for Catch2 CLI options directly:
build/DePumpTests_artefacts/Release/DePumpTests
# full recovery-quality regression (~8 min, 81 synthetic fixtures):
tools/render/matrix_sweep.sh [workdir]
```

The `.claude/skills/build-and-validate` skill runs the build/sign/pluginval
sequence; `offline-render-harness` covers `depump_render` usage.

## Run

```bash
open build/DePumpApp_artefacts/Release/DePump.app
build/depump_render_artefacts/Release/depump_render --batch IN_DIR --out-dir OUT_DIR
```

## Install locations

`COPY_PLUGIN_AFTER_BUILD` is `TRUE`: the plugin build auto-copies
**unsigned** to the user-level `~/Library/Audio/Plug-Ins/{VST3,Components}/`.
Sign the installed copies after every build (Soundminer will not load an
unsigned plugin):

```bash
codesign --force --deep --timestamp --sign "Developer ID Application: ZQ SFX (TEAMID)" <plugin-path>
```

Soundminer scans the system path only, which needs `sudo` and is a manual,
owner-only step:

```bash
sudo cp -R "$HOME/Library/Audio/Plug-Ins/VST3/DePump.vst3" /Library/Audio/Plug-Ins/VST3/
```

## Project layout

```
src/dsp/    pure host-free DSP core (GainCurve, Envelope, PumpAnalysis, Recovery)
src/io/     WAV read/write, mono mix (shared by app + CLI)
src/app/    DePump.app: Main.cpp, MainComponent (GUI, drag-drop, background worker)
src/        PluginProcessor.{h,cpp} — companion VST3/AU
tools/render/  depump_render CLI + matrix_sweep.sh regression gate
tests/      DePumpTests, DspTests, AnalysisTests, RecoveryRobustnessTests
```

## Roadmap

See `ROADMAP.md` for the sequenced plan and current status, and
`HANDOFF.md` for the detailed "pick up here" state and open decisions.

## Identity & contact

ZQ SFX — https://www.zq-sfx.com — connect@zq-sfx.com

## Licence

Copyright (c) 2026 ZQ SFX.

Licensed under the GNU General Public License v3.0 or later (GPL-3.0-or-later). See
[LICENSE](LICENSE). Built with [JUCE](https://juce.com), used under its AGPLv3 option, which
GPL-3.0 is compatible with.

## Version control

Git only for this project (an exception to the workspace's Diversion-first
policy — see `../CLAUDE.md` §3), with a private GitHub remote at
`github.com/themightyzq/DePump`.
