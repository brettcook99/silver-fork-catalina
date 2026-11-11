/*
 * GOATTRACKER instrument designer helpers (see gpatch.h)
 */

#include "goattrk2.h"
#include "gpatch.h"

/*
 * Table parts
 */

int patch_partlen(int t, int pos)
{
  if ((pos < 0) || (pos >= MAX_TABLELEN)) return 0;
  return gettablepartlen(t, pos);
}

int patch_partstart(int t, int ins)
{
  if ((ins < 1) || (ins >= MAX_INSTR)) return -1;
  if (!instr[ins].ptr[t]) return -1;
  return instr[ins].ptr[t] - 1;
}

int patch_freerows(int t)
{
  return MAX_TABLELEN - gettablelen(t);
}

int patch_addrows(int t, const unsigned char *l, const unsigned char *r, int rows)
{
  int start = gettablelen(t);
  int c;

  if (start + rows > MAX_TABLELEN) return 0;
  for (c = 0; c < rows; c++)
  {
    ltable[t][start + c] = l[c];
    rtable[t][start + c] = r[c];
  }
  return start + 1;
}

int patch_insertrow(int t, int pos, unsigned char l, unsigned char r, int keepstart)
{
  if ((pos < 0) || (pos >= MAX_TABLELEN)) return 0;
  if (gettablelen(t) >= MAX_TABLELEN) return 0;
  inserttable(t, pos, keepstart ? 1 : 0);
  ltable[t][pos] = l;
  rtable[t][pos] = r;
  return 1;
}

void patch_deleterow(int t, int pos)
{
  if ((pos < 0) || (pos >= MAX_TABLELEN)) return;
  deletetable(t, pos);
}

// Is a speedtable-row command? (portamento, vibrato, funktempo)
static int speedcommand(int cmd)
{
  return ((cmd >= CMD_PORTAUP) && (cmd <= CMD_VIBRATO)) || (cmd == CMD_FUNKTEMPO);
}

// Counts what points into rows from..from+len-1 of table t, apart from
// instrument skip: instruments, wavetable commands, pattern commands and
// jumps from rows outside the range. A part that starts further up without
// an FF row in between runs on into these rows, so pointers to those rows
// count too
static int references(int t, int from, int len, int skip)
{
  int lo = from + 1;
  int hi = from + len;
  int count = 0;
  int c, d;

  if (len <= 0) return 0;
  if (t != STBL)
  {
    while ((lo > 1) && (ltable[t][lo - 2] != 0xff)) lo--;
  }

  for (c = 1; c < MAX_INSTR; c++)
  {
    if (c == skip) continue;
    if ((instr[c].ptr[t] >= lo) && (instr[c].ptr[t] <= hi)) count++;
  }

  for (c = 0; c < MAX_TABLELEN; c++)
  {
    unsigned char l = ltable[WTBL][c];
    if ((l >= WAVECMD) && (l <= WAVELASTCMD))
    {
      int cmd = l & 0xf;
      int hit = (t < STBL) ? (cmd == CMD_SETWAVEPTR + t) : speedcommand(cmd);
      if ((hit) && (rtable[WTBL][c] >= lo) && (rtable[WTBL][c] <= hi)) count++;
    }
  }

  for (c = 0; c < MAX_PATT; c++)
  {
    for (d = 0; d < pattlen[c]; d++)
    {
      int cmd = pattern[c][d * 4 + 2];
      int param = pattern[c][d * 4 + 3];
      int hit = (t < STBL) ? (cmd == CMD_SETWAVEPTR + t) : speedcommand(cmd);
      if ((hit) && (param >= lo) && (param <= hi)) count++;
    }
  }

  if (t != STBL)
  {
    for (c = 0; c < MAX_TABLELEN; c++)
    {
      if ((c >= lo - 1) && (c < from + len)) continue;
      if ((ltable[t][c] == 0xff) && (rtable[t][c] >= lo) && (rtable[t][c] <= hi)) count++;
    }
  }
  return count;
}

int patch_isshared(int t, int ins)
{
  int start = patch_partstart(t, ins);
  if (start < 0) return 0;
  return references(t, start, patch_partlen(t, start), ins) > 0;
}

// Copies the part starting at row from to the end of the table, with its
// jumps into itself moved along. Returns the copy's 1-based position, 0 if
// there is no room
static int copypart(int t, int from)
{
  int len = patch_partlen(t, from);
  int dest = gettablelen(t);
  int c;

  if ((len <= 0) || (dest + len > MAX_TABLELEN)) return 0;
  for (c = 0; c < len; c++)
  {
    unsigned char l = ltable[t][from + c];
    unsigned char r = rtable[t][from + c];
    if ((t != STBL) && (l == 0xff) && (r >= from + 1) && (r <= from + len))
      r = (unsigned char)(r - from + dest);
    ltable[t][dest + c] = l;
    rtable[t][dest + c] = r;
  }
  return dest + 1;
}

int patch_unshare(int t, int ins)
{
  int start = patch_partstart(t, ins);
  int newptr;

  if (start < 0) return 1;
  newptr = copypart(t, start);
  if (!newptr) return 0;
  instr[ins].ptr[t] = (unsigned char)newptr;
  return 1;
}

int patch_setpart(int t, int ins, const unsigned char *l, const unsigned char *r, int rows)
{
  int start, len, c;

  if ((rows < 1) || (t == STBL)) return 0;

  if (patch_isshared(t, ins))
  {
    // Check for room for both the copy and the change first
    int start0 = patch_partstart(t, ins);
    int len0 = patch_partlen(t, start0);
    int extra = (rows > len0) ? rows - len0 : 0;
    if (patch_freerows(t) < len0 + extra) return 0;
    if (!patch_unshare(t, ins)) return 0;
  }

  start = patch_partstart(t, ins);
  if (start < 0)
  {
    start = gettablelen(t);
    if (start + rows > MAX_TABLELEN) return 0;
    instr[ins].ptr[t] = (unsigned char)(start + 1);
    len = rows;
  }
  else
  {
    len = patch_partlen(t, start);
    if ((rows > len) && (patch_freerows(t) < rows - len)) return 0;

    // Grow or shrink the part just before its last row
    while (len < rows)
    {
      int pos = start + len - 1;
      if (pos < start) pos = start;
      if (!patch_insertrow(t, pos, 0, 0, pos == start)) return 0;
      len++;
    }
    while (len > rows)
    {
      deletetable(t, start + len - 2 >= start ? start + len - 2 : start);
      len--;
    }
  }

  for (c = 0; c < rows; c++)
  {
    ltable[t][start + c] = l[c];
    if (l[c] == 0xff)
      rtable[t][start + c] = (unsigned char)(r[c] ? start + r[c] : 0);
    else
      rtable[t][start + c] = r[c];
  }
  return 1;
}

/*
 * Instruments
 */

int patch_emptyinstr(int i)
{
  INSTR *p = &instr[i];
  return (!p->ad) && (!p->sr) && (!p->ptr[0]) && (!p->ptr[1]) && (!p->ptr[2]) && (!p->ptr[3]) &&
    (!p->vibdelay) && (!p->name[0]);
}

int patch_findempty(int after)
{
  int c;

  for (c = 1; c < MAX_INSTR; c++)
  {
    int i = after + c;
    while (i >= MAX_INSTR) i -= MAX_INSTR - 1;
    if (i < 1) i = 1;
    if (patch_emptyinstr(i)) return i;
  }
  return 0;
}

int patch_copyinstr(int dest, int src)
{
  INSTR copy = instr[src];
  int t;

  if ((dest < 1) || (dest >= MAX_INSTR) || (dest == src)) return 0;
  // Room for both copies first, so that nothing is left half done
  for (t = WTBL; t <= PTBL; t++)
  {
    if ((copy.ptr[t]) && (patch_freerows(t) < patch_partlen(t, copy.ptr[t] - 1))) return 0;
  }
  memcpy(copy.name, instr[dest].name, MAX_INSTRNAMELEN);
  copy.ptr[FTBL] = 0;
  for (t = WTBL; t <= PTBL; t++)
  {
    if (copy.ptr[t])
    {
      int p = copypart(t, copy.ptr[t] - 1);
      if (!p) return 0;
      copy.ptr[t] = (unsigned char)p;
    }
  }
  instr[dest] = copy;
  return 1;
}

// The first row of the part for which match() is true, -1 if none
static int findrow(int t, int ins, int (*match)(unsigned char l))
{
  int start = patch_partstart(t, ins);
  int len, c;

  if (start < 0) return -1;
  len = patch_partlen(t, start);
  for (c = start; c < start + len; c++)
  {
    if (ltable[t][c] == 0xff) break;
    if (match(ltable[t][c])) return c;
  }
  return -1;
}

static int iswaverow(unsigned char l) { return (l >= 0x10) && (l < WAVECMD); }
static int issetrow(unsigned char l) { return (l >= 0x80) && (l < 0xff); }
static int iscutoffrow(unsigned char l) { return l == 0x00; }

int patch_waverow(int ins)
{
  return findrow(WTBL, ins, iswaverow);
}

int patch_needwaverow(int ins)
{
  int row = patch_waverow(ins);
  int start;

  if (row >= 0) return row;
  start = patch_partstart(WTBL, ins);
  if (start < 0)
  {
    unsigned char l[2] = { 0x41, 0xff };
    unsigned char r[2] = { 0x00, 0x00 };
    int p = patch_addrows(WTBL, l, r, 2);
    if (!p) return -1;
    instr[ins].ptr[WTBL] = (unsigned char)p;
    return p - 1;
  }
  if (!patch_insertrow(WTBL, start, 0x41, 0x80, 1)) return -1;
  return start;
}

int patch_pulserow(int ins)
{
  return findrow(PTBL, ins, issetrow);
}

int patch_needpulserow(int ins, int pw)
{
  int row = patch_pulserow(ins);
  int start;

  if (row >= 0) return row;
  pw &= 0xfff;
  start = patch_partstart(PTBL, ins);
  if (start < 0)
  {
    unsigned char l[2] = { (unsigned char)(0x80 | (pw >> 8)), 0xff };
    unsigned char r[2] = { (unsigned char)(pw & 0xff), 0x00 };
    int p = patch_addrows(PTBL, l, r, 2);
    if (!p) return -1;
    instr[ins].ptr[PTBL] = (unsigned char)p;
    return p - 1;
  }
  if (!patch_insertrow(PTBL, start, (unsigned char)(0x80 | (pw >> 8)), (unsigned char)(pw & 0xff), 1)) return -1;
  return start;
}

int patch_filterrow(int ins)
{
  return findrow(FTBL, ins, issetrow);
}

int patch_cutoffrow(int ins)
{
  return findrow(FTBL, ins, iscutoffrow);
}

int patch_needfilter(int ins, int route)
{
  int row = patch_filterrow(ins);
  int start = patch_partstart(FTBL, ins);

  route &= 7;
  if (start < 0)
  {
    unsigned char l[3] = { 0x90, 0x00, 0xff };
    unsigned char r[3] = { (unsigned char)(0x80 | route), 0x60, 0x00 };
    int p = patch_addrows(FTBL, l, r, 3);
    if (!p) return -1;
    instr[ins].ptr[FTBL] = (unsigned char)p;
    return p - 1;
  }
  if (row < 0)
  {
    if (!patch_insertrow(FTBL, start, 0x90, (unsigned char)(0x80 | route), 1)) return -1;
    row = start;
  }
  if (patch_cutoffrow(ins) < 0)
  {
    if (!patch_insertrow(FTBL, row + 1, 0x00, 0x60, 0)) return -1;
  }
  return row;
}

/*
 * Vibrato
 */

int patch_setvibrato(int ins, int l, int r)
{
  int cur = instr[ins].ptr[STBL];
  int c;

  l &= 0xff;
  r &= 0xff;
  if ((!l) && (!r))
  {
    instr[ins].ptr[STBL] = 0;
    return 1;
  }
  // The instrument's own row: change it
  if ((cur) && (!references(STBL, cur - 1, 1, ins)))
  {
    ltable[STBL][cur - 1] = (unsigned char)l;
    rtable[STBL][cur - 1] = (unsigned char)r;
    return 1;
  }
  for (c = 0; c < MAX_TABLELEN; c++)
  {
    if ((ltable[STBL][c] == l) && (rtable[STBL][c] == r))
    {
      instr[ins].ptr[STBL] = (unsigned char)(c + 1);
      return 1;
    }
  }
  for (c = 0; c < MAX_TABLELEN; c++)
  {
    if ((!ltable[STBL][c]) && (!rtable[STBL][c]))
    {
      ltable[STBL][c] = (unsigned char)l;
      rtable[STBL][c] = (unsigned char)r;
      instr[ins].ptr[STBL] = (unsigned char)(c + 1);
      return 1;
    }
  }
  return 0;
}

/*
 * Simulation (as gplay.c runs the tables)
 */

void patch_simpulse(int ins, int frames, unsigned short *out, int *rowat)
{
  int ptr = instr[ins].ptr[PTBL];
  int time = 0;
  int guard;
  unsigned pulse = 0;
  int f;

  for (f = 0; f < frames; f++)
  {
    if ((ptr) && (ptr <= MAX_TABLELEN))
    {
      // Jump (a jump onto a jump would stop the song in the player)
      for (guard = 0; (ptr) && (ltable[PTBL][ptr - 1] == 0xff) && (guard < 2); guard++)
        ptr = rtable[PTBL][ptr - 1];
      if ((ptr) && (ltable[PTBL][ptr - 1] == 0xff)) ptr = 0;

      if (ptr)
      {
        if (!time)
        {
          if (ltable[PTBL][ptr - 1] >= 0x80)
          {
            pulse = ((ltable[PTBL][ptr - 1] & 0xf) << 8) | rtable[PTBL][ptr - 1];
            ptr++;
          }
          else time = ltable[PTBL][ptr - 1];
        }
        if (time)
        {
          unsigned char speed = rtable[PTBL][ptr - 1];
          if (speed < 0x80) pulse += speed;
          else pulse += speed - 0x100;
          pulse &= 0xfff;
          time--;
          if (!time) ptr++;
        }
        if (ptr > MAX_TABLELEN) ptr = 0;
      }
    }
    else ptr = 0;
    out[f] = (unsigned short)pulse;
    if (rowat) rowat[f] = ptr ? ptr - 1 : -1;
  }
}

void patch_simfilter(int ins, int frames, unsigned char *out, int *rowat)
{
  int ptr = instr[ins].ptr[FTBL];
  int time = 0;
  int guard;
  unsigned char cutoff = 0;
  int f;

  for (f = 0; f < frames; f++)
  {
    if ((ptr) && (ptr <= MAX_TABLELEN))
    {
      for (guard = 0; (ptr) && (ltable[FTBL][ptr - 1] == 0xff) && (guard < 2); guard++)
        ptr = rtable[FTBL][ptr - 1];
      if ((ptr) && (ltable[FTBL][ptr - 1] == 0xff)) ptr = 0;

      if (ptr)
      {
        if (!time)
        {
          if (ltable[FTBL][ptr - 1] >= 0x80)
          {
            ptr++;
            if ((ptr <= MAX_TABLELEN) && (ltable[FTBL][ptr - 1] == 0x00))
            {
              cutoff = rtable[FTBL][ptr - 1];
              ptr++;
            }
          }
          else
          {
            if (ltable[FTBL][ptr - 1]) time = ltable[FTBL][ptr - 1];
            else
            {
              cutoff = rtable[FTBL][ptr - 1];
              ptr++;
            }
          }
        }
        if ((time) && (ptr <= MAX_TABLELEN))
        {
          cutoff = (unsigned char)(cutoff + rtable[FTBL][ptr - 1]);
          time--;
          if (!time) ptr++;
        }
        if (ptr > MAX_TABLELEN) ptr = 0;
      }
    }
    else ptr = 0;
    out[f] = cutoff;
    if (rowat) rowat[f] = ptr ? ptr - 1 : -1;
  }
}

/*
 * Names
 */

static void trimright(char *s)
{
  size_t len = strlen(s);
  while ((len) && (s[len - 1] == ' ')) s[--len] = 0;
}

int patch_splitname(const char *name, char *base)
{
  size_t len;

  memcpy(base, name, MAX_INSTRNAMELEN);
  base[MAX_INSTRNAMELEN - 1] = 0;
  trimright(base);
  len = strlen(base);
  if ((len >= 2) && (base[len - 2] == ':') && (base[len - 1] >= '1') && (base[len - 1] <= '3'))
  {
    int n = base[len - 1] - '0';
    base[len - 2] = 0;
    trimright(base);
    return n;
  }
  return 0;
}

int patch_findosc(const char *base, int n)
{
  char b[MAX_INSTRNAMELEN + 1];
  int c;

  if (!base[0]) return 0;
  for (c = 1; c < MAX_INSTR; c++)
  {
    if (patch_splitname(instr[c].name, b) != n) continue;
    if (!strcmp(b, base)) return c;
  }
  return 0;
}

void patch_setname(int ins, const char *base, int n)
{
  char name[MAX_INSTRNAMELEN + 8];

  if (n) snprintf(name, sizeof name, "%.*s:%d", MAX_INSTRNAMELEN - 3, base, n);
  else snprintf(name, sizeof name, "%.*s", MAX_INSTRNAMELEN - 1, base);
  memset(instr[ins].name, 0, MAX_INSTRNAMELEN);
  strncpy(instr[ins].name, name, MAX_INSTRNAMELEN - 1);
}
