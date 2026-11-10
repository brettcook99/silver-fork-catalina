# GoatTracker v2.82
##### Macbook related fixes and improvements
\
Editor by Lasse Öörni (loorni@gmail.com)  
HardSID 4U support by Téli Sándor.  
Uses reSID engine by Dag Lem.  
Uses reSID distortion / nonlinearity by Antti Lankila.  
Uses 6510 crossassembler from Exomizer2 beta by Magnus Lind.  
Uses the SDL library.
User interface built with [Dear ImGui](https://github.com/ocornut/imgui) by Omar Cornut (MIT license).
Fonts: [Inter](https://rsms.me/inter/) by Rasmus Andersson and [JetBrains Mono](https://www.jetbrains.com/lp/mono/) (SIL Open Font License).  
GoatTracker icon by Antonio Vera.  
Command quick reference by Simon Bennett.  
Patches and further development by Stefan A. Haubenthal, Valerio Cannone, Raine M. Ekman,
Tero Lindeman, Henrik Paulini and Groepaz.  
Microtonal support by Birgit Jauernig.  
Fixes and additions by [Joel Ricci](https://github.com/joelricci).<br>
Undo system by Jason Page.

---
Distributed under GNU General Public License
(see the file COPYING for details)  

Covert BitOps homepage:
http://covertbitops.c64.org  

GoatTracker 2 SourceForge.net page:
http://sourceforge.net/projects/goattracker2  

<br>

-- See **build instructions for  [MAC](../build_instructions_macos.txt)** ! --

<br>

## Revision history

This fork is based on the official above version of Goat Tracker. See [readme.txt](../readme.txt) for full history.

### New user interface

The 100x37 text screen has been replaced by a modern interface in the style of a DAW like Logic Pro (Dear ImGui on SDL2). Every keyboard command still works as before.

- **Control bar**: transport buttons (play from start, play from position, play pattern, stop, record = edit mode, follow), an LCD display (time, position, row, subtune, octave, instrument) and the engine settings (PAL/NTSC, 6581/8580, speed, hard restart ADSR, reSID interpolation and filter) with voice activity meters.
- **Arrangement**: the three orderlists as track lanes. Patterns are regions as long as they play, with a preview of their notes, so what plays at the same time lines up; transpose and repeat commands are chips. Click to select, double-click to edit a pattern, right-click for commands, CMD+wheel to zoom. A playhead follows the song.
- **Pattern Editor**: three channel columns with colored notes, instruments and commands, row highlighting, the playing row in yellow, mute/solo buttons. Click a cell to move the cursor, drag to select, SHIFT+click to extend, right-click for transpose, cut/copy/paste, insert/delete, split/join etc.
- **Library & Inspector**: a searchable instrument list (double-click to rename, hold the speaker to hear it) and an inspector with envelope knobs and graph, waveform buttons, the table pointers with jump buttons, vibrato and gate/hard restart settings.
- **Tables**: the wave, pulse, filter and speed tables side by side; hovering a row explains what it does; the current instrument's rows are highlighted.
- **Song**: name, author and copyright, editing and export options.
- Panels can be moved and docked anywhere (View > Reset Layout restores the default). The layout is saved in `~/.goattrk/imgui.ini`.
- **Mouse wheel / trackpad** scroll every panel without moving the edit cursor. Scrolling the pattern editor while the song plays pauses follow playback until you click the Follow button or stop.
- **Files (macOS)**: File > Open / Save As use the macOS open and save panels, and Open Recent lists recent files. Songs (.sng), instruments (.ins), SID, MIDI and chipsynth files can be dropped on the window or double-clicked in the Finder. A song with unsaved changes asks whether to save it first. Export (CMD+E, F9) has a dialog for the format, addresses and player options and then a save panel.
- Text size: CMD + = / - / 0. Full screen: CTRL+CMD+F or ALT+ENTER.
- Help (F12, CMD+?) is a searchable window.
- **SID import**: File > Import > SID File (CMD+SHIFT+I), or drop / double-click a .sid file. A dialog shows the tune's title, author, subtunes and chip. SIDs exported by GoatTracker are turned back into their song exactly (all subtunes, patterns, instruments and tables; only instrument names and unused data are lost). Any other SID is played on an emulated C64 and its notes and sounds are written into a new song: a starting point for editing, not an exact copy. The command line tool `sid2sng` does the same (`sid2sng tune.sid tune.sng [-exact|-approx] [-sN subtune] [-tSECONDS]`).
- **chipsynth C64 presets**: File > Import > chipsynth C64 Patch, or drop / double-click a .fermatax file. Each oscillator that makes a sound becomes an instrument (the first in the current slot, the others in the next empty ones): ADSR, waveform with sync and ring, coarse tune (wavetable), pulse width (pulsetable) and the filter type, resonance and cutoff when the oscillator goes through the filter (filtertable). Step sequencers, modulators, effects and fine tune have no GoatTracker equivalent and are left out; a report says what was skipped. CMD+Z undoes the import.

Earlier changes in this fork:

- Undo edits using CTRL/CMD+Z (Thanks to Jason Page)
- Toggle ReSID interpolation and filter patch (distortion) mode by clicking INT/FP or press SHIFT+F9.
- Fine vibrato (FV) setting is now saved to config.
- Macbook keyboard insert support using ALT/OPTION+BACKSPACE or FN+SHIFT+BACKSPACE.
- Deleting rows now works correctly on a Macbook keyboard
- Delete using fn + backspace on a Macbook keyboard
- Backspace works as expected when entering filenames
- Better support for some USB keyboards (unwanted characters)
- Build instructions for MacOS (see separate [txt file](../build_instructions_macos.txt))
- Builds on Intel macOS 11 and later
- Resizable window: drag the window edges (or use the green button / ALT+ENTER for fullscreen) and the whole screen scales to fit with sharp text, keeping its proportions.
- Text size: CTRL/CMD + = (bigger), CTRL/CMD + - (smaller), CTRL/CMD + 0 (default size), or A- / A+ in the right-click top bar menu. In fullscreen the screen always fills the display.
- The window size is saved to the config and restored on the next start. The default size fits the desktop.
- Uses the system mouse pointer, so it stays sharp at any size.
- ALT/OPTION+ENTER no longer also enters a keyoff into the pattern.
- MIDI keyboard input (macOS and Windows): notes played on a MIDI keyboard work like the note keys. In the pattern editor they are entered in edit mode and played in jam mode; in the other editors they play the current instrument. Releasing the key releases the note. MIDI note 60 (middle C) is C-4. All MIDI devices and channels are used, and on macOS devices plugged in while the tracker runs are picked up automatically. Turn it off with the `-m0` option or in the config file.
- macOS menu bar with File, Edit, Control and Help menus. Every menu item also has a CMD key shortcut:
  - Control: Play From Beginning CMD+1, Play From Current Position CMD+2, Play Pattern CMD+3, Stop CMD+4, Go To Pattern Editor CMD+5, Song Editor CMD+6, Instrument/Table Editor CMD+7, Songname Editor CMD+8, Export Song CMD+9 (CMD+1 to CMD+9 work like F1 to F9, so no fn key is needed), Record (edit mode) CMD+R, Follow Playback CMD+F, Play / Stop.
  - Edit: Undo CMD+Z, Cut CMD+X, Copy CMD+C, Paste CMD+V (SHIFT+X, C, V in every editor), Delete, Select All CMD+A (marks the whole pattern or orderlist).
  - File: Load CMD+O, Merge Song SHIFT+CMD+O, Save CMD+S, Import MIDI File CMD+I. Help: the online help (F12).
- Numpad 0 plays from the start of the pattern and stops (SHIFT+numpad 0 plays with the opposite follow mode). In the pattern editor numpad 1-7 go to rows 8, 16, 24, 32, 40, 48 and 56; while playing with follow on, playback jumps there too. Numpad 8 and 9 still enter digits.
- SPACE in the pattern editor starts playing from the cursor and stops. CMD+R switches between edit mode (record) and jam mode, which SPACE used to do. SHIFT+SPACE still plays from the cursor without following.
- Follow playback is on by default: while the song plays, the pattern editor scrolls with it so the row being played stays on screen. CMD+F (or Control > Follow Playback) turns it off and on, SHIFT+F1-F3 plays the other way once. The setting is saved in the config file (`-f0` turns it off from the command line).
- MIDI file import: File > Import MIDI File (CMD+I) converts a standard MIDI file (.mid) to a song and loads it. The command line tool `midi2sng` does the same: `midi2sng song.mid song.sng [options]` (run it without arguments for the options: rows per beat, pattern length, transpose, MIDI channels, drums). The notes are shared out to the 3 SID channels; when more than 3 notes play at once, the highest and lowest new notes win and the rest are left out. Every MIDI channel gets a simple instrument of its own (pulse, saw, triangle, bass), and the MIDI tempo becomes the song tempo.
