cmake_minimum_required(VERSION 3.25)
if(APPLE)
    # Universal binary is mandatory: Soundminer will not load anything else (JUCE/CLAUDE.md section 5).
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build architectures for macOS")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version (universal binaries cannot go below 11.0)")
endif()
project(DePump VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.14
    GIT_SHALLOW TRUE
)
FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.8.1
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(JUCE Catch2)

# The ZQ SFX house UI (tokens, LookAndFeel, knobs, fonts, the company mark). Must come after
# JUCE. Pinned by tag: bump deliberately, then re-run depump_ui_snapshot and compare.
FetchContent_Declare(zqsfx_ui
    GIT_REPOSITORY https://github.com/themightyzq/zqsfx_ui.git
    GIT_TAG v0.2.1
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(zqsfx_ui)

juce_add_plugin(DePump
    COMPANY_NAME "ZQ SFX"
    COMPANY_WEBSITE "https://www.zq-sfx.com"
    COMPANY_EMAIL "connect@zq-sfx.com"
    COMPANY_COPYRIGHT "Copyright (c) 2026 ZQ SFX"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE ZQSF
    PLUGIN_CODE Dpmp
    BUNDLE_ID "com.zqsfx.depump"
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "DePump"
    VST3_CATEGORIES "Fx" "Dynamics" "Restoration"
)

target_sources(DePump PRIVATE
    src/PluginProcessor.cpp
    src/PluginLearnEngine.cpp
)

target_include_directories(DePump PRIVATE src)

# Soundminer compatibility — see CLAUDE.md; never remove these.
set(DEPUMP_HOST_COMPAT_DEFS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
)
if(APPLE)
    list(APPEND DEPUMP_HOST_COMPAT_DEFS JUCE_JACK=0 JUCE_ALSA=0)
endif()

target_compile_definitions(DePump PUBLIC ${DEPUMP_HOST_COMPAT_DEFS})

target_link_libraries(DePump
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Pure, host-free DSP core (see TESTING.md purity rule). Shared by the
# engine CLI, tests, and (once the processor uses it) the plugin.
set(DEPUMP_DSP_SOURCES
    src/dsp/GainCurve.cpp
    src/dsp/Envelope.cpp
    src/dsp/PumpAnalysis.cpp
    src/dsp/Recovery.cpp
)

set(DEPUMP_IO_SOURCES
    src/io/AudioFileIO.cpp
)

# The plugin now calls into the pure DSP core (Learn wires it up; see
# src/PluginLearnEngine.cpp) — needs the same sources as the app/CLI.
target_sources(DePump PRIVATE ${DEPUMP_DSP_SOURCES})

# --- Engine CLI: headless driver of the product pipeline ---
juce_add_console_app(depump_render PRODUCT_NAME "depump_render")

target_sources(depump_render PRIVATE
    tools/render/main.cpp
    ${DEPUMP_DSP_SOURCES}
    ${DEPUMP_IO_SOURCES}
)

target_include_directories(depump_render PRIVATE src)

target_link_libraries(depump_render
    PRIVATE
        juce::juce_audio_formats
        juce::juce_recommended_config_flags
)

# --- Standalone app: the primary product (see CLAUDE.md) ---
juce_add_gui_app(DePumpApp
    PRODUCT_NAME "DePump"
    COMPANY_NAME "ZQ SFX"
    COMPANY_WEBSITE "https://www.zq-sfx.com"
    COMPANY_EMAIL "connect@zq-sfx.com"
    COMPANY_COPYRIGHT "Copyright (c) 2026 ZQ SFX"
    BUNDLE_ID "com.zqsfx.depump.app"
)

target_sources(DePumpApp PRIVATE
    src/app/Main.cpp
    src/app/MainComponent.cpp
    ${DEPUMP_DSP_SOURCES}
    ${DEPUMP_IO_SOURCES}
)

target_include_directories(DePumpApp PRIVATE src)
# App is not a plugin: only the module-slimming defines apply here.
target_compile_definitions(DePumpApp PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)

target_link_libraries(DePumpApp
    PRIVATE
        zqsfx::ui
        juce::juce_audio_formats
        juce::juce_gui_extra
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# --- Headless UI render: the look-and-feel regression gate ---
juce_add_console_app(depump_ui_snapshot PRODUCT_NAME "depump_ui_snapshot")

target_sources(depump_ui_snapshot PRIVATE
    tools/ui_snapshot/main.cpp
    src/app/MainComponent.cpp
    ${DEPUMP_DSP_SOURCES}
    ${DEPUMP_IO_SOURCES}
)

target_include_directories(depump_ui_snapshot PRIVATE src)
target_compile_definitions(depump_ui_snapshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)

target_link_libraries(depump_ui_snapshot
    PRIVATE
        zqsfx::ui
        juce::juce_audio_formats
        juce::juce_gui_extra
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

# --- Unit tests (definition-of-done gate) ---
juce_add_console_app(DePumpTests PRODUCT_NAME "DePumpTests")

target_sources(DePumpTests PRIVATE
    tests/DePumpTests.cpp
    tests/DspTests.cpp
    tests/AnalysisTests.cpp
    tests/RecoveryRobustnessTests.cpp
    tests/PluginDspTests.cpp
    src/PluginProcessor.cpp
    src/PluginLearnEngine.cpp
    ${DEPUMP_DSP_SOURCES}
)

target_include_directories(DePumpTests PRIVATE src)
target_compile_definitions(DePumpTests PRIVATE ${DEPUMP_HOST_COMPAT_DEFS}
    # tests/PluginDspTests.cpp pumps the message loop with
    # MessageManager::runDispatchLoopUntil() so PluginLearnEngine's
    # background-thread callAsync() results can be delivered synchronously
    # in a console test app. Test-only; never defined for the shipped plugin.
    JUCE_MODAL_LOOPS_PERMITTED=1)

target_link_libraries(DePumpTests
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        Catch2::Catch2WithMain
        juce::juce_recommended_config_flags
)

enable_testing()
list(APPEND CMAKE_MODULE_PATH "${Catch2_SOURCE_DIR}/extras")
include(Catch)
catch_discover_tests(DePumpTests)
