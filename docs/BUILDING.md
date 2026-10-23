# Building Eighth Hall from source - a beginner's guide

You don't need to know C++ or JUCE to build this. You need three free tools:
**Git** (downloads/uploads code), **CMake** (prepares the build), and a
**C++ compiler** (turns code into a plugin). CMake downloads JUCE for you.

---

## 1. Install the tools (one time)

### Windows
1. **Visual Studio 2022 Community** (free): <https://visualstudio.microsoft.com/vs/community/>
   In the installer, tick **"Desktop development with C++"**. That gives you the compiler *and* CMake.
2. **Git for Windows**: <https://git-scm.com/download/win> (defaults are fine).

### macOS
1. Install **Xcode** from the App Store, open it once, then in Terminal run:
   `xcode-select --install`
2. Install **Homebrew** (<https://brew.sh>), then: `brew install cmake git`

### Linux (Ubuntu/Debian)
```
sudo apt install build-essential cmake git ninja-build libasound2-dev libx11-dev \
  libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev libxrandr-dev \
  libxrender-dev libfreetype-dev libfontconfig1-dev libgl1-mesa-dev
```

---

## 2. Get the code

Open a terminal (Windows: **"Developer PowerShell for VS 2022"** from the Start menu) and run:

```
git clone https://github.com/MissMarzelous/EighthHall.git
cd EighthHall
```

## 3. Build it

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

The first run takes several minutes (it downloads and compiles JUCE). Later builds
only recompile what you changed.

Your plugins appear in **`build/EighthHall_artefacts/Release/`**:

| Folder | What it is |
|---|---|
| `VST3/Eighth Hall.vst3` | VST3 plugin (all platforms) |
| `AU/Eighth Hall.component` | Audio Unit (Mac) |
| `LV2/Eighth Hall.lv2` | LV2 plugin (Linux) |
| `Standalone/` | App you can run without a DAW |

Install them as described in [INSTALL.md](INSTALL.md). To have the build copy the
plugin into your plugin folder automatically, change `COPY_PLUGIN_AFTER_BUILD`
to `TRUE` in `CMakeLists.txt`.

## 4. Run the tests

```
ctest --test-dir build -C Release --output-on-failure
```

This runs `tests/ReverbTests.cpp`, which measures the reverb like lab equipment
would: decay accuracy, stability, CPU use. To also render demo audio files:

```
build/EighthHallTests --render demo-audio          (Mac/Linux)
build\Release\EighthHallTests.exe --render demo-audio   (Windows)
```
(Create the `demo-audio` folder first.)

---

## 5. Working on the code in an IDE

An IDE lets you edit, build, and debug with buttons instead of commands.

- **Windows (easiest):** in Visual Studio choose **File -> Open -> Folder** and pick the
  project folder. Wait for "CMake generation finished" in the Output panel, pick
  **EighthHall_Standalone.exe** from the dropdown next to the green Run button, and press **F5**.
  Built plugins land in `out\build\x64-Debug\EighthHall_artefacts\`.
- **Windows (solution file):** `cmake -B build-vs -G "Visual Studio 17 2022"` then open `build-vs/EighthHall.sln`.
  Right-click **EighthHall_Standalone** -> *Set as Startup Project* -> press **F5** to run it.
- **Mac:** `cmake -B build-xcode -G Xcode` then open `build-xcode/EighthHall.xcodeproj`.
  Choose the **EighthHall_Standalone** scheme and press **Cmd+R**.
- **Any OS:** Visual Studio Code (with the "CMake Tools" extension) or CLion can open
  the folder directly.

**Tip - testing without a DAW:** the Standalone app is the fastest way to hear
your changes. For testing as a real plugin, JUCE includes *AudioPluginHost*
(in `build/_deps/juce-src/extras/AudioPluginHost`).

### Where things are

```
Source/
  dsp/BuildingBlocks.h   delay lines, filters, allpasses, LFOs (plain C++)
  dsp/FdnReverb.h        the reverb engine (plain C++, no JUCE)
  Parameters.h           every knob: ID, range, default, display text
  Presets.cpp            factory presets
  PluginProcessor.*      connects the DAW to the engine
  PluginEditor.*         the window and its layout
  gui/                   look & feel, decay graph, meters
tests/ReverbTests.cpp    automated measurements
```

Good first experiments:
- Add a preset in `Presets.cpp`.
- Change the accent colour in `gui/EighthHallLookAndFeel.h`.
- Change the delay lengths in `dsp/FdnReverb.h` (`baseLineMs`) and listen to how
  the character changes. Then run the tests to make sure decay is still accurate.

---

## 6. Publishing to GitHub

1. On github.com, click **New repository**, name it `EighthHall`, leave it empty (no README).
2. In the project folder:
   ```
   git init
   git add .
   git commit -m "Eighth Hall reverb v1.0.0"
   git branch -M main
   git remote add origin https://github.com/MissMarzelous/EighthHall.git
   git push -u origin main
   ```
3. Open the **Actions** tab on GitHub. GitHub builds Windows, Mac and Linux
   versions for you, runs the tests and pluginval, and attaches the results.
4. To publish downloadable installers as a **Release**: on your repo page click
   **Releases -> Draft a new release**, type `v1.0.0` in **Choose a tag** (choose
   "create new tag"), give it a title, and click **Publish release**. About 20 minutes
   later the Windows, Mac and Linux zips are attached to that release - your
   website's Download button points there.

When you change the version, update `VERSION` in the `project(...)` line of
`CMakeLists.txt` and publish a release with a matching `vX.Y.Z` tag.

---

## Not included (and why)

- **AAX / Pro Tools:** needs Avid's developer program and PACE code-signing.
- **macOS notarization:** needs a paid Apple Developer account ($99/yr). Without it,
  users run the one-line `xattr` command in INSTALL.md.
- **Windows code signing:** needs a paid certificate; unsigned VST3s load fine in DAWs.
