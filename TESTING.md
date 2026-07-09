# DePump Test Strategy

Decided via suite prompt 02, approved by owner 2026-07-09.

## Harness

Catch2 v3 (FetchContent), test target `DePumpTests` (JUCE console app —
host-free). Run via either:

```bash
ctest --test-dir build --output-on-failure   # preferred; CI-ready
build/DePumpTests_artefacts/Release/DePumpTests   # direct, for Catch2 CLI options
```

## The DSP purity rule (binding)

All DSP lands as pure, host-free classes in `src/dsp/`: no JUCE plugin
lifecycle, no APVTS access, no editor types — buffers, floats, and plain
parameter structs in/out. The processor is a thin wrapper that owns APVTS
and forwards to `src/dsp/`. This is what keeps the core numerically
testable offline. New DSP files are added to both the plugin and test
targets in CMakeLists.txt.

## Unit vs. integration boundary

**Unit — pure, offline, host-free. Most coverage lives here.**
Feed known buffers, assert on output samples numerically. Concrete for
DePump: inverse-envelope gain math (known pumped input × known pump
profile → flat output within tolerance); parameter smoothing (no zipper
steps across block boundaries); state serialization round-trip;
denormal/NaN handling; bit-identical bypass/pass-through; latency
reporting; behavior across sample rates (44.1/48/96k) and block sizes
(64–1024) — never one fixed configuration.

**Integration — the plugin in a host context. Fewer, reserved for seams.**
pluginval strictness 10 on VST3 + AU (part of definition of done, see
build-and-validate skill); DAW ear-check for audible behavior changes;
Soundminer load check after installs. Covers format wrappers, automation,
bus layouts, state recall in-host.

## Coverage posture (policy, not percentage)

Test what fails silently or audibly: gain staging, envelope shapes,
smoothing, state round-trip, latency, denormal/NaN edges. A useful
pattern: offline-render a block and assert numerically (a -6 dB gain
halves amplitude; bypass is bit-identical). Do not chase coverage on GUI
glue. Synthetic fixtures (known clean signal × known gain curve) make
de-pump recovery objectively measurable — the offline render harness
(ROADMAP) builds on this.
