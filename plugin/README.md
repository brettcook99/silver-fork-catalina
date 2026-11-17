# GoatTracker Ultimate plugin (VST3 / VST / AAX / AU)

The plugin is the standalone tracker running inside a DAW: the same retro
screen, keys, mouse, dialogs and SID Designer, with the DAW's tempo and
position, MIDI from the DAW's tracks, and files dragged onto its window.

| Format | Hosts | Built |
|---|---|---|
| VST3 | UA LUNA (Windows, macOS), Cakewalk Sonar, Cubase, Studio One, Reaper, Bitwig, Ableton Live 11+, FL Studio... | Windows, macOS |
| VST (VST 2) | hosts without VST3: Ableton Live 10 and older, Blue Cat PatchWork, older DAWs... | Windows, macOS |
| AU | LUNA on macOS (its default), Logic, GarageBand | macOS |
| AAX | Pro Tools (it loads nothing else) | when the AAX SDK is given |

The VST 2 version is built with the headers of FST (`plugin/vst2`), a
reverse-engineered VST 2 interface under the GPL 3, because Steinberg no
longer hands out its VST 2 SDK. FST's authors note that Steinberg's licence
terms cover the "VST" name; the plugin is free software and isn't sold.

## Using it

- Put it on an instrument track. The song is saved in the DAW's project.
- **The blue title bar** shows the song's name. A **right click** on it shows
  **LOAD, SAVE, IMPORT, EXPORT, EDIT, CLEAR** there instead (it stays until an
  item is chosen or you click elsewhere); a left click on one, or letting go of
  the right button over it, opens its dropdown: load / save a song or an
  instrument, import SID, MIDI or chipsynth C64 (.fermatax) files, export SID
  (the packer / relocator), MIDI or .fermatax, undo / redo (CMD+Z /
  CMD+SHIFT+Z, CTRL on Windows, if the DAW lets the plugin have those keys),
  clear the track (the cursor
  channel's pattern), the pattern (all three channels' patterns) or all (the
  clear dialog, everything ticked). The files are chosen in the system's own
  file dialogs, and each kind of file starts in the folder used last for it
  (remembered in goattrk2.cfg).
- **DAW SYNC** (click `SYNC` in the title bar): while the DAW plays, the song
  plays from the DAW's position at the DAW's tempo. One pattern row is one
  highlight step (1/4 beat by default), so beat 1 of bar 1 is row 0 (the
  dialog sets another start bar). The DAW's start, stop, loop and jumps
  control the playback; the tracker's own play keys work while the DAW is
  stopped. At the song's own tempo (shown in the dialog) the song sounds
  exactly as in the standalone program; at another tempo the whole C64 frame
  clock follows the DAW's.
- **MIDI**: channels 1, 2 and 3 play voices 1, 2 and 3. The plugin starts in
  jam mode. The **sustain pedal / footswitch (CC 64)** switches: pressed =
  edit mode, released = jam mode (ESC, the middle mouse button and NUMPAD . switch
  too, and the mode stays until one of them switches again; the DAW's own
  record button doesn't change it). In edit mode the notes are recorded into
  the patterns at the rows playing while the DAW plays (note-offs as `---`;
  one playback pass is one undo step), so three MIDI tracks on channels 1-3
  record three voices at once; with the song stopped they are entered at the
  cursor. (The DAW sends a track's live MIDI only while the track is armed or
  selected.) With MIDI INPUT 2 or 3 PATTERNS (bottom of the screen) the MIDI
  channel doesn't matter: the notes are shared out to 2 or 3 voices from the
  cursor's (the next free one in turn), so one DAW track can play chords.
  CTRL+SHIFT+RETURN (CMD+SHIFT+RETURN on macOS) enters a keyoff. Other
  channels and the CC knobs work as a MIDI keyboard does in the standalone
  program (CC 60 chooses the instrument, CC 90 sets the value of the effect
  picked in the middle panel).
- **Patches**: a program change (the patch chosen on a DAW's MIDI track)
  makes that MIDI channel's notes play and record with that instrument:
  patch 1 = instrument 01, patch 3 = instrument 03 ... up to 63 = 3F. So MIDI
  channel 2 with patch 3 plays voice 2 with instrument 03. Channels without
  a program change use the instrument chosen in the editor. The choices are
  saved with the project.
- **Drag & drop**: .sng, .ins, .mid, .sid and .fermatax files dropped on the
  window open as in the standalone program.
- Settings (goattrk2.cfg, recent files) are in `%APPDATA%\GoatTracker Ultimate`
  on Windows and in `~/.goattrk` (shared with the standalone program) on macOS.

Some hosts keep keys for themselves while a plugin window is open (Pro Tools
keeps SPACE for its transport, for example).

## How it is built

- `src/` — the tracker, the same sources as the standalone program. Plugin
  builds define `GT_PLUGIN`; the standalone build is unchanged.
- `src/plugin/` — the core library's plugin layer: `gtcore_api.h` (the C
  interface), `gtplugin.c` (the GoatTracker thread, input, picture, project
  state), `gtplugin_audio.c` (audio, DAW sync, MIDI recording).
- `plugin/core/` — builds the core library `gtcore.dll` / `gtcore.dylib` with
  SDL2 linked statically (`plugin/cmake/sdl2-plugin.cmake`: offscreen video,
  no audio).
- `plugin/shell/` — the JUCE 8 plugin around it: processor, editor (shows the
  core's picture, passes keys / mouse / drops), the core loader.
- `plugin/tests/` — `gtcore_render` (renders a song without a host, with a
  simulated DAW transport, MIDI and a register log `GTCORE_REGLOG=file`) and
  `gttesthost` (a small host that opens the editor).

GoatTracker keeps its song, editor and SID in global variables, so each plugin
instance copies the core library to a file of its own in the temporary folder
and loads that copy. The tracker runs on a thread of its own exactly as the
standalone program does (its main loop and dialog loops), drawing its window
into memory with SDL's software renderer; the editor shows that picture.

### Windows

Needs Visual Studio 2022 or later, CMake, Git Bash, and:
[llvm-mingw](https://github.com/mstorsjo/llvm-mingw) (UCRT x86_64),
the [SDL2 2.32.10](https://github.com/libsdl-org/SDL/releases/tag/release-2.32.10) source,
[JUCE 8.0.15](https://github.com/juce-framework/JUCE).

```
plugin/build_win.sh LLVM_MINGW=<llvm-mingw> SDL2_SRC=<SDL2-2.32.10> JUCE_DIR=<JUCE>
```

Then copy `build-plugin/shell/GoatTrackerUltimate_artefacts/Release/VST3/GoatTracker Ultimate.vst3`
to `C:\Program Files\Common Files\VST3`. The VST 2 version is
`.../Release/VST/GoatTracker Ultimate.dll` with the folder
`GoatTracker Ultimate Data` next to it (the core library as `gtcore.bin`, so
that a DAW scanning for plugins leaves it alone, and the fonts): copy both to
the DAW's VST plugin folder.

### macOS

`.github/workflows/build-plugin-macos.yml` builds universal (Intel + Apple
Silicon, macOS 10.15+) VST3, VST and AU plugins, ad-hoc signed. Copy them to
`~/Library/Audio/Plug-Ins/VST3`, `~/Library/Audio/Plug-Ins/VST` and
`~/Library/Audio/Plug-Ins/Components`.
Downloaded plugins need their quarantine flag removed
(`xattr -dr com.apple.quarantine <plugin>`) unless they are signed with a
Developer ID and notarized.

### AAX (Pro Tools)

1. Get the AAX SDK from [developer.avid.com](https://developer.avid.com) (free
   developer account).
2. Build with `AAX_SDK_PATH=<AAX SDK>` (Windows script / CMake) or the
   `AAX_SDK_URL` secret (macOS workflow).
3. Retail Pro Tools loads only AAX plugins signed with PACE's `wraptool`
   (PACE / iLok developer account). Avid's Pro Tools Developer build loads
   unsigned ones for testing.

## Licences

The tracker is GPL v2 or later; JUCE 8 is used under its AGPLv3 licence (or
its commercial / Starter licence), and the VST 2 headers (FST) are GPL 3 or
later, so the plugin is distributed with its source. The exporter's assembler (`src/asm/`) is for non-commercial use only,
as in the standalone program.
