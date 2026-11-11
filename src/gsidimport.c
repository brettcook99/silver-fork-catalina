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
#define MAXPULSEROWS 8
#define NUMLEVELS 11
#define STABLEPROG 12             // Longer programs: pitch drift is left out
#define STOPPEDSECS 2              // No change for this long at the end: the tune has stopped
#define LOOPSECS 600               // Recorded at least this long to find the loop

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
  int pulsecont;                  // The pulse goes on from the previous note
  int glide;                      // Legato: glide speed (frequency per frame)
  int instr;
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
} LEVEL;

// Less detail step by step until the instruments fit: the pulse first, then
// when the gate goes off, then the length of the wave program, the ADSR last
// (it's most of the sound)
static const LEVEL levels[NUMLEVELS] =
{
  {PROGMAX, 0x40, 4, 0x10, 0, 0},
  {PROGMAX, 0x100, 2, 0x40, 0, 0},
  {PROGMAX, 0x200, 1, 0x80, 0, 1},
  {16, 0x200, 1, 0x100, 0, 1},
  {16, 0x1000, 0, 0x100, 0, 1},
  {16, 0x1000, 0, 0x100, 0, 0x3f},
  {8, 0x1000, 0, 0x100, 1, 0x3f},
  {4, 0x1000, 0, 0x100, 1, 0x3f},
  {4, 0x1000, 0, 0x100, 2, 0x3f},
  {2, 0x1000, 0, 0x100, 2, 0x3f},
  {1, 0x1000, 0, 0x100, 2, 0x3f}
};

// Runs a routine. irqmode 0 = called with JSR, 1 = KERNAL IRQ ($0314)
// handler, 2 = hardware IRQ ($FFFE) handler. Returns 1 when done, -1 on a
// CPU error, -2 if it didn't finish
static int runroutine(CPU6502 *cpu, unsigned adr, int a, int irqmode, unsigned maxinstr, unsigned char *dip)
{
  unsigned n;
  unsigned char d = 0;
  unsigned char *mem = cpu->mem;

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
    int r = cpu6502_step(cpu);
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

// Records the SID registers after every play call. Returns 1 on success,
// 0 if the subtune can't be recorded (the reason in err), -1 if out of
// memory
static int record(const SIDFILE *sf, int subtune, int seconds, SFRAME **framesout, int *numticks, double *rateout,
  char *err, int errsize)
{
  CPU6502 *cpu;
  SFRAME *frames = NULL;
  const SIDINFO *i = &sf->info;
  unsigned playadr = i->playaddress;
  int irqmode = 0;
  int ntsc = (i->clock == 2);
  int fps = ntsc ? 60 : 50;
  int framecycles = ntsc ? NTSC_FRAMECYCLES : PAL_FRAMECYCLES;
  double rate = 1.0, acc = 0.0;
  int maxticks, ticks = 0, frame, r, speedbit;
  int result = 0;

  cpu = (CPU6502 *)calloc(1, sizeof(CPU6502));
  if (!cpu) return -1;
  memcpy(cpu->mem + i->loadaddress, sf->data, i->datasize);
  cpu->mem[0x01] = 0x37;
  cpu->mem[0xd41b] = 0x03;

  r = runroutine(cpu, i->initaddress, subtune - 1, 0, INIT_MAXINSTR, NULL);
  if (r == -1)
  {
    setreport(err, errsize, "The tune's init routine crashed (opcode $%02X at $%04X)", cpu->errorop, cpu->errorpc);
    goto fail;
  }

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
  if ((speedbit) || (i->rsid))
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
      const unsigned char *m = cpu->mem;
      int v;

      r = runroutine(cpu, playadr, 0, irqmode, PLAY_MAXINSTR, &f->dip);
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
      for (v = 0; v < MAX_CHN; v++)
      {
        f->freq[v] = m[0xd400 + v*7] | (m[0xd401 + v*7] << 8);
        f->pulse[v] = (m[0xd402 + v*7] | (m[0xd403 + v*7] << 8)) & 0xfff;
        f->ctrl[v] = m[0xd404 + v*7];
        f->ad[v] = m[0xd405 + v*7];
        f->sr[v] = m[0xd406 + v*7];
      }
      f->cutoff = m[0xd416];
      f->resfilt = m[0xd417];
      f->modevol = m[0xd418];
      ticks++;
    }
  }
  free(cpu);
  *framesout = frames;
  *numticks = ticks;
  return 1;

fail:
  free(frames);
  free(cpu);
  return result;
}

static double clockhz = PAL_CLOCK;

static double notesemis(unsigned freq)
{
  return 57.0 + 12.0 * log(freq * clockhz / 16777216.0 / 440.0) / log(2.0);
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
    if ((!tonal) && (rel % 12)) rel = 0x100;
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

// Pulse program from the recorded pulse values: set the start pulse, then
// modulation steps (a back-and-forth sweep loops)
static void makepulse(AINSTR *in, const int *pv, int len, const LEVEL *lv)
{
  int runlen[MAXPULSEROWS], rundelta[MAXPULSEROWS], runset[MAXPULSEROWS];
  int numruns = 0, f = 1, c;
  int p0 = (pv[0] + lv->p0quant / 2) / lv->p0quant * lv->p0quant;

  if (p0 > 0xfff) p0 = 0xfff;
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
    while ((f + c < len) && (c < 127) && (pulsedelta(pv[f+c], pv[f+c-1]) == d)) c++;
    runset[numruns] = -1;
    runlen[numruns] = c;
    rundelta[numruns] = d;
    numruns++;
    f += c;
  }
  // Holding the pulse at the end needs no row
  while ((numruns) && (runset[numruns-1] < 0) && (!rundelta[numruns-1])) numruns--;

  in->pl[0] = 0x80 | (p0 >> 8);
  in->pr[0] = p0 & 0xff;
  in->plen = 1;

  // Back and forth sweep: loop the two steps
  if ((lv->maxruns >= 2) && (numruns >= 3) && (runset[0] < 0) && (runset[1] < 0) && (runset[2] < 0) &&
      (rundelta[0]) && (rundelta[1] == -rundelta[0]) && (rundelta[2] == rundelta[0]))
  {
    in->pl[1] = runlen[0];
    in->pr[1] = rundelta[0] & 0xff;
    in->pl[2] = runlen[1];
    in->pr[2] = rundelta[1] & 0xff;
    in->pl[3] = 0xff;
    in->pr[3] = 2;
    in->plen = 4;
    return;
  }
  if (numruns > lv->maxruns) numruns = lv->maxruns;
  for (c = 0; c < numruns; c++)
  {
    if (runset[c] >= 0)
    {
      in->pl[in->plen] = 0x80 | (runset[c] >> 8);
      in->pr[in->plen] = runset[c] & 0xff;
    }
    else
    {
      in->pl[in->plen] = runlen[c];
      in->pr[in->plen] = rundelta[c] & 0xff;
    }
    in->plen++;
  }
  in->pl[in->plen] = 0xff;
  in->pr[in->plen] = 0;
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
static int noteentries(const RECORDING *rec, const ANOTE *n, int cap, unsigned char *wl, unsigned char *wr, int *pv)
{
  int f, len = n->plen, tones = 0;
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
    // Leading frames still on the previous note's frequency
    if ((keep) && (rec->fr[t].freq[n->voice] == rec->fr[n->start - 1].freq[n->voice]))
    {
      if (wr[f] != 0) wr[f] = 0x80;
    }
    else keep = 0;
  }
  // Pitch that wanders without repeating (slides) would make every note an
  // instrument of its own: keep the waveforms & absolute notes only
  if (len > STABLEPROG)
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
    len = cap;
  }
  return len;
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

// Does the instrument play like the note?
static int instrmatches(const AINSTR *in, const ANOTE *n, const SFRAME *f0, const unsigned char *wl, const unsigned char *wr,
  const int *pv, int len, int pulse, const LEVEL *lv)
{
  int f;
  int adok = (in->ad == f0->ad[n->voice]), srok = (in->sr == f0->sr[n->voice]);
  int gt = in->gatetimer & 0x3f;
  if (in->firstwave != n->firstwave) return 0;
  // How the note starts (after a rest: any way but legato)
  if ((n->gtflags >= 0) ? ((in->gatetimer & 0xc0) != n->gtflags) : (in->gatetimer & 0x40)) return 0;
  // When the gate goes off before the next row
  if (gt > n->gtmax) return 0;
  if ((n->gtwant) && (abs(gt - n->gtwant) > lv->gttol)) return 0;
  if ((!adok) && (lv->adsrflex < 2)) return 0;
  if ((!srok) && (lv->adsrflex < 1)) return 0;
  if ((!adok) && (!srok)) return 0;
  for (f = 0; f < len; f++)
  {
    int row = f;
    if (row >= in->wlen) row = (in->wloop >= 0) ? in->wloop + (f - in->wloop) % (in->wlen - in->wloop) : in->wlen - 1;
    if ((wl[f] != in->wl[row]) || (wr[f] != in->wr[row])) return 0;
  }
  if (pulse)
  {
    int sim[PROGMAX];
    if (!in->plen) return 0;
    simpulse(in, len, sim);
    for (f = 0; f < len; f++)
    {
      if (pulsediff(sim[f], pv[f]) > lv->pulsetol) return 0;
    }
  }
  return 1;
}

// Gives every note an instrument at a level of detail. Returns the number
// of instruments, or -1 if they don't fit (instruments or table rows) and
// force isn't set
static int assigninstruments(ANOTE **order, int numnotes, AINSTR *ins, int maxins, int levelnum, int force)
{
  const LEVEL *lv = &levels[levelnum];
  int numins = 0, c, d;
  int waverows = 0, pulserows = 0;

  for (c = 0; c < numnotes; c++)
  {
    ANOTE *n = order[c];
    const SFRAME *f0 = &n->rec->fr[n->start];
    unsigned char wl[PROGMAX], wr[PROGMAX];
    int pv[PROGMAX];
    int len = noteentries(n->rec, n, lv->wcap, wl, wr, pv);
    int pulse = usespulse(wl, len) && (!n->pulsecont);
    int time = notetime(n);
    AINSTR *in;

    for (d = 0; d < numins; d++)
    {
      if (instrmatches(&ins[d], n, f0, wl, wr, pv, len, pulse, lv)) break;
    }
    if (d < numins)
    {
      n->instr = d;
      if (time < ins[d].firstuse) ins[d].firstuse = time;
      continue;
    }
    if (numins >= maxins)
    {
      int best = 0, bestscore = -1;
      if (!force) return -1;
      // Out of instruments: the most alike, the ADSR first (a command can
      // set one of AD/SR)
      for (d = 0; d < numins; d++)
      {
        int score = 0;
        if (ins[d].ad == f0->ad[n->voice]) score += 16;
        if (ins[d].sr == f0->sr[n->voice]) score += 16;
        if (ins[d].gateoff == n->shortgate) score += 8;
        if ((ins[d].wl[0] & 0xf0) == (wl[0] & 0xf0)) score += 8;
        if ((ins[d].plen > 0) == (pulse > 0)) score += 4;
        if (ins[d].gatetimer == n->gatetimer) score += 1;
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
    in->wlen = findloop(wl, wr, len, &in->wloop);
    memcpy(in->wl, wl, in->wlen);
    memcpy(in->wr, wr, in->wlen);
    for (d = 0; d < in->wlen; d++)
    {
      if (!(in->wl[d] & 0x01)) in->gateoff = 1;
    }
    if (pulse) makepulse(in, pv, len, lv);
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
    for (d = 0; d < numins; d++)
    {
      if ((ins[d].wlen == in->wlen) && (ins[d].wloop == in->wloop) && (!memcmp(ins[d].wl, in->wl, in->wlen)) &&
        (!memcmp(ins[d].wr, in->wr, in->wlen))) break;
    }
    if (d == numins) waverows += in->wlen + 1;
    for (d = 0; d < numins; d++)
    {
      if ((ins[d].plen == in->plen) && (!memcmp(ins[d].pl, in->pl, in->plen)) && (!memcmp(ins[d].pr, in->pr, in->plen))) break;
    }
    if (d == numins) pulserows += in->plen;
    numins++;
    if ((!force) && ((waverows > MAX_TABLELEN) || (pulserows > MAX_TABLELEN))) return -1;
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
  int looprow, endrow;            // Loop: rows [looprow, endrow) repeat (-1 = no loop)
  int totalrows;                  // Rows in the grid
  unsigned char *grid[MAX_CHN];   // The rows, 4 bytes each
  int songlen;                    // Orderlist positions
  int restart;                    // Orderlist restart position
  int *chunkrow, *chunklen;       // First row & rows of each orderlist position
  int *chunk[MAX_CHN];            // Pattern (UPATT index) at each orderlist position
  int usednotes, dropped, truncated;
  int gttempo, gtfunk;            // Tempo in the song; gtfunk: funktempo with this second value (0 = none)
  double tickratio;               // Song player ticks per play call of the SID (1 = the same speed)
  double pitchdist[2];            // How far the notes are from the frequency table, played PAL/NTSC (semitones)
} ASUB;

static void freesub(ASUB *s)
{
  int v;
  for (v = 0; v < MAX_CHN; v++)
  {
    free(s->grid[v]);
    free(s->chunk[v]);
    free(s->rec.pn[v]);
  }
  free(s->chunkrow);
  free(s->chunklen);
  free(s->notes);
  free(s->fr);
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
static int analysesub(const SIDFILE *sf, const SID2SNG_OPTIONS *opt, int seconds, int findloop, ASUB *s)
{
  const SFRAME *fr;
  ANOTE *notes = NULL;
  int *onsets = NULL, *onsetsum = NULL;
  int numticks = 0, numnotes = 0, maxnotes = 0;
  int tempo = 6, phase = 0;
  int result = -1, v, c, d, t;

  result = record(sf, s->subtune, seconds, &s->fr, &s->numticks, &s->rate, s->err, sizeof s->err);
  if (result <= 0) return result;
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
        if ((prev < 0) || (fabs(st - prev) >= 0.5 + STICKY)) prev = clampint((int)floor(st + 0.5), 0, MAX_NOTES - 1);
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
    for (c = 0; c < numnotes; c++)
    {
      int t = (notes[c].start + 2 < numticks) ? notes[c].start + 2 : notes[c].start;
      unsigned freq = fr[t].freq[notes[c].voice];
      if (freq < 0x100) continue;
      for (k = 0; k < 2; k++)
      {
        double semi = 12.0 * log(freq * srcclock * PAL_CLOCK / (k ? NTSC_CLOCK : PAL_CLOCK) / 16777216.0 / 440.0) / log(2.0);
        s->pitchdist[k] += fabs(semi - floor(semi + 0.5));
      }
      count++;
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
  if (!onsets) goto done;
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
      // The longest row that most notes start on
      int bestt = 0;
      double bestscore = -1.0;
      for (d = 1; d <= 8; d++)
      {
        int tt = common / d;
        int p, hits = 0;
        double score;
        if ((common % d) || (tt < 3)) continue;
        for (p = 0; p < tt; p++)
        {
          int h = 0;
          for (c = 0; c < numon; c++) if ((onsets[c] - p) % tt == 0) h++;
          if (h > hits) hits = h;
        }
        score = (double)hits / numon;
        if (score >= 0.8)
        {
          bestt = tt;
          break;
        }
        if (score > bestscore + 0.05)
        {
          bestscore = score;
          bestt = tt;
        }
      }
      if (!bestt) bestt = (common < 3) ? common * ((3 + common - 1) / common) : common;
      tempo = bestt;
    }
    tempo = clampint(tempo, 3, 0x7f);

    // Where the rows start
    {
      int hits = -1;
      for (d = 0; d < tempo; d++)
      {
        int h = 0;
        for (c = 0; c < numon; c++) if ((onsets[c] >= d) && ((onsets[c] - d) % tempo == 0)) h++;
        if (h > hits)
        {
          hits = h;
          phase = d;
        }
      }
    }
  }
  for (c = 0; c < numnotes; c++) notes[c].row = timetorow(notes[c].start, phase, tempo);

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
      // gate goes off are in the program only while the release is heard
      n->shortgate = (!n->tied) && (!n->legato) && (n->gateoff < n->end) && (timetorow(n->gateoff, phase, tempo) <= n->row);
      if ((n->tied) || (n->shortgate)) d = audibleend(fr, n, tickms);
      else d = n->gateoff;
      if (d > numticks) d = numticks;
      n->plen = clampint(d - n->pstart, 1, PROGMAX);
    }
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
  s->tempo = tempo;
  s->phase = phase;

  // The loop: where the tune starts repeating, on whole rows (the period a
  // whole number of rows, the loop starting on the first frame of a row).
  // It has to leave out at least a pattern of rows
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
    r = findrepeat(fr, numticks, onsetsum, tempo, minsave, stopped, &start, &period);
    if (r < 0) goto done;
    if (r)
    {
      int row = timetorow(start, phase, tempo);
      if (firsttick(row, phase, tempo) < start) row++;
      if ((firsttick(row, phase, tempo) + period <= numticks) && (row + period / tempo <= MAX_SONGLEN * MAX_PATTROWS))
      {
        // The recording after the loop (and what its notes' sound needs)
        // isn't needed
        int keep = firsttick(row, phase, tempo) + period + PROGMAX + 2;
        s->looprow = row;
        s->endrow = row + period / tempo;
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
  return rate * (ntsc ? 60.0 : 50.0);
}

// A tune that plays at another rate than GoatTracker can (0.5 or 1-16
// calls per frame, PAL or NTSC): the speed for it. Each subtune's rows get
// the tempo that lasts as long; the instruments' programs run per call, so
// they get faster or slower. Chosen: the tempos as close as can be, the
// programs' speed as little off as can be, the SID's own PAL/NTSC if alike
static void choosespeed(const ASUB *subs, int numsubs, int srcntsc, int *ntsc, int *multiplier)
{
  double best = 1e9;
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

// Puts a subtune's notes on the rows, with the tempo, filter & volume
// commands (for the song played at the speed multiplier given, -1 = not
// known). Returns 0 if out of memory
static int buildgrid(ASUB *s, const AINSTR *ins, SNGDATA *sng, int multiplier)
{
  const SFRAME *fr = s->fr;
  ANOTE *notes = s->notes;
  unsigned char **grid = s->grid;
  int *owner[MAX_CHN] = {NULL, NULL, NULL};
  int numnotes = s->numnotes, numticks = s->numticks;
  int tempo = s->tempo, phase = s->phase;
  int totalrows = 1, ok = 0, v, c, d, t;

  if (s->looprow >= 0)
  {
    // Up to the end of the loop: after it the tune repeats
    totalrows = s->endrow;
  }
  else
  {
    for (c = 0; c < numnotes; c++)
    {
      int offrow = timetorow(notes[c].gateoff, phase, tempo);
      if (notes[c].row + 2 > totalrows) totalrows = notes[c].row + 2;
      if (offrow + 1 > totalrows) totalrows = offrow + 1;
    }
    if (totalrows > MAX_SONGLEN * MAX_PATTROWS)
    {
      totalrows = MAX_SONGLEN * MAX_PATTROWS;
      s->truncated = 1;
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
      if ((!n->shortgate) || (ins[n->instr].gateoff)) continue;
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
    int curtype = 0, curres = 0, curcut = -1, curvol = 15;
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
    for (r = 0; r < totalrows; r++)
    {
      int want[3], wantdata[3], numwant = 0, w;
      const SFRAME *f;
      t = phase + r * tempo;
      if (t >= numticks) break;
      f = &fr[t];
      if (((f->modevol & 0x70) != curtype) || (f->resfilt != curres))
      {
        unsigned char pl[3], pr[3];
        int ptr;
        pl[0] = 0x80 | (f->modevol & 0x70);
        pr[0] = f->resfilt;
        pl[1] = 0x00;
        pr[1] = f->cutoff;
        pl[2] = 0xff;
        pr[2] = 0x00;
        ptr = addprogram(sng, FTBL, pl, pr, 3);
        if (ptr)
        {
          want[numwant] = CMD_SETFILTERPTR;
          wantdata[numwant++] = ptr;
        }
      }
      else if ((int)f->cutoff != curcut)
      {
        want[numwant] = CMD_SETFILTERCUTOFF;
        wantdata[numwant++] = f->cutoff;
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
          if (want[w] == CMD_SETFILTERPTR)
          {
            curtype = f->modevol & 0x70;
            curres = f->resfilt;
            curcut = f->cutoff;
          }
          if (want[w] == CMD_SETFILTERCUTOFF) curcut = f->cutoff;
          if (want[w] == CMD_SETMASTERVOL) curvol = f->modevol & 0x0f;
          break;
        }
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

// A different pattern
typedef struct
{
  const unsigned char *data;
  int len;                        // Rows
  unsigned hash;
  int refs;                       // Orderlist positions that use it
  int number;                     // In the song, -1 = not numbered yet
} UPATT;

static unsigned patthash(const unsigned char *p, int len)
{
  unsigned h = 2166136261u;
  int c;
  for (c = 0; c < len; c++) h = (h ^ p[c]) * 16777619u;
  return h;
}

// Orderlist positions of patternlength rows (the last one filled up with
// empty rows). With a loop (loopstart >= 0) the rows before it and the loop
// are cut separately, the last pattern of each shorter if need be (the
// loop must be exact). Sets s->chunkrow/chunklen/songlen/restart
static void layoutsub(ASUB *s, int loopstart, int patternlength)
{
  int bounds[3], numseg, seg, r, n = 0;

  bounds[0] = 0;
  if (loopstart >= 0)
  {
    bounds[1] = loopstart;
    bounds[2] = loopstart + s->endrow - s->looprow;
    numseg = 2;
  }
  else
  {
    bounds[1] = s->totalrows;
    numseg = 1;
  }
  s->restart = 0;
  for (seg = 0; seg < numseg; seg++)
  {
    if (seg) s->restart = n;
    for (r = bounds[seg]; r < bounds[seg+1]; r += patternlength)
    {
      s->chunkrow[n] = r;
      s->chunklen[n] = ((numseg > 1) && (bounds[seg+1] - r < patternlength)) ? bounds[seg+1] - r : patternlength;
      n++;
    }
  }
  s->songlen = n;
}

// Room for a subtune's orderlist positions, laid out with the loop where it
// was found. Returns 0 if out of memory
static int cutsub(ASUB *s, int patternlength)
{
  int maxlen = (s->totalrows + patternlength - 1) / patternlength + 2;
  int v;

  free(s->chunkrow);
  free(s->chunklen);
  s->chunkrow = (int *)malloc(maxlen * sizeof(int));
  s->chunklen = (int *)malloc(maxlen * sizeof(int));
  if ((!s->chunkrow) || (!s->chunklen)) return 0;
  for (v = 0; v < MAX_CHN; v++)
  {
    free(s->chunk[v]);
    s->chunk[v] = (int *)malloc(maxlen * sizeof(int));
    if (!s->chunk[v]) return 0;
  }
  layoutsub(s, s->looprow, patternlength);
  return 1;
}

// Finds the different patterns in all the subtunes' orderlist positions
// (into *uout, the positions' chunk[] numbered). Returns how many, -1 if
// out of memory
static int findpatterns(ASUB *subs, int numsubs, UPATT **uout)
{
  UPATT *u;
  int *table;
  int maxchunks = 0, tablesize, numunique = 0, s, v, d, e;

  for (s = 0; s < numsubs; s++) maxchunks += subs[s].songlen * MAX_CHN;
  for (tablesize = 1; tablesize < maxchunks * 2; tablesize <<= 1);
  free(*uout);
  u = *uout = (UPATT *)malloc(maxchunks * sizeof(UPATT));
  table = (int *)malloc(tablesize * sizeof(int));
  if ((!u) || (!table))
  {
    free(table);
    return -1;
  }
  for (d = 0; d < tablesize; d++) table[d] = -1;

  for (s = 0; s < numsubs; s++)
  {
    for (v = 0; v < MAX_CHN; v++)
    {
      for (d = 0; d < subs[s].songlen; d++)
      {
        int len = subs[s].chunklen[d];
        const unsigned char *src = &subs[s].grid[v][subs[s].chunkrow[d] * 4];
        unsigned h = patthash(src, len * 4) + len * 0x9e3779b9u;
        int slot = h & (tablesize - 1);
        while ((e = table[slot]) >= 0)
        {
          if ((u[e].hash == h) && (u[e].len == len) && (!memcmp(u[e].data, src, len * 4))) break;
          slot = (slot + 1) & (tablesize - 1);
        }
        if (e < 0)
        {
          e = numunique++;
          table[slot] = e;
          u[e].data = src;
          u[e].len = len;
          u[e].hash = h;
          u[e].refs = 0;
          u[e].number = -1;
        }
        u[e].refs++;
        subs[s].chunk[v][d] = e;
      }
    }
  }
  free(table);
  return numunique;
}

// Cuts the subtunes' rows into patterns, the same ones only once, and makes
// the orderlists. If there are too many patterns, longer ones are tried,
// then the end of the longest subtune is left out, a pattern at a time.
// Returns 0 if out of memory
static int makepatterns(ASUB *subs, int numsubs, SNGDATA *sng, int patternlength)
{
  UPATT *u = NULL;
  int numunique = 0, ok = 0, s, v, d, e;

  for (;;)
  {
    int toolong = 0;

    for (s = 0; s < numsubs; s++)
    {
      if (!cutsub(&subs[s], patternlength)) goto done;
      if (subs[s].songlen > MAX_SONGLEN) toolong = 1;
    }
    numunique = findpatterns(subs, numsubs, &u);
    if (numunique < 0) goto done;

    // A loop goes round, so it can start later: on the pattern boundary
    // after it, where that makes fewer patterns (a tune built in patterns
    // counted from its start, not from the loop)
    for (s = 0; s < numsubs; s++)
    {
      int n;
      if ((subs[s].looprow < 0) || (!(subs[s].looprow % patternlength))) continue;
      layoutsub(&subs[s], (subs[s].looprow + patternlength - 1) / patternlength * patternlength, patternlength);
      n = findpatterns(subs, numsubs, &u);
      if (n < 0) goto done;
      if (n < numunique)
      {
        numunique = n;
        if (subs[s].songlen > MAX_SONGLEN) toolong = 1;
        continue;
      }
      layoutsub(&subs[s], subs[s].looprow, patternlength);
      numunique = findpatterns(subs, numsubs, &u);
      if (numunique < 0) goto done;
    }

    if ((!toolong) && (numunique <= MAX_PATT)) break;
    if (patternlength >= MAX_PATTROWS) break;
    patternlength = MAX_PATTROWS;
  }

  // Still too many: the longest subtunes get shorter
  for (;;)
  {
    int longest = 0;
    for (s = 1; s < numsubs; s++) if (subs[s].songlen > subs[longest].songlen) longest = s;
    if ((numunique <= MAX_PATT) && (subs[longest].songlen <= MAX_SONGLEN)) break;
    // (Can't happen: one pattern per channel & subtune always fits)
    if (subs[longest].songlen <= 1) break;
    d = --subs[longest].songlen;
    for (v = 0; v < MAX_CHN; v++)
    {
      if (!--u[subs[longest].chunk[v][d]].refs) numunique--;
    }
    if (subs[longest].restart >= subs[longest].songlen) subs[longest].restart = 0;
    subs[longest].truncated = 1;
  }

  // Numbered in order of use
  sng->numpatterns = 0;
  sng->numsongs = numsubs;
  for (s = 0; s < numsubs; s++)
  {
    for (v = 0; v < MAX_CHN; v++)
    {
      for (d = 0; d < subs[s].songlen; d++)
      {
        e = subs[s].chunk[v][d];
        if (u[e].number < 0)
        {
          if (sng->numpatterns >= MAX_PATT) goto done;
          u[e].number = sng->numpatterns++;
          memcpy(sng->pattern[u[e].number], u[e].data, u[e].len * 4);
          sng->pattlen[u[e].number] = u[e].len;
        }
        sng->songorder[s][v][d] = u[e].number;
      }
      sng->songlen[s][v] = subs[s].songlen;
      sng->songorder[s][v][subs[s].songlen] = LOOPSONG;
      sng->songorder[s][v][subs[s].songlen + 1] = subs[s].restart;
    }
  }
  ok = 1;

done:
  free(u);
  return ok;
}

static int approxconvert(const SIDFILE *sf, const SID2SNG_OPTIONS *opt, SNGDATA *sng, SID2SNG_PLAYBACK *pb,
  char *report, int reportsize)
{
  ASUB *subs = NULL;
  ANOTE **order = NULL;
  AINSTR *ins = NULL;
  int numsubs, first, seconds, patternlength;
  int numins = 0, numgated = 0, numok = 0, totalnotes = 0;
  int usednotes = 0, dropped = 0, truncated = 0, empty = 0, looped = 0, samerate = 1;
  int srcntsc = (sf->info.clock == 2), gtntsc = (sf->info.clock == 2), multiplier = 1, scaled = 0;
  int insmintempo[MAX_INSTR];
  int level = 0, ok = 0, pass, s, c, d, r;
  double rate = 1.0, maxtempoerr = 0.0, minratio = 1e9, maxratio = 0.0;

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

  if ((sf->info.rsid) && (!sf->info.playaddress))
  {
    setreport(report, reportsize, "This is an RSID tune: it runs from its own interrupts and needs a real C64, "
      "so it can't be recorded. A PSID version of the tune may work");
    return 0;
  }

  // Notes from the register values as GoatTracker's frequency table has
  // them. It is for PAL; an NTSC tune is to be played in NTSC mode (the same
  // table on the faster clock), so it comes out at its own pitch
  clockhz = PAL_CLOCK;
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
    // On the other PAL/NTSC the SID's clock differs: the notes again, from
    // the pitches they are to sound at
    if (gtntsc == srcntsc) break;
    for (s = 0; s < numsubs; s++)
    {
      freesub(&subs[s]);
      memset(&subs[s], 0, sizeof(ASUB));
    }
    samerate = 1;
    clockhz = (srcntsc ? NTSC_CLOCK : PAL_CLOCK) * PAL_CLOCK / (gtntsc ? NTSC_CLOCK : PAL_CLOCK);
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
  if ((!order) || (!ins)) goto oom;
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
        fprintf(stderr, "level %d: %d instruments needed\n", level, assigninstruments(order, numgated, all, numgated, level, 1));
        free(all);
      }
    }
#endif
    numins = assigninstruments(order, numgated, ins, MAX_INSTR - 1, level, level == NUMLEVELS - 1);
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
    unsigned char wl[PROGMAX + 1], wr[PROGMAX + 1];
    char name[MAX_INSTRNAMELEN + 1];

    memcpy(wl, a->wl, a->wlen);
    memcpy(wr, a->wr, a->wlen);
    wl[a->wlen] = 0xff;
    wr[a->wlen] = (a->wloop >= 0) ? a->wloop + 1 : 0;
    in->ad = a->ad;
    in->sr = a->sr;
    in->ptr[WTBL] = addprogram(sng, WTBL, wl, wr, a->wlen + 1);
    if (a->plen) in->ptr[PTBL] = addprogram(sng, PTBL, a->pl, a->pr, a->plen);
    in->gatetimer = a->gatetimer;
    // The gate goes off within the row before the note: the player stops
    // the song if the timer is longer than the tempo. (Tempos fitted to
    // another speed can be shorter, and an instrument shared with a slower
    // subtune may come from there)
    if ((a->gatetimer & 0x3f) > insmintempo[c] - 1) in->gatetimer = (a->gatetimer & 0xc0) | (insmintempo[c] - 1);
    in->firstwave = a->firstwave;
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
      if (!buildgrid(&subs[s], ins, sng, pb->multiplier)) goto oom;
    }
    else if (!allocgrid(&subs[s], 1)) goto oom;
  }
  if (!makepatterns(subs, numsubs, sng, patternlength)) goto oom;

  for (s = 0; s < numsubs; s++)
  {
    usednotes += subs[s].usednotes;
    dropped += subs[s].dropped;
    if (subs[s].truncated) truncated++;
    if (!subs[s].ok) empty++;
    if ((subs[s].ok) && (subs[s].looprow >= 0)) looped++;
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
