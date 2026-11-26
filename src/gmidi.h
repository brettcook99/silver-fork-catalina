#ifndef GMIDI_H
#define GMIDI_H

// MIDI input: notes played on a MIDI keyboard work like the note keys.
// Supported on macOS (CoreMIDI) and Windows (WinMM). On other platforms
// midi_init() finds no devices and MIDI input stays off.

#define MIDI_NOTEON 1
#define MIDI_NOTEOFF 2
#define MIDI_CONTROL 3
// A button of the MIDI controller device (a note: value = velocity, 0 = let
// go), see midi_controllerdevice
#define MIDI_TRANSPORT 4

// Device names (as the system gives them) are at most this long
#define MIDI_DEVICENAMELEN 256

// Opens the MIDI input devices: the input device (or every one) and the
// controller device (see midi_inputdevice). Returns the number of devices
// opened. On macOS devices plugged in later are picked up automatically.
int midi_init(void);
void midi_uninit(void);

// The names of the MIDI input devices in the system (at most max). Returns
// how many
int midi_listdevices(char (*names)[MIDI_DEVICENAMELEN], int max);
// Opens the devices again after midi_inputdevice / midi_controllerdevice
// changed (if MIDI input is on)
void midi_setdevices(void);

// Gets the next received event. Returns 0 if there is none, otherwise
// MIDI_NOTEON or MIDI_NOTEOFF with the MIDI note number (0-127) in *note,
// MIDI_CONTROL with the controller number (0-127) in *note, or
// MIDI_TRANSPORT (the controller device's note on / off) with the note
int midi_getevent(int *note);

// The velocity (note on) or value (control change, 0-127) of the event
// midi_getevent() returned last
int midi_eventvalue(void);

// The MIDI channel (0-15) of the event midi_getevent() returned last
int midi_eventchannel(void);

// Queues an event as if it came from a MIDI device (the plugin's MIDI from
// the host). Safe from one other thread
void midi_pushevent_ch(int type, int channel, int data1, int data2);
// The same for an event that came earlier, at time (SDL_GetTicks())
void midi_pushevent_at(int type, int channel, int data1, int data2, unsigned time);

// When the event midi_getevent() returned last arrived, in milliseconds
// (SDL_GetTicks())
unsigned midi_eventtime(void);

// A function that handles each note on / off as it is queued, in the thread
// that queues it (the MIDI input's, or the plugin's audio thread), so that
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
// Preferences: the device whose notes and knobs are used ("" = every
// device), and the controller device whose buttons are the transport ("" =
// none). The controller device never plays notes; a device chosen as both
// is the controller
extern char midi_inputdevice[MIDI_DEVICENAMELEN];
extern char midi_controllerdevice[MIDI_DEVICENAMELEN];
#endif

#endif
