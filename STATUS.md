# DePump - Status

**As of 2026-09-29.** Earlier report: [`../docs/STATE_OF_THE_UNION_2026-09.md`](../docs/STATE_OF_THE_UNION_2026-09.md)

Removes audible sidechain pumping from stems so they load cleanly into a DAW.

## 2026-09-29 (portfolio review)

Full report: ../docs/STATE_OF_THE_UNION_2026-09-29.md

### Verified today

| Check | Result |
|---|---|
| Build | Fresh universal Release, 0 errors |
| First-party warnings | 0 |
| ctest | 22/22 (20 before today's +2) |
| pluginval (strictness 5) | PASS |
| auval | PASS |
| Deployment target | minos 11.0 |
| zqsfx_ui pin | v0.2.1 (NOT v0.4.0 as previously written) |
| VST3 class ID / vendor | ZQSF prefix, vendor "ZQ SFX" |

### Fixed today (git d4934df)

- Learn result callbacks no longer use-after-free after destruction/prepare
- State flag is written to a copy

### Open (see report backlog)

- Always-on soft clip (about -0.7 dB at -6 dBFS peaks, even at Amount 0)
- stopThread(2000) can force-kill a long analysis
- Stopped-transport phase re-anchor can buzz
- Learn param is automatable
- Generic editor never shows Learn status
- Audible check in a DAW (also real-host phase lock unverified)
- Fitter can pick the wrong phase on an unlucky window (press Learn again)

## 2026-09-23 (history)

- Engine wired into plugin as Learn-then-apply (press Learn, 3s capture, background fit, parameters set, zero-latency correction phase-locked to host timeline)
- New parameters: learn (meta) and hold
- processBlockBypassed; engaged flag keeps default state bit-identical pass-through until Learn
- Output trim works before Learn
- Causal soft clip
- 20/20 tests (2 new that day)
- pluginval strictness 10, auval, universal
- macOS deployment target pinned to 11.0 before project()
- zqsfx_ui pin is v0.2.1 (the v0.4.0 bump noted that day was wrong)
- Pushed to GitHub

## What works

- Standalone app and CLI renderer work
- Plugin runs the analysis engine via Learn (no longer a pass-through)
