# CLAUDE.md — DePump

DePump removes baked-in sidechain pumping from stems by estimating and
inverting the gain envelope. Primary users: producers and engineers
working with AI-separated or client-supplied stems.

**Product shape (decided by owner, 2026-07-09, superseding the earlier
plugin-first framing):** the primary product is a STANDALONE macOS app —
the "RX route." Deep offline analysis of whole files, fully automatic
recovery (no tempo/beat input from the user), batch processing of stem
folders, non-destructive output. The VST3/AU plugin is a companion
product built on the same core, deprioritized until the app works. All
recovery intelligence lives in the pure DSP core (src/dsp/) so both
products share one engine.

## Non-negotiable: the real-time audio thread
processBlock and everything it calls run on a real-time thread. There:
no heap allocation, no locks of any kind (including try_lock), no I/O,
no logging, no unbounded-duration operations, no std::shared_ptr on
audio-thread objects. Use juce::ScopedNoDenormals in processBlock. Never
assume a fixed buffer size or sample rate. Cross-thread communication is
lock-free only: atomics, FIFOs, APVTS. AudioProcessor (audio thread) and
AudioProcessorEditor (message thread) stay strictly separated. This
outranks convenience and idiomatic C++ everywhere it applies.

## Targets & toolchain (decided by owner, 2026-07-09)
- Primary target: standalone analysis/batch app (GUI app target, plus a
  headless CLI driver of the same engine for dev/scripting). Companion
  formats VST3 + AU keep building and passing gates but do not drive
  feature work. Platform: macOS only. Universal binary (arm64 + x86_64).
- Build: CMake + JUCE 8 (pinned via CMake FetchContent), C++20,
  Xcode clang. No Projucer.
- Versioning: semver, 0.x until first usable release.
- Code signing with "Developer ID Application: ZQ SFX
  (TEAMID)" is part of every release build — Soundminer will not
  load unsigned plugins. Notarization deferred until distribution.

## Soundminer compatibility (binding; owner-supplied, 2026-07-09)
Source: Project_HyperPrism/VST3_SOUNDMINER_SETUP.md. Never remove:
- Compile definitions: JUCE_WEB_BROWSER=0, JUCE_USE_CURL=0,
  JUCE_VST3_CAN_REPLACE_VST2=0, JUCE_DISPLAY_SPLASH_SCREEN=0,
  JUCE_REPORT_APP_USAGE=0; on macOS also JUCE_JACK=0, JUCE_ALSA=0.
- juce_add_plugin: IS_SYNTH FALSE, NEEDS_MIDI_INPUT/OUTPUT FALSE,
  IS_MIDI_EFFECT FALSE, EDITOR_WANTS_KEYBOARD_FOCUS FALSE.
- Vendor identity: COMPANY_NAME "ZQ SFX", PLUGIN_MANUFACTURER_CODE
  ZQFX (matches HyperPrism); PLUGIN_CODE Dpmp is DePump's unique code.
- All automatable parameters live in APVTS with unique IDs.
- Soundminer scans /Library/Audio/Plug-Ins/VST3/ — installing there
  needs sudo and is a manual user step; builds auto-copy to the
  user-level ~/Library/Audio/Plug-Ins/ folders.

## Build & test commands (verified 2026-07-09)
- Configure: `cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"`
- Build: `cmake --build build --config Release -j"$(sysctl -n hw.ncpu)"`
- Unit tests: `ctest --test-dir build --output-on-failure`
- Test strategy, unit/integration boundary, and the binding DSP purity
  rule (all DSP is pure + host-free in src/dsp/): see TESTING.md
- Sign (build auto-copies UNSIGNED to ~/Library/Audio/Plug-Ins — sign
  the installed copies after every build):
  `codesign --force --deep --timestamp --sign "Developer ID Application: ZQ SFX (TEAMID)" <plugin-path>`
- Validate: `/Applications/pluginval.app/Contents/MacOS/pluginval --strictness-level 10 --validate <plugin-path>`
  (for AU, run `killall -9 AudioComponentRegistrar` first if stale)
- Artifacts: `build/DePump_artefacts/Release/{VST3,AU,Standalone}`
The `build-and-validate` skill runs this whole sequence.

## Definition of done (decided by owner, 2026-07-09)
A change is done when: unit tests pass; pluginval at strictness 10
passes on all built formats; and any audible behavior change has been
checked by ear in a real DAW. State plainly which gates were run.

## Working principles
- Verify your own work; do not trust that it worked. Re-read the edit,
  run the suite, reproduce the original case before claiming success.
- Investigate before answering; base claims on what the code shows and
  flag inferences as inferences.
- Root-cause fixes only; no bandaids.

## Git & repo hygiene
- git with a private GitHub remote. Feature branches; main stays
  releasable. Imperative-mood commit subjects.
- Blocking: failing build/tests on main; secrets or unversioned binary
  junk about to be committed.
- Deferrable (log to ROADMAP.md, never interrupt feature work):
  formatting drift, dependency bumps, documentation gaps, CI polish.

## Steering layers
- CLAUDE.md — always-on rules (this file).
- Skills (.claude/skills/) — reusable playbooks, loaded on invocation:
  - `build-and-validate` — build → pluginval → artifact locations
    (stub until the scaffold session verifies real commands).
  - `offline-render-harness` — render test WAVs through the processor
    to iterate DSP without a DAW (stub until the harness exists).
- Subagents — context-isolating or parallel research; use built-ins
  (Explore, general-purpose); no custom subagents defined.
- Hooks (.claude/settings.json) — clang-format runs automatically on
  every C++ file edit; never encode a deterministic guard as a prompt.
