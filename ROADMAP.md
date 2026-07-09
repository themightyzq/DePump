# DePump Roadmap

## Next
- Scaffold CMake + JUCE 8 project (AU + VST3, universal binary); fill in
  CLAUDE.md "Build & test commands" and the build-and-validate skill with
  verified commands.
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
- CI, code-signing + notarization (deferred until distribution).

## Deferred hygiene log
(Log deferrable repo-hygiene items here per CLAUDE.md; none yet.)
