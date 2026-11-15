/*
 * GOATTRACKER chipsynth C64 preset (.fermatax) import
 *
 * The parameter numbers were worked out from presets that each change one
 * setting of chipsynth's Init patch (see gfermatax.h).
 */

#include "goattrk2.h"
#include "gfermatax.h"
#include "gpatch.h"

#define MAX_PARAMS 512
#define NUM_OSC 3

// chipsynth C64 Init patch values (settings missing from a preset)
#define DEF_PULSEWIDTH 2048
#define DEF_WAVEFORM 4          // pulse
#define DEF_ATTACK 0
#define DEF_DECAY 0
#define DEF_SUSTAIN 15
#define DEF_RELEASE 0
#define DEF_CUTOFF 1024
#define DEF_RESONANCE 0

typedef struct
{
  int id;
  double value;
} PARAM;

static PARAM params[MAX_PARAMS];
static int numparams;

static int hasparam(int id)
{
  int c;
  for (c = 0; c < numparams; c++)
  {
    if (params[c].id == id) return 1;
  }
  return 0;
}

static double getparam_f(int id, double def)
{
  int c;
  for (c = 0; c < numparams; c++)
  {
    if (params[c].id == id) return params[c].value;
  }
  return def;
}

static int getparam_i(int id, int def)
{
  double v = getparam_f(id, def);
  return (int)(v < 0 ? v - 0.5 : v + 0.5);
}

static int clamp(int v, int lo, int hi)
{
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

// Value of attribute name="..." inside the tag starting at tag
static int attribute(const char *tag, const char *tagend, const char *name, char *out, int outsize)
{
  size_t len = strlen(name);
  const char *p = tag;

  while ((p = strstr(p, name)) && (p < tagend))
  {
    if (((p == tag) || (p[-1] == ' ') || (p[-1] == '\t') || (p[-1] == '\n')) && (p[len] == '=') && (p[len + 1] == '"'))
    {
      const char *v = p + len + 2;
      const char *e = strchr(v, '"');
      int n;
      if ((!e) || (e > tagend)) return 0;
      n = (int)(e - v);
      if (n >= outsize) n = outsize - 1;
      memcpy(out, v, n);
      out[n] = 0;
      return 1;
    }
    p += len;
  }
  return 0;
}

// Reads the <Param id= value=> settings of the instrument slot
static int readpreset(const char *path, char *report, int reportsize)
{
  FILE *handle = fopen(path, "rb");
  char *text;
  long size;
  const char *slot, *slotend, *p;

  numparams = 0;
  if (!handle)
  {
    snprintf(report, reportsize, "Can't open the file");
    return 0;
  }
  fseek(handle, 0, SEEK_END);
  size = ftell(handle);
  fseek(handle, 0, SEEK_SET);
  if ((size <= 0) || (size > 4 * 1024 * 1024))
  {
    fclose(handle);
    snprintf(report, reportsize, "Not a chipsynth preset");
    return 0;
  }
  text = (char *)malloc(size + 1);
  if (!text)
  {
    fclose(handle);
    snprintf(report, reportsize, "Out of memory");
    return 0;
  }
  size = (long)fread(text, 1, size, handle);
  fclose(handle);
  text[size] = 0;

  // The instrument slot; the effect slots have parameters of their own
  slot = strstr(text, "<Slot ");
  slotend = slot ? strstr(slot, "</Slot>") : NULL;
  if ((!strstr(text, "<Save")) || (!slot) || (!slotend))
  {
    free(text);
    snprintf(report, reportsize, "Not a chipsynth preset (no instrument slot found)");
    return 0;
  }
  {
    char name[32];
    const char *tagend = strchr(slot, '>');
    if ((tagend) && (attribute(slot + 1, tagend, "name", name, sizeof name)) && (strcmp(name, "C64")))
    {
      free(text);
      snprintf(report, reportsize, "This is a preset for chipsynth %s, not chipsynth C64", name);
      return 0;
    }
  }

  p = slot;
  while ((p = strstr(p, "<Param ")) && (p < slotend) && (numparams < MAX_PARAMS))
  {
    const char *tagend = strchr(p, '>');
    char id[16], value[64];
    if (!tagend) break;
    if ((attribute(p + 1, tagend, "id", id, sizeof id)) && (attribute(p + 1, tagend, "value", value, sizeof value)))
    {
      params[numparams].id = atoi(id);
      params[numparams].value = atof(value);
      numparams++;
    }
    p = tagend;
  }
  free(text);
  return 1;
}

// Is oscillator n on and making a sound? (1 is on by default, 2 and 3 off;
// without a waveform an oscillator is silent)
static int oscon(int n)
{
  if (!getparam_i(512 + 16 * n, n == 0 ? 1 : 0)) return 0;
  return (getparam_i(768 + 16 * n + 2, DEF_WAVEFORM) & 0x0f) != 0;
}

// Adds rows to the end of a table. Returns the 1-based start position, 0 if
// there is no room
static int addtable(int t, const unsigned char *left, const unsigned char *right, int rows)
{
  return patch_addrows(t, left, right, rows);
}

static int emptyinstrument(int i)
{
  return patch_emptyinstr(i);
}

// Makes instrument i from oscillator n. filterroute: the SID voices the
// filter is for (oscillator n plays on voice n+1, as in the instrument
// designer), 0 = no filtertable for this instrument. Returns 0 if a table is
// full
static int makeinstrument(int i, int n, const char *name, char *notes, int notessize, int filterroute)
{
  int base = 768 + 16 * n;
  int waveform = getparam_i(base + 2, DEF_WAVEFORM) & 0x0f;
  int pw = clamp(getparam_i(base + 0, DEF_PULSEWIDTH), 0, 4095);
  int a = clamp(getparam_i(base + 7, DEF_ATTACK), 0, 15);
  int d = clamp(getparam_i(base + 8, DEF_DECAY), 0, 15);
  int s = clamp(getparam_i(base + 9, DEF_SUSTAIN), 0, 15);
  int r = clamp(getparam_i(base + 10, DEF_RELEASE), 0, 15);
  int sync = getparam_i(base + 5, 0) != 0;
  int ring = getparam_i(base + 6, 0) != 0;
  int coarse = clamp(getparam_i(512 + 16 * n + 5, 0), -32, 95);
  int fine = getparam_i(512 + 16 * n + 6, 0);
  unsigned char l[4], rt[4];
  INSTR *p = &instr[i];

  clearinstr(i);
  p->ad = (unsigned char)((a << 4) | d);
  p->sr = (unsigned char)((s << 4) | r);

  if (!waveform)
  {
    // No waveform: silent in chipsynth too. Use pulse so that it sounds
    waveform = 4;
    strncat(notes, " No oscillator has a waveform, so pulse is used.", notessize - strlen(notes) - 1);
  }

  if (waveform >= 0x0e)
  {
    // E0-FF would be wavetable commands
    waveform &= 0x07;
    char text[96];
    snprintf(text, sizeof text, " Osc %d: noise combined with saw and pulse can't be used; noise was left out.", n + 1);
    strncat(notes, text, notessize - strlen(notes) - 1);
  }

  // Wavetable: the waveform with gate, sync and ring, and the tuning
  l[0] = (unsigned char)((waveform << 4) | 0x01 | (sync ? 0x02 : 0) | (ring ? 0x04 : 0));
  rt[0] = (unsigned char)(coarse >= 0 ? coarse : 0x80 + coarse);
  l[1] = 0xff;
  rt[1] = 0x00;
  p->ptr[WTBL] = (unsigned char)addtable(WTBL, l, rt, 2);
  if (!p->ptr[WTBL]) return 0;

  // Pulsetable: set the pulse width
  if (waveform & 4)
  {
    l[0] = (unsigned char)(0x80 | (pw >> 8));
    rt[0] = (unsigned char)(pw & 0xff);
    l[1] = 0xff;
    rt[1] = 0x00;
    p->ptr[PTBL] = (unsigned char)addtable(PTBL, l, rt, 2);
    if (!p->ptr[PTBL]) return 0;
  }

  // Filtertable: type, resonance, voices, cutoff. chipsynth has one filter
  // for all oscillators, so only one instrument gets the table
  if (filterroute)
  {
    int cutoff = clamp(getparam_i(865, DEF_CUTOFF), 0, 2047);
    int res = clamp(getparam_i(866, DEF_RESONANCE), 0, 15);
    int lp = getparam_i(867, 1) != 0;
    int bp = getparam_i(868, 0) != 0;
    int hp = getparam_i(869, 0) != 0;
    l[0] = (unsigned char)(0x80 | (lp ? 0x10 : 0) | (bp ? 0x20 : 0) | (hp ? 0x40 : 0));
    rt[0] = (unsigned char)((res << 4) | (filterroute & 7));
    l[1] = 0x00;
    rt[1] = (unsigned char)(cutoff >> 3);
    l[2] = 0xff;
    rt[2] = 0x00;
    p->ptr[FTBL] = (unsigned char)addtable(FTBL, l, rt, 3);
    if (!p->ptr[FTBL]) return 0;
  }

  if ((fine > 2) || (fine < -2))
  {
    char text[80];
    snprintf(text, sizeof text, " Osc %d: fine tune %+d cents was left out.", n + 1, fine);
    strncat(notes, text, notessize - strlen(notes) - 1);
  }

  memset(p->name, 0, MAX_INSTRNAMELEN);
  strncpy(p->name, name, MAX_INSTRNAMELEN - 1);
  return 1;
}

int fermatax_import(const char *path, int slot, char *report, int reportsize)
{
  char basename[MAX_INSTRNAMELEN + 8];
  char notes[512];
  int oscs[NUM_OSC];
  int numosc = 0;
  int made = 0;
  int route = 0;
  int filterdone = 0;
  int n;

  report[0] = 0;
  notes[0] = 0;
  if (!readpreset(path, report, reportsize)) return 0;
  if ((slot < 1) || (slot >= MAX_INSTR)) slot = 1;

  // Instrument name from the file name
  {
    const char *b = gt_basename(path);
    const char *dot = strrchr(b, '.');
    int len = dot ? (int)(dot - b) : (int)strlen(b);
    if (len > MAX_INSTRNAMELEN - 1) len = MAX_INSTRNAMELEN - 1;
    memcpy(basename, b, len);
    basename[len] = 0;
  }

  for (n = 0; n < NUM_OSC; n++)
  {
    if (oscon(n)) oscs[numosc++] = n;
  }
  if (!numosc)
  {
    // Nothing sounds: take oscillator 1 anyway
    oscs[numosc++] = 0;
  }
  else if (numosc < NUM_OSC)
  {
    for (n = 0; n < NUM_OSC; n++)
    {
      if ((getparam_i(512 + 16 * n, n == 0 ? 1 : 0)) && (!oscon(n)))
      {
        char text[64];
        snprintf(text, sizeof text, " Osc %d has no waveform (silent) and was skipped.", n + 1);
        strncat(notes, text, sizeof notes - strlen(notes) - 1);
      }
    }
  }

  // The voices that go through the filter (oscillator n on voice n+1)
  for (n = 0; n < numosc; n++)
  {
    if (getparam_i(768 + 16 * oscs[n] + 11, 0)) route |= 1 << oscs[n];
  }

  for (n = 0; n < numosc; n++)
  {
    int i = slot;
    int thisroute = 0;
    char name[MAX_INSTRNAMELEN + 8];

    if (n)
    {
      // Further oscillators go into the next empty instruments
      for (i = slot + 1; i < MAX_INSTR; i++)
      {
        if (emptyinstrument(i)) break;
      }
      if (i >= MAX_INSTR)
      {
        strncat(notes, " No empty instrument for the other oscillators.", sizeof notes - strlen(notes) - 1);
        break;
      }
    }
    // Several oscillators: "NAME:1".."NAME:3", one patch in the instrument
    // designer
    if (numosc > 1)
    {
      char cut[MAX_INSTRNAMELEN];
      memcpy(cut, basename, MAX_INSTRNAMELEN - 3);
      cut[MAX_INSTRNAMELEN - 3] = 0;
      snprintf(name, sizeof name, "%s:%d", cut, oscs[n] + 1);
    }
    else strcpy(name, basename);

    // The first oscillator that is filtered gets the filtertable
    if ((route) && (!filterdone) && (getparam_i(768 + 16 * oscs[n] + 11, 0)))
    {
      thisroute = route;
      filterdone = 1;
    }
    if (!makeinstrument(i, oscs[n], name, notes, sizeof notes, thisroute))
    {
      snprintf(report, reportsize, "The tables are full");
      return made;
    }
    made++;
  }

  // What can't be part of an instrument
  if (hasparam(262))
  {
    int clock = getparam_i(262, 985248);
    if ((clock > 1000000) && (!editorInfo.ntsc))
      strncat(notes, " The preset is for NTSC.", sizeof notes - strlen(notes) - 1);
    if ((clock < 1000000) && (editorInfo.ntsc))
      strncat(notes, " The preset is for PAL.", sizeof notes - strlen(notes) - 1);
  }
  if (hasparam(1024))
  {
    int chip = getparam_i(1024, 33);
    if ((chip < 32) && (editorInfo.sidmodel))
      strncat(notes, " The preset uses a 6581 SID.", sizeof notes - strlen(notes) - 1);
  }

  if (made == 1)
    snprintf(report, reportsize, "Instrument %02X.%s", slot, notes);
  else
    snprintf(report, reportsize, "%d instruments (one per oscillator) from %02X.%s", made, slot, notes);
  return made;
}

/*
 * Export: an instrument (with the other oscillators of its patch) as a
 * chipsynth C64 preset
 */

#include "gfermatax_init.h"

#define MAX_OUTPARAMS 64

typedef struct
{
  int id;
  char value[32];
} OUTPARAM;

static OUTPARAM outparams[MAX_OUTPARAMS];
static int numout;

static void setout(int id, const char *value)
{
  int c;

  for (c = 0; c < numout; c++)
  {
    if (outparams[c].id == id) break;
  }
  if (c == numout)
  {
    if (numout >= MAX_OUTPARAMS) return;
    numout++;
  }
  outparams[c].id = id;
  snprintf(outparams[c].value, sizeof outparams[c].value, "%s", value);
}

static void setouti(int id, int value)
{
  char text[16];

  snprintf(text, sizeof text, "%d", value);
  setout(id, text);
}

static int cmpout(const void *a, const void *b)
{
  return ((const OUTPARAM *)a)->id - ((const OUTPARAM *)b)->id;
}

// Oscillator n from instrument i: waveform, sync, ring and coarse tune from
// its main wave row, the pulse width from its first pulse row, ADSR. Returns
// the voices its filter part routes (bits), -1 if it has none
static int exportosc(int n, int i, int *filtered)
{
  INSTR *p = &instr[i];
  int base = 768 + 16 * n;
  int wr = patch_waverow(i);
  int pr = patch_pulserow(i);
  int fr = patch_filterrow(i);
  int waveform = 4;
  int sync = 0, ring = 0, coarse = 0;

  if (wr >= 0)
  {
    int l = ltable[WTBL][wr];
    int r = rtable[WTBL][wr];
    waveform = (l >> 4) & 0x0f;
    sync = (l & 0x02) != 0;
    ring = (l & 0x04) != 0;
    if (r < 0x60) coarse = r;
    else if (r < 0x80) coarse = r - 0x80;
  }
  else if ((p->firstwave >= 0x10) && (p->firstwave < 0xe0)) waveform = (p->firstwave >> 4) & 0x0f;

  setouti(512 + 16 * n, 1);
  setouti(base + 2, waveform);
  if (sync) setouti(base + 5, 1);
  if (ring) setouti(base + 6, 1);
  if (coarse) setouti(512 + 16 * n + 5, coarse);
  setouti(base + 7, p->ad >> 4);
  setouti(base + 8, p->ad & 0x0f);
  setouti(base + 9, p->sr >> 4);
  setouti(base + 10, p->sr & 0x0f);
  if (pr >= 0) setouti(base + 0, ((ltable[PTBL][pr] & 0x0f) << 8) | rtable[PTBL][pr]);
  else if (waveform & 4) setouti(base + 0, DEF_PULSEWIDTH);

  *filtered = 0;
  if (fr < 0) return -1;
  *filtered = 1;
  return rtable[FTBL][fr] & 7;
}

int fermatax_export(const char *path, int ins, char *report, int reportsize)
{
  int osc[NUM_OSC] = { 0, 0, 0 };
  char base[MAX_INSTRNAMELEN + 1];
  int filterins = 0, route = 0;
  int n, c, count = 0;
  FILE *handle;

  report[0] = 0;
  if ((ins < 1) || (ins >= MAX_INSTR))
  {
    snprintf(report, reportsize, "Select an instrument first");
    return 0;
  }

  // A patch of the SID designer ("NAME:1".."NAME:3"): its oscillators;
  // otherwise the instrument is oscillator 1
  if (patch_splitname(instr[ins].name, base))
  {
    for (n = 0; n < NUM_OSC; n++) osc[n] = patch_findosc(base, n + 1);
  }
  if ((!osc[0]) && (!osc[1]) && (!osc[2])) osc[0] = ins;

  numout = 0;
  for (c = 0; c < (int)(sizeof fermataxinitparams / sizeof fermataxinitparams[0]); c++)
    setout(fermataxinitparams[c].id, fermataxinitparams[c].value);
  if (!osc[0]) setouti(512, 0);

  for (n = 0; n < NUM_OSC; n++)
  {
    int filtered, r;

    if (!osc[n]) continue;
    count++;
    r = exportosc(n, osc[n], &filtered);
    if ((filtered) && (!filterins))
    {
      filterins = osc[n];
      route = r;
    }
  }

  // The filter (one for all oscillators in chipsynth): from the first
  // instrument with a filter part; the oscillators of the voices it routes
  // go through it (oscillator n plays on voice n+1)
  if (filterins)
  {
    int fr = patch_filterrow(filterins);
    int cr = patch_cutoffrow(filterins);
    int l = ltable[FTBL][fr];
    int r = rtable[FTBL][fr];

    setouti(866, r >> 4);
    setouti(867, (l & 0x10) ? 1 : 0);
    setouti(868, (l & 0x20) ? 1 : 0);
    setouti(869, (l & 0x40) ? 1 : 0);
    if (cr >= 0) setouti(865, rtable[FTBL][cr] << 3);
    for (n = 0; n < NUM_OSC; n++)
    {
      if ((osc[n]) && (route & (1 << n))) setouti(768 + 16 * n + 11, 1);
    }
  }

  // The SID: 8580 (Init) or 6581, and NTSC (PAL is the default)
  setouti(1024, editorInfo.sidmodel ? 33 : 29);
  if (editorInfo.ntsc) setouti(262, 1022727);

  qsort(outparams, numout, sizeof outparams[0], cmpout);

  handle = fopen(path, "wb");
  if (!handle)
  {
    snprintf(report, reportsize, "Can't write %s", gt_basename(path));
    return 0;
  }
  for (c = 0; c < (int)(sizeof fermataxinit / sizeof fermataxinit[0]); c++)
  {
    if (fermataxinit[c])
    {
      fputs(fermataxinit[c], handle);
      fputc(10, handle);
      continue;
    }
    for (n = 0; n < numout; n++)
    {
      fprintf(handle, "        <Param id=\"%d\" value=\"%s\" />", outparams[n].id, outparams[n].value);
      fputc(10, handle);
    }
  }
  if (fclose(handle))
  {
    snprintf(report, reportsize, "Can't write %s", gt_basename(path));
    return 0;
  }
  if (count > 1)
    snprintf(report, reportsize, "Exported %s: %d oscillators (the patch %s)", gt_basename(path), count, base);
  else
    snprintf(report, reportsize, "Exported %s", gt_basename(path));
  return 1;
}
