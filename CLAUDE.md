# CLAUDE.md — DePump

JUCE audio plugin that removes baked-in sidechain pumping from stems by
estimating and inverting the gain envelope. Primary users: producers and
engineers working with AI-separated or client-supplied stems.

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
- Formats: AU + VST3. Platform: macOS only. Universal binary
  (arm64 + x86_64).
- Build: CMake + JUCE 8 (pinned via CMake FetchContent), C++20,
  Xcode clang. No Projucer.
- Versioning: semver, 0.x until first usable release.
- Distribution: personal/local use for now; code-signing and
  notarization deferred until distribution is planned.

## Build & test commands
No code exists yet. The scaffold session must fill this section with
commands verified to actually run, and remove this notice. Do not
invent commands that have not been executed.

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
