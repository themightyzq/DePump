# DePump Roadmap

Product pivot 2026-07-09 (owner decision): standalone deep-analysis
batch app is the primary product — the "RX route." Fully automatic
recovery, no tempo/beat input, batch a folder of stems, non-destructive
output. Plugin becomes a companion built on the same core. REVIEW-UX.md
predates the pivot (plugin-framed); its findings on headroom (#4),
Amount-not-Mix (#5), visualization (#2), and auto-analysis (#3) carry
over — tempo-free operation is now core design rather than a fix.

## Next — the engine, in measurable steps
1. Offline render harness / engine CLI (see
   .claude/skills/offline-render-harness): file → pipeline → file, with
   envelope extraction, synthetic pumped fixtures (known clean × known
   gain curve), and numeric comparison. This is the embryo of the
   product engine AND how recovery quality gets measured from day one.
2. Analysis core v1 (fully automatic, no tempo input): envelope
   autocorrelation → pump period; cycle folding → median dip template +
   phase; time-varying tracking for drift. Measured against fixtures:
   recovered output within a stated dB tolerance of the clean original.
3. Recovery core v1: apply the inverse of the estimated gain curve with
   Amount scaling, headroom management/true-peak ceiling, output trim.
4. Batch CLI: process a folder of stems non-destructively (suffix or
   output dir) — immediately useful before any GUI exists.
5. GUI app v1: drop files/folder, waveform + detected-pump overlay
   (REVIEW-UX #2), A/B preview, per-file confidence readout, batch
   queue, render.

## Then
- Multiband analysis/recovery (multiband-sidechained material).
- Neural gain inversion (de-limiter literature) where template methods
  fail — irregular/program-dependent pumping.
- Companion plugin: same core; Learn button = analysis v1 on a captured
  window; Soundminer compatibility rules apply here (see CLAUDE.md).

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
