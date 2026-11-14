# GoatTracker Ultimate plugin (VST3 / AAX / AU)

The plugin is the standalone tracker running inside a DAW: the same retro
screen, keys, mouse, dialogs and SID Designer, with the DAW's tempo and
position, MIDI from the DAW's tracks, and files dragged onto its window.

| Format | Hosts | Built |
|---|---|---|
| VST3 | UA LUNA (Windows, macOS), Cakewalk Sonar, Cubase, Reaper, Bitwig... | Windows, macOS |
| AU | LUNA on macOS (its default), Logic, GarageBand | macOS |
| AAX | Pro Tools (it loads nothing else) | when the AAX SDK is given |

## Using it

- Put it on an instrument track. The song is saved in the DAW's project; the
  tracker's own Load / Save still work for .sng files.
- **DAW SYNC** (click `SYNC` in the title bar): while the DAW plays, the song
  plays from the DAW's position at the DAW's tempo. One pattern row is one
  highlight step (1/4 beat by default), so beat 1 of bar 1 is row 0 (the
  dialog sets another start bar). The DAW's start, stop, loop and jumps
  control the playback; the tracker's own play keys work while the DAW is
  stopped. At the song's own tempo (shown in the dialog) the song sounds
  exactly as in the standalone program; at another tempo the whole C64 frame
  clock follows the DAW's.
- **MIDI**: channels 1, 2 and 3 play voices 1, 2 and 3. In edit mode, while the
  song plays, the notes are recorded into the patterns at the rows playing
  (note-offs as `---`; one recording pass is one undo step), so three MIDI
  tracks on channels 1-3 record three voices at once. With the song stopped
  they are entered at the cursor. Other channels and the CC knobs work as a
  MIDI keyboard does in the standalone program.
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
to `C:\Program Files\Common Files\VST3`.

### macOS

`.github/workflows/build-plugin-macos.yml` builds universal (Intel + Apple
Silicon, macOS 10.15+) VST3 and AU plugins, ad-hoc signed. Copy them to
`~/Library/Audio/Plug-Ins/VST3` and `~/Library/Audio/Plug-Ins/Components`.
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
its commercial / Starter licence), so the plugin is distributed with its
source. The exporter's assembler (`src/asm/`) is for non-commercial use only,
as in the standalone program.
