# =============================================================================
#  Eighth Hall Reverb - build script
#
#  CMake is a tool that writes the "project files" for whatever computer you're
#  on: a Visual Studio solution on Windows, an Xcode project on Mac, or
#  Makefiles/Ninja on Linux. You never edit those generated files - you edit
#  THIS file, and CMake regenerates them.
#
#  Quick start (any OS, from the folder that contains this file):
#      cmake -B build -DCMAKE_BUILD_TYPE=Release
#      cmake --build build --config Release
#
#  See docs/BUILDING.md for a step-by-step beginner walkthrough.
# =============================================================================

cmake_minimum_required(VERSION 3.22)

# On macOS, build for macOS 11 (Big Sur) and newer. Must be set before project().
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(EighthHall VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows: bake the C++ runtime into the plugin so users don't need to install
# the "Visual C++ Redistributable" for it to load.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# -----------------------------------------------------------------------------
#  Get JUCE
#
#  By default CMake downloads JUCE for you (pinned to an exact version so every
#  build is identical). If you already have JUCE on your computer you can point
#  at it instead:   cmake -B build -DEIGHTHHALL_JUCE_PATH=/path/to/JUCE
# -----------------------------------------------------------------------------
set(EIGHTHHALL_JUCE_PATH "" CACHE PATH "Optional path to a local JUCE checkout")

if(EIGHTHHALL_JUCE_PATH)
    add_subdirectory(${EIGHTHHALL_JUCE_PATH} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.9
        GIT_SHALLOW    ON)
    FetchContent_MakeAvailable(JUCE)
endif()

# -----------------------------------------------------------------------------
#  Which plugin formats to build on each platform
#    VST3       - Windows, Mac, Linux (Cubase, Ableton, FL Studio, Reaper, Studio One...)
#    AU         - Mac only (Logic Pro, GarageBand)
#    LV2        - Linux (Ardour, Bitwig on Linux)
#    Standalone - a regular app you can open without a DAW (handy for testing)
#  AAX (Pro Tools) needs Avid's private SDK and a signing account, so it is not
#  included here - see docs/BUILDING.md.
# -----------------------------------------------------------------------------
set(EIGHTHHALL_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND EIGHTHHALL_FORMATS AU)
elseif(UNIX)
    list(APPEND EIGHTHHALL_FORMATS LV2)
endif()

juce_add_plugin(EighthHall
    PRODUCT_NAME                "Eighth Hall"
    COMPANY_NAME                "Fabrick Audio"
    COMPANY_WEBSITE             "https://marlenafabrick.net"
    BUNDLE_ID                   com.fabrickaudio.eighthhall
    PLUGIN_MANUFACTURER_CODE    Fbrk          # 4 letters, at least one uppercase. Identifies YOU.
    PLUGIN_CODE                 Ehal          # 4 letters, unique per plugin you make.
    FORMATS                     ${EIGHTHHALL_FORMATS}
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE         # set TRUE to auto-install into your plugin folder
    VST3_CATEGORIES             Fx Reverb
    AU_MAIN_TYPE                kAudioUnitType_Effect
    LV2URI                      "https://marlenafabrick.net/plugins/eighth-hall")

target_sources(EighthHall PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Presets.cpp
    Source/gui/EighthHallLookAndFeel.cpp
    Source/gui/DecayDisplay.cpp)

target_include_directories(EighthHall PRIVATE Source)

target_compile_definitions(EighthHall PUBLIC
    JUCE_WEB_BROWSER=0                 # we don't embed a web browser
    JUCE_USE_CURL=0                    # we don't download anything
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(EighthHall
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# -----------------------------------------------------------------------------
#  Tests: a small command-line program that checks the reverb engine
#  mathematically (stable, no NaNs, decay time matches the knob, etc.).
#  The DSP engine is plain C++, so these tests don't need JUCE at all.
#  Run with:   ctest --test-dir build -C Release --output-on-failure
# -----------------------------------------------------------------------------
option(EIGHTHHALL_BUILD_TESTS "Build the DSP test program" ON)

if(EIGHTHHALL_BUILD_TESTS)
    enable_testing()
    add_executable(EighthHallTests tests/ReverbTests.cpp)
    target_include_directories(EighthHallTests PRIVATE Source)
    add_test(NAME EighthHallDSP COMMAND EighthHallTests)
endif()
