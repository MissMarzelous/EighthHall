# Installing Eighth Hall

Eighth Hall is a free algorithmic reverb by Fabrick Audio (Marlena Fabrick).
After copying the files below, restart your DAW (or run its plugin rescan).

## Windows (64-bit)

1. Copy **`Eighth Hall.vst3`** to `C:\Program Files\Common Files\VST3\`
2. Open your DAW and rescan plugins. Eighth Hall appears under **Fabrick Audio** / **Reverb**.

`Eighth Hall.exe` is a standalone version you can run without a DAW.

## macOS (11 Big Sur or newer, Apple Silicon and Intel)

1. Copy **`Eighth Hall.vst3`** to `~/Library/Audio/Plug-Ins/VST3/`
2. Copy **`Eighth Hall.component`** (Audio Unit, for Logic Pro / GarageBand) to `~/Library/Audio/Plug-Ins/Components/`
3. Because this free build isn't notarized by Apple, macOS may block it the first
   time. Open **Terminal** and run:

   ```
   xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/"Eighth Hall.vst3"
   xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/"Eighth Hall.component"
   ```

4. Restart your DAW. In Logic, if it doesn't appear, open **Plug-in Manager** and click **Reset & Rescan Selection**.

> `~` means your home folder. In Finder, press **Cmd + Shift + G** and paste the path.

## Linux (x86-64)

- VST3: copy `Eighth Hall.vst3` to `~/.vst3/`
- LV2: copy `Eighth Hall.lv2` to `~/.lv2/`

## Supported formats and hosts

| Format | Platforms | Example hosts |
|---|---|---|
| VST3 | Windows, macOS, Linux | Ableton Live, Cubase, FL Studio, Reaper, Studio One, Bitwig |
| AU | macOS | Logic Pro, GarageBand, Ableton Live |
| LV2 | Linux | Ardour, Carla |
| Standalone | all | none needed |

Pro Tools (AAX) isn't included: AAX requires a signed developer agreement with Avid.
