/*
 * GoatTracker Ultimate: SID-Wizard modules (.swm), import and export
 *
 * The format (SWM1, written by SID-Wizard 1.x up to 1.97), as SID-Wizard's
 * own sources define it (SWM-spec.src, packdepack.inc, player.asm):
 *   2 bytes   C64 load address (not used)
 *   64 bytes  header: "SWM1", frame speed, highlight step, ..., the counts of
 *             sequences ($0C), patterns ($0D) and instruments ($0E), the
 *             chord and tempo table lengths ($0F, $10), author info ($18-$3F:
 *             "AUTHOR : TITLE")
 *   the sequences (orderlists, 3 per subtune), each followed by its length
 *   the patterns, packed, each followed by its unpacked size and its rows
 *   the instruments, each followed by its size and its 8-character name
 *   the chord table, the tempo table, 2 tempo bytes per subtune
 * A module is read from the end backwards, as SID-Wizard reads it: each
 * size comes after the data it belongs to.
 *
 * A SID-Wizard instrument has wave/arpeggio, pulse and filter tables of its
 * own. GoatTracker's tables are shared, so the instruments' tables are
 * copied into them (and out of them for the export). What GoatTracker has no
 * place for (chords, tempo programs, delays, the sync / ring / detune
 * columns, the gate-off table jumps, orderlist volume and tempo bytes) is
 * left out and counted in the report. Slide and vibrato speeds are
 * approximated: the two players compute them differently.
 */

#define GSWM_C

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "goattrk2.h"
#include "gswm.h"
#ifdef GT_PLUGIN
#include "plugin/gtplugin.h"
#endif

#define SWM_HEADER 64
#define SWM_LOADADDR 0x3333
#define SWM_MAXFILE 0x10000
#define SWM_MAXSUBTUNES 8
#define SWM_MAXPATT 100
#define SWM_MAXPATTSIZE 0xfa       // unpacked bytes, the end mark included
#define SWM_MAXPATTROWS 0xf8
#define SWM_MAXSEQ 126
#define SWM_MAXINSTR 62
#define SWM_MAXINSTRSIZE 120       // without the name
#define SWM_NAMELEN 8
#define SWM_TABLESTART 0x10        // the wave table, after the instrument's header
#define SWM_MAXEXTRA 16            // table programs of the patterns per instrument

// Speedtable entries made for the slides and vibratos
#define SPEED_NOTEREL 0x80

// What couldn't be converted
typedef struct
{
  int fx;           // pattern effects with no GoatTracker command
  int fxbusy;       // effects on a row that has a command already
  int seq;          // orderlist volume / tempo bytes, subtune jumps
  int ends;         // tracks that stop (GoatTracker loops them)
  int chords;
  int tables;       // table rows that didn't fit
  int instr;        // instrument settings (delays, gate-off jumps...)
  int tempo;
  int notes;        // notes out of range
  int patterns;
  int subtunes;
} SWMLOST;

static void reportlost(const SWMLOST *l, char *text, int size)
{
  int n = (int)strlen(text);

#define LOSTPART(count, what) \
  if ((count) && (n < size - 1)) \
    n += snprintf(text + n, size - n, "%s%d %s", strchr(text, '(') ? ", " : " (left out: ", (count), (what));

  LOSTPART(l->fx, "effects");
  LOSTPART(l->fxbusy, "effects on rows with one already");
  LOSTPART(l->seq, "orderlist commands");
  LOSTPART(l->ends, "track ends (looped)");
  LOSTPART(l->chords, "chords");
  LOSTPART(l->tables, "table rows");
  LOSTPART(l->instr, "instrument settings");
  LOSTPART(l->tempo, "tempo settings");
  LOSTPART(l->notes, "notes out of range");
  LOSTPART(l->patterns, "patterns");
  LOSTPART(l->subtunes, "subtunes");
#undef LOSTPART
  if ((strchr(text, '(')) && (n < size - 1)) snprintf(text + n, size - n, ")");
}

static int readfile(const char *path, unsigned char **data, int *len)
{
  FILE *f = fopen(path, "rb");
  long n;

  if (!f) return 0;
  fseek(f, 0, SEEK_END);
  n = ftell(f);
  fseek(f, 0, SEEK_SET);
  if ((n <= 0) || (n > SWM_MAXFILE))
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

static int clampint(int v, int lo, int hi)
{
  return (v < lo) ? lo : (v > hi) ? hi : v;
}

/*
 * The tables (both ways)
 */

static int tlen[MAX_TABLES];

// Appends a row to GoatTracker table t: its index (1-based), 0 if full
static int addtablerow(int t, int l, int r)
{
  if (tlen[t] >= MAX_TABLELEN - 1) return 0;
  ltable[t][tlen[t]] = (unsigned char)l;
  rtable[t][tlen[t]] = (unsigned char)r;
  return ++tlen[t];
}

// A speedtable entry (an equal one is shared): its index, 0 if full
static int speedentry(int l, int r)
{
  int c;

  for (c = 0; c < tlen[STBL]; c++)
  {
    if ((ltable[STBL][c] == l) && (rtable[STBL][c] == r)) return c + 1;
  }
  return addtablerow(STBL, l, r);
}

// SID-Wizard slide speed (its player: a step from an exponential table, so
// about the same part of a halfstep at every pitch) as GoatTracker's
// note-relative speed: a halfstep's frequency step shifted right
static int slideshift(int speed)
{
  int shift = 0;

  if (speed < 1) speed = 1;
  while ((shift < 8) && ((128 >> shift) > speed)) shift++;
  return shift;
}

static int slideentry(int speed)
{
  return speedentry(SPEED_NOTEREL, slideshift(speed));
}

// SID-Wizard vibrato (amplitude and rate nibbles) as a GoatTracker vibrato
// speedtable entry: the half period in frames, the depth relative to the
// note. 0 = no vibrato
static int vibentry(int amplitude, int rate)
{
  int shift;

  if (!amplitude) return 0;
  shift = clampint(8 - amplitude / 2, 0, 7);
  return speedentry(SPEED_NOTEREL | clampint(rate ? rate : 4, 1, 0x7f), shift);
}

/*
 * Import
 */

// A pattern, decoded: per row the GoatTracker note and instrument, and the
// SID-Wizard effects still to be converted
typedef struct
{
  int rows;
  unsigned char note[SWM_MAXPATTROWS];
  unsigned char instr[SWM_MAXPATTROWS];
  unsigned char col1[SWM_MAXPATTROWS];      // special note column values
  unsigned char col2[SWM_MAXPATTROWS];      // special instrument column values
  unsigned char fx[SWM_MAXPATTROWS];        // effect: big $01-$1F, small $20+
  unsigned char fxparam[SWM_MAXPATTROWS];
  unsigned char hasfx[SWM_MAXPATTROWS];
} SWMPATT;

// An instrument's tables in GoatTracker's: per SID-Wizard row (by its byte
// offset in the instrument) the GoatTracker row (1-based)
typedef struct
{
  unsigned char base[3];                    // the first rows (0 = none)
  int start[3];                             // the tables' byte offsets
  int haschord;                             // its wave table plays the chord
  int firstwave;
  unsigned char rowat[3][128];              // per byte offset of a row
} SWMINSTRMAP;

// A pattern ends before end: [packed data][unpacked size][rows]. Unpacks it
// backwards (as SID-Wizard does: runs of zeros are $00 then $70-$77 for 2-9
// more) into out, the end mark included. Returns where its packed data
// starts, -1 if the bytes don't add up
static int unpackpattern(const unsigned char *d, int lo, int end, unsigned char *out, int *size, int *rows)
{
  int k, q;

  if (end - 2 < lo) return -1;
  *rows = d[end - 1];
  *size = d[end - 2];
  if (*size < 1) return -1;
  out[*size - 1] = 0xff;
  k = *size - 1;
  q = end - 3;
  while (k > 0)
  {
    unsigned char b;

    if (q < lo) return -1;
    b = d[q];
    if ((b >= 0x70) && (b <= 0x77) && (q - 1 >= lo) && ((d[q - 1] == 0) || ((d[q - 1] >= 0x70) && (d[q - 1] <= 0x77))))
    {
      int z = b - 0x6e;

      if (z > k) return -1;
      while (z--) out[--k] = 0;
    }
    else out[--k] = b;
    q--;
  }
  return q + 1;
}

static void decodepattern(const unsigned char *u, int size, SWMPATT *p, SWMLOST *lost)
{
  int i = 0, r = 0;

  memset(p, 0, sizeof *p);
  while ((i < size) && (u[i] != 0xff) && (r < SWM_MAXPATTROWS))
  {
    unsigned char b1 = u[i++];
    int n = b1 & 0x7f;

    p->note[r] = REST;
    if ((n >= 0x01) && (n <= 0x5f))
    {
      int note = 0x5f + n;
      if (note > LASTNOTE)
      {
        lost->notes++;
        note = REST;
      }
      p->note[r] = (unsigned char)note;
    }
    else if (n == 0x7e) p->note[r] = KEYOFF;
    else if (n == 0x7d) p->note[r] = KEYON;
    else if (n) p->col1[r] = (unsigned char)n;
    if ((b1 & 0x80) && (i < size))
    {
      unsigned char b2 = u[i++];
      int v = b2 & 0x7f;

      if ((v >= 0x01) && (v <= 0x3e)) p->instr[r] = (unsigned char)v;
      else if (v) p->col2[r] = (unsigned char)v;
      if ((b2 & 0x80) && (i < size))
      {
        unsigned char b3 = u[i++];

        p->hasfx[r] = 1;
        p->fx[r] = b3;
        if ((b3 < 0x20) && (i < size)) p->fxparam[r] = u[i++];
      }
    }
    r++;
  }
  p->rows = r;
}

// While a pattern is converted: the instrument playing, the envelope it has
// (for the effects that set one nibble), the chord set (-1 = the
// instrument's own) and a portamento waiting for the next note
typedef struct
{
  int instr;
  int ad, sr;
  int chord;
  int porta;
} SWMSTATE;

static void setcmd(unsigned char *row, int cmd, int data, SWMLOST *lost)
{
  if ((row[2]) || (row[3]))
  {
    lost->fxbusy++;
    return;
  }
  row[2] = (unsigned char)cmd;
  row[3] = (unsigned char)data;
}

static int needentry(int e, SWMLOST *lost)
{
  if (!e) lost->tables++;
  return e;
}

// Small effects ($2x-$Fx; the instrument column's $4x-$7x are the same)
static void smallfx(unsigned char *row, int v, SWMSTATE *st, SWMLOST *lost)
{
  int lo = v & 0x0f;

  switch (v >> 4)
  {
    case 0x2:
    st->ad = (lo << 4) | (st->ad & 0x0f);
    setcmd(row, CMD_SETAD, st->ad, lost);
    break;

    case 0x3:
    st->ad = (st->ad & 0xf0) | lo;
    setcmd(row, CMD_SETAD, st->ad, lost);
    break;

    case 0x4:
    setcmd(row, CMD_SETWAVE, (lo << 4) | 0x01, lost);
    break;

    case 0x5:
    st->sr = (lo << 4) | (st->sr & 0x0f);
    setcmd(row, CMD_SETSR, st->sr, lost);
    break;

    case 0x6:
    st->sr = (st->sr & 0xf0) | lo;
    setcmd(row, CMD_SETSR, st->sr, lost);
    break;

    case 0x7:
    st->chord = lo;
    break;

    case 0x8:
    setcmd(row, CMD_VIBRATO, vibentry(lo, 0), lost);
    break;

    case 0xa:
    setcmd(row, CMD_SETMASTERVOL, lo, lost);
    break;

    default:
    lost->fx++;
    break;
  }
}

// Big effects ($01-$1F with a parameter). instr: the instrument playing (for
// the table jumps), 0 if not known
static void bigfx(unsigned char *row, int fx, int p, int instr, const SWMINSTRMAP *maps, SWMSTATE *st, SWMLOST *lost)
{
  switch (fx)
  {
    case 0x01:
    case 0x02:
    setcmd(row, fx, p ? needentry(slideentry(p), lost) : 0, lost);
    break;

    case 0x03:
    setcmd(row, CMD_TONEPORTA, (p == 0xff) ? 0 : needentry(slideentry(p), lost), lost);
    break;

    case 0x04:
    setcmd(row, CMD_SETWAVE, p, lost);
    break;

    case 0x05:
    st->ad = p;
    setcmd(row, CMD_SETAD, p, lost);
    break;

    case 0x06:
    st->sr = p;
    setcmd(row, CMD_SETSR, p, lost);
    break;

    case 0x07:
    st->chord = p & 0x1f;
    break;

    case 0x08:
    setcmd(row, CMD_VIBRATO, vibentry(p >> 4, p & 0x0f), lost);
    break;

    case 0x09:
    case 0x0a:
    case 0x0b:
    {
      // Go to row p of the instrument's table
      int t = fx - 0x09;
      int g = 0;

      if ((instr) && (instr <= SWM_MAXINSTR) && (maps[instr].base[t]))
      {
        int at = maps[instr].start[t] + 3 * p;
        if ((at >= 0) && (at < 128)) g = maps[instr].rowat[t][at];
      }
      if (g) setcmd(row, CMD_SETWAVEPTR + t, g, lost);
      else lost->fx++;
    }
    break;

    case 0x0f:
    setcmd(row, CMD_SETFILTERCUTOFF, p, lost);
    break;

    case 0x10:
    setcmd(row, CMD_SETTEMPO, clampint(p & 0x7f, 3, 0x7f), lost);
    break;

    case 0x13:
    setcmd(row, CMD_SETTEMPO, clampint(p & 0x7f, 3, 0x7f) | 0x80, lost);
    break;

    case 0x11:
    case 0x14:
    setcmd(row, CMD_FUNKTEMPO, needentry(speedentry(clampint(p >> 4, 3, 15), clampint(p & 0x0f, 3, 15)), lost), lost);
    break;

    case 0x1f:
    setcmd(row, CMD_SETFILTERCTRL, p, lost);
    break;

    case 0x16:
    break;

    default:
    lost->fx++;
    break;
  }
}

// The rows of an instrument's table from byte offset start: their offsets
// (up to the single $FF that ends it). Returns the number of rows
static int tablerows(const unsigned char *ib, int size, int start, int *offs, int maxrows)
{
  int n = 0, o = start;

  while ((o >= 0) && (o + 2 < size + 1) && (ib[o] != 0xff) && (n < maxrows))
  {
    offs[n++] = o;
    o += 3;
  }
  return n;
}

// A SID-Wizard arpeggio value as GoatTracker's wavetable note column
static int arpnote(int v, int shift, SWMLOST *lost)
{
  if (v == 0x80) return 0x80;
  if (v == 0x7f)
  {
    lost->chords++;
    return shift ? (shift & 0x7f) : 0x00;
  }
  if (v < 0x7f)
  {
    // Up (above $5F GoatTracker's notes go down)
    v = clampint(v + shift, -32, 0x5f);
    return (v < 0) ? (0x80 + v) : v;
  }
  if (v >= 0xe0)
  {
    v = clampint(v - 0x100 + shift, -32, 0x5f);
    return (v < 0) ? (0x80 + v) : v;
  }
  // Absolute: SID-Wizard's $80+n is GoatTracker's $7F+n ($80 = C-0 can't be
  // given there: C#-0)
  v -= 1;
  return clampint(v, 0x81, 0xdf);
}

// The chord table: where each chord (1-based) starts in it
static const unsigned char *chordtab = NULL;
static int chordtablen = 0;
static int chordat[32];
static int nchords = 0;

static void findchords(const unsigned char *tab, int len)
{
  int c = 0;

  chordtab = tab;
  chordtablen = len;
  nchords = 0;
  while ((c < len) && (nchords < 31))
  {
    chordat[++nchords] = c;
    while ((c < len) && (tab[c] != 0x7e) && (tab[c] != 0x7f)) c++;
    c++;
  }
}

// A note some halfsteps up or down (and the instrument's octave shift) as
// GoatTracker's wavetable note column
static int relnote(int semis, int shift)
{
  int v = clampint(semis + shift, -32, 0x5f);
  return (v < 0) ? 0x80 + v : v;
}

// A table's rows in GoatTracker's form, before they are placed. A jump's
// target: the row's index + 1 (-1 = stop, -2 = a SID-Wizard offset not
// resolved yet, in tl)
#define SWM_MAXROWS 400
typedef struct
{
  int tl[SWM_MAXROWS], tr[SWM_MAXROWS], tj[SWM_MAXROWS];
  int nrow;
  int rowof[128];       // per SID-Wizard row (byte offset): the first row made
  int firstwave;
  int haschord;         // the wave table plays the chord
} SWMTROWS;

static void trow(SWMTROWS *o, int l, int r, int j)
{
  if (o->nrow >= SWM_MAXROWS) return;
  o->tl[o->nrow] = l;
  o->tr[o->nrow] = r;
  o->tj[o->nrow] = j;
  o->nrow++;
}

// Instrument table t's rows (the wave table playing chord as its chord)
static void convtable(int t, const unsigned char *ib, int size, int chord, int chans, SWMTROWS *o, SWMLOST *lost)
{
  int start = (t == 0) ? SWM_TABLESTART : ib[0x0a + t - 1];
  int wavespeed = ib[7] & 0x3f;
  int shift = (signed char)ib[9];
  int offs[64], n, c, k;

  memset(o, 0, sizeof *o);
  for (c = 0; c < 128; c++) o->rowof[c] = -1;
  if ((start < SWM_TABLESTART) || (start > size)) return;
  n = tablerows(ib, size, start, offs, 64);
  for (c = 0; c < n; c++)
  {
    const unsigned char *r = &ib[offs[c]];
    int first = o->nrow;

    if (r[0] == 0xfe)
    {
      // A jump: to itself (or in the wave table $80 and up) = stop
      if ((r[1] == offs[c]) || ((t == WTBL) && (r[1] >= 0x80))) trow(o, 0xff, 0, -1);
      else trow(o, r[1], 0, -2);
    }
    else if ((t == WTBL) && (r[1] == 0x7f))
    {
      // The chord: its notes one after another (as fast as the table), then
      // back to its first note or on with the table
      int at = ((chord >= 1) && (chord <= nchords)) ? chordat[chord] : -1;
      int chordfirst = o->nrow;
      int wave = (r[0] > WAVELASTDELAY) && (r[0] < WAVESILENT) ? r[0] : 0;
      int loop = 0;

      o->haschord = 1;
      if ((wave) && (!o->firstwave)) o->firstwave = wave;
      if (at < 0)
        trow(o, wave, relnote(0, shift), 0);
      else
      {
        for (k = 0; (at + k < chordtablen) && (k < 32); k++)
        {
          int v = chordtab[at + k];
          if ((v == 0x7e) || (v == 0x7f))
          {
            loop = (v == 0x7f);
            break;
          }
          trow(o, k ? 0 : wave, relnote((v < 0x80) ? v : v - 0x100, shift), 0);
          if (wavespeed) trow(o, clampint(wavespeed, 1, WAVELASTDELAY), 0x80, 0);
        }
        if (o->nrow == chordfirst) trow(o, wave, relnote(0, shift), 0);
      }
      if (loop) trow(o, 0xff, 0, chordfirst + 1);
    }
    else if (t == WTBL)
    {
      int wave = r[0];
      int note = arpnote(r[1], shift, lost);

      if (wave >= WAVESILENT)
      {
        wave = 0x81;
        lost->tables++;
      }
      if (wave <= WAVELASTDELAY)
      {
        // Held for some frames: the note now, then the wait
        trow(o, 0, note, 0);
        if (wave) trow(o, wave, 0x80, 0);
      }
      else
      {
        if (!o->firstwave) o->firstwave = wave;
        trow(o, wave, note, 0);
      }
      // Slower than every frame
      if (wavespeed) trow(o, clampint(wavespeed, 1, WAVELASTDELAY), 0x80, 0);
    }
    else if (t == PTBL)
    {
      if (r[0]) trow(o, r[0], r[1], 0);
    }
    else
    {
      if (r[0] >= 0x80)
      {
        int mask = ((r[2] & 0xf0) == 0x80) ? (r[2] & 0x07) : chans;
        trow(o, 0x80 | (r[0] & 0x70), ((r[0] & 0x0f) << 4) | mask, 0);
        trow(o, 0x00, r[1], 0);
      }
      else if (r[0])
      {
        int speed = (signed char)r[1] / 8;
        if (!speed) speed = ((signed char)r[1] < 0) ? -1 : (r[1] ? 1 : 0);
        trow(o, r[0], speed & 0xff, 0);
      }
    }
    if ((offs[c] < 128) && (o->nrow > first)) o->rowof[offs[c]] = first;
  }
  if (!o->nrow) return;
  // A table without a jump at its end stops there
  if ((o->tj[o->nrow - 1] == 0) && (o->tl[o->nrow - 1] != 0xff)) trow(o, 0xff, 0, -1);
  // The jumps' targets as row indexes
  for (k = 0; k < o->nrow; k++)
  {
    if (o->tj[k] == -2)
    {
      int at = (o->tl[k] < 128) ? o->rowof[o->tl[k]] : -1;
      o->tl[k] = 0xff;
      o->tj[k] = (at >= 0) ? at + 1 : -1;
    }
    else if (o->tj[k] == -1) o->tl[k] = 0xff;
  }
}

// Puts rows into GoatTracker table t: shared with equal rows there already
// (another instrument's), else added (as far as they fit, then a stop).
// Returns where they start (1-based), 0 if not at all
static int placetable(int t, SWMTROWS *o, SWMLOST *lost)
{
  int nrow = o->nrow, base, k, c;

  if (!nrow) return 0;
  for (base = 0; base + nrow <= tlen[t]; base++)
  {
    for (k = 0; k < nrow; k++)
    {
      if (o->tj[k] > 0)
      {
        if ((ltable[t][base + k] != 0xff) || (rtable[t][base + k] != base + o->tj[k])) break;
      }
      else if (o->tj[k] < 0)
      {
        if ((ltable[t][base + k] != 0xff) || (rtable[t][base + k])) break;
      }
      else if ((ltable[t][base + k] != o->tl[k]) || (rtable[t][base + k] != o->tr[k])) break;
    }
    if (k == nrow) return base + 1;
  }
  {
    int room = MAX_TABLELEN - 1 - tlen[t];
    if (room < 1)
    {
      lost->tables += nrow;
      return 0;
    }
    if (nrow > room)
    {
      lost->tables += nrow - room;
      nrow = room;
      o->tl[nrow - 1] = 0xff;
      o->tj[nrow - 1] = -1;
      for (k = 0; k < nrow; k++)
      {
        if (o->tj[k] > nrow) o->tj[k] = -1;
      }
      for (c = 0; c < 128; c++)
      {
        if (o->rowof[c] >= nrow - 1) o->rowof[c] = -1;
      }
      o->nrow = nrow;
    }
  }
  base = tlen[t];
  for (k = 0; k < nrow; k++)
  {
    ltable[t][base + k] = (unsigned char)o->tl[k];
    rtable[t][base + k] = (unsigned char)((o->tj[k] > 0) ? base + o->tj[k] : (o->tj[k] < 0) ? 0 : o->tr[k]);
  }
  tlen[t] += nrow;
  return base + 1;
}

// The instruments' bytes and the wave table programs made per chord
static unsigned char swminstr[SWM_MAXINSTR + 1][256];
static int swminstrsize[SWM_MAXINSTR + 1];
static int swminstrchans[SWM_MAXINSTR + 1];
static int chordprog[SWM_MAXINSTR + 1][32];   // 0 = not made, -1 = none

// The wave table program of instrument num playing chord: its first row
// (1-based), 0 if it can't be made
static int chordprogram(int num, int chord, SWMLOST *lost)
{
  static SWMTROWS o;
  int ptr;

  if ((num < 1) || (num > SWM_MAXINSTR) || (chord < 0) || (chord > 31)) return 0;
  if (chordprog[num][chord]) return (chordprog[num][chord] > 0) ? chordprog[num][chord] : 0;
  convtable(WTBL, swminstr[num], swminstrsize[num], chord, swminstrchans[num], &o, lost);
  ptr = placetable(WTBL, &o, lost);
  chordprog[num][chord] = ptr ? ptr : -1;
  return ptr;
}

static void importinstr(int num, const unsigned char *ib, int size, const unsigned char *name, int chans,
  SWMINSTRMAP *map, SWMLOST *lost)
{
  static SWMTROWS o;
  INSTR *in = &instr[num];
  int c, t;

  memset(map, 0, sizeof *map);
  memcpy(swminstr[num], ib, 256);
  swminstrsize[num] = size;
  swminstrchans[num] = chans;
  for (c = 0; c < 32; c++) chordprog[num][c] = 0;
  in->ad = ib[3];
  in->sr = ib[4];
  in->gatetimer = (unsigned char)(ib[0] & 3);
  if (!in->gatetimer) in->gatetimer = 0x41;
  in->vibdelay = ib[6];
  if (ib[5] >> 4)
  {
    in->ptr[STBL] = (unsigned char)vibentry(ib[5] >> 4, ib[5] & 0x0f);
    if (!in->ptr[STBL]) lost->tables++;
  }
  if ((ib[0] & 0xc4) || ((ib[0] & 0x30) == 0x20) || ((ib[0] & 0x30) == 0x30)) lost->instr++;  // staccato, vibrato up / down, no table reset
  if ((ib[0x0c]) || (ib[0x0d]) || (ib[0x0e])) lost->instr++;
  for (c = 0; c < SWM_NAMELEN; c++)
  {
    char ch = (char)name[c];
    if ((ch < 32) || (ch > 126)) ch = ' ';
    in->name[c] = ch;
  }
  in->name[SWM_NAMELEN] = 0;
  for (c = SWM_NAMELEN - 1; (c >= 0) && ((in->name[c] == ' ') || (in->name[c] == '.')); c--) in->name[c] = 0;

  for (t = 0; t < 3; t++)
  {
    int start = (t == 0) ? SWM_TABLESTART : ib[0x0a + t - 1];

    if ((start < SWM_TABLESTART) || (start > size)) continue;
    map->start[t] = start;
    // The wave table with the instrument's own chord
    convtable(t, ib, size, (t == WTBL) ? (ib[8] & 0x1f) : 0, chans, &o, lost);
    if (t == WTBL)
    {
      map->haschord = o.haschord;
      if (o.firstwave) map->firstwave = o.firstwave;
    }
    map->base[t] = (unsigned char)placetable(t, &o, lost);
    for (c = 0; c < 128; c++)
    {
      if ((map->base[t]) && (o.rowof[c] >= 0)) map->rowat[t][c] = (unsigned char)(map->base[t] + o.rowof[c]);
    }
    in->ptr[t] = map->base[t];
    if (t == WTBL) chordprog[num][ib[8] & 0x1f] = map->base[t] ? map->base[t] : -1;
    // A table that stops at once: none
    if ((in->ptr[t]) && (ltable[t][in->ptr[t] - 1] == 0xff) && (!rtable[t][in->ptr[t] - 1])) in->ptr[t] = 0;
  }
  if (ib[0] & 0x08) in->firstwave = ib[0x0f] ? ib[0x0f] : 0x09;
  else in->firstwave = map->firstwave ? (unsigned char)map->firstwave : 0x09;
}

int swm_import(const char *path, char *report, int reportsize)
{
  unsigned char *d = NULL;
  int len = 0, base, pos, c, s, p;
  int nseq, npatt, ninst, nchord, ntempo, nsub;
  int seqstart[SWM_MAXSUBTUNES * MAX_CHN * 2], seqlen[SWM_MAXSUBTUNES * MAX_CHN * 2];
  int instrstart[SWM_MAXINSTR + 1], instrsize[SWM_MAXINSTR + 1];
  static SWMPATT patts[SWM_MAXPATT + 1];
  static SWMINSTRMAP maps[SWM_MAXINSTR + 1];
  int pattrows[SWM_MAXPATT + 1];
  int gtpatt[SWM_MAXPATT + 1][2];
  int instrchans[SWM_MAXINSTR + 1];
  int pattinstr[SWM_MAXPATT + 1];
  int nextpatt, emptypatt = -1;
  int subtempo = 6;
  unsigned char u[256];
  SWMLOST lost;
  char info[41];

  memset(&lost, 0, sizeof lost);
  report[0] = 0;
  if (!readfile(path, &d, &len))
  {
    snprintf(report, reportsize, "The file can't be read (or is larger than 64 KB)");
    return 0;
  }
  // A VICE .P00 file has a header of its own first
  base = (len > 0x1a + 2 && !memcmp(d, "C64File", 7)) ? 0x1a + 2 : 2;
  if ((len < base + SWM_HEADER) || (memcmp(d + base, "SWM", 3)))
  {
    snprintf(report, reportsize, "Not a SID-Wizard module");
    free(d);
    return 0;
  }
  if (d[base + 3] != '1')
  {
    snprintf(report, reportsize, "A SID-Wizard module for %s: only modules for one SID can be imported",
      (d[base + 3] == 'S') ? "2 SIDs" : (d[base + 3] == 'T') ? "3 SIDs" : "several SIDs");
    free(d);
    return 0;
  }
  nseq = d[base + 0x0c];
  npatt = d[base + 0x0d];
  ninst = d[base + 0x0e];
  nchord = d[base + 0x0f];
  ntempo = d[base + 0x10];
  nsub = nseq ? (nseq - 1) / 3 + 1 : 1;
  if ((nseq > SWM_MAXSUBTUNES * MAX_CHN) || (npatt > SWM_MAXPATT) || (ninst > SWM_MAXINSTR))
  {
    snprintf(report, reportsize, "The module has more sequences, patterns or instruments than SID-Wizard allows");
    free(d);
    return 0;
  }

  // Backwards from the end
  pos = len;
  pos -= 2 * nsub;
  if (pos >= base + SWM_HEADER)
  {
    if (d[pos] & 0x80) subtempo = d[pos] & 0x7f;
    else
    {
      subtempo = d[pos];
      lost.tempo++;
    }
    for (s = 1; s < nsub; s++)
    {
      if (d[pos + 2 * s] != d[pos]) lost.tempo++;
    }
  }
  pos -= ntempo;
  if (ntempo) lost.tempo++;
  pos -= nchord;
  if ((pos >= base + SWM_HEADER) && (pos + nchord <= len)) findchords(d + pos, nchord);
  else findchords(NULL, 0);
  for (c = ninst; c >= 1; c--)
  {
    int size;
    if (pos - 9 < base + SWM_HEADER) break;
    size = d[pos - 9];
    instrsize[c] = size;
    instrstart[c] = pos - 9 - size;
    pos = instrstart[c];
  }
  if (c >= 1) pos = -1;
  for (p = npatt; (p >= 1) && (pos >= 0); p--)
  {
    int size, rows, start = unpackpattern(d, base + SWM_HEADER, pos, u, &size, &rows);
    if (start < 0)
    {
      pos = -1;
      break;
    }
    decodepattern(u, size, &patts[p], &lost);
    pattrows[p] = patts[p].rows;
    pos = start;
  }
  for (s = nseq - 1; (s >= 0) && (pos >= 0); s--)
  {
    if (pos - 1 < base + SWM_HEADER)
    {
      pos = -1;
      break;
    }
    seqlen[s] = d[pos - 1];
    seqstart[s] = pos - 1 - seqlen[s];
    pos = seqstart[s];
  }
  if (pos != base + SWM_HEADER)
  {
    snprintf(report, reportsize, "The module's sizes don't add up (damaged, or a format this importer doesn't know)");
    free(d);
    return 0;
  }

  // The song
  stopsong();
  // (In the plugin the engine's lock: its audio thread waits)
  sound_lockplayer();
  clearsong(1, 1, 1, 1, 1);
  memset(tlen, 0, sizeof tlen);

  // Author info: "AUTHOR : TITLE"
  memcpy(info, d + base + 0x18, 40);
  info[40] = 0;
  {
    char *colon = strchr(info, ':');
    char *a = info, *t = colon ? colon + 1 : NULL;
    if (colon) *colon = 0;
    while (*a == ' ') a++;
    for (c = (int)strlen(a) - 1; (c >= 0) && (a[c] == ' '); c--) a[c] = 0;
    if (t)
    {
      while (*t == ' ') t++;
      for (c = (int)strlen(t) - 1; (c >= 0) && (t[c] == ' '); c--) t[c] = 0;
      strncpy(songname, t, MAX_STR - 1);
      strncpy(authorname, a, MAX_STR - 1);
    }
    else strncpy(songname, a, MAX_STR - 1);
  }

  // The patterns: up to 128 rows each, longer ones in two
  nextpatt = npatt;
  for (p = 1; p <= npatt; p++)
  {
    gtpatt[p][0] = p - 1;
    gtpatt[p][1] = -1;
    if ((patts[p].rows > MAX_PATTROWS) && (nextpatt < MAX_PATT))
      gtpatt[p][1] = nextpatt++;
    else if (patts[p].rows > MAX_PATTROWS) lost.patterns++;
  }

  // The channels each instrument plays on (its filter's routing)
  memset(instrchans, 0, sizeof instrchans);
  for (s = 0; s < nseq; s++)
  {
    for (c = 0; c < seqlen[s]; c++)
    {
      int b = d[seqstart[s] + c], r;
      if (b == 0xff)
      {
        c++;
        continue;
      }
      if ((b < 0x01) || (b > 0x7f) || (b > npatt)) continue;
      for (r = 0; r < patts[b].rows; r++)
      {
        if (patts[b].instr[r]) instrchans[patts[b].instr[r]] |= 1 << (s % MAX_CHN);
      }
    }
  }

  for (c = 1; c <= ninst; c++)
  {
    static unsigned char ib[256];
    memset(ib, 0, sizeof ib);
    memcpy(ib, d + instrstart[c], instrsize[c]);
    ib[instrsize[c]] = 0xff;
    importinstr(c, ib, instrsize[c], d + instrstart[c] + instrsize[c] + 1, instrchans[c] ? instrchans[c] : 7, &maps[c], &lost);
  }

  // The instrument each pattern starts with (when first played: for its
  // table jumps)
  {
    int seen[SWM_MAXPATT + 1];
    memset(seen, 0, sizeof seen);
    memset(pattinstr, 0, sizeof pattinstr);
    for (s = 0; s < nseq; s++)
    {
      int cur = 0, r;
      for (c = 0; c < seqlen[s]; c++)
      {
        int b = d[seqstart[s] + c];
        if (b == 0xff) break;
        if ((b < 0x01) || (b > 0x7f) || (b > npatt)) continue;
        if (!seen[b])
        {
          seen[b] = 1;
          pattinstr[b] = cur;
        }
        for (r = 0; r < patts[b].rows; r++)
        {
          if (patts[b].instr[r]) cur = patts[b].instr[r];
        }
      }
    }
  }

  // The patterns' rows
  for (p = 1; p <= npatt; p++)
  {
    SWMPATT *sp = &patts[p];
    SWMSTATE st;
    int r;

    memset(&st, 0, sizeof st);
    st.instr = pattinstr[p];
    st.chord = -1;
    if ((st.instr >= 1) && (st.instr <= ninst))
    {
      st.ad = instr[st.instr].ad;
      st.sr = instr[st.instr].sr;
    }
    for (r = 0; r < sp->rows; r++)
    {
      int gp = gtpatt[p][r >= MAX_PATTROWS ? 1 : 0];
      unsigned char *row;
      int note;

      if (gp < 0) break;
      row = &pattern[gp][(r % MAX_PATTROWS) * 4];
      row[0] = sp->note[r];
      row[1] = sp->instr[r];
      note = (sp->note[r] >= FIRSTNOTE) && (sp->note[r] <= LASTNOTE);
      if (sp->instr[r])
      {
        st.instr = sp->instr[r];
        st.ad = instr[st.instr].ad;
        st.sr = instr[st.instr].sr;
      }
      // A portamento into this note
      if ((note) && (st.porta))
      {
        setcmd(row, CMD_TONEPORTA, needentry(slideentry(110), &lost), &lost);
        st.porta = 0;
      }
      if (sp->col1[r])
      {
        int v = sp->col1[r];
        if ((v >= 0x60) && (v <= 0x6f)) smallfx(row, 0x80 | (v & 0x0f), &st, &lost);
        else if (v == 0x78) st.porta = 1;
        else lost.fx++;
      }
      if (sp->col2[r])
      {
        int v = sp->col2[r];
        if (v == 0x3f) setcmd(row, CMD_TONEPORTA, 0, &lost);
        else smallfx(row, v, &st, &lost);
      }
      if (sp->hasfx[r])
      {
        if (sp->fx[r] < 0x20) bigfx(row, sp->fx[r], sp->fxparam[r], st.instr, maps, &st, &lost);
        else smallfx(row, sp->fx[r], &st, &lost);
      }
      // A note with a chord other than its instrument's: the instrument's
      // wave table with that chord
      if ((note) && (st.chord >= 0) && (st.instr >= 1) && (st.instr <= ninst) && (maps[st.instr].haschord) &&
        (st.chord != (swminstr[st.instr][8] & 0x1f)))
      {
        int ptr = chordprogram(st.instr, st.chord, &lost);
        if (ptr) setcmd(row, CMD_SETWAVEPTR, ptr, &lost);
        else lost.chords++;
      }
    }
    for (c = 0; c < 2; c++)
    {
      int gp = gtpatt[p][c];
      int rows = sp->rows - c * MAX_PATTROWS;
      if (gp < 0) continue;
      rows = clampint(rows, 1, MAX_PATTROWS);
      pattern[gp][rows * 4] = ENDPATT;
    }
  }

  // The orderlists
  if (nsub > MAX_SONGS) nsub = MAX_SONGS;
  for (s = 0; s < nsub; s++)
  {
    for (c = 0; c < MAX_CHN; c++)
    {
      unsigned char *ol = songorder[s][c];
      int at[SWM_MAXSEQ + 2];
      int n = 0, i, restart = 0, ended = 1;
      int q = s * MAX_CHN + c;
      const unsigned char *seq = (q < nseq) ? d + seqstart[q] : NULL;
      int sl = (q < nseq) ? seqlen[q] : 0;

      memset(ol, 0, MAX_SONGLEN + 2);
      for (i = 0; i < sl; i++)
      {
        int b = seq[i];

        at[i] = n;
        if ((b >= 0x01) && (b <= 0x7f))
        {
          if ((b <= npatt) && (n < MAX_SONGLEN - 2))
          {
            ol[n++] = (unsigned char)gtpatt[b][0];
            if (gtpatt[b][1] >= 0) ol[n++] = (unsigned char)gtpatt[b][1];
          }
        }
        else if ((b >= 0x80) && (b <= 0x9f))
        {
          int t = b - 0x90;
          if (t > 14)
          {
            t = 14;
            lost.seq++;
          }
          if (n < MAX_SONGLEN - 2) ol[n++] = (unsigned char)((t < 0) ? TRANSDOWN + 16 + t : TRANSUP + t);
        }
        else if ((b >= 0xa0) && (b <= 0xef)) lost.seq++;
        else if (b == 0xfe) break;
        else if (b == 0xff)
        {
          int x = (i + 1 < sl) ? seq[i + 1] : 0;
          if (x < 0x80) restart = (x <= i) ? at[x] : 0;
          else lost.seq++;
          ended = 0;
          break;
        }
      }
      if (ended) lost.ends += (n > 0);
      if (!n)
      {
        // A track without patterns: an empty one
        if (emptypatt < 0)
        {
          emptypatt = (nextpatt < MAX_PATT) ? nextpatt++ : 0;
          memset(pattern[emptypatt], 0, MAX_PATTROWS * 4 + 4);
          for (i = 0; i < 0x20; i++) pattern[emptypatt][i * 4] = REST;
          pattern[emptypatt][0x20 * 4] = ENDPATT;
        }
        ol[n++] = (unsigned char)emptypatt;
      }
      if (restart >= n) restart = 0;
      ol[n] = LOOPSONG;
      ol[n + 1] = (unsigned char)restart;
    }
  }
  if (nseq > MAX_SONGS * MAX_CHN) lost.subtunes++;

  // The tempo: the tempo instrument ($3F) if not the usual 6
  if ((subtempo >= 3) && (subtempo != (editorInfo.multiplier ? 6 * editorInfo.multiplier : 6)))
  {
    clearinstr(MAX_INSTR - 1);
    instr[MAX_INSTR - 1].ad = (unsigned char)subtempo;
  }
  if (d[base + 4] > 1) lost.tempo++;

  for (c = 0; c < MAX_CHN; c++)
  {
    editorUndoInfo.editorInfo[c].epnum = songorder[0][c][0] < MAX_PATT ? songorder[0][c][0] : 0;
    editorUndoInfo.editorInfo[c].espos = 0;
    editorUndoInfo.editorInfo[c].esend = 0;
  }
  countpatternlengths();
  sound_unlockplayer();
#ifdef GT_PLUGIN
  gtsync_requestresync();
#endif

  snprintf(report, reportsize, "%d subtune%s, %d pattern%s, %d instrument%s", nsub, nsub == 1 ? "" : "s",
    npatt, npatt == 1 ? "" : "s", ninst, ninst == 1 ? "" : "s");
  reportlost(&lost, report, reportsize);
  free(d);
  return 1;
}

/*
 * Export
 */

typedef struct
{
  unsigned char *b;
  int n, size;
} SWMOUT;

static void put(SWMOUT *o, int v)
{
  if (o->n < o->size) o->b[o->n] = (unsigned char)v;
  o->n++;
}

// The command of a GoatTracker row as a SID-Wizard effect: big (returns the
// effect $01-$1F, *param set), small ($20-$FF) or 0 = none
static int exportfx(int cmd, int data, const int tablerow[][MAX_TABLELEN + 1], SWMLOST *lost, int *param)
{
  *param = 0;
  switch (cmd)
  {
    case CMD_PORTAUP:
    case CMD_PORTADOWN:
    case CMD_TONEPORTA:
    {
      int speed = 0;
      if (data)
      {
        int l = ltable[STBL][data - 1], r = rtable[STBL][data - 1];
        if (l >= 0x80) speed = 128 >> clampint(r, 0, 7);
        else speed = ((l << 8) | r) * 128 / 255;
        speed = clampint(speed, 1, (cmd == CMD_TONEPORTA) ? 0xfe : 0xff);
      }
      else if (cmd == CMD_TONEPORTA) speed = 0xff;
      *param = speed;
      return cmd;
    }

    case CMD_VIBRATO:
    {
      int amplitude = 0, rate = 0;
      if (data)
      {
        int l = ltable[STBL][data - 1], r = rtable[STBL][data - 1];
        rate = clampint(l & 0x7f, 1, 15);
        amplitude = (l >= 0x80) ? clampint(16 - 2 * r, 1, 15) : clampint(r / 16, 1, 15);
      }
      *param = (amplitude << 4) | rate;
      return 0x08;
    }

    case CMD_SETAD:
    *param = data;
    return 0x05;

    case CMD_SETSR:
    *param = data;
    return 0x06;

    case CMD_SETWAVE:
    *param = data;
    return 0x04;

    case CMD_SETWAVEPTR:
    case CMD_SETPULSEPTR:
    case CMD_SETFILTERPTR:
    {
      int t = cmd - CMD_SETWAVEPTR;
      int r = ((tablerow) && (data > 0) && (data <= MAX_TABLELEN)) ? tablerow[t][data] : -1;
      if (r < 0)
      {
        lost->fx++;
        return 0;
      }
      *param = r;
      return 0x09 + t;
    }

    case CMD_SETFILTERCTRL:
    *param = data;
    return 0x1f;

    case CMD_SETFILTERCUTOFF:
    *param = data;
    return 0x0f;

    case CMD_SETMASTERVOL:
    return 0xa0 | (data & 0x0f);

    case CMD_FUNKTEMPO:
    if (data)
    {
      *param = (clampint(ltable[STBL][data - 1], 0, 15) << 4) | clampint(rtable[STBL][data - 1], 0, 15);
      return 0x11;
    }
    lost->fx++;
    return 0;

    case CMD_SETTEMPO:
    *param = data & 0x7f;
    return (data & 0x80) ? 0x13 : 0x10;
  }
  return 0;
}

// Packs a pattern's bytes (end mark not included): a run of 3 or more zeros
// is $00 then $70-$77 for 2-9 more
static void packbytes(SWMOUT *o, const unsigned char *u, int n)
{
  int i = 0;

  while (i < n)
  {
    int z = 0;
    while ((i + z < n) && (!u[i + z])) z++;
    if (z >= 3)
    {
      put(o, 0);
      z--;
      i++;
      while (z >= 2)
      {
        int k = (z > 9) ? 9 : z;
        if (z - k == 1) k--;
        if (k < 2) break;
        put(o, 0x6e + k);
        z -= k;
        i += k;
      }
      while (z > 0)
      {
        put(o, 0);
        z--;
        i++;
      }
    }
    else
    {
      put(o, u[i]);
      i++;
    }
  }
}

// One GoatTracker row as SID-Wizard bytes (1-4)
static int encoderow(const unsigned char *row, const int tablerow[][MAX_TABLELEN + 1], SWMLOST *lost, unsigned char *out)
{
  int c1 = 0, c2 = 0, fx, param, n = 0;
  int note = row[0], ins = row[1];

  if ((note >= FIRSTNOTE) && (note <= LASTNOTE)) c1 = note - 0x5f;
  else if (note == KEYOFF) c1 = 0x7e;
  else if (note == KEYON) c1 = 0x7d;
  if ((ins >= 1) && (ins <= SWM_MAXINSTR)) c2 = ins;
  else if (ins) lost->instr++;
  fx = exportfx(row[2], row[3], tablerow, lost, &param);
  out[n++] = (unsigned char)(c1 | ((c2 || fx) ? 0x80 : 0));
  if (c2 || fx)
  {
    out[n++] = (unsigned char)(c2 | (fx ? 0x80 : 0));
    if (fx)
    {
      out[n++] = (unsigned char)fx;
      if (fx < 0x20) out[n++] = (unsigned char)param;
    }
  }
  return n;
}

// Copies GoatTracker table t into an instrument's table at byte offset at:
// the rows from each of starts (the instrument's own first, 0 = none) up to
// their jumps. Rows copied already are jumped to instead. tablerow gets the
// SID-Wizard row of each GoatTracker row copied (-1 = not copied). Returns
// the bytes written, the end mark ($FF) included
static int exporttable(int t, const int *startlist, int nstartlist, unsigned char *ib, int at, int room, int *tablerow, SWMLOST *lost)
{
  int jumpat[MAX_TABLELEN], jumpto[MAX_TABLELEN];
  int starts[MAX_TABLELEN + SWM_MAXEXTRA + 1];
  int njumps = 0, n = 0, k, c, full = 0, nstarts = nstartlist;
  int lastband = 0x10, lastres = 0;

  for (c = 0; c <= MAX_TABLELEN; c++) tablerow[c] = -1;
  for (c = 0; c < nstartlist; c++) starts[c] = startlist[c];
  for (k = 0; (k < nstarts) && (!full); k++)
  {
    int g = starts[k];

    // No table of its own, but programs the patterns go to: the table it
    // starts with on each note stops at once
    if ((!k) && (!g) && (nstarts > 1))
    {
      jumpat[njumps] = at + n;
      jumpto[njumps++] = 0;
      ib[at + n] = 0xfe;
      ib[at + n + 1] = 0;
      ib[at + n + 2] = 0;
      n += 3;
      continue;
    }
    if ((g < 1) || (g > MAX_TABLELEN) || (tablerow[g] >= 0)) continue;
    if ((n + 3 + 1 > room) || (njumps >= MAX_TABLELEN - 1))
    {
      lost->tables++;
      break;
    }
    for (;;)
    {
      int l, r, end = 0;

      if ((g >= 1) && (g <= MAX_TABLELEN) && (tablerow[g] >= 0))
      {
        // Into rows copied already
        jumpat[njumps] = at + n;
        jumpto[njumps++] = g;
        end = 1;
      }
      else if ((g < 1) || (g > MAX_TABLELEN) || (ltable[t][g - 1] == 0xff))
      {
        int to = ((g >= 1) && (g <= MAX_TABLELEN)) ? rtable[t][g - 1] : 0;
        // The jump row is a row too (one to itself stays where it is)
        if ((g >= 1) && (g <= MAX_TABLELEN)) tablerow[g] = n / 3;
        jumpat[njumps] = at + n;
        jumpto[njumps++] = to;
        // A jump to rows not copied: they are copied too
        if ((to >= 1) && (to <= MAX_TABLELEN) && (tablerow[to] < 0) && (nstarts < MAX_TABLELEN + SWM_MAXEXTRA + 1))
          starts[nstarts++] = to;
        end = 1;
      }
      else if (n + 3 + 3 + 1 > room)
      {
        // No more room: a stop
        lost->tables++;
        jumpat[njumps] = at + n;
        jumpto[njumps++] = 0;
        end = 1;
        full = 1;
      }
      if (end)
      {
        ib[at + n] = 0xfe;
        ib[at + n + 1] = 0;
        ib[at + n + 2] = 0;
        n += 3;
        break;
      }
      l = ltable[t][g - 1];
      r = rtable[t][g - 1];
      tablerow[g] = n / 3;
      if (t == WTBL)
      {
        int wave = l, note = r;

        // A waveform register value without a waveform (SID-Wizard's table
        // takes $10 and up): with the test bit, silent as well
        if ((wave >= WAVESILENT) && (wave <= WAVELASTSILENT)) wave = 0x18 | (wave & 0x07);
        else if (wave >= WAVECMD)
        {
          wave = 0;
          lost->tables++;
        }
        if (note == 0x80) ;
        else if (note > 0x80)
        {
          // SID-Wizard's absolute notes end at $DF
          if (note + 1 > 0xdf) lost->tables++;
          note = clampint(note + 1, 0x81, 0xdf);
        }
        else if (note >= 0x60) note = note + 0x80;
        ib[at + n] = (unsigned char)wave;
        ib[at + n + 1] = (unsigned char)note;
        ib[at + n + 2] = 0;
      }
      else if (t == PTBL)
      {
        ib[at + n] = (unsigned char)l;
        ib[at + n + 1] = (unsigned char)r;
        ib[at + n + 2] = 0;
      }
      else
      {
        if (l >= 0x80)
        {
          // Type, resonance & channels, and the cutoff row after it
          int cutoff = 0;
          lastband = l & 0x70;
          lastres = r >> 4;
          if ((g < MAX_TABLELEN) && (ltable[t][g] == 0x00))
          {
            cutoff = rtable[t][g];
            g++;
            tablerow[g] = n / 3;
          }
          ib[at + n] = (unsigned char)(0x80 | lastband | lastres);
          ib[at + n + 1] = (unsigned char)cutoff;
          ib[at + n + 2] = (unsigned char)(0x80 | (r & 0x07));
        }
        else if (l == 0x00)
        {
          ib[at + n] = (unsigned char)(0x80 | lastband | lastres);
          ib[at + n + 1] = (unsigned char)r;
          ib[at + n + 2] = 0;
        }
        else
        {
          ib[at + n] = (unsigned char)l;
          ib[at + n + 1] = (unsigned char)clampint((signed char)r * 8, -128, 127);
          ib[at + n + 2] = 0;
        }
      }
      n += 3;
      g++;
    }
  }
  // The jumps' targets: rows copied (byte offsets in the instrument), else
  // a stop (a jump to itself; in the wave table $80 and up)
  for (k = 0; k < njumps; k++)
  {
    int g = jumpto[k];
    int target = jumpat[k];

    if ((g >= 1) && (g <= MAX_TABLELEN) && (tablerow[g] >= 0)) target = at + 3 * tablerow[g];
    else if (t == WTBL) target = 0xd0;
    ib[jumpat[k] + 1] = (unsigned char)target;
  }
  ib[at + n] = 0xff;
  return n + 1;
}

int swm_export(const char *path, char *report, int reportsize)
{
  static unsigned char buf[SWM_MAXFILE];
  static int tablerow[SWM_MAXINSTR + 1][3][MAX_TABLELEN + 1];
  static int pattinstr[MAX_PATT];
  static int extra[SWM_MAXINSTR + 1][3][SWM_MAXEXTRA], nextra[SWM_MAXINSTR + 1][3];
  SWMOUT o;
  SWMLOST lost;
  int nsub = 0, nseq, npatt = 0, ninst = 0;
  int swmpatt[MAX_PATT][4], npieces[MAX_PATT];
  int s, c, p, i;
  FILE *f;
  char info[64];

  memset(&lost, 0, sizeof lost);
  report[0] = 0;
  o.b = buf;
  o.n = 0;
  o.size = sizeof buf;
  countpatternlengths();

  // The subtunes with music (at most 8)
  for (s = 0; s < MAX_SONGS; s++)
  {
    if ((songlen[s][0]) || (songlen[s][1]) || (songlen[s][2])) nsub = s + 1;
  }
  if (!nsub) nsub = 1;
  if (nsub > SWM_MAXSUBTUNES)
  {
    lost.subtunes += nsub - SWM_MAXSUBTUNES;
    nsub = SWM_MAXSUBTUNES;
  }
  nseq = nsub * MAX_CHN;

  // The instruments up to the last one in use (63 is the tempo / RESO one)
  for (c = 1; c <= SWM_MAXINSTR; c++)
  {
    INSTR *in = &instr[c];
    if ((in->ad) || (in->sr) || (in->ptr[WTBL]) || (in->ptr[PTBL]) || (in->ptr[FTBL]) || (in->name[0])) ninst = c;
  }

  // The patterns in the orderlists exported, numbered in order; one too
  // big for SID-Wizard ($F9 bytes or $F8 rows) goes in pieces (the rows
  // where each starts in swmpatt)
  for (p = 0; p < MAX_PATT; p++) npieces[p] = 0;
  for (s = 0; s < nsub; s++)
  {
    for (c = 0; c < MAX_CHN; c++)
    {
      for (i = 0; i < songlen[s][c]; i++)
      {
        static const int sizingrows[3][MAX_TABLELEN + 1];
        int gp = songorder[s][c][i], at = 0, bytes = 0, r;
        unsigned char tmp[8];

        if ((gp >= MAX_PATT) || (npieces[gp])) continue;
        swmpatt[gp][0] = 0;
        npieces[gp] = 1;
        for (r = 0; r < pattlen[gp]; r++)
        {
          int k = encoderow(&pattern[gp][r * 4], sizingrows, &lost, tmp);
          if ((bytes + k + 1 > SWM_MAXPATTSIZE) || (r - at >= SWM_MAXPATTROWS))
          {
            if (npieces[gp] < 4) swmpatt[gp][npieces[gp]++] = r;
            else lost.patterns++;
            at = r;
            bytes = 0;
          }
          bytes += k;
        }
      }
    }
  }
  // (exportfx() was counted for the sizes: counted again when written)
  memset(&lost, 0, sizeof lost);
  if (nsub < MAX_SONGS)
  {
    for (s = nsub; s < MAX_SONGS; s++)
    {
      if ((songlen[s][0]) || (songlen[s][1]) || (songlen[s][2])) lost.subtunes++;
    }
  }

  // The instrument each pattern starts with (when first played), and the
  // table programs the patterns' commands go to while each instrument plays
  // (copied into the instrument's tables: SID-Wizard's are its own)
  {
    static int seen[MAX_PATT];
    memset(seen, 0, sizeof seen);
    memset(pattinstr, 0, sizeof pattinstr);
    memset(nextra, 0, sizeof nextra);
    for (s = 0; s < nsub; s++)
    {
      for (c = 0; c < MAX_CHN; c++)
      {
        int cur = 0;
        for (i = 0; i < songlen[s][c]; i++)
        {
          int gp = songorder[s][c][i], r;
          if (gp >= MAX_PATT) continue;
          if (!seen[gp])
          {
            seen[gp] = 1;
            pattinstr[gp] = cur;
          }
          for (r = 0; r < pattlen[gp]; r++)
          {
            unsigned char *row = &pattern[gp][r * 4];
            if (row[1]) cur = row[1];
            if ((row[2] >= CMD_SETWAVEPTR) && (row[2] <= CMD_SETFILTERPTR) && (row[3]) && (cur >= 1) && (cur <= ninst))
            {
              int t = row[2] - CMD_SETWAVEPTR, k;
              for (k = 0; k < nextra[cur][t]; k++)
              {
                if (extra[cur][t][k] == row[3]) break;
              }
              if ((k == nextra[cur][t]) && (k < SWM_MAXEXTRA)) extra[cur][t][nextra[cur][t]++] = row[3];
            }
          }
        }
      }
    }
  }

  // SID-Wizard pattern numbers
  {
    int number[MAX_PATT];
    for (p = 0; p < MAX_PATT; p++)
    {
      number[p] = 0;
      if (npieces[p])
      {
        number[p] = npatt + 1;
        npatt += npieces[p];
      }
    }
    if (npatt > SWM_MAXPATT)
    {
      snprintf(report, reportsize, "The song needs %d SID-Wizard patterns: SID-Wizard has at most %d", npatt, SWM_MAXPATT);
      return 0;
    }

    // Header
    put(&o, SWM_LOADADDR & 0xff);
    put(&o, SWM_LOADADDR >> 8);
    put(&o, 'S');
    put(&o, 'W');
    put(&o, 'M');
    put(&o, '1');
    put(&o, clampint(editorInfo.multiplier, 1, 8));
    put(&o, clampint(stepsize / (resomode > 0 ? resomode : 1), 1, 0xff));
    put(&o, 1);
    put(&o, 1);
    put(&o, 0xff);
    put(&o, 0xff);
    put(&o, 0xff);
    put(&o, 0x20);
    put(&o, nseq);
    put(&o, npatt);
    put(&o, ninst);
    put(&o, 0);
    put(&o, 0);
    put(&o, 0);
    put(&o, 0);
    put(&o, 0);
    put(&o, 0);
    put(&o, 0);
    put(&o, 0);
    put(&o, 0);
    snprintf(info, sizeof info, "%s : %s", authorname, songname);
    for (c = 0; c < 40; c++)
    {
      int ch = (c < (int)strlen(info)) ? toupper((unsigned char)info[c]) : ' ';
      if ((ch < 32) || (ch > 126)) ch = ' ';
      put(&o, ch);
    }

    // The sequences
    for (s = 0; s < nsub; s++)
    {
      for (c = 0; c < MAX_CHN; c++)
      {
        unsigned char seq[256];
        int pos[MAX_SONGLEN + 2];
        int n = 0, repeat = 1;
        int restart = songorder[s][c][songlen[s][c] + 1];

        for (i = 0; i < songlen[s][c]; i++)
        {
          int b = songorder[s][c][i];
          pos[i] = n;
          if (b < MAX_PATT)
          {
            int k, rpt;
            for (rpt = 0; rpt < repeat; rpt++)
            {
              for (k = 0; k < npieces[b]; k++)
              {
                if (n < SWM_MAXSEQ - 2) seq[n++] = (unsigned char)(number[b] + k);
              }
            }
            repeat = 1;
          }
          else if ((b >= REPEAT) && (b < TRANSDOWN)) repeat = b - REPEAT + 1;
          else if ((b >= TRANSDOWN) && (b < LOOPSONG)) seq[n++] = (unsigned char)(0x90 + ((b >= TRANSUP) ? b - TRANSUP : b - TRANSDOWN - 16));
        }
        if (n >= SWM_MAXSEQ - 2)
        {
          snprintf(report, reportsize, "Subtune %d track %d's orderlist is too long for SID-Wizard (%d bytes at most)", s, c + 1, SWM_MAXSEQ);
          return 0;
        }
        if (!n)
        {
          seq[n++] = 0xfe;
        }
        else
        {
          seq[n++] = 0xff;
          seq[n++] = (unsigned char)(((restart >= 0) && (restart < songlen[s][c])) ? pos[restart] : 0);
        }
        for (i = 0; i < n; i++) put(&o, seq[i]);
        put(&o, n);
      }
    }

    // The instruments' tables first: the pattern commands need their rows
    {
      static unsigned char ibs[SWM_MAXINSTR + 1][SWM_MAXINSTRSIZE + 16];
      static int isize[SWM_MAXINSTR + 1];

      for (c = 1; c <= ninst; c++)
      {
        INSTR *in = &instr[c];
        unsigned char *ib = ibs[c];
        int at, k, t;

        memset(ib, 0, SWM_MAXINSTRSIZE + 16);
        k = in->gatetimer & 0x3f;
        ib[0] = (unsigned char)(clampint(k, 0, 2) | 0x08 | 0x10);
        if (in->gatetimer & 0x40) ib[0] &= ~3;
        ib[1] = (unsigned char)(editorInfo.adparam >> 8);
        ib[2] = (unsigned char)(editorInfo.adparam & 0xff);
        ib[3] = in->ad;
        ib[4] = in->sr;
        if (in->ptr[STBL])
        {
          int l = ltable[STBL][in->ptr[STBL] - 1], r = rtable[STBL][in->ptr[STBL] - 1];
          int rate = clampint(l & 0x7f, 1, 15);
          int amplitude = (l >= 0x80) ? clampint(16 - 2 * r, 1, 15) : clampint(r / 16, 1, 15);
          ib[5] = (unsigned char)((amplitude << 4) | rate);
        }
        ib[6] = in->vibdelay;
        ib[7] = 0;
        ib[8] = 0;
        ib[9] = 0;
        ib[0x0f] = in->firstwave ? in->firstwave : 0x09;
        at = SWM_TABLESTART;
        for (t = 0; t < 3; t++)
        {
          int room = SWM_MAXINSTRSIZE - at - 4 * (2 - t);
          int starts[SWM_MAXEXTRA + 1], e;
          starts[0] = in->ptr[t];
          for (e = 0; e < nextra[c][t]; e++) starts[e + 1] = extra[c][t][e];
          if (t == 1) ib[0x0a] = (unsigned char)at;
          if (t == 2) ib[0x0b] = (unsigned char)at;
          at += exporttable(t, starts, nextra[c][t] + 1, ib, at, room, tablerow[c][t], &lost);
        }
        // The size: the last $FF (the filter table's end) gives way to it
        isize[c] = at - 1;
      }

      // The patterns
      for (p = 0; p < MAX_PATT; p++)
      {
        int k;
        for (k = 0; k < npieces[p]; k++)
        {
          unsigned char u[512];
          int from = swmpatt[p][k];
          int to = (k + 1 < npieces[p]) ? swmpatt[p][k + 1] : pattlen[p];
          int n = 0, r, cur = pattinstr[p];

          for (r = 0; r < from; r++)
          {
            if (pattern[p][r * 4 + 1]) cur = pattern[p][r * 4 + 1];
          }
          for (r = from; (r < to) && (n < SWM_MAXPATTSIZE); r++)
          {
            if (pattern[p][r * 4 + 1]) cur = pattern[p][r * 4 + 1];
            n += encoderow(&pattern[p][r * 4],
              ((cur >= 1) && (cur <= ninst)) ? (const int (*)[MAX_TABLELEN + 1])tablerow[cur] : NULL, &lost, u + n);
          }
          if (n + 1 > SWM_MAXPATTSIZE) n = SWM_MAXPATTSIZE - 1;
          packbytes(&o, u, n);
          put(&o, n + 1);
          put(&o, clampint(to - from, 1, SWM_MAXPATTROWS));
        }
      }

      // The instruments
      for (c = 1; c <= ninst; c++)
      {
        int k;
        for (k = 0; k < isize[c]; k++) put(&o, ibs[c][k]);
        put(&o, isize[c]);
        for (k = 0; k < SWM_NAMELEN; k++)
        {
          int ch = (k < (int)strlen(instr[c].name)) ? toupper((unsigned char)instr[c].name[k]) : ' ';
          if ((ch < 32) || (ch > 126)) ch = ' ';
          put(&o, ch);
        }
      }
    }
  }

  // No chords, no tempo programs; each subtune's tempo
  for (s = 0; s < nsub; s++)
  {
    INSTR *ti = &instr[MAX_INSTR - 1];
    int tempo = ((ti->ad >= 3) && (!ti->ptr[WTBL])) ? ti->ad : 6;
    put(&o, 0x80 | clampint(tempo, 0, 0x7f));
    put(&o, 0x83);
  }

  if (o.n > o.size)
  {
    snprintf(report, reportsize, "The song is too large for a SID-Wizard module");
    return 0;
  }
  f = fopen(path, "wb");
  if (!f)
  {
    snprintf(report, reportsize, "The file can't be written");
    return 0;
  }
  if (fwrite(buf, 1, o.n, f) != (size_t)o.n)
  {
    fclose(f);
    snprintf(report, reportsize, "The file can't be written");
    return 0;
  }
  fclose(f);
  snprintf(report, reportsize, "%d subtune%s, %d pattern%s, %d instrument%s", nsub, nsub == 1 ? "" : "s",
    npatt, npatt == 1 ? "" : "s", ninst, ninst == 1 ? "" : "s");
  reportlost(&lost, report, reportsize);
  return 1;
}
