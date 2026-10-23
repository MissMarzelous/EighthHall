# Eighth Hall - project notes for Claude Code

Algorithmic reverb plugin (VST3 / AU / LV2 / Standalone) by Marlena Fabrick (Fabrick Audio),
built as a portfolio piece for audio-software jobs (target: Waves Audio). Marlena is new to
C++ and JUCE: explain changes in plain language and keep code heavily commented.

## Build
- CMake + JUCE 8.0.9 (downloaded automatically by FetchContent). C++20.
- `cmake -B build -DCMAKE_BUILD_TYPE=Release` then `cmake --build build --config Release`
- Tests: `ctest --test-dir build -C Release --output-on-failure`
- Visual Studio: File -> Open -> Folder, run target `EighthHall_Standalone.exe`.
- Artefacts: `build/EighthHall_artefacts/<Config>/{VST3,AU,LV2,Standalone}/` (bundle names contain a space: "Eighth Hall.vst3").

## Layout
- `Source/dsp/` - reverb engine, plain C++ with NO JUCE dependency (keep it that way so tests stay standalone).
  - `FdnReverb.h`: 8-line feedback delay network, Householder feedback matrix, Hermite-interpolated
    modulated delays, one-pole damping per line, RT60 gains `g = 10^(-3 d / (RT60 fs))`, freeze.
  - `BuildingBlocks.h`: DelayLine, OnePoleLowpass, Allpass, SvfFilter (TPT), SineLfo, LinearSmoother.
- `Source/Parameters.h` - parameter IDs, ranges, text formatting. Never rename a released parameter ID.
- `Source/Presets.cpp` - factory presets (values in real units).
- `Source/PluginProcessor.*` - host interface, state save/restore, meters.
- `Source/PluginEditor.*` + `Source/gui/` - fixed 960x560 design canvas scaled to window size;
  custom LookAndFeel, decay graph, level meter.
- `tests/ReverbTests.cpp` - measured RT60 (1 kHz octave band, Schroeder/T30), sample-rate
  independence, 200-run randomized stability, transparency, freeze, stereo correlation, CPU.
  `--render <dir>` writes demo WAVs + a 32-bit float impulse response.
- `tools/plot_ir.py` - IR spectrogram + octave-band decay plot for docs.
- `.github/workflows/build.yml` - Windows/macOS-universal/Linux build, tests, pluginval strictness 10,
  auval, packaging; publishing a GitHub Release (tag `vX.Y.Z`) attaches the zips.
- `website/eighth-hall/` - static project page for marlenafabrick.net (Hostinger, upload to public_html).
  `reverb-engine.js` is a JS port of the C++ engine for the live playground (`playground.js`);
  keep it in sync with `FdnReverb.h` when the DSP changes. Samples come from `tools/make_web_samples.py`.

## Rules
- Audio thread (`processBlock`, `FdnReverb::process`): no allocation, locks, logging or file I/O.
- Keep the build warning-free under `juce_recommended_warning_flags`.
- After DSP changes, run the tests; decay accuracy must stay within 5%.
- Plugin codes: manufacturer `Fbrk`, plugin `Ehal`. License: AGPLv3 (JUCE open-source terms).
