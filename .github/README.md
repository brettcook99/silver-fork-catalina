# GoatTracker Ultimate (v2.82)
##### GoatTracker 2 with a modern or the classic retro interface, Macbook related fixes and improvements
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

### GoatTracker Ultimate: modern or retro interface

- **Choose at startup**: a window asks for the **Modern** interface (below) or the **Retro** one, the classic 100x37 GoatTracker 2 text screen in its original font and colors, scaled sharply to the window, with its mouse controls (click to edit, right/middle-drag to mark, right-click the title bar for the menu, the status line toggles). Tick *Don't ask again* to keep the choice; hold Shift (or Option on macOS) while starting, use View > Ask for Interface at Next Launch, or the `-u` option (`-u0` ask, `-u1` modern, `-u2` retro) to choose again. Both interfaces edit the same songs with the same keys.
- The retro interface keeps the new version's features: the macOS menu bar, native open/save panels, drag & drop, SID / MIDI / chipsynth import and export. Dialogs added by the new version (save changes, quit, clear, hard restart ADSR, export, SID import) appear as pop-ups over the text screen. CTRL/CMD + / - step through the classic window sizes, F12 shows the classic help screen.
- **Numpad** (both interfaces): 1-8 go to pattern rows 00, 08, 16 ... 56; `.` switches between edit and jam mode; CTRL+1/2/3 put the cursor on track 1, 2 or 3; 0 plays the pattern / stops.
- **Mouse wheel** scrolls the pattern in both interfaces.
- **Double-click a .sng or .sid in the Finder**: GoatTracker Ultimate opens it and starts playing (a SID is converted with the last import options, no dialog). A file dropped on the window is only opened.
- F10 in the instrument editor also loads chipsynth C64 `.fermatax` patches.
- The app is now called **GoatTracker Ultimate** (menu bar, window title, `GoatTracker Ultimate.app`). Settings stay in `~/.goattrk/`.

### Instrument designer

A full-window instrument editor that looks like a Commodore 64 screen (80x50 characters in the C64's font style, colors and border, `READY.` and a blinking cursor) and is laid out like the patch page of Plogue's chipsynth C64.

- **Open it from anywhere** with CMD+D (CTRL+D on Windows / Linux) or ALT/OPTION+numpad 5, the rainbow button in the control bar, View > Instrument Designer, *Open in Instrument Designer* in the Library's right-click menu or the Inspector, or *C64* in the retro interface's right-click menu bar. ESC (RUN/STOP), the *TRACKER* tab or the same keys go back; the tracker is left on the instrument you designed.
- **A patch has up to three oscillators**, each a GoatTracker instrument played on SID voice 1, 2 or 3 at the same time, so sync and ring modulation between oscillators work as in chipsynth. The oscillators of the patch `BASS` are the instruments named `BASS:1`, `BASS:2` and `BASS:3`; an instrument without such a name is a one-oscillator patch. Click an oscillator's light to add it (a copy of the first, in the next empty instrument) or to mute it while playing.
- **PATCH page**: per oscillator the waveforms, FILT / SYNC / RING / TEST, hard restart and legato, pulse width and ADSR bars, transpose or fixed pitch, vibrato and gate timer; the filter's cutoff, resonance, type and voices, a chip display (click to switch 6581 / 8580, PAL / NTSC) and a graph of the cutoff sweep.
- **WAVE, PULSE and FILTER pages**: the table rows as steps — waveform bits and arpeggio notes per step, pulse width and filter sweeps with a graph of what the player will do, the loop point, wave presets (arpeggios, drums) and sweep makers. A *SHARED* table part can be given its own copy.
- **EMU page**: SID chip, clock, player speed, hard restart ADSR, reSID mode, a live view of the SID registers in machine-language-monitor style and the voices.
- A clickable keyboard plays the patch; so do the note keys and a MIDI keyboard. F1-F4 play and stop the song, CMD/CTRL+Z undoes. Every change is an ordinary instrument / table edit, so it saves with the song and shows in the tracker.
- The characters are built in (not Commodore's ROM). A C64 character ROM file named `chargen` (e.g. from VICE) in the settings folder is used instead.

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
- **SID import**: File > Import > SID File (CMD+SHIFT+I), or drop / double-click a .sid file. A dialog shows the tune's title, author, subtunes and chip. SIDs exported by GoatTracker are turned back into their song exactly (all subtunes, patterns, instruments and tables; only instrument names and unused data are lost). Any other SID is played on an emulated C64, every subtune (up to 32) or just the one chosen, and its notes and sounds are written into a new song: a starting point for editing, not an exact copy. Where a tune starts over, the song ends there and loops back, so whole tunes fit (each subtune is played up to 10 minutes to find the loop). The import sets the speed multiplier, PAL/NTSC and hard restart to play the song at the SID's speed; a player running at a speed GoatTracker doesn't have (such as a 35 Hz CIA timer) gets the nearest one, with each subtune's tempo fitted to it. How the import went is shown in the status bar. The command line tool `sid2sng` does the same (`sid2sng tune.sid tune.sng [-exact|-approx] [-sN subtune] [-tSECONDS] [-noloop]`).
- **chipsynth C64 presets**: File > Import > chipsynth C64 Patch, or drop / double-click a .fermatax file. Each oscillator that makes a sound becomes an instrument (the first in the current slot, the others in the next empty ones, named `NAME:1`..`NAME:3` so that they form one patch in the instrument designer): ADSR, waveform with sync and ring, coarse tune (wavetable) and pulse width (pulsetable). chipsynth's one filter (type, resonance, cutoff) becomes a filtertable on the first filtered oscillator's instrument, routing the voices of all filtered oscillators (oscillator n plays on voice n). Step sequencers, modulators, effects and fine tune have no GoatTracker equivalent and are left out; a report says what was skipped. CMD+Z undoes the import.

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
