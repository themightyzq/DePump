# DePump Roadmap

## Next
- Build the offline render harness (see .claude/skills/offline-render-harness).
- MVP algorithm: user-guided inverse envelope (tempo-synced rate, depth,
  dip shape, phase offset) — the mirror image of pump generators.

## Later
- Learn-from-audio mode: beat-synchronous envelope folding → median dip
  template → inversion.
- Sidechain key input (kick stem as trigger for phase alignment).
- Few-band (3–4) variant for multiband-sidechained material.
- Headroom management / max-boost ceiling to avoid amplifying
  AI-separation artifacts in the dips.
- CI; notarization (deferred until distribution — code signing itself is
  already part of every build, required for Soundminer).
- Soundminer install: signed VST3 into /Library/Audio/Plug-Ins/VST3/
  (manual sudo step).

## Deferred hygiene log
(Log deferrable repo-hygiene items here per CLAUDE.md; none yet.)
