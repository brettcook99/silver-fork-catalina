#ifndef GSOUND_H
#define GSOUND_H

#ifdef GSOUND_C

#ifdef __WIN32__
#include <winioctl.h>
#else
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include "cwsid.h"
#endif

#endif

#define MINMIXRATE 11025
#define MAXMIXRATE 48000
#define MINBUF 5
#define MAXBUF 2000
// 32 ms at 48 kHz, as in Scott's settings (the output buffer itself is 32
// samples by default, see audiobuffer in goattrk2.c). The old defaults:
// 100 ms (4096 samples: notes played from the keyboard or MIDI were heard up
// to 0.2 s late), then 25 ms (1024 samples), then 15 ms. On macOS (Core
// Audio, through SDL) at least two 15 ms buffers are queued whatever is set
#define DEFAULTBUF 32
// The defaults before, still in old config files
#define OLDDEFAULTBUF 100
#define PREVDEFAULTBUF 25
#define DEFAULTMIXRATE 48000

#define PALFRAMERATE 50
#define PALCLOCKRATE 985248
#define NTSCFRAMERATE 60
#define NTSCCLOCKRATE 1022727

#define MIXBUFFERSIZE 65536

int sound_init(unsigned b, unsigned mr, unsigned writer, unsigned hardsid, unsigned m, unsigned ntsc, unsigned multiplier, unsigned catweasel, unsigned interpolate, unsigned customclockrate);
void sound_uninit(void);
void sound_suspend(void);
// Keeps the sound from calling the player between the two (the player's
// state changes, see stopsong())
void sound_lockplayer(void);
void sound_unlockplayer(void);
#ifndef GT_PLUGIN
void sound_closeoutput(void);
#endif
void sound_flush(void);

#endif
