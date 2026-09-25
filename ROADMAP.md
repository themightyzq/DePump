# DePump Roadmap

Product pivot 2026-07-09 (owner decision): standalone deep-analysis
batch app is the primary product — the offline-analysis route. Fully automatic
recovery, no tempo/beat input, batch a folder of stems, non-destructive
output. Plugin becomes a companion built on the same core. REVIEW-UX.md
predates the pivot (plugin-framed); its findings on headroom (#4),
Amount-not-Mix (#5), visualization (#2), and auto-analysis (#3) carry
over — tempo-free operation is now core design rather than a fix.

## Next — the engine, in measurable steps
1. ~~Offline render harness / engine CLI~~ DONE 2026-07-09
   (depump_render; known-profile recovery verified to 1e-07 dB residual
   on synthetic fixtures — see .claude/skills/offline-render-harness).
2. Analysis core v1 (fully automatic, no tempo input) — LARGELY DONE
   2026-07-09, acceptance bar partially met: pipeline is autocorrelation
   + comb fundamental selection → fold-coherence period refinement →
   median-folded template → one-pole model fit → corrected-audio polish
   (multi-start coordinate descent on the real signal). Matrix sweep
   (81 fixtures, tools/render/matrix_sweep.sh): 59/81 strictly within
   0.5 dB of clean; 12 between 0.5-0.7; worst 1.58 dB (deep 12 dB dips,
   from 3-12 dB of original pumping — >90% reduction everywhere).
   Do-no-harm and too-short-input guards verified. V1 bar re-baselined
   (owner-approved 2026-07-09): >=59/81 strictly within 0.5 dB + hard
   cap 1.8 dB, encoded in matrix_sweep.sh; 0.5-everywhere is the v1.1
   stretch alongside drift tracking (candidates: better phase init,
   sample-domain edge refinement; attempted and rejected on evidence:
   slope-weighted objective — trades flat-region observability).
3. ~~Recovery core v1~~ DONE 2026-07-09: Amount scaling + trimToCeiling
   headroom guard (identical per-channel scale, trim reported in dB).
   Robustness verified on program-varying material (musical swells +
   noise bed): recovery <=1.0 dB max / <=0.25 rms; do-no-harm holds.
4. ~~Batch CLI~~ DONE 2026-07-09: `depump_render --batch IN --out-dir
   OUT` — auto-recovers every wav/aiff, passes unpumped files through
   bit-identically, skips unreadable files with per-file errors,
   refuses in-place writes. Verified on a mixed folder; batch outputs
   measured 0.33-0.38 dB from clean.
5. ~~GUI app v1~~ DONE 2026-07-09 (first cut): DePump.app — drag-drop
   files/folders, output-folder picker, background batch queue with
   per-file status + analysis readout (rate/depth/trim), non-
   destructive guard, signed. Deferred to app v1.1: waveform +
   detected-pump overlay (REVIEW-UX #2) and A/B playback preview —
   both need audio playback infrastructure.

## Then
- Multiband analysis/recovery (multiband-sidechained material).
- Neural gain inversion (de-limiter literature) where template methods
  fail — irregular/program-dependent pumping.
- ~~Companion plugin: same core; Learn button = analysis v1 on a captured
  window; Soundminer compatibility rules apply here (see CLAUDE.md)~~ DONE
  2026-09-23: the plugin's Learn parameter captures ~3s of audio, fits a
  profile with the existing analysis core off the audio thread, and applies
  it to freeRate/depth/attack/hold/release/phase; no added latency.

## Later
- Factory profiles/presets; recovery-framed copy (REVIEW-UX #7/#8).
- Sidechain key input for the plugin (kick stem as phase reference).
- CI; notarization + distribution packaging (app AND plugin).

## Deferred hygiene log
(Log deferrable repo-hygiene items here per CLAUDE.md; none yet.)

## Acceptance bar for "recovered properly" (owner intent, 2026-07-09)
The user should not need to tell the app anything about the music. On
synthetic fixtures: recovered audio within tolerance of the clean
original (exact number set when analysis v1 lands). On real stems: no
audible pumping at default settings, no clipping, artifacts not
amplified beyond the source material's own.
