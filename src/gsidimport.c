//
// GOATTRACKER v2 SID file import: C64 music file (.sid) -> song (.sng)
//
// See gsidimport.h for what the conversion does. This file doesn't depend on
// the rest of the tracker, so the sid2sng tool can use it too.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include "gcommon.h"
#include "gsngwrite.h"
#include "cpu6502.h"
#include "gsidimport.h"

#define SID_MAXSIZE (0x10000 + 0x400)
#define NUMNOTES (LASTNOTE - FIRSTNOTE + 1)

typedef struct
{
  SIDINFO info;
  unsigned flags;
  const unsigned char *data;      // C64 data
  unsigned char *buffer;          // Whole file
} SIDFILE;

static void setreport(char *report, int reportsize, const char *fmt, ...)
{
  va_list ap;
  if ((!report) || (reportsize <= 0)) return;
  va_start(ap, fmt);
  vsnprintf(report, reportsize, fmt, ap);
  va_end(ap);
  report[reportsize - 1] = 0;
}

static void addreport(char *report, int reportsize, const char *fmt, ...)
{
  va_list ap;
  int len;
  if ((!report) || (reportsize <= 0)) return;
  len = (int)strlen(report);
  if (len >= reportsize - 1) return;
  va_start(ap, fmt);
  vsnprintf(report + len, reportsize - len, fmt, ap);
  va_end(ap);
  report[reportsize - 1] = 0;
}

void sid2sng_defaults(SID2SNG_OPTIONS *opt)
{
  opt->mode = SID2SNG_AUTO;
  opt->subtune = 0;
  opt->seconds = 180;
  opt->rowsperbeat = 0;
  opt->tempo = 0;
  opt->patternlength = 64;
  opt->loops = 1;
}

//
// SID file header
//

static unsigned be16(const unsigned char *p)
{
  return (p[0] << 8) | p[1];
}

static void copyheadertext(char *dest, const unsigned char *src)
{
  int c;
  for (c = 0; (c < 32) && (src[c]); c++) dest[c] = (src[c] >= 32) ? (char)src[c] : ' ';
  dest[c] = 0;
}

// Parses the header & finds the C64 data. Returns 1 on success, else 0 with
// the reason in err
static int parsesid(unsigned char *buf, long size, SIDFILE *sf, char *err, int errsize)
{
  SIDINFO *i = &sf->info;
  unsigned dataoffset;
  long datasize;

  memset(sf, 0, sizeof(SIDFILE));
  sf->buffer = buf;
  if ((size < 0x76) || ((memcmp(buf, "PSID", 4)) && (memcmp(buf, "RSID", 4))))
  {
    setreport(err, errsize, "Not a SID file (no PSID/RSID header)");
    return 0;
  }
  i->rsid = (buf[0] == 'R');
  i->version = be16(buf + 4);
  dataoffset = be16(buf + 6);
  if ((i->version < 1) || (i->version > 4) || (dataoffset < 0x76) || ((long)dataoffset > size))
  {
    setreport(err, errsize, "Unsupported or damaged SID header (version %d)", i->version);
    return 0;
  }
  i->loadaddress = be16(buf + 8);
  i->initaddress = be16(buf + 10);
  i->playaddress = be16(buf + 12);
  i->songs = be16(buf + 14);
  i->startsong = be16(buf + 16);
  i->speed = ((unsigned)be16(buf + 18) << 16) | be16(buf + 20);
  copyheadertext(i->title, buf + 0x16);
  copyheadertext(i->author, buf + 0x36);
  copyheadertext(i->released, buf + 0x56);
  if ((i->version >= 2) && (dataoffset >= 0x7c)) sf->flags = be16(buf + 0x76);
  i->clock = (sf->flags >> 2) & 3;
  i->model = (sf->flags >> 4) & 3;
  if (i->songs < 1) i->songs = 1;
  if (i->songs > 256) i->songs = 256;
  if ((i->startsong < 1) || (i->startsong > i->songs)) i->startsong = 1;

  sf->data = buf + dataoffset;
  datasize = size - dataoffset;
  if (!i->loadaddress)
  {
    if (datasize < 2)
    {
      setreport(err, errsize, "The SID file has no C64 data");
      return 0;
    }
    i->loadaddress = sf->data[0] | (sf->data[1] << 8);
    sf->data += 2;
    datasize -= 2;
  }
  if (datasize < 1)
  {
    setreport(err, errsize, "The SID file has no C64 data");
    return 0;
  }
  // Data past the end of the C64 memory is left out
  if (i->loadaddress + datasize > 0x10000) datasize = 0x10000 - i->loadaddress;
  i->datasize = (unsigned)datasize;
  if (!i->initaddress) i->initaddress = i->loadaddress;
  return 1;
}

static unsigned char *readfile(const char *path, long *size)
{
  FILE *f = fopen(path, "rb");
  unsigned char *buf;
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  *size = ftell(f);
  fseek(f, 0, SEEK_SET);
  if ((*size <= 0) || (*size > SID_MAXSIZE))
  {
    fclose(f);
    return NULL;
  }
  buf = (unsigned char *)malloc(*size);
  if ((!buf) || (fread(buf, 1, *size, f) != (size_t)*size))
  {
    free(buf);
    fclose(f);
    return NULL;
  }
  fclose(f);
  return buf;
}

int sidinfo_read(const char *sidpath, SIDINFO *info)
{
  long size;
  unsigned char *buf = readfile(sidpath, &size);
  SIDFILE sf;
  int ok;
  if (!buf) return 0;
  ok = parsesid(buf, size, &sf, NULL, 0);
  if (ok) *info = sf.info;
  free(buf);
  return ok;
}

static void sngtexts(SNGDATA *sng, const SIDINFO *i)
{
  sngdata_settext(sng->songname, i->title);
  sngdata_settext(sng->authorname, i->author);
  sngdata_settext(sng->copyrightname, i->released);
}

//
// EXACT mode: GoatTracker 2 packed songs
//
// The packer (greloc.c) puts the player first and then the data, always in
// this order: frequency table, song table (lo/hi), pattern table (lo/hi),
// instrument columns, wave/pulse/filter/speed tables, orderlists, patterns.
// The data addresses are found from the player code by matching the
// instruction sequences that use them, and checked by that layout.
//

#define MAXVARIANTS 32
#define MAXPATTERNTEXT 200
#define MAXTOKENS 48

// Pattern tokens: exact byte 00-ff, any byte, low/high byte of a capture
#define TOK_ANY 0x100
#define TOK_LO 0x200
#define TOK_HI 0x300

typedef struct
{
  int len;
  unsigned short tok[MAXTOKENS];
} PVARIANT;

typedef struct
{
  int num;
  PVARIANT v[MAXVARIANTS];
} PATTERN;

typedef struct
{
  const unsigned char *mem;       // 64K
  unsigned lo, hi;                // Loaded area [lo, hi)
  unsigned cap[26];               // Captured words of the last match
} SCAN;

// Turns "[optional]" and "(alt | alt)" groups into plain token lists
static void expandpattern(const char *text, PATTERN *pt)
{
  const char *open = NULL;
  const char *s;
  int depth = 0;

  for (s = text; *s; s++)
  {
    if ((*s == '[') || (*s == '('))
    {
      open = s;
      break;
    }
  }

  if (!open)
  {
    // Plain: compile the tokens
    PVARIANT *v;
    if (pt->num >= MAXVARIANTS) return;
    v = &pt->v[pt->num++];
    v->len = 0;
    s = text;
    for (;;)
    {
      while (*s == ' ') s++;
      if (!*s) break;
      if (v->len >= MAXTOKENS) break;
      if ((s[0] == '?') && (s[1] == '?')) v->tok[v->len++] = TOK_ANY;
      else if (s[0] == '<') v->tok[v->len++] = TOK_LO + (s[1] - 'a');
      else if (s[0] == '>') v->tok[v->len++] = TOK_HI + (s[1] - 'a');
      else v->tok[v->len++] = (unsigned short)strtol(s, NULL, 16);
      while ((*s) && (*s != ' ')) s++;
    }
    return;
  }
  else
  {
    const char *close = NULL;
    const char *alt[MAXVARIANTS];
    int altlen[MAXVARIANTS];
    int numalt = 0;
    int c;

    alt[0] = open + 1;
    for (s = open; *s; s++)
    {
      if ((*s == '[') || (*s == '(')) depth++;
      if ((*s == ']') || (*s == ')'))
      {
        depth--;
        if (!depth)
        {
          close = s;
          break;
        }
      }
      if ((*s == '|') && (depth == 1) && (numalt < MAXVARIANTS - 1))
      {
        altlen[numalt] = (int)(s - alt[numalt]);
        numalt++;
        alt[numalt] = s + 1;
      }
    }
    if (!close) return;
    altlen[numalt] = (int)(close - alt[numalt]);
    numalt++;
    if (*open == '[')
    {
      alt[numalt] = close;
      altlen[numalt] = 0;
      numalt++;
    }
    for (c = 0; c < numalt; c++)
    {
      char buf[MAXPATTERNTEXT * 2];
      int pre = (int)(open - text);
      if (pre + altlen[c] + strlen(close + 1) + 3 >= sizeof buf) continue;
      memcpy(buf, text, pre);
      buf[pre] = ' ';
      memcpy(buf + pre + 1, alt[c], altlen[c]);
      buf[pre + 1 + altlen[c]] = ' ';
      strcpy(buf + pre + 2 + altlen[c], close + 1);
      expandpattern(buf, pt);
    }
  }
}

static int matchvariant(SCAN *sc, const PVARIANT *v, unsigned pos)
{
  unsigned cap[26];
  int c;
  if (pos + v->len > sc->hi) return 0;
  memset(cap, 0, sizeof cap);
  for (c = 0; c < v->len; c++)
  {
    unsigned t = v->tok[c];
    unsigned b = sc->mem[pos + c];
    if (t < 0x100)
    {
      if (b != t) return 0;
    }
    else if ((t & 0xff00) == TOK_LO) cap[t & 0xff] |= b;
    else if ((t & 0xff00) == TOK_HI) cap[t & 0xff] |= b << 8;
  }
  memcpy(sc->cap, cap, sizeof cap);
  return 1;
}

// Finds the pattern at or after *pos. Returns 1 & sets *pos if found
static int findnext(SCAN *sc, const char *text, unsigned *pos)
{
  PATTERN pt;
  unsigned p;
  int c;
  pt.num = 0;
  expandpattern(text, &pt);
  for (p = *pos; p < sc->hi; p++)
  {
    for (c = 0; c < pt.num; c++)
    {
      if (matchvariant(sc, &pt.v[c], p))
      {
        *pos = p;
        return 1;
      }
    }
  }
  return 0;
}

static int findfirst(SCAN *sc, const char *text)
{
  unsigned pos = sc->lo;
  return findnext(sc, text, &pos);
}

static unsigned word(const unsigned char *mem, unsigned adr)
{
  return mem[adr & 0xffff] | (mem[(adr + 1) & 0xffff] << 8);
}

typedef struct
{
  // Found in the player code
  unsigned base;
  int multiplier;                 // -1 = not known (no speed code), 0 = 25Hz
  unsigned songtbllo, songtblhi, patttbllo, patttblhi;
  unsigned insad;
  int numsongs, numpatt, numinstr;
  int columns;
  int haspulse, hasfilter, hasvib, fixedparams;
  int fixedgatetimer, fixedfirstwave;
  int wavedelay, simplepulse;
  unsigned tblleft[MAX_TABLES], tblright[MAX_TABLES];
  int tbllen[MAX_TABLES];
  int defaulttempo;
  int firstnohr, firstlegato;     // Instrument number boundaries
  int hrcode;                     // Hardrestart code present
  unsigned adparam;
} GTPLAYER;

static int gtfail(char *err, int errsize, const char *text)
{
  setreport(err, errsize, "%s", text);
  return 0;
}

// Finds the player & data. Returns 1 on success, else 0 with the reason
static int findgtplayer(SCAN *sc, const SIDFILE *sf, GTPLAYER *gp, char *err, int errsize)
{
  const unsigned char *mem = sc->mem;
  unsigned pos, chncounter, chninstr, chnwave, chngate;
  unsigned tblend;
  int c;

  memset(gp, 0, sizeof(GTPLAYER));
  gp->multiplier = -1;

  // Jump table, possibly after the CIA timer setup of multispeed songs
  gp->base = sf->info.initaddress;
  if ((mem[gp->base] == 0xa2) && (mem[gp->base + 2] == 0x8e) && (mem[gp->base + 3] == 0x04) &&
      (mem[gp->base + 4] == 0xdc) && (mem[gp->base + 5] == 0xa2) && (mem[gp->base + 7] == 0x8e) &&
      (mem[gp->base + 8] == 0x05) && (mem[gp->base + 9] == 0xdc))
  {
    unsigned speed = mem[gp->base + 1] | (mem[gp->base + 6] << 8);
    unsigned full = (sf->info.clock == 2) ? 0x42c6 : 0x4cc7;
    if (speed >= full * 3 / 2) gp->multiplier = 0;
    else if (speed) gp->multiplier = (full + speed / 2) / speed;
    gp->base += 10;
  }
  if ((mem[gp->base] != 0x4c) || (mem[gp->base + 3] != 0x4c) || (sf->info.playaddress != gp->base + 3))
    return gtfail(err, errsize, "No GoatTracker 2 player (jump table) found");

  // Sequencer: song table
  if (!findfirst(sc, "BC ?? ?? B9 <a >a 85 ?? B9 <b >b 85 ?? BC ?? ?? B1 ?? C9 FF"))
    return gtfail(err, errsize, "No GoatTracker 2 sequencer code found");
  gp->songtbllo = sc->cap[0];
  gp->songtblhi = sc->cap[1];
  // New note fetch: pattern table
  if (!findfirst(sc, "BC ?? ?? B9 <a >a 85 ?? B9 <b >b 85 ?? BC ?? ?? B1 ?? C9 40"))
    return gtfail(err, errsize, "No GoatTracker 2 pattern code found");
  gp->patttbllo = sc->cap[0];
  gp->patttblhi = sc->cap[1];

  if ((gp->songtblhi <= gp->songtbllo) || ((gp->songtblhi - gp->songtbllo) % 3) ||
      (gp->songtblhi - gp->songtbllo > MAX_SONGS * 3) ||
      (gp->patttbllo != gp->songtblhi + (gp->songtblhi - gp->songtbllo)) ||
      (gp->patttblhi <= gp->patttbllo) || (gp->patttblhi - gp->patttbllo > MAX_PATT))
    return gtfail(err, errsize, "GoatTracker 2 song/pattern tables not as expected");
  gp->numsongs = (gp->songtblhi - gp->songtbllo) / 3;
  gp->numpatt = gp->patttblhi - gp->patttbllo;
  gp->insad = gp->patttblhi + gp->numpatt;

  // Instrument AD & SR are loaded together (SR first, or AD first in the
  // alternative player): the distance tells the number of instruments.
  // (The alternative player loads the wavepointer just before AD: that
  // pair is twice as far apart)
  pos = sc->lo;
  while (findnext(sc, "B9 <a >a (9D ?? ?? | 95 ?\?) B9 <b >b (9D ?? ?? | 95 ?\?)", &pos))
  {
    unsigned a = sc->cap[0], b = sc->cap[1];
    if ((a != b) && (((a < b) ? a : b) + 1 == gp->insad))
    {
      int dist = (a < b) ? b - a : a - b;
      if ((!gp->numinstr) || (dist < gp->numinstr)) gp->numinstr = dist;
    }
    pos++;
  }
  if ((gp->numinstr < 1) || (gp->numinstr >= MAX_INSTR))
    return gtfail(err, errsize, "GoatTracker 2 instrument data not found");

  // Wave, pulse & filter table jumps: "lda left,y; cmp #$ff; iny; tya;
  // bcc; [clc;] lda right-1,y; sta ptr". The wavetable is the first after
  // the instruments; pulse stores the pointer with sta abs,x, filter abs
  for (c = 0; c < MAX_TABLES; c++) gp->tbllen[c] = -1;
  pos = sc->lo;
  while (findnext(sc, "B9 <a >a C9 FF C8 98 90 ?? [18] B9 <b >b <c", &pos))
  {
    unsigned left = sc->cap[0], right = sc->cap[1] + 1;
    unsigned st = sc->cap[2];
    pos++;
    if ((right < left) || (right - left > MAX_TABLELEN)) continue;
    if ((left > gp->insad) && (!((left - gp->insad) % gp->numinstr)) && (st == 0x9d) &&
        ((left - gp->insad) / gp->numinstr >= 3) && ((left - gp->insad) / gp->numinstr <= 9))
    {
      if ((gp->tbllen[WTBL] < 0) || (left < gp->tblleft[WTBL]))
      {
        gp->tblleft[WTBL] = left;
        gp->tblright[WTBL] = right;
        gp->tbllen[WTBL] = right - left;
      }
    }
  }
  if (gp->tbllen[WTBL] < 0) return gtfail(err, errsize, "GoatTracker 2 wavetable not found");
  tblend = gp->tblright[WTBL] + gp->tbllen[WTBL];
  // Pulse & filter follow directly, if they are there (both empty ones are
  // at the same address)
  for (c = PTBL; c <= FTBL; c++)
  {
    pos = sc->lo;
    while (findnext(sc, "B9 <a >a C9 FF C8 98 90 ?? [18] B9 <b >b <c", &pos))
    {
      unsigned left = sc->cap[0], right = sc->cap[1] + 1;
      unsigned st = sc->cap[2];
      pos++;
      if ((left == tblend) && (right >= left) && (right - left <= MAX_TABLELEN) &&
          (st == ((c == PTBL) ? 0x9d : 0x8d)) && (left != gp->tblleft[WTBL]))
      {
        gp->tblleft[c] = left;
        gp->tblright[c] = right;
        gp->tbllen[c] = right - left;
        tblend = right + gp->tbllen[c];
        break;
      }
    }
  }
  gp->haspulse = (gp->tbllen[PTBL] >= 0);
  gp->hasfilter = (gp->tbllen[FTBL] >= 0);
  for (c = 0; c < MAX_TABLES - 1; c++) if (gp->tbllen[c] < 0) gp->tbllen[c] = 0;

  // Speedtable: after the others, between two zero bytes if it is used.
  // The orderlists follow
  {
    unsigned song0 = mem[gp->songtbllo] | (mem[gp->songtblhi] << 8);
    if (song0 == tblend)
    {
      gp->tbllen[STBL] = 0;
      gp->tblleft[STBL] = gp->tblright[STBL] = tblend;
    }
    else
    {
      int len;
      if ((song0 < tblend + 2) || ((song0 - tblend) & 1)) return gtfail(err, errsize, "GoatTracker 2 speedtable not as expected");
      len = (song0 - tblend - 2) / 2;
      if ((len > MAX_TABLELEN) || (mem[tblend]) || (mem[tblend + 1 + len]))
        return gtfail(err, errsize, "GoatTracker 2 speedtable not as expected");
      gp->tbllen[STBL] = len;
      gp->tblleft[STBL] = tblend + 1;
      gp->tblright[STBL] = tblend + 2 + len;
    }
  }

  // Player variables used to tell the options apart
  if (!findfirst(sc, "A9 <t 9D ?? ?? A9 01 9D <c >c 9D <i >i 4C"))
    return gtfail(err, errsize, "GoatTracker 2 song init code not found");
  gp->defaulttempo = sc->cap[19] & 0xff;
  chncounter = sc->cap[2];
  chninstr = sc->cap[8];
  if (!findfirst(sc, "BD <w >w 3D <g >g (9D ?? ?? | 95 ?\?)"))
    return gtfail(err, errsize, "GoatTracker 2 waveform code not found");
  chnwave = sc->cap[22];
  chngate = sc->cap[6];

  // Same gateoff timer & first waveform for all instruments?
  pos = sc->lo;
  gp->fixedparams = 0;
  while (findnext(sc, "BD <c >c C9 <g F0", &pos))
  {
    if (sc->cap[2] == chncounter)
    {
      gp->fixedparams = 1;
      gp->fixedgatetimer = sc->cap[6] & 0xff;
      break;
    }
    pos++;
  }
  if (gp->fixedparams)
  {
    int found = 0;
    pos = sc->lo;
    while (findnext(sc, "A9 <f 9D <w >w [9D ?? ??] (A9 FF 9D <g >g | FE <g >g)", &pos))
    {
      if ((sc->cap[22] == chnwave) && (sc->cap[6] == chngate))
      {
        gp->fixedfirstwave = sc->cap[5] & 0xff;
        found = 1;
        break;
      }
      pos++;
    }
    if (!found) return gtfail(err, errsize, "GoatTracker 2 first waveform not found");
  }

  // Instrument columns: AD, SR, wave, [pulse], [filter], [vibrato param &
  // delay], [gateoff timer & first waveform]
  if ((gp->tblleft[WTBL] - gp->insad) % gp->numinstr) return gtfail(err, errsize, "GoatTracker 2 instruments not as expected");
  gp->columns = (gp->tblleft[WTBL] - gp->insad) / gp->numinstr;
  c = gp->columns - 3 - gp->haspulse - gp->hasfilter - (gp->fixedparams ? 0 : 2);
  if ((c != 0) && (c != 2)) return gtfail(err, errsize, "GoatTracker 2 instrument columns not as expected");
  gp->hasvib = (c == 2);

  // Wavetable delays & pulse format
  {
    char text[MAXPATTERNTEXT];
    unsigned w = gp->tblleft[WTBL] - 1;
    sprintf(text, "B9 %02X %02X C9 10 B0", w & 0xff, w >> 8);
    gp->wavedelay = findfirst(sc, text);
    if (gp->haspulse)
    {
      unsigned p = gp->tblleft[PTBL] - 1, r = gp->tblright[PTBL] - 1;
      sprintf(text, "B9 %02X %02X [10 ??] B9 %02X %02X", p & 0xff, p >> 8, r & 0xff, r >> 8);
      gp->simplepulse = findfirst(sc, text);
    }
  }

  // Instrument order: hardrestart, no hardrestart, legato
  gp->firstnohr = gp->firstlegato = MAX_INSTR;
  if (findfirst(sc, "A9 <s (9D ?? ?? | 95 ?\?) A9 <a (9D ?? ?? | 95 ?\?) A9 FE 9D <g >g") && (sc->cap[6] == chngate))
  {
    gp->hrcode = 1;
    gp->adparam = ((sc->cap[0] & 0xff) << 8) | (sc->cap[18] & 0xff);
  }
  pos = sc->lo;
  while (findnext(sc, "BD <i >i C9 <n B0 <r", &pos))
  {
    if (sc->cap[8] == chninstr)
    {
      unsigned target = pos + 7 + (signed char)(sc->cap[17] & 0xff);
      gp->firstnohr = sc->cap[13] & 0xff;
      if ((mem[target] == 0xc9) && (mem[target + 2] == 0x90) && (mem[target + 4] == 0xb0))
      {
        gp->firstlegato = mem[target + 1];
        // Multispeed: the packer counts one legato instrument too many
        if (gp->multiplier > 1) gp->firstlegato--;
      }
      break;
    }
    pos++;
  }
  if ((gp->firstnohr == MAX_INSTR) && (!gp->hrcode)) gp->firstnohr = 1;
  return 1;
}

// Unpacks a pattern (the inverse of packpattern() in greloc.c). Returns the
// number of bytes used, -1 on error
static int unpackpattern(const unsigned char *mem, unsigned adr, unsigned end, unsigned char *dest, int *rows)
{
  int pos = 0;
  int r = 0;
  int cmd = 0, data = 0;

  while (pos < 256)
  {
    unsigned char b;
    int instr = 0;

    if (adr + pos >= end) return -1;
    b = mem[adr + pos];
    if (!b)
    {
      pos++;
      break;
    }
    if (b < FX)
    {
      instr = b;
      pos++;
      if (adr + pos >= end) return -1;
      b = mem[adr + pos];
    }
    if ((b >= FX) && (b < FIRSTNOTE))
    {
      cmd = b & 0x0f;
      data = 0;
      pos++;
      if (cmd)
      {
        data = mem[adr + pos];
        pos++;
      }
      if (b >= FXONLY)
      {
        if (r >= MAX_PATTROWS) return -1;
        dest[r*4] = REST;
        dest[r*4+1] = instr;
        dest[r*4+2] = cmd;
        dest[r*4+3] = data;
        r++;
        continue;
      }
      if (adr + pos >= end) return -1;
      b = mem[adr + pos];
    }
    if ((b >= FIRSTNOTE) && (b <= KEYON))
    {
      if (r >= MAX_PATTROWS) return -1;
      dest[r*4] = b;
      dest[r*4+1] = instr;
      dest[r*4+2] = cmd;
      dest[r*4+3] = data;
      r++;
      pos++;
    }
    else if (b > KEYON)
    {
      // Packed rest
      int n = 0x100 - b;
      if ((instr) || (r + n > MAX_PATTROWS)) return -1;
      while (n--)
      {
        dest[r*4] = REST;
        dest[r*4+1] = 0;
        dest[r*4+2] = cmd;
        dest[r*4+3] = data;
        r++;
      }
      pos++;
    }
    else return -1;
  }
  if (!r) return -1;
  *rows = r;
  return pos;
}

static double packedtuning(SCAN *sc, unsigned songtbllo);

static int exactconvert(const SIDFILE *sf, SNGDATA *sng, SID2SNG_PLAYBACK *pb, char *report, int reportsize)
{
  unsigned char *mem;
  SCAN sc;
  GTPLAYER gp;
  unsigned adr, end;
  int s, c, d, t;
  int ok = 0;

  mem = (unsigned char *)calloc(0x10000, 1);
  if (!mem)
  {
    setreport(report, reportsize, "Out of memory");
    return 0;
  }
  memcpy(mem + sf->info.loadaddress, sf->data, sf->info.datasize);
  sc.mem = mem;
  sc.lo = sf->info.loadaddress;
  sc.hi = sf->info.loadaddress + sf->info.datasize;
  end = sc.hi;

  if (!findgtplayer(&sc, sf, &gp, report, reportsize)) goto done;
#ifdef SID2SNG_DEBUG
  fprintf(stderr, "base %04X mult %d songtbl %04X/%04X patttbl %04X/%04X insad %04X songs %d patt %d instr %d cols %d\n"
    "pulse %d filter %d vib %d fixed %d (gt %02X fw %02X) wavedelay %d simplepulse %d deftempo %d nohr %d legato %d hr %d (%04X)\n",
    gp.base, gp.multiplier, gp.songtbllo, gp.songtblhi, gp.patttbllo, gp.patttblhi, gp.insad, gp.numsongs, gp.numpatt,
    gp.numinstr, gp.columns, gp.haspulse, gp.hasfilter, gp.hasvib, gp.fixedparams, gp.fixedgatetimer, gp.fixedfirstwave,
    gp.wavedelay, gp.simplepulse, gp.defaulttempo, gp.firstnohr, gp.firstlegato, gp.hrcode, gp.adparam);
  for (t = 0; t < MAX_TABLES; t++) fprintf(stderr, "table %d: %04X/%04X len %d\n", t, gp.tblleft[t], gp.tblright[t], gp.tbllen[t]);
#endif

  sngdata_clear(sng);
  sngtexts(sng, &sf->info);

  // Orderlists, one after another
  adr = gp.tblright[STBL] + gp.tbllen[STBL];
  sng->numsongs = gp.numsongs;
  for (s = 0; s < gp.numsongs; s++)
  {
    for (c = 0; c < MAX_CHN; c++)
    {
      int n = s * 3 + c;
      unsigned start = mem[gp.songtbllo + n] | (mem[gp.songtblhi + n] << 8);
      unsigned char packed[MAX_SONGLEN + 2];
      int len = 0;

      if (start != adr)
      {
        setreport(report, reportsize, "GoatTracker 2 orderlists not as expected");
        goto done;
      }
      while ((adr < end) && (mem[adr] != LOOPSONG))
      {
        if (len >= MAX_SONGLEN)
        {
          setreport(report, reportsize, "GoatTracker 2 orderlist too long");
          goto done;
        }
        if ((mem[adr] < REPEAT) && (mem[adr] >= gp.numpatt))
        {
          setreport(report, reportsize, "GoatTracker 2 orderlist uses a pattern that isn't there");
          goto done;
        }
        packed[len++] = mem[adr++];
      }
      if (adr + 1 >= end)
      {
        setreport(report, reportsize, "GoatTracker 2 orderlist not as expected");
        goto done;
      }
      adr++;
      // The packer puts repeats after the pattern; turn back
      for (d = 0, t = 0; d < len; d++)
      {
        if ((packed[d] < REPEAT) && (d + 1 < len) && (packed[d+1] >= REPEAT) && (packed[d+1] < TRANSDOWN))
        {
          sng->songorder[s][c][t++] = packed[d+1];
          sng->songorder[s][c][t++] = packed[d];
          d++;
        }
        else sng->songorder[s][c][t++] = packed[d];
      }
      if (!len)
      {
        setreport(report, reportsize, "GoatTracker 2 orderlist is empty");
        goto done;
      }
      sng->songlen[s][c] = len;
      sng->songorder[s][c][len] = LOOPSONG;
      sng->songorder[s][c][len + 1] = mem[adr++];
    }
  }

  // Patterns, one after another
  sng->numpatterns = gp.numpatt;
  for (c = 0; c < gp.numpatt; c++)
  {
    unsigned start = mem[gp.patttbllo + c] | (mem[gp.patttblhi + c] << 8);
    int rows;
    int size;

    if (start != adr)
    {
      setreport(report, reportsize, "GoatTracker 2 patterns not as expected");
      goto done;
    }
    size = unpackpattern(mem, adr, end, sng->pattern[c], &rows);
    if (size < 0)
    {
      setreport(report, reportsize, "GoatTracker 2 pattern %02X could not be unpacked", c);
      goto done;
    }
    // Tempo commands were decreased by one for the player
    for (d = 0; d < rows; d++)
    {
      if ((sng->pattern[c][d*4+2] == CMD_SETTEMPO) && ((sng->pattern[c][d*4+3] & 0x7f) >= 2))
        sng->pattern[c][d*4+3]++;
    }
    sng->pattlen[c] = rows;
    adr += size;
  }
  if (adr != end)
  {
    setreport(report, reportsize, "GoatTracker 2 data doesn't end after the patterns");
    goto done;
  }

  // Instruments
  sng->numinstr = gp.numinstr;
  for (c = 1; c <= gp.numinstr; c++)
  {
    INSTR *in = &sng->instr[c];
    int col = 3;
    unsigned a = gp.insad + c - 1;
    int n = gp.numinstr;

    in->ad = mem[a];
    in->sr = mem[a + n];
    in->ptr[WTBL] = mem[a + 2*n];
    if (gp.haspulse) in->ptr[PTBL] = mem[a + (col++)*n];
    if (gp.hasfilter) in->ptr[FTBL] = mem[a + (col++)*n];
    if (gp.hasvib)
    {
      unsigned char param = mem[a + (col++)*n];
      unsigned char delay = mem[a + (col++)*n];
      // Stored as delay-1 when the delay isn't 0 (both 0 otherwise)
      if ((param) || (delay))
      {
        in->ptr[STBL] = param;
        in->vibdelay = delay + 1;
      }
    }
    if (gp.fixedparams)
    {
      in->gatetimer = gp.fixedgatetimer & 0x3f;
      in->firstwave = gp.fixedfirstwave;
    }
    else
    {
      in->gatetimer = mem[a + (col++)*n] & 0x3f;
      in->firstwave = mem[a + (col++)*n];
    }
    if (c >= gp.firstlegato) in->gatetimer |= 0x40;
    else if (c >= gp.firstnohr) in->gatetimer |= 0x80;
  }

  // Speedtable rows that nothing uses were pointed to by instruments with
  // vibrato delay 0 (the packer keeps the row but drops the pointer). Give
  // them back to such instruments: no sound changes, and packing again
  // gives the same SID
  {
    unsigned char used[MAX_TABLELEN + 1];
    int i = 1;
    memset(used, 0, sizeof used);
    for (c = 1; c <= gp.numinstr; c++) used[sng->instr[c].ptr[STBL]] = 1;
    for (c = 0; c < gp.numpatt; c++)
    {
      for (d = 0; d < sng->pattlen[c]; d++)
      {
        int cmd = sng->pattern[c][d*4+2];
        if (((cmd >= CMD_PORTAUP) && (cmd <= CMD_VIBRATO)) || (cmd == CMD_FUNKTEMPO))
          used[sng->pattern[c][d*4+3]] = 1;
      }
    }
    for (d = 0; d < gp.tbllen[WTBL]; d++)
    {
      int cmd = mem[gp.tblleft[WTBL] + d] - WAVECMD;
      if ((cmd >= CMD_PORTAUP) && (cmd <= CMD_VIBRATO)) used[mem[gp.tblright[WTBL] + d]] = 1;
    }
    for (d = 1; d <= gp.tbllen[STBL]; d++)
    {
      if (used[d]) continue;
      while ((i <= gp.numinstr) && ((sng->instr[i].ptr[STBL]) || (sng->instr[i].vibdelay))) i++;
      if (i > gp.numinstr) break;
      sng->instr[i++].ptr[STBL] = d;
    }
  }

  // A default tempo other than 6 (per speed multiple) comes from instrument
  // $3F's attack/decay
  {
    int mult = (gp.multiplier >= 0) ? gp.multiplier : 1;
    int expected = mult ? mult * 6 - 1 : 5;
    if ((gp.defaulttempo != expected) && (gp.defaulttempo >= 1) && (gp.numinstr < MAX_INSTR - 1))
    {
      INSTR *in = &sng->instr[MAX_INSTR - 1];
      in->ad = gp.defaulttempo + 1;
      sngdata_settext(in->name, "Tempo");
      sng->numinstr = MAX_INSTR - 1;
    }
  }

  // Tables, converted back from the player's format
  for (t = 0; t < MAX_TABLES; t++)
  {
    sng->tablelen[t] = gp.tbllen[t];
    for (d = 0; d < gp.tbllen[t]; d++)
    {
      unsigned char l = mem[gp.tblleft[t] + d];
      unsigned char r = mem[gp.tblright[t] + d];

      switch (t)
      {
        case WTBL:
        if (l < WAVECMD)
        {
          if (gp.wavedelay)
          {
            if ((l >= 0x10) && (l < 0x20)) l = WAVESILENT | (l & 0x0f);
            else if (l >= 0x20) l -= 0x10;
          }
          else
          {
            if ((l >= 0x01) && (l < 0x10)) l = WAVESILENT | l;
          }
          r ^= 0x80;
        }
        break;

        case PTBL:
        if ((l != 0xff) && (gp.simplepulse))
        {
          if (l >= 0x80)
          {
            l = 0x80 | (r & 0x0f);
            r &= 0xf0;
          }
          else
          {
            unsigned char speed = ((r & 0x0f) << 4) | (r >> 4);
            if (speed & 0x80) speed++;
            r = (speed & 0x0f) << 4;
          }
        }
        break;

        case FTBL:
        if ((l != 0xff) && (l > 0x80)) l = 0x80 | ((l & 0x38) << 1);
        break;
      }
      sng->ltable[t][d] = l;
      sng->rtable[t][d] = r;
    }
  }

  setreport(report, reportsize, "Exact: GoatTracker 2 song, %d subtune%s, %d patterns, %d instruments",
    gp.numsongs, (gp.numsongs > 1) ? "s" : "", gp.numpatt, gp.numinstr);
  if (gp.multiplier > 1) addreport(report, reportsize, ", %dx speed", gp.multiplier);
  if (gp.multiplier == 0) addreport(report, reportsize, ", 25Hz (speed multiplier 0)");
  if ((gp.hrcode) && (gp.adparam != 0x0f00)) addreport(report, reportsize, ", hardrestart ADSR %04X", gp.adparam);
  if (sf->info.clock == 2) addreport(report, reportsize, ", NTSC");
  // How it plays: no speed code in the player is 1x
  pb->multiplier = (gp.multiplier < 0) ? 1 : ((gp.multiplier > 16) ? -1 : gp.multiplier);
  pb->adparam = gp.hrcode ? (int)gp.adparam : -1;
  pb->basepitch = packedtuning(&sc, gp.songtbllo);
  if (pb->basepitch > 0.0) addreport(report, reportsize, ", tuned to A-4 = %.2f Hz (base pitch)", pb->basepitch);
  ok = 1;

done:
  free(mem);
  return ok;
}

//
// APPROX mode: record the SID registers, turn them into notes & instruments
//

#define PAL_CLOCK 985248.0
#define NTSC_CLOCK 1022727.0
#define PAL_FRAMECYCLES 19656
#define NTSC_FRAMECYCLES 17095
#define MAXTICKS 250000
#define INIT_MAXINSTR 4000000
#define PLAY_MAXINSTR 500000
#define SENTINEL 0x0000           // Return address for interrupt handlers
#define PROGMAX 32                // Frames of a note used for its instrument
#define LOOPMAXPERIOD 8           // Longest wavetable loop (arpeggio) found
#define RUNMIN 4                  // Frames a new pitch must last to be a new note
#define STICKY 0.35               // Extra semitones before the pitch changes note
#define MAXPULSEROWS 12
#define PULSECHAIN 1024          // Frames a pulse program may follow (through the notes after)
#define NUMLEVELS 11
#define STABLEPROG 12             // Longer programs: pitch drift is left out
#define STOPPEDSECS 2              // No change for this long at the end: the tune has stopped
#define LOOPSECS 600               // Recorded at least this long to find the loop
#define TEMPOLOSS 0.01             // Notes (of all) a longer row may lose more than the finest
#define TEMPOOFF 0.05              // ... or have off its rows more than the finest
#define TWOPI 6.283185307179586
#define TUNEMIN 0.1                // Semitones off GoatTracker's table: the song gets a base pitch
#define TUNECONSISTENT 0.5         // How alike the notes' tuning must be for that (0-1)

typedef struct
{
  unsigned short freq[MAX_CHN];
  unsigned short pulse[MAX_CHN];
  unsigned char ctrl[MAX_CHN];
  unsigned char ad[MAX_CHN];
  unsigned char sr[MAX_CHN];
  unsigned char dip;              // Bit n: voice n gate was off during the call
  unsigned char cutoff;           // $D416
  unsigned char resfilt;          // $D417
  unsigned char modevol;          // $D418
} SFRAME;

typedef struct
{
  const SFRAME *fr;
  int numticks;
  int *pn[MAX_CHN];               // Pitch (sticky note) per tick, -1 = none
} RECORDING;

typedef struct
{
  const RECORDING *rec;           // The subtune's recording
  int sub;                        // The subtune (index of the ones converted)
  int voice;
  int start, end;                 // Ticks: [start, end)
  int gateoff;                    // Tick when the gate went off (<= end)
  int legato;                     // Started by a pitch change, no new attack
  int tied;                       // The next note follows without a keyoff
  int shortgate;                  // The gate goes off before the next row: the
                                  // program turns it off and plays the release
  int base;                       // Note 0-NUMNOTES-1
  int gatetimer;                  // Instrument gatetimer (with HR/legato bits)
  int gtflags;                    // How the note starts: 0 hardrestart, $80 no
                                  // hardrestart, $40 legato, -1 any (after a rest)
  // When the gate goes off: the player reads the next row (a keyoff or the
  // next note) the gatetimer of ticks before it. In the player's time (its
  // gate goes on at the start of the note's row):
  int offtick;                    // Tick to turn the gate off at, -1 = no matter
  int offfirst, offlast;          // Rows it may read for that (offlast: the
                                  // next note's, no keyoff needed)
  int offwant;                    // The gatetimer for it, recorded ticks
  int gtwant;                     // The same in the song's ticks, 0 = any
  int gtmax;                      // Longest gatetimer the song's tempo allows
  int firstwave;
  int pstart, plen;               // Frames for the instrument program
  int gplen;                      // The same up to the gate going off (a note ended
                                  // by a keyoff: without the release)
  int pulsecont;                  // The pulse goes on from the previous note
  int pchain;                     // Frames of pulse its pulse program follows: its
                                  // own, or on through the notes after it that
                                  // go on with its sweep (no program of theirs)
  int glide;                      // Legato: glide speed (frequency per frame)
  int vibdelay, vibleft, vibright; // Vibrato as GoatTracker's instrument vibrato
                                  // (delay, speedtable row), 0 = none
  int hradsr;                     // Hardrestart: the ADSR in the gap before it
                                  // (AD << 8 | SR), -1 = none
  int instr;
  int ovwave, ovpulse;            // A wave/pulse program of its own (APROGS index),
                                  // set by a command on its row; -1 = the instrument's
  int row;
} ANOTE;

typedef struct
{
  unsigned char ad, sr, firstwave, gatetimer;
  int wlen, wloop;                // Wave program rows, loop row (-1 = stop)
  unsigned char wl[PROGMAX], wr[PROGMAX];
  int plen;                       // Pulse program rows (0 = no pulse)
  unsigned char pl[MAXPULSEROWS + 1], pr[MAXPULSEROWS + 1];
  int gateoff;                    // The wave program turns the gate off
  int vibdelay, vibleft, vibright; // Instrument vibrato (0 = none)
  int firstuse;
  int number;
} AINSTR;

typedef struct
{
  int wcap;                       // Max wave program frames
  int pulsetol;                   // Allowed pulse difference
  int maxruns;                    // Pulse modulation steps
  int p0quant;                    // Start pulse rounding
  int adsrflex;                   // 1 = SR may differ, 2 = AD or SR (not both:
                                  // a pattern command sets the one that differs)
  int gttol;                      // Ticks the gate may go off early/late
  int vib;                        // 1 = the vibrato as found
} LEVEL;

// Less detail step by step until the instruments fit: the pulse first, then
// when the gate goes off, then the length of the wave program, the ADSR last
// (it's most of the sound)
static const LEVEL levels[NUMLEVELS] =
{
  {PROGMAX, 0x40, 4, 0x10, 0, 0, 1},
  {PROGMAX, 0x100, 2, 0x40, 0, 0, 1},
  {PROGMAX, 0x200, 1, 0x80, 0, 1, 1},
  {16, 0x200, 1, 0x100, 0, 1, 1},
  {16, 0x1000, 0, 0x100, 0, 1, 1},
  {16, 0x1000, 0, 0x100, 0, 0x3f, 1},
  {8, 0x1000, 0, 0x100, 1, 0x3f, 1},
  {4, 0x1000, 0, 0x100, 1, 0x3f, 1},
  {4, 0x1000, 0, 0x100, 2, 0x3f, 1},
  {2, 0x1000, 0, 0x100, 2, 0x3f, 1},
  {1, 0x1000, 0, 0x100, 2, 0x3f, 0}
};

//
// The C64 the tunes are recorded on
//
// The CPU with 64K of RAM, and of the rest of the C64 what players use. The
// SID's registers are written, and voice 3's oscillator and envelope run (a
// port of reSID 0.16's WaveformGenerator & EnvelopeGenerator by Dag Lem), so
// that OSC3 ($D41B) and ENV3 ($D41C) read as on a C64: players take random
// numbers (noise) or modulation from them. A PSID is played by calling its
// play routine, as SIDDump does (raster waits let through). An RSID runs as
// on a C64: its init routine (which may not return: a main loop) with the
// CIA timers and the VIC-II raster interrupt calling its interrupt routines,
// the KERNAL's interrupt entry and exit emulated (there is no ROM)
//

#define ACC_READ 1
#define ACC_WRITE 2
#define FLAG_I 0x04
#define PAL_LINES 312
#define NTSC_LINES 263
#define PAL_LINECYCLES 63
#define NTSC_LINECYCLES 65
#define MAXDEPTH 8

// Which memory address the next instruction reads and/or writes, for the
// addressing modes that can reach the I/O area: absolute, indexed and
// indirect (zeropage & stack can't). Returns ACC_READ | ACC_WRITE (both: read-
// modify-write), 0 if none
static int memaccess(const CPU6502 *cpu, unsigned *ea)
{
  static const int alumodes[8] = {4, 0, 0, 1, 5, 0, 3, 2};
  const unsigned char *m = cpu->mem;
  unsigned pc = cpu->pc, op = m[pc];
  unsigned lo = m[(pc + 1) & 0xffff], hi = m[(pc + 2) & 0xffff];
  int cc = op & 3, bbb = (op >> 2) & 7, aaa = op >> 5;
  int mode = 0, acc;

  if (cc == 0)
  {
    // BIT, JMP, STY, LDY, CPY, CPX, NOP abs/abs,X; SHY
    if ((op == 0x4c) || (op == 0x6c)) return 0;
    if (bbb == 3) mode = 1;
    else if (bbb == 7) mode = 2;
    acc = (aaa == 4) ? ACC_WRITE : ACC_READ;
  }
  else if (cc == 2)
  {
    // Shifts, INC, DEC: read-modify-write. STX, LDX (abs,Y); SHX
    if (bbb == 3) mode = 1;
    else if (bbb == 7) mode = ((aaa == 4) || (aaa == 5)) ? 3 : 2;
    acc = (aaa == 4) ? ACC_WRITE : ((aaa == 5) ? ACC_READ : ACC_READ | ACC_WRITE);
  }
  else
  {
    // ORA AND EOR ADC STA LDA CMP SBC, and the illegal combinations of a
    // shift with them (read-modify-write), SAX/SHA/TAS, LAX/LAS
    mode = alumodes[bbb];
    if ((cc == 3) && (bbb == 7) && ((aaa == 4) || (aaa == 5))) mode = 3;
    if (aaa == 4) acc = ACC_WRITE;
    else if ((cc == 1) || (aaa == 5)) acc = ACC_READ;
    else acc = ACC_READ | ACC_WRITE;
  }
  switch (mode)
  {
    case 1: *ea = lo | (hi << 8); break;
    case 2: *ea = ((lo | (hi << 8)) + cpu->x) & 0xffff; break;
    case 3: *ea = ((lo | (hi << 8)) + cpu->y) & 0xffff; break;
    case 4: *ea = m[(lo + cpu->x) & 0xff] | (m[(lo + cpu->x + 1) & 0xff] << 8); break;
    case 5: *ea = ((m[lo] | (m[(lo + 1) & 0xff] << 8)) + cpu->y) & 0xffff; break;
    default: return 0;
  }
  return acc;
}

// Whether an instruction writes to memory (other than the stack)
static int opwrites(unsigned op)
{
  int cc = op & 3, bbb = (op >> 2) & 7, aaa = op >> 5;
  if (cc == 0) return (aaa == 4) && ((bbb == 1) || (bbb == 3) || (bbb == 5) || (bbb == 7));
  if (cc == 2) return (aaa != 5) && (bbb & 1);
  if (bbb == 2) return 0;
  return (aaa == 4) || ((cc == 3) && (aaa != 5));
}

// SID voice 3: oscillator and envelope (reSID 0.16)
typedef struct
{
  unsigned acc, shift, freq, pw;
  int wave, test;
  int gate, attack, decay, sustain, release;
  int state;                      // 0 attack, 1 decay/sustain, 2 release
  int ratecounter, rateperiod, expcounter, expperiod, envelope, holdzero;
  long long time;                 // C64 cycle it runs up to
} SIDV3;

static const int rateperiods[16] =
{
  9, 32, 63, 95, 149, 220, 267, 313, 392, 977, 1954, 3126, 3907, 11720, 19532, 31251
};

static void v3reset(SIDV3 *v)
{
  memset(v, 0, sizeof(SIDV3));
  v->shift = 0x7ffff8;
  v->state = 2;
  v->rateperiod = rateperiods[0];
  v->expperiod = 1;
  v->holdzero = 1;
}

static void v3clock(SIDV3 *v, long long now)
{
  long long dt = now - v->time;
  int step;

  if (dt <= 0) return;
  v->time = now;

  // The accumulator; the noise shift register is clocked each time bit 19
  // of it goes high
  if (!v->test)
  {
    unsigned long long a0 = v->acc, d = (unsigned long long)dt * v->freq;
    long long edges = (long long)((a0 + d + 0x80000) >> 20) - (long long)((a0 + 0x80000) >> 20);
    v->acc = (unsigned)((a0 + d) & 0xffffff);
    while (edges-- > 0)
    {
      unsigned bit0 = ((v->shift >> 22) ^ (v->shift >> 17)) & 1;
      v->shift = ((v->shift << 1) & 0x7fffff) | bit0;
    }
  }

  // The envelope: a step each rate period (and exponential counter period
  // when it goes down)
  step = v->rateperiod - v->ratecounter;
  if (step <= 0) step += 0x7fff;
  while (dt)
  {
    if (dt < step)
    {
      v->ratecounter += (int)dt;
      if (v->ratecounter & 0x8000) v->ratecounter = (v->ratecounter + 1) & 0x7fff;
      return;
    }
    // Nothing changes any more (at zero, or at the sustain level): only the
    // counters go on
    if ((v->holdzero) || ((v->state == 1) && (v->envelope == v->sustain * 0x11)))
    {
      long long n;
      dt -= step;
      n = 1 + dt / v->rateperiod;
      v->ratecounter = (int)(dt % v->rateperiod);
      v->expcounter = v->holdzero ? 0 : (int)((v->expcounter + n) % v->expperiod);
      return;
    }
    v->ratecounter = 0;
    dt -= step;
    if ((v->state == 0) || (++v->expcounter == v->expperiod))
    {
      v->expcounter = 0;
      switch (v->state)
      {
        case 0:
        v->envelope = (v->envelope + 1) & 0xff;
        if (v->envelope == 0xff)
        {
          v->state = 1;
          v->rateperiod = rateperiods[v->decay];
        }
        break;

        case 1:
        if (v->envelope != v->sustain * 0x11) v->envelope--;
        break;

        default:
        v->envelope = (v->envelope - 1) & 0xff;
        break;
      }
      switch (v->envelope)
      {
        case 0xff: v->expperiod = 1; break;
        case 0x5d: v->expperiod = 2; break;
        case 0x36: v->expperiod = 4; break;
        case 0x1a: v->expperiod = 8; break;
        case 0x0e: v->expperiod = 16; break;
        case 0x06: v->expperiod = 30; break;
        case 0x00: v->expperiod = 1; v->holdzero = 1; break;
      }
    }
    step = v->rateperiod;
  }
}

// A register write, reg $0E-$14 (the voice's registers)
static void v3write(SIDV3 *v, int reg, unsigned char d)
{
  switch (reg)
  {
    case 0x0e: v->freq = (v->freq & 0xff00) | d; break;
    case 0x0f: v->freq = (v->freq & 0xff) | (d << 8); break;
    case 0x10: v->pw = (v->pw & 0xf00) | d; break;
    case 0x11: v->pw = (v->pw & 0xff) | ((d & 0x0f) << 8); break;

    case 0x12:
    {
      int test = d & 0x08, gate = d & 0x01;
      v->wave = d >> 4;
      if (test)
      {
        v->acc = 0;
        v->shift = 0;
      }
      else if (v->test) v->shift = 0x7ffff8;
      v->test = test;
      if ((!v->gate) && (gate))
      {
        v->state = 0;
        v->rateperiod = rateperiods[v->attack];
        v->holdzero = 0;
      }
      else if ((v->gate) && (!gate))
      {
        v->state = 2;
        v->rateperiod = rateperiods[v->release];
      }
      v->gate = gate;
    }
    break;

    case 0x13:
    v->attack = d >> 4;
    v->decay = d & 0x0f;
    if (v->state == 0) v->rateperiod = rateperiods[v->attack];
    else if (v->state == 1) v->rateperiod = rateperiods[v->decay];
    break;

    case 0x14:
    v->sustain = d >> 4;
    v->release = d & 0x0f;
    if (v->state == 2) v->rateperiod = rateperiods[v->release];
    break;
  }
}

// OSC3: the top 8 bits of the waveform output (the combined waveforms as
// the AND of theirs, roughly what the chips give)
static unsigned char v3osc(const SIDV3 *v)
{
  unsigned a = v->acc, s = v->shift, out = 0xfff;
  if (!v->wave) return 0;
  if (v->wave & 8)
  {
    if (v->wave != 8) return 0;
    out = ((s & 0x400000) >> 11) | ((s & 0x100000) >> 10) | ((s & 0x010000) >> 7) | ((s & 0x002000) >> 5) |
      ((s & 0x000800) >> 4) | ((s & 0x000080) >> 1) | ((s & 0x000010) << 1) | ((s & 0x000004) << 2);
  }
  if (v->wave & 1) out &= (((a & 0x800000) ? ~a : a) >> 11) & 0xfff;
  if (v->wave & 2) out &= a >> 12;
  if ((v->wave & 4) && (!v->test) && ((a >> 12) < v->pw)) out = 0;
  return (unsigned char)(out >> 4);
}

// CIA 6526: the two timers and their interrupts
typedef struct
{
  unsigned latch[2], counter[2];
  unsigned char cr[2];
  unsigned char icr, mask;
} CIA;

// Counts a timer down by d cycles (or d timer A underflows). Returns how
// many times it underflowed
static int ciatimer(CIA *x, int t, long long d)
{
  int n = 0;
  while (d > 0)
  {
    if ((long long)x->counter[t] >= d)
    {
      x->counter[t] -= (unsigned)d;
      break;
    }
    d -= x->counter[t] + 1;
    x->counter[t] = x->latch[t];
    x->icr |= 1 << t;
    n++;
    if (x->cr[t] & 0x08)
    {
      // One-shot
      x->cr[t] &= ~0x01;
      break;
    }
  }
  return n;
}

static void ciaclock(CIA *x, long long d)
{
  int under = 0;
  if ((x->cr[0] & 0x21) == 0x01) under = ciatimer(x, 0, d);
  if (x->cr[1] & 0x01)
  {
    int mode = (x->cr[1] >> 5) & 3;
    if (mode == 0) ciatimer(x, 1, d);
    else if ((mode == 2) && (under)) ciatimer(x, 1, under);
  }
}

// Cycles to its next underflow (counting cycles), -1 = none
static long long cianext(const CIA *x)
{
  long long t = -1;
  if ((x->cr[0] & 0x21) == 0x01) t = x->counter[0] + 1;
  if (((x->cr[1] & 0x61) == 0x01) && ((t < 0) || (x->counter[1] + 1 < t))) t = x->counter[1] + 1;
  return t;
}

static unsigned char ciaread(CIA *x, int reg, unsigned char old)
{
  unsigned char d;
  switch (reg)
  {
    case 0x04: return x->counter[0] & 0xff;
    case 0x05: return x->counter[0] >> 8;
    case 0x06: return x->counter[1] & 0xff;
    case 0x07: return x->counter[1] >> 8;
    case 0x0d:
    d = x->icr | ((x->icr & x->mask & 0x1f) ? 0x80 : 0);
    x->icr = 0;
    return d;
    case 0x0e: return x->cr[0];
    case 0x0f: return x->cr[1];
  }
  return old;
}

static void ciawrite(CIA *x, int reg, unsigned char d)
{
  switch (reg)
  {
    case 0x04:
    case 0x06:
    x->latch[(reg - 4) >> 1] = (x->latch[(reg - 4) >> 1] & 0xff00) | d;
    break;

    case 0x05:
    case 0x07:
    {
      int t = (reg - 5) >> 1;
      x->latch[t] = (x->latch[t] & 0xff) | (d << 8);
      if (!(x->cr[t] & 0x01)) x->counter[t] = x->latch[t];
    }
    break;

    case 0x0d:
    if (d & 0x80) x->mask |= d & 0x1f;
    else x->mask &= ~(d & 0x1f);
    break;

    case 0x0e:
    case 0x0f:
    x->cr[reg - 0x0e] = d & ~0x10;
    if (d & 0x10) x->counter[reg - 0x0e] = x->latch[reg - 0x0e];
    break;
  }
}

typedef struct
{
  CPU6502 cpu;                    // (with the 64K of RAM)
  int full;                       // RSID: the whole C64 runs, else play calls
  int framecycles, linecycles, lines;
  long long now;                  // Full: the C64 cycle. Play calls: the cycle
                                  // the routine started at
  SIDV3 v3;
  unsigned char sid[0x20];        // The SID registers written (full)
  int mainwrote;                  // Voice/filter registers written by the main
                                  // program since the last frame (full)
  unsigned char dip;              // Gates gone off since the last tick (full)
  CIA cia[2];
  int rastercmp;                  // VIC-II raster interrupt line
  unsigned char vicirq, vicmask;
  int nmiline, nmipending;        // CIA 2 interrupt output, NMI on its edge
  int depth;                      // Interrupts running: their stack pointer,
  unsigned char framesp[MAXDEPTH];  // source (see interrupt()), and whether
  int framekey[MAXDEPTH];           // they wrote the SID's voice/filter
  unsigned char framewrote[MAXDEPTH]; // registers
} C64;

static long long c64now(const C64 *c)
{
  return c->full ? c->now : c->now + c->cpu.cycles;
}

static int isio(const C64 *c, unsigned ea)
{
  if (!c->full) return (ea >= 0xd400) && (ea < 0xd800);
  return (ea >= 0xd000) && (ea < 0xe000) && (c->cpu.mem[1] & 0x03) && (c->cpu.mem[1] & 0x04);
}

// I/O registers whose value changes by itself (or reading them does
// something): the SID's OSC3/ENV3, the VIC-II raster & interrupts, the CIA
// timers & interrupts
static int iovolatile(unsigned ea)
{
  int reg;
  if ((ea >= 0xd400) && (ea < 0xd800)) return ((ea & 0x1f) == 0x1b) || ((ea & 0x1f) == 0x1c);
  if (ea < 0xd400)
  {
    reg = ea & 0x3f;
    return (reg == 0x11) || (reg == 0x12) || (reg == 0x19);
  }
  if ((ea >= 0xdc00) && (ea < 0xde00))
  {
    reg = ea & 0x0f;
    return ((reg >= 0x04) && (reg <= 0x07)) || (reg == 0x0d);
  }
  return 0;
}

static int rasterline(const C64 *c)
{
  return (int)((c->now % c->framecycles) / c->linecycles);
}

// Makes the memory hold what a read of an I/O register gives
static void ioread(C64 *c, unsigned ea)
{
  unsigned char *m = c->cpu.mem;
  if ((ea >= 0xd400) && (ea < 0xd800))
  {
    int reg = ea & 0x1f;
    if ((reg == 0x1b) || (reg == 0x1c))
    {
      v3clock(&c->v3, c64now(c));
      m[ea] = (reg == 0x1b) ? v3osc(&c->v3) : (unsigned char)c->v3.envelope;
    }
    else if ((reg == 0x19) || (reg == 0x1a)) m[ea] = 0xff;
    return;
  }
  if (!c->full) return;
  if (ea < 0xd400)
  {
    int line = rasterline(c);
    switch (ea & 0x3f)
    {
      case 0x11: m[ea] = (m[ea] & 0x7f) | ((line & 0x100) >> 1); break;
      case 0x12: m[ea] = line & 0xff; break;
      case 0x19: m[ea] = c->vicirq | 0x70 | ((c->vicirq & c->vicmask) ? 0x80 : 0); break;
      case 0x1a: m[ea] = c->vicmask | 0xf0; break;
    }
  }
  else if ((ea >= 0xdc00) && (ea < 0xde00))
  {
    m[ea] = ciaread(&c->cia[(ea >> 8) & 1], ea & 0x0f, m[ea]);
    if (ea >= 0xdd00) c->nmiline = 0;
  }
}

static void iowrite(C64 *c, unsigned ea, unsigned char d)
{
  if ((ea >= 0xd400) && (ea < 0xd800))
  {
    int reg = ea & 0x1f;
    if ((reg >= 0x0e) && (reg <= 0x14))
    {
      v3clock(&c->v3, c64now(c));
      v3write(&c->v3, reg, d);
    }
    if (reg < 0x19)
    {
      c->sid[reg] = d;
      if (reg < 0x18)
      {
        if (!c->depth) c->mainwrote = 1;
        else c->framewrote[((c->depth > MAXDEPTH) ? MAXDEPTH : c->depth) - 1] = 1;
      }
      if (((reg == 0x04) || (reg == 0x0b) || (reg == 0x12)) && (!(d & 0x01))) c->dip |= 1 << (reg / 7);
    }
    return;
  }
  if (!c->full) return;
  if (ea < 0xd400)
  {
    switch (ea & 0x3f)
    {
      case 0x11: c->rastercmp = (c->rastercmp & 0xff) | ((d & 0x80) << 1); break;
      case 0x12: c->rastercmp = (c->rastercmp & 0x100) | d; break;
      case 0x19: c->vicirq &= ~(d & 0x0f); break;
      case 0x1a: c->vicmask = d & 0x0f; break;
    }
  }
  else if ((ea >= 0xdc00) && (ea < 0xde00)) ciawrite(&c->cia[(ea >> 8) & 1], ea & 0x0f, d);
}

// Executes an instruction, with the I/O it reads and writes. Returns as
// cpu6502_step()
static int iostep(C64 *c)
{
  unsigned ea = 0;
  unsigned char old = 0;
  int acc = memaccess(&c->cpu, &ea), r;

  if ((acc) && (!isio(c, ea))) acc = 0;
  if (acc & ACC_READ)
  {
    ioread(c, ea);
    old = c->cpu.mem[ea];
  }
  r = cpu6502_step(&c->cpu);
  if ((acc & ACC_WRITE) && (r > 0))
  {
    // (A read-modify-write writes the old value first)
    if (acc & ACC_READ) iowrite(c, ea, old);
    iowrite(c, ea, c->cpu.mem[ea]);
  }
  return r;
}

static void c64init(C64 *c, const SIDFILE *sf, int full)
{
  int ntsc = (sf->info.clock == 2);
  memset(c, 0, sizeof(C64));
  c->full = full;
  c->framecycles = ntsc ? NTSC_FRAMECYCLES : PAL_FRAMECYCLES;
  c->linecycles = ntsc ? NTSC_LINECYCLES : PAL_LINECYCLES;
  c->lines = ntsc ? NTSC_LINES : PAL_LINES;
  v3reset(&c->v3);
  if (full)
  {
    // As after RUN: the KERNAL's interrupt vectors, CIA 1 timer A
    // interrupting 60 times a second, CIA 2 and the raster interrupt off
    c->cpu.mem[0x314] = 0x31;
    c->cpu.mem[0x315] = 0xea;
    c->cpu.mem[0x318] = 0x47;
    c->cpu.mem[0x319] = 0xfe;
    c->cia[0].latch[0] = c->cia[0].counter[0] = ntsc ? 0x4295 : 0x4025;
    c->cia[0].cr[0] = 0x01;
    c->cia[0].mask = 0x01;
    c->cia[0].latch[1] = c->cia[0].counter[1] = 0xffff;
    c->cia[1].latch[0] = c->cia[1].counter[0] = 0xffff;
    c->cia[1].latch[1] = c->cia[1].counter[1] = 0xffff;
    c->rastercmp = 0x100;
  }
  memcpy(c->cpu.mem + sf->info.loadaddress, sf->data, sf->info.datasize);
  c->cpu.mem[0x01] = 0x37;
}

// Runs a routine (play calls). irqmode 0 = called with JSR, 1 = KERNAL IRQ
// ($0314) handler, 2 = hardware IRQ ($FFFE) handler. Returns 1 when done, -1
// on a CPU error, -2 if it didn't finish
static int runroutine(C64 *c, unsigned adr, int a, int irqmode, unsigned maxinstr, unsigned char *dip)
{
  CPU6502 *cpu = &c->cpu;
  unsigned char *mem = cpu->mem;
  unsigned n;
  unsigned char d = 0;

  cpu6502_init(cpu, adr, a, 0, 0);
  if (irqmode)
  {
    mem[0x100 + cpu->sp--] = SENTINEL >> 8;
    mem[0x100 + cpu->sp--] = SENTINEL & 0xff;
    mem[0x100 + cpu->sp--] = 0x20;
    if (irqmode == 1)
    {
      mem[0x100 + cpu->sp--] = 0;
      mem[0x100 + cpu->sp--] = 0;
      mem[0x100 + cpu->sp--] = 0;
    }
  }
  for (n = 0; n < maxinstr; n++)
  {
    int r = iostep(c);
    if (r < 0) return -1;
    if (!r) break;
    // Let raster waits finish
    ++mem[0xd012];
    if ((!mem[0xd012]) || ((mem[0xd011] & 0x80) && (mem[0xd012] >= 0x38)))
    {
      mem[0xd011] ^= 0x80;
      mem[0xd012] = 0x00;
    }
    if (dip)
    {
      if (!(mem[0xd404] & 1)) d |= 1;
      if (!(mem[0xd40b] & 1)) d |= 2;
      if (!(mem[0xd412] & 1)) d |= 4;
    }
    if ((irqmode) && (cpu->pc == SENTINEL)) break;
    if (((mem[0x01] & 0x07) != 0x05) && ((cpu->pc == 0xea31) || (cpu->pc == 0xea81))) break;
  }
  if (dip) *dip = d;
  return (n >= maxinstr) ? -2 : 1;
}

// The SID registers into a recorded frame
static void snapshot(SFRAME *f, const unsigned char *r, unsigned char dip)
{
  int v;
  for (v = 0; v < MAX_CHN; v++)
  {
    f->freq[v] = r[v*7] | (r[v*7+1] << 8);
    f->pulse[v] = (r[v*7+2] | (r[v*7+3] << 8)) & 0xfff;
    f->ctrl[v] = r[v*7+4];
    f->ad[v] = r[v*7+5];
    f->sr[v] = r[v*7+6];
  }
  f->dip = dip;
  f->cutoff = r[0x16];
  f->resfilt = r[0x17];
  f->modevol = r[0x18];
  // Voice 3 switched off ($D418 bit 7) and not through the filter can't be
  // heard: players use it for random numbers or as a modulator. GoatTracker
  // can't switch it off, so it is left out (unless voice 1's ring modulation
  // or sync, which voice 3 drives, is on)
  if ((f->modevol & 0x80) && (!(f->resfilt & 0x04)) && (!(f->ctrl[0] & 0x06)))
  {
    f->ctrl[2] = 0;
    f->dip &= ~4;
  }
}

static int growframes(SFRAME **frames, int *maxframes, int n)
{
  if (n < *maxframes) return 1;
  {
    int newmax = *maxframes ? *maxframes * 2 : 4096;
    SFRAME *nf;
    if (newmax > MAXTICKS) newmax = MAXTICKS;
    if (n >= newmax) return 1;
    nf = (SFRAME *)realloc(*frames, newmax * sizeof(SFRAME));
    if (!nf) return 0;
    *frames = nf;
    *maxframes = newmax;
  }
  return 1;
}

static void push(CPU6502 *cpu, unsigned char d)
{
  cpu->mem[0x100 + cpu->sp--] = d;
}

static unsigned char pull(CPU6502 *cpu)
{
  return cpu->mem[0x100 + ++cpu->sp];
}

// Lets time pass (full mode): the timers count, the raster moves
static void advance(C64 *c, long long d)
{
  long long prev = c->now, lstart = (long long)c->rastercmp * c->linecycles;
  int line;
  c->now += d;
  ciaclock(&c->cia[0], d);
  ciaclock(&c->cia[1], d);
  line = (c->cia[1].icr & c->cia[1].mask & 0x1f) ? 1 : 0;
  if ((line) && (!c->nmiline)) c->nmipending = 1;
  c->nmiline = line;
  if ((c->rastercmp < c->lines) &&
    ((c->now - lstart + c->framecycles) / c->framecycles > (prev - lstart + c->framecycles) / c->framecycles))
    c->vicirq |= 0x01;
}

// Cycles to the next thing that happens (full mode), at most limit
static long long nextevent(const C64 *c, long long limit)
{
  long long t = limit, n;
  int x;
  for (x = 0; x < 2; x++)
  {
    n = cianext(&c->cia[x]);
    if ((n >= 0) && (n < t)) t = n;
  }
  if (c->rastercmp < c->lines)
  {
    long long lstart = (long long)c->rastercmp * c->linecycles;
    long long pos = (c->now - lstart) % c->framecycles;
    if (pos < 0) pos += c->framecycles;
    n = c->framecycles - pos;
    if (n < t) t = n;
  }
  n = c->framecycles - c->now % c->framecycles;
  if (n < t) t = n;
  return (t < 1) ? 1 : t;
}

// An interrupt (full mode): through the KERNAL when its ROM is in. Its
// source: 2/3 CIA 1 timer A/B, $100 + line VIC-II raster, 4/5 CIA 2 timer A/B
// (NMI), 1 other. Returns the cycles it takes
static int interrupt(C64 *c, int nmi)
{
  CPU6502 *cpu = &c->cpu;
  const unsigned char *m = cpu->mem;
  int key = 1;
  if (nmi) key = (c->cia[1].icr & c->cia[1].mask & 0x01) ? 4 : 5;
  else if (c->vicirq & c->vicmask & 0x01) key = 0x100 | c->rastercmp;
  else if (c->cia[0].icr & c->cia[0].mask & 0x01) key = 2;
  else if (c->cia[0].icr & c->cia[0].mask & 0x02) key = 3;
  if (c->depth < MAXDEPTH)
  {
    c->framesp[c->depth] = cpu->sp;
    c->framekey[c->depth] = key;
    c->framewrote[c->depth] = 0;
  }
  c->depth++;
  push(cpu, cpu->pc >> 8);
  push(cpu, cpu->pc & 0xff);
  push(cpu, (cpu->flags | 0x20) & ~0x10);
  cpu->flags |= FLAG_I;
  if (m[1] & 0x02)
  {
    if (nmi) cpu->pc = m[0x318] | (m[0x319] << 8);
    else
    {
      push(cpu, cpu->a);
      push(cpu, cpu->x);
      push(cpu, cpu->y);
      cpu->pc = m[0x314] | (m[0x315] << 8);
      return 29;
    }
  }
  else cpu->pc = nmi ? m[0xfffa] | (m[0xfffb] << 8) : m[0xfffe] | (m[0xffff] << 8);
  return 7;
}

// Interrupt sources seen while an RSID runs
#define MAXKEYS 16

typedef struct
{
  int key;
  int calls, wrote;               // Interrupts, those that wrote the SID
} KEYSTAT;

// An RSID: the C64 runs until cycle end. The SID is recorded at the end of
// each interrupt from source musickey (-1: none, once a frame instead).
// stats (if not NULL) gets the sources & how often they wrote the SID,
// *mainframes the frames in which the main program did. Returns 1 on
// success, 0 if the tune crashed (err), -1 if out of memory
static int runfull(C64 *c, const SIDFILE *sf, int subtune, long long end, int musickey, SFRAME **framesout,
  int *numticks, double *rateout, KEYSTAT *stats, int *numstats, int *mainframes, char *err, int errsize)
{
  CPU6502 *cpu = &c->cpu;
  unsigned char *m = cpu->mem;
  SFRAME *ticks = NULL;
  int maxticks = 0, nt = 0, idle = 0, result = -1;
  long long firsttick = 0, lasttick = 0, nextframe = c->framecycles;
  unsigned char seen[16][7];      // The main program's last states (busy loops)
  int seenpos = 0, clean = 0;

  if (numstats) *numstats = 0;
  if (mainframes) *mainframes = 0;
  cpu6502_init(cpu, sf->info.initaddress, subtune - 1, 0, 0);
  while (c->now < end)
  {
    unsigned startcycles;
    int r;

    if (c->now >= nextframe)
    {
      nextframe += c->framecycles;
      if ((mainframes) && (c->mainwrote)) (*mainframes)++;
      c->mainwrote = 0;
      if (musickey == -1)
      {
        // Once a frame (a tune playing from its main program)
        if (!growframes(&ticks, &maxticks, nt)) goto done;
        if (nt < maxticks) snapshot(&ticks[nt++], c->sid, c->dip);
        c->dip = 0;
      }
    }
    // Interrupts
    if (c->nmipending)
    {
      c->nmipending = 0;
      advance(c, interrupt(c, 1));
      clean = 0;
    }
    else if ((!(cpu->flags & FLAG_I)) && ((c->cia[0].icr & c->cia[0].mask & 0x1f) || (c->vicirq & c->vicmask)))
    {
      advance(c, interrupt(c, 0));
      clean = 0;
    }
    if ((idle) && (!c->depth))
    {
      // Nothing to run until something interrupts
      long long d = nextevent(c, end - c->now);
      if (c->now + d > nextframe) d = nextframe - c->now;
      advance(c, (d < 1) ? 1 : d);
      continue;
    }

    // The KERNAL's interrupt exits (there's no ROM): they pull the registers
    // and return. Other calls to it return at once
    if ((m[1] & 0x02) && (cpu->pc >= 0xe000))
    {
      unsigned pc = cpu->pc;
      if (pc == 0xea31) ciaread(&c->cia[0], 0x0d, 0);
      if ((pc == 0xea31) || (pc == 0xea81) || (pc == 0xfebc) || (pc == 0xfe47))
      {
        if (pc != 0xfe47)
        {
          cpu->y = pull(cpu);
          cpu->x = pull(cpu);
          cpu->a = pull(cpu);
        }
        cpu->flags = pull(cpu);
        cpu->pc = pull(cpu);
        cpu->pc |= pull(cpu) << 8;
        r = 2;
      }
      else if (cpu->sp == 0xff) r = 0;
      else
      {
        cpu->pc = pull(cpu);
        cpu->pc |= pull(cpu) << 8;
        cpu->pc++;
        r = 1;
      }
      advance(c, 20);
      clean = 0;
    }
    else
    {
      unsigned pc = cpu->pc, ea;
      unsigned char op = m[pc];
      int acc = memaccess(cpu, &ea);
      if (!c->depth)
      {
        // The main program waiting in a loop for an interrupt: in the same
        // state as a few instructions ago, nothing written (but the stack)
        // or read from I/O that changes since. Nothing changes until
        // something interrupts
        unsigned char st[7];
        int k, busy = 0;
        st[0] = pc & 0xff;
        st[1] = pc >> 8;
        st[2] = cpu->a;
        st[3] = cpu->x;
        st[4] = cpu->y;
        st[5] = cpu->flags;
        st[6] = cpu->sp;
        for (k = 1; (k <= clean) && (k <= 16); k++)
        {
          if (!memcmp(seen[(seenpos - k) & 15], st, 7))
          {
            busy = 1;
            break;
          }
        }
        if (busy)
        {
          long long d = nextevent(c, end - c->now);
          if (c->now + d > nextframe) d = nextframe - c->now;
          advance(c, (d < 1) ? 1 : d);
          clean = 0;
          continue;
        }
        memcpy(seen[seenpos & 15], st, 7);
        seenpos++;
        if ((opwrites(op)) || ((acc & ACC_READ) && (isio(c, ea)) && (iovolatile(ea)))) clean = 0;
        else clean++;
      }
      startcycles = cpu->cycles;
      r = iostep(c);
      advance(c, cpu->cycles - startcycles);
      if (r < 0)
      {
        setreport(err, errsize, "The tune crashed (opcode $%02X at $%04X)", cpu->errorop, cpu->errorpc);
        result = 0;
        goto done;
      }
      if ((r > 0) && (op == 0x40)) r = 2;
    }
    if (!r)
    {
      // The init routine returned (or a BRK): the C64 waits for interrupts.
      // (With them enabled: as the player of a SID file does)
      if (!c->depth)
      {
        idle = 1;
        cpu->flags &= ~FLAG_I;
      }
      else
      {
        setreport(err, errsize, "The tune's interrupt routine doesn't return (BRK at $%04X)", cpu->pc);
        result = 0;
        goto done;
      }
    }
    // An interrupt routine returned
    while ((r == 2) && (c->depth) && ((c->depth > MAXDEPTH) || (cpu->sp == c->framesp[c->depth - 1])))
    {
      c->depth--;
      if (c->depth < MAXDEPTH)
      {
        int key = c->framekey[c->depth], k;
        if (stats)
        {
          for (k = 0; (k < *numstats) && (stats[k].key != key); k++);
          if ((k == *numstats) && (k < MAXKEYS))
          {
            stats[k].key = key;
            stats[k].calls = stats[k].wrote = 0;
            (*numstats)++;
          }
          if (k < *numstats)
          {
            stats[k].calls++;
            if (c->framewrote[c->depth]) stats[k].wrote++;
          }
        }
        if (key == musickey)
        {
          if (!growframes(&ticks, &maxticks, nt)) goto done;
          if (nt < maxticks)
          {
            if (!nt) firsttick = c->now;
            lasttick = c->now;
            snapshot(&ticks[nt++], c->sid, c->dip);
          }
          c->dip = 0;
        }
        // (What it wrote counts for the one it interrupted too)
        if ((c->depth) && (c->framewrote[c->depth])) c->framewrote[c->depth - 1] = 1;
      }
      r = 1;
    }
  }

  if (musickey < 0) *rateout = 1.0;
  else
  {
    double rate = ((nt >= 2) && (lasttick > firsttick)) ?
      (double)(nt - 1) * c->framecycles / (double)(lasttick - firsttick) : 1.0;
    if (fabs(rate - floor(rate + 0.5)) < 0.04 * rate) rate = floor(rate + 0.5);
    if (rate < 0.25) rate = 0.25;
    if (rate > 16.0) rate = 16.0;
    *rateout = rate;
  }
  *framesout = ticks;
  *numticks = nt;
  ticks = NULL;
  result = 1;

done:
  free(ticks);
  return result;
}

// An RSID: first a few seconds to see which interrupt plays the music (the
// one that writes the SID most often; if the main program writes it in more
// frames, it plays from there), then the recording, at the rate of that
// interrupt. Returns as record()
static int recordfull(C64 *c, const SIDFILE *sf, int subtune, int seconds, SFRAME **framesout, int *numticks,
  double *rateout, char *err, int errsize)
{
  KEYSTAT stats[MAXKEYS];
  SFRAME *probe = NULL;
  double clock = (sf->info.clock == 2) ? NTSC_CLOCK : PAL_CLOCK, rate;
  int numstats = 0, mainframes = 0, musickey = -1, best = 0, n, k, r;

  r = runfull(c, sf, subtune, (long long)(clock * ((seconds < 5) ? seconds : 5)), -2, &probe, &n, &rate, stats, &numstats,
    &mainframes, err, errsize);
  free(probe);
  if (r <= 0) return r;
  for (k = 0; k < numstats; k++)
  {
    if (stats[k].wrote > best)
    {
      best = stats[k].wrote;
      musickey = stats[k].key;
    }
  }
  if ((mainframes > best) || (!best)) musickey = -1;
#ifdef SID2SNG_DEBUG
  if (getenv("SID2SNG_RSID"))
  {
    for (k = 0; k < numstats; k++) fprintf(stderr, "source %x: %d calls, %d wrote\n", stats[k].key, stats[k].calls, stats[k].wrote);
    fprintf(stderr, "main program wrote in %d frames: music from %x\n", mainframes, musickey);
  }
#endif
  c64init(c, sf, 1);
  return runfull(c, sf, subtune, (long long)(clock * seconds), musickey, framesout, numticks, rateout, NULL, NULL, NULL,
    err, errsize);
}

// Records the SID registers after every play call (of an RSID: every
// interrupt that plays). Returns 1 on success, 0 if the subtune can't be
// recorded (the reason in err), -1 if out of memory
static int record(const SIDFILE *sf, int subtune, int seconds, SFRAME **framesout, int *numticks, double *rateout,
  char *err, int errsize)
{
  C64 *c;
  CPU6502 *cpu;
  SFRAME *frames = NULL;
  const SIDINFO *i = &sf->info;
  unsigned playadr = i->playaddress;
  int irqmode = 0;
  int ntsc = (i->clock == 2);
  int fps = ntsc ? 60 : 50;
  int framecycles = ntsc ? NTSC_FRAMECYCLES : PAL_FRAMECYCLES;
  double rate = 1.0, acc = 0.0;
  long long initend;
  int maxticks, ticks = 0, frame, r, speedbit;
  int result = 0;

  c = (C64 *)calloc(1, sizeof(C64));
  if (!c) return -1;
  cpu = &c->cpu;
  if (i->rsid)
  {
    c64init(c, sf, 1);
    result = recordfull(c, sf, subtune, seconds, framesout, numticks, rateout, err, errsize);
    free(c);
    return result;
  }
  c64init(c, sf, 0);

  r = runroutine(c, i->initaddress, subtune - 1, 0, INIT_MAXINSTR, NULL);
  if (r == -1)
  {
    setreport(err, errsize, "The tune's init routine crashed (opcode $%02X at $%04X)", cpu->errorop, cpu->errorpc);
    goto fail;
  }
  initend = cpu->cycles;

  if (!playadr)
  {
    if ((cpu->mem[0x01] & 0x07) == 0x05)
    {
      playadr = cpu->mem[0xfffe] | (cpu->mem[0xffff] << 8);
      irqmode = 2;
    }
    else
    {
      playadr = cpu->mem[0x314] | (cpu->mem[0x315] << 8);
      irqmode = 1;
    }
    if ((!playadr) || ((irqmode == 1) && (playadr >= 0xe000)))
    {
      setreport(err, errsize, "The tune has no play routine to call (no play address, no interrupt set up)");
      goto fail;
    }
  }

  // CIA timer speed: calls per frame
  speedbit = (subtune <= 32) ? ((i->speed >> (subtune - 1)) & 1) : ((i->speed >> 31) & 1);
  if (speedbit)
  {
    unsigned timer = cpu->mem[0xdc04] | (cpu->mem[0xdc05] << 8);
    if (!timer) timer = 0x4025;
    rate = (double)framecycles / (double)(timer + 1);
    if (fabs(rate - floor(rate + 0.5)) < 0.04 * rate) rate = floor(rate + 0.5);
    if (rate < 0.25) rate = 0.25;
    if (rate > 16.0) rate = 16.0;
  }
  *rateout = rate;

  maxticks = (int)(seconds * fps * rate) + 1;
  if (maxticks > MAXTICKS) maxticks = MAXTICKS;
  frames = (SFRAME *)calloc(maxticks, sizeof(SFRAME));
  if (!frames)
  {
    result = -1;
    goto fail;
  }

  for (frame = 0; (frame < seconds * fps) && (ticks < maxticks); frame++)
  {
    double newacc = acc + rate;
    int calls = (int)floor(newacc) - (int)floor(acc);
    acc = newacc;
    while ((calls--) && (ticks < maxticks))
    {
      SFRAME *f = &frames[ticks];
      unsigned char dip;

      // (The C64's time, for the SID's voice 3)
      c->now = initend + (long long)(ticks * (double)framecycles / rate);
      r = runroutine(c, playadr, 0, irqmode, PLAY_MAXINSTR, &dip);
      if (r == -1)
      {
        setreport(err, errsize, "The tune's play routine crashed (opcode $%02X at $%04X)", cpu->errorop, cpu->errorpc);
        goto fail;
      }
      if (r == -2)
      {
        setreport(err, errsize, "The tune's play routine doesn't return (it may need a real C64)");
        goto fail;
      }
      snapshot(f, cpu->mem + 0xd400, dip);
      ticks++;
    }
  }
  free(c);
  *framesout = frames;
  *numticks = ticks;
  return 1;

fail:
  free(frames);
  free(c);
  return result;
}

// What the notes are worked out for (see approxconvert()). clockhz turns the
// SID's register values into the ones that sound the same on the clock the
// song is played at, on GoatTracker's PAL scale; tunehz is the A-4 of the
// frequency table on that scale (GoatTracker's base pitch, 440 for its own
// table), gtfreq[] the table
static double clockhz = PAL_CLOCK;
static double tunehz = 440.0;
static unsigned short gtfreq[96];

static double notesemis(unsigned freq)
{
  return 57.0 + 12.0 * log(freq * clockhz / 16777216.0 / tunehz) / log(2.0);
}

static int clampint(int n, int lo, int hi)
{
  if (n < lo) return lo;
  if (n > hi) return hi;
  return n;
}

static int nearestnote(unsigned freq)
{
  return clampint((int)floor(notesemis(freq) + 0.5), 0, MAX_NOTES - 1);
}

// Wavetable entry for a recorded frame. gate 0: the program turns the gate
// off (both players write the waveform ANDed with the gate mask). tonal: the
// note is mostly a tone, its noise is an attack that follows the pitch
static void frameentry(const SFRAME *f, int v, int note, int base, int gate, int tonal, unsigned char *wl, unsigned char *wr)
{
  unsigned char c = f->ctrl[v] | 0x01;   // Else the gate is the player's business
  int rel;

  if (!gate) c &= 0xfe;
  if (!(c & 0xf0)) *wl = WAVESILENT | (c & 0x0f);
  else if (c >= WAVESILENT) *wl = 0x80 | (c & 0x0f);
  else *wl = c;

  if (!f->freq[v])
  {
    *wr = 0x80;
    return;
  }
  if (note < 0)
  {
    *wr = 0x80 | clampint(nearestnote(f->freq[v]), 1, MAX_NOTES - 1);
    return;
  }
  if (c & 0x80)
  {
    // Noise follows the note in a tone, or when it's the note's pitch (or
    // octaves of it); else it's a fixed pitch (drums)
    note = nearestnote(f->freq[v]);
    rel = note - base;
    if (rel % 12) rel = 0x100;
  }
  else rel = note - base;
  if ((rel >= -0x20) && (rel <= 0x5f)) *wr = rel & 0x7f;
  else *wr = 0x80 | clampint(note, 1, MAX_NOTES - 1);
}

// Shortest "start + loop" that gives the entries. Returns the rows, the
// loop start in *loop (-1 = hold the last row)
static int findloop(const unsigned char *wl, const unsigned char *wr, int len, int *loop)
{
  int total, a;
  for (total = 1; total <= len; total++)
  {
    for (a = 0; a < total; a++)
    {
      int p = total - a;
      int f;
      if (p > LOOPMAXPERIOD) continue;
      // A loop must be seen repeat at least once (else hold the last row)
      if ((p > 1) && (len - total < p)) continue;
      for (f = a + p; f < len; f++)
      {
        if ((wl[f] != wl[f-p]) || (wr[f] != wr[f-p])) break;
      }
      if (f >= len)
      {
        *loop = (p > 1) ? a : -1;
        return total;
      }
    }
  }
  *loop = -1;
  return len;
}

static int pulsediff(int a, int b)
{
  int d = (a - b) & 0xfff;
  if (d >= 0x800) d = 0x1000 - d;
  return d;
}

static int pulsedelta(int a, int b)
{
  return ((a - b + 0x800) & 0xfff) - 0x800;
}

// The pulse values a pulse program gives (as the player runs it)
static void simpulse(const AINSTR *in, int frames, int *out)
{
  int ptr = 0, time = 0, pulse = 0, f;
  for (f = 0; f < frames; f++)
  {
    if (ptr >= 0)
    {
      int advance = 1;
      if (in->pl[ptr] >= 0x80) pulse = ((in->pl[ptr] & 0x0f) << 8) | in->pr[ptr];
      else
      {
        if (!time) time = in->pl[ptr];
        pulse += (signed char)in->pr[ptr];
        time--;
        if (time) advance = 0;
      }
      if (advance)
      {
        ptr++;
        if ((ptr >= in->plen) || (in->pl[ptr] == 0xff))
          ptr = ((ptr < in->plen) && (in->pr[ptr])) ? in->pr[ptr] - 1 : -1;
      }
    }
    out[f] = pulse & 0xfff;
  }
}

// Adds pulse program rows for a modulation run (127 frames a row at most).
// Returns the first row's index, -1 if there's no room
static int pulserun(AINSTR *in, int frames, int delta)
{
  int first = in->plen;
  while (frames > 0)
  {
    int n = (frames > 0x7f) ? 0x7f : frames;
    if (in->plen >= MAXPULSEROWS) return -1;
    in->pl[in->plen] = n;
    in->pr[in->plen] = delta & 0xff;
    in->plen++;
    frames -= n;
  }
  return first;
}

// Pulse program from the recorded pulse values (len frames, the note's own
// the first ownlen): set the start pulse, then modulation steps. A sweep
// going back and forth loops its two steps (after a first part step when it
// starts halfway); one still going at the end of a pulse chain (pv longer
// than the note) goes on
static void makepulse(AINSTR *in, const int *pv, int len, int ownlen, const LEVEL *lv)
{
  int runlen[MAXPULSEROWS], rundelta[MAXPULSEROWS], runset[MAXPULSEROWS];
  int numruns = 0, f = 1, c, cycle = -1, ended, loopto = -1;
  int p = pv[0], p0;

  // No modulation steps: the pulse the note has on average (its start may
  // be a sweep's, 0 say)
  if (!lv->maxruns)
  {
    long sum = 0;
    for (c = 0; c < ownlen; c++) sum += pv[c];
    p = (int)(sum / ownlen);
    len = ownlen;
  }
  p0 = (p + lv->p0quant / 2) / lv->p0quant * lv->p0quant;
  // (Rounded to 0 or $1000 a pulse that isn't would be silent)
  if ((!p0) && (p)) p0 = lv->p0quant;
  if (p0 > 0xfff) p0 = (p < 0xfff) ? 0x1000 - lv->p0quant : 0xfff;
  while ((f < len) && (numruns < MAXPULSEROWS - 1))
  {
    int d = pulsedelta(pv[f], pv[f-1]);
    if ((d > 127) || (d < -128))
    {
      runset[numruns] = pv[f];
      runlen[numruns] = 1;
      rundelta[numruns] = 0;
      numruns++;
      f++;
      continue;
    }
    c = 1;
    while ((f + c < len) && (pulsedelta(pv[f+c], pv[f+c-1]) == d)) c++;
    runset[numruns] = -1;
    runlen[numruns] = c;
    rundelta[numruns] = d;
    numruns++;
    f += c;
  }
  ended = (f >= len);
  // Holding the pulse at the end needs no row
  while ((numruns) && (runset[numruns-1] < 0) && (!rundelta[numruns-1]))
  {
    numruns--;
    ended = 0;
  }

  in->pl[0] = 0x80 | (p0 >> 8);
  in->pr[0] = p0 & 0xff;
  in->plen = 1;

  // Back and forth: steps d, -d, d from the start (or after a first part step
  // of -d: the sweep started halfway)
  if (lv->maxruns >= 2)
  {
    for (c = 0; (c <= 1) && (cycle < 0); c++)
    {
      int k;
      if (numruns < c + 3) break;
      for (k = 0; k < c + 3; k++) if (runset[k] >= 0) break;
      if (k < c + 3) continue;
      if ((rundelta[c]) && (rundelta[c+1] == -rundelta[c]) && (rundelta[c+2] == rundelta[c]) &&
        ((!c) || (rundelta[0] == rundelta[c+1]))) cycle = c;
    }
  }
  if (cycle >= 0)
  {
    int r;
    if ((cycle) && (pulserun(in, runlen[0], rundelta[0]) < 0)) goto stop;
    r = pulserun(in, runlen[cycle], rundelta[cycle]);
    if ((r < 0) || (pulserun(in, runlen[cycle+1], rundelta[cycle+1]) < 0)) goto stop;
    loopto = r;
    goto end;
  }
  if (numruns > lv->maxruns)
  {
    numruns = lv->maxruns;
    ended = 0;
  }
  for (c = 0; c < numruns; c++)
  {
    int r;
    if (runset[c] >= 0)
    {
      if (in->plen >= MAXPULSEROWS) break;
      in->pl[in->plen] = 0x80 | (runset[c] >> 8);
      in->pr[in->plen] = runset[c] & 0xff;
      in->plen++;
      continue;
    }
    r = pulserun(in, runlen[c], rundelta[c]);
    if (r < 0) break;
    // A sweep still going at the end of the notes that follow it goes on
    if ((c == numruns - 1) && (ended) && (len > ownlen) && (rundelta[c])) loopto = r;
  }
  if (c < numruns) loopto = -1;
  goto end;

stop:
  // (No room for the loop: just the start pulse)
  in->plen = 1;
  loopto = -1;
end:
  in->pl[in->plen] = 0xff;
  in->pr[in->plen] = (loopto >= 0) ? loopto + 1 : 0;
  in->plen++;
}

static int notecmp_len(const void *a, const void *b)
{
  const ANOTE *na = *(const ANOTE * const *)a;
  const ANOTE *nb = *(const ANOTE * const *)b;
  if (na->plen != nb->plen) return nb->plen - na->plen;
  if (na->sub != nb->sub) return na->sub - nb->sub;
  if (na->start != nb->start) return na->start - nb->start;
  return na->voice - nb->voice;
}

// When a note plays, over all the subtunes (for numbering in order of use)
static int notetime(const ANOTE *n)
{
  return n->sub * (MAXTICKS + 1) + n->start;
}

static int notecmp_start(const void *a, const void *b)
{
  const ANOTE *na = (const ANOTE *)a;
  const ANOTE *nb = (const ANOTE *)b;
  if (na->voice != nb->voice) return na->voice - nb->voice;
  return na->start - nb->start;
}

// The program entries of a note
static int noteentries(const RECORDING *rec, const ANOTE *n, int cap, int drift, int release, unsigned char *wl,
  unsigned char *wr, int *pv)
{
  int f, len = release ? n->plen : n->gplen, tones = 0;
  int keep = (n->start > 0);
  for (f = 0; f < len; f++)
  {
    unsigned char c = rec->fr[n->pstart + f].ctrl[n->voice];
    if ((c & 0xf0) && (!(c & 0x80))) tones++;
  }
  for (f = 0; f < len; f++)
  {
    int t = n->pstart + f;
    // (The player's gate goes on a frame before the program, with the
    // first waveform: the gate is on as many frames as in the recording)
    frameentry(&rec->fr[t], n->voice, rec->pn[n->voice][t], n->base, (!n->shortgate) || (t - n->pstart + 1 < n->gateoff - n->start),
      tones * 2 >= len, &wl[f], &wr[f]);
    pv[f] = rec->fr[t].pulse[n->voice];
    // A silent frame (no waveform, or the test bit: often a hardrestart at
    // the end) sounds the same at any pitch: it keeps the pitch (programs
    // that differ only there are one)
    if ((f) && ((wl[f] >= WAVESILENT) || (wl[f] & 0x08))) wr[f] = 0x80;
    // Leading frames still on the previous note's frequency
    if ((keep) && (rec->fr[t].freq[n->voice] == rec->fr[n->start - 1].freq[n->voice]))
    {
      if (wr[f] != 0) wr[f] = 0x80;
    }
    else keep = 0;
  }
  // Pitch that wanders without repeating (slides) would make every note an
  // instrument of its own: keep the waveforms & absolute notes only (unless
  // it fits: drift set)
  if ((len > STABLEPROG) && (!drift))
  {
    int loop;
    if (findloop(wl, wr, len, &loop) > STABLEPROG)
    {
      unsigned char zr[PROGMAX];
      for (f = 0; f < len; f++) zr[f] = (wr[f] & 0x80) ? wr[f] : 0;
      if (findloop(wl, zr, len, &loop) <= STABLEPROG) memcpy(wr, zr, len);
    }
  }
  // Fewer frames: the last one kept is the most common of the rest (the
  // sound the note mostly has)
  if (len > cap)
  {
    int best = cap - 1, bestcount = 0, e;
    for (f = cap - 1; f < len; f++)
    {
      int count = 0;
      for (e = cap - 1; e < len; e++)
      {
        if ((wl[e] == wl[f]) && (wr[e] == wr[f])) count++;
      }
      if (count > bestcount)
      {
        bestcount = count;
        best = f;
      }
    }
    wl[cap - 1] = wl[best];
    wr[cap - 1] = wr[best];
    pv[cap - 1] = pv[best];
    // (A pitch that keeps changing has none: an arpeggio coming back to the
    // note stays on the note, a slide at the last tone's pitch of the rows
    // before)
    if ((bestcount * 2 <= len - cap + 1) && (wr[cap - 1] < 0x80))
    {
      int back = 0;
      for (e = cap - 1; e < len; e++) if (wr[e] == 0) back++;
      if (back < 2) for (e = cap - 2; (e >= 0) && ((wr[e] >= 0x80) || (wl[e] & 0x80) || (wl[e] < 0x10)); e--);
      wr[cap - 1] = ((back < 2) && (e >= 0)) ? wr[e] : 0;
    }
    len = cap;
  }
  return len;
}

// A wave program as written into the song: after the first row (which sets
// the note's frequency), rows that keep the pitch the row before them gave
// (on a loop, also the last row) don't set it: setting the note restarts
// vibrato and stops slides
static void keeppitch(unsigned char *wr, int len, int loop)
{
  unsigned char orig[PROGMAX + 1];
  int f;
  memcpy(orig, wr, len);
  for (f = 1; f < len; f++)
  {
    if ((orig[f] >= 0x80) || (orig[f-1] != orig[f])) continue;
    if ((loop == f) && (orig[len-1] != orig[f])) continue;
    wr[f] = 0x80;
  }
}

static int usespulse(const unsigned char *wl, int len)
{
  int f;
  for (f = 0; f < len; f++)
  {
    if ((wl[f] >= 0x10) && (wl[f] < WAVESILENT) && (wl[f] & 0x40)) return 1;
  }
  return 0;
}

// The wave & pulse programs of the instruments, and of notes that have a
// program of their own (set by a command on their row): each once, their
// rows counted against the tables' room
typedef struct
{
  int len, loop;                  // Rows, loop row (-1 = stop)
  unsigned char l[PROGMAX], r[PROGMAX];
  int gateoff;                    // The program turns the gate off
} AWAVE;

typedef struct
{
  int len;
  unsigned char l[MAXPULSEROWS + 1], r[MAXPULSEROWS + 1];
} APULSE;

#define MAXPROGS (MAX_TABLELEN / 2 + 1)

typedef struct
{
  AWAVE wave[MAXPROGS];
  APULSE pulse[MAXPROGS];
  int numwave, numpulse;
  int waverows, pulserows;
} APROGS;

// The wave program's index (added if new), -1 if there's no room
static int findwave(APROGS *pg, const unsigned char *l, const unsigned char *r, int len, int loop)
{
  AWAVE *w;
  int c;
  for (c = 0; c < pg->numwave; c++)
  {
    w = &pg->wave[c];
    if ((w->len == len) && (w->loop == loop) && (!memcmp(w->l, l, len)) && (!memcmp(w->r, r, len))) return c;
  }
  if ((pg->numwave >= MAXPROGS) || (pg->waverows + len + 1 > MAX_TABLELEN)) return -1;
  w = &pg->wave[pg->numwave];
  w->len = len;
  w->loop = loop;
  memcpy(w->l, l, len);
  memcpy(w->r, r, len);
  w->gateoff = 0;
  for (c = 0; c < len; c++)
  {
    if (!(l[c] & 0x01)) w->gateoff = 1;
  }
  pg->waverows += len + 1;
  return pg->numwave++;
}

static int findpulse(APROGS *pg, const unsigned char *l, const unsigned char *r, int len)
{
  APULSE *p;
  int c;
  for (c = 0; c < pg->numpulse; c++)
  {
    p = &pg->pulse[c];
    if ((p->len == len) && (!memcmp(p->l, l, len)) && (!memcmp(p->r, r, len))) return c;
  }
  if ((pg->numpulse >= MAXPROGS) || (pg->pulserows + len > MAX_TABLELEN)) return -1;
  p = &pg->pulse[pg->numpulse];
  p->len = len;
  memcpy(p->l, l, len);
  memcpy(p->r, r, len);
  pg->pulserows += len;
  return pg->numpulse++;
}

// The pulse program for a note (into in): from its pulse values, or (its
// sweep going on through the notes after it) from the pulse of them all
static void notepulse(const ANOTE *n, const int *pv, int len, const LEVEL *lv, AINSTR *in)
{
  if ((n->pchain > len) && (lv->maxruns))
  {
    int chain[PULSECHAIN];
    int f, frames = n->pchain;
    if (n->pstart + frames > n->rec->numticks) frames = n->rec->numticks - n->pstart;
    for (f = 0; f < frames; f++) chain[f] = n->rec->fr[n->pstart + f].pulse[n->voice];
    if (frames > len)
    {
      makepulse(in, chain, frames, len, lv);
      return;
    }
  }
  makepulse(in, pv, len, len, lv);
}

#define DIFF_NONE 0
#define DIFF_AD 1
#define DIFF_SR 2
#define DIFF_WAVE 3
#define DIFF_PULSE 4

// Does the instrument play like the note? Returns -1 if not, else what
// differs (one thing at most, that a command on the note's row sets): AD
// or SR when the level allows it, the wave or pulse program when override
// is set (then also AD or SR at any level), else DIFF_NONE
static int instrmatches(const AINSTR *in, const ANOTE *n, const SFRAME *f0, const unsigned char *wl, const unsigned char *wr,
  const int *pv, int len, int pulse, const LEVEL *lv, int override)
{
  int f, diff = DIFF_NONE;
  int adok = (in->ad == f0->ad[n->voice]), srok = (in->sr == f0->sr[n->voice]);
  int gt = in->gatetimer & 0x3f;
  if (in->firstwave != n->firstwave) return -1;
  // How the note starts (after a rest: any way but legato)
  if ((n->gtflags >= 0) ? ((in->gatetimer & 0xc0) != n->gtflags) : (in->gatetimer & 0x40)) return -1;
  // When the gate goes off before the next row
  if (gt > n->gtmax) return -1;
  if ((n->gtwant) && (abs(gt - n->gtwant) > lv->gttol)) return -1;
  if ((!adok) && (!srok)) return -1;
  if (!adok)
  {
    if ((!override) && (lv->adsrflex < 2)) return -1;
    diff = DIFF_AD;
  }
  if (!srok)
  {
    if ((!override) && (lv->adsrflex < 1)) return -1;
    diff = DIFF_SR;
  }
  if ((lv->vib) && ((in->vibdelay != n->vibdelay) || (in->vibleft != n->vibleft) || (in->vibright != n->vibright)))
    return -1;
  // A note whose pulse goes on from the note before can't have a pulse
  // program: it would start the pulse over (while the pulse matters)
  if ((n->pulsecont) && (in->plen) && (lv->pulsetol < 0x1000)) return -1;
  for (f = 0; f < len; f++)
  {
    int row = f;
    if (row >= in->wlen) row = (in->wloop >= 0) ? in->wloop + (f - in->wloop) % (in->wlen - in->wloop) : in->wlen - 1;
    if ((wl[f] != in->wl[row]) || (wr[f] != in->wr[row]))
    {
      if ((!override) || (diff)) return -1;
      diff = DIFF_WAVE;
      break;
    }
  }
  if (pulse)
  {
    int sim[PROGMAX];
    int same = (in->plen > 0);
    if (same)
    {
      simpulse(in, len, sim);
      for (f = 0; f < len; f++)
      {
        if (pulsediff(sim[f], pv[f]) > lv->pulsetol) same = 0;
      }
    }
    if (!same)
    {
      if ((!override) || (diff)) return -1;
      diff = DIFF_PULSE;
    }
  }
  return diff;
}

// Gives every note an instrument at a level of detail (override: also one
// that differs in one thing a command on the note's row sets: its AD, SR,
// wave or pulse program). Returns the number of instruments, or -1 if they
// don't fit (instruments or table rows) and force isn't set
static int assigninstruments(ANOTE **order, int numnotes, AINSTR *ins, int maxins, APROGS *pg, int levelnum, int drift,
  int override, int release, int force)
{
  const LEVEL *lv = &levels[levelnum];
  int numins = 0, c, d;

  pg->numwave = pg->numpulse = 0;
  pg->waverows = pg->pulserows = 0;
  for (c = 0; c < numnotes; c++)
  {
    ANOTE *n = order[c];
    const SFRAME *f0 = &n->rec->fr[n->start];
    unsigned char wl[PROGMAX], wr[PROGMAX];
    int pv[PROGMAX];
    int len = noteentries(n->rec, n, lv->wcap, drift, release, wl, wr, pv);
    int pulse = usespulse(wl, len) && (!n->pulsecont);
    int time = notetime(n);
    int best = -1, bestdiff = DIFF_PULSE + 1, w, p;
    AINSTR *in;

    n->ovwave = n->ovpulse = -1;
    for (d = 0; d < numins; d++)
    {
      int k = instrmatches(&ins[d], n, f0, wl, wr, pv, len, pulse, lv, override);
      if ((k < 0) || (k >= bestdiff)) continue;
      best = d;
      bestdiff = k;
      if (k == DIFF_NONE) break;
    }
    // (The note's own program, if there's room in the table for it)
    if (bestdiff == DIFF_WAVE)
    {
      int loop, wlen = findloop(wl, wr, len, &loop);
      n->ovwave = findwave(pg, wl, wr, wlen, loop);
      if (n->ovwave < 0) best = -1;
    }
    if (bestdiff == DIFF_PULSE)
    {
      AINSTR t;
      memset(&t, 0, sizeof t);
      notepulse(n, pv, len, lv, &t);
      n->ovpulse = findpulse(pg, t.pl, t.pr, t.plen);
      if (n->ovpulse < 0) best = -1;
    }
    if (best >= 0)
    {
      n->instr = best;
      if (time < ins[best].firstuse) ins[best].firstuse = time;
      continue;
    }
    n->ovwave = n->ovpulse = -1;
    if (numins >= maxins)
    {
      int bestscore = -1;
      if (!force) return -1;
      best = 0;
      // Out of instruments: the most alike, the ADSR first (a command can
      // set one of AD/SR)
      for (d = 0; d < numins; d++)
      {
        int score = 0;
        if (ins[d].ad == f0->ad[n->voice]) score += 16;
        if (ins[d].sr == f0->sr[n->voice]) score += 16;
        if (ins[d].gateoff == n->shortgate) score += 8;
        if ((ins[d].wl[0] & 0xf0) == (wl[0] & 0xf0)) score += 8;
        // (A note whose pulse goes on is better off with a pulse program
        // than with none: the pulse may not have been set. One that sets
        // the pulse would be silent without: that before the ADSR)
        if ((usespulse(wl, len)) && (!n->pulsecont)) score += (ins[d].plen > 0) ? 40 : 0;
        else if ((ins[d].plen > 0) == (usespulse(wl, len) > 0)) score += 4;
        if (ins[d].gatetimer == n->gatetimer) score += 1;
        if ((ins[d].vibdelay == n->vibdelay) && (ins[d].vibleft == n->vibleft) && (ins[d].vibright == n->vibright)) score += 6;
        if (score > bestscore)
        {
          bestscore = score;
          best = d;
        }
      }
      n->instr = best;
      if (time < ins[best].firstuse) ins[best].firstuse = time;
      continue;
    }
    in = &ins[numins];
    memset(in, 0, sizeof(AINSTR));
    in->ad = f0->ad[n->voice];
    in->sr = f0->sr[n->voice];
    in->firstwave = n->firstwave;
    in->gatetimer = n->gatetimer;
    if (lv->vib)
    {
      in->vibdelay = n->vibdelay;
      in->vibleft = n->vibleft;
      in->vibright = n->vibright;
    }
    in->wlen = findloop(wl, wr, len, &in->wloop);
    memcpy(in->wl, wl, in->wlen);
    memcpy(in->wr, wr, in->wlen);
    for (d = 0; d < in->wlen; d++)
    {
      if (!(in->wl[d] & 0x01)) in->gateoff = 1;
    }
    if (pulse) notepulse(n, pv, len, lv, in);
    in->firstuse = time;
#ifdef SID2SNG_DEBUG
    if ((getenv("SID2SNG_INSDUMP")) && (atoi(getenv("SID2SNG_INSDUMP")) == levelnum))
    {
      int e;
      fprintf(stderr, "s%d v%d t%d len %d: %02X %02X fw %02X gt %02X%s |", n->sub, n->voice, n->start, len, in->ad, in->sr,
        in->firstwave, in->gatetimer, in->gateoff ? " gateoff" : "");
      for (e = 0; e < in->wlen; e++) fprintf(stderr, " %02X:%02X", in->wl[e], in->wr[e]);
      fprintf(stderr, " loop %d | pulse", in->wloop);
      for (e = 0; e < in->plen; e++) fprintf(stderr, " %02X:%02X", in->pl[e], in->pr[e]);
      fprintf(stderr, "\n");
    }
#endif
    n->instr = numins;
    // The table has each program once (addprogram() finds it again)
    w = findwave(pg, in->wl, in->wr, in->wlen, in->wloop);
    p = in->plen ? findpulse(pg, in->pl, in->pr, in->plen) : 0;
    numins++;
    if ((!force) && ((w < 0) || (p < 0))) return -1;
  }
  return numins;
}

// Finds a program already in the table (jump targets relative to the
// program), or adds it. Returns the 1-based row, 0 if no room
static int addprogram(SNGDATA *sng, int t, const unsigned char *pl, const unsigned char *pr, int len)
{
  unsigned char *tl = sng->ltable[t];
  unsigned char *tr = sng->rtable[t];
  int c, d, pos;

  for (c = 0; c + len <= sng->tablelen[t]; c++)
  {
    for (d = 0; d < len; d++)
    {
      unsigned char r = ((pl[d] == 0xff) && (pr[d])) ? pr[d] + c : pr[d];
      if ((tl[c+d] != pl[d]) || (tr[c+d] != r)) break;
    }
    if (d == len) return c + 1;
  }
  if (sng->tablelen[t] + len > MAX_TABLELEN) return 0;
  pos = sng->tablelen[t];
  for (d = 0; d < len; d++)
  {
    tl[pos + d] = pl[d];
    tr[pos + d] = ((pl[d] == 0xff) && (pr[d])) ? pr[d] + pos : pr[d];
  }
  sng->tablelen[t] += len;
  return pos + 1;
}

// A wave program into the wavetable, as written into the song: after the
// first row, rows that keep the pitch don't set it (keeppitch()), then the
// jump to the loop (or the stop). Returns the 1-based row, 0 if no room
static int wavetotable(SNGDATA *sng, const unsigned char *l, const unsigned char *r, int len, int loop)
{
  unsigned char wl[PROGMAX + 1], wr[PROGMAX + 1];
  memcpy(wl, l, len);
  memcpy(wr, r, len);
  keeppitch(wr, len, loop);
  wl[len] = 0xff;
  wr[len] = (loop >= 0) ? loop + 1 : 0;
  return addprogram(sng, WTBL, wl, wr, len + 1);
}

// Speedtable row for a glide speed (0 = none: tie notes)
static int speedrow(SNGDATA *sng, int speed)
{
  unsigned char l, r;
  int shift = 0;
  if (speed <= 0) return 0;
  // Rounded to 4 significant bits, there are only 255 rows
  while ((speed >> shift) > 15) shift++;
  if (shift) speed = ((speed + (1 << (shift - 1))) >> shift) << shift;
  if (speed > 0x7fff) speed = 0x7fff;
  l = speed >> 8;
  r = speed & 0xff;
  return addprogram(sng, STBL, &l, &r, 1);
}

static const char *wavename(unsigned char w)
{
  if ((w < 0x10) || (w >= WAVESILENT)) return "Silent";
  if (w & 0x80) return "Noise";
  switch (w & 0x70)
  {
    case 0x10: return "Tri";
    case 0x20: return "Saw";
    case 0x40: return "Pulse";
  }
  return "Mix";
}

static int cmpint(const void *a, const void *b)
{
  return *(const int *)a - *(const int *)b;
}

static int growtonotes(ANOTE **notes, int *maxnotes, int numnotes)
{
  if (numnotes >= *maxnotes)
  {
    int newmax = *maxnotes ? *maxnotes * 2 : 1024;
    ANOTE *newnotes = (ANOTE *)realloc(*notes, newmax * sizeof(ANOTE));
    if (!newnotes) return 0;
    *notes = newnotes;
    *maxnotes = newmax;
  }
  return 1;
}

// GoatTracker's frequency table (PAL)
static const unsigned short gtdefaultfreq[96] =
{
  0x0117,0x0127,0x0139,0x014b,0x015f,0x0174,0x018a,0x01a1,0x01ba,0x01d4,0x01f0,0x020e,
  0x022d,0x024e,0x0271,0x0296,0x02be,0x02e8,0x0314,0x0343,0x0374,0x03a9,0x03e1,0x041c,
  0x045a,0x049c,0x04e2,0x052d,0x057c,0x05cf,0x0628,0x0685,0x06e8,0x0752,0x07c1,0x0837,
  0x08b4,0x0939,0x09c5,0x0a5a,0x0af7,0x0b9e,0x0c4f,0x0d0a,0x0dd1,0x0ea3,0x0f82,0x106e,
  0x1168,0x1271,0x138a,0x14b3,0x15ee,0x173c,0x189e,0x1a15,0x1ba2,0x1d46,0x1f04,0x20dc,
  0x22d0,0x24e2,0x2714,0x2967,0x2bdd,0x2e79,0x313c,0x3429,0x3744,0x3a8d,0x3e08,0x41b8,
  0x45a1,0x49c5,0x4e28,0x52cd,0x57ba,0x5cf1,0x6278,0x6853,0x6e87,0x751a,0x7c10,0x8371,
  0x8b42,0x9389,0x9c4f,0xa59b,0xaf74,0xb9e2,0xc4f0,0xd0a6,0xdd0e,0xea33,0xf820,0xffff
};

// The frequency table the notes are worked out for: GoatTracker's own (base
// pitch 0), or the one the tracker makes for a base pitch (A-4 in Hz on the
// PAL clock: calculatefreqtable() in goattrk2.c, 12 notes an octave)
static void settuning(double basepitch)
{
  int c;
  if (basepitch <= 0.0)
  {
    tunehz = 440.0;
    memcpy(gtfreq, gtdefaultfreq, sizeof gtfreq);
    return;
  }
  tunehz = basepitch;
  for (c = 0; c < 96; c++)
  {
    // (The tracker keeps the base pitch as a float)
    double freq = (double)(float)basepitch * (16777216.0 / 985248.0) * pow(2.0, 0.25) / 32.0 * pow(2.0, c / 12.0);
    int intfreq = (int)(freq + 0.5);
    gtfreq[c] = (intfreq > 0xffff) ? 0xffff : intfreq;
  }
}

// EXACT: the tuning of a packed song. The packer puts the frequency table
// (the notes the song uses, low bytes then high bytes) just before the song
// table; the player reads it with "lda lo-FIRSTNOTE,y; sta; lda hi-FIRSTNOTE,
// y". Returns the base pitch whose table it is, 0 for GoatTracker's own, -1
// if not found or another tuning
static double packedtuning(SCAN *sc, unsigned songtbllo)
{
  unsigned pos = sc->lo;
  while (findnext(sc, "B9 <a >a (9D ?? ?? | 95 ?\?) B9 <b >b (9D ?? ?? | 95 ?\?)", &pos))
  {
    unsigned a = sc->cap[0], b = sc->cap[1];
    int num = (int)b - (int)a, first, n, top, same = 1, k;
    double est;
    pos++;
    if ((num < 1) || (num > 96)) continue;
    first = (int)songtbllo - (int)b - num;
    if ((first < 0) || (first + num > 96) || (a + first < sc->lo) || (b + first + num > sc->hi)) continue;
    for (n = first; n < first + num; n++)
    {
      unsigned f = sc->mem[a + n] | (sc->mem[b + n] << 8);
      if (f != gtdefaultfreq[n]) same = 0;
    }
    if (same) return 0.0;
    // The base pitch from the highest note (the most exact; not one cut off
    // at $FFFF), the one of the nearby ones in 0.01 Hz steps that gives the
    // whole table
    top = first + num - 1;
    while ((top > first) && ((sc->mem[a + top] | (sc->mem[b + top] << 8)) == 0xffff)) top--;
    est = (sc->mem[a + top] | (sc->mem[b + top] << 8)) * 985248.0 / 16777216.0 * pow(2.0, (57 - top) / 12.0);
    est = floor(est * 100.0 + 0.5) / 100.0;
    for (k = 0; k < 7; k++)
    {
      double tryhz = est + ((k & 1) ? -0.01 : 0.01) * ((k + 1) / 2);
      if (tryhz <= 0.0) continue;
      settuning(tryhz);
      for (n = first; n < first + num; n++)
      {
        unsigned f = sc->mem[a + n] | (sc->mem[b + n] << 8);
        if (f != gtfreq[n]) break;
      }
      if (n == first + num)
      {
        settuning(0.0);
        return tryhz;
      }
    }
    settuning(0.0);
    return -1.0;
  }
  return -1.0;
}

#define VIBFRAMES 48              // Frames of a note looked at for vibrato
#define VIBMAXCMP 31              // Slowest vibrato (ticks between turns)
#define VIBMAXDELAY 24

// The frequency offsets of GoatTracker's instrument vibrato (depth 1) from a
// note's start (tick 0): the first wave row sets the note (tick 1), effects
// run from tick 2 on (realtime optimization off, see approxconvert()), after
// the delay counted down on them
static void simvib(int delay, int cmp, int frames, int *out)
{
  unsigned char vt = 0;
  int off = 0, f, dl = delay;
  for (f = 0; f < frames; f++)
  {
    if (f >= 2)
    {
      if (dl > 1) dl--;
      else
      {
        if ((vt < 0x80) && (vt > cmp)) vt ^= 0xff;
        vt += 2;
        off += (vt & 1) ? -1 : 1;
      }
    }
    out[f] = off;
  }
}

// Vibrato in a note that holds its pitch: the instrument vibrato (delay,
// speed, depth: note-relative if near as good, so that the notes of an
// instrument can share it) that gives its frequency offsets best
static void findvibrato(const SFRAME *fr, int numticks, const int *pn, ANOTE *n)
{
  int o[VIBFRAMES], s[VIBFRAMES], w[VIBFRAMES];
  int len, f, v = n->voice, turns = 0, dir = 0, lo = 0x10000, hi = -1, count = 0;
  int delay, cmp, bestdelay = 0, bestl = 0, bestr = 0, onset = 0, startdelay;
  int absdelay = 0, absl = 0, absr = 0, reldelay = 0, rell = 0, relr = 0;
  double e0 = 0.0, best, bestabs = 1e30, bestrel = 1e30;
  unsigned center;

  n->vibdelay = n->vibleft = n->vibright = 0;
  if ((n->legato) || (n->base < 0) || (n->base + 1 >= 96)) return;
  for (len = 0; (len < VIBFRAMES) && (n->start + len < n->end) && (n->start + len < numticks); len++);
  if (len < 6) return;
  // The frames that play the note (waveform, no noise or test bit)
  for (f = 0; f < len; f++)
  {
    const SFRAME *fx = &fr[n->start + f];
    unsigned char c = fx->ctrl[v];
    w[f] = (f >= 1) && (c & 0x70) && (!(c & 0x88)) && (fx->freq[v]) && (pn[n->start + f] == n->base);
    if ((f >= 1) && (c & 0x70) && (!(c & 0x88)) && (pn[n->start + f] != n->base)) return;
    if (!w[f]) continue;
    if ((int)fx->freq[v] < lo) lo = fx->freq[v];
    if ((int)fx->freq[v] > hi) hi = fx->freq[v];
    count++;
  }
  if ((count < 5) || (hi <= lo)) return;
  center = (lo + hi + 1) / 2;
  // Big enough to hear (8 cents), turning a few times
  if ((hi - lo) * 1000 < (int)center * 5) return;
  for (f = 0; f < len; f++)
  {
    o[f] = w[f] ? (int)fr[n->start + f].freq[v] - (int)center : 0;
    e0 += (double)o[f] * o[f];
    if ((f) && (w[f]) && (w[f-1]) && (o[f] != o[f-1]))
    {
      int d = (o[f] > o[f-1]) ? 1 : -1;
      if ((dir) && (d != dir)) turns++;
      dir = d;
    }
  }
  if (turns < 2) return;

  // Where it starts wobbling: the delay. (Its phase can't be had: many
  // players' vibrato runs on through the notes, GoatTracker's starts with
  // each note. So the speed and depth are fitted at the phase that fits
  // best, but the instrument starts it where the note does)
  for (f = 1; (f < len) && ((!w[f]) || (!o[f]) || (o[f] == o[f-1])); f++);
  onset = f;
  startdelay = (onset - 1 > 1) ? onset - 1 : 1;
  if (startdelay > VIBMAXDELAY) return;
  for (cmp = 0; cmp <= VIBMAXCMP; cmp++)
  {
    for (delay = startdelay; (delay <= startdelay + 2 * cmp + 4) && (delay <= VIBMAXDELAY + 2 * VIBMAXCMP); delay++)
    {
      double so = 0.0, ss = 0.0, err;
      int depth, shift;
      simvib(delay, cmp, len, s);
      for (f = 0; f < len; f++)
      {
        if (!w[f]) continue;
        so += (double)o[f] * s[f];
        ss += (double)s[f] * s[f];
      }
      if (ss <= 0.0) continue;
      depth = (int)floor(so / ss + 0.5);
      if (depth >= 1)
      {
        if (depth > 255) depth = 255;
        err = e0 - 2.0 * depth * so + (double)depth * depth * ss;
        if (err < bestabs)
        {
          bestabs = err;
          absdelay = startdelay;
          absl = cmp;
          absr = depth;
        }
      }
      // Note-relative: the semitone's frequency step shifted down
      for (shift = 0; shift < 8; shift++)
      {
        int d = (gtfreq[n->base + 1] - gtfreq[n->base]) >> shift;
        if (d < 1) break;
        err = e0 - 2.0 * d * so + (double)d * d * ss;
        if (err < bestrel)
        {
          bestrel = err;
          reldelay = startdelay;
          rell = 0x80 | cmp;
          relr = shift;
        }
      }
    }
  }
  // Note-relative when near as good (an instrument's notes can share it)
  if ((reldelay) && (bestrel <= bestabs * 1.25 + e0 * 0.02))
  {
    best = bestrel;
    bestdelay = reldelay;
    bestl = rell;
    bestr = relr;
  }
  else
  {
    best = bestabs;
    bestdelay = absdelay;
    bestl = absl;
    bestr = absr;
  }
  // Only a good fit
  if ((!bestdelay) || (best > e0 * 0.45)) return;
  n->vibdelay = bestdelay;
  n->vibleft = bestl;
  n->vibright = bestr;
}

// The tick when a note can't be heard any more after its gate went off (the
// release has died down, at the release rate of each frame's SR), at most
// its end. tickms: milliseconds per tick
static int audibleend(const SFRAME *fr, const ANOTE *n, double tickms)
{
  static const int releasems[16] = {6, 24, 48, 72, 114, 168, 204, 240, 300, 750, 1500, 2400, 3000, 9000, 15000, 24000};
  double level = 1.0;
  int t;
  for (t = n->gateoff; t < n->end; t++)
  {
    level -= tickms / releasems[fr[t].sr[n->voice] & 0x0f];
    if (level <= 0.0) break;
  }
  return t;
}

static int timetorow(int t, int phase, int tempo)
{
  int r = (int)floor((double)(t - phase) / tempo + 0.5);
  return (r < 0) ? 0 : r;
}

// The first tick that timetorow() puts on a row
static int firsttick(int row, int phase, int tempo)
{
  return phase + row * tempo - tempo / 2;
}

//
// A tune whose timing shifts a frame or two against a grid of rows now and
// then (a part a frame longer than whole rows, say) is put on the rows by
// leaving those frames out (or doubling them): song ticks are the
// recording's ticks without them. at[] the ticks where the timing shifts,
// by[] by how much, in order
//

#define DRIFTRUN 4                // Onsets in a row on a shifted grid: the timing has shifted
#define DRIFTMAX 2                // Most ticks it may shift by (a third of a row at most)

static int warptick(const int *at, const int *by, int num, int t)
{
  int i, d = 0;
  for (i = 0; (i < num) && (at[i] <= t); i++) d += by[i];
  return t - d;
}

// The recording's tick a song tick plays (where frames are left out, the
// later ones)
static int unwarptick(const int *at, const int *by, int num, int g)
{
  int i, d = 0;
  for (i = 0; i < num; i++)
  {
    if (at[i] - (d + by[i]) > g) break;
    d += by[i];
  }
  return g + d;
}

// Where the onsets (in order) shift against a grid of tt ticks: a run of
// DRIFTRUN onsets all off by the same few ticks. Returns how many shifts
// (max at most)
static int finddrift(const int *on, int n, int tt, int *at, int *by, int max)
{
  int phase = 0, hits = -1, run = 0, runby = 0, runstart = 0, num = 0, shift = 0, c, p;
  if ((!n) || (tt < 3)) return 0;
  // (Starting on the grid most of the first notes are on)
  for (p = 0; p < tt; p++)
  {
    int h = 0;
    for (c = 0; (c < n) && (c < 64); c++) if (!((on[c] - p) % tt)) h++;
    if (h > hits)
    {
      hits = h;
      phase = p;
    }
  }
  for (c = 0; c < n; c++)
  {
    int r, d;
    if ((c) && (on[c] == on[c-1])) continue;
    r = (((on[c] - shift - phase) % tt) + tt) % tt;
    if (!r)
    {
      run = 0;
      continue;
    }
    d = (r > tt / 2) ? r - tt : r;
    if ((abs(d) > DRIFTMAX) || (abs(d) * 3 > tt))
    {
      run = 0;
      continue;
    }
    if ((run) && (d == runby)) run++;
    else
    {
      run = 1;
      runby = d;
      runstart = on[c];
    }
    if (run < DRIFTRUN) continue;
    if (num >= max) break;
    at[num] = runstart;
    by[num++] = d;
    shift += d;
    run = 0;
  }
  return num;
}

// One subtune of the conversion
typedef struct
{
  int subtune;                    // 1-n
  int ok;                         // Recorded, has notes
  char err[256];                  // Else why not
  RECORDING rec;
  SFRAME *fr;
  int numticks;
  double rate;
  ANOTE *notes;
  int numnotes;
  int tempo, phase;
  int *driftat, *driftby;         // Where the rows' timing shifts, by how much (see
  int numdrift;                   // finddrift()): rows are on song ticks
  int looprow, endrow;            // Loop: rows [looprow, endrow) repeat (-1 = no loop)
  int stopped;                    // The tune stops (nothing changes at the end of the recording)
  int totalrows;                  // Rows in the grid
  unsigned char *grid[MAX_CHN];   // The rows, 4 bytes each
  int usednotes, dropped, truncated;
  int gttempo, gtfunk;            // Tempo in the song; gtfunk: funktempo with this second value (0 = none)
  double tickratio;               // Song player ticks per play call of the SID (1 = the same speed)
  double pitchdist[2];            // How far the notes are from the frequency table, played PAL/NTSC (semitones)
  double tunesin, tunecos;        // The notes' pitch beyond the nearest note (A-4 = 440 Hz as they
  int tunecount;                  // sound) as directions on a circle, summed; how many
} ASUB;

static void freesub(ASUB *s)
{
  int v;
  for (v = 0; v < MAX_CHN; v++)
  {
    free(s->grid[v]);
    free(s->rec.pn[v]);
  }
  free(s->notes);
  free(s->fr);
  free(s->driftat);
  free(s->driftby);
}

// Frames alike for finding the loop. The pulse width is left out: a pulse
// sweep often runs on through the loop (and the song's pulse comes from the
// instruments anyway)
static int sameframe(const SFRAME *a, const SFRAME *b)
{
  return (!memcmp(a->freq, b->freq, sizeof a->freq)) && (!memcmp(a->ctrl, b->ctrl, MAX_CHN)) &&
    (!memcmp(a->ad, b->ad, MAX_CHN)) && (!memcmp(a->sr, b->sr, MAX_CHN)) &&
    (a->dip == b->dip) && (a->cutoff == b->cutoff) && (a->resfilt == b->resfilt) && (a->modevol == b->modevol);
}

static unsigned framehash(const SFRAME *f)
{
  unsigned h = 2166136261u;
  int v;
  for (v = 0; v < MAX_CHN; v++)
  {
    h = (h ^ f->freq[v]) * 16777619u;
    h = (h ^ (f->ctrl[v] | (f->ad[v] << 8) | (f->sr[v] << 16))) * 16777619u;
  }
  return (h ^ (f->dip | (f->cutoff << 8) | (f->resfilt << 16) | ((unsigned)f->modevol << 24))) * 16777619u;
}

// Where a subtune starts repeating: a start and a period (a multiple of
// step ticks) after which the recording only repeats itself, to its end.
// Of those, the one that leaves out the most of the recording, at least
// minsave ticks, with notes starting in the period. Returns 1 if found, 0
// if not, -1 if out of memory
static int findrepeat(const SFRAME *fr, int numticks, const int *onsetsum, int step, int minsave, int stopped,
  int *startout, int *periodout)
{
  unsigned *h;
  int p, t, found = 0, bestsave = minsave - 1;

  // Nothing changing at the end: the tune has stopped, it doesn't loop
  for (t = numticks - 1; (t > 0) && (sameframe(&fr[t], &fr[t-1])); t--);
  if (numticks - t >= stopped) return 0;

  h = (unsigned *)malloc((numticks + 1) * sizeof(unsigned));
  if (!h) return -1;
  for (t = 0; t < numticks; t++) h[t] = framehash(&fr[t]);
  // (A longer period can't leave out more than the best one so far)
  for (p = step; numticks - p > bestsave; p += step)
  {
    int s, save;
    for (t = numticks - p - 1; (t >= 0) && (h[t] == h[t+p]) && (sameframe(&fr[t], &fr[t+p])); t--);
    s = t + 1;
    save = numticks - p - s;
    if ((save <= bestsave) || (onsetsum[s + p] == onsetsum[s])) continue;
    found = 1;
    bestsave = save;
    *startout = s;
    *periodout = p;
  }
  free(h);
  return found;
}

// Records a subtune and finds its notes, its rhythm, the frames of each
// note's sound and (findloop set) where it starts repeating. Returns 1 on
// success, 0 if the subtune can't be used (the reason in s->err), -1 if
// out of memory
static int ratemultiplier(double rate);

static int analysesub(const SIDFILE *sf, const SID2SNG_OPTIONS *opt, int seconds, int findloop, ASUB *s)
{
  const SFRAME *fr;
  ANOTE *notes = NULL;
  int *onsets = NULL, *onsetsum = NULL, *warped = NULL, *driftat = NULL, *driftby = NULL;
  int drifting;
  int numticks = 0, numnotes = 0, maxnotes = 0;
  int tempo = 6, phase = 0;
  int result = -1, v, c, d, t;

  result = record(sf, s->subtune, seconds, &s->fr, &s->numticks, &s->rate, s->err, sizeof s->err);
  if (result <= 0) return result;
  drifting = 1;
  result = -1;
  fr = s->fr;
  numticks = s->numticks;
  s->rec.fr = fr;
  s->rec.numticks = numticks;

  // Pitch of every tick, sticking to the previous note through vibrato
  for (v = 0; v < MAX_CHN; v++)
  {
    int prev = -1;
    s->rec.pn[v] = (int *)malloc((numticks + 1) * sizeof(int));
    if (!s->rec.pn[v]) goto done;
    for (t = 0; t < numticks; t++)
    {
      unsigned freq = fr[t].freq[v];
      if (!freq) s->rec.pn[v][t] = -1;
      else
      {
        double st = notesemis(freq);
        int near = clampint((int)floor(st + 0.5), 0, MAX_NOTES - 1);
        if ((prev < 0) || (fabs(st - prev) >= 0.5 + STICKY)) prev = near;
        // Settled (the same for as long as a new note lasts, so not
        // vibrato): the nearest note. A slide that ends a bit off a note
        // would stay on the one before
        else if ((prev != near) && (t >= RUNMIN - 1))
        {
          int k;
          for (k = 1; (k < RUNMIN) && (fr[t-k].freq[v] == freq); k++);
          if (k == RUNMIN)
          {
            prev = near;
            for (k = 1; k < RUNMIN; k++) s->rec.pn[v][t-k] = prev;
          }
        }
        s->rec.pn[v][t] = prev;
      }
    }
  }

  // Notes start on gate-on edges
  for (v = 0; v < MAX_CHN; v++)
  {
    int prevgate = 0, cur = -1;
    for (t = 0; t < numticks; t++)
    {
      int gate = fr[t].ctrl[v] & 1;
      if ((gate) && ((!prevgate) || ((fr[t].dip >> v) & 1)))
      {
        if (!growtonotes(&notes, &maxnotes, numnotes)) goto done;
        if (cur >= 0)
        {
          notes[cur].end = t;
          if (notes[cur].gateoff < 0) notes[cur].gateoff = t;
        }
        cur = numnotes++;
        memset(&notes[cur], 0, sizeof(ANOTE));
        notes[cur].voice = v;
        notes[cur].start = t;
        notes[cur].end = numticks;
        notes[cur].gateoff = -1;
        notes[cur].hradsr = -1;
        notes[cur].base = -1;
      }
      else if ((!gate) && (prevgate) && (cur >= 0) && (notes[cur].gateoff < 0)) notes[cur].gateoff = t;
      prevgate = gate;
    }
    if ((cur >= 0) && (notes[cur].gateoff < 0)) notes[cur].gateoff = numticks;
  }

  // Base notes, and new (legato) notes where the pitch moves to another
  // note and stays there
  {
    int orignotes = numnotes;
    for (c = 0; c < orignotes; c++)
    {
      int runnote = -1, runstart = 0, runlen = 0, ref = -1, refend = -1;
      int cur = c;
      v = notes[c].voice;

      for (t = notes[c].start; t < notes[cur].gateoff; t++)
      {
        int note = s->rec.pn[v][t];
        if ((note < 0) || (fr[t].ctrl[v] & 0x80) || (!(fr[t].ctrl[v] & 0xf0)))
        {
          runnote = -1;
          runlen = 0;
          continue;
        }
        if (note == runnote) runlen++;
        else
        {
          runnote = note;
          runstart = t;
          runlen = 1;
        }
        if (note == ref) refend = t;
        if (runlen != RUNMIN) continue;
        if (ref < 0)
        {
          ref = note;
          refend = t;
          if (notes[cur].base < 0) notes[cur].base = note;
        }
        else if ((note != ref) && (runstart > notes[cur].start + 1))
        {
          // The new note starts when the pitch leaves the old one: a glide
          // (toneportamento) if that takes a while
          int li, from = runstart, glide = 0;
          if ((refend > notes[cur].start) && (refend + 1 < runstart))
          {
            int frames = runstart - refend;
            int diff = abs((int)fr[runstart].freq[v] - (int)fr[refend].freq[v]);
            from = refend + 1;
            if (frames >= 2) glide = diff / (frames - 1);
          }
          if (!growtonotes(&notes, &maxnotes, numnotes)) goto done;
          li = numnotes++;
          notes[li] = notes[cur];
          notes[li].start = from;
          notes[li].legato = 1;
          notes[li].glide = glide;
          notes[li].base = note;
          notes[li].tied = 0;
          // The note so far ends here, the gate stays on
          notes[cur].end = from;
          notes[cur].gateoff = from;
          notes[cur].tied = 1;
          cur = li;
          ref = note;
          refend = t;
        }
      }
      // No stable pitch: the most common one
      if (notes[c].base < 0)
      {
        int counts[MAX_NOTES];
        int best = -1;
        ANOTE *n = &notes[c];
        memset(counts, 0, sizeof counts);
        for (t = n->start; (t < n->gateoff) && (t < n->start + PROGMAX); t++)
        {
          int note = s->rec.pn[v][t];
          if ((note >= 0) && (!(fr[t].ctrl[v] & 0x80))) counts[note]++;
        }
        for (d = 0; d < MAX_NOTES; d++)
        {
          if ((counts[d]) && ((best < 0) || (counts[d] > counts[best]))) best = d;
        }
        if (best < 0) best = fr[n->start].freq[v] ? nearestnote(fr[n->start].freq[v]) : 48;
        n->base = best;
      }
    }
    qsort(notes, numnotes, sizeof(ANOTE), notecmp_start);
    for (c = 0; c < numnotes; c++) notes[c].base = clampint(notes[c].base, 0, NUMNOTES - 1);
  }

  // How well the notes fit GoatTracker's frequency table played PAL or
  // NTSC (a tune is tuned for its own C64's clock, or for the other one)
  {
    double srcclock = (sf->info.clock == 2) ? NTSC_CLOCK : PAL_CLOCK;
    int k, count = 0;
    s->pitchdist[0] = s->pitchdist[1] = 0.0;
    s->tunesin = s->tunecos = 0.0;
    s->tunecount = 0;
    for (c = 0; c < numnotes; c++)
    {
      int t = (notes[c].start + 2 < numticks) ? notes[c].start + 2 : notes[c].start;
      unsigned freq = fr[t].freq[notes[c].voice];
      unsigned char ctrl = fr[t].ctrl[notes[c].voice];
      double semi;
      if (freq < 0x100) continue;
      for (k = 0; k < 2; k++)
      {
        semi = 12.0 * log(freq * srcclock * PAL_CLOCK / (k ? NTSC_CLOCK : PAL_CLOCK) / 16777216.0 / 440.0) / log(2.0);
        s->pitchdist[k] += fabs(semi - floor(semi + 0.5));
      }
      count++;
      // (Tones only: noise is often a fixed pitch)
      if ((ctrl & 0x80) || (ctrl & 0x08) || (!(ctrl & 0x70))) continue;
      semi = 12.0 * log(freq * srcclock / 16777216.0 / 440.0) / log(2.0);
      s->tunesin += sin(TWOPI * semi);
      s->tunecos += cos(TWOPI * semi);
      s->tunecount++;
    }
    if (count)
    {
      s->pitchdist[0] /= count;
      s->pitchdist[1] /= count;
    }
  }

  if (!numnotes)
  {
    setreport(s->err, sizeof s->err, "No notes found in subtune %d (silent, or it doesn't play)", s->subtune);
    result = 0;
    goto done;
  }

  // Rhythm: the most common time between notes, split into rows. From the
  // length asked for (a longer recording to find the loop gives the same)
  onsets = (int *)malloc(numnotes * sizeof(int));
  warped = (int *)malloc(numnotes * sizeof(int));
  driftat = (int *)malloc((numnotes + 1) * sizeof(int));
  driftby = (int *)malloc((numnotes + 1) * sizeof(int));
  if ((!onsets) || (!warped) || (!driftat) || (!driftby)) goto done;
  {
    int fps = (sf->info.clock == 2) ? 60 : 50;
    int rhythmend = (int)(clampint(opt->seconds, 1, 1800) * fps * s->rate) + 1;
    int hist[256];
    int common = 0, numon = 0;
    memset(hist, 0, sizeof hist);
    for (c = 0; c + 1 < numnotes; c++)
    {
      if ((notes[c].voice == notes[c+1].voice) && (notes[c+1].start < rhythmend))
      {
        int diff = notes[c+1].start - notes[c].start;
        if ((diff >= 2) && (diff < 256)) hist[diff]++;
      }
    }
    for (d = 2; d < 256; d++) if (hist[d] > hist[common]) common = d;
    if (!common) common = 6;
    for (c = 0; c < numnotes; c++) if (notes[c].start < rhythmend) onsets[numon++] = notes[c].start;
    if (!numon) onsets[numon++] = notes[0].start;
    qsort(onsets, numon, sizeof(int), cmpint);

    if (opt->tempo) tempo = opt->tempo;
    else if (opt->rowsperbeat > 0) tempo = (common + opt->rowsperbeat / 2) / opt->rowsperbeat;
    else
    {
      // The longest row that most notes start on, unless it loses notably
      // more notes than finer rows (a note off the rows that lands on the
      // row of the note before it is lost), or has notably more off them
      // (On the rows with the shifts of the timing taken out)
      int bestt = 0, lost[9], fewest = 0x7fffffff;
      double bestscore = -1.0, score[9], finest = 0.0;
      for (d = 1; d <= 8; d++)
      {
        int tt = common / d;
        int p, hits = 0, bestp = 0, nd;
        score[d] = -1.0;
        lost[d] = 0;
        if ((common % d) || (tt < 3)) continue;
        nd = drifting ? finddrift(onsets, numon, tt, driftat, driftby, numon) : 0;
        for (c = 0; c < numon; c++) warped[c] = warptick(driftat, driftby, nd, onsets[c]);
        for (p = 0; p < tt; p++)
        {
          int h = 0;
          for (c = 0; c < numon; c++) if ((warped[c] - p) % tt == 0) h++;
          if (h > hits)
          {
            hits = h;
            bestp = p;
          }
        }
        score[d] = (double)hits / numon;
        if (score[d] > finest) finest = score[d];
        for (c = 1; c < numnotes; c++)
        {
          if ((notes[c].start < rhythmend) && (notes[c].voice == notes[c-1].voice) &&
            (timetorow(warptick(driftat, driftby, nd, notes[c].start), bestp, tt) ==
            timetorow(warptick(driftat, driftby, nd, notes[c-1].start), bestp, tt))) lost[d]++;
        }
        if (lost[d] < fewest) fewest = lost[d];
      }
      for (d = 1; d <= 8; d++)
      {
        if (score[d] < 0.0) continue;
        if ((score[d] >= 0.8) && (lost[d] <= fewest + numon * TEMPOLOSS) && (score[d] >= finest - TEMPOOFF))
        {
          bestt = common / d;
          break;
        }
        if ((finest < 0.8) && (score[d] > bestscore + 0.05))
        {
          bestscore = score[d];
          bestt = common / d;
        }
      }
      if (!bestt) bestt = (common < 3) ? common * ((3 + common - 1) / common) : common;
      tempo = bestt;
    }
    tempo = clampint(tempo, 3, 0x7f);

    // Where the timing shifts, over the whole recording
    {
      int *all = (int *)malloc((numnotes + 1) * sizeof(int));
      if (!all) goto done;
      for (c = 0; c < numnotes; c++) all[c] = notes[c].start;
      qsort(all, numnotes, sizeof(int), cmpint);
      s->driftat = (int *)malloc((numnotes / DRIFTRUN + 1) * sizeof(int));
      s->driftby = (int *)malloc((numnotes / DRIFTRUN + 1) * sizeof(int));
      if ((!s->driftat) || (!s->driftby))
      {
        free(all);
        goto done;
      }
      s->numdrift = drifting ? finddrift(all, numnotes, tempo, s->driftat, s->driftby, numnotes / DRIFTRUN + 1) : 0;
      free(all);
    }
    for (c = 0; c < numon; c++) warped[c] = warptick(s->driftat, s->driftby, s->numdrift, onsets[c]);

    // Where the rows start
    {
      int hits = -1;
      for (d = 0; d < tempo; d++)
      {
        int h = 0;
        for (c = 0; c < numon; c++) if ((warped[c] >= d) && ((warped[c] - d) % tempo == 0)) h++;
        if (h > hits)
        {
          hits = h;
          phase = d;
        }
      }
    }
  }
  for (c = 0; c < numnotes; c++) notes[c].row = timetorow(warptick(s->driftat, s->driftby, s->numdrift, notes[c].start), phase, tempo);

  // Hardrestart (or not) from the gap before each note: a short gap is the
  // previous note's gate going off before this one (tied, its gatetimer
  // then); first waveform & the frames for the instrument
  for (c = 0; c < numnotes; c++)
  {
    ANOTE *n = &notes[c];
    ANOTE *p = ((c) && (notes[c-1].voice == n->voice)) ? &notes[c-1] : NULL;
    v = n->voice;
    n->gtflags = -1;
    if (n->legato) n->gtflags = 0x40;
    else if (p)
    {
      int gap = n->start - p->gateoff;
      if (gap <= 0)
      {
        // Gate off & on within one call
        n->gtflags = 0x80;
        p->tied = 1;
      }
      else if (gap <= 3)
      {
        int hr = 0;
        for (t = p->gateoff; t < n->start; t++)
        {
          if ((fr[t].ad[v] != fr[p->start].ad[v]) || (fr[t].sr[v] != fr[p->start].sr[v])) hr = 1;
        }
        n->gtflags = hr ? 0 : 0x80;
        if (hr) n->hradsr = (fr[n->start - 1].ad[v] << 8) | fr[n->start - 1].sr[v];
        p->tied = 1;
      }
    }

    if (n->legato)
    {
      n->firstwave = 0;
      n->pstart = n->start + 1;
    }
    else
    {
      unsigned char c0 = fr[n->start].ctrl[v];
      if ((c0 & 0x08) || (!(c0 & 0xf0)))
      {
        // Test bit / no waveform on the first frame, as GoatTracker's
        n->firstwave = (c0 | 0x01) & 0x0f;
        n->pstart = n->start + 1;
      }
      else
      {
        n->firstwave = 0x09;
        n->pstart = n->start;
      }
    }
    if (n->pstart >= numticks) n->pstart = n->start;
  }
  {
    double tickms = 1000.0 / (((sf->info.clock == 2) ? 60 : 50) * s->rate);
    for (c = 0; c < numnotes; c++)
    {
      ANOTE *n = &notes[c];
      int t0 = n->pstart;
      v = n->voice;
      // A pulse sweep going on smoothly through the note start wasn't reset:
      // the instrument leaves the pulse program running
      if (n->legato) n->pulsecont = 1;
      else if ((t0 >= 2) && (t0 + 1 < numticks))
      {
        int d0 = pulsedelta(fr[t0-1].pulse[v], fr[t0-2].pulse[v]);
        int d1 = pulsedelta(fr[t0].pulse[v], fr[t0-1].pulse[v]);
        int d2 = pulsedelta(fr[t0+1].pulse[v], fr[t0].pulse[v]);
        if ((abs(d1 - d0) <= 2) && (abs(d2 - d0) <= 2)) n->pulsecont = 1;
        else if (t0 >= 3)
        {
          // Also when the sweep pauses or turns around on the first frame
          // (Rob Hubbard's players): no jump bigger than its steps
          int step = abs(pulsedelta(fr[t0-2].pulse[v], fr[t0-3].pulse[v]));
          if (abs(d0) > step) step = abs(d0);
          if ((step) && (abs(d1) <= step + 2) && (abs(d2) <= step + 2)) n->pulsecont = 1;
        }
      }
      // The gate goes off before the next row (a keyoff can't be that
      // early): the program turns it off and goes on into the release, where
      // the sound still changes (drum noise, arpeggios). The frames after the
      // gate goes off are in the program only while the release is heard;
      // so for a note ended by a keyoff (the program goes on after it) when
      // the sound changes there
      n->shortgate = (!n->tied) && (!n->legato) && (n->gateoff < n->end) &&
        (timetorow(warptick(s->driftat, s->driftby, s->numdrift, n->gateoff), phase, tempo) <= n->row);
      d = audibleend(fr, n, tickms);
      n->gplen = -1;
      if ((!n->tied) && (!n->shortgate) && (n->gateoff > n->pstart) && (n->gateoff < numticks))
      {
        const SFRAME *g = &fr[n->gateoff - 1];
        n->gplen = clampint(n->gateoff - n->pstart, 1, PROGMAX);
        for (t = n->gateoff; (t < d) && (t < numticks); t++)
        {
          if (((fr[t].ctrl[v] | 0x01) != (g->ctrl[v] | 0x01)) || (fr[t].freq[v] != g->freq[v])) break;
        }
        if ((t >= d) || (t >= numticks)) d = n->gateoff;
      }
      if (d > numticks) d = numticks;
      n->plen = clampint(d - n->pstart, 1, PROGMAX);
      if (n->gplen < 0) n->gplen = n->plen;
    }
  }
  // A pulse can go on from the previous note only when an earlier note of
  // the voice has set one: the first pulse note sets it (the song starts
  // with the pulse at 0, silent). (Pulse in the release after a keyoff
  // doesn't count: the program may leave it out)
  {
    int pulseset[MAX_CHN] = {0, 0, 0};
    for (c = 0; c < numnotes; c++)
    {
      ANOTE *n = &notes[c];
      int pulse = 0;
      if (n->legato) continue;
      for (t = n->pstart; (t < n->pstart + n->gplen) && (t < numticks); t++)
      {
        if ((fr[t].ctrl[n->voice] & 0x40) && (fr[t].ctrl[n->voice] < 0x80)) pulse = 1;
      }
      if (!pulse) continue;
      if (!pulseset[n->voice]) n->pulsecont = 0;
      pulseset[n->voice] = 1;
    }
  }
  // A note that starts a pulse sweep the next notes go on with (their
  // instruments have no pulse program): its program follows the sweep
  // through them, up to the next note that starts the pulse anew
  for (c = 0; c < numnotes; c++) notes[c].pchain = notes[c].plen;
  for (c = 0; c < numnotes; c++)
  {
    ANOTE *n = &notes[c];
    int e, end, goeson = 0;
    if ((n->legato) || (n->pulsecont)) continue;
    for (e = c + 1; (e < numnotes) && (notes[e].voice == n->voice); e++)
    {
      int pulse = 0;
      for (t = notes[e].pstart; (t < notes[e].pstart + notes[e].plen) && (t < numticks); t++)
      {
        if ((fr[t].ctrl[n->voice] & 0x40) && (fr[t].ctrl[n->voice] < 0x80)) pulse = 1;
      }
      if ((pulse) && (!notes[e].pulsecont)) break;
      if ((pulse) && (!notes[e].legato)) goeson = 1;
    }
    if (!goeson) continue;
    end = ((e < numnotes) && (notes[e].voice == n->voice)) ? notes[e].pstart : numticks;
    if (end > n->pstart + PULSECHAIN) end = n->pstart + PULSECHAIN;
    // (Up to where the pulse is set anew)
    for (t = n->pstart + n->plen; t < end; t++)
    {
      int d = pulsedelta(fr[t].pulse[n->voice], fr[t-1].pulse[n->voice]);
      if ((d > 127) || (d < -128)) end = t;
    }
    n->pchain = clampint(end - n->pstart, n->plen, PULSECHAIN);
  }

  // When the gate goes off: the player reads the next row (a keyoff, or the
  // next note when tied) the gatetimer of ticks before it, so the gatetimer
  // picks the tick within the row. The gate stays on as many ticks as in the
  // recording (it goes on at the start of the note's row), through the
  // legato notes that follow
  for (c = 0; c < numnotes; c++)
  {
    ANOTE *n = &notes[c];
    int e = c, next, rb, want;
    n->offtick = -1;
    n->offwant = 0;
    if (n->legato) continue;
    while ((e + 1 < numnotes) && (notes[e+1].voice == n->voice) && (notes[e+1].legato)) e++;
    next = ((e + 1 < numnotes) && (notes[e+1].voice == n->voice)) ? e + 1 : -1;
    // (Tied: usually the next note's row, but a keyoff when that note is
    // off the grid, further than a gatetimer can reach)
    n->offlast = (next >= 0) ? notes[next].row : 0x7fffffff;
    n->offfirst = notes[e].row + 1;
    if (n->offfirst > n->offlast) n->offfirst = n->offlast;
    // The program turns the gate off, or it stays on
    if ((n->shortgate) || (notes[e].gateoff >= numticks)) continue;
    n->offtick = phase + n->row * tempo + notes[e].gateoff - n->start;
    // The first row after it (1-tempo ticks), or 1 tick early when it's on
    // a row's start (a gatetimer is 1 to tempo-1)
    rb = (int)floor((double)(n->offtick - phase) / tempo) + 1;
    if (rb < n->offfirst) rb = n->offfirst;
    if (rb > n->offlast) rb = n->offlast;
    want = phase + rb * tempo - n->offtick;
    if ((want >= tempo) && (rb - 1 >= n->offfirst)) want -= tempo - 1;
    n->offwant = clampint(want, 1, tempo - 1);
  }
  for (c = 0; c < numnotes; c++) findvibrato(fr, numticks, s->rec.pn[notes[c].voice], &notes[c]);
  s->tempo = tempo;
  s->phase = phase;

  // A tune that stops: nothing changes for a while at the end
  {
    int fps = (sf->info.clock == 2) ? 60 : 50;
    for (t = numticks - 1; (t > 0) && (sameframe(&fr[t], &fr[t-1])); t--);
    s->stopped = (numticks - t >= (int)(STOPPEDSECS * fps * s->rate) + 1);
  }

  // The loop: where the tune starts repeating, on whole rows (the period a
  // whole number of rows, the loop starting on the first frame of a row).
  // It has to leave out at least a pattern of rows. (With the timing
  // shifting, the period in song ticks is whole rows, in the recording's it
  // may be more or less)
  s->looprow = -1;
  if (findloop)
  {
    int fps = (sf->info.clock == 2) ? 60 : 50;
    int stopped = (int)(STOPPEDSECS * fps * s->rate) + 1;
    int minsave = clampint(opt->patternlength, 16, MAX_PATTROWS) * tempo;
    int start = 0, period = 0, r;

    onsetsum = (int *)calloc(numticks + 1, sizeof(int));
    if (!onsetsum) goto done;
    for (c = 0; c < numnotes; c++) onsetsum[notes[c].start + 1]++;
    for (t = 0; t < numticks; t++) onsetsum[t + 1] += onsetsum[t];
    r = findrepeat(fr, numticks, onsetsum, s->numdrift ? 1 : tempo, minsave, stopped, &start, &period);
    if (r < 0) goto done;
    if (r)
    {
      int gstart = warptick(s->driftat, s->driftby, s->numdrift, start);
      int gperiod = warptick(s->driftat, s->driftby, s->numdrift, start + period) - gstart;
      int prows = (gperiod + tempo / 2) / tempo;
      int row = timetorow(gstart, phase, tempo), first;
      if (firsttick(row, phase, tempo) < gstart) row++;
      first = unwarptick(s->driftat, s->driftby, s->numdrift, firsttick(row, phase, tempo));
      if ((prows > 0) && (abs(gperiod - prows * tempo) <= DRIFTMAX) && (first + period <= numticks) &&
        (row + prows <= MAX_SONGLEN * MAX_PATTROWS))
      {
        // The recording after the loop (and what its notes' sound needs)
        // isn't needed
        int keep = first + period + PROGMAX + 2;
        s->looprow = row;
        s->endrow = row + prows;
        if (keep < numticks)
        {
          SFRAME *newfr = (SFRAME *)realloc(s->fr, keep * sizeof(SFRAME));
          if (newfr) s->fr = newfr;
          for (v = 0; v < MAX_CHN; v++)
          {
            int *newpn = (int *)realloc(s->rec.pn[v], (keep + 1) * sizeof(int));
            if (newpn) s->rec.pn[v] = newpn;
          }
          s->numticks = s->rec.numticks = keep;
          s->rec.fr = s->fr;
        }
      }
    }
  }
  result = 1;

done:
  s->notes = notes;
  s->numnotes = numnotes;
  free(onsets);
  free(onsetsum);
  free(warped);
  free(driftat);
  free(driftby);
  return result;
}

// The speed multiplier for play calls per frame: 1-16, 0 = 25Hz, -1 = none
static int ratemultiplier(double rate)
{
  if ((rate >= 1.0) && (rate <= 16.0) && (rate == floor(rate))) return (int)rate;
  if (fabs(rate - 0.5) < 0.02) return 0;
  return -1;
}

// The song's tempo for rows of exact ticks: one tempo, or (*funk set) a
// funktempo pair alternating tempo and *funk, half a tick finer. Returns the
// tempo, *err the relative difference
static int fittempo(double exact, int *funk, double *err)
{
  int t = clampint((int)floor(exact + 0.5), 3, 0x7f);
  int a = clampint((int)floor(exact), 3, 0x7e);
  double terr = fabs(t / exact - 1.0), ferr = fabs((a + 0.5) / exact - 1.0);
  if (ferr < terr - 1e-9)
  {
    *funk = a + 1;
    *err = ferr;
    return a;
  }
  *funk = 0;
  *err = terr;
  return t;
}

// Play calls per second
static double callhz(double rate, int ntsc)
{
  return rate * (ntsc ? NTSC_CLOCK / NTSC_FRAMECYCLES : PAL_CLOCK / PAL_FRAMECYCLES);
}

// A tune that plays at another rate than GoatTracker can (0.5 or 1-16
// calls per frame, PAL or NTSC): the speed for it. Each subtune's rows get
// the tempo that lasts as long; the instruments' programs run per call, so
// they get faster or slower. Chosen: the tempos as close as can be, the
// programs' speed as little off as can be, the SID's own PAL/NTSC if alike
static int tuneconsistent(const ASUB *subs, int numsubs, double *offset);

static void choosespeed(const ASUB *subs, int numsubs, int srcntsc, int *ntsc, int *multiplier)
{
  double best = 1e9, offset;
  // (Notes all alike off the table get a base pitch that fits them: then
  // they are on pitch either way)
  int tuned = tuneconsistent(subs, numsubs, &offset);
  int n, m, s;

  *ntsc = srcntsc;
  *multiplier = 1;
  for (n = 0; n < 2; n++)
  {
    for (m = 0; m <= 16; m++)
    {
      double gthz = callhz(m ? m : 0.5, n);
      double maxerr = 0.0, programs = 0.0, pitch = 0.0, cost;
      int count = 0;
      for (s = 0; s < numsubs; s++)
      {
        double ratio, err;
        int funk;
        if (!subs[s].ok) continue;
        ratio = gthz / callhz(subs[s].rate, srcntsc);
        fittempo(subs[s].tempo * ratio, &funk, &err);
        if (err > maxerr) maxerr = err;
        programs += fabs(log(ratio) / log(2.0));
        pitch += subs[s].pitchdist[n];
        count++;
      }
      if (!count) return;
      if (tuned) pitch = 0.0;
      // (A tune a third of a semitone off counts about as a tempo 2% off)
      cost = maxerr + 0.1 * programs / count + 0.06 * pitch / count + ((n != srcntsc) ? 0.002 : 0.0);
      if (cost < best - 1e-12)
      {
        best = cost;
        *ntsc = n;
        *multiplier = m;
      }
    }
  }
}

// Are the notes tuned alike (all about as far off equal temperament at A-4 =
// 440 Hz)? *offset gets how far, -0.5 to 0.5 semitones
static int tuneconsistent(const ASUB *subs, int numsubs, double *offset)
{
  double ssum = 0.0, csum = 0.0;
  int count = 0, s;
  for (s = 0; s < numsubs; s++)
  {
    if (!subs[s].ok) continue;
    ssum += subs[s].tunesin;
    csum += subs[s].tunecos;
    count += subs[s].tunecount;
  }
  *offset = 0.0;
  if (count < 8) return 0;
  *offset = atan2(ssum, csum) / TWOPI;
  return sqrt(ssum * ssum + csum * csum) / count >= TUNECONSISTENT;
}

// The base pitch (A-4 in Hz on the PAL clock, GoatTracker's setting) whose
// frequency table has the tune's notes when played PAL or NTSC, 0 for
// GoatTracker's own table (near enough, or no tuning fits all the notes).
// The notes keep their names: an NTSC tune tuned for its 60 Hz C64 played in
// NTSC mode gets 423.87 Hz (GoatTracker's own table is for PAL: on the NTSC
// clock it is 0.65 semitones sharp)
static double findtuning(const ASUB *subs, int numsubs, int ntsc)
{
  double offset, basepitch;
  if (!tuneconsistent(subs, numsubs, &offset)) return 0.0;
  basepitch = 440.0 * pow(2.0, offset / 12.0) * PAL_CLOCK / (ntsc ? NTSC_CLOCK : PAL_CLOCK);
  if (fabs(12.0 * log(basepitch / 440.0) / log(2.0)) < TUNEMIN) return 0.0;
  return floor(basepitch * 100.0 + 0.5) / 100.0;
}

// Empty rows for a subtune (with room to round up to whole patterns).
// Returns 0 if out of memory
static int allocgrid(ASUB *s, int totalrows)
{
  int allocrows = totalrows + 2 * MAX_PATTROWS;
  int v, d;
  for (v = 0; v < MAX_CHN; v++)
  {
    s->grid[v] = (unsigned char *)malloc(allocrows * 4);
    if (!s->grid[v]) return 0;
    for (d = 0; d < allocrows; d++)
    {
      s->grid[v][d*4] = REST;
      s->grid[v][d*4+1] = 0;
      s->grid[v][d*4+2] = 0;
      s->grid[v][d*4+3] = 0;
    }
  }
  s->totalrows = totalrows;
  return 1;
}

//
// The filter as filtertable programs: set the type, resonance & routing and
// the cutoff, then modulation steps (a sweep), looping if it goes on so. A
// program starts with a command on a row (the frame after it, as the
// player's filter runs before the rows are read) and runs until the next
// one; one is started where the one running no longer gives the recorded
// filter
//

#define MAXFROWS 16               // Rows of a filter program (with the jump)
#define FHORIZON 1024             // Frames a filter program may follow
#define FILTERTOL 3               // Cutoff difference that doesn't matter
#define FLOOPMAX 4                // Longest loop of modulation steps

typedef struct
{
  unsigned char l[MAXFROWS], r[MAXFROWS];
  int len;                        // Rows, the last one the jump (1-based target, 0 = stop)
} FPROG;

// The player's filter on one frame (gplay.c playroutine())
typedef struct
{
  FPROG prog;
  int ptr;                        // 1-based row, 0 = stopped
  int time;
  unsigned char cutoff, type, ctrl;
} FSTATE;

static void fstep(FSTATE *f)
{
  const FPROG *p = &f->prog;
  if ((f->ptr <= 0) || (f->ptr > p->len)) return;
  if (p->l[f->ptr-1] == 0xff)
  {
    f->ptr = p->r[f->ptr-1];
    if ((f->ptr <= 0) || (f->ptr > p->len)) return;
  }
  if (!f->time)
  {
    if (p->l[f->ptr-1] >= 0x80)
    {
      f->type = p->l[f->ptr-1] & 0x70;
      f->ctrl = p->r[f->ptr-1];
      f->ptr++;
      if ((f->ptr <= p->len) && (p->l[f->ptr-1] == 0x00))
      {
        f->cutoff = p->r[f->ptr-1];
        f->ptr++;
      }
    }
    else
    {
      if (p->l[f->ptr-1]) f->time = p->l[f->ptr-1];
      else
      {
        f->cutoff = p->r[f->ptr-1];
        f->ptr++;
      }
    }
  }
  if (f->time)
  {
    f->cutoff += p->r[f->ptr-1];
    f->time--;
    if (!f->time) f->ptr++;
  }
}

// Does it give the frame's filter? (Which voices go through it, its type,
// resonance, cutoff; none: the rest doesn't matter)
static int fsame(const FSTATE *f, const SFRAME *fr)
{
  if ((!(fr->resfilt & 0x07)) && (!(f->ctrl & 0x07))) return 1;
  if ((f->ctrl != fr->resfilt) || (f->type != (fr->modevol & 0x70))) return 0;
  return abs((int)f->cutoff - (int)fr->cutoff) <= FILTERTOL;
}

// A filter program for the recording's filter from frame from on (while its
// type, resonance & routing stay the same)
static void filterprogram(const SFRAME *fr, int from, int numticks, FPROG *p)
{
  int rd[FHORIZON], rn[FHORIZON];
  int end, m = 0, k, a, per, bestframes = 0, besta = -1, bestp = 0;

  for (end = from + 1; (end < numticks) && (end - from < FHORIZON) && (fr[end].resfilt == fr[from].resfilt) &&
    ((fr[end].modevol & 0x70) == (fr[from].modevol & 0x70)); end++);
  // The cutoff changes per frame, as runs of the same change (127 frames a
  // row at most)
  for (k = from + 1; k < end; k++)
  {
    int d = (fr[k].cutoff - fr[k-1].cutoff) & 0xff;
    if ((m) && (rd[m-1] == d) && (rn[m-1] < 0x7f)) rn[m-1]++;
    else
    {
      rd[m] = d;
      rn[m++] = 1;
    }
  }
  // Holding the cutoff at the end needs no row
  while ((m) && (!rd[m-1])) m--;
  // Without a loop: up to where the cutoff holds, as far as it goes (then
  // another program takes over; one for a sweep that comes again is the
  // same). With a loop of up to FLOOPMAX runs (seen once again at least):
  // as far as it goes on so. The one that gives the most frames
  for (k = 0; (k < m) && (k < MAXFROWS - 3); k++)
  {
    if ((!rd[k]) && (rn[k] >= 2)) break;
    bestframes += rn[k];
  }
  for (a = 0; (a < m) && (a + 4 <= MAXFROWS); a++)
  {
    int pre = 0;
    for (k = 0; k < a; k++) pre += rn[k];
    for (per = 1; (per <= FLOOPMAX) && (a + per + 3 <= MAXFROWS) && (a + 2 * per <= m); per++)
    {
      int i, frames = pre;
      for (i = a; i < m; i++)
      {
        int j = a + (i - a) % per;
        if (rd[i] != rd[j]) break;
        if (rn[i] != rn[j])
        {
          // (The last run may be cut short)
          if (rn[i] < rn[j]) frames += rn[i];
          break;
        }
        frames += rn[i];
      }
      if ((i < a + 2 * per) || (frames <= bestframes)) continue;
      bestframes = frames;
      besta = a;
      bestp = per;
    }
  }
  p->l[0] = 0x80 | (fr[from].modevol & 0x70);
  p->r[0] = fr[from].resfilt;
  p->l[1] = 0x00;
  p->r[1] = fr[from].cutoff;
  p->len = 2;
  if (besta >= 0)
  {
    for (k = 0; k < besta + bestp; k++)
    {
      p->l[p->len] = rn[k];
      p->r[p->len++] = rd[k];
    }
    p->l[p->len] = 0xff;
    p->r[p->len] = 3 + besta;
    p->len++;
    return;
  }
  for (k = 0; (k < m) && (p->len < MAXFROWS - 1); k++)
  {
    if ((!rd[k]) && (rn[k] >= 2)) break;
    p->l[p->len] = rn[k];
    p->r[p->len++] = rd[k];
  }
  p->l[p->len] = 0xff;
  p->r[p->len] = 0;
  p->len++;
}

// Puts a subtune's notes on the rows, with the tempo, filter & volume
// commands (for the song played at the speed multiplier given, -1 = not
// known). Returns 0 if out of memory
static int buildgrid(ASUB *s, const AINSTR *ins, const APROGS *pg, SNGDATA *sng, int multiplier)
{
  const SFRAME *fr = s->fr;
  ANOTE *notes = s->notes;
  unsigned char **grid = s->grid;
  int *owner[MAX_CHN] = {NULL, NULL, NULL};
  int numnotes = s->numnotes, numticks = s->numticks;
  int tempo = s->tempo, phase = s->phase;
  int totalrows = 1, ok = 0, v, c, d, t, k;

  if (s->looprow >= 0)
  {
    // Up to the end of the loop: after it the tune repeats
    totalrows = s->endrow;
  }
  else
  {
    // (Room for the last keyoffs: a gatetimer turns the gate off before the
    // keyoff's row, which can be up to 2 rows after the gate went off)
    for (c = 0; c < numnotes; c++)
    {
      int offrow = timetorow(warptick(s->driftat, s->driftby, s->numdrift, notes[c].gateoff), phase, tempo);
      if (notes[c].row + 2 > totalrows) totalrows = notes[c].row + 2;
      if (offrow + 3 > totalrows) totalrows = offrow + 3;
    }
    if (totalrows > MAX_SONGLEN * MAX_PATTROWS)
    {
      totalrows = MAX_SONGLEN * MAX_PATTROWS;
      s->truncated = 1;
    }
    // A tune that stops ends so: the orderlist loops on an empty row after
    // it (it would start over otherwise)
    else if (s->stopped)
    {
      s->looprow = totalrows;
      s->endrow = totalrows + 1;
      totalrows++;
    }
  }
  if (!allocgrid(s, totalrows)) goto done;
  for (c = 0; c < numnotes; c++) if (notes[c].row < totalrows) s->usednotes++;
  for (v = 0; v < MAX_CHN; v++)
  {
    owner[v] = (int *)malloc(totalrows * sizeof(int));
    if (!owner[v]) goto done;
    for (d = 0; d < totalrows; d++) owner[v][d] = -1;
  }

  for (c = 0; c < numnotes; c++)
  {
    ANOTE *n = &notes[c];
    int r = n->row;
    if (r >= totalrows) continue;
    if (owner[n->voice][r] >= 0)
    {
      // Two notes on one row: the longer one stays
      ANOTE *o = &notes[owner[n->voice][r]];
      s->dropped++;
      if (o->end - o->start >= n->end - n->start) continue;
    }
    owner[n->voice][r] = c;
    grid[n->voice][r*4] = FIRSTNOTE + n->base;
    grid[n->voice][r*4+2] = 0;
    grid[n->voice][r*4+3] = 0;
    if (n->instr < 0)
    {
      // Legato: toneportamento to the note, speed 0 = right away
      grid[n->voice][r*4+1] = 0;
      grid[n->voice][r*4+2] = CMD_TONEPORTA;
      // (Per player call: on another speed, per call of the song's)
      grid[n->voice][r*4+3] = speedrow(sng, (int)(n->glide / s->tickratio + 0.5));
      continue;
    }
    grid[n->voice][r*4+1] = ins[n->instr].number;
    // The instrument's ADSR may differ (when simplified): set it
    if (fr[n->start].sr[n->voice] != ins[n->instr].sr)
    {
      grid[n->voice][r*4+2] = CMD_SETSR;
      grid[n->voice][r*4+3] = fr[n->start].sr[n->voice];
    }
    else if (fr[n->start].ad[n->voice] != ins[n->instr].ad)
    {
      grid[n->voice][r*4+2] = CMD_SETAD;
      grid[n->voice][r*4+3] = fr[n->start].ad[n->voice];
    }
    // A wave or pulse program of its own (the rest as the instrument's)
    else if (n->ovwave >= 0)
    {
      const AWAVE *w = &pg->wave[n->ovwave];
      int row = wavetotable(sng, w->l, w->r, w->len, w->loop);
      if (row)
      {
        grid[n->voice][r*4+2] = CMD_SETWAVEPTR;
        grid[n->voice][r*4+3] = row;
      }
    }
    else if (n->ovpulse >= 0)
    {
      const APULSE *pp = &pg->pulse[n->ovpulse];
      int row = addprogram(sng, PTBL, pp->l, pp->r, pp->len);
      if (row)
      {
        grid[n->voice][r*4+2] = CMD_SETPULSEPTR;
        grid[n->voice][r*4+3] = row;
      }
    }
  }
  // Keyoffs: on the row (before the next note's) that the instrument's
  // gatetimer turns the gate off nearest to when it went off. Not when the
  // next note's row does it, or the instrument's program
  for (c = 0; c < numnotes; c++)
  {
    ANOTE *n = &notes[c];
    int offrow, last, r;
    if ((n->legato) || (n->instr < 0) || (n->row >= totalrows) || (owner[n->voice][n->row] != c)) continue;
    if (n->offtick < 0)
    {
      // A short gate the instrument's program doesn't turn off (simplified):
      // the first row after
      if ((!n->shortgate) || ((n->ovwave >= 0) ? pg->wave[n->ovwave].gateoff : ins[n->instr].gateoff)) continue;
      offrow = n->row + 1;
      last = ((c + 1 < numnotes) && (notes[c+1].voice == n->voice)) ? notes[c+1].row : totalrows;
    }
    else
    {
      double gt = clampint(ins[n->instr].gatetimer & 0x3f, 1, n->gtmax) / s->tickratio;
      double best = 1e9;
      last = (n->offlast < totalrows) ? n->offlast : totalrows;
      offrow = last;
      for (r = n->offfirst; r <= last; r++)
      {
        double err = fabs(phase + r * tempo - gt - n->offtick);
        if (err < best)
        {
          best = err;
          offrow = r;
        }
        if (phase + r * tempo - gt > n->offtick) break;
      }
    }
    if ((offrow < last) && (offrow < totalrows) && (grid[n->voice][offrow*4] == REST))
      grid[n->voice][offrow*4] = KEYOFF;
  }

  // Tempo, filter & volume as commands, in whichever channel has room
  {
    FSTATE cur;
    const SFRAME *ff = fr;
    SFRAME *resampled = NULL;
    int numff = numticks, curvol = 15;
    int r;
    // The song's tempo (fitted to its speed, see choosespeed()), or a
    // funktempo pair from the speedtable. (The default tempo is 6 at 1x
    // speed, 6 per multiple otherwise)
    if ((s->gttempo != 6) || (s->gtfunk) || (multiplier != 1))
    {
      unsigned char fl = s->gttempo, fr2 = s->gtfunk;
      int funkrow = s->gtfunk ? addprogram(sng, STBL, &fl, &fr2, 1) : 0;
      for (v = 0; (v < MAX_CHN) && (grid[v][2]); v++);
      if (v == MAX_CHN) v = 0;
      if (funkrow)
      {
        grid[v][2] = CMD_FUNKTEMPO;
        grid[v][3] = funkrow;
      }
      else
      {
        grid[v][2] = CMD_SETTEMPO;
        grid[v][3] = s->gttempo;
      }
    }
    // (The filter runs per frame of the song: at another speed than the
    // SID's its frames are the recording's resampled; and where the timing
    // shifts, frames are left out, see finddrift())
    if ((fabs(s->tickratio - 1.0) > 1e-6) || (s->numdrift))
    {
      numff = (int)(warptick(s->driftat, s->driftby, s->numdrift, numticks) * s->tickratio) + 1;
      resampled = (SFRAME *)malloc(numff * sizeof(SFRAME));
      if (!resampled) goto done;
      for (k = 0; k < numff; k++)
        resampled[k] = fr[clampint(unwarptick(s->driftat, s->driftby, s->numdrift, (int)(k / s->tickratio)), 0, numticks - 1)];
      ff = resampled;
    }
    memset(&cur, 0, sizeof cur);
    for (r = 0; r < totalrows; r++)
    {
      int want[3], wantdata[3], numwant = 0, w, need = 0, g0, g1;
      FSTATE next;
      const SFRAME *f;
      t = phase + r * tempo;
      d = unwarptick(s->driftat, s->driftby, s->numdrift, t);
      if (d >= numticks) break;
      f = &fr[d];
      g0 = (int)floor(t * s->tickratio + 0.5);
      g1 = (int)floor((t + tempo) * s->tickratio + 0.5);
      // The filter on the frames this row's commands decide: a program of
      // its own where the one running doesn't give it (also on the loop's
      // first row: it comes round from the loop's end)
      next = cur;
      for (k = g0 + 1; (k <= g1) && (k < numff); k++)
      {
        fstep(&next);
        if (!fsame(&next, &ff[k])) need = 1;
      }
      if ((r == s->looprow) && (g0 + 1 < numff) && (ff[g0+1].resfilt & 0x07)) need = 1;
      if ((need) && (g0 + 1 < numff))
      {
        FSTATE start;
        int ptr;
        memset(&start, 0, sizeof start);
        filterprogram(ff, g0 + 1, numff, &start.prog);
        start.ptr = 1;
        ptr = addprogram(sng, FTBL, start.prog.l, start.prog.r, start.prog.len);
        if (ptr)
        {
          want[numwant] = CMD_SETFILTERPTR;
          wantdata[numwant++] = ptr;
          start.cutoff = cur.cutoff;
          start.type = cur.type;
          start.ctrl = cur.ctrl;
        }
        else
        {
          // (No room in the table: the cutoff, with the rest as it is)
          want[numwant] = CMD_SETFILTERCUTOFF;
          wantdata[numwant++] = ff[g0+1].cutoff;
          start = cur;
          start.cutoff = ff[g0+1].cutoff;
        }
        next = start;
      }
      if ((f->modevol & 0x0f) != curvol)
      {
        want[numwant] = CMD_SETMASTERVOL;
        wantdata[numwant++] = f->modevol & 0x0f;
      }
      for (w = 0; w < numwant; w++)
      {
        for (v = 0; v < MAX_CHN; v++)
        {
          if (grid[v][r*4+2]) continue;
          grid[v][r*4+2] = want[w];
          grid[v][r*4+3] = wantdata[w];
          if ((want[w] == CMD_SETFILTERPTR) || (want[w] == CMD_SETFILTERCUTOFF)) cur = next;
          if (want[w] == CMD_SETMASTERVOL) curvol = f->modevol & 0x0f;
          break;
        }
      }
      // The filter through the row's frames
      for (k = g0 + 1; (k <= g1) && (k < numff); k++) fstep(&cur);
    }
    free(resampled);
  }

  // An AD or SR command the same on two notes stays on the rows between
  // them (rests, keyoffs): the packer leaves out a command the same as the
  // row before's, and it does the same again there. Else it would be packed
  // anew on each note
  for (v = 0; v < MAX_CHN; v++)
  {
    int last = -1, from = -1, r;
    for (r = 0; r < totalrows; r++)
    {
      unsigned char *row = &grid[v][r*4];
      if (row[2])
      {
        if ((last >= 0) && (from >= 0) && (row[2] == grid[v][last*4+2]) && (row[3] == grid[v][last*4+3]) &&
          ((row[2] == CMD_SETAD) || (row[2] == CMD_SETSR)))
        {
          int k;
          for (k = from; k < r; k++)
          {
            grid[v][k*4+2] = row[2];
            grid[v][k*4+3] = row[3];
          }
        }
        last = r;
        from = r + 1;
      }
      else if ((row[0] != REST) && (row[0] != KEYOFF))
      {
        last = -1;
        from = -1;
      }
    }
  }

  // The loop goes on after its end (the rows after the grid), so that it
  // can start a bit later, on a pattern boundary
  if (s->looprow >= 0)
  {
    int len = s->endrow - s->looprow;
    for (v = 0; v < MAX_CHN; v++)
    {
      for (d = totalrows; d < totalrows + 2 * MAX_PATTROWS; d++)
        memcpy(&grid[v][d*4], &grid[v][(s->looprow + (d - s->looprow) % len) * 4], 4);
    }
  }
  ok = 1;

done:
  for (v = 0; v < MAX_CHN; v++) free(owner[v]);
  return ok;
}

// Bytes of a pattern packed by the relocator (greloc.c packpattern(), with
// effects): more than 256 and it can't pack the song
static int packedsize(const unsigned char *p, int rows)
{
  unsigned char im[MAX_PATTROWS * 4];
  int instr = 0, command = -1, databyte = -1, len = 0, size = 0, c, d;

  // The rows as the player reads them: instrument changes, commands when
  // they change, notes
  for (c = 0; (c < rows) && (c < MAX_PATTROWS); c++)
  {
    const unsigned char *r = &p[c*4];
    int changed = (r[2] != command) || (r[3] != databyte);
    if ((r[1]) && ((!c) || (r[1] != instr)))
    {
      im[len++] = INSTRCHG;
      instr = r[1];
    }
    if (changed)
    {
      command = r[2];
      databyte = r[3];
      im[len++] = ((r[0] == REST) ? FXONLY : FX) + command;
      if (command) im[len++] = databyte;
      if (r[0] != REST) im[len++] = r[0];
    }
    else im[len++] = r[0];
  }
  // Rests of a row of their own packed together (up to 64)
  for (c = 0; c < len;)
  {
    int packok = (c > 0);
    if (im[c] < FX)
    {
      size++;
      c++;
      packok = 0;
    }
    if ((im[c] >= FX) && (im[c] < FIRSTNOTE))
    {
      int fxonly = (im[c] >= FXONLY);
      size++;
      if (im[c++] & 0x0f)
      {
        size++;
        c++;
      }
      if (fxonly) continue;
      packok = 0;
    }
    if ((!packok) || (im[c] != REST))
    {
      size++;
      c++;
      continue;
    }
    for (d = c; (d < len) && (im[d] == REST) && (d - c < 64); d++);
    size++;
    c = (d - c > 1) ? d : c + 1;
  }
  return size;
}

//
// Patterns & orderlists
//
// Each channel of each subtune is cut into blocks of rows (the song before
// its loop and the loop each from their start, so that the orderlist can
// loop back) that become patterns, the same ones once: also ones that are
// the same but for their notes being transposed (the orderlist transposes
// them, -16 to +14 semitones). Then patterns that always come one after the
// other are joined, up to a length: fewer patterns, or fewer orderlist
// positions. Each channel is cut its own way. The blocks are as long as the
// pattern length asked for, shorter where that makes too many patterns; if
// it is still too many, the ends of the longest subtunes are left out
//

#define NOTRANS 0x100                     // A pattern without notes: any transpose
#define MINTRANS (TRANSDOWN - TRANSUP)    // -16
#define MAXTRANS (LOOPSONG - 1 - TRANSUP) // +14
#define MINBLOCK 8
#define MAXJOINPASSES 64

// An orderlist position of a channel: a pattern played transposed
typedef struct
{
  int pid;                        // Pattern
  int trans;                      // Transpose (NOTRANS: no notes in it)
  int rows;
  int segstart;                   // 1 = first of the song, 2 = first of its loop
} TITEM;

typedef struct
{
  unsigned char *data;            // rows * 4
  int rows;
  int lo, hi;                     // Lowest & highest note (0-92), -1 = none
  unsigned hash;                  // Of the rows with the notes from the lowest
  int next;                       // The next one in the same hash slot
  int refs;
  int number;                     // In the song, -1 = not numbered yet
} TPATT;

typedef struct
{
  TPATT *p;
  int num, max;
  int *slot;                      // Hash table: first pattern, -1 = none
  int numslots;
  TITEM *item[MAX_SONGS][MAX_CHN];
  int numitems[MAX_SONGS][MAX_CHN];
  int maxitems[MAX_SONGS][MAX_CHN];
} PLAYOUT;

static void freelayout(PLAYOUT *pl)
{
  int s, v, c;
  for (c = 0; c < pl->num; c++) free(pl->p[c].data);
  free(pl->p);
  free(pl->slot);
  for (s = 0; s < MAX_SONGS; s++)
  {
    for (v = 0; v < MAX_CHN; v++) free(pl->item[s][v]);
  }
  memset(pl, 0, sizeof(PLAYOUT));
}

// Empties a layout (keeping its memory). Returns 0 if out of memory
static int clearlayout(PLAYOUT *pl, int numslots)
{
  int s, v, c;
  for (c = 0; c < pl->num; c++) free(pl->p[c].data);
  pl->num = 0;
  if (pl->numslots != numslots)
  {
    free(pl->slot);
    pl->slot = (int *)malloc(numslots * sizeof(int));
    if (!pl->slot) return 0;
    pl->numslots = numslots;
  }
  for (c = 0; c < numslots; c++) pl->slot[c] = -1;
  for (s = 0; s < MAX_SONGS; s++)
  {
    for (v = 0; v < MAX_CHN; v++) pl->numitems[s][v] = 0;
  }
  return 1;
}

static void notespan(const unsigned char *d, int rows, int *lo, int *hi)
{
  int r;
  *lo = *hi = -1;
  for (r = 0; r < rows; r++)
  {
    int n = d[r*4];
    if ((n < FIRSTNOTE) || (n > LASTNOTE)) continue;
    n -= FIRSTNOTE;
    if ((*lo < 0) || (n < *lo)) *lo = n;
    if (n > *hi) *hi = n;
  }
}

static unsigned canonhash(const unsigned char *d, int rows, int lo)
{
  unsigned h = 2166136261u;
  int c;
  for (c = 0; c < rows * 4; c++)
  {
    unsigned b = d[c];
    if ((!(c & 3)) && (b >= FIRSTNOTE) && (b <= LASTNOTE)) b = 0x100 + b - FIRSTNOTE - lo;
    h = (h ^ b) * 16777619u;
  }
  return (h ^ (unsigned)rows) * 16777619u;
}

// The same rows, the notes transposed (a from alo, b from blo)?
static int canonsame(const unsigned char *a, int alo, const unsigned char *b, int blo, int rows)
{
  int c;
  for (c = 0; c < rows * 4; c++)
  {
    if ((!(c & 3)) && (a[c] >= FIRSTNOTE) && (a[c] <= LASTNOTE))
    {
      if ((b[c] < FIRSTNOTE) || (b[c] > LASTNOTE) || (a[c] - alo != b[c] - blo)) return 0;
    }
    else if (a[c] != b[c]) return 0;
  }
  return 1;
}

// Adds a pattern (find: the same one, transposed, if there is one). *trans
// gets the transpose that plays it as given: with base (the transpose it is
// played at itself, NOTRANS = none) it is to stay in range. Returns the
// pattern, -1 if out of memory
static int addpatt(PLAYOUT *pl, const unsigned char *d, int rows, int find, int base, int *trans)
{
  TPATT *p;
  int lo, hi, e, best = -1, besttrans = 0;
  unsigned h;

  notespan(d, rows, &lo, &hi);
  h = canonhash(d, rows, (lo < 0) ? 0 : lo);
  for (e = find ? pl->slot[h & (pl->numslots - 1)] : -1; e >= 0; e = pl->p[e].next)
  {
    int t;
    p = &pl->p[e];
    if ((p->hash != h) || (p->rows != rows) || ((p->lo < 0) != (lo < 0))) continue;
    if (!canonsame(d, lo, p->data, p->lo, rows)) continue;
    if (lo < 0)
    {
      *trans = NOTRANS;
      return e;
    }
    t = lo - p->lo;
    if (base != NOTRANS) t += base;
    if ((t < MINTRANS) || (t > MAXTRANS)) continue;
    if ((best < 0) || (abs(t) < abs(besttrans)))
    {
      best = e;
      besttrans = t;
    }
  }
  if (best >= 0)
  {
    *trans = besttrans;
    return best;
  }
  if (pl->num >= pl->max)
  {
    int newmax = pl->max ? pl->max * 2 : 256;
    TPATT *newp = (TPATT *)realloc(pl->p, newmax * sizeof(TPATT));
    if (!newp) return -1;
    pl->p = newp;
    pl->max = newmax;
  }
  p = &pl->p[pl->num];
  p->data = (unsigned char *)malloc(rows * 4);
  if (!p->data) return -1;
  memcpy(p->data, d, rows * 4);
  p->rows = rows;
  p->lo = lo;
  p->hi = hi;
  p->hash = h;
  p->refs = 0;
  p->number = -1;
  p->next = pl->slot[h & (pl->numslots - 1)];
  pl->slot[h & (pl->numslots - 1)] = pl->num;
  *trans = (lo < 0) ? NOTRANS : ((base != NOTRANS) ? base : 0);
  return pl->num++;
}

static int additem(PLAYOUT *pl, int s, int v, int pid, int trans, int rows, int segstart)
{
  TITEM *it;
  if (pl->numitems[s][v] >= pl->maxitems[s][v])
  {
    int newmax = pl->maxitems[s][v] ? pl->maxitems[s][v] * 2 : 64;
    TITEM *newitem = (TITEM *)realloc(pl->item[s][v], newmax * sizeof(TITEM));
    if (!newitem) return 0;
    pl->item[s][v] = newitem;
    pl->maxitems[s][v] = newmax;
  }
  it = &pl->item[s][v][pl->numitems[s][v]++];
  it->pid = pid;
  it->trans = trans;
  it->rows = rows;
  it->segstart = segstart;
  return 1;
}

// How a subtune is laid out: its loop (from loopstart, looplen rows, or
// none: looplen 0), and its rows kept (rows: the end left out if fewer)
typedef struct
{
  int loopstart, looplen;
  int rows, keep;
} SUBLAYOUT;

// The rows of a subtune's channel cut into blocks of len rows, as patterns
static int layoutblocks(PLAYOUT *pl, const ASUB *subs, int numsubs, const SUBLAYOUT *sl, int len)
{
  int s, v, seg;
  for (s = 0; s < numsubs; s++)
  {
    for (v = 0; v < MAX_CHN; v++)
    {
      for (seg = 0; seg < 2; seg++)
      {
        int a = seg ? sl[s].loopstart : 0;
        int b = seg ? sl[s].loopstart + sl[s].looplen : (sl[s].looplen ? sl[s].loopstart : sl[s].rows);
        int r;
        if ((seg) && (!sl[s].looplen)) break;
        if (b > sl[s].keep) b = sl[s].keep;
        for (r = a; r < b;)
        {
          int rows = (b - r < len) ? b - r : len, trans, pid;
          // (A row packs into 4 bytes at most: 64 rows always fit. A block
          // the relocator can't pack is cut in halves)
          while ((rows > 64) && (packedsize(&subs[s].grid[v][r*4], rows) > 256)) rows /= 2;
          pid = addpatt(pl, &subs[s].grid[v][r*4], rows, 1, NOTRANS, &trans);
          if (pid < 0) return 0;
          if (!additem(pl, s, v, pid, trans, rows, (r == a) ? seg + 1 : 0)) return 0;
          r += rows;
        }
      }
    }
  }
  return 1;
}

static int transdelta(const TITEM *a, const TITEM *b)
{
  if ((a->trans == NOTRANS) || (b->trans == NOTRANS)) return NOTRANS;
  return b->trans - a->trans;
}

// Joins patterns that always come one after the other (where both don't
// come elsewhere first: fewer patterns; then where one of them doesn't:
// fewer orderlist positions), up to maxrows and what the relocator can
// pack. Returns 0 if out of memory
static int joinpatterns(PLAYOUT *pl, int numsubs, int maxrows)
{
  int *occ = NULL, *fol = NULL, *foldelta = NULL, *folcount = NULL, *pre = NULL, *predelta = NULL, *precount = NULL;
  int *sel = NULL, *used = NULL;
  int pass, ok = 0;

  for (pass = 0; pass < MAXJOINPASSES; pass++)
  {
    int n = pl->num, numsel = 0, kind, s, v, i, x;
    free(occ); free(fol); free(foldelta); free(folcount); free(pre); free(predelta); free(precount); free(sel); free(used);
    occ = (int *)calloc(n, sizeof(int));
    fol = (int *)malloc(n * sizeof(int));
    foldelta = (int *)malloc(n * sizeof(int));
    folcount = (int *)calloc(n, sizeof(int));
    pre = (int *)malloc(n * sizeof(int));
    predelta = (int *)malloc(n * sizeof(int));
    precount = (int *)calloc(n, sizeof(int));
    sel = (int *)malloc(n * sizeof(int));
    used = (int *)calloc(n, sizeof(int));
    if ((!occ) || (!fol) || (!foldelta) || (!folcount) || (!pre) || (!predelta) || (!precount) || (!sel) || (!used)) goto done;
    for (x = 0; x < n; x++) fol[x] = pre[x] = sel[x] = -1;

    // What follows each pattern, what comes before it
    for (s = 0; s < numsubs; s++)
    {
      for (v = 0; v < MAX_CHN; v++)
      {
        TITEM *it = pl->item[s][v];
        int num = pl->numitems[s][v];
        for (i = 0; i < num; i++)
        {
          int y, d;
          x = it[i].pid;
          occ[x]++;
          if ((i + 1 >= num) || (it[i+1].segstart)) continue;
          y = it[i+1].pid;
          d = transdelta(&it[i], &it[i+1]);
          if (fol[x] == -1)
          {
            fol[x] = y;
            foldelta[x] = d;
          }
          else if ((fol[x] != y) || (foldelta[x] != d)) fol[x] = -2;
          folcount[x]++;
          if (pre[y] == -1)
          {
            pre[y] = x;
            predelta[y] = d;
          }
          else if ((pre[y] != x) || (predelta[y] != d)) pre[y] = -2;
          precount[y]++;
        }
      }
    }

    // Pairs to join: both always together, else one of them (not both
    // ways in one pass: each pattern in one pair at most)
    for (kind = 0; (kind < 2) && (!numsel); kind++)
    {
      for (x = 0; x < n; x++)
      {
        int y, d, both, alwaysx, alwaysy;
        if ((!occ[x]) || (fol[x] < 0) || (fol[x] == x)) continue;
        y = fol[x];
        d = foldelta[x];
        alwaysx = (folcount[x] == occ[x]);
        alwaysy = (pre[y] == x) && (predelta[y] == d) && (precount[y] == occ[y]);
        both = alwaysx && alwaysy;
        if (kind ? ((!alwaysx) && (!alwaysy)) : (!both)) continue;
        if ((used[x]) || (used[y])) continue;
        if (pl->p[x].rows + pl->p[y].rows > maxrows) continue;
        if ((d != NOTRANS) && ((pl->p[y].lo + d < 0) || (pl->p[y].hi + d > NUMNOTES - 1))) continue;
        sel[x] = y;
        used[x] = used[y] = 1;
        numsel++;
      }
      // (Pairs where y always comes after x, x also elsewhere)
      if (kind)
      {
        int y;
        for (y = 0; y < n; y++)
        {
          int d;
          if ((!occ[y]) || (pre[y] < 0) || (pre[y] == y) || (precount[y] != occ[y])) continue;
          x = pre[y];
          d = predelta[y];
          if ((used[x]) || (used[y])) continue;
          if (pl->p[x].rows + pl->p[y].rows > maxrows) continue;
          if ((d != NOTRANS) && ((pl->p[y].lo + d < 0) || (pl->p[y].hi + d > NUMNOTES - 1))) continue;
          sel[x] = y;
          foldelta[x] = d;
          used[x] = used[y] = 1;
          numsel++;
        }
      }
    }
    if (!numsel) break;

    // The joined patterns (that the relocator can pack), and where they go
    {
      int *joined = (int *)malloc(n * sizeof(int));
      int made = 0;
      if (!joined) goto done;
      for (x = 0; x < n; x++)
      {
        unsigned char buf[MAX_PATTROWS * 4];
        int y = sel[x], d, rx, ry, r, trans, z;
        joined[x] = -1;
        if (y < 0) continue;
        d = foldelta[x];
        rx = pl->p[x].rows;
        ry = pl->p[y].rows;
        memcpy(buf, pl->p[x].data, rx * 4);
        memcpy(buf + rx * 4, pl->p[y].data, ry * 4);
        // (y's notes as played after x's: both transposed as x)
        if ((d != NOTRANS) && (pl->p[x].lo >= 0))
        {
          for (r = rx; r < rx + ry; r++)
          {
            if ((buf[r*4] >= FIRSTNOTE) && (buf[r*4] <= LASTNOTE)) buf[r*4] += d;
          }
        }
        if (packedsize(buf, rx + ry) > 256) continue;
        z = addpatt(pl, buf, rx + ry, 0, NOTRANS, &trans);
        if (z < 0)
        {
          free(joined);
          goto done;
        }
        joined[x] = z;
        made++;
      }
      if (made)
      {
        for (s = 0; s < numsubs; s++)
        {
          for (v = 0; v < MAX_CHN; v++)
          {
            TITEM *it = pl->item[s][v];
            int num = pl->numitems[s][v], out = 0;
            for (i = 0; i < num; i++)
            {
              x = it[i].pid;
              if ((x < n) && (joined[x] >= 0) && (i + 1 < num) && (!it[i+1].segstart) && (it[i+1].pid == sel[x]) &&
                (transdelta(&it[i], &it[i+1]) == foldelta[x]))
              {
                TITEM j = it[i];
                j.pid = joined[x];
                if (j.trans == NOTRANS) j.trans = it[i+1].trans;
                j.rows += it[i+1].rows;
                it[out++] = j;
                i++;
              }
              else it[out++] = it[i];
            }
            pl->numitems[s][v] = out;
          }
        }
      }
      free(joined);
      if (!made) break;
    }
  }
  ok = 1;

done:
  free(occ); free(fol); free(foldelta); free(folcount); free(pre); free(predelta); free(precount); free(sel); free(used);
  return ok;
}

// The patterns again from the ones used, each only once (joined ones can be
// the same as others), counted. Returns how many, -1 if out of memory
static int finishlayout(PLAYOUT *pl, int numsubs)
{
  PLAYOUT n;
  int s, v, i, count = 0;
  memset(&n, 0, sizeof n);
  if (!clearlayout(&n, pl->numslots)) goto oom;
  for (s = 0; s < numsubs; s++)
  {
    for (v = 0; v < MAX_CHN; v++)
    {
      for (i = 0; i < pl->numitems[s][v]; i++)
      {
        const TITEM *it = &pl->item[s][v][i];
        int trans, pid = addpatt(&n, pl->p[it->pid].data, it->rows, 1, it->trans, &trans);
        if (pid < 0) goto oom;
        if (!additem(&n, s, v, pid, trans, it->rows, it->segstart)) goto oom;
        if (!n.p[pid].refs++) count++;
      }
    }
  }
  freelayout(pl);
  *pl = n;
  return count;
oom:
  freelayout(&n);
  return -1;
}

// A channel's orderlist (out NULL: only counted): transposes when they
// change (at the loop's start when not known: it comes round from the end),
// a pattern played again repeated. Returns the length, *restart the loop's
// position
static int orderlist(const PLAYOUT *pl, int s, int v, unsigned char *out, int *restart)
{
  const TITEM *it = pl->item[s][v];
  int num = pl->numitems[s][v], len = 0, cur = 0, known = 1, i = 0;
  *restart = 0;
  while (i < num)
  {
    int reps = 1;
    if (it[i].segstart == 2)
    {
      *restart = len;
      known = 0;
    }
    while ((i + reps < num) && (reps < 16) && (!it[i+reps].segstart) && (it[i+reps].pid == it[i].pid) &&
      (it[i+reps].trans == it[i].trans)) reps++;
    if ((it[i].trans != NOTRANS) && ((!known) || (cur != it[i].trans)))
    {
      if (out) out[len] = TRANSUP + it[i].trans;
      len++;
      cur = it[i].trans;
      known = 1;
    }
    if (reps > 1)
    {
      if (out) out[len] = REPEAT + reps - 1;
      len++;
    }
    if (out) out[len] = pl->p[it[i].pid].number;
    len++;
    i += reps;
  }
  return len;
}

// Lays out the subtunes with blocks of len rows, joined up to maxrows.
// Returns the patterns, -1 if out of memory; *toolong gets the first
// subtune with an orderlist too long, -1 if none
static int trylayout(PLAYOUT *pl, const ASUB *subs, int numsubs, const SUBLAYOUT *sl, int len, int maxrows, int join,
  int *toolong)
{
  int s, v, n, restart;
  if (!clearlayout(pl, 1 << 16)) return -1;
  if (!layoutblocks(pl, subs, numsubs, sl, len)) return -1;
  if ((join) && (!joinpatterns(pl, numsubs, maxrows))) return -1;
  n = finishlayout(pl, numsubs);
  if (n < 0) return -1;
  *toolong = -1;
  for (s = 0; s < numsubs; s++)
  {
    for (v = 0; v < MAX_CHN; v++)
    {
      if ((orderlist(pl, s, v, NULL, &restart) > MAX_SONGLEN) && (*toolong < 0)) *toolong = s;
    }
  }
  return n;
}

// Cuts the subtunes' rows into patterns and makes the orderlists. Returns 0
// if out of memory
static int makepatterns(ASUB *subs, int numsubs, SNGDATA *sng, int patternlength)
{
  PLAYOUT pl;
  SUBLAYOUT *sl = NULL, *bestsl = NULL;
  int tries[16][2], numtries = 0, t, best = -1, bestn = 0x7fffffff, n = 0, toolong, ok = 0, s, v, i, len;

  memset(&pl, 0, sizeof pl);
  sl = (SUBLAYOUT *)calloc(numsubs, sizeof(SUBLAYOUT));
  bestsl = (SUBLAYOUT *)calloc(numsubs, sizeof(SUBLAYOUT));
  if ((!sl) || (!bestsl)) goto done;

  // Blocks of the pattern length, then shorter ones, joined up to it; then
  // the same up to the longest pattern
  for (len = patternlength; len >= MINBLOCK; len /= 2)
  {
    tries[numtries][0] = len;
    tries[numtries++][1] = patternlength;
  }
  if (patternlength < MAX_PATTROWS)
  {
    for (len = MAX_PATTROWS; len >= MINBLOCK; len /= 2)
    {
      tries[numtries][0] = len;
      tries[numtries++][1] = MAX_PATTROWS;
    }
  }

  for (t = 0; t < numtries; t++)
  {
    len = tries[t][0];
    for (s = 0; s < numsubs; s++)
    {
      ASUB *a = &subs[s];
      sl[s].looplen = 0;
      sl[s].loopstart = 0;
      sl[s].rows = (a->totalrows + len - 1) / len * len;
      if (a->looprow >= 0)
      {
        // A tune that stops loops on a block of empty rows after its end
        if (a->stopped)
        {
          sl[s].loopstart = (a->looprow + len - 1) / len * len;
          sl[s].looplen = len;
        }
        else
        {
          sl[s].loopstart = a->looprow;
          sl[s].looplen = a->endrow - a->looprow;
        }
        sl[s].rows = sl[s].loopstart + sl[s].looplen;
      }
      sl[s].keep = sl[s].rows;
    }
    // A loop goes round, so it can start later: on the block boundary after
    // it, where that makes fewer patterns (a tune built in patterns counted
    // from its start, not from the loop)
    n = trylayout(&pl, subs, numsubs, sl, len, tries[t][1], 0, &toolong);
    if (n < 0) goto done;
    for (s = 0; s < numsubs; s++)
    {
      int was = sl[s].loopstart, m;
      if ((!sl[s].looplen) || (subs[s].stopped) || (!(was % len))) continue;
      sl[s].loopstart = (was + len - 1) / len * len;
      sl[s].rows = sl[s].keep = sl[s].loopstart + sl[s].looplen;
      m = trylayout(&pl, subs, numsubs, sl, len, tries[t][1], 0, &toolong);
      if (m < 0) goto done;
      if (m < n) n = m;
      else
      {
        sl[s].loopstart = was;
        sl[s].rows = sl[s].keep = sl[s].loopstart + sl[s].looplen;
      }
    }
    n = trylayout(&pl, subs, numsubs, sl, len, tries[t][1], 1, &toolong);
    if (n < 0) goto done;
#ifdef SID2SNG_DEBUG
    if (getenv("SID2SNG_PATT")) fprintf(stderr, "blocks %d join %d: %d patterns, orderlist too long %d\n", len, tries[t][1], n, toolong);
#endif
    // (An orderlist too long counts as too many patterns)
    if (toolong >= 0) n += MAX_PATT;
    if (n < bestn)
    {
      bestn = n;
      best = t;
      memcpy(bestsl, sl, numsubs * sizeof(SUBLAYOUT));
    }
    if (n <= MAX_PATT) break;
  }

  // Still too many: the subtune whose orderlist is too long, else the
  // longest one, gets shorter
  len = tries[best][0];
  memcpy(sl, bestsl, numsubs * sizeof(SUBLAYOUT));
  for (;;)
  {
    int cut, longest = 0;
    n = trylayout(&pl, subs, numsubs, sl, len, tries[best][1], 1, &toolong);
    if (n < 0) goto done;
#ifdef SID2SNG_DEBUG
    if (getenv("SID2SNG_PATT")) fprintf(stderr, "  cut: %d patterns, orderlist too long %d\n", n, toolong);
#endif
    if ((n <= MAX_PATT) && (toolong < 0)) break;
    if (toolong >= 0) longest = toolong;
    else
    {
      for (s = 1; s < numsubs; s++) if (sl[s].keep > sl[longest].keep) longest = s;
    }
    // (Can't happen: a block per channel & subtune always fits)
    if (sl[longest].keep <= len) break;
    cut = sl[longest].keep / ((n > MAX_PATT * 3 / 2) ? 16 : 64) / len * len;
    if (cut < len) cut = len;
    sl[longest].keep -= cut;
    subs[longest].truncated = 1;
  }

  // Numbered in order of use
  sng->numpatterns = 0;
  sng->numsongs = numsubs;
  for (s = 0; s < numsubs; s++)
  {
    for (v = 0; v < MAX_CHN; v++)
    {
      int restart, olen;
      for (i = 0; i < pl.numitems[s][v]; i++)
      {
        TPATT *p = &pl.p[pl.item[s][v][i].pid];
        if (p->number >= 0) continue;
        if (sng->numpatterns >= MAX_PATT) goto done;
        p->number = sng->numpatterns++;
        memcpy(sng->pattern[p->number], p->data, p->rows * 4);
        sng->pattlen[p->number] = p->rows;
      }
      olen = orderlist(&pl, s, v, sng->songorder[s][v], &restart);
      if (olen > MAX_SONGLEN) goto done;
      sng->songlen[s][v] = olen;
      sng->songorder[s][v][olen] = LOOPSONG;
      sng->songorder[s][v][olen + 1] = restart;
    }
  }
  ok = 1;

done:
  freelayout(&pl);
  free(sl);
  free(bestsl);
  return ok;
}

// The players start each channel on instrument 1 at the default tempo (6
// ticks a row, 6 per speed multiple): they read the first row when the tick
// counter comes to instrument 1's gatetimer, before the song's own tempo is
// set; then the rows up to the channel's first note at the subtune's tempo.
// A gatetimer longer than a row is never come to: the packed SID's player
// never reads a row (the subtune is silent) and the tracker stops the song.
// The packer numbers the instruments its own way, hardrestart ones first,
// then no-hardrestart, then legato ones, each in the song's order; so the
// first instrument of the first kind the song uses becomes instrument 1.
// That one is made instrument 1 here too, picking one whose gatetimer the
// default tempo and every subtune's (mintempo: the fastest) come to (or
// shortening its gatetimer if none has)
static void fixstartinstrument(SNGDATA *sng, int multiplier, int mintempo)
{
  int used[MAX_INSTR];
  int limit = (multiplier > 0) ? multiplier * 6 - 1 : 5;
  int kind = 3, first = 0, pick = 0, s, v, d, r, i;

  if (mintempo - 1 < limit) limit = mintempo - 1;
  if (limit < 1) limit = 1;
  memset(used, 0, sizeof used);
  for (s = 0; s < sng->numsongs; s++)
  {
    for (v = 0; v < MAX_CHN; v++)
    {
      for (d = 0; d < sng->songlen[s][v]; d++)
      {
        int p = sng->songorder[s][v][d];
        if (p >= sng->numpatterns) continue;
        for (r = 0; r < sng->pattlen[p]; r++)
        {
          int ins = sng->pattern[p][r*4+1];
          if ((ins > 0) && (ins < MAX_INSTR)) used[ins] = 1;
        }
      }
    }
  }
  for (i = 1; i < MAX_INSTR; i++)
  {
    int k;
    if (!used[i]) continue;
    k = (sng->instr[i].gatetimer & 0x40) ? 2 : ((sng->instr[i].gatetimer & 0x80) ? 1 : 0);
    if (k < kind)
    {
      kind = k;
      first = i;
      pick = 0;
    }
    if ((k == kind) && (!pick) && ((sng->instr[i].gatetimer & 0x3f) <= limit)) pick = i;
  }
  if (!first) return;
  if (!pick)
  {
    pick = first;
    sng->instr[pick].gatetimer = (sng->instr[pick].gatetimer & 0xc0) | limit;
  }
  if (pick != 1)
  {
    INSTR tmp = sng->instr[1];
    int p;
    sng->instr[1] = sng->instr[pick];
    sng->instr[pick] = tmp;
    // (Their names end in their numbers)
    for (i = 0; i < 2; i++)
    {
      INSTR *in = &sng->instr[i ? pick : 1];
      int len = 0;
      char num[4];
      while ((len < MAX_INSTRNAMELEN) && (in->name[len])) len++;
      snprintf(num, sizeof num, "%02X", i ? pick : 1);
      if ((len >= 3) && (in->name[len - 3] == ' ')) memcpy(&in->name[len - 2], num, 2);
    }
    for (p = 0; p < sng->numpatterns; p++)
    {
      for (r = 0; r < sng->pattlen[p]; r++)
      {
        unsigned char *ins = &sng->pattern[p][r*4+1];
        if (*ins == 1) *ins = (unsigned char)pick;
        else if (*ins == pick) *ins = 1;
      }
    }
  }
  if (sng->numinstr < pick) sng->numinstr = pick;
}

static int approxconvert(const SIDFILE *sf, const SID2SNG_OPTIONS *opt, SNGDATA *sng, SID2SNG_PLAYBACK *pb,
  char *report, int reportsize)
{
  ASUB *subs = NULL;
  ANOTE **order = NULL;
  AINSTR *ins = NULL;
  APROGS *progs = NULL;
  int numsubs, first, seconds, patternlength;
  int numins = 0, numgated = 0, numok = 0, totalnotes = 0;
  int usednotes = 0, dropped = 0, truncated = 0, empty = 0, looped = 0, samerate = 1;
  int srcntsc = (sf->info.clock == 2), gtntsc = (sf->info.clock == 2), multiplier = 1, scaled = 0;
  int insmintempo[MAX_INSTR];
  int level = 0, last, ok = 0, pass, s, c, d, r;
  double rate = 1.0, maxtempoerr = 0.0, minratio = 1e9, maxratio = 0.0, basepitch = 0.0;

  // One subtune, or all of them (as many as a song can have)
  if (opt->subtune)
  {
    if ((opt->subtune < 1) || (opt->subtune > sf->info.songs))
    {
      setreport(report, reportsize, "There is no subtune %d (the SID has %d)", opt->subtune, sf->info.songs);
      return 0;
    }
    first = opt->subtune;
    numsubs = 1;
  }
  else
  {
    first = 1;
    numsubs = (sf->info.songs < MAX_SONGS) ? sf->info.songs : MAX_SONGS;
  }
  seconds = clampint(opt->seconds, 1, 1800);
  patternlength = clampint(opt->patternlength, 16, MAX_PATTROWS);

  // Notes from the register values as GoatTracker's frequency table has
  // them, at first its own table played on the SID's clock (PAL or NTSC
  // mode). Then, if the speed is to be the other one, or the tune is tuned
  // otherwise (an NTSC tune for its 60 Hz C64: GoatTracker's table is for
  // PAL, in NTSC mode it is 0.65 semitones sharp), the notes again for the
  // clock it plays at, on the table of the base pitch that fits them
  clockhz = PAL_CLOCK;
  settuning(0.0);
  subs = (ASUB *)calloc(numsubs, sizeof(ASUB));
  if (!subs) goto oom;
  for (pass = 0; ; pass++)
  {
    numok = 0;
    totalnotes = 0;
    for (s = 0; s < numsubs; s++)
    {
      // Looking for the loop, it is recorded longer. Without one, the length
      // asked for (recorded again, so that it comes out the same)
      int loopsecs = (seconds > LOOPSECS) ? seconds : LOOPSECS;
      subs[s].subtune = first + s;
      subs[s].looprow = -1;
      r = opt->loops ? analysesub(sf, opt, loopsecs, 1, &subs[s]) : 0;
      if (r < 0) goto oom;
      if ((r == 0) || (subs[s].looprow < 0))
      {
        freesub(&subs[s]);
        memset(&subs[s], 0, sizeof(ASUB));
        subs[s].subtune = first + s;
        subs[s].looprow = -1;
        r = analysesub(sf, opt, seconds, 0, &subs[s]);
        if (r < 0) goto oom;
      }
      if (!r) continue;
      subs[s].ok = 1;
      numok++;
      totalnotes += subs[s].numnotes;
    }
    if (!numok) break;

    // The speed it plays at: the SID's, when GoatTracker has it. Else the
    // one that fits best, the tempos fitted to it
    for (s = 0; (s < numsubs) && (!subs[s].ok); s++);
    rate = subs[s].rate;
    for (; s < numsubs; s++) if ((subs[s].ok) && (subs[s].rate != rate)) samerate = 0;
    if (pass) break;
    gtntsc = srcntsc;
    if ((samerate) && (ratemultiplier(rate) >= 0)) multiplier = ratemultiplier(rate);
    else
    {
      scaled = 1;
      choosespeed(subs, numsubs, srcntsc, &gtntsc, &multiplier);
    }
    // On the other PAL/NTSC the SID's clock differs, another tuning has
    // other notes: the notes again, from the pitches they are to sound at
    basepitch = findtuning(subs, numsubs, gtntsc);
    if ((gtntsc == srcntsc) && (basepitch == 0.0)) break;
    for (s = 0; s < numsubs; s++)
    {
      freesub(&subs[s]);
      memset(&subs[s], 0, sizeof(ASUB));
    }
    samerate = 1;
    clockhz = (srcntsc ? NTSC_CLOCK : PAL_CLOCK) * PAL_CLOCK / (gtntsc ? NTSC_CLOCK : PAL_CLOCK);
    settuning(basepitch);
  }
  if (!numok)
  {
    // Why the start song didn't work (the others usually alike)
    s = ((numsubs > 1) && (sf->info.startsong <= numsubs)) ? sf->info.startsong - 1 : 0;
    setreport(report, reportsize, "%s", subs[s].err);
    goto done;
  }
  pb->multiplier = multiplier;
  pb->ntsc = gtntsc;
  pb->basepitch = basepitch;
  for (s = 0; s < numsubs; s++)
  {
    ASUB *a = &subs[s];
    a->gttempo = a->tempo;
    a->gtfunk = 0;
    a->tickratio = 1.0;
    if ((!a->ok) || (!scaled)) continue;
    {
      double err;
      a->tickratio = callhz(multiplier ? multiplier : 0.5, gtntsc) / callhz(a->rate, srcntsc);
      a->gttempo = fittempo(a->tempo * a->tickratio, &a->gtfunk, &err);
      if (err > maxtempoerr) maxtempoerr = err;
      if (a->tickratio < minratio) minratio = a->tickratio;
      if (a->tickratio > maxratio) maxratio = a->tickratio;
    }
  }
  // The gatetimers in the song's ticks (shorter than its rows)
  for (s = 0; s < numsubs; s++)
  {
    ASUB *a = &subs[s];
    int gtmax = ((a->gtfunk) && (a->gtfunk < a->gttempo)) ? a->gtfunk - 1 : a->gttempo - 1;
    if (!a->ok) continue;
    if (gtmax < 1) gtmax = 1;
    for (c = 0; c < a->numnotes; c++)
    {
      ANOTE *n = &a->notes[c];
      n->gtmax = gtmax;
      n->gtwant = (n->offtick >= 0) ? clampint((int)(n->offwant * a->tickratio + 0.5), 1, gtmax) : 0;
      n->gatetimer = ((n->gtflags >= 0) ? n->gtflags : 0) | (n->gtwant ? n->gtwant : clampint(2, 1, gtmax));
    }
  }

  // Instruments: notes that play alike share one, in all the subtunes. Long
  // notes first, they show more of the sound. Less detail until it all fits
  order = (ANOTE **)malloc(totalnotes * sizeof(ANOTE *));
  ins = (AINSTR *)malloc(MAX_INSTR * sizeof(AINSTR));
  progs = (APROGS *)malloc(sizeof(APROGS));
  if ((!order) || (!ins) || (!progs)) goto oom;
  for (s = 0; s < numsubs; s++)
  {
    if (!subs[s].ok) continue;
    for (c = 0; c < subs[s].numnotes; c++)
    {
      ANOTE *n = &subs[s].notes[c];
      n->instr = -1;
      n->sub = s;
      n->rec = &subs[s].rec;
      // After the loop the notes only repeat: they aren't used
      if ((subs[s].looprow >= 0) && (n->row >= subs[s].endrow)) continue;
      if (!n->legato) order[numgated++] = n;
    }
  }
  qsort(order, numgated, sizeof(ANOTE *), notecmp_len);
  for (level = 0; level < NUMLEVELS; level++)
  {
#ifdef SID2SNG_DEBUG
    if (getenv("SID2SNG_NOTES"))
    {
      AINSTR *all = (AINSTR *)malloc((numgated + 1) * sizeof(AINSTR));
      if (all)
      {
        fprintf(stderr, "level %d: %d instruments needed, %d with commands\n", level,
          assigninstruments(order, numgated, all, numgated, progs, level, 0, 0, 1, 1),
          assigninstruments(order, numgated, all, numgated, progs, level, 0, 1, 1, 1));
        free(all);
      }
    }
#endif
    // (Each level with the pitch sweeps of long sounds first, the last
    // without; then with notes that differ from their instrument in one
    // thing, set by a command on their row; then leaving out how the sound
    // changes after a keyoff)
    last = (level == NUMLEVELS - 1);
    numins = -1;
    if (!last) numins = assigninstruments(order, numgated, ins, MAX_INSTR - 1, progs, level, 1, 0, 1, 0);
    if (numins < 0) numins = assigninstruments(order, numgated, ins, MAX_INSTR - 1, progs, level, 0, 0, 1, 0);
    if ((numins < 0) && (!last)) numins = assigninstruments(order, numgated, ins, MAX_INSTR - 1, progs, level, 1, 1, 1, 0);
    if (numins < 0) numins = assigninstruments(order, numgated, ins, MAX_INSTR - 1, progs, level, 0, 1, 1, 0);
    if ((numins < 0) && (!last)) numins = assigninstruments(order, numgated, ins, MAX_INSTR - 1, progs, level, 1, 1, 0, 0);
    if (numins < 0) numins = assigninstruments(order, numgated, ins, MAX_INSTR - 1, progs, level, 0, 1, 0, last);
    if (numins >= 0) break;
  }
#ifdef SID2SNG_DEBUG
  if (getenv("SID2SNG_NOTES"))
  {
    FILE *df = fopen(getenv("SID2SNG_NOTES"), "w");
    if (df)
    {
      for (s = 0; s < numsubs; s++)
      {
        ASUB *a = &subs[s];
        if (!a->ok) continue;
        fprintf(df, "subtune %d tempo %d phase %d level %d ticks %d loop rows %d-%d rate %.6f\n", a->subtune, a->tempo,
          a->phase, level, a->numticks, a->looprow, a->endrow, a->rate);
        for (c = 0; c < a->numnotes; c++)
          fprintf(df, "%d %d %d %d %d %d\n", a->notes[c].voice, a->notes[c].start, a->notes[c].base, a->notes[c].legato,
            a->notes[c].gateoff, a->notes[c].instr);
      }
      fclose(df);
    }
  }
#endif
  // Numbered in order of first use
  for (c = 0; c < numins; c++)
  {
    int num = 1;
    for (d = 0; d < numins; d++)
    {
      if ((ins[d].firstuse < ins[c].firstuse) || ((ins[d].firstuse == ins[c].firstuse) && (d < c))) num++;
    }
    ins[c].number = num;
  }

  // The fastest tempo each instrument plays in
  for (c = 0; c < MAX_INSTR; c++) insmintempo[c] = 0x7f;
  for (s = 0; s < numsubs; s++)
  {
    if (!subs[s].ok) continue;
    for (c = 0; c < subs[s].numnotes; c++)
    {
      int i = subs[s].notes[c].instr;
      if ((i >= 0) && (i < MAX_INSTR) && (subs[s].gttempo < insmintempo[i])) insmintempo[i] = subs[s].gttempo;
    }
  }

  // The song
  sngdata_clear(sng);
  sngtexts(sng, &sf->info);
  sng->numinstr = numins;
  for (c = 0; c < numins; c++)
  {
    AINSTR *a = &ins[c];
    INSTR *in = &sng->instr[a->number];
    char name[MAX_INSTRNAMELEN + 1];

    in->ad = a->ad;
    in->sr = a->sr;
    in->ptr[WTBL] = wavetotable(sng, a->wl, a->wr, a->wlen, a->wloop);
    if (a->plen) in->ptr[PTBL] = addprogram(sng, PTBL, a->pl, a->pr, a->plen);
    in->gatetimer = a->gatetimer;
    // The gate goes off within the row before the note: the player stops
    // the song if the timer is longer than the tempo. (Tempos fitted to
    // another speed can be shorter, and an instrument shared with a slower
    // subtune may come from there)
    if ((a->gatetimer & 0x3f) > insmintempo[c] - 1) in->gatetimer = (a->gatetimer & 0xc0) | (insmintempo[c] - 1);
    in->firstwave = a->firstwave;
    if (a->vibdelay)
    {
      unsigned char vl = a->vibleft, vr = a->vibright;
      int row = addprogram(sng, STBL, &vl, &vr, 1);
      if (row)
      {
        in->ptr[STBL] = row;
        in->vibdelay = a->vibdelay;
      }
    }
    memset(name, 0, sizeof name);
    snprintf(name, sizeof name, "%s %02X", wavename(a->wl[0]), a->number);
    memcpy(in->name, name, MAX_INSTRNAMELEN);
  }

  // The rows of each subtune (an empty pattern for the ones that don't
  // play, so the subtunes keep their numbers), then the patterns
  for (s = 0; s < numsubs; s++)
  {
    if (subs[s].ok)
    {
      if (!buildgrid(&subs[s], ins, progs, sng, pb->multiplier)) goto oom;
    }
    else if (!allocgrid(&subs[s], 1)) goto oom;
  }
  if (!makepatterns(subs, numsubs, sng, patternlength)) goto oom;
  {
    int mintempo = 0x7f;
    for (s = 0; s < numsubs; s++)
    {
      if (!subs[s].ok) continue;
      if (subs[s].gttempo < mintempo) mintempo = subs[s].gttempo;
      if ((subs[s].gtfunk) && (subs[s].gtfunk < mintempo)) mintempo = subs[s].gtfunk;
    }
    fixstartinstrument(sng, pb->multiplier, mintempo);
  }

  // The hardrestart ADSR the tune uses most (before notes that start with
  // one), so that it sounds the same in the song
  {
    int vals[64], counts[64], numvals = 0, best = -1, k;
    for (s = 0; s < numsubs; s++)
    {
      if (!subs[s].ok) continue;
      for (c = 0; c < subs[s].numnotes; c++)
      {
        int a = subs[s].notes[c].hradsr;
        if ((a < 0) || (subs[s].notes[c].instr < 0)) continue;
        for (k = 0; (k < numvals) && (vals[k] != a); k++);
        if (k == numvals)
        {
          if (numvals == 64) continue;
          vals[numvals] = a;
          counts[numvals++] = 0;
        }
        counts[k]++;
      }
    }
    for (k = 0; k < numvals; k++) if ((best < 0) || (counts[k] > counts[best])) best = k;
    pb->adparam = (best >= 0) ? vals[best] : 0x0f00;
  }
  // The player's optimizations off: pulse steps would be left out on the
  // frames the sequencer reads a row (a sweep going on through the notes
  // falls behind), vibrato & slides on a row's first frame (they would
  // fall out of step with the tune's)
  pb->optimizepulse = 0;
  pb->optimizerealtime = 0;

  for (s = 0; s < numsubs; s++)
  {
    usednotes += subs[s].usednotes;
    dropped += subs[s].dropped;
    if (subs[s].truncated) truncated++;
    if (!subs[s].ok) empty++;
    if ((subs[s].ok) && (subs[s].looprow >= 0) && (!subs[s].stopped)) looped++;
  }

  if (numsubs == 1)
  {
    setreport(report, reportsize, "Approximate: subtune %d, %d notes, %d instruments, tempo %d",
      subs[0].subtune, usednotes - dropped, numins, subs[0].gttempo);
    if (subs[0].gtfunk) addreport(report, reportsize, "/%d (funktempo)", subs[0].gtfunk);
    addreport(report, reportsize, ", %d patterns", sng->numpatterns);
    if (looped) addreport(report, reportsize, ", loops where it repeats");
  }
  else
  {
    int mintempo = 0x7f, maxtempo = 0;
    for (s = 0; s < numsubs; s++)
    {
      int top;
      if (!subs[s].ok) continue;
      top = subs[s].gtfunk ? subs[s].gtfunk : subs[s].gttempo;
      if (subs[s].gttempo < mintempo) mintempo = subs[s].gttempo;
      if (top > maxtempo) maxtempo = top;
    }
    setreport(report, reportsize, "Approximate: %d subtunes, %d notes, %d instruments, tempo %d",
      numsubs, usednotes - dropped, numins, mintempo);
    if (maxtempo != mintempo) addreport(report, reportsize, "-%d", maxtempo);
    addreport(report, reportsize, ", %d patterns", sng->numpatterns);
    if (looped) addreport(report, reportsize, ", %d loop%s where %s", looped, (looped > 1) ? "" : "s",
      (looped > 1) ? "they repeat" : "it repeats");
  }
  if (scaled)
  {
    // Played at another speed than the SID's: the tempos fitted, the
    // instruments' programs (per call) faster or slower
    if (multiplier) addreport(report, reportsize, ", played at %dx", multiplier);
    else addreport(report, reportsize, ", played at 25Hz");
    addreport(report, reportsize, " %s", gtntsc ? "NTSC" : "PAL");
    if (samerate) addreport(report, reportsize, " for its %.1f Hz player", callhz(rate, srcntsc));
    else addreport(report, reportsize, " for its players of different speeds");
    addreport(report, reportsize, " (tempos within %.1f%%", maxtempoerr * 100.0);
    if (maxratio > 1.03) addreport(report, reportsize, ", effects up to %d%% faster", (int)((maxratio - 1.0) * 100.0 + 0.5));
    if (minratio < 0.97) addreport(report, reportsize, ", effects up to %d%% slower", (int)((1.0 - minratio) * 100.0 + 0.5));
    addreport(report, reportsize, ")");
  }
  else
  {
    if (pb->multiplier > 1) addreport(report, reportsize, ", %dx speed", pb->multiplier);
    else if (pb->multiplier == 0) addreport(report, reportsize, ", 25Hz (speed multiplier 0)");
    if (pb->ntsc) addreport(report, reportsize, ", NTSC");
  }
  if (basepitch > 0.0) addreport(report, reportsize, ", tuned to A-4 = %.2f Hz (base pitch)", basepitch);
  if (dropped) addreport(report, reportsize, ", %d notes too close together left out", dropped);
  if (level) addreport(report, reportsize, ", instruments simplified to fit");
  if (truncated)
  {
    if (numsubs == 1) addreport(report, reportsize, ", the end left out (too long)");
    else if (truncated == numsubs) addreport(report, reportsize, ", the ends left out (too long to fit)");
    else addreport(report, reportsize, ", the end of %d subtune%s left out (too long to fit)", truncated, (truncated > 1) ? "s" : "");
  }
  if (empty)
  {
    addreport(report, reportsize, ", subtune%s", (empty > 1) ? "s" : "");
    for (s = 0, c = 0; s < numsubs; s++)
    {
      if (subs[s].ok) continue;
      addreport(report, reportsize, "%s %d", c ? "," : "", subs[s].subtune);
      c++;
    }
    addreport(report, reportsize, " left empty (no notes, or %s)", (empty > 1) ? "they don't play" : "it doesn't play");
  }
  if ((!opt->subtune) && (sf->info.songs > numsubs))
    addreport(report, reportsize, ", subtunes %d-%d left out (a song has at most %d)", numsubs + 1, sf->info.songs, MAX_SONGS);
  ok = 1;
  goto done;

oom:
  setreport(report, reportsize, "Out of memory");
done:
  if (subs)
  {
    for (s = 0; s < numsubs; s++) freesub(&subs[s]);
    free(subs);
  }
  free(order);
  free(ins);
  free(progs);
  return ok;
}

int sid2sng(const char *sidpath, const char *sngpath, const SID2SNG_OPTIONS *opt, char *report, int reportsize)
{
  return sid2sng_play(sidpath, sngpath, opt, NULL, report, reportsize);
}

int sid2sng_play(const char *sidpath, const char *sngpath, const SID2SNG_OPTIONS *opt, SID2SNG_PLAYBACK *playback,
  char *report, int reportsize)
{
  SID2SNG_OPTIONS o;
  SID2SNG_PLAYBACK pb;
  SIDFILE sf;
  SNGDATA *sng = NULL;
  unsigned char *buf;
  long size;
  int ok = 0;
  char exacterr[256];

  if (opt) o = *opt;
  else sid2sng_defaults(&o);
  pb.multiplier = -1;
  pb.ntsc = 0;
  pb.adparam = -1;
  pb.optimizepulse = -1;
  pb.optimizerealtime = -1;
  pb.basepitch = -1.0;

  buf = readfile(sidpath, &size);
  if (!buf)
  {
    setreport(report, reportsize, "Could not read the SID file");
    return 0;
  }
  if (!parsesid(buf, size, &sf, report, reportsize)) goto done;
  pb.ntsc = (sf.info.clock == 2);
  sng = sngdata_alloc();
  if (!sng)
  {
    setreport(report, reportsize, "Out of memory");
    goto done;
  }

  exacterr[0] = 0;
  if (o.mode != SID2SNG_APPROX)
  {
    ok = exactconvert(&sf, sng, &pb, exacterr, sizeof exacterr);
    if (ok) setreport(report, reportsize, "%s", exacterr);
    else if (o.mode == SID2SNG_EXACT)
    {
      setreport(report, reportsize, "Not a GoatTracker 2 SID: %s", exacterr);
      goto done;
    }
  }
  if (!ok) ok = approxconvert(&sf, &o, sng, &pb, report, reportsize);
  if (!ok) goto done;

  if (!sngwrite(sngpath, sng))
  {
    setreport(report, reportsize, "Could not write the song file");
    ok = 0;
  }
  if ((ok) && (playback)) *playback = pb;

done:
  free(sng);
  free(buf);
  return ok;
}
