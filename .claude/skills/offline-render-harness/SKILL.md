---
name: offline-render-harness
description: Render test WAVs through the DePump processor offline and compare gain envelopes, so DSP changes can be iterated and A/B'd without opening a DAW. Use when developing or debugging the de-pump algorithm, comparing envelope estimates, or checking that a DSP change did what was intended.
---

# Offline Render Harness

> STUB — the harness does not exist yet. Build it as one of the first
> post-scaffold tasks, then replace this stub with the verified usage.

## Intended design (to be verified when built)

A console target (e.g. `depump_render`) that:
1. Loads an input WAV (test fixtures live in `tests/fixtures/`, including
   synthetic pumped material: known clean signal × known periodic gain curve).
2. Runs it through the AudioProcessor offline at a chosen sample rate and
   block size (vary both — never bake in assumptions).
3. Writes the output WAV plus a short-term loudness/gain envelope CSV.
4. A comparison mode diffs two envelope CSVs and reports max/RMS deviation.

Because fixtures can be synthetic (clean × known g(t)), recovery quality is
measurable objectively: apply DePump to the pumped fixture and compare
against the clean original.

## Usage (fill in when the harness exists)

TBD
