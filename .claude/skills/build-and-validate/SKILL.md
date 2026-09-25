---
name: build-and-validate
description: Build DePump (CMake, VST3 + AU + Standalone, universal binary), sign the installed plugins, and validate with pluginval strictness 10. Use whenever a change needs the definition-of-done build/validation gates run, or the user asks to build, rebuild, validate, or install the plugin.
---

# Build and Validate

All commands below were verified working on 2026-07-09. Run from the repo
root. Report actual gate results — never claim a gate passed without output.

## 1. Configure (only needed once, or after CMakeLists changes)

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
```

## 2. Build (long — run in background)

```bash
cmake --build build --config Release -j"$(sysctl -n hw.ncpu)"
```

Build auto-copies plugins (UNSIGNED) to `~/Library/Audio/Plug-Ins/VST3/`
and `~/Library/Audio/Plug-Ins/Components/`.

## 3. Unit tests

```bash
ctest --test-dir build --output-on-failure
```

(Direct binary for Catch2 CLI options: `build/DePumpTests_artefacts/Release/DePumpTests`)

## 4. Sign installed copies (required — Soundminer won't load unsigned)

```bash
IDENTITY="$(security find-identity -v -p codesigning | grep 'Developer ID Application' | head -1 | sed -E 's/.*"(.*)"/\1/')"  # your ZQ SFX Developer ID certificate
codesign --force --deep --timestamp --sign "$IDENTITY" "$HOME/Library/Audio/Plug-Ins/VST3/DePump.vst3"
codesign --force --deep --timestamp --sign "$IDENTITY" "$HOME/Library/Audio/Plug-Ins/Components/DePump.component"
codesign -v "$HOME/Library/Audio/Plug-Ins/VST3/DePump.vst3"
codesign -v "$HOME/Library/Audio/Plug-Ins/Components/DePump.component"
```

## 5. pluginval, strictness 10, both formats

```bash
/Applications/pluginval.app/Contents/MacOS/pluginval --strictness-level 10 --validate "$HOME/Library/Audio/Plug-Ins/VST3/DePump.vst3"
killall -9 AudioComponentRegistrar 2>/dev/null
/Applications/pluginval.app/Contents/MacOS/pluginval --strictness-level 10 --validate "$HOME/Library/Audio/Plug-Ins/Components/DePump.component"
```

Both must end with `SUCCESS`.

## 6. Verify universal binary (after CMake/toolchain changes)

```bash
file build/DePump_artefacts/Release/VST3/DePump.vst3/Contents/MacOS/DePump
# must show both x86_64 and arm64
```

## Soundminer system install (manual user step — needs sudo)

Soundminer scans the SYSTEM folder. After signing, the user runs:

```bash
sudo cp -R "$HOME/Library/Audio/Plug-Ins/VST3/DePump.vst3" /Library/Audio/Plug-Ins/VST3/
```

Suggest they type it with a `!` prefix in the prompt; do not run sudo yourself.

## Artifact locations

- `build/DePump_artefacts/Release/{VST3,AU,Standalone}` (unsigned)
- `~/Library/Audio/Plug-Ins/VST3/DePump.vst3`, `~/Library/Audio/Plug-Ins/Components/DePump.component` (signed after step 4)
