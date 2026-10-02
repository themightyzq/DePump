# Changelog

## Unreleased

### Changed

- Plugin: zqsfx_ui moves from v0.2.1 to v0.5.0. Every knob takes keyboard focus (arrow keys,
  Shift+arrow for a fine step) and double-click returns it to the parameter default; the editor
  no longer sets keyboard focus on the knobs itself, and a test covers all of it.

## 0.2.0 - 2026-10-01

### Changed (audible)

- Plugin: the safety clip no longer colours normal-level audio. It used to apply tanh to
  every sample once the correction was engaged, which turned a -6 dBFS peak into about
  -6.7 dBFS and added distortion even at Amount 0. It is now exactly transparent up to
  -1 dBFS and limits smoothly above that, to a ceiling of -0.3 dBFS.
- Plugin: with a stopped host transport that reports the same position every block, the
  correction no longer restarts its curve each block (a buzz at the block rate, or a held
  boost inside a dip). It free-runs on its own clock, as it already did for hosts that
  report no position.

### Changed

- Plugin: new window (house UI) with the model controls, a Learn button and a status line
  that shows listening, analysing with progress, learned, or the reason Learn failed. The
  window scales uniformly.
- Plugin: the Learn parameter is no longer automatable. Its ID is unchanged.
- Plugin: closing a session or changing the sample rate during a Learn analysis now stops
  the analysis at once instead of waiting for it, or forcing the thread down after 2 seconds.
- Fonts: the three embedded typefaces (SIL OFL 1.1) are credited in the README and their
  licence texts are in the licenses folder.
- Plugin: the Learn button becomes Cancel while Learn is listening or analysing. Cancelling
  applies nothing, puts the Learn parameter back to 0 and shows "Cancelled" in the status line.
- Plugin: the window size is saved in the plugin state (editor_width, editor_height; not
  parameters) and restored, clamped to the allowed range.
- Plugin: the knob value boxes are 25 px tall (they were 16 px, under the 22 px target size).
- macOS bundles (VST3, AU, Standalone, DePump.app) now carry the OFL licence texts in
  Contents/Resources/licenses.
