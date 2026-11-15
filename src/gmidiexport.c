//
// GOATTRACKER MIDI file export: the song as a standard MIDI file (.mid)
//
// See gmidiexport.h. The player runs the song without sound (playoffline()
// in gplay.c) and tells about every note it starts, every keyoff and where
// channel 1's rows start. Each of those rows is 1/stepsize beat: the frames
// in between are spread over it, and a row of another length is a tempo
// change, so the notes stay on the beat grid and in time.
//

#include "goattrk2.h"
#include "gmidiexport.h"

#define PPQ 960                   // MIDI ticks per quarter note
#define MAXMINUTES 60             // A song that doesn't loop is cut here
#define VELOCITY 100
#define TRACKS (MAX_CHN + 1)      // The tempo track, then one per channel

typedef struct
{
  unsigned frame;
  int chn;
  int note;                       // 0-95 from C-0, -1 = keyoff
} MNOTEEVENT;

typedef struct
{
  unsigned char *data;
  int len;
  int cap;
  unsigned lasttick;
} MTRACK;

typedef struct
{
  MNOTEEVENT *events;
  int numevents, eventcap;
  unsigned *rows;                 // The frames where channel 1's rows start
  int numrows, rowcap;
  int failed;

  MTRACK track[TRACKS];
  double fps;                     // Player frames per second
  int stepsize;                   // Rows per beat (the highlighting step)
  int sounding[MAX_CHN];          // MIDI note playing on each channel + 1
  int notes;
  int tempos;
} MEXPORT;

static void *grow(void *p, int *cap, int size, int *failed)
{
  int newcap = *cap ? *cap * 2 : 1024;
  void *q = realloc(p, (size_t)newcap * size);

  if (!q)
  {
    *failed = 1;
    return p;
  }
  *cap = newcap;
  return q;
}

// The player's notes, keyoffs and frames (see playoffline())
static void hook(int c, int note, unsigned frame, void *user)
{
  MEXPORT *e = (MEXPORT *)user;

  if (e->failed) return;
  if (c < 0)
  {
    if (!note) return;
    if (e->numrows >= e->rowcap) e->rows = (unsigned *)grow(e->rows, &e->rowcap, sizeof(unsigned), &e->failed);
    if (e->numrows < e->rowcap) e->rows[e->numrows++] = frame;
    return;
  }
  if (c >= MAX_CHN) return;
  if (e->numevents >= e->eventcap) e->events = (MNOTEEVENT *)grow(e->events, &e->eventcap, sizeof(MNOTEEVENT), &e->failed);
  if (e->numevents >= e->eventcap) return;
  e->events[e->numevents].frame = frame;
  e->events[e->numevents].chn = c;
  e->events[e->numevents].note = note;
  e->numevents++;
}

// The length (frames) of channel 1's row k: the last one is as long as the
// one before it
static unsigned rowlength(MEXPORT *e, int k)
{
  if (e->numrows < 2) return 6;
  if (k >= e->numrows - 1) k = e->numrows - 2;
  return e->rows[k + 1] - e->rows[k];
}

// The MIDI tick of a frame
static unsigned frametick(MEXPORT *e, unsigned frame)
{
  int lo = 0, hi = e->numrows - 1;
  double rowticks = (double)PPQ / e->stepsize;
  unsigned len;

  if ((!e->numrows) || (frame <= e->rows[0])) return 0;
  // The last row starting at or before the frame
  while (lo < hi)
  {
    int mid = (lo + hi + 1) / 2;
    if (e->rows[mid] <= frame) lo = mid;
    else hi = mid - 1;
  }
  len = rowlength(e, lo);
  if (!len) len = 1;
  return (unsigned)(lo * rowticks + (frame - e->rows[lo]) * rowticks / len + 0.5);
}

static void put(MEXPORT *e, MTRACK *t, int byte)
{
  if (e->failed) return;
  if (t->len >= t->cap)
  {
    int cap = t->cap ? t->cap * 2 : 4096;
    unsigned char *p = (unsigned char *)realloc(t->data, cap);
    if (!p)
    {
      e->failed = 1;
      return;
    }
    t->data = p;
    t->cap = cap;
  }
  t->data[t->len++] = (unsigned char)byte;
}

static void putvlq(MEXPORT *e, MTRACK *t, unsigned value)
{
  unsigned char bytes[5];
  int n = 0;

  do
  {
    bytes[n++] = (unsigned char)(value & 0x7f);
    value >>= 7;
  } while ((value) && (n < 5));
  while (n > 1) put(e, t, bytes[--n] | 0x80);
  put(e, t, bytes[0]);
}

// The delta time before an event at tick
static void putdelta(MEXPORT *e, MTRACK *t, unsigned tick)
{
  if (tick < t->lasttick) tick = t->lasttick;
  putvlq(e, t, tick - t->lasttick);
  t->lasttick = tick;
}

static void putmeta(MEXPORT *e, MTRACK *t, unsigned tick, int type, const unsigned char *data, int len)
{
  int c;

  putdelta(e, t, tick);
  put(e, t, 0xff);
  put(e, t, type);
  putvlq(e, t, (unsigned)len);
  for (c = 0; c < len; c++) put(e, t, data[c]);
}

static void puttext(MEXPORT *e, MTRACK *t, int type, const char *text)
{
  if ((text) && (text[0])) putmeta(e, t, 0, type, (const unsigned char *)text, (int)strlen(text));
}

static void puttempo(MEXPORT *e, unsigned tick, unsigned rowlen)
{
  double uspq = 1000000.0 * e->stepsize * rowlen / e->fps;
  unsigned u = (unsigned)(uspq + 0.5);
  unsigned char d[3];

  if (u < 1) u = 1;
  if (u > 0xffffff) u = 0xffffff;
  d[0] = (unsigned char)(u >> 16);
  d[1] = (unsigned char)(u >> 8);
  d[2] = (unsigned char)u;
  putmeta(e, &e->track[0], tick, 0x51, d, 3);
  e->tempos++;
}

static void notemessage(MEXPORT *e, int c, unsigned tick, int status, int note, int velocity)
{
  MTRACK *t = &e->track[c + 1];

  putdelta(e, t, tick);
  put(e, t, status | c);
  put(e, t, note & 0x7f);
  put(e, t, velocity & 0x7f);
}

static void noteoff(MEXPORT *e, int c, unsigned tick)
{
  if (!e->sounding[c]) return;
  notemessage(e, c, tick, 0x80, e->sounding[c] - 1, 0);
  e->sounding[c] = 0;
}

static void write32(FILE *f, unsigned v)
{
  fputc((v >> 24) & 0xff, f);
  fputc((v >> 16) & 0xff, f);
  fputc((v >> 8) & 0xff, f);
  fputc(v & 0xff, f);
}

static void write16(FILE *f, unsigned v)
{
  fputc((v >> 8) & 0xff, f);
  fputc(v & 0xff, f);
}

int midiexport(const char *path, char *report, int reportsize)
{
  MEXPORT *e;
  FILE *f;
  int loopframe[MAX_CHN];
  unsigned endtick[MAX_CHN];
  unsigned frames, maxframes, songend = 0;
  unsigned rowticks;
  int c, k, ok = 0;
  int cut = 0;

  report[0] = 0;
  e = (MEXPORT *)calloc(1, sizeof(MEXPORT));
  if (!e)
  {
    snprintf(report, reportsize, "Out of memory");
    return 0;
  }
  e->fps = editorInfo.ntsc ? 60.0 : 50.0;
  if (editorInfo.multiplier) e->fps *= editorInfo.multiplier;
  else e->fps /= 2.0;
  e->stepsize = (stepsize > 0) ? stepsize : 4;
  rowticks = PPQ / e->stepsize;

  maxframes = (unsigned)(e->fps * 60.0 * MAXMINUTES);
  frames = playoffline(editorInfo.esnum, maxframes, hook, e, loopframe);

  // Where each channel ends: a row after it came to the end of its
  // orderlist (the last row's note starts there), or where the run stopped
  for (c = 0; c < MAX_CHN; c++)
  {
    if (loopframe[c] >= 0) endtick[c] = frametick(e, (unsigned)loopframe[c]) + rowticks;
    else
    {
      endtick[c] = frametick(e, frames);
      if (frames >= maxframes) cut = 1;
    }
    if (endtick[c] > songend) songend = endtick[c];
  }

  // The tempo track: names, 4/4 and a tempo for each row length
  puttext(e, &e->track[0], 0x03, songname[0] ? songname : (loadedsongfilename[0] ? gt_basename(loadedsongfilename) : "GoatTracker song"));
  puttext(e, &e->track[0], 0x02, authorname);
  {
    unsigned char ts[4] = { 4, 2, 24, 8 };
    putmeta(e, &e->track[0], 0, 0x58, ts, 4);
  }
  {
    unsigned last = 0;
    for (k = 0; (k < e->numrows) || (!k); k++)
    {
      unsigned len = rowlength(e, k);
      unsigned tick = k * rowticks;
      if (tick >= songend) break;
      if (len != last) puttempo(e, tick, len);
      last = len;
    }
  }
  putmeta(e, &e->track[0], songend, 0x2f, NULL, 0);

  // The channels' notes
  for (c = 0; c < MAX_CHN; c++)
  {
    char name[32];
    snprintf(name, sizeof name, "SID channel %d", c + 1);
    puttext(e, &e->track[c + 1], 0x03, name);
  }
  for (k = 0; k < e->numevents; k++)
  {
    MNOTEEVENT *ev = &e->events[k];
    unsigned tick = frametick(e, ev->frame);

    c = ev->chn;
    if (tick > endtick[c]) continue;
    noteoff(e, c, tick);
    if ((ev->note >= 0) && (tick < endtick[c]))
    {
      int n = ev->note + 12;
      if (n > 127) n = 127;
      notemessage(e, c, tick, 0x90, n, VELOCITY);
      e->sounding[c] = n + 1;
      e->notes++;
    }
  }
  for (c = 0; c < MAX_CHN; c++)
  {
    noteoff(e, c, endtick[c]);
    putmeta(e, &e->track[c + 1], songend, 0x2f, NULL, 0);
  }

  if (e->failed)
    snprintf(report, reportsize, "Out of memory");
  else if (!e->notes)
    snprintf(report, reportsize, "The song has no notes");
  else if (!(f = fopen(path, "wb")))
    snprintf(report, reportsize, "Can't write %s", gt_basename(path));
  else
  {
    int seconds;

    fwrite("MThd", 4, 1, f);
    write32(f, 6);
    write16(f, 1);
    write16(f, TRACKS);
    write16(f, PPQ);
    for (c = 0; c < TRACKS; c++)
    {
      fwrite("MTrk", 4, 1, f);
      write32(f, (unsigned)e->track[c].len);
      fwrite(e->track[c].data, e->track[c].len, 1, f);
    }
    if (fclose(f))
      snprintf(report, reportsize, "Can't write %s", gt_basename(path));
    else
    {
      // The length: up to the last channel's end
      unsigned endframe = 0;
      for (c = 0; c < MAX_CHN; c++)
      {
        unsigned fe = (loopframe[c] >= 0) ? (unsigned)loopframe[c] + rowlength(e, e->numrows) : frames;
        if (fe > endframe) endframe = fe;
      }
      if ((e->numrows) && (endframe > e->rows[0])) endframe -= e->rows[0];
      seconds = (int)(endframe / e->fps + 0.5);
      snprintf(report, reportsize, "Exported %s: %d notes, %d:%02d%s%s", gt_basename(path), e->notes, seconds / 60, seconds % 60,
        e->tempos > 1 ? ", with tempo changes" : "", cut ? " (the song doesn't end: cut after an hour)" : "");
      ok = 1;
    }
  }

  for (c = 0; c < TRACKS; c++) free(e->track[c].data);
  free(e->events);
  free(e->rows);
  free(e);
  return ok;
}
