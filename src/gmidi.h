#ifndef GMIDI_H
#define GMIDI_H

// MIDI input: notes played on a MIDI keyboard work like the note keys.
// Supported on macOS (CoreMIDI) and Windows (WinMM). On other platforms
// midi_init() finds no devices and MIDI input stays off.

#define MIDI_NOTEON 1
#define MIDI_NOTEOFF 2
#define MIDI_CONTROL 3

// Opens every MIDI input device. Returns the number of devices opened.
// On macOS devices plugged in later are picked up automatically.
int midi_init(void);
void midi_uninit(void);

// Gets the next received event. Returns 0 if there is none, otherwise
// MIDI_NOTEON or MIDI_NOTEOFF with the MIDI note number (0-127) in *note, or
// MIDI_CONTROL with the controller number (0-127) in *note
int midi_getevent(int *note);

// The velocity (note on) or value (control change, 0-127) of the event
// midi_getevent() returned last
int midi_eventvalue(void);

// When the event midi_getevent() returned last arrived, in milliseconds
// (SDL_GetTicks())
unsigned midi_eventtime(void);

// A function that handles each note on / off as it is queued, in the thread
// that queues it (the MIDI input's), so that
// the note sounds at once instead of when the editor gets to it. Its
// return value is kept with the event: midi_eventassign() for the event
// midi_getevent() returned last (0 for events it didn't see)
void midi_setnotehook(int (*hook)(int type, int channel, int note, unsigned time));
int midi_eventassign(void);
// Holds the queue (and the note hook) while the editor changes what the
// hook uses
void midi_lock(void);
void midi_unlock(void);

// Number of MIDI input devices currently open
int midi_devicecount(void);

// A relative (endless) knob's control change value as steps: 1-63 = that
// many steps up, 65-127 = (value - 64) steps down, 0 and 64 = none
int midi_relativesteps(int value);

#ifndef GMIDI_C
extern unsigned midiinput;
#endif

#endif
