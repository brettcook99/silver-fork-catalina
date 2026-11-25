/*
 * GoatTracker: ProTracker modules (.mod), import and export
 *
 * The format as ProTracker 2.3 reads and writes it (the pt2-clone sources,
 * github.com/8bitbubsy/pt2-clone, were the reference): a 20-byte title, 31
 * sample headers of 30 bytes (name, length in words, finetune, volume 0-64,
 * loop start and length in words), the song length, a restart byte, 128
 * pattern numbers, the tag "M.K." (more than 64 patterns: "M!K!") at 1080,
 * the patterns (64 rows of 4 bytes per channel: sample number in the high
 * nibbles of bytes 0 and 2, the Amiga period in the rest of bytes 0-1, the
 * effect in the low nibble of byte 2 and its parameter in byte 3), then the
 * samples (signed 8-bit). Old Soundtracker modules have 15 samples and no
 * tag; xCHN / xxCH / FLT8 / OKTA modules more channels (up to 8 here).
 *
 * Import: each GoatTracker track gets one module channel, or two merged
 * (where both have a note on a row the lower channel's note plays, counted
 * as lost). Periods become notes, ProTracker's C-1 GoatTracker's C-3 (a
 * sample with a 32-byte cycle sounds at that pitch; a sample looping one
 * cycle of 8-256 bytes is moved by its octaves). The samples become
 * instruments of the same number and name: noise for noisy samples, a saw
 * for looped ones, a decaying pulse for the others; the sound itself can't
 * be taken over. Speed / BPM (Fxx) becomes the tempo (frames per row at
 * 50 Hz: speed * 125 / BPM); portamento (1xx, 2xx), tone portamento (3xx,
 * 5xy) and vibrato (4xy, 6xy) become commands 1-4 with note-relative speed
 * table entries (a halfstep's frequency step shifted, near the module's
 * period step); C00 and EC0 on a row without a note a keyoff. Pattern breaks
 * (Dxx) and position jumps (Bxx) end their pattern there; a jump back is the
 * song's loop. Arpeggios, volume and the other effects are left out and
 * counted.
 *
 * Export: the current subtune's tracks 1-3 as channels 1-3 (channel 4 has
 * the speed commands and the last pattern's break). Every track is followed
 * through its orderlist (transposes, repeats) until the longest one comes
 * to its end; shorter ones go round again, as GoatTracker plays them. Rows
 * are cut into 64-row patterns (equal ones shared). Each instrument used
 * gets a sample: one looped cycle of the waveform its wavetable settles on
 * (pulse with its first pulse width), or 1 KB of noise. The cycle is 32 bytes, or 8-128 for
 * an instrument whose notes fit ProTracker's three octaves better an octave
 * or two higher or lower (the notes are written so that they sound right). Keyoffs become C00, tempo
 * commands speed (Fxx), commands 1-4 their ProTracker effects; the other
 * commands are left out and counted.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include "goattrk2.h"
#include "gmod.h"
#ifdef GT_PLUGIN
#include "plugin/gtplugin.h"
#endif

#define MODROWS 64
#define MOD_MAXFILE (8 * 1024 * 1024)
#define MOD_MAXPOS 128
#define MOD_MAXPATT 128
#define PTPERIODS 36
// GoatTracker's note (0 = C-0) for ProTracker's C-1
#define PTNOTE0 36
// Note-relative speed table entry (left side bit 7)
#define SPEED_NOTEREL 0x80
// A halfstep's period step relative to the period: 1 - 2^(-1/12)
#define HALFSTEP 0.05612568731830647
#define GTNOTES (LASTNOTE - FIRSTNOTE + 1)

// ProTracker's periods (finetune 0), C-1 to B-3
static const unsigned short ptperiod[PTPERIODS] =
{
  856, 808, 762, 720, 678, 640, 604, 570, 538, 508, 480, 453,
  428, 404, 381, 360, 339, 320, 302, 285, 269, 254, 240, 226,
  214, 202, 190, 180, 170, 160, 151, 143, 135, 127, 120, 113
};

typedef struct
{
  char name[23];
  int length;               // bytes
  int volume;               // 0-64
  int loopstart, looplength;
  const signed char *data;  // NULL if the file has none
  int datalen;              // bytes there (the file may be cut short)
  int oct;                  // a looped cycle of 8-256 bytes: octaves below a 32-byte one
} MODSAMPLE;

typedef struct
{
  unsigned char *d;
  int len;
  int channels;
  int nsamples;
  int songlen;
  unsigned char order[MOD_MAXPOS];
  int npatt;
  const unsigned char *patt;
  MODSAMPLE smp[32];        // 1-31
  char title[21];
} MODFILE;

typedef struct
{
  int arp;          // arpeggios
  int volume;       // volume changes and slides
  int effects;      // other effects
  int busy;         // effects on rows that had one already
  int merged;       // notes of a merged channel on a row with a note
  int notes;        // notes out of range (moved by octaves)
  int tempo;        // tempos the player can't do (clamped)
  int breaks;       // breaks to a row other than 0, jumps ahead
  int patterns;     // patterns that didn't fit
  int tables;       // speed table rows that didn't fit
  int samples;      // instruments past 31 (export)
  int rows;         // rows past the module's 128 positions (export)
} MODLOST;

static void reportlost(const MODLOST *l, char *text, int size)
{
  int n = (int)strlen(text);

#define LOSTPART(count, what) \
  if ((count) && (n < size - 1)) \
    n += snprintf(text + n, size - n, "%s%d %s", strchr(text, '(') ? ", " : " (left out: ", (count), (what));

  LOSTPART(l->arp, "arpeggios");
  LOSTPART(l->volume, "volume effects");
  LOSTPART(l->effects, "other effects");
  LOSTPART(l->busy, "effects on rows with one already");
  LOSTPART(l->merged, "notes of merged channels");
  LOSTPART(l->notes, "notes out of range (moved by octaves)");
  LOSTPART(l->tempo, "tempos out of range");
  LOSTPART(l->breaks, "breaks to a row / jumps ahead");
  LOSTPART(l->patterns, "patterns");
  LOSTPART(l->tables, "speed table rows");
  LOSTPART(l->samples, "instruments past 31");
  LOSTPART(l->rows, "rows past 128 positions");
#undef LOSTPART
  if ((strchr(text, '(')) && (n < size - 1)) snprintf(text + n, size - n, ")");
}

static int clampint(int v, int lo, int hi)
{
  return (v < lo) ? lo : (v > hi) ? hi : v;
}

static int readfile(const char *path, unsigned char **data, int *len)
{
  FILE *f = fopen(path, "rb");
  long n;

  if (!f) return 0;
  fseek(f, 0, SEEK_END);
  n = ftell(f);
  fseek(f, 0, SEEK_SET);
  if ((n <= 0) || (n > MOD_MAXFILE))
  {
    fclose(f);
    return 0;
  }
  *data = (unsigned char *)malloc(n);
  if (!*data)
  {
    fclose(f);
    return 0;
  }
  if (fread(*data, 1, n, f) != (size_t)n)
  {
    fclose(f);
    free(*data);
    return 0;
  }
  fclose(f);
  *len = (int)n;
  return 1;
}

// A name from the file: printable characters, no spaces at the end
static void cleanname(const unsigned char *s, int n, char *out)
{
  int c;

  for (c = 0; c < n; c++)
  {
    if (!s[c]) break;
    out[c] = ((s[c] >= 32) && (s[c] < 127)) ? (char)s[c] : ' ';
  }
  out[c] = 0;
  while ((c > 0) && (out[c - 1] == ' ')) out[--c] = 0;
}

/*
 * Reading a module
 */

static int modparse(MODFILE *m, char *report, int reportsize)
{
  const unsigned char *d = m->d;
  int len = m->len;
  int hdr, c, ofs, s;

  m->channels = 0;
  m->nsamples = 31;
  if (len >= 1084)
  {
    char tag[5];

    memcpy(tag, d + 1080, 4);
    tag[4] = 0;
    if ((!strcmp(tag, "M.K.")) || (!strcmp(tag, "M!K!")) || (!strcmp(tag, "M&K!")) || (!strcmp(tag, "FLT4")) ||
        (!strcmp(tag, "4CHN")) || (!strcmp(tag, "N.T.")))
      m->channels = 4;
    else if ((!strcmp(tag, "FLT8")) || (!strcmp(tag, "OKTA")) || (!strcmp(tag, "OCTA")) || (!strcmp(tag, "CD81")))
      m->channels = 8;
    else if ((tag[0] >= '1') && (tag[0] <= '9') && (!strcmp(tag + 1, "CHN")))
      m->channels = tag[0] - '0';
    else if ((isdigit((unsigned char)tag[0])) && (isdigit((unsigned char)tag[1])) && (tag[2] == 'C') && ((tag[3] == 'H') || (tag[3] == 'N')))
      m->channels = (tag[0] - '0') * 10 + (tag[1] - '0');
  }
  if (!m->channels)
  {
    // No tag: a Soundtracker module with 15 samples, if it looks like one
    m->nsamples = 15;
    m->channels = 4;
    if ((len < 600 + 1024) || (d[470] < 1) || (d[470] > 128))
    {
      snprintf(report, reportsize, "Not a ProTracker module");
      return 0;
    }
    for (c = 0; c < 128; c++)
    {
      if (d[472 + c] >= 64)
      {
        snprintf(report, reportsize, "Not a ProTracker module");
        return 0;
      }
    }
    for (s = 0; s < 15; s++)
    {
      if (d[20 + s * 30 + 25] > 64)
      {
        snprintf(report, reportsize, "Not a ProTracker module");
        return 0;
      }
    }
  }
  if (m->channels > MOD_MAXCHN)
  {
    snprintf(report, reportsize, "A module with %d channels: up to %d can be imported", m->channels, MOD_MAXCHN);
    return 0;
  }
  hdr = 20 + m->nsamples * 30;
  m->songlen = d[hdr];
  memcpy(m->order, d + hdr + 2, MOD_MAXPOS);
  if ((m->songlen < 1) || (m->songlen > MOD_MAXPOS))
  {
    snprintf(report, reportsize, "The module's song length (%d) isn't 1-128", m->songlen);
    return 0;
  }
  m->npatt = 0;
  for (c = 0; c < MOD_MAXPOS; c++)
  {
    if (m->order[c] >= MOD_MAXPATT) m->order[c] = 0;
    if (m->order[c] >= m->npatt) m->npatt = m->order[c] + 1;
  }
  ofs = hdr + 2 + MOD_MAXPOS + ((m->nsamples == 31) ? 4 : 0);
  m->patt = d + ofs;
  ofs += m->npatt * MODROWS * m->channels * 4;
  if (ofs > len)
  {
    snprintf(report, reportsize, "The file is cut short (its patterns are incomplete)");
    return 0;
  }
  memset(m->smp, 0, sizeof m->smp);
  for (s = 1; s <= m->nsamples; s++)
  {
    const unsigned char *h = d + 20 + (s - 1) * 30;
    MODSAMPLE *sm = &m->smp[s];

    cleanname(h, 22, sm->name);
    sm->length = ((h[22] << 8) | h[23]) * 2;
    sm->volume = (h[25] > 64) ? 64 : h[25];
    sm->loopstart = ((h[26] << 8) | h[27]) * 2;
    sm->looplength = ((h[28] << 8) | h[29]) * 2;
    if (ofs < len)
    {
      sm->data = (const signed char *)(d + ofs);
      sm->datalen = (len - ofs < sm->length) ? len - ofs : sm->length;
    }
    ofs += sm->length;
    // A loop of one cycle (a power of two of 8-256 bytes, as chip music
    // and GoatTracker's own exports have): its octave is known
    sm->oct = 0;
    if ((sm->looplength >= 8) && (sm->looplength <= 256) && (!(sm->looplength & (sm->looplength - 1))) &&
        (sm->loopstart + sm->looplength <= sm->length))
    {
      int l = sm->looplength;
      while (l > 32)
      {
        l >>= 1;
        sm->oct++;
      }
      while (l < 32)
      {
        l <<= 1;
        sm->oct--;
      }
    }
  }
  cleanname(d, 20, m->title);
  return 1;
}

static int readmod(const char *path, MODFILE *m, char *report, int reportsize)
{
  memset(m, 0, sizeof *m);
  if (!readfile(path, &m->d, &m->len))
  {
    snprintf(report, reportsize, "The file can't be read (or is larger than 8 MB)");
    return 0;
  }
  if (!modparse(m, report, reportsize))
  {
    free(m->d);
    m->d = NULL;
    return 0;
  }
  return 1;
}

static void cell(const MODFILE *m, int p, int row, int ch, int *period, int *smp, int *fx, int *par)
{
  const unsigned char *c = m->patt + ((p * MODROWS + row) * m->channels + ch) * 4;

  *smp = (c[0] & 0xf0) | (c[2] >> 4);
  *period = ((c[0] & 0x0f) << 8) | c[1];
  *fx = c[2] & 0x0f;
  *par = c[3];
}

// GoatTracker's note (0 = C-0) for an Amiga period
static int periodnote(int period)
{
  double semis = 12.0 * log(428.0 / period) / log(2.0);
  return PTNOTE0 + 12 + (int)floor(semis + 0.5);
}

int mod_scan(const char *path, MODSCAN *scan, char *report, int reportsize)
{
  MODFILE m;
  int i, r, ch, s;
  int counted[MOD_MAXPATT];

  memset(scan, 0, sizeof *scan);
  if (!readmod(path, &m, report, reportsize)) return 0;
  strcpy(scan->name, m.title);
  scan->channels = m.channels;
  scan->positions = m.songlen;
  scan->patterns = m.npatt;
  for (s = 1; s <= m.nsamples; s++)
  {
    if (m.smp[s].length > 2) scan->samples++;
  }
  // The notes of the patterns in the song (each once)
  memset(counted, 0, sizeof counted);
  for (i = 0; i < m.songlen; i++)
  {
    int p = m.order[i];
    if (counted[p]) continue;
    counted[p] = 1;
    for (r = 0; r < MODROWS; r++)
    {
      for (ch = 0; ch < m.channels; ch++)
      {
        int period, smp, fx, par;
        cell(&m, p, r, ch, &period, &smp, &fx, &par);
        if (period) scan->notes[ch]++;
      }
    }
  }
  free(m.d);
  return 1;
}

void mod_defaultoptions(const MODSCAN *scan, MODIMPORT_OPTIONS *o)
{
  int t, c, n = 0;
  int chosen[MOD_MAXCHN];

  memset(o, 0, sizeof *o);
  if ((!scan) || (scan->channels <= 3))
  {
    for (t = 0; t < 3; t++) o->source[t] = ((!scan) || (t < scan->channels)) ? 1u << t : 0;
    return;
  }
  // The three channels with the most notes (of equal ones the first)
  memset(chosen, 0, sizeof chosen);
  for (t = 0; t < 3; t++)
  {
    int best = -1;
    for (c = 0; c < scan->channels; c++)
    {
      if ((!chosen[c]) && ((best < 0) || (scan->notes[c] > scan->notes[best]))) best = c;
    }
    chosen[best] = 1;
  }
  for (c = 0; c < scan->channels; c++)
  {
    if (chosen[c]) o->source[n++] = 1u << c;
  }
}

/*
 * Import
 */

static int tlen[MAX_TABLES];

static int addtablerow(int t, int l, int r)
{
  if (tlen[t] >= MAX_TABLELEN - 1) return 0;
  ltable[t][tlen[t]] = (unsigned char)l;
  rtable[t][tlen[t]] = (unsigned char)r;
  return ++tlen[t];
}

static int speedentry(int l, int r, MODLOST *lost)
{
  int c, e;

  for (c = 0; c < tlen[STBL]; c++)
  {
    if ((ltable[STBL][c] == l) && (rtable[STBL][c] == r)) return c + 1;
  }
  e = addtablerow(STBL, l, r);
  if (!e) lost->tables++;
  return e;
}

// The shift of a note-relative speed: halfsteps per frame as 2^-shift
static int relshift(double halfsteps)
{
  if (halfsteps <= 0.0) return 7;
  return clampint((int)floor(-log(halfsteps) / log(2.0) + 0.5), 0, 7);
}

// ProTracker slide amount (period units per tick) at a period as a speed
// table entry
static int slideentry(int period, int amount, MODLOST *lost)
{
  if (amount < 1) amount = 1;
  if (period < 1) period = 428;
  return speedentry(SPEED_NOTEREL, relshift(amount / (HALFSTEP * period)), lost);
}

// ProTracker vibrato (speed x: 64 steps a cycle, depth y: about 2*y period
// units each way) as a speed table entry: the frames until the direction
// changes, the note-relative step
static int vibentry(int period, int x, int y, MODLOST *lost)
{
  int frames = clampint((int)(16.0 / (x ? x : 1) + 0.5), 1, 0x7f);
  double amp;

  if (period < 1) period = 428;
  amp = 2.0 * y / (HALFSTEP * period);
  return speedentry(SPEED_NOTEREL | frames, relshift(amp / frames), lost);
}

typedef struct
{
  int period;       // the note playing (for the slides' speeds)
  int lastsmp;
  int tonespeed;    // speed table entry of the last tone portamento
  int vibx, viby;   // the last vibrato
} CHSTATE;

typedef struct
{
  int note;         // 0 = none, else a note, KEYOFF
  int instr;
  int cmd, data;
} GTROW;

// A module channel's row as GoatTracker's
static void convrow(const MODFILE *m, int p, int r, int ch, CHSTATE *st, GTROW *o, MODLOST *lost)
{
  int period, smp, fx, par;
  int oldperiod = st->period;

  cell(m, p, r, ch, &period, &smp, &fx, &par);
  memset(o, 0, sizeof *o);
  if (smp > m->nsamples) smp = 0;
  if (smp) st->lastsmp = smp;
  if (period)
  {
    int n = periodnote(period) - ((st->lastsmp) ? m->smp[st->lastsmp].oct * 12 : 0);

    if ((n < 0) || (n >= GTNOTES))
    {
      lost->notes++;
      while (n < 0) n += 12;
      while (n >= GTNOTES) n -= 12;
    }
    o->note = FIRSTNOTE + n;
    o->instr = st->lastsmp;
    st->period = period;
  }

  switch (fx)
  {
    case 0x0:
    if (par) lost->arp++;
    break;

    case 0x1:
    case 0x2:
    if (par)
    {
      o->cmd = (fx == 1) ? CMD_PORTAUP : CMD_PORTADOWN;
      o->data = slideentry(oldperiod, par, lost);
      if (!o->data) o->cmd = 0;
    }
    break;

    case 0x3:
    case 0x5:
    if ((fx == 3) && (par)) st->tonespeed = slideentry(oldperiod ? oldperiod : period, par, lost);
    if (st->tonespeed)
    {
      o->cmd = CMD_TONEPORTA;
      o->data = st->tonespeed;
    }
    if ((fx == 5) && (par)) lost->volume++;
    break;

    case 0x4:
    case 0x6:
    if (fx == 4)
    {
      if (par >> 4) st->vibx = par >> 4;
      if (par & 0x0f) st->viby = par & 0x0f;
    }
    if ((st->vibx) && (st->viby))
    {
      o->cmd = CMD_VIBRATO;
      o->data = vibentry(st->period, st->vibx, st->viby, lost);
      if (!o->data) o->cmd = 0;
    }
    if ((fx == 6) && (par)) lost->volume++;
    break;

    case 0xa:
    if (par) lost->volume++;
    break;

    case 0xc:
    // Volume 0 without a note: the note stops
    if ((!par) && (!o->note)) o->note = KEYOFF;
    else lost->volume++;
    break;

    case 0xe:
    // EC0 (note cut at once) without a note: the note stops
    if ((par == 0xc0) && (!o->note)) o->note = KEYOFF;
    else if ((par >> 4) == 0xa || (par >> 4) == 0xb) lost->volume++;
    else if (par) lost->effects++;
    break;

    case 0xb:
    case 0xd:
    case 0xf:
    // The song's structure and tempo (see mod_import())
    break;

    default:
    if (par || (fx == 9)) lost->effects++;
    break;
  }
}

// Stores a converted track pattern (rows of 4 bytes): an equal one is shared.
// Returns its number, -1 if there is no room
static int storepattern(const unsigned char *rows, int len, int *npatt, MODLOST *lost)
{
  int p, d;

  for (p = 0; p < *npatt; p++)
  {
    if ((pattlen[p] == len) && (!memcmp(pattern[p], rows, len * 4))) return p;
  }
  if (*npatt >= MAX_PATT)
  {
    lost->patterns++;
    return -1;
  }
  p = (*npatt)++;
  memset(pattern[p], 0, MAX_PATTROWS * 4 + 4);
  memcpy(pattern[p], rows, len * 4);
  for (d = len; d <= MAX_PATTROWS; d++) pattern[p][d * 4] = (d == len) ? ENDPATT : 0;
  pattlen[p] = len;
  return p;
}

// An instrument for sample s: its name, a SID sound in its place
static void makeinstr(int num, const MODSAMPLE *s)
{
  INSTR *in = &instr[num];
  int noise = 0, looped = (s->looplength > 2) && (s->loopstart + s->looplength <= s->length + 2);
  int wave, ad, sr, pulse = 0, wptr, pptr = 0;

  // Noisy: the sign changes more often than every 4th byte
  if ((s->data) && (s->datalen > 16))
  {
    int n = s->datalen > 4096 ? 4096 : s->datalen;
    int c, changes = 0;
    for (c = 1; c < n; c++)
    {
      if (((s->data[c] >= 4) && (s->data[c - 1] < -4)) || ((s->data[c] < -4) && (s->data[c - 1] >= 4))) changes++;
    }
    noise = (changes * 4 > n);
  }
  if (noise)
  {
    wave = 0x81;
    ad = 0x05;
    sr = 0x00;
  }
  else if (looped)
  {
    wave = 0x21;
    ad = 0x08;
    sr = (clampint((s->volume * 10 + 32) / 64, 1, 10) << 4) | 0x06;
  }
  else
  {
    wave = 0x41;
    pulse = 0x800;
    ad = 0x09;
    sr = 0x00;
  }

  wptr = addtablerow(WTBL, wave, 0x00);
  addtablerow(WTBL, 0xff, 0x00);
  if (pulse)
  {
    pptr = addtablerow(PTBL, 0x80 | ((pulse >> 8) & 0x0f), pulse & 0xff);
    addtablerow(PTBL, 0xff, 0x00);
  }
  in->ad = (unsigned char)ad;
  in->sr = (unsigned char)sr;
  in->ptr[WTBL] = (unsigned char)wptr;
  in->ptr[PTBL] = (unsigned char)pptr;
  in->ptr[FTBL] = 0;
  in->ptr[STBL] = 0;
  in->vibdelay = 0;
  in->gatetimer = 2;
  in->firstwave = 0x09;
  memset(in->name, 0, MAX_INSTRNAMELEN);
  if (s->name[0]) strncpy(in->name, s->name, MAX_INSTRNAMELEN - 1);
  else snprintf(in->name, MAX_INSTRNAMELEN, "Sample %02d", num);
}

int mod_import(const char *path, const MODIMPORT_OPTIONS *opt, char *report, int reportsize)
{
  MODFILE m;
  MODLOST lost;
  int seq[MOD_MAXPOS], nseq = 0, loopto = 0;
  int visited[MOD_MAXPOS];
  int pattend[MOD_MAXPATT], jumpto[MOD_MAXPATT];
  CHSTATE st[MOD_MAXCHN];
  static unsigned char rows[MAX_CHN][MODROWS * 4];
  int gtpatt[MOD_MAXPOS][MAX_CHN];
  int npatt = 0, notes = 0, ninstr = 0;
  int speed = 6, bpm = 125, tempo = 6;
  int i, k, r, t, c, s;
  unsigned src[MAX_CHN];

  memset(&lost, 0, sizeof lost);
  report[0] = 0;
  if (!readmod(path, &m, report, reportsize)) return 0;
  for (t = 0; t < MAX_CHN; t++)
  {
    src[t] = opt ? opt->source[t] & ((1u << m.channels) - 1) : (1u << t);
  }
  if ((!src[0]) && (!src[1]) && (!src[2]))
  {
    snprintf(report, reportsize, "No channel chosen for the tracks");
    free(m.d);
    return 0;
  }

  // Where each pattern ends: its first break (Dxx) or position jump (Bxx)
  for (k = 0; k < m.npatt; k++)
  {
    pattend[k] = MODROWS;
    jumpto[k] = -1;
    for (r = 0; (r < MODROWS) && (pattend[k] == MODROWS); r++)
    {
      for (c = 0; c < m.channels; c++)
      {
        int period, smp, fx, par;
        cell(&m, k, r, c, &period, &smp, &fx, &par);
        if (fx == 0xb)
        {
          pattend[k] = r + 1;
          jumpto[k] = par;
        }
        if (fx == 0xd)
        {
          pattend[k] = r + 1;
          if (par) lost.breaks++;
        }
      }
    }
  }

  // The positions as they play: a jump back is the song's loop
  memset(visited, 0, sizeof visited);
  i = 0;
  for (;;)
  {
    if (i >= m.songlen)
    {
      loopto = 0;
      break;
    }
    if (visited[i])
    {
      for (k = 0; k < nseq; k++)
      {
        if (seq[k] == i) loopto = k;
      }
      break;
    }
    visited[i] = 1;
    seq[nseq++] = i;
    if (jumpto[m.order[i]] >= 0)
    {
      int j = jumpto[m.order[i]];
      if (j >= m.songlen) j = 0;
      if (j > i + 1) lost.breaks++;
      i = j;
    }
    else i++;
  }

  // The song
  stopsong();
  sound_lockplayer();
  clearsong(1, 1, 1, 1, 1);
  memset(tlen, 0, sizeof tlen);
  strncpy(songname, m.title, MAX_STR - 1);

  for (s = 1; s <= m.nsamples; s++)
  {
    if ((m.smp[s].length > 2) || (m.smp[s].name[0]))
    {
      makeinstr(s, &m.smp[s]);
      if (m.smp[s].length > 2) ninstr++;
    }
  }

  memset(st, 0, sizeof st);
  for (c = 0; c < MOD_MAXCHN; c++) st[c].period = 428;
  for (k = 0; k < nseq; k++)
  {
    int p = m.order[seq[k]];
    int len = pattend[p];

    for (r = 0; r < len; r++)
    {
      GTROW row[MOD_MAXCHN];
      int newtempo = tempo;

      for (c = 0; c < m.channels; c++) convrow(&m, p, r, c, &st[c], &row[c], &lost);

      // Speed and BPM (any channel) as the tempo
      for (c = 0; c < m.channels; c++)
      {
        int period, smp, fx, par;
        cell(&m, p, r, c, &period, &smp, &fx, &par);
        if (fx != 0xf) continue;
        if (!par) lost.tempo++;
        else if (par < 0x20) speed = par;
        else bpm = par;
      }
      if ((speed) && (bpm))
      {
        int f = (int)floor(speed * 125.0 / bpm + 0.5);
        if ((f < 3) || (f > 0x7f)) lost.tempo++;
        newtempo = clampint(f, 3, 0x7f);
      }

      for (t = 0; t < MAX_CHN; t++)
      {
        unsigned char *o = &rows[t][r * 4];
        GTROW out;
        int a = -1, b = -1;

        memset(&out, 0, sizeof out);
        for (c = 0; c < m.channels; c++)
        {
          if (!(src[t] & (1u << c))) continue;
          if (a < 0) a = c;
          else if (b < 0) b = c;
        }
        if (a >= 0)
        {
          // Merged: the note of the lower channel that has one, the command
          // of the other channel if that one has none
          int ch = ((b >= 0) && (!row[a].note) && (row[b].note)) ? b : a;

          out = row[ch];
          if ((b >= 0) && (row[a].note) && (row[b].note)) lost.merged++;
          if ((b >= 0) && (!out.cmd))
          {
            int other = (ch == a) ? b : a;
            out.cmd = row[other].cmd;
            out.data = row[other].data;
          }
        }
        o[0] = out.note ? (unsigned char)out.note : REST;
        o[1] = (unsigned char)((out.note && out.note != KEYOFF) ? out.instr : 0);
        o[2] = (unsigned char)out.cmd;
        o[3] = (unsigned char)out.data;
        if ((out.note >= FIRSTNOTE) && (out.note <= LASTNOTE)) notes++;
      }

      // The tempo on the first track with a free command column
      if ((newtempo != tempo) || ((k == 0) && (r == 0) && (newtempo != 6)))
      {
        for (t = 0; t < MAX_CHN; t++)
        {
          if (!rows[t][r * 4 + 2]) break;
        }
        if (t >= MAX_CHN)
        {
          t = 0;
          lost.busy++;
        }
        rows[t][r * 4 + 2] = CMD_SETTEMPO;
        rows[t][r * 4 + 3] = (unsigned char)newtempo;
        tempo = newtempo;
      }
    }

    for (t = 0; t < MAX_CHN; t++)
    {
      int gp = storepattern(rows[t], len, &npatt, &lost);
      gtpatt[k][t] = (gp < 0) ? 0 : gp;
    }
  }

  // Orderlists
  for (t = 0; t < MAX_CHN; t++)
  {
    for (k = 0; k < nseq; k++) songorder[0][t][k] = (unsigned char)gtpatt[k][t];
    songorder[0][t][nseq] = LOOPSONG;
    songorder[0][t][nseq + 1] = (unsigned char)loopto;
  }
  for (c = 0; c < MAX_CHN; c++)
  {
    editorUndoInfo.editorInfo[c].epnum = songorder[0][c][0];
    editorUndoInfo.editorInfo[c].espos = 0;
    editorUndoInfo.editorInfo[c].esend = 0;
  }
  countpatternlengths();
  sound_unlockplayer();
#ifdef GT_PLUGIN
  gtsync_requestresync();
#endif

  snprintf(report, reportsize, "%d positions, %d patterns, %d notes, %d instruments", nseq, npatt, notes, ninstr);
  reportlost(&lost, report, reportsize);
  free(m.d);
  return 1;
}

/*
 * Export
 */

// Follows a track through its orderlist as the player does
typedef struct
{
  int ptr, trans, repeat, patt, row;
  int ok;           // 0: the orderlist ended (or is broken)
  int looped;       // its end (RST) has been passed
} WALK;

static void walknext(WALK *w, int s, int c)
{
  const unsigned char *ol = songorder[s][c];

  if (ol[w->ptr] == LOOPSONG)
  {
    w->looped = 1;
    w->ptr = ol[w->ptr + 1];
    if (w->ptr >= songlen[s][c])
    {
      w->ok = 0;
      return;
    }
  }
  if ((ol[w->ptr] >= TRANSDOWN) && (ol[w->ptr] < LOOPSONG))
  {
    w->trans = ol[w->ptr] - TRANSUP;
    w->ptr++;
  }
  if ((ol[w->ptr] >= REPEAT) && (ol[w->ptr] < TRANSDOWN))
  {
    w->repeat = ol[w->ptr] - REPEAT;
    w->ptr++;
  }
  w->patt = ol[w->ptr];
  if (w->repeat) w->repeat--;
  else w->ptr++;
  w->row = 0;
  if ((w->patt >= MAX_PATT) || (pattlen[w->patt] < 1)) w->ok = 0;
}

static void walkstart(WALK *w, int s, int c)
{
  memset(w, 0, sizeof *w);
  w->ok = (songlen[s][c] > 0);
  if (w->ok) walknext(w, s, c);
}

static void walkrow(WALK *w, int s, int c)
{
  if (!w->ok) return;
  w->row++;
  if (w->row >= pattlen[w->patt]) walknext(w, s, c);
}

static int gtfreq(int note)
{
  note = clampint(note, 0, GTNOTES - 1);
  return freqtbllo[note] | (freqtblhi[note] << 8);
}

// A speed table entry of a slide as ProTracker's period units per tick at a
// note (period)
static int slideamount(int entry, int note, int period)
{
  int l, r;

  if ((entry < 1) || (entry > MAX_TABLELEN)) return 0;
  l = ltable[STBL][entry - 1];
  r = rtable[STBL][entry - 1];
  if (l & SPEED_NOTEREL)
    return clampint((int)floor(HALFSTEP * period / (double)(1 << (r & 7)) + 0.5), 1, 0xff);
  else
  {
    int speed = (l << 8) | r;
    int f = gtfreq(note);
    if (f < 1) f = 1;
    return clampint((int)floor((double)period * speed / f + 0.5), 1, 0xff);
  }
}

// A vibrato speed table entry as ProTracker's 4xy
static int vibparam(int entry, int note, int period)
{
  int l, r, frames, x, y;
  double halfsteps;

  if ((entry < 1) || (entry > MAX_TABLELEN)) return 0;
  l = ltable[STBL][entry - 1];
  r = rtable[STBL][entry - 1];
  frames = (l & 0x7f) ? (l & 0x7f) : 1;
  if (l & SPEED_NOTEREL) halfsteps = frames / (double)(1 << (r & 7));
  else
  {
    int step = gtfreq(note + 1) - gtfreq(note);
    halfsteps = (step > 0) ? frames * r / (double)step : 0.0;
  }
  x = clampint((int)floor(16.0 / frames + 0.5), 1, 15);
  y = clampint((int)floor(halfsteps * HALFSTEP * period / 2.0 + 0.5), 1, 15);
  return (x << 4) | y;
}

// The waveform the instrument's sound settles on: its wavetable's last one
// before the program stops or loops (a drum's noise hit before a bass's
// pulse doesn't count). 0x41 if it has none
static int sustainwave(int ins)
{
  int pos = instr[ins].ptr[WTBL];
  int wave = 0x41;
  int k;

  for (k = 0; (k < 64) && (pos >= 1) && (pos <= MAX_TABLELEN); k++)
  {
    int l = ltable[WTBL][pos - 1];
    if (l == 0xff)
    {
      int to = rtable[WTBL][pos - 1];
      // A jump forward is followed; a stop or a loop back ends it
      if ((!to) || (to <= pos)) break;
      pos = to;
      continue;
    }
    if ((l >= 0x10) && (l <= 0xdf)) wave = l;
    pos++;
  }
  return wave;
}

// One cycle of the instrument's sound (or 1 KB of noise): 32 bytes, twice
// as long for each octave of oct (the sample sounds an octave lower, so its
// notes are written an octave higher), half as long for each octave below
// 0
static int makesample(int ins, int oct, signed char *out)
{
  int w = sustainwave(ins);
  int len = (oct >= 0) ? (32 << oct) : (32 >> -oct);
  int k;

  if (w & 0x80)
  {
    unsigned seed = 0x1234567u + (unsigned)ins;
    for (k = 0; k < 1024; k++)
    {
      seed = seed * 1103515245u + 12345u;
      out[k] = (signed char)((int)((seed >> 16) & 0xff) - 128);
    }
    return 1024;
  }
  if (w & 0x40)
  {
    int pw = 0x800, duty;
    int p = instr[ins].ptr[PTBL];
    if ((p >= 1) && (p <= MAX_TABLELEN) && (ltable[PTBL][p - 1] >= 0x80) && (ltable[PTBL][p - 1] < 0xff))
      pw = ((ltable[PTBL][p - 1] & 0x0f) << 8) | rtable[PTBL][p - 1];
    duty = clampint((pw * len + 2048) / 4096, 1, len - 1);
    for (k = 0; k < len; k++) out[k] = (k < duty) ? 100 : -100;
    return len;
  }
  if (w & 0x20)
  {
    for (k = 0; k < len; k++) out[k] = (signed char)(-112 + k * 224 / len);
    return len;
  }
  for (k = 0; k < len; k++)
    out[k] = (signed char)((k < len / 2) ? -112 + k * 448 / len : 112 - (k - len / 2) * 448 / len);
  return len;
}

static void put16(unsigned char *p, int v)
{
  p[0] = (unsigned char)((v >> 8) & 0xff);
  p[1] = (unsigned char)(v & 0xff);
}

int mod_export(const char *path, char *report, int reportsize)
{
  MODLOST lost;
  WALK w[MAX_CHN];
  int s = editorInfo.esnum;
  int c, i, k, total = 0, maxrows = MOD_MAXPOS * MODROWS;
  unsigned char *rows;            // total rows x 4 channels x 4 bytes
  int smpof[MAX_INSTR];
  int smpins[32], nsmp = 0;
  // Per instrument its notes, and the octaves its sample is shifted by so
  // that they fit ProTracker's three (see makesample())
  static int notecount[MAX_INSTR][GTNOTES];
  int oct[MAX_INSTR];
  int lastins[MAX_CHN], curnote[MAX_CHN], curperiod[MAX_CHN];
  int npos, npatt = 0, posof[MOD_MAXPOS];
  int fps100, bpm, div;
  int start;
  static signed char smpdata[32][1024];      // (128 bytes at most for a cycle)
  int smplen[32];
  FILE *f;
  unsigned char hdr[1084];
  int notes = 0;

  memset(&lost, 0, sizeof lost);
  report[0] = 0;

  // The longest track's rows up to its orderlist's end
  for (c = 0; c < MAX_CHN; c++)
  {
    int n = 0;
    walkstart(&w[c], s, c);
    while ((w[c].ok) && (!w[c].looped) && (n <= maxrows))
    {
      n++;
      walkrow(&w[c], s, c);
    }
    if (n > total) total = n;
  }
  if (!total)
  {
    snprintf(report, reportsize, "The subtune has no rows to export");
    return 0;
  }
  if (total > maxrows)
  {
    lost.rows += total - maxrows;
    total = maxrows;
  }
  rows = (unsigned char *)calloc((size_t)total * 16, 1);
  if (!rows)
  {
    snprintf(report, reportsize, "Out of memory");
    return 0;
  }

  // The frame rate as ProTracker's BPM: 125 = 50 Hz. Above 255 the speeds
  // are divided
  fps100 = (editorInfo.ntsc ? 6000 : 5000);
  if (editorInfo.multiplier) fps100 *= editorInfo.multiplier;
  else fps100 /= 2;
  bpm = (fps100 * 125 + 2500) / 5000;
  div = 1;
  while (bpm > 255)
  {
    bpm = (bpm + 1) / 2;
    div *= 2;
  }

  // The tempo the song starts with: the tempo instrument's ($3F), else 6
  start = editorInfo.multiplier ? 6 * editorInfo.multiplier : 6;
  if ((instr[MAX_INSTR - 1].ad >= 2) && (!instr[MAX_INSTR - 1].ptr[WTBL])) start = instr[MAX_INSTR - 1].ad;

  // The notes each instrument plays: its octave shift puts most of them in
  // ProTracker's range (C-1 to B-3, GoatTracker's C-3 to B-5)
  memset(notecount, 0, sizeof notecount);
  for (c = 0; c < MAX_CHN; c++)
  {
    walkstart(&w[c], s, c);
    lastins[c] = 0;
  }
  for (i = 0; i < total; i++)
  {
    for (c = 0; c < MAX_CHN; c++)
    {
      const unsigned char *gr;
      if (!w[c].ok) continue;
      gr = &pattern[w[c].patt][w[c].row * 4];
      if ((gr[1] > 0) && (gr[1] < MAX_INSTR)) lastins[c] = gr[1];
      if ((gr[0] >= FIRSTNOTE) && (gr[0] <= LASTNOTE) && (lastins[c]))
        notecount[lastins[c]][clampint(gr[0] - FIRSTNOTE + w[c].trans, 0, GTNOTES - 1)]++;
      walkrow(&w[c], s, c);
    }
  }
  for (k = 0; k < MAX_INSTR; k++)
  {
    int best = -1, bestin = -1, o, n;
    for (o = 0; o <= 4; o++)
    {
      // 0, 1, -1, 2, -2: of equal ones the smaller shift
      int sh = (o & 1) ? (o + 1) / 2 : -(o / 2);
      int in = 0;
      for (n = 0; n < GTNOTES; n++)
      {
        int pt = n - PTNOTE0 + sh * 12;
        if ((pt >= 0) && (pt < PTPERIODS)) in += notecount[k][n];
      }
      if (in > bestin)
      {
        bestin = in;
        best = sh;
      }
    }
    // Noise isn't shifted: its sample has no octave an import could see
    oct[k] = (sustainwave(k) & 0x80) ? 0 : best;
  }

  memset(smpof, 0, sizeof smpof);
  for (c = 0; c < MAX_CHN; c++)
  {
    walkstart(&w[c], s, c);
    lastins[c] = 0;
    curnote[c] = 48;
    curperiod[c] = 428;
  }
  for (i = 0; i < total; i++)
  {
    unsigned char *rc = rows + (size_t)i * 16;
    int tempo = (i == 0) ? start : -1;

    for (c = 0; c < MAX_CHN; c++)
    {
      unsigned char *o = rc + c * 4;
      const unsigned char *gr;
      int note, ins, cmd, data, fx = 0, par = 0, period = 0, smp = 0;

      if (!w[c].ok) continue;
      gr = &pattern[w[c].patt][w[c].row * 4];
      note = gr[0];
      ins = gr[1];
      cmd = gr[2];
      data = gr[3];
      if ((ins > 0) && (ins < MAX_INSTR)) lastins[c] = ins;
      if ((note >= FIRSTNOTE) && (note <= LASTNOTE))
      {
        int n = clampint(note - FIRSTNOTE + w[c].trans, 0, GTNOTES - 1);
        int pt = n - PTNOTE0 + (lastins[c] ? oct[lastins[c]] * 12 : 0);

        if ((pt < 0) || (pt >= PTPERIODS)) lost.notes++;
        while (pt < 0) pt += 12;
        while (pt >= PTPERIODS) pt -= 12;
        period = ptperiod[pt];
        curnote[c] = n;
        if (lastins[c])
        {
          if (!smpof[lastins[c]])
          {
            if (nsmp < 31)
            {
              smpins[nsmp] = lastins[c];
              smpof[lastins[c]] = ++nsmp;
            }
            else
            {
              smpof[lastins[c]] = 31;
              lost.samples++;
            }
          }
          smp = smpof[lastins[c]];
        }
        notes++;
      }
      else if (note == KEYOFF)
      {
        fx = 0xc;
        par = 0;
      }

      switch (cmd)
      {
        case CMD_PORTAUP:
        case CMD_PORTADOWN:
        case CMD_TONEPORTA:
        case CMD_VIBRATO:
        if (fx)
        {
          lost.busy++;
          break;
        }
        if (cmd == CMD_VIBRATO)
        {
          par = vibparam(data, curnote[c], period ? period : curperiod[c]);
          fx = par ? 4 : 0;
        }
        else if ((cmd == CMD_TONEPORTA) && (!data))
        {
          fx = 3;
          par = 0xff;
        }
        else
        {
          // Up and down: the period goes the other way
          par = slideamount(data, curnote[c], curperiod[c]);
          fx = par ? ((cmd == CMD_PORTAUP) ? 1 : (cmd == CMD_PORTADOWN) ? 2 : 3) : 0;
        }
        break;

        case CMD_SETTEMPO:
        // 2 and 3 both give rows of 3 frames; 0 and 1 recall the funktempo
        if ((data & 0x7f) >= 2) tempo = ((data & 0x7f) == 2) ? 3 : data & 0x7f;
        if (data & 0x80) lost.tempo++;
        break;

        case CMD_FUNKTEMPO:
        // Its two row lengths (the speed table's values) on average
        if ((data >= 1) && (data <= MAX_TABLELEN))
        {
          int l = ltable[STBL][data - 1] ? ltable[STBL][data - 1] : 256;
          int r = rtable[STBL][data - 1] ? rtable[STBL][data - 1] : 256;
          tempo = (l + r + 1) / 2;
        }
        break;

        case CMD_DONOTHING:
        break;

        default:
        lost.effects++;
        break;
      }
      if (period) curperiod[c] = period;
      o[0] = (unsigned char)((smp & 0xf0) | ((period >> 8) & 0x0f));
      o[1] = (unsigned char)(period & 0xff);
      o[2] = (unsigned char)(((smp & 0x0f) << 4) | (fx & 0x0f));
      o[3] = (unsigned char)par;
      walkrow(&w[c], s, c);
    }

    // Speed (and at the start the BPM) on channel 4
    if (tempo >= 1)
    {
      int sp = (tempo + div / 2) / div;
      if (sp > 0x1f)
      {
        sp = 0x1f;
        lost.tempo++;
      }
      if (sp < 1) sp = 1;
      rc[3 * 4 + 2] = 0x0f;
      rc[3 * 4 + 3] = (unsigned char)sp;
    }
    if ((i == 0) && (bpm != 125))
    {
      // On the first channel with a free effect column
      for (c = 0; c < MAX_CHN; c++)
      {
        if (!(rc[c * 4 + 2] & 0x0f) && (!rc[c * 4 + 3])) break;
      }
      if (c < MAX_CHN)
      {
        rc[c * 4 + 2] = (unsigned char)((rc[c * 4 + 2] & 0xf0) | 0x0f);
        rc[c * 4 + 3] = (unsigned char)(bpm < 0x20 ? 0x20 : bpm);
      }
      else lost.tempo++;
    }
  }

  // 64-row patterns, the last one ends with a break; equal ones shared
  npos = (total + MODROWS - 1) / MODROWS;
  {
    static unsigned char patts[MOD_MAXPOS][MODROWS * 16];

    for (k = 0; k < npos; k++)
    {
      unsigned char cur[MODROWS * 16];
      int n = total - k * MODROWS;
      int q;

      if (n > MODROWS) n = MODROWS;
      memset(cur, 0, sizeof cur);
      memcpy(cur, rows + (size_t)k * MODROWS * 16, (size_t)n * 16);
      if (n < MODROWS)
      {
        unsigned char *last = cur + (n - 1) * 16;
        for (c = MAX_CHN; c >= 0; c--)
        {
          if (!(last[c * 4 + 2] & 0x0f) && (!last[c * 4 + 3])) break;
        }
        if (c >= 0)
        {
          last[c * 4 + 2] = (unsigned char)((last[c * 4 + 2] & 0xf0) | 0x0d);
          last[c * 4 + 3] = 0;
        }
      }
      for (q = 0; q < npatt; q++)
      {
        if (!memcmp(patts[q], cur, sizeof cur)) break;
      }
      if (q == npatt) memcpy(patts[npatt++], cur, sizeof cur);
      posof[k] = q;
    }

    // Samples
    for (k = 0; k < nsmp; k++) smplen[k] = makesample(smpins[k], oct[smpins[k]], smpdata[k]);

    f = fopen(path, "wb");
    if (!f)
    {
      free(rows);
      snprintf(report, reportsize, "The file can't be written");
      return 0;
    }
    memset(hdr, 0, sizeof hdr);
    for (k = 0; (k < 20) && (songname[k]); k++) hdr[k] = (unsigned char)songname[k];
    for (k = 0; k < 31; k++)
    {
      unsigned char *h = hdr + 20 + k * 30;
      if (k < nsmp)
      {
        const char *name = instr[smpins[k]].name;
        int j;
        for (j = 0; (j < 22) && (j < MAX_INSTRNAMELEN) && (name[j]); j++) h[j] = (unsigned char)name[j];
        put16(h + 22, smplen[k] / 2);
        h[24] = 0;
        h[25] = 64;
        put16(h + 26, 0);
        put16(h + 28, smplen[k] / 2);
      }
      else put16(h + 28, 1);
    }
    hdr[950] = (unsigned char)npos;
    hdr[951] = 0x7f;
    for (k = 0; k < npos; k++) hdr[952 + k] = (unsigned char)posof[k];
    memcpy(hdr + 1080, (npatt > 64) ? "M!K!" : "M.K.", 4);
    fwrite(hdr, 1, sizeof hdr, f);
    fwrite(patts, 1, (size_t)npatt * MODROWS * 16, f);
    for (k = 0; k < nsmp; k++) fwrite(smpdata[k], 1, smplen[k], f);
    if (ferror(f))
    {
      fclose(f);
      free(rows);
      snprintf(report, reportsize, "The file can't be written");
      return 0;
    }
    fclose(f);
  }
  free(rows);

  snprintf(report, reportsize, "%d positions, %d patterns, %d notes, %d samples", npos, npatt, notes, nsmp);
  reportlost(&lost, report, reportsize);
  return 1;
}
