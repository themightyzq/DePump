# DePump Roadmap

Shaped by REVIEW-UX.md (early-cycle expert review, approved 2026-07-09).

## Next
- Offline render harness (see .claude/skills/offline-render-harness) —
  prerequisite for measurable DSP work.
- MVP algorithm: user-guided inverse envelope. Scope grown per
  REVIEW-UX.md: Sync AND Free (Hz) rate modes with dotted/triplet
  divisions (#1/#6 — Soundminer has no tempo; verify what
  AudioPlayHead returns there), Amount scaling not dry/wet Mix (#5),
  output trim + headroom story (#4). Parameter surface already updated.
- First UI investment: gain-curve-over-audio-envelope visualization —
  the affordance that makes Phase usable (#2). Before any other custom
  UI work.

## Then (first post-MVP feature — the product's identity)
- Learn mode (#3): beat-synchronous envelope folding → median dip
  template → auto-set rate/phase/depth/shape. Also solves phase
  anchoring in transport-less hosts.

## Later
- Factory presets + APVTS preset save (#8).
- Recovery-framed parameter copy/tooltips (#7).
- Full custom editor (branding, grouping) reusing the envelope view (#9).
- Sidechain key input (kick stem as trigger for phase alignment).
- Few-band (3–4) variant for multiband-sidechained material.
- CI; notarization (deferred until distribution — code signing itself is
  already part of every build, required for Soundminer).
- Soundminer install: signed VST3 into /Library/Audio/Plug-Ins/VST3/
  (manual sudo step).

## Deferred hygiene log
(Log deferrable repo-hygiene items here per CLAUDE.md; none yet.)

## Late-cycle UX pass criteria (from REVIEW-UX.md)
Phase alignable in <30 s on a real stem; correct behavior confirmed
inside Soundminer; no clipped output on a hot stem at defaults;
first-time user reaches "audibly fixed" without reading anything.
