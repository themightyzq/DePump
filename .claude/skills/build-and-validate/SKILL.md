---
name: build-and-validate
description: Build DePump (CMake, AU + VST3, universal binary) and validate with pluginval strictness 10. Use whenever a change needs the definition-of-done build/validation gates run, or the user asks to build, rebuild, or validate the plugin.
---

# Build and Validate

> STUB — this project is not scaffolded yet. The scaffold session must
> replace this stub with commands it has actually executed and verified,
> per CLAUDE.md "Build & test commands". Do not run the placeholders
> below as if they were real.

## Intended playbook (to be verified at scaffold time)

1. Configure: `cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"`
2. Build: `cmake --build build --config Release`
3. Run unit tests: (test target TBD)
4. pluginval, strictness 10, against both formats:
   - VST3: `pluginval --strictness-level 10 --validate <path-to>.vst3`
   - AU: `pluginval --strictness-level 10 --validate <path-to>.component`
     (AU may require the component installed to `~/Library/Audio/Plug-Ins/Components` and a `killall -9 AudioComponentRegistrar` first)
5. Report which gates ran and their actual results — never claim a gate passed without output.

## Artifact locations (fill in at scaffold time)

- Built plugins: TBD (typically `build/DePump_artefacts/Release/`)
