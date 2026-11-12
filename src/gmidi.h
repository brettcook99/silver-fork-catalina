#ifndef GMIDI_H
#define GMIDI_H

// MIDI input: notes played on a MIDI keyboard work like the note keys.
// Supported on macOS (CoreMIDI) and Windows (WinMM). On other platforms
// midi_init() finds no devices and MIDI input stays off.

#define MIDI_NOTEON 1
#define MIDI_NOTEOFF 2

// Opens every MIDI input device. Returns the number of devices opened.
// On macOS devices plugged in later are picked up automatically.
int midi_init(void);
void midi_uninit(void);

// Gets the next received note event. Returns 0 if there is none, otherwise
// MIDI_NOTEON or MIDI_NOTEOFF with the MIDI note number (0-127) in *note
int midi_getevent(int *note);

// When the event midi_getevent() returned last arrived, in milliseconds
// (SDL_GetTicks())
unsigned midi_eventtime(void);

// Number of MIDI input devices currently open
int midi_devicecount(void);

#ifndef GMIDI_C
extern unsigned midiinput;
#endif

#endif
