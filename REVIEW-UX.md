# REVIEW-UX.md — End-User Expert Review (early-cycle)

Reviewed 2026-07-09 against commit 515ef67. **Early-cycle pass: roadmap
input.** The product is a validated scaffold (pass-through DSP, generic
editor, 6 APVTS params: Rate 1/1–1/16, Depth 0–24 dB, Phase ±50%,
Attack 1–100 ms, Release 20–500 ms, Mix 0–100%). This review judges the
planned MVP workflow that surface embodies, as a demanding user of
restoration/dynamics tools would meet it. The late-cycle pass should
verify execution against the criteria below.

Persona held: a producer/mixer with a folder of AI-separated or
client-supplied stems, pads and bass audibly pumping, wanting them flat
NOW; secondary persona: a post/SFX editor auditioning and processing in
Soundminer (a stated project goal).

Findings sorted by user impact.

---

## 1. Tempo-synced-only Rate likely breaks the Soundminer workflow — DEFECT (against stated goals)

**What the user experiences:** In Soundminer there is no session tempo or
beat grid. A Rate parameter that only offers note divisions (1/1–1/16)
needs host BPM and transport position to mean anything. In a
library-browser host the plugin would either assume a default BPM
(silently wrong for almost every file) or free-run with no way to match
the material.

**Why it matters:** Soundminer compatibility is a binding project goal
(CLAUDE.md). A de-pumper whose core timing control is meaningless in
that host fails its own spec.

**Status: believed, not yet verified** — confirm what
`AudioPlayHead::getPosition()` actually returns inside Soundminer before
building the MVP. But the fix is needed regardless (see below): plenty of
pumped material isn't on a clean grid of the host's tempo anyway.

**Roadmap input:** Rate must have a **free mode (Hz or ms cycle length)**
alongside sync mode, plus a way to anchor phase without a transport
(tap/retrigger, or alignment learned from the audio itself). This is MVP
scope, not a later add.

## 2. Phase cannot be set by ear on a blind slider — GAP (table stakes)

**What the user experiences:** Phase is THE parameter that makes or
breaks recovery — boosting even slightly off-beat makes the pumping
*worse* (boost lands on the untouched part, dip stays). At 1/4-note,
120 BPM, the ±50% range spans ±250 ms; finding the right offset by
looping audio and nudging a generic slider is a guessing game that makes
the user feel stupid.

**Why it matters:** Every product in the adjacent category of
pump-generator plugins ships a curve-over-waveform
display — users expect to *see* where the envelope sits against the
audio. For an *inverse* tool the need is stronger, since the cost of
misalignment is amplification of the artifact.

**Roadmap input:** The MVP editor needs, at minimum, a gain-curve
visualization with the incoming audio's envelope behind it. This is not
UI polish to defer with the custom editor — it is the affordance that
makes Phase usable at all. (Testable criterion for the late pass: a user
can align phase on a real stem in under 30 seconds without docs.)

## 3. No "Learn" mode — OPPORTUNITY (the leapfrog)

**What the user experiences (as planned):** They must reverse-engineer
the original mix's compressor settings — rate, depth, attack, release,
phase — by ear. That is exactly the expertise barrier a recovery tool
exists to remove. The category's best restoration and dynamic-fixer
tools lead with analyze-then-adjust, not dial-from-scratch.

**Why it matters:** Beat-synchronous envelope folding over a few bars can
estimate rate, phase, depth, and dip shape automatically (already
sketched in ROADMAP.md as a later item). One **Learn button that
pre-sets the manual controls** turns first-success time from minutes
into seconds, and is the difference between "another envelope tool" and
"the plugin that fixes pumping."

**Roadmap input:** Keep the manual engine as MVP (it is the engine
either way, and expert users need the manual override), but promote
Learn from "Later" to **immediately after MVP**, before any custom-UI
investment beyond finding #2. Learn also solves #1's no-transport phase
anchoring for free.

## 4. Boosted dips will clip and nothing manages headroom — GAP

**What the user experiences:** Up to +24 dB of recovery gain into
material often already near 0 dBFS. First real-world use on a loud stem
clips the channel; the user blames the plugin.

**Why it matters:** Restoration users judge tools by whether output is
trustworthy for direct bounce/batch use (Soundminer especially).

**Roadmap input:** MVP needs an output trim and a true-peak-aware
ceiling (or at minimum a clip indicator); state the gain-staging story
in the UI. Also decide and document float-headroom behavior (32-bit
float paths don't clip until the DAC — but bounces to fixed formats do).

## 5. "Mix" is the wrong control for this job — GAP (design decision needed)

**What the user experiences:** A Mix knob on a de-pumper blends the
*pumped* signal back in — parallel-mixing the artifact you are removing.
Users reaching for "less effect" actually want a *weaker inverse
envelope*, not a dry blend. Worse, comb-filtering risk if any latency is
ever introduced.

**Roadmap input:** Replace Mix with **Amount** (scales the inverse
envelope depth 0–100%). If Mix survives, it needs a documented reason.
Decide before MVP DSP lands, since it changes the parameter's meaning
(and saved-state compatibility) forever.

## 6. Rate divisions too coarse for real material — BELOW EXPECTATION

Straight 1/1–1/16 only. Real tracks pump on dotted-8th (very common in
EDM sidechain), triplets, or half-time feel. Add dotted/triplet
variants (and free mode per #1). Cheap now, breaking-change later.

## 7. Parameter language speaks compressor, not recovery — BELOW EXPECTATION

"Attack/Release" describe the *original compressor being undone*, which
is a mental 180° for users ("attack of what?"). Consider "Dip speed" /
"Recovery" or similar, with tooltips framing everything as *undoing* the
pump. Copy-level, but it shapes whether the product feels purpose-built.

## 8. No presets — OPPORTUNITY (cheap win)

Factory starting points ("4-on-floor 1/4", "dotted-8th trance",
"half-time 140") + APVTS preset save. Low effort, high perceived
completeness at first launch.

## 9. Generic editor is fine for dev, disqualifying for beta — GAP (sequencing note)

No branding, no grouping, no visualization. Acceptable while the only
user is the developer; must not be what an external tester first sees.
Custom UI work should be scheduled *after* the envelope display (#2)
exists, and can reuse it.

---

## Summary for the roadmap

MVP scope should grow by three things that are cheap now and breaking
later: **free-rate mode (#1)**, **Amount-not-Mix (#5)**,
**dotted/triplet divisions (#6)** — plus **envelope visualization (#2)**
as the first UI investment. **Learn mode (#3)** is the product's actual
identity and should be the first post-MVP feature. #4's output trim
belongs in the MVP DSP; #7/#8 ride along with UI work.

Late-cycle pass criteria: phase alignable in <30 s on a real stem;
correct behavior confirmed inside Soundminer (no-tempo host); no clipped
output on a hot stem at default settings; a first-time user reaches
"audibly fixed" without reading anything.
