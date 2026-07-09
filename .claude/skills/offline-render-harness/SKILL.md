---
name: offline-render-harness
description: Drive the DePump engine headlessly with depump_render — generate synthetic pumped fixtures, render/recover files, extract RMS envelopes, and numerically compare results. Use when developing or debugging analysis/recovery DSP, measuring recovery quality, or checking that a DSP change did what was intended.
---

# Offline Render Harness (`depump_render`)

All commands verified 2026-07-09. Binary:
`build/depump_render_artefacts/Release/depump_render` (builds with the
normal build; thin CLI over the pure core in `src/dsp/`).

## Generate ground-truth fixtures

```bash
depump_render --make-fixture --out-dir DIR [--sr 48000] [--seconds 8] \
  [--rate 2 --depth 9 --attack 10 --hold 60 --release 150 --phase 0.25]
```

Writes `clean.wav` (constant-amplitude chord) and `pumped.wav`
(clean × synthesized gain curve). Because the curve is known, recovery
quality is objectively measurable.

## Render / recover

```bash
depump_render --in X.wav [--out Y.wav] [--envelope Z.csv] \
  [--apply | --invert] [--amount 0..1] [profile args as above]
```

- No `--apply`/`--invert`: pipeline identity — output verified
  bit-identical to input.
- `--invert` with a profile applies the inverse gain curve (recovery).
- `--envelope` writes RMS envelope CSV (20 ms window / 5 ms hop,
  `time_sec,rms_db`).

## Automatic analysis and recovery (analysis core v1)

```bash
depump_render --in pumped.wav --auto --out recovered.wav [--amount 0..1]
```

Prints detection verdict, confidence, period/depth/dip, and the fitted
one-pole model parameters (or "model fit: rejected" when the measured
template is used directly). No tempo input needed. Clean audio is
detected as unpumped and passed through bit-identically.

Full-matrix regression (81 fixtures, ~8 min):

```bash
tools/render/matrix_sweep.sh [workdir]   # exit 0 iff all within 0.5 dB
```

## Compare envelopes (scriptable pass/fail)

```bash
depump_render --compare A.csv --with B.csv [--tolerance dB]
# exit 0 within tolerance, 1 above it, 2 on bad input
```

## Verified reference results (2026-07-09)

Fixture at rate 2 Hz / depth 9 dB / attack 10 / hold 60 / release 150 /
phase 0.25, 48 kHz: pumped-vs-clean max deviation 8.97 dB (≈ the baked
depth); after `--invert` with the same profile, recovered-vs-clean max
deviation 1e-07 dB. Known-profile recovery is exact; automatic analysis
(ROADMAP step 2) only has to find the profile.

## Notes

- PumpProfile.holdMs is distinct from attackMs: attack is the one-pole
  fall speed, hold is how long gain stays down (trigger energy length).
- Core math lives in src/dsp/ (GainCurve, Envelope, PumpProfile) —
  pure, host-free, unit-tested in tests/DspTests.cpp.
