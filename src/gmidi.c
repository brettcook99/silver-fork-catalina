//
// GOATTRACKER v2 MIDI input
//
// Every MIDI input device is opened and note on/off messages from all
// channels are put into a queue, which the editor reads once per frame.
// The MIDI system calls us from its own thread, so the queue is the only
// thing shared between the threads.
//

#define GMIDI_C

#include <string.h>
#include <SDL.h>
#include "gmidi.h"

unsigned midiinput = 1;

#if defined(__APPLE__) || defined(__WIN32__) || defined(_WIN32)
#define MIDI_SUPPORTED
#endif

#ifdef MIDI_SUPPORTED

#define MIDI_QUEUESIZE 256
#define MIDI_MAXDEVICES 64

static volatile int midiqueue[MIDI_QUEUESIZE];
// When each event arrived (SDL_GetTicks() milliseconds)
static volatile unsigned midiqueuetime[MIDI_QUEUESIZE];
static unsigned midilasttime = 0;
static volatile unsigned midiqueuewrite = 0;
static volatile unsigned midiqueueread = 0;
static volatile int midiqueuelock = 0;
static int mididevices = 0;

// Called from the MIDI thread(s). Events are dropped if the queue is full
static void midi_pushevent(int type, int note)
{
  unsigned next;

  while (__sync_lock_test_and_set(&midiqueuelock, 1))
    ;
  next = (midiqueuewrite + 1) % MIDI_QUEUESIZE;
  if (next != midiqueueread)
  {
    midiqueue[midiqueuewrite] = (type << 8) | (note & 0x7f);
    midiqueuetime[midiqueuewrite] = SDL_GetTicks();
    __sync_synchronize();
    midiqueuewrite = next;
  }
  __sync_lock_release(&midiqueuelock);
}

static void midi_handlemessage(int status, int data1, int data2)
{
  switch (status & 0xf0)
  {
    case 0x90:
    // Note on with velocity 0 is a note off
    midi_pushevent(data2 ? MIDI_NOTEON : MIDI_NOTEOFF, data1);
    break;

    case 0x80:
    midi_pushevent(MIDI_NOTEOFF, data1);
    break;
  }
}

int midi_getevent(int *note)
{
  int event;

  if (midiqueueread == midiqueuewrite) return 0;
  __sync_synchronize();
  event = midiqueue[midiqueueread];
  midilasttime = midiqueuetime[midiqueueread];
  __sync_synchronize();
  midiqueueread = (midiqueueread + 1) % MIDI_QUEUESIZE;

  *note = event & 0x7f;
  return event >> 8;
}

unsigned midi_eventtime(void)
{
  return midilasttime;
}

int midi_devicecount(void)
{
  return mididevices;
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
  midi_handlemessage(p->status, p->data[0], p->data[1]);
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

// (Re)connects the input port to every MIDI source in the system
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
    if (!src) continue;

    memset(&midiparser[mididevices], 0, sizeof(MIDIPARSER));
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

#else

//
// Windows: WinMM
//

#include <windows.h>
#include <mmsystem.h>

static HMIDIIN midihandles[MIDI_MAXDEVICES];

static void CALLBACK midi_winmmcallback(HMIDIIN handle, UINT msg, DWORD_PTR instance, DWORD_PTR param1, DWORD_PTR param2)
{
  (void)handle;
  (void)instance;
  (void)param2;

  // WinMM delivers whole short messages, with running status expanded
  if (msg == MIM_DATA)
    midi_handlemessage(param1 & 0xff, (param1 >> 8) & 0x7f, (param1 >> 16) & 0x7f);
}

int midi_init(void)
{
  UINT c, devices;

  if (mididevices) return mididevices;

  devices = midiInGetNumDevs();
  for (c = 0; (c < devices) && (mididevices < MIDI_MAXDEVICES); c++)
  {
    HMIDIIN handle;

    if (midiInOpen(&handle, c, (DWORD_PTR)midi_winmmcallback, 0, CALLBACK_FUNCTION) != MMSYSERR_NOERROR)
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

#endif

#else

//
// Other platforms: no MIDI input
//

int midi_init(void)
{
  return 0;
}

void midi_uninit(void)
{
}

int midi_getevent(int *note)
{
  (void)note;
  return 0;
}

unsigned midi_eventtime(void)
{
  return 0;
}

int midi_devicecount(void)
{
  return 0;
}

#endif
