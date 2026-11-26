//
// GOATTRACKER v2 MIDI input
//
// The MIDI input device chosen in Preferences (or every one) is opened and
// its note on/off and control change messages from all channels are put into
// a queue, which the editor reads once per frame. So are the note on / off
// messages of the MIDI controller device chosen there (the transport
// buttons), as MIDI_TRANSPORT: they play nothing (its knobs work as the
// input device's). The sustain pedal (CC 64) holds note offs. Notes are played at once as they are queued, by the
// editor's note hook (midi_setnotehook()): waiting for the editor's next
// frame made them up to a frame late, unevenly.
// The MIDI system calls us from its own thread, so the queue (and the
// hook's state, under the same lock) is the only thing shared between the
// threads.
//

#define GMIDI_C

#include <stdio.h>
#include <string.h>
#include <SDL.h>
#include "gmidi.h"

unsigned midiinput = 1;
char midi_inputdevice[MIDI_DEVICENAMELEN] = "";
char midi_controllerdevice[MIDI_DEVICENAMELEN] = "";

#if defined(__APPLE__) || defined(__WIN32__) || defined(_WIN32)
#define MIDI_SUPPORTED
#endif

#ifdef MIDI_SUPPORTED

#define MIDI_QUEUESIZE 256
#define MIDI_MAXDEVICES 64

static volatile int midiqueue[MIDI_QUEUESIZE];
// When each event arrived (SDL_GetTicks() milliseconds), and what the note
// hook made of it
static volatile unsigned midiqueuetime[MIDI_QUEUESIZE];
static volatile int midiqueueassign[MIDI_QUEUESIZE];
static unsigned midilasttime = 0;
static int midilastassign = 0;
static int (*midi_notehook)(int type, int channel, int note, unsigned time) = NULL;
static int midilastvalue = 0;
static int midilastchannel = 0;
static volatile unsigned midiqueuewrite = 0;
static volatile unsigned midiqueueread = 0;
static volatile int midiqueuelock = 0;
static int mididevices = 0;

// Called from the MIDI thread(s) (in the plugin from the host's audio
// thread). Events are dropped if the queue is full
void midi_pushevent_ch(int type, int channel, int data1, int data2)
{
  midi_pushevent_at(type, channel, data1, data2, SDL_GetTicks());
}

// The same, for an event that came at time (SDL_GetTicks() milliseconds)
void midi_pushevent_at(int type, int channel, int data1, int data2, unsigned time)
{
  unsigned next;
  int assign = 0;

  while (__sync_lock_test_and_set(&midiqueuelock, 1))
    ;
  // The note sounds now (even if the queue is full)
  if ((midi_notehook) && ((type == MIDI_NOTEON) || (type == MIDI_NOTEOFF)))
    assign = midi_notehook(type, channel & 0xf, data1 & 0x7f, time);
  next = (midiqueuewrite + 1) % MIDI_QUEUESIZE;
  if (next != midiqueueread)
  {
    midiqueue[midiqueuewrite] = (type << 20) | ((channel & 0xf) << 16) | ((data1 & 0x7f) << 8) | (data2 & 0x7f);
    midiqueuetime[midiqueuewrite] = time;
    midiqueueassign[midiqueuewrite] = assign;
    __sync_synchronize();
    midiqueuewrite = next;
  }
  __sync_lock_release(&midiqueuelock);
}

void midi_setnotehook(int (*hook)(int type, int channel, int note, unsigned time))
{
  midi_lock();
  midi_notehook = hook;
  midi_unlock();
}

void midi_lock(void)
{
  while (__sync_lock_test_and_set(&midiqueuelock, 1))
    ;
}

void midi_unlock(void)
{
  __sync_lock_release(&midiqueuelock);
}

int midi_eventassign(void)
{
  return midilastassign;
}

// What a device is opened for (see midi_roleof())
#define MIDIROLE_INPUT 1
#define MIDIROLE_CONTROLLER 2

// The pedal let go of notes: their note offs now
static void midi_sustainrelease(int channel, int value)
{
  unsigned char notes[128];
  int n, k;

  midi_lock();
  n = midi_sustainpedal(channel, value, notes);
  midi_unlock();
  for (k = 0; k < n; k++) midi_pushevent_ch(MIDI_NOTEOFF, channel, notes[k], 0);
}

// A message of the input device (or the test hooks): notes, with the
// sustain pedal (CC 64) holding note offs, and knobs
void midi_inputmessage(int status, int data1, int data2)
{
  int channel = status & 0x0f;
  int held;

  switch (status & 0xf0)
  {
    case 0x90:
    if (data2)
    {
      midi_lock();
      midi_sustainnoteon(channel, data1);
      midi_unlock();
      midi_pushevent_ch(MIDI_NOTEON, channel, data1, data2);
      break;
    }
    // Note on with velocity 0 is a note off
    case 0x80:
    midi_lock();
    held = midi_sustainnoteoff(channel, data1);
    midi_unlock();
    if (!held) midi_pushevent_ch(MIDI_NOTEOFF, channel, data1, 0);
    break;

    case 0xb0:
    if (data1 == 64) midi_sustainrelease(channel, data2);
    else midi_pushevent_ch(MIDI_CONTROL, channel, data1, data2);
    break;
  }
}

static void midi_handlemessage(int role, int status, int data1, int data2)
{
  int channel = status & 0x0f;

  // The controller device: its buttons (notes, velocity 0 = let go) and its
  // knobs (control changes, as the input device's), not notes to play
  if (role == MIDIROLE_CONTROLLER)
  {
    if ((status & 0xf0) == 0x90) midi_pushevent_ch(MIDI_TRANSPORT, channel, data1, data2);
    else if ((status & 0xf0) == 0x80) midi_pushevent_ch(MIDI_TRANSPORT, channel, data1, 0);
    else if ((status & 0xf0) == 0xb0)
    {
      if (data1 == 64) midi_sustainrelease(channel, data2);
      else midi_pushevent_ch(MIDI_CONTROL, channel, data1, data2);
    }
    return;
  }
  midi_inputmessage(status, data1, data2);
}

int midi_getevent(int *note)
{
  int event;

  if (midiqueueread == midiqueuewrite) return 0;
  __sync_synchronize();
  event = midiqueue[midiqueueread];
  midilasttime = midiqueuetime[midiqueueread];
  midilastassign = midiqueueassign[midiqueueread];
  __sync_synchronize();
  midiqueueread = (midiqueueread + 1) % MIDI_QUEUESIZE;

  *note = (event >> 8) & 0x7f;
  midilastvalue = event & 0x7f;
  midilastchannel = (event >> 16) & 0xf;
  return event >> 20;
}

int midi_eventchannel(void)
{
  return midilastchannel;
}

unsigned midi_eventtime(void)
{
  return midilasttime;
}

int midi_eventvalue(void)
{
  return midilastvalue;
}

int midi_devicecount(void)
{
  return mididevices;
}

// What the device of this name is opened for: the controller device's
// buttons, the input device's notes (every device's if none is chosen), or
// nothing (0: not opened)
static int midi_roleof(const char *name)
{
  if ((midi_controllerdevice[0]) && (!strcmp(name, midi_controllerdevice))) return MIDIROLE_CONTROLLER;
  if ((!midi_inputdevice[0]) || (!strcmp(name, midi_inputdevice))) return MIDIROLE_INPUT;
  return 0;
}

#ifdef __APPLE__

//
// macOS: CoreMIDI
//

#include <CoreMIDI/CoreMIDI.h>

typedef struct
{
  unsigned char status;
  unsigned char data[2];
  int count;
  int role;
} MIDIPARSER;

static MIDIPARSER midiparser[MIDI_MAXDEVICES];

// Parses a raw MIDI byte stream (with running status) into messages
static void midi_parsebyte(MIDIPARSER *p, unsigned char b)
{
  if (b >= 0xf8) return; // Realtime messages can appear anywhere; ignore

  if (b & 0x80)
  {
    // System common & sysex messages cancel running status
    p->status = (b < 0xf0) ? b : 0;
    p->count = 0;
    return;
  }

  if (!p->status) return; // Sysex data or data without a status byte

  p->data[p->count++] = b;
  if ((p->status & 0xf0) == 0xc0 || (p->status & 0xf0) == 0xd0)
  {
    // Program change & channel pressure have one data byte
    if (p->count < 1) return;
  }
  else
  {
    if (p->count < 2) return;
  }
  p->count = 0;
  midi_handlemessage(p->role, p->status, p->data[0], p->data[1]);
}

static MIDIClientRef midiclient = 0;
static MIDIPortRef midiport = 0;
static MIDIEndpointRef midisources[MIDI_MAXDEVICES];

static void midi_coremidiread(const MIDIPacketList *list, void *portref, void *srcref)
{
  MIDIPARSER *p = srcref ? (MIDIPARSER *)srcref : &midiparser[0];
  const MIDIPacket *packet = &list->packet[0];
  UInt32 c;
  UInt16 d;

  (void)portref;

  for (c = 0; c < list->numPackets; c++)
  {
    for (d = 0; d < packet->length; d++)
      midi_parsebyte(p, packet->data[d]);
    packet = MIDIPacketNext(packet);
  }
}

// A MIDI source's name, as Audio MIDI Setup shows it
static void midi_sourcename(MIDIEndpointRef src, int index, char *name, int size)
{
  CFStringRef s = NULL;

  name[0] = 0;
  if ((MIDIObjectGetStringProperty(src, kMIDIPropertyDisplayName, &s) == noErr) && (s))
  {
    if (!CFStringGetCString(s, name, size, kCFStringEncodingUTF8)) name[0] = 0;
    CFRelease(s);
  }
  if (!name[0]) snprintf(name, size, "MIDI input %d", index + 1);
}

int midi_listdevices(char (*names)[MIDI_DEVICENAMELEN], int max)
{
  ItemCount c, sources = MIDIGetNumberOfSources();
  int n = 0;

  for (c = 0; (c < sources) && (n < max); c++)
  {
    MIDIEndpointRef src = MIDIGetSource(c);
    if (!src) continue;
    midi_sourcename(src, (int)c, names[n++], MIDI_DEVICENAMELEN);
  }
  return n;
}

// (Re)connects the input port to the MIDI sources chosen (see midi_roleof())
static void midi_connectall(void)
{
  ItemCount c, sources;
  int d;

  for (d = 0; d < mididevices; d++)
    MIDIPortDisconnectSource(midiport, midisources[d]);
  mididevices = 0;

  sources = MIDIGetNumberOfSources();
  for (c = 0; (c < sources) && (mididevices < MIDI_MAXDEVICES); c++)
  {
    MIDIEndpointRef src = MIDIGetSource(c);
    char name[MIDI_DEVICENAMELEN];
    int role;

    if (!src) continue;
    midi_sourcename(src, (int)c, name, sizeof name);
    role = midi_roleof(name);
    if (!role) continue;

    memset(&midiparser[mididevices], 0, sizeof(MIDIPARSER));
    midiparser[mididevices].role = role;
    if (MIDIPortConnectSource(midiport, src, &midiparser[mididevices]) == noErr)
    {
      midisources[mididevices] = src;
      mididevices++;
    }
  }
}

// Called on the main thread's run loop (which SDL runs) when devices are
// plugged in or removed
static void midi_coremidinotify(const MIDINotification *message, void *refcon)
{
  (void)refcon;

  if (message->messageID == kMIDIMsgSetupChanged)
    midi_connectall();
}

int midi_init(void)
{
  if (midiclient) return mididevices;

  if (MIDIClientCreate(CFSTR("GoatTracker"), midi_coremidinotify, NULL, &midiclient) != noErr)
  {
    midiclient = 0;
    return 0;
  }
  if (MIDIInputPortCreate(midiclient, CFSTR("GoatTracker Input"), midi_coremidiread, NULL, &midiport) != noErr)
  {
    MIDIClientDispose(midiclient);
    midiclient = 0;
    midiport = 0;
    return 0;
  }

  midi_connectall();
  return mididevices;
}

void midi_uninit(void)
{
  if (!midiclient) return;

  // Disposing the client also closes its port and connections
  MIDIClientDispose(midiclient);
  midiclient = 0;
  midiport = 0;
  mididevices = 0;
}

void midi_setdevices(void)
{
  if (midiclient) midi_connectall();
  else if (midiinput) midi_init();
}

#else

//
// Windows: WinMM
//

#include <windows.h>
#include <mmsystem.h>

static HMIDIIN midihandles[MIDI_MAXDEVICES];

// The device's role (midi_roleof()) is its instance value
static void CALLBACK midi_winmmcallback(HMIDIIN handle, UINT msg, DWORD_PTR instance, DWORD_PTR param1, DWORD_PTR param2)
{
  (void)handle;
  (void)param2;

  // WinMM delivers whole short messages, with running status expanded
  if (msg == MIM_DATA)
    midi_handlemessage((int)instance, param1 & 0xff, (param1 >> 8) & 0x7f, (param1 >> 16) & 0x7f);
}

// A MIDI input device's name, as Windows gives it
static void midi_devicename(UINT device, char *name, int size)
{
  MIDIINCAPSA caps;

  memset(&caps, 0, sizeof caps);
  if ((midiInGetDevCapsA(device, &caps, sizeof caps) == MMSYSERR_NOERROR) && (caps.szPname[0]))
    snprintf(name, size, "%s", caps.szPname);
  else
    snprintf(name, size, "MIDI input %u", device + 1);
}

int midi_listdevices(char (*names)[MIDI_DEVICENAMELEN], int max)
{
  UINT c, devices = midiInGetNumDevs();
  int n = 0;

  for (c = 0; (c < devices) && (n < max); c++)
    midi_devicename(c, names[n++], MIDI_DEVICENAMELEN);
  return n;
}

int midi_init(void)
{
  UINT c, devices;

  if (mididevices) return mididevices;

  devices = midiInGetNumDevs();
  for (c = 0; (c < devices) && (mididevices < MIDI_MAXDEVICES); c++)
  {
    HMIDIIN handle;
    char name[MIDI_DEVICENAMELEN];
    int role;

    // Only the devices chosen in Preferences (others stay free for other
    // programs)
    midi_devicename(c, name, sizeof name);
    role = midi_roleof(name);
    if (!role) continue;
    if (midiInOpen(&handle, c, (DWORD_PTR)midi_winmmcallback, (DWORD_PTR)role, CALLBACK_FUNCTION) != MMSYSERR_NOERROR)
      continue; // In use by another program, for example
    if (midiInStart(handle) != MMSYSERR_NOERROR)
    {
      midiInClose(handle);
      continue;
    }
    midihandles[mididevices++] = handle;
  }
  return mididevices;
}

void midi_uninit(void)
{
  int c;

  for (c = 0; c < mididevices; c++)
  {
    midiInStop(midihandles[c]);
    midiInReset(midihandles[c]);
    midiInClose(midihandles[c]);
  }
  mididevices = 0;
}

void midi_setdevices(void)
{
  midi_uninit();
  if (midiinput) midi_init();
}

#endif

#else

//
// Other platforms: no MIDI input
//

void midi_inputmessage(int status, int data1, int data2)
{
  (void)status;
  (void)data1;
  (void)data2;
}

int midi_init(void)
{
  return 0;
}

void midi_uninit(void)
{
}

int midi_listdevices(char (*names)[MIDI_DEVICENAMELEN], int max)
{
  (void)names;
  (void)max;
  return 0;
}

void midi_setdevices(void)
{
}

int midi_getevent(int *note)
{
  (void)note;
  return 0;
}

void midi_pushevent_ch(int type, int channel, int data1, int data2)
{
  (void)type;
  (void)channel;
  (void)data1;
  (void)data2;
}

void midi_pushevent_at(int type, int channel, int data1, int data2, unsigned time)
{
  (void)type;
  (void)channel;
  (void)data1;
  (void)data2;
  (void)time;
}

int midi_eventchannel(void)
{
  return 0;
}

unsigned midi_eventtime(void)
{
  return 0;
}

int midi_eventvalue(void)
{
  return 0;
}

int midi_devicecount(void)
{
  return 0;
}

void midi_setnotehook(int (*hook)(int type, int channel, int note, unsigned time))
{
  (void)hook;
}

int midi_eventassign(void)
{
  return 0;
}

void midi_lock(void)
{
}

void midi_unlock(void)
{
}

#endif

// The sustain pedal (MIDI CC 64), each MIDI channel's own, as on a synth:
// while it is down a key let go keeps its note sounding, and the notes go
// off when it comes up (a key struck again meanwhile gets its own note off).
// A pedal may send many values while it is down: only crossing 64 counts.
// A DAW that stops sends 0, which lets go of what the pedal still held
static unsigned char sustaindown[16];
static unsigned char sustainheld[16][128];

int midi_sustainnoteoff(int channel, int note)
{
  channel &= 0xf;
  if (!sustaindown[channel]) return 0;
  sustainheld[channel][note & 0x7f] = 1;
  return 1;
}

void midi_sustainnoteon(int channel, int note)
{
  sustainheld[channel & 0xf][note & 0x7f] = 0;
}

int midi_sustainpedal(int channel, int value, unsigned char *notes)
{
  int down = value >= 64;
  int n = 0, k;

  channel &= 0xf;
  if (down == sustaindown[channel]) return 0;
  sustaindown[channel] = down;
  if (down) return 0;
  for (k = 0; k < 128; k++)
  {
    if (sustainheld[channel][k])
    {
      sustainheld[channel][k] = 0;
      notes[n++] = k;
    }
  }
  return n;
}

int midi_relativesteps(int value)
{
  value &= 0x7f;
  if ((value > 0) && (value < 64)) return value;
  if (value > 64) return -(value - 64);
  return 0;
}
