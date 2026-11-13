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
// 15 ms = 662 samples at 44.1 kHz. On macOS (Core Audio, through SDL) two
// such buffers are queued, 30 ms; shorter ones would make SDL queue more of
// them. The old defaults: 100 ms (4096 samples: notes played from the
// keyboard or MIDI were heard up to 0.2 s late), then 25 ms (1024 samples,
// rounded down to a power of two: 46 ms queued)
#define DEFAULTBUF 15
// The defaults before, still in old config files
#define OLDDEFAULTBUF 100
#define PREVDEFAULTBUF 25
#define DEFAULTMIXRATE 44100

#define PALFRAMERATE 50
#define PALCLOCKRATE 985248
#define NTSCFRAMERATE 60
#define NTSCCLOCKRATE 1022727

#define MIXBUFFERSIZE 65536

int sound_init(unsigned b, unsigned mr, unsigned writer, unsigned hardsid, unsigned m, unsigned ntsc, unsigned multiplier, unsigned catweasel, unsigned interpolate, unsigned customclockrate);
void sound_uninit(void);
void sound_suspend(void);
void sound_flush(void);

#endif
