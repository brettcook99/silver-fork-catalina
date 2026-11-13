// GoatTracker GUI: the SID designer
//
// A full-window mode: under the tracker's title bar, a Commodore 64 screen
// (gui_c64.h) in a frame like the C64's nameplate, laid out like the patch
// page of Plogue's chipsynth C64: three oscillators (waveforms, sync / ring
// / test, pulse width, envelope) and a filter, plus pages with step
// sequencers for the wave, pulse and filter tables, one with the emulation
// settings and an instrument browser. MIDI knobs set the envelope and the
// filter.
//
// Each oscillator is a GoatTracker instrument, played on the SID voice with
// the same number, so that sync and ring modulation between the oscillators
// work as in chipsynth. The oscillators of the patch "BASS" are the
// instruments "BASS:1", "BASS:2" and "BASS:3" (the names the .fermatax import
// gives them); an instrument without such a number is a patch with one
// oscillator. The patch is worked out from the instruments again on every
// frame, so the designer always shows the song as it is after undo, loading
// or editing in the tracker.

#include "gui_c64.h"

extern "C" {
#include "../gpatch.h"
}

using namespace gui;
using namespace c64;

namespace gui
{
  bool showdesigner = false;
}

// Screen layout (rows)
#define HEADER_Y 0
#define TAB_Y 6
#define PAGE_Y 8
#define PAGE_H 30
#define HINT_Y 39

// The frame: Scott's template (designerframe.png, made from measurements on
// his 520 x 345 image: white panel with a blue bar along the top, the C64
// nameplate badge). It fills the retro screen's place, the tracker's title
// bar over its blue bar. On its white middle the designer's window, as in
// Scott's picture of the right look: a lavender border, like a C64's border,
// around the violet screen. The border's place, in the template's pixels:
#define FRAME_W 520.0f
#define FRAME_H 345.0f
#define BORDER_X0 13.0f
#define BORDER_Y0 49.07f
#define BORDER_X1 508.0f
#define BORDER_Y1 335.2f
#define BORDERCOLOR 0xf7c1ff          // Lavender: the border
#define SCREENCOLOR 0x755df9          // Violet: the designer's screen (BLUE)
#define BORDERW 0.0323f               // Border at the sides (of its width)
#define LABELCOLOR 0xc9b4ff           // Lavender: labels and lines (LIGHTBLUE)
#define FAINTCOLOR 0xa29cc0           // Greyed out text (GREY)
#define DIMCOLOR 0x5443c8             // Disabled things (DARKGREY)
#define WARNCOLOR 0xff8a7a            // Lights, warnings (LIGHTRED)

enum { PAGE_PATCH, PAGE_WAVE, PAGE_PULSE, PAGE_FILTER, PAGE_EMU, PAGE_BROWSER, NUM_PAGES };
static const char *pagelabels[NUM_PAGES] = { " PATCH ", " WAVE ", " PULSE ", " FILTER ", " EMU ", " BROWSER " };
static const char *pagetips[NUM_PAGES] =
{
  "THE THREE OSCILLATORS AND THE FILTER (TAB: NEXT PAGE)",
  "STEP SEQUENCER OF THE WAVETABLE: WAVEFORMS AND ARPEGGIO NOTES",
  "STEP SEQUENCER OF THE PULSETABLE: PULSE WIDTH SWEEPS",
  "STEP SEQUENCER OF THE FILTERTABLE: FILTER TYPE AND CUTOFF SWEEPS",
  "SID CHIP, CLOCK, PLAYER SPEED, HARD RESTART AND THE SID REGISTERS",
  "THE INSTRUMENTS IN A FOLDER: UP / DOWN TRIES ONE, ENTER OR DOUBLE-CLICK LOADS IT"
};

// Where the sounding note comes from
enum { SRC_NONE, SRC_MOUSE, SRC_KEY, SRC_MIDI, SRC_PREVIEW };

// Keyboard navigation
enum { NAV_NONE, NAV_UP, NAV_DOWN, NAV_LEFT, NAV_RIGHT, NAV_ENTER };

// Widget ids: area (high byte) + number
#define WID(area, n) (((area) << 8) | ((n) & 0xff))
enum
{
  ID_HEADER = 1, ID_TABS, ID_OSC1, ID_OSC2, ID_OSC3, ID_FILTER, ID_SEQHEAD,
  ID_STEP, ID_KIND, ID_BITS0, ID_BITS7 = ID_BITS0 + 7, ID_NOTE, ID_LANE, ID_FIX, ID_LOOP,
  ID_SEQBTN, ID_LISTNUM, ID_LISTKIND, ID_LISTA, ID_LISTB, ID_LISTC, ID_LISTD, ID_LISTE, ID_LISTF,
  ID_LISTG, ID_LISTH, ID_SWEEP, ID_EMU, ID_BROWSER
};

static int page = PAGE_PATCH;
static int osc[PATCH_OSC];            // Instrument of each oscillator, 0 = none
static bool oscmute[PATCH_OSC];       // Not played
static char patchbase[MAX_INSTRNAMELEN + 1];
static int seqosc = 0;                // Oscillator on the WAVE and PULSE pages
static int savededitmode = EDIT_INSTRUMENT;
static bool drawnlastframe = false;

static int heldnote = 0;
static int heldsource = SRC_NONE;
static int heldkey = 0;
static bool heldvoice[PATCH_OSC];

static bool nameedit = false;
static char namebuf[MAX_INSTRNAMELEN + 1];

static int selstep[NUM_PAGES];
static int scrollpos[NUM_PAGES];

// The sweep makers
static int psweeplo = 0x200, psweephi = 0xe00, psweepspeed = 0x20;
static int fsweepfrom = 0xe0, fsweepto = 0x20, fsweepframes = 0x40;

// Mouse
static struct
{
  bool enabled;          // On the screen and no dialog open
  float x, y;            // In characters
  bool clicked, down;
  float wheel;
  bool shift;
} in;
static int active = 0;                // Widget being dragged
static float dragx, dragy;
static int dragstart;
static bool dragfine;

// Keyboard focus (LEFT/RIGHT move it, UP/DOWN change values, ENTER presses)
static int focus = 0;
static int focuslist[600];
static int nfocus = 0;
static bool keyboardmode = false;
static int navqueue[16];
static bool navshiftqueue[16];
static int navhead = 0, navtail = 0;
static int nav = NAV_NONE;
static bool navshift = false;

static const char *hint = NULL;
static const char *focushint = NULL;
static bool editopen = false;
static Uint32 cctime = 0;             // When a MIDI knob last changed something
#define CCPAUSE 600                   // ms: a pause that ends a knob's undo step

// The instrument browser's try-out (preview): the instrument tried is loaded
// into the patch's instrument; the song as it was is kept here to be put
// back. Editing anything keeps the instrument tried (as an undo step)
static struct
{
  bool active;
  int slot;                           // The instrument it is loaded into
  char path[MAX_PATHNAME];
  unsigned sum;                       // Checksum of the song with it loaded
  INSTR instr[MAX_INSTR];
  unsigned char ltable[MAX_TABLES][MAX_TABLELEN];
  unsigned char rtable[MAX_TABLES][MAX_TABLELEN];
  unsigned char pattern[MAX_PATT][MAX_PATTROWS * 4 + 4];
} pv;
static void keeppreview(void);
static void cancelpreview(void);

/*
 * Undo: everything the designer may change is marked, as table rows that are
 * inserted or deleted move pointers in all instruments, tables and patterns
 */

static void openedit(void)
{
  int c;

  if (editopen) return;
  if (pv.active) keeppreview();
  undobegin();
  for (c = 0; c < MAX_INSTR; c++) undoinstrument(c);
  for (c = 0; c < MAX_TABLES; c++) undotable(c);
  for (c = 0; c < MAX_PATT; c++) undopattern(c);
  editopen = true;
}

static void closeedit(void)
{
  if (!editopen) return;
  undoend();
  editopen = false;
}

// A setting of the editor (SID model...), undoable like in the control bar
static void editorsetting(unsigned *value, unsigned newvalue, bool reinitsound)
{
  if (*value == newvalue) return;
  undoCreateEditorInfoBackup();
  *value = newvalue;
  undoAddEditorSettingsToList();
  if (reinitsound) do_sound_init();
}

static void tablefull(void)
{
  gui_message("?OUT OF MEMORY ERROR: the table is full");
}

/*
 * The patch
 */

static void bindpatch(void)
{
  int primary = editorInfo.einum;
  int n, k;

  if ((primary < 1) || (primary >= MAX_INSTR))
  {
    primary = 1;
    editorInfo.einum = 1;
  }
  n = patch_splitname(instr[primary].name, patchbase);
  for (k = 0; k < PATCH_OSC; k++) osc[k] = 0;
  if (!n)
  {
    osc[0] = primary;
    return;
  }
  for (k = 0; k < PATCH_OSC; k++)
    osc[k] = (k == n - 1) ? primary : patch_findosc(patchbase, k + 1);
}

static bool multiosc(void)
{
  char base[MAX_INSTRNAMELEN + 1];
  return patch_splitname(instr[editorInfo.einum].name, base) != 0;
}

static int firstosc(void)
{
  int k;
  for (k = 0; k < PATCH_OSC; k++)
  {
    if (osc[k]) return k;
  }
  return -1;
}

// The oscillator whose filter part runs (the last one started wins)
static int filterowner(void)
{
  int k;
  for (k = PATCH_OSC - 1; k >= 0; k--)
  {
    if ((osc[k]) && (instr[osc[k]].ptr[FTBL])) return k;
  }
  return -1;
}

// The filter part to change: the owner's, or a new one on the first
// oscillator. Returns its instrument, 0 if none could be made
static int needfilter(int route)
{
  int k = filterowner();

  if (k < 0)
  {
    k = firstosc();
    if (k < 0) return 0;
  }
  else route = 0;
  if (patch_needfilter(osc[k], route) < 0)
  {
    tablefull();
    return 0;
  }
  return osc[k];
}

// Switches SID voice n's filter routing (making a filter part if needed)
static void toggleroute(int n)
{
  int owner = filterowner();
  int frow = (owner >= 0) ? patch_filterrow(osc[owner]) : -1;
  int fi;

  openedit();
  if (frow >= 0)
  {
    rtable[FTBL][frow] ^= (unsigned char)(1 << n);
    return;
  }
  // A new filter part routes voice n; a new set row in an existing part
  // routes nothing yet
  fi = needfilter(1 << n);
  if ((fi) && (owner >= 0))
  {
    frow = patch_filterrow(fi);
    if (frow >= 0) rtable[FTBL][frow] |= (unsigned char)(1 << n);
  }
}

// Turns on oscillator k: a copy of the first oscillator in the next empty
// instrument, named after the patch
static void addosc(int k)
{
  char base[MAX_INSTRNAMELEN + 1];
  int primary = editorInfo.einum;
  int first = firstosc();
  int highest = primary;
  int n, j, dest;

  if (first < 0) return;
  n = patch_splitname(instr[primary].name, base);
  if (!base[0]) snprintf(base, sizeof base, "PATCH %02X", primary);
  for (j = 0; j < PATCH_OSC; j++)
  {
    if (osc[j] > highest) highest = osc[j];
  }
  dest = patch_findempty(highest);
  if (!dest)
  {
    gui_message("No empty instrument for oscillator %d", k + 1);
    return;
  }
  openedit();
  if (!patch_copyinstr(dest, osc[first]))
  {
    tablefull();
    return;
  }
  if (!n) patch_setname(primary, base, first + 1);
  patch_setname(dest, base, k + 1);
  oscmute[k] = false;
  gui_message("Oscillator %d is instrument %02X", k + 1, dest);
}

// Moves to the patch before or after this one
static void steppatch(int dir)
{
  int lo = MAX_INSTR, hi = 0, k, i;

  for (k = 0; k < PATCH_OSC; k++)
  {
    if (!osc[k]) continue;
    if (osc[k] < lo) lo = osc[k];
    if (osc[k] > hi) hi = osc[k];
  }
  if (!hi) lo = hi = editorInfo.einum;
  i = (dir < 0) ? lo - 1 : hi + 1;
  if ((i < 1) || (i >= MAX_INSTR)) return;
  editorInfo.einum = i;
  for (k = 0; k < PATCH_OSC; k++) oscmute[k] = false;
  nameedit = false;
}

// Removes the instrument's wave, pulse and filter table parts (rows that
// nothing else uses are deleted; a shared part is only let go of)
static void removeparts(int ins)
{
  int t;

  for (t = WTBL; t <= FTBL; t++)
  {
    int start = patch_partstart(t, ins);
    int len;

    if (start < 0) continue;
    if (patch_isshared(t, ins))
    {
      instr[ins].ptr[t] = 0;
      continue;
    }
    len = patch_partlen(t, start);
    instr[ins].ptr[t] = 0;
    while (len-- > 0) patch_deleterow(t, start);
  }
}

static void noteoff(int source, int key);

// CLEAR: the patch becomes an init patch, a plain sound to start from: one
// oscillator (the patch's first instrument) playing a sawtooth with full
// sustain. The other oscillators' instruments are emptied, the table rows
// the patch alone used are deleted. Can be undone
static void initpatch(void)
{
  static const unsigned char wl[2] = { 0x21, 0xff };
  static const unsigned char wr[2] = { 0x00, 0x00 };
  int first = firstosc();
  int ins, k;

  if (first < 0) return;
  if (nameedit) nameedit = false;
  if (heldnote) noteoff(heldsource, heldkey);
  openedit();
  ins = osc[first];
  for (k = 0; k < PATCH_OSC; k++)
  {
    if (!osc[k]) continue;
    removeparts(osc[k]);
    clearinstr(osc[k]);
  }
  instr[ins].ad = 0x00;
  instr[ins].sr = 0xf0;
  if (!patch_setpart(WTBL, ins, wl, wr, 2)) tablefull();
  editorInfo.einum = ins;
  for (k = 0; k < PATCH_OSC; k++) oscmute[k] = false;
  for (k = 0; k < NUM_PAGES; k++) selstep[k] = -1;
  seqosc = 0;
  bindpatch();
  gui_message("Init patch in instrument %02X (CTRL+Z undoes)", ins);
}

static void startnameedit(void)
{
  if (multiosc()) strcpy(namebuf, patchbase);
  else
  {
    memcpy(namebuf, instr[editorInfo.einum].name, MAX_INSTRNAMELEN);
    namebuf[MAX_INSTRNAMELEN - 1] = 0;
  }
  nameedit = true;
}

static void commitname(void)
{
  int k;

  if (!nameedit) return;
  nameedit = false;
  openedit();
  if (!multiosc()) patch_setname(editorInfo.einum, namebuf, 0);
  else
  {
    char base[MAX_INSTRNAMELEN + 1];
    strcpy(base, namebuf);
    if (!base[0]) snprintf(base, sizeof base, "PATCH %02X", editorInfo.einum);
    for (k = 0; k < PATCH_OSC; k++)
    {
      if (osc[k]) patch_setname(osc[k], base, k + 1);
    }
  }
}

/*
 * Playing notes: the whole patch, oscillator n on voice n
 */

static void noteon(int note, int source, int key)
{
  int k;

  if ((note < FIRSTNOTE) || (note > LASTNOTE)) return;
  bindpatch();
  heldnote = note;
  heldsource = source;
  heldkey = key;
  for (k = 0; k < PATCH_OSC; k++)
  {
    heldvoice[k] = (osc[k] != 0) && (!oscmute[k]);
    if (heldvoice[k]) playtestnote(note, osc[k], k);
  }
}

static void noteoff(int source, int key)
{
  int k;

  if ((!heldnote) || (heldsource != source)) return;
  if ((source == SRC_KEY) && (key != heldkey)) return;
  for (k = 0; k < PATCH_OSC; k++)
  {
    if (heldvoice[k]) releasenote(k);
    heldvoice[k] = false;
  }
  heldnote = 0;
  heldsource = SRC_NONE;
}

/*
 * Widgets on the character screen. Immediate mode, like ImGui: each call
 * draws the widget and handles the mouse and the keyboard focus
 */

static bool over(float x, float y, float w, float h)
{
  return (in.enabled) && (in.x >= x) && (in.x < x + w) && (in.y >= y) && (in.y < y + h);
}

// Puts the widget into the keyboard focus order. Returns true if it has the
// focus (shown only after keyboard use)
static bool takefocus(int id)
{
  if (nfocus < (int)(sizeof focuslist / sizeof focuslist[0])) focuslist[nfocus++] = id;
  return (focus == id) && (keyboardmode);
}

static void tip(const char *text)
{
  if (text) hint = text;
}

// A text button. on: drawn in reverse video in color oncol. Returns true when
// clicked, or ENTER / SPACE pressed while it has the keyboard focus
static bool button(int id, int x, int y, const char *label, bool on, int oncol, const char *tiptext, bool enabled = true)
{
  int w = (int)strlen(label);
  bool focused = enabled && takefocus(id);
  bool hovered = enabled && over((float)x, (float)y, (float)w, 1.0f);
  bool pressed = false;
  int fg;

  if (!enabled) fg = on ? GREY : DARKGREY;
  else if (focused) fg = YELLOW;
  else if (on) fg = hovered ? WHITE : oncol;
  else fg = hovered ? WHITE : LIGHTBLUE;
  print(x, y, label, fg, on);
  if (hovered)
  {
    tip(tiptext);
    if (in.clicked) pressed = true;
  }
  if (focused)
  {
    focushint = tiptext;
    if (nav == NAV_ENTER) pressed = true;
  }
  return pressed;
}

// A clickable area drawn by the caller. Returns true when clicked or
// pressed; *state gets 0 = normal, 1 = hovered, 2 = focused
static bool area(int id, int x, int y, int w, int h, const char *tiptext, int *state, bool enabled = true)
{
  bool focused = enabled && takefocus(id);
  bool hovered = enabled && over((float)x, (float)y, (float)w, (float)h);
  bool pressed = false;

  *state = focused ? 2 : hovered ? 1 : 0;
  if (hovered)
  {
    tip(tiptext);
    if (in.clicked) pressed = true;
  }
  if (focused)
  {
    focushint = tiptext;
    if (nav == NAV_ENTER) pressed = true;
  }
  return pressed;
}

static int clampint(int v, int lo, int hi)
{
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

// Starts dragging widget id
static void startdrag(int id, int value)
{
  active = id;
  dragx = in.x;
  dragy = in.y;
  dragstart = value;
  dragfine = in.shift;
  openedit();
}

// Value changes from the wheel and the keyboard. step: normal step, big: with
// SHIFT on the keyboard (with SHIFT the wheel goes in single steps)
static void wheelandkeys(int *value, bool hovered, bool focused, int step, int big)
{
  if ((hovered) && (in.wheel != 0.0f))
  {
    openedit();
    *value += (in.wheel > 0.0f ? 1 : -1) * (in.shift ? 1 : step);
  }
  if (focused)
  {
    if ((nav == NAV_UP) || (nav == NAV_DOWN))
    {
      openedit();
      *value += (nav == NAV_UP ? 1 : -1) * (navshift ? big : step);
    }
  }
}

// A vertical slider of w x h characters. Click / drag to set it (SHIFT + drag:
// fine steps), wheel, UP / DOWN
static bool vslider(int id, int x, int y, int w, int h, int *value, int minv, int maxv, int col, const char *tiptext, bool enabled = true,
  int track = BLACK)
{
  bool focused = enabled && takefocus(id);
  bool hovered = enabled && over((float)x - 0.25f, (float)y - 0.5f, (float)w + 0.5f, (float)h + 1.0f);
  int range = maxv - minv;
  int old = *value;
  int step = range > 255 ? 16 : 1;

  if (enabled)
  {
    if (hovered)
    {
      tip(tiptext);
      if (in.clicked) startdrag(id, *value);
    }
    if ((active == id) && (in.down))
    {
      if (in.shift != dragfine)
      {
        dragstart = *value;
        dragy = in.y;
        dragfine = in.shift;
      }
      if (dragfine) *value = dragstart + (int)((dragy - in.y) * CHARSIZE);
      else *value = minv + (int)floorf(((float)(y + h) - in.y) / h * range + 0.5f);
    }
    if (focused) focushint = tiptext;
    wheelandkeys(value, hovered, focused, step, range > 255 ? 256 : (range + 15) / 16);
  }
  *value = clampint(*value, minv, maxv);
  vbar(x, y, w, h, range ? (float)(*value - minv) / range : 0.0f, enabled ? (focused ? YELLOW : col) : DARKGREY, focused ? GREY : track);
  return *value != old;
}

// A horizontal slider of w characters
static bool hslider(int id, int x, int y, int w, int *value, int minv, int maxv, int col, const char *tiptext, bool enabled = true)
{
  bool focused = enabled && takefocus(id);
  bool hovered = enabled && over((float)x - 0.5f, (float)y, (float)w + 1.0f, 1.0f);
  int range = maxv - minv;
  int old = *value;
  int step = range > 255 ? 16 : 1;

  if (enabled)
  {
    if (hovered)
    {
      tip(tiptext);
      if (in.clicked) startdrag(id, *value);
    }
    if ((active == id) && (in.down))
    {
      if (in.shift != dragfine)
      {
        dragstart = *value;
        dragx = in.x;
        dragfine = in.shift;
      }
      if (dragfine) *value = dragstart + (int)((in.x - dragx) * CHARSIZE);
      else *value = minv + (int)floorf((in.x - x) / w * range + 0.5f);
    }
    if (focused) focushint = tiptext;
    wheelandkeys(value, hovered, focused, step, range > 255 ? 256 : (range + 15) / 16);
  }
  *value = clampint(*value, minv, maxv);
  hbar(x, y, w, range ? (float)(*value - minv) / range : 0.0f, enabled ? (focused ? YELLOW : col) : DARKGREY, focused ? DARKGREY : BLACK);
  return *value != old;
}

// A value shown as text: drag up / down, wheel, UP / DOWN
static bool dragvalue(int id, int x, int y, int w, int *value, int minv, int maxv, int step, const char *text, int col, const char *tiptext, bool enabled = true)
{
  bool focused = enabled && takefocus(id);
  bool hovered = enabled && over((float)x, (float)y, (float)w, 1.0f);
  int old = *value;

  if (enabled)
  {
    if (hovered)
    {
      tip(tiptext);
      if (in.clicked) startdrag(id, *value);
    }
    if ((active == id) && (in.down))
    {
      if (in.shift != dragfine)
      {
        dragstart = *value;
        dragy = in.y;
        dragfine = in.shift;
      }
      *value = dragstart + (int)((dragy - in.y) * 2.0f) * (dragfine ? 1 : step);
    }
    if (focused) focushint = tiptext;
    wheelandkeys(value, hovered, focused, step, step * 16);
  }
  *value = clampint(*value, minv, maxv);
  {
    int fg = !enabled ? DARKGREY : focused ? YELLOW : (hovered || active == id) ? WHITE : col;
    int c = print(x, y, text, fg, active == id);
    while (c < x + w) put(c++, y, SC_SPACE, fg);
  }
  return *value != old;
}

/*
 * Values of an instrument
 */

static unsigned char waveleft(int ins)
{
  int row = patch_waverow(ins);
  return row >= 0 ? ltable[WTBL][row] : 0;
}

// Waveform bits (1 tri, 2 saw, 4 pulse, 8 noise) of a wave row left byte
static int wavebits(unsigned char l)
{
  if ((l >= 0x10) && (l < WAVESILENT)) return l >> 4;
  return 0;
}

// The SID control register value of a wave row
static int controlvalue(unsigned char l)
{
  if ((l >= 0x10) && (l < WAVESILENT)) return l;
  if ((l >= WAVESILENT) && (l <= WAVELASTSILENT)) return l & 0x0f;
  return 0;
}

// Left byte of a wave row for a control register value. -1 if the
// waveforms can't be used in the wavetable (noise with saw and pulse)
static int waverowleft(int value)
{
  int bits = (value >> 4) & 15;
  if ((bits == 0x0e) || (bits == 0x0f)) return -1;
  if (!bits) return WAVESILENT | (value & 0x0f);
  return value & 0xff;
}

// Changes the main wave row's control register value with XOR mask
static void togglewave(int ins, int mask)
{
  int row, left;

  openedit();
  row = patch_needwaverow(ins);
  if (row < 0)
  {
    tablefull();
    return;
  }
  left = waverowleft(controlvalue(ltable[WTBL][row]) ^ mask);
  if (left < 0)
  {
    gui_message("Noise can't be combined with saw and pulse in the wavetable");
    return;
  }
  ltable[WTBL][row] = (unsigned char)left;
  // Pulse needs a pulse width
  if ((left >= 0x10) && (left < WAVESILENT) && (left & 0x40) && (!instr[ins].ptr[PTBL]))
  {
    if (patch_needpulserow(ins, 0x800) < 0) tablefull();
  }
}

static int pulsewidth(int ins)
{
  int row = patch_pulserow(ins);
  if (row < 0) return 0;
  return ((ltable[PTBL][row] & 0x0f) << 8) | rtable[PTBL][row];
}

static void setpulsewidth(int ins, int pw)
{
  int row = patch_needpulserow(ins, pw);
  if (row < 0)
  {
    tablefull();
    return;
  }
  ltable[PTBL][row] = (unsigned char)(0x80 | ((pw >> 8) & 0x0f));
  rtable[PTBL][row] = (unsigned char)(pw & 0xff);
}

// Note column values: relative (-32..95), absolute (note 1-95) or none
static bool noteisabs(unsigned char r)
{
  return r > 0x80;
}

static int noterelative(unsigned char r)
{
  if (r >= 0x80) return 0;
  return r < 0x60 ? r : (int)r - 0x80;
}

static unsigned char relativenote(int v)
{
  v = clampint(v, -32, 95);
  return (unsigned char)(v >= 0 ? v : 0x80 + v);
}

static void notetext(unsigned char r, char *out, int size)
{
  if (r == 0x80) snprintf(out, size, "--");
  else if (noteisabs(r))
  {
    int n = r & 0x7f;
    snprintf(out, size, "%s", n < 93 ? notename[n] : "???");
  }
  else
  {
    int v = noterelative(r);
    snprintf(out, size, "%c%02d", v < 0 ? '-' : '+', v < 0 ? -v : v);
  }
}

/*
 * Header, tabs, keyboard, hint and READY line
 */

static void drawheader(void)
{
  int primary = editorInfo.einum;
  char text[40];
  int state;

  // "SID Designer" and the rainbow stripes left of "64" are drawn by
  // drawlogo()
  print(23, HEADER_Y + 2, "64", WHITE);

  // Patch selector
  box(26, HEADER_Y + 1, 31, 3, LIGHTBLUE, "PATCH", WHITE);
  if (button(WID(ID_HEADER, 1), 27, HEADER_Y + 2, "<", false, WHITE, "PREVIOUS PATCH (INSTRUMENT BEFORE THIS ONE)")) steppatch(-1);
  printfmt(29, HEADER_Y + 2, YELLOW, "%02X", primary);
  if (nameedit)
  {
    int c = print(32, HEADER_Y + 2, namebuf, WHITE);
    if ((int)(ImGui::GetTime() * 3.0) & 1) put(c, HEADER_Y + 2, SC_BLOCK, WHITE);
    if ((in.clicked) && (!over(32, HEADER_Y + 2, 16, 1))) commitname();
  }
  else
  {
    char name[MAX_INSTRNAMELEN + 1];
    memcpy(name, instr[primary].name, MAX_INSTRNAMELEN);
    name[MAX_INSTRNAMELEN - 1] = 0;
    if (multiosc()) snprintf(text, sizeof text, "%s", patchbase);
    else snprintf(text, sizeof text, "%s", name);
    if (area(WID(ID_HEADER, 2), 32, HEADER_Y + 2, 16, 1, "CLICK TO RENAME THE PATCH (ALL ITS OSCILLATORS)", &state))
      startnameedit();
    print(32, HEADER_Y + 2, text[0] ? text : "(NO NAME)", text[0] ? (state ? YELLOW : WHITE) : DARKGREY);
  }
  if (button(WID(ID_HEADER, 3), 55, HEADER_Y + 2, ">", false, WHITE, "NEXT PATCH (INSTRUMENT AFTER THIS ONE)")) steppatch(1);

  // Octave, file commands
  print(59, HEADER_Y + 1, "OCTAVE", LIGHTBLUE);
  if (button(WID(ID_HEADER, 4), 67, HEADER_Y + 1, "<", false, WHITE, "OCTAVE DOWN (PAGE DOWN)"))
  {
    if (editorInfo.epoctave > 0) editorInfo.epoctave--;
  }
  printfmt(69, HEADER_Y + 1, WHITE, "%d", editorInfo.epoctave);
  if (button(WID(ID_HEADER, 5), 71, HEADER_Y + 1, ">", false, WHITE, "OCTAVE UP (PAGE UP)"))
  {
    if (editorInfo.epoctave < 7) editorInfo.epoctave++;
  }
  if (button(WID(ID_HEADER, 6), 59, HEADER_Y + 3, "LOAD", false, WHITE, "LOAD AN INSTRUMENT INTO THE FIRST OSCILLATOR"))
    cmd_post(GTCMD_LOADINSTR);
  if (button(WID(ID_HEADER, 7), 65, HEADER_Y + 3, "SAVE", false, WHITE, "SAVE THE FIRST OSCILLATOR'S INSTRUMENT"))
    cmd_post(GTCMD_SAVEINSTR);
  if (button(WID(ID_HEADER, 8), 71, HEADER_Y + 3, "CLEAR", false, WHITE, "CLEAR TO AN INIT PATCH: ONE PLAIN OSCILLATOR (CTRL+Z UNDOES)"))
    initpatch();
}

// The badge's rainbow colors (the C64's own red and orange are too dark on
// the violet screen)
static const ImU32 rainbow[5] =
{
  IM_COL32(0xff, 0x5b, 0x57, 255), IM_COL32(0xff, 0xcc, 0x90, 255), IM_COL32(0xff, 0xff, 0x6f, 255),
  IM_COL32(0xa2, 0xff, 0x9c, 255), IM_COL32(0x00, 0xff, 0xff, 255)
};

// "SID Designer" in the header in the tracker's own font (as in its title
// bar), and the rainbow stripes between it and "64". Drawn over the screen
static void drawlogo(void)
{
  ImGuiIO &io = ImGui::GetIO();
  ImDrawList *dl = ImGui::GetBackgroundDrawList();
  ImVec2 s0 = c64::screenmin();
  float cell = c64::charsize();
  float fb = io.DisplayFramebufferScale.x > 0.0f ? io.DisplayFramebufferScale.x : 1.0f;
  const char *text = "SID Designer";
  float pixel, width;
  ImVec2 pos;
  int r;

  // Stripes in the lower half of rows 0-4, columns 12-21
  for (r = 0; r < 5; r++)
  {
    float y = s0.y + cell * (HEADER_Y + r + 0.5f);
    dl->AddRectFilled(ImVec2(s0.x + cell * 12.0f, y), ImVec2(s0.x + cell * 22.0f, y + cell * 0.5f), rainbow[r]);
  }

  // Font pixels of whole screen pixels, filling columns 0-10 (fractions only
  // when whole ones would not fit)
  pixel = floorf(cell * 11.0f / retrotextwidth(text, 1.0f) * fb + 0.5f) / fb;
  if (retrotextwidth(text, pixel) > cell * 11.0f) pixel = cell * 11.0f / retrotextwidth(text, 1.0f);
  if (pixel < 1.0f / fb) pixel = 1.0f / fb;
  width = retrotextwidth(text, pixel);
  pos = ImVec2(floorf((s0.x + cell * 11.0f - width) * fb) / fb, floorf((s0.y + cell * (HEADER_Y + 2.5f) - pixel * 7.5f) * fb) / fb);
  drawretrotext(dl, pos, pixel, pixel, IM_COL32(255, 255, 255, 255), text);
}

static void drawtabs(void)
{
  int p, x = 1;

  for (p = 0; p < NUM_PAGES; p++)
  {
    if (button(WID(ID_TABS, p), x, TAB_Y, pagelabels[p], page == p, WHITE, pagetips[p]))
    {
      page = p;
      focus = 0;
    }
    x += (int)strlen(pagelabels[p]) + 1;
  }
  hline(0, TAB_Y + 1, COLS, LIGHTBLUE);
}

// The bottom line: what the control under the pointer (or with the keyboard
// focus) does, a status message, or the keys
static void drawhint(void)
{
  const char *text = hint ? hint : focushint;
  const char *msg = gui_currentmessage();
  char line[COLS + 1];
  int col = LIGHTBLUE;

  if ((!text) && (msg))
  {
    text = msg;
    col = WHITE;
  }
  if (!text) text = "TAB PAGES  ARROWS/ENTER CONTROLS  Z-M Q-P PLAY  PGUP/PGDN OCTAVE  ESC TRACKER";
  snprintf(line, sizeof line, "%s", text);
  print(0, HINT_Y, line, col);
}

/*
 * PATCH page: oscillators and filter
 */

static const unsigned char waveicons[4][2][4] =
{
  { { SC_SPACE, SC_DIAG_UP, SC_DIAG_DOWN, SC_SPACE }, { SC_DIAG_UP, SC_SPACE, SC_SPACE, SC_DIAG_DOWN } },
  { { SC_SPACE, SC_DIAG_UP, SC_VLINE, SC_SPACE }, { SC_DIAG_UP, SC_SPACE, SC_VLINE, SC_DIAG_UP } },
  { { SC_CORNER_TL, SC_HLINE, SC_CORNER_TR, SC_SPACE }, { SC_CORNER_BR, SC_SPACE, SC_CORNER_BL, SC_HLINE } },
  { { SC_QUAD_UR, SC_QUAD_ULLR, SC_QUAD_LL, SC_QUAD_UL }, { SC_QUAD_ULLR, SC_QUAD_LR, SC_QUAD_UL, SC_QUAD_ULLR } }
};

static const char *wavetips[4] =
{
  "TRIANGLE WAVEFORM (CAN BE COMBINED WITH THE OTHERS)",
  "SAWTOOTH WAVEFORM",
  "PULSE WAVEFORM: SET ITS WIDTH WITH PW",
  "NOISE WAVEFORM"
};

static void drawosc(int k)
{
  int x0 = k * 20;
  int ins = osc[k];
  bool on = ins != 0;
  int area0 = ID_OSC1 + k;
  int boxcol = on ? LIGHTBLUE : DARKGREY;
  int state, b, v;
  char text[40];
  unsigned char left = on ? waveleft(ins) : 0;
  int bits = wavebits(left);
  int ctrl = controlvalue(left) & 0x0f;
  int owner = filterowner();
  int frow = (owner >= 0) ? patch_filterrow(osc[owner]) : -1;
  int route = (frow >= 0) ? (rtable[FTBL][frow] & 7) : 0;
  INSTR *p = on ? &instr[ins] : NULL;

  box(x0, PAGE_Y, 20, PAGE_H, boxcol);

  // LED and title: switches the oscillator on / off
  {
    bool lit = on && !oscmute[k];
    const char *ledtip = !on ? "CLICK TO ADD THIS OSCILLATOR (A COPY OF THE FIRST, IN THE NEXT EMPTY INSTRUMENT)" :
      lit ? "CLICK TO MUTE THIS OSCILLATOR WHILE YOU PLAY" : "CLICK TO HEAR THIS OSCILLATOR AGAIN";
    if (area(WID(area0, 1), x0 + 1, PAGE_Y, 16, 1, ledtip, &state))
    {
      if (!on) addosc(k);
      else oscmute[k] = !oscmute[k];
    }
    put(x0 + 1, PAGE_Y, SC_SPACE, boxcol);
    put(x0 + 2, PAGE_Y, lit ? SC_BALL : SC_CIRCLE, lit ? LIGHTRED : (state ? WHITE : GREY));
    put(x0 + 3, PAGE_Y, SC_SPACE, boxcol);
    snprintf(text, sizeof text, "OSCILLATOR %d", k + 1);
    print(x0 + 4, PAGE_Y, text, state == 2 ? YELLOW : (on ? WHITE : GREY));
    put(x0 + 16, PAGE_Y, SC_SPACE, boxcol);
  }

  if (!on)
  {
    print(x0 + 2, PAGE_Y + 2, "OFF", GREY);
    print(x0 + 2, PAGE_Y + 4, "CLICK THE LIGHT", GREY);
    print(x0 + 2, PAGE_Y + 5, "TO ADD IT. IT", GREY);
    print(x0 + 2, PAGE_Y + 6, "PLAYS ON SID", GREY);
    snprintf(text, sizeof text, "VOICE %d", k + 1);
    print(x0 + 2, PAGE_Y + 7, text, GREY);
    return;
  }

  // Instrument number and name
  {
    char name[MAX_INSTRNAMELEN + 1];
    memcpy(name, p->name, MAX_INSTRNAMELEN);
    name[MAX_INSTRNAMELEN - 1] = 0;
    name[13] = 0;
    printfmt(x0 + 2, PAGE_Y + 1, YELLOW, "%02X", ins);
    print(x0 + 5, PAGE_Y + 1, name, oscmute[k] ? GREY : WHITE);
  }

  // Waveforms
  print(x0 + 1, PAGE_Y + 3, "WAVE", LIGHTBLUE);
  for (b = 0; b < 4; b++)
  {
    int y = PAGE_Y + 4 + b * 3;
    bool waveon = (bits & (1 << b)) != 0;
    int col, cx, cy;
    if (area(WID(area0, 0x10 + b), x0 + 1, y, 4, 2, wavetips[b], &state)) togglewave(ins, 0x10 << b);
    col = (state == 2) ? YELLOW : waveon ? LIGHTGREEN : (state ? WHITE : LIGHTBLUE);
    for (cy = 0; cy < 2; cy++)
    {
      for (cx = 0; cx < 4; cx++)
        put(x0 + 1 + cx, y + cy, waveicons[b][cy][cx] | (waveon ? SC_REVERSE : 0), col);
    }
  }

  // Filter, sync, ring, test, hard restart, legato
  if (button(WID(area0, 0x20), x0 + 6, PAGE_Y + 4, "FILT", (route & (1 << k)) != 0, LIGHTGREEN,
    "ROUTE THIS VOICE THROUGH THE FILTER (SEE THE FILTER PANEL)"))
    toggleroute(k);
  if (button(WID(area0, 0x21), x0 + 6, PAGE_Y + 6, "SYNC", (ctrl & 2) != 0, LIGHTGREEN,
    k == 0 ? "HARD SYNC TO OSCILLATOR 3" : k == 1 ? "HARD SYNC TO OSCILLATOR 1" : "HARD SYNC TO OSCILLATOR 2"))
    togglewave(ins, 0x02);
  if (button(WID(area0, 0x22), x0 + 6, PAGE_Y + 8, "RING", (ctrl & 4) != 0, LIGHTGREEN,
    k == 0 ? "RING MODULATION BY OSCILLATOR 3 (USE TRIANGLE)" : k == 1 ? "RING MODULATION BY OSCILLATOR 1 (USE TRIANGLE)" :
    "RING MODULATION BY OSCILLATOR 2 (USE TRIANGLE)"))
    togglewave(ins, 0x04);
  if (button(WID(area0, 0x23), x0 + 6, PAGE_Y + 10, "TEST", (ctrl & 8) != 0, LIGHTGREEN, "THE TEST BIT: RESETS AND HOLDS THE OSCILLATOR"))
    togglewave(ins, 0x08);
  if (button(WID(area0, 0x24), x0 + 6, PAGE_Y + 12, "HR", !(p->gatetimer & 0x80), LIGHTGREEN,
    "HARD RESTART BEFORE EACH NOTE (MORE RELIABLE ATTACK)"))
  {
    openedit();
    p->gatetimer ^= 0x80;
  }
  if (button(WID(area0, 0x25), x0 + 6, PAGE_Y + 14, "LEG", (p->gatetimer & 0x40) != 0, LIGHTGREEN,
    "LEGATO: NO GATE OFF BEFORE THE NEXT NOTE"))
  {
    openedit();
    p->gatetimer ^= 0x40;
  }

  // Pulse width and envelope (the envelope bars alternate in two greens so
  // that the side by side bars stay apart)
  print(x0 + 11, PAGE_Y + 3, "PW", LIGHTBLUE);
  print(x0 + 15, PAGE_Y + 3, "A", LIGHTBLUE);
  print(x0 + 16, PAGE_Y + 3, "D", LIGHTBLUE);
  print(x0 + 17, PAGE_Y + 3, "S", LIGHTBLUE);
  print(x0 + 18, PAGE_Y + 3, "R", LIGHTBLUE);
  v = pulsewidth(ins);
  if (vslider(WID(area0, 0x30), x0 + 11, PAGE_Y + 4, 2, 12, &v, 0, 0xfff, CYAN,
    "PULSE WIDTH (SHIFT+DRAG: FINE). SWEEPS ON THE PULSE PAGE")) setpulsewidth(ins, v);
  {
    static const char *adsrtips[4] =
    {
      "ATTACK: HOW FAST THE NOTE RISES (2 MS - 8 S)",
      "DECAY: HOW FAST IT FALLS TO THE SUSTAIN LEVEL",
      "SUSTAIN: THE LEVEL WHILE THE NOTE IS HELD",
      "RELEASE: HOW FAST IT FADES AFTER THE NOTE"
    };
    int n;
    for (n = 0; n < 4; n++)
    {
      unsigned char *reg = (n < 2) ? &p->ad : &p->sr;
      int shift = (n & 1) ? 0 : 4;
      v = (*reg >> shift) & 15;
      if (vslider(WID(area0, 0x31 + n), x0 + 15 + n, PAGE_Y + 4, 1, 12, &v, 0, 15, (n & 1) ? GREEN : LIGHTGREEN, adsrtips[n], true,
        (n & 1) ? DARKGREY : BLACK))
        *reg = (unsigned char)((*reg & ~(15 << shift)) | (v << shift));
    }
  }
  printfmt(x0 + 11, PAGE_Y + 16, WHITE, "%03X", pulsewidth(ins));
  printfmt(x0 + 15, PAGE_Y + 16, WHITE, "%X%X%X%X", p->ad >> 4, p->ad & 15, p->sr >> 4, p->sr & 15);

  // Lower half: tuning, vibrato, gate timer
  put(x0, PAGE_Y + 17, SC_TEE_RIGHT, boxcol);
  hline(x0 + 1, PAGE_Y + 17, 18, boxcol);
  put(x0 + 19, PAGE_Y + 17, SC_TEE_LEFT, boxcol);
  {
    int row = patch_waverow(ins);
    unsigned char r = (row >= 0) ? rtable[WTBL][row] : 0x80;
    bool fixed = noteisabs(r);
    print(x0 + 1, PAGE_Y + 18, "COARSE", LIGHTBLUE);
    if (fixed)
    {
      v = r & 0x7f;
      if (hslider(WID(area0, 0x40), x0 + 8, PAGE_Y + 18, 7, &v, 1, 92, LIGHTGREEN, "FIXED PITCH: THE NOTE THIS OSCILLATOR ALWAYS PLAYS"))
      {
        row = patch_needwaverow(ins);
        if (row >= 0) rtable[WTBL][row] = (unsigned char)(0x80 | v);
        else tablefull();
      }
    }
    else
    {
      v = noterelative(r);
      if (hslider(WID(area0, 0x40), x0 + 8, PAGE_Y + 18, 7, &v, -24, 24, LIGHTGREEN, "TRANSPOSE IN HALF STEPS (THE WAVETABLE'S FIRST NOTE)"))
      {
        row = patch_needwaverow(ins);
        if (row >= 0) rtable[WTBL][row] = relativenote(v);
        else tablefull();
      }
    }
    notetext((row >= 0) ? rtable[WTBL][row] : 0x80, text, sizeof text);
    print(x0 + 16, PAGE_Y + 18, text, WHITE);
    if (button(WID(area0, 0x41), x0 + 8, PAGE_Y + 19, "FIXED", fixed, LIGHTGREEN, "FIXED PITCH: PLAY THE SAME NOTE WHATEVER KEY (DRUMS)"))
    {
      openedit();
      row = patch_needwaverow(ins);
      if (row < 0) tablefull();
      else if (fixed) rtable[WTBL][row] = 0x00;
      else rtable[WTBL][row] = (unsigned char)(0x80 | clampint(48 + noterelative(rtable[WTBL][row]), 1, 92));
    }
  }

  {
    int sp = p->ptr[STBL];
    int vl = sp ? ltable[STBL][sp - 1] : 0;
    int vr = sp ? rtable[STBL][sp - 1] : 0;

    print(x0 + 1, PAGE_Y + 21, "VIB DLY", LIGHTBLUE);
    v = p->vibdelay;
    if (hslider(WID(area0, 0x42), x0 + 8, PAGE_Y + 21, 7, &v, 0, 255, LIGHTGREEN, "VIBRATO DELAY: FRAMES BEFORE THE VIBRATO STARTS (0 = NONE)"))
      p->vibdelay = (unsigned char)v;
    printfmt(x0 + 16, PAGE_Y + 21, WHITE, "%02X", p->vibdelay);

    print(x0 + 1, PAGE_Y + 23, "VIB SPD", LIGHTBLUE);
    v = vl;
    if (hslider(WID(area0, 0x43), x0 + 8, PAGE_Y + 23, 7, &v, 0, 255, LIGHTGREEN, "VIBRATO SPEED (HALF PERIOD IN FRAMES; 80+: DEPTH IN NOTE STEPS)"))
    {
      if (!patch_setvibrato(ins, v, vr)) tablefull();
      if ((!p->vibdelay) && (v)) p->vibdelay = 1;
    }
    printfmt(x0 + 16, PAGE_Y + 23, WHITE, "%02X", vl);

    print(x0 + 1, PAGE_Y + 25, "VIB DEP", LIGHTBLUE);
    v = vr;
    if (hslider(WID(area0, 0x44), x0 + 8, PAGE_Y + 25, 7, &v, 0, 255, LIGHTGREEN, "VIBRATO DEPTH"))
    {
      if (!patch_setvibrato(ins, vl, v)) tablefull();
      if ((!p->vibdelay) && (v)) p->vibdelay = 1;
    }
    printfmt(x0 + 16, PAGE_Y + 25, WHITE, "%02X", vr);
  }

  print(x0 + 1, PAGE_Y + 27, "GATE", LIGHTBLUE);
  v = p->gatetimer & 0x3f;
  if (hslider(WID(area0, 0x45), x0 + 8, PAGE_Y + 27, 7, &v, 1, 0x3f, LIGHTGREEN, "GATE TIMER: FRAMES OF GATE OFF / HARD RESTART BEFORE THE NEXT NOTE"))
    p->gatetimer = (unsigned char)((p->gatetimer & 0xc0) | v);
  printfmt(x0 + 16, PAGE_Y + 27, WHITE, "%02X", p->gatetimer & 0x3f);

  // Where the instrument's tables start
  printfmt(x0 + 1, PAGE_Y + 28, GREY, "W%02X P%02X F%02X S%02X", p->ptr[WTBL], p->ptr[PTBL], p->ptr[FTBL], p->ptr[STBL]);
}

static void drawfilterpanel(void)
{
  int x0 = 60;
  int owner = filterowner();
  int fi = owner >= 0 ? osc[owner] : 0;
  int frow = fi ? patch_filterrow(fi) : -1;
  int crow = fi ? patch_cutoffrow(fi) : -1;
  int type = (frow >= 0) ? (ltable[FTBL][frow] & 0x70) : 0;
  int res = (frow >= 0) ? (rtable[FTBL][frow] >> 4) : 0;
  int route = (frow >= 0) ? (rtable[FTBL][frow] & 7) : 0;
  int cutoff = (crow >= 0) ? rtable[FTBL][crow] : 0;
  bool any = firstosc() >= 0;
  int v, n, state;
  char text[40];

  box(x0, PAGE_Y, 20, PAGE_H, LIGHTBLUE, "FILTER", WHITE);
  if (fi) printfmt(x0 + 2, PAGE_Y + 1, GREY, "ON OSC %d, INS %02X", owner + 1, fi);
  else print(x0 + 2, PAGE_Y + 1, "NOT USED", GREY);

  print(x0 + 1, PAGE_Y + 3, "CUT", LIGHTBLUE);
  print(x0 + 5, PAGE_Y + 3, "RES", LIGHTBLUE);
  print(x0 + 9, PAGE_Y + 3, "TYPE", LIGHTBLUE);
  print(x0 + 14, PAGE_Y + 3, "VOICE", LIGHTBLUE);

  v = cutoff;
  if (vslider(WID(ID_FILTER, 1), x0 + 1, PAGE_Y + 4, 2, 12, &v, 0, 255, YELLOW, "FILTER CUTOFF FREQUENCY (SWEEPS ON THE FILTER PAGE)", any))
  {
    fi = needfilter(1 << firstosc());
    crow = fi ? patch_cutoffrow(fi) : -1;
    if (crow >= 0) rtable[FTBL][crow] = (unsigned char)v;
  }
  v = res;
  if (vslider(WID(ID_FILTER, 2), x0 + 6, PAGE_Y + 4, 1, 12, &v, 0, 15, YELLOW, "FILTER RESONANCE", any))
  {
    fi = needfilter(1 << firstosc());
    frow = fi ? patch_filterrow(fi) : -1;
    if (frow >= 0) rtable[FTBL][frow] = (unsigned char)((rtable[FTBL][frow] & 0x0f) | (v << 4));
  }
  {
    static const char *typelabels[3] = { "LP", "BP", "HP" };
    static const char *typetips[3] = { "LOW PASS", "BAND PASS", "HIGH PASS" };
    for (n = 0; n < 3; n++)
    {
      if (button(WID(ID_FILTER, 3 + n), x0 + 9, PAGE_Y + 4 + n * 2, typelabels[n], (type & (0x10 << n)) != 0, YELLOW, typetips[n], any))
      {
        openedit();
        fi = needfilter(1 << firstosc());
        frow = fi ? patch_filterrow(fi) : -1;
        if (frow >= 0) ltable[FTBL][frow] ^= (unsigned char)(0x10 << n);
      }
    }
  }
  for (n = 0; n < 3; n++)
  {
    snprintf(text, sizeof text, " %d ", n + 1);
    if (button(WID(ID_FILTER, 6 + n), x0 + 15, PAGE_Y + 4 + n * 2, text, (route & (1 << n)) != 0, YELLOW,
      "SID VOICES THAT GO THROUGH THE FILTER (OSCILLATOR N PLAYS ON VOICE N)", any))
      toggleroute(n);
  }
  printfmt(x0 + 1, PAGE_Y + 16, WHITE, "%02X", cutoff);
  printfmt(x0 + 6, PAGE_Y + 16, WHITE, "%X", res);

  put(x0, PAGE_Y + 17, SC_TEE_RIGHT, LIGHTBLUE);
  hline(x0 + 1, PAGE_Y + 17, 18, LIGHTBLUE);
  put(x0 + 19, PAGE_Y + 17, SC_TEE_LEFT, LIGHTBLUE);

  // The chip display: click to switch the chip and the clock
  fill(x0 + 1, PAGE_Y + 19, 18, 3, SC_SPACE, LIGHTGREEN, BLACK);
  if (area(WID(ID_FILTER, 10), x0 + 1, PAGE_Y + 19, 18, 1, "THE EMULATED SID CHIP. CLICK TO SWITCH 6581 / 8580", &state))
    editorsetting(&editorInfo.sidmodel, editorInfo.sidmodel ^ 1, true);
  print(x0 + 2, PAGE_Y + 19, editorInfo.sidmodel ? "MOS 8580 R5" : "MOS 6581 R4", state ? YELLOW : LIGHTGREEN, false, BLACK);
  if (area(WID(ID_FILTER, 11), x0 + 1, PAGE_Y + 20, 18, 1, "PAL (50 HZ) OR NTSC (60 HZ). CLICK TO SWITCH", &state))
    editorsetting(&editorInfo.ntsc, editorInfo.ntsc ^ 1, true);
  print(x0 + 2, PAGE_Y + 20, editorInfo.ntsc ? "NTSC 1.023 MHZ" : "PAL  0.985 MHZ", state ? YELLOW : LIGHTGREEN, false, BLACK);
  print(x0 + 2, PAGE_Y + 21, (interpolate & 2) ? "RESID-FP" : (interpolate & 1) ? "RESID INTERP." : "RESID FAST", GREEN, false, BLACK);

  // Cutoff over time
  print(x0 + 1, PAGE_Y + 23, "CUTOFF SWEEP", LIGHTBLUE);
  if (fi)
  {
    unsigned char cut[64];
    int c;
    patch_simfilter(fi, 64, cut, NULL);
    for (c = 0; c < 16; c++)
    {
      int m = cut[c * 4];
      int j;
      for (j = 1; j < 4; j++)
      {
        if (cut[c * 4 + j] > m) m = cut[c * 4 + j];
      }
      vbar(x0 + 2 + c, PAGE_Y + 24, 1, 4, m / 255.0f, YELLOW, BLACK);
    }
  }
  else fill(x0 + 2, PAGE_Y + 24, 16, 4, SC_SPACE, BLACK, BLACK);
}

static void drawpatchpage(void)
{
  int k;
  for (k = 0; k < PATCH_OSC; k++) drawosc(k);
  drawfilterpanel();
}

/*
 * WAVE / PULSE / FILTER pages: the table parts as steps
 */

// The instrument's part: rows from start, len rows, the ending FF row (-1 if
// none). Returns the number of steps (rows before the FF), -1 if no part
static int partinfo(int t, int ins, int *start, int *len, int *ffrow)
{
  *start = patch_partstart(t, ins);
  *len = 0;
  *ffrow = -1;
  if (*start < 0) return -1;
  *len = patch_partlen(t, *start);
  if ((*len > 0) && (ltable[t][*start + *len - 1] == 0xff)) *ffrow = *start + *len - 1;
  return (*ffrow >= 0) ? *len - 1 : *len;
}

// Row 8: oscillator choice and the instrument's table part. Returns the
// instrument, 0 if the oscillator is off
static int seqheader(int t, const char *tablename)
{
  int k, start, len, ffrow;
  int ins;
  char text[40];

  print(1, PAGE_Y, "OSCILLATOR", LIGHTBLUE);
  for (k = 0; k < PATCH_OSC; k++)
  {
    snprintf(text, sizeof text, " %d ", k + 1);
    if (button(WID(ID_SEQHEAD, k), 12 + k * 4, PAGE_Y, text, seqosc == k, osc[k] ? WHITE : GREY, "CHOOSE THE OSCILLATOR (INSTRUMENT) TO EDIT"))
    {
      seqosc = k;
      selstep[page] = -1;
      scrollpos[page] = 0;
    }
  }
  ins = osc[seqosc];
  if (!ins)
  {
    print(26, PAGE_Y, "THIS OSCILLATOR IS OFF", GREY);
    if (button(WID(ID_SEQHEAD, 5), 50, PAGE_Y, " ADD IT ", true, LIGHTGREEN, "ADD THE OSCILLATOR (A COPY OF THE FIRST ONE)")) addosc(seqosc);
    return 0;
  }
  {
    char name[MAX_INSTRNAMELEN + 1];
    memcpy(name, instr[ins].name, MAX_INSTRNAMELEN);
    name[MAX_INSTRNAMELEN - 1] = 0;
    printfmt(26, PAGE_Y, YELLOW, "%02X", ins);
    print(29, PAGE_Y, name, WHITE);
  }
  if (partinfo(t, ins, &start, &len, &ffrow) >= 0)
  {
    printfmt(44, PAGE_Y, GREY, "%s %02X-%02X", tablename, start + 1, start + len);
    if (patch_isshared(t, ins))
    {
      print(63, PAGE_Y, "SHARED", LIGHTRED);
      if (button(WID(ID_SEQHEAD, 6), 70, PAGE_Y, "UNSHARE", false, WHITE, "OTHER INSTRUMENTS USE THESE ROWS TOO. CLICK TO GIVE THIS ONE ITS OWN COPY"))
      {
        openedit();
        if (!patch_unshare(t, ins)) tablefull();
      }
    }
  }
  return ins;
}

// Inserts a row into a part before pos (keeps the instrument's pointer on the
// first row when inserting there)
static bool insertrow(int t, int start, int pos, unsigned char l, unsigned char r)
{
  openedit();
  if (!patch_insertrow(t, pos, l, r, pos == start))
  {
    tablefull();
    return false;
  }
  return true;
}

static void deleterow(int t, int steps, int pos)
{
  if (steps <= 1)
  {
    gui_message("The last step can't be deleted");
    return;
  }
  openedit();
  patch_deleterow(t, pos);
}

// The loop (jump) at the end of a part: LOOP TO step / END
static void looptext(int start, int len, unsigned char target, char *out, int size)
{
  if (!target) snprintf(out, size, "END");
  else if ((target >= start + 1) && (target <= start + len)) snprintf(out, size, "LOOP TO %02X", target - start);
  else snprintf(out, size, "JUMP TO ROW %02X", target);
}

// Scroll buttons for a list of n items with visible shown
static void scrollbuttons(int x, int y, int n, int visible)
{
  int *pos = &scrollpos[page];
  if (n <= visible)
  {
    *pos = 0;
    return;
  }
  if (*pos > n - visible) *pos = n - visible;
  if (button(WID(ID_SEQBTN, 0x40), x, y, "<<", false, WHITE, "SCROLL BACK")) *pos -= visible / 2;
  if (button(WID(ID_SEQBTN, 0x41), x + 3, y, ">>", false, WHITE, "SCROLL ON")) *pos += visible / 2;
  if (*pos > n - visible) *pos = n - visible;
  if (*pos < 0) *pos = 0;
}

// Wave presets (rows of the part; relative jumps)
struct WavePreset
{
  const char *label;
  const char *tip;
  int rows;
  unsigned char l[8], r[8];
  int ad, sr;      // -1 = keep
};

static const WavePreset wavepresets[] =
{
  { "INIT", "A PLAIN PULSE WAVE", 2, { 0x41, 0xff }, { 0x00, 0x00 }, -1, -1 },
  { "ARP MAJ", "MAJOR CHORD ARPEGGIO (0 4 7)", 4, { 0x41, 0x41, 0x41, 0xff }, { 0x00, 0x04, 0x07, 0x01 }, -1, -1 },
  { "ARP MIN", "MINOR CHORD ARPEGGIO (0 3 7)", 4, { 0x41, 0x41, 0x41, 0xff }, { 0x00, 0x03, 0x07, 0x01 }, -1, -1 },
  { "OCTAVE", "OCTAVE ARPEGGIO (0 12)", 3, { 0x41, 0x41, 0xff }, { 0x00, 0x0c, 0x01 }, -1, -1 },
  { "KICK", "BASS DRUM: NOISE CLICK, THEN A FALLING TRIANGLE", 6, { 0x81, 0x11, 0x11, 0x11, 0x10, 0xff }, { 0xc8, 0xa8, 0xa4, 0xa0, 0x9c, 0x00 }, 0x08, 0x00 },
  { "SNARE", "SNARE DRUM: PULSE AND NOISE", 6, { 0x81, 0x41, 0x81, 0x81, 0x80, 0xff }, { 0xd0, 0xa8, 0xc8, 0xc4, 0xc0, 0x00 }, 0x09, 0x00 },
  { "HIHAT", "HI-HAT: SHORT HIGH NOISE", 2, { 0x81, 0xff }, { 0xdc, 0x00 }, 0x04, 0x00 }
};

static void drawwavepage(void)
{
  int ins = seqheader(WTBL, "WAVETABLE");
  int start, len, ffrow, steps, s, b, state;
  int playrow = -1;
  int *sel = &selstep[PAGE_WAVE];
  int *scroll = &scrollpos[PAGE_WAVE];
  const int visible = 18;
  static const char *bitlabels[8] = { "NOI", "PUL", "SAW", "TRI", "TST", "RNG", "SYN", "GAT" };
  static const char *bittips[8] =
  {
    "NOISE WAVEFORM ON THIS STEP", "PULSE WAVEFORM ON THIS STEP", "SAWTOOTH WAVEFORM ON THIS STEP", "TRIANGLE WAVEFORM ON THIS STEP",
    "TEST BIT ON THIS STEP", "RING MODULATION ON THIS STEP", "HARD SYNC ON THIS STEP", "GATE (THE NOTE SOUNDS) ON THIS STEP"
  };
  char text[40];

  if (!ins) return;
  steps = partinfo(WTBL, ins, &start, &len, &ffrow);
  if (steps < 0)
  {
    print(1, PAGE_Y + 3, "THIS INSTRUMENT HAS NO WAVETABLE: IT IS SILENT.", WHITE);
    if (button(WID(ID_SEQBTN, 1), 1, PAGE_Y + 5, " CREATE ONE ", true, LIGHTGREEN, "MAKE A WAVETABLE WITH A PULSE WAVE"))
    {
      openedit();
      if (patch_needwaverow(ins) < 0) tablefull();
    }
    return;
  }
  if ((chn[seqosc].instr == ins) && (chn[seqosc].ptr[WTBL])) playrow = chn[seqosc].ptr[WTBL] - 1;
  if (*sel >= steps) *sel = steps - 1;

  print(1, PAGE_Y + 2, "STEP", LIGHTBLUE);
  print(1, PAGE_Y + 3, "TYPE", LIGHTBLUE);
  for (b = 0; b < 8; b++) print(1, PAGE_Y + 4 + b, bitlabels[b], LIGHTBLUE);
  print(1, PAGE_Y + 12, "NOTE", LIGHTBLUE);
  print(1, PAGE_Y + 13, "+32", GREY);
  print(1, PAGE_Y + 16, "+0", GREY);
  print(1, PAGE_Y + 20, "-32", GREY);
  print(1, PAGE_Y + 22, "FIX", LIGHTBLUE);
  print(1, PAGE_Y + 23, "LOOP", LIGHTBLUE);

  scrollbuttons(74, PAGE_Y + 25, steps, visible);
  for (s = *scroll; (s < steps) && (s < *scroll + visible); s++)
  {
    int row = start + s;
    int x = 8 + (s - *scroll) * 4;
    unsigned char l = ltable[WTBL][row];
    unsigned char r = rtable[WTBL][row];
    bool iswave = (l >= 0x10) && (l < WAVECMD);
    bool iswait = (l >= WAVEDELAY) && (l <= WAVELASTDELAY);
    bool iscmd = (l >= WAVECMD) && (l <= WAVELASTCMD);
    int col;
    int v;

    // Step number: click to select
    snprintf(text, sizeof text, "%02X", s + 1);
    if (area(WID(ID_STEP, s), x, PAGE_Y + 2, 2, 1, "CLICK TO SELECT THE STEP (FOR INSERT / DELETE)", &state)) *sel = (*sel == s) ? -1 : s;
    col = (row == playrow) ? YELLOW : (*sel == s) ? WHITE : (state ? WHITE : LIGHTBLUE);
    print(x, PAGE_Y + 2, text, col, (row == playrow) || (*sel == s));

    // Type: click to switch wave / wait / no change
    if (area(WID(ID_KIND, s), x, PAGE_Y + 3, 2, 1, iscmd ? "A COMMAND STEP (EDIT IT IN THE TRACKER'S TABLES)" :
      "STEP TYPE: WV = SET WAVEFORM, WT = WAIT, -- = NO CHANGE. CLICK TO SWITCH", &state, !iscmd))
    {
      openedit();
      if (iswave) ltable[WTBL][row] = 0x01;
      else if (iswait) ltable[WTBL][row] = 0x00;
      else ltable[WTBL][row] = 0x41;
      l = ltable[WTBL][row];
      iswave = (l >= 0x10) && (l < WAVECMD);
      iswait = (l >= WAVEDELAY) && (l <= WAVELASTDELAY);
    }
    print(x, PAGE_Y + 3, iswave ? "WV" : iswait ? "WT" : iscmd ? "CM" : "--", state ? YELLOW : GREY);

    // Waveform and control bits
    if (iscmd)
    {
      printfmt(x, PAGE_Y + 4, WHITE, "F%X", l & 0x0f);
      printfmt(x, PAGE_Y + 5, WHITE, "%02X", r);
    }
    else if (iswait)
    {
      v = l;
      snprintf(text, sizeof text, "%02X", l);
      if (dragvalue(WID(ID_BITS0, s), x, PAGE_Y + 4, 2, &v, 1, 15, 1, text, WHITE, "WAIT THIS MANY FRAMES (DRAG OR WHEEL)"))
        ltable[WTBL][row] = (unsigned char)v;
      print(x, PAGE_Y + 5, "FR", GREY);
    }
    else
    {
      int value = controlvalue(l);
      for (b = 0; b < 8; b++)
      {
        int bit = 0x80 >> b;
        bool on = (value & bit) != 0;
        if (area(WID(ID_BITS0 + b, s), x, PAGE_Y + 4 + b, 2, 1, bittips[b], &state))
        {
          int left = waverowleft((iswave ? value : 0x01) ^ bit);
          if (left < 0) gui_message("Noise can't be combined with saw and pulse in the wavetable");
          else
          {
            openedit();
            ltable[WTBL][row] = (unsigned char)left;
          }
        }
        put(x + 1, PAGE_Y + 4 + b, (iswave && on) ? SC_BALL : SC_CIRCLE,
          state == 2 ? YELLOW : (iswave && on) ? (b < 4 ? YELLOW : LIGHTGREEN) : (state ? WHITE : DARKGREY));
      }
    }

    // Note: drag the text or the lane
    if (!iscmd)
    {
      notetext(r, text, sizeof text);
      if (noteisabs(r))
      {
        v = r & 0x7f;
        if (dragvalue(WID(ID_NOTE, s), x, PAGE_Y + 12, 3, &v, 1, 92, 1, text, WHITE, "FIXED NOTE OF THIS STEP (DRAG OR WHEEL)"))
          rtable[WTBL][row] = (unsigned char)(0x80 | v);
      }
      else
      {
        v = noterelative(r);
        if (dragvalue(WID(ID_NOTE, s), x, PAGE_Y + 12, 3, &v, -32, 95, 1, text, (s & 1) ? LIGHTGREY : WHITE,
          "NOTE OF THIS STEP IN HALF STEPS FROM THE PLAYED NOTE. -- = NO PITCH CHANGE"))
          rtable[WTBL][row] = relativenote(v);
      }

      // The lane: a bar up or down from the middle
      {
        bool hovered = over((float)x, PAGE_Y + 13.0f, 3.0f, 8.0f);
        int rel = noteisabs(r) || (r == 0x80) ? 0 : noterelative(r);
        int cy;
        if (hovered)
        {
          tip("DRAG UP / DOWN TO SET THE STEP'S NOTE (ARPEGGIO)");
          if (in.clicked) startdrag(WID(ID_LANE, s), rel);
        }
        if ((active == WID(ID_LANE, s)) && (in.down))
        {
          int nv = clampint((int)floorf((PAGE_Y + 17.0f - in.y) * 8.0f + 0.5f), -32, 32);
          rtable[WTBL][row] = relativenote(nv);
          rel = nv;
        }
        for (cy = 0; cy < 8; cy++) put(x + 1, PAGE_Y + 13 + cy, SC_SPACE, BLACK, BLACK);
        if ((r != 0x80) && (!noteisabs(r)))
        {
          if (rel > 0) vbar(x + 1, PAGE_Y + 13, 1, 4, clampint(rel, 0, 32) / 32.0f, LIGHTGREEN, BLACK);
          if (rel < 0)
          {
            // Downwards: upper eighth blocks
            static const int downfill[9] =
            {
              SC_SPACE, SC_EIGHTH_UPPER, SC_QUARTER_UPPER, SC_3EIGHTHS_UPPER, SC_HALF_LOWER | SC_REVERSE,
              SC_3EIGHTHS_LOWER | SC_REVERSE, SC_QUARTER_LOWER | SC_REVERSE, SC_EIGHTH_LOWER | SC_REVERSE, SC_BLOCK
            };
            int e = clampint(-rel, 0, 32);
            for (cy = 0; cy < 4; cy++)
            {
              int f = clampint(e - cy * 8, 0, 8);
              put(x + 1, PAGE_Y + 17 + cy, downfill[f], LIGHTRED, BLACK);
            }
          }
          if (!rel) put(x + 1, PAGE_Y + 16, SC_EIGHTH_LOWER, GREY, BLACK);
        }
        else if (noteisabs(r)) put(x + 1, PAGE_Y + 16, 'F' - '@', YELLOW, BLACK);
      }

      // Fixed note on / off
      if (area(WID(ID_FIX, s), x, PAGE_Y + 22, 2, 1, "FIXED: THE STEP PLAYS THE SAME NOTE WHATEVER KEY (DRUMS)", &state))
      {
        openedit();
        if (noteisabs(r)) rtable[WTBL][row] = 0x00;
        else rtable[WTBL][row] = (unsigned char)(0x80 | clampint(48 + noterelative(r), 1, 92));
      }
      put(x + 1, PAGE_Y + 22, noteisabs(rtable[WTBL][row]) ? SC_BALL : SC_CIRCLE,
        state == 2 ? YELLOW : noteisabs(rtable[WTBL][row]) ? YELLOW : (state ? WHITE : DARKGREY));
    }

    // Loop target
    if (ffrow >= 0)
    {
      unsigned char target = rtable[WTBL][ffrow];
      bool here = target == row + 1;
      if (area(WID(ID_LOOP, s), x, PAGE_Y + 23, 2, 1, "CLICK: THE WAVETABLE LOOPS BACK TO THIS STEP (CLICK AGAIN: NO LOOP)", &state))
      {
        openedit();
        rtable[WTBL][ffrow] = here ? 0 : (unsigned char)(row + 1);
      }
      put(x + 1, PAGE_Y + 23, here ? SC_ARROWUP : SC_HLINE, here ? YELLOW : (state ? WHITE : DARKGREY));
    }
  }

  // Where the part ends
  if (ffrow >= 0)
  {
    looptext(start, len, rtable[WTBL][ffrow], text, sizeof text);
    printfmt(8, PAGE_Y + 25, GREY, "AFTER STEP %02X: %s", steps, text);
  }
  else print(8, PAGE_Y + 25, "NO END ROW (FF) - FIX IT IN THE TRACKER", LIGHTRED);

  // Step buttons
  if (button(WID(ID_SEQBTN, 2), 1, PAGE_Y + 27, "+STEP", false, WHITE, "ADD A STEP AT THE END (A COPY OF THE LAST)"))
  {
    int pos = (ffrow >= 0) ? ffrow : start + len;
    int src = (steps > 0) ? start + steps - 1 : -1;
    insertrow(WTBL, start, pos, src >= 0 ? ltable[WTBL][src] : 0x41, src >= 0 ? rtable[WTBL][src] : 0x80);
  }
  if (button(WID(ID_SEQBTN, 3), 7, PAGE_Y + 27, "-STEP", false, WHITE, "REMOVE THE LAST STEP"))
    deleterow(WTBL, steps, start + steps - 1);
  if (button(WID(ID_SEQBTN, 4), 13, PAGE_Y + 27, "INSERT", false, WHITE, "INSERT A STEP BEFORE THE SELECTED ONE", *sel >= 0))
    insertrow(WTBL, start, start + *sel, ltable[WTBL][start + *sel], rtable[WTBL][start + *sel]);
  if (button(WID(ID_SEQBTN, 5), 20, PAGE_Y + 27, "DELETE", false, WHITE, "DELETE THE SELECTED STEP", *sel >= 0))
  {
    deleterow(WTBL, steps, start + *sel);
    *sel = -1;
  }

  // Presets
  print(1, PAGE_Y + 29, "PRESETS", LIGHTBLUE);
  {
    int x = 9;
    unsigned n;
    for (n = 0; n < sizeof wavepresets / sizeof wavepresets[0]; n++)
    {
      const WavePreset *wp = &wavepresets[n];
      if (button(WID(ID_SEQBTN, 0x10 + n), x, PAGE_Y + 29, wp->label, false, WHITE, wp->tip))
      {
        openedit();
        if (!patch_setpart(WTBL, ins, wp->l, wp->r, wp->rows)) tablefull();
        else
        {
          if (wp->ad >= 0) instr[ins].ad = (unsigned char)wp->ad;
          if (wp->sr >= 0) instr[ins].sr = (unsigned char)wp->sr;
          *sel = -1;
        }
      }
      x += (int)strlen(wp->label) + 2;
    }
  }
}

// Heading of the step list of the PULSE and FILTER pages (one table row a
// line)
static void steplistheader(void)
{
  print(1, PAGE_Y + 15, "  STEP  TYPE", LIGHTBLUE);
}

// Graph of values over time (2 frames per column): 72 columns x 12 rows
static void drawgraph(const int *values, int maxv, int col, const char *top, const char *mid)
{
  int c;

  print(1, PAGE_Y + 2, top, GREY);
  print(1, PAGE_Y + 7, mid, GREY);
  print(1, PAGE_Y + 13, "0", GREY);
  for (c = 0; c < 72; c++)
  {
    int v = values[c * 2] > values[c * 2 + 1] ? values[c * 2] : values[c * 2 + 1];
    vbar(8 + c, PAGE_Y + 2, 1, 12, (float)v / maxv, col, BLACK);
  }
  // Time: PAL frames, 25 columns a second
  print(8, PAGE_Y + 14, "0S", GREY);
  print(33, PAGE_Y + 14, "1S", GREY);
  print(58, PAGE_Y + 14, "2S", GREY);
}

static void signedtext(int v, char *out, int size)
{
  snprintf(out, size, "%c%02X", v < 0 ? '-' : '+', v < 0 ? -v : v);
}

static int signedvalue(unsigned char r)
{
  return r < 0x80 ? r : (int)r - 0x100;
}

static void drawpulsepage(void)
{
  int ins = seqheader(PTBL, "PULSETABLE");
  int start, len, ffrow, steps, i, state;
  int playrow = -1;
  int *sel = &selstep[PAGE_PULSE];
  int *scroll = &scrollpos[PAGE_PULSE];
  const int visible = 10;
  char text[40];

  if (!ins) return;
  steps = partinfo(PTBL, ins, &start, &len, &ffrow);
  if (steps < 0)
  {
    print(1, PAGE_Y + 3, "NO PULSETABLE: THE PULSE WIDTH IS NOT SET.", WHITE);
    if (button(WID(ID_SEQBTN, 1), 1, PAGE_Y + 5, " CREATE ONE ", true, LIGHTGREEN, "MAKE A PULSETABLE THAT SETS THE WIDTH TO 800"))
    {
      openedit();
      if (patch_needpulserow(ins, 0x800) < 0) tablefull();
    }
    return;
  }
  if ((chn[seqosc].instr == ins) && (chn[seqosc].ptr[PTBL])) playrow = chn[seqosc].ptr[PTBL] - 1;

  {
    unsigned short pw[144];
    int values[144];
    int f;
    patch_simpulse(ins, 144, pw, NULL);
    for (f = 0; f < 144; f++) values[f] = pw[f];
    drawgraph(values, 0xfff, CYAN, "FFF", "800");
  }

  if (*sel >= len) *sel = -1;
  steplistheader();
  scrollbuttons(74, PAGE_Y + 15, len, visible);
  for (i = *scroll; (i < len) && (i < *scroll + visible); i++)
  {
    int row = start + i;
    int y = PAGE_Y + 16 + (i - *scroll);
    unsigned char l = ltable[PTBL][row];
    unsigned char r = rtable[PTBL][row];
    int v;

    if (row == playrow) put(1, y, '>', YELLOW);
    snprintf(text, sizeof text, "%02X", i + 1);
    if (area(WID(ID_LISTNUM, i), 3, y, 2, 1, "CLICK TO SELECT THE STEP", &state)) *sel = (*sel == i) ? -1 : i;
    print(3, y, text, (*sel == i) ? WHITE : (state ? WHITE : LIGHTBLUE), *sel == i);

    if (l == 0xff)
    {
      int target = r ? r - start : 0;
      print(7, y, "LOOP", LIGHTBLUE);
      looptext(start, len, r, text, sizeof text);
      v = target;
      if (dragvalue(WID(ID_LISTA, i), 13, y, 16, &v, 0, steps, 1, text, WHITE, "WHERE IT GOES AFTER THE LAST STEP: A STEP TO LOOP TO, OR END"))
        rtable[PTBL][row] = (unsigned char)(v ? start + v : 0);
    }
    else if (l >= 0x80)
    {
      if (area(WID(ID_LISTKIND, i), 7, y, 4, 1, "SET THE PULSE WIDTH. CLICK TO MAKE IT A SWEEP STEP", &state))
      {
        openedit();
        ltable[PTBL][row] = 0x20;
        rtable[PTBL][row] = 0x10;
      }
      print(7, y, "SET", state ? YELLOW : WHITE);
      v = ((l & 0x0f) << 8) | r;
      snprintf(text, sizeof text, "%03X", v);
      if (dragvalue(WID(ID_LISTA, i), 13, y, 4, &v, 0, 0xfff, 0x10, text, WHITE, "PULSE WIDTH (DRAG OR WHEEL, SHIFT: FINE)"))
      {
        ltable[PTBL][row] = (unsigned char)(0x80 | (v >> 8));
        rtable[PTBL][row] = (unsigned char)(v & 0xff);
      }
      if (hslider(WID(ID_LISTB, i), 18, y, 48, &v, 0, 0xfff, CYAN, "PULSE WIDTH"))
      {
        ltable[PTBL][row] = (unsigned char)(0x80 | (v >> 8));
        rtable[PTBL][row] = (unsigned char)(v & 0xff);
      }
    }
    else
    {
      int sp = signedvalue(r);
      if (area(WID(ID_LISTKIND, i), 7, y, 4, 1, "SWEEP THE PULSE WIDTH. CLICK TO MAKE IT A SET STEP", &state))
      {
        openedit();
        ltable[PTBL][row] = 0x88;
        rtable[PTBL][row] = 0x00;
      }
      print(7, y, "SWEEP", state ? YELLOW : WHITE);
      v = l;
      snprintf(text, sizeof text, "%02X FRAMES", l);
      if (dragvalue(WID(ID_LISTA, i), 13, y, 10, &v, 1, 0x7f, 1, text, WHITE, "HOW MANY FRAMES THE SWEEP LASTS"))
        ltable[PTBL][row] = (unsigned char)v;
      v = sp;
      signedtext(sp, text, sizeof text);
      print(25, y, "BY", LIGHTBLUE);
      if (dragvalue(WID(ID_LISTB, i), 28, y, 4, &v, -128, 127, 1, text, WHITE, "CHANGE OF THE PULSE WIDTH EACH FRAME"))
        rtable[PTBL][row] = (unsigned char)(v & 0xff);
      signedtext(sp * l, text, sizeof text);
      printfmt(34, y, GREY, "= %s IN ALL", text);
    }
  }

  if (button(WID(ID_SEQBTN, 2), 1, PAGE_Y + 27, "+SET", false, WHITE, "ADD A STEP THAT SETS THE PULSE WIDTH"))
    insertrow(PTBL, start, ffrow >= 0 ? ffrow : start + len, 0x88, 0x00);
  if (button(WID(ID_SEQBTN, 3), 6, PAGE_Y + 27, "+SWEEP", false, WHITE, "ADD A SWEEP STEP"))
    insertrow(PTBL, start, ffrow >= 0 ? ffrow : start + len, 0x20, 0x10);
  if (button(WID(ID_SEQBTN, 4), 13, PAGE_Y + 27, "DELETE", false, WHITE, "DELETE THE SELECTED STEP", (*sel >= 0) && (start + *sel != ffrow)))
  {
    deleterow(PTBL, steps, start + *sel);
    *sel = -1;
  }

  // The sweep maker: up and down between two widths, looping
  print(1, PAGE_Y + 29, "MAKE A SWEEP", LIGHTBLUE);
  snprintf(text, sizeof text, "FROM %03X", psweeplo);
  dragvalue(WID(ID_SWEEP, 1), 14, PAGE_Y + 29, 8, &psweeplo, 0, 0xfff, 0x10, text, WHITE, "LOWEST PULSE WIDTH OF THE SWEEP");
  snprintf(text, sizeof text, "TO %03X", psweephi);
  dragvalue(WID(ID_SWEEP, 2), 24, PAGE_Y + 29, 6, &psweephi, 0, 0xfff, 0x10, text, WHITE, "HIGHEST PULSE WIDTH OF THE SWEEP");
  snprintf(text, sizeof text, "SPEED %02X", psweepspeed);
  dragvalue(WID(ID_SWEEP, 3), 32, PAGE_Y + 29, 8, &psweepspeed, 1, 0x7f, 1, text, WHITE, "HOW MUCH THE WIDTH CHANGES EACH FRAME");
  if (button(WID(ID_SWEEP, 4), 42, PAGE_Y + 29, " MAKE IT ", true, LIGHTGREEN, "REPLACE THE PULSETABLE WITH THIS SWEEP (UP, DOWN, LOOP)"))
  {
    unsigned char ls[32], rs[32];
    int n = 0, half, total;
    if (psweephi <= psweeplo) gui_message("The sweep needs TO above FROM");
    else
    {
      total = (psweephi - psweeplo) / psweepspeed;
      if (total < 1) total = 1;
      ls[n] = (unsigned char)(0x80 | (psweeplo >> 8));
      rs[n++] = (unsigned char)(psweeplo & 0xff);
      for (half = 0; half < 2; half++)
      {
        int left = total;
        while ((left > 0) && (n < 30))
        {
          int t = left > 0x7f ? 0x7f : left;
          ls[n] = (unsigned char)t;
          rs[n++] = (unsigned char)((half ? -psweepspeed : psweepspeed) & 0xff);
          left -= t;
        }
      }
      ls[n] = 0xff;
      rs[n++] = 0x02;
      openedit();
      if (!patch_setpart(PTBL, ins, ls, rs, n)) tablefull();
      *sel = -1;
    }
  }
}

static void drawfilterpage(void)
{
  int owner = filterowner();
  int fi = owner >= 0 ? osc[owner] : 0;
  int start, len, ffrow, steps, i, n, state;
  int playrow = -1;
  int *sel = &selstep[PAGE_FILTER];
  int *scroll = &scrollpos[PAGE_FILTER];
  const int visible = 10;
  char text[48];

  if (!fi)
  {
    print(1, PAGE_Y, "THE PATCH DOESN'T USE THE FILTER.", WHITE);
    if (button(WID(ID_SEQBTN, 1), 1, PAGE_Y + 2, " USE THE FILTER ", true, LIGHTGREEN,
      "MAKE A FILTERTABLE (LOW PASS) ON THE FIRST OSCILLATOR", firstosc() >= 0))
    {
      openedit();
      needfilter(1 << firstosc());
    }
    return;
  }

  printfmt(1, PAGE_Y, LIGHTBLUE, "FILTERTABLE OF OSCILLATOR %d,", owner + 1);
  printfmt(30, PAGE_Y, YELLOW, "%02X", fi);
  steps = partinfo(FTBL, fi, &start, &len, &ffrow);
  printfmt(44, PAGE_Y, GREY, "FILTERTABLE %02X-%02X", start + 1, start + len);
  if (patch_isshared(FTBL, fi))
  {
    print(63, PAGE_Y, "SHARED", LIGHTRED);
    if (button(WID(ID_SEQHEAD, 6), 70, PAGE_Y, "UNSHARE", false, WHITE, "OTHER INSTRUMENTS USE THESE ROWS TOO. CLICK TO GIVE THIS ONE ITS OWN COPY"))
    {
      openedit();
      if (!patch_unshare(FTBL, fi)) tablefull();
    }
  }
  if (playfilterptr()) playrow = playfilterptr() - 1;

  {
    unsigned char cut[144];
    int values[144];
    int f;
    patch_simfilter(fi, 144, cut, NULL);
    for (f = 0; f < 144; f++) values[f] = cut[f];
    drawgraph(values, 0xff, YELLOW, "FF", "80");
  }

  if (*sel >= len) *sel = -1;
  steplistheader();
  scrollbuttons(74, PAGE_Y + 15, len, visible);
  for (i = *scroll; (i < len) && (i < *scroll + visible); i++)
  {
    int row = start + i;
    int y = PAGE_Y + 16 + (i - *scroll);
    unsigned char l = ltable[FTBL][row];
    unsigned char r = rtable[FTBL][row];
    int v;

    if (row == playrow) put(1, y, '>', YELLOW);
    snprintf(text, sizeof text, "%02X", i + 1);
    if (area(WID(ID_LISTNUM, i), 3, y, 2, 1, "CLICK TO SELECT THE STEP", &state)) *sel = (*sel == i) ? -1 : i;
    print(3, y, text, (*sel == i) ? WHITE : (state ? WHITE : LIGHTBLUE), *sel == i);

    if (l == 0xff)
    {
      int target = r ? r - start : 0;
      print(7, y, "LOOP", LIGHTBLUE);
      looptext(start, len, r, text, sizeof text);
      v = target;
      if (dragvalue(WID(ID_LISTA, i), 13, y, 16, &v, 0, steps, 1, text, WHITE, "WHERE IT GOES AFTER THE LAST STEP: A STEP TO LOOP TO, OR END"))
        rtable[FTBL][row] = (unsigned char)(v ? start + v : 0);
    }
    else if (l >= 0x80)
    {
      static const char *typelabels[3] = { "LP", "BP", "HP" };
      if (area(WID(ID_LISTKIND, i), 7, y, 4, 1, "SET THE FILTER TYPE, RESONANCE AND VOICES. CLICK TO CHANGE THE STEP TYPE", &state))
      {
        openedit();
        ltable[FTBL][row] = 0x00;
        rtable[FTBL][row] = 0x80;
      }
      print(7, y, "SET", state ? YELLOW : WHITE);
      for (n = 0; n < 3; n++)
      {
        if (button(WID(ID_LISTA + n, i), 13 + n * 3, y, typelabels[n], (l & (0x10 << n)) != 0, YELLOW, "FILTER TYPE"))
        {
          openedit();
          ltable[FTBL][row] ^= (unsigned char)(0x10 << n);
        }
      }
      v = r >> 4;
      snprintf(text, sizeof text, "RES %X", r >> 4);
      if (dragvalue(WID(ID_LISTD, i), 23, y, 5, &v, 0, 15, 1, text, WHITE, "RESONANCE"))
        rtable[FTBL][row] = (unsigned char)((r & 0x0f) | (v << 4));
      print(30, y, "VOICES", LIGHTBLUE);
      for (n = 0; n < 3; n++)
      {
        snprintf(text, sizeof text, "%d", n + 1);
        if (button(WID(ID_LISTE + n, i), 37 + n * 2, y, text, (r & (1 << n)) != 0, YELLOW, "SID VOICES THAT GO THROUGH THE FILTER"))
        {
          openedit();
          rtable[FTBL][row] ^= (unsigned char)(1 << n);
        }
      }
    }
    else if (l == 0x00)
    {
      if (area(WID(ID_LISTKIND, i), 7, y, 4, 1, "SET THE CUTOFF. CLICK TO CHANGE THE STEP TYPE", &state))
      {
        openedit();
        ltable[FTBL][row] = 0x20;
        rtable[FTBL][row] = 0x01;
      }
      print(7, y, "CUT", state ? YELLOW : WHITE);
      v = r;
      snprintf(text, sizeof text, "%02X", r);
      if (dragvalue(WID(ID_LISTA, i), 13, y, 3, &v, 0, 0xff, 1, text, WHITE, "CUTOFF (DRAG OR WHEEL)"))
        rtable[FTBL][row] = (unsigned char)v;
      if (hslider(WID(ID_LISTB, i), 18, y, 48, &v, 0, 0xff, YELLOW, "CUTOFF"))
        rtable[FTBL][row] = (unsigned char)v;
    }
    else
    {
      int sp = signedvalue(r);
      if (area(WID(ID_LISTKIND, i), 7, y, 4, 1, "SWEEP THE CUTOFF. CLICK TO CHANGE THE STEP TYPE", &state))
      {
        openedit();
        ltable[FTBL][row] = 0x90;
        rtable[FTBL][row] = (unsigned char)(0x80 | (1 << owner));
      }
      print(7, y, "SWEEP", state ? YELLOW : WHITE);
      v = l;
      snprintf(text, sizeof text, "%02X FRAMES", l);
      if (dragvalue(WID(ID_LISTA, i), 13, y, 10, &v, 1, 0x7f, 1, text, WHITE, "HOW MANY FRAMES THE SWEEP LASTS"))
        ltable[FTBL][row] = (unsigned char)v;
      v = sp;
      signedtext(sp, text, sizeof text);
      print(25, y, "BY", LIGHTBLUE);
      if (dragvalue(WID(ID_LISTB, i), 28, y, 4, &v, -128, 127, 1, text, WHITE, "CHANGE OF THE CUTOFF EACH FRAME"))
        rtable[FTBL][row] = (unsigned char)(v & 0xff);
      signedtext(sp * l, text, sizeof text);
      printfmt(34, y, GREY, "= %s IN ALL", text);
    }
  }

  if (button(WID(ID_SEQBTN, 2), 1, PAGE_Y + 27, "+SET", false, WHITE, "ADD A STEP THAT SETS THE FILTER TYPE"))
    insertrow(FTBL, start, ffrow >= 0 ? ffrow : start + len, 0x90, (unsigned char)(0x80 | (1 << owner)));
  if (button(WID(ID_SEQBTN, 3), 6, PAGE_Y + 27, "+CUT", false, WHITE, "ADD A STEP THAT SETS THE CUTOFF"))
    insertrow(FTBL, start, ffrow >= 0 ? ffrow : start + len, 0x00, 0x80);
  if (button(WID(ID_SEQBTN, 4), 11, PAGE_Y + 27, "+SWEEP", false, WHITE, "ADD A CUTOFF SWEEP STEP"))
    insertrow(FTBL, start, ffrow >= 0 ? ffrow : start + len, 0x20, 0xff);
  if (button(WID(ID_SEQBTN, 5), 18, PAGE_Y + 27, "DELETE", false, WHITE, "DELETE THE SELECTED STEP", (*sel >= 0) && (start + *sel != ffrow)))
  {
    deleterow(FTBL, steps, start + *sel);
    *sel = -1;
  }

  // The sweep maker: from one cutoff to another, once
  print(1, PAGE_Y + 29, "MAKE A SWEEP", LIGHTBLUE);
  snprintf(text, sizeof text, "FROM %02X", fsweepfrom);
  dragvalue(WID(ID_SWEEP, 1), 14, PAGE_Y + 29, 7, &fsweepfrom, 0, 0xff, 1, text, WHITE, "CUTOFF AT THE START OF THE NOTE");
  snprintf(text, sizeof text, "TO %02X", fsweepto);
  dragvalue(WID(ID_SWEEP, 2), 23, PAGE_Y + 29, 5, &fsweepto, 0, 0xff, 1, text, WHITE, "CUTOFF AT THE END OF THE SWEEP");
  snprintf(text, sizeof text, "IN %02X FRAMES", fsweepframes);
  dragvalue(WID(ID_SWEEP, 3), 30, PAGE_Y + 29, 12, &fsweepframes, 1, 0xff, 1, text, WHITE, "HOW LONG THE SWEEP TAKES");
  if (button(WID(ID_SWEEP, 4), 44, PAGE_Y + 29, " MAKE IT ", true, LIGHTGREEN, "REPLACE THE FILTERTABLE WITH THIS SWEEP (KEEPS TYPE, RESONANCE, VOICES)"))
  {
    unsigned char ls[16], rs[16];
    int frow = patch_filterrow(fi);
    int diff = fsweepto - fsweepfrom;
    int frames = fsweepframes;
    int speed, cnt = 0;

    ls[cnt] = (frow >= 0) ? ltable[FTBL][frow] : 0x90;
    rs[cnt++] = (frow >= 0) ? rtable[FTBL][frow] : (unsigned char)(0x80 | (1 << owner));
    ls[cnt] = 0x00;
    rs[cnt++] = (unsigned char)fsweepfrom;
    if (diff)
    {
      int a = diff < 0 ? -diff : diff;
      if (frames > a) frames = a;
      speed = diff / frames;
      if (speed > 127) speed = 127;
      if (speed < -128) speed = -128;
      frames = diff / speed;
      while ((frames > 0) && (cnt < 12))
      {
        int t = frames > 0x7f ? 0x7f : frames;
        ls[cnt] = (unsigned char)t;
        rs[cnt++] = (unsigned char)(speed & 0xff);
        frames -= t;
      }
    }
    ls[cnt] = 0xff;
    rs[cnt++] = 0x00;
    openedit();
    if (!patch_setpart(FTBL, fi, ls, rs, cnt)) tablefull();
    *sel = -1;
  }
}

/*
 * EMU page: chip and player settings, the SID registers
 */

static void drawemupage(void)
{
  int v, n;
  char text[64];
  static const char *voicewaves[4] = { "TRI", "SAW", "PUL", "NOI" };

  box(0, PAGE_Y, 40, 14, LIGHTBLUE, "EMULATION", WHITE);
  print(2, PAGE_Y + 2, "SID CHIP", LIGHTBLUE);
  if (button(WID(ID_EMU, 1), 16, PAGE_Y + 2, " 6581 ", !editorInfo.sidmodel, WHITE, "THE ORIGINAL SID (C64 BREADBIN): GRITTY FILTER"))
    editorsetting(&editorInfo.sidmodel, 0, true);
  if (button(WID(ID_EMU, 2), 23, PAGE_Y + 2, " 8580 ", editorInfo.sidmodel != 0, WHITE, "THE LATER SID (C64C): CLEAN FILTER, LOUDER COMBINED WAVEFORMS"))
    editorsetting(&editorInfo.sidmodel, 1, true);

  print(2, PAGE_Y + 4, "CLOCK", LIGHTBLUE);
  if (button(WID(ID_EMU, 3), 16, PAGE_Y + 4, " PAL ", !editorInfo.ntsc, WHITE, "EUROPEAN C64: 50 FRAMES A SECOND"))
    editorsetting(&editorInfo.ntsc, 0, true);
  if (button(WID(ID_EMU, 4), 23, PAGE_Y + 4, " NTSC ", editorInfo.ntsc != 0, WHITE, "AMERICAN C64: 60 FRAMES A SECOND"))
    editorsetting(&editorInfo.ntsc, 1, true);

  print(2, PAGE_Y + 6, "PLAYER SPEED", LIGHTBLUE);
  v = editorInfo.multiplier;
  if (editorInfo.multiplier) snprintf(text, sizeof text, "%uX A FRAME", editorInfo.multiplier);
  else snprintf(text, sizeof text, "25 HZ");
  if (dragvalue(WID(ID_EMU, 5), 16, PAGE_Y + 6, 12, &v, 0, 16, 1, text, WHITE, "HOW MANY TIMES A FRAME THE PLAYER RUNS (DRAG OR WHEEL)"))
    editorsetting(&editorInfo.multiplier, (unsigned)v, true);

  print(2, PAGE_Y + 8, "HARD RESTART", LIGHTBLUE);
  for (n = 0; n < 4; n++)
  {
    static const char *labels = "ADSR";
    int shift = 12 - n * 4;
    v = (editorInfo.adparam >> shift) & 15;
    snprintf(text, sizeof text, "%c%X", labels[n], v);
    if (dragvalue(WID(ID_EMU, 6 + n), 16 + n * 4, PAGE_Y + 8, 3, &v, 0, 15, 1, text, WHITE,
      "THE ENVELOPE SET JUST BEFORE EACH NOTE (HARD RESTART). DRAG OR WHEEL"))
      editorsetting(&editorInfo.adparam, (editorInfo.adparam & ~(15u << shift)) | ((unsigned)v << shift), false);
  }

  print(2, PAGE_Y + 10, "SOUND", LIGHTBLUE);
  {
    static const char *labels[3] = { " FAST ", " INTERP ", " FP " };
    static const unsigned values[3] = { 0, 1, 3 };
    static const char *tips[3] =
    {
      "RESID WITHOUT INTERPOLATION: LEAST CPU",
      "RESID WITH INTERPOLATION: BETTER SOUND",
      "RESID-FP: MODELS THE 6581'S FILTER DISTORTION"
    };
    int x = 16;
    for (n = 0; n < 3; n++)
    {
      if (button(WID(ID_EMU, 10 + n), x, PAGE_Y + 10, labels[n], (interpolate & 3) == values[n], WHITE, tips[n]))
      {
        interpolate = values[n];
        stopsong();
        do_sound_init();
      }
      x += (int)strlen(labels[n]) + 1;
    }
  }

  // The SID registers as a machine language monitor shows memory
  box(40, PAGE_Y, 40, 14, LIGHTBLUE, "SID REGISTERS", WHITE);
  for (n = 0; n < 4; n++)
  {
    int base = n * 7;
    int count = (n < 3) ? 7 : 4;
    int c, x;
    x = printfmt(42, PAGE_Y + 2 + n * 2, LIGHTBLUE, ".:D4%02X", base);
    for (c = 0; c < count; c++)
    {
      printfmt(x + 1, PAGE_Y + 2 + n * 2, WHITE, "%02X", sidreg[base + c]);
      x += 3;
    }
    if (n < 3) printfmt(70, PAGE_Y + 2 + n * 2, GREY, "VOICE %d", n + 1);
    else print(70, PAGE_Y + 2 + n * 2, "FILTER", GREY);
  }
  print(42, PAGE_Y + 11, "FREQ  PW   CTRL AD SR", GREY);

  // The voices
  box(0, PAGE_Y + 15, COLS, 11, LIGHTBLUE, "VOICES", WHITE);
  for (n = 0; n < MAX_CHN; n++)
  {
    int y = PAGE_Y + 17 + n * 3;
    unsigned char wave = sidreg[n * 7 + 4];
    int b, x;
    printfmt(2, y, WHITE, "VOICE %d", n + 1);
    put(11, y, (wave & 1) ? SC_BALL : SC_CIRCLE, (wave & 1) ? LIGHTRED : GREY);
    x = 13;
    for (b = 0; b < 4; b++)
    {
      print(x, y, voicewaves[b], (wave & (0x10 << b)) ? YELLOW : DARKGREY);
      x += 4;
    }
    if (chn[n].instr) printfmt(30, y, GREY, "INS %02X", chn[n].instr);
    hbar(38, y, 40, chn[n].mute ? 0.0f : voicelevel[n], LIGHTGREEN, BLACK);
  }

  print(1, PAGE_Y + 27, usingrom() ? "CHARACTERS: FROM THE C64 ROM FILE" :
    "CHARACTERS: BUILT IN. A C64 CHARACTER ROM NAMED CHARGEN IN THE", GREY);
  if (!usingrom()) print(1, PAGE_Y + 28, "SETTINGS FOLDER IS USED INSTEAD (E.G. FROM VICE).", GREY);
}

/*
 * BROWSER page: the instruments (.ins files) and the folders in a folder.
 * Moving onto an instrument (UP / DOWN or a click) tries it: it is loaded
 * into the patch's instrument and a note plays. ENTER or a double-click
 * keeps it; leaving the page puts the song back as it was
 */

#define BR_MAXENTRIES 4000
#define BR_LISTY (PAGE_Y + 5)               // First row of the list
#define BR_ROWS (PAGE_Y + PAGE_H - 1 - BR_LISTY)
#define BR_W 58                             // Width of the list's box
#define PREVIEWMS 700                       // How long the tried note is held

#if defined(_WIN32)
#define PATHSEP '\\'
#else
#define PATHSEP '/'
#endif

struct BrowserEntry
{
  char name[MAX_FILENAME];
  bool dir;
  bool valid;                               // A GoatTracker instrument
  char insname[MAX_INSTRNAMELEN + 1];       // Its own name
  unsigned char ad, sr;
};

static BrowserEntry *brentries = NULL;
static int brcount = 0;
static int brfiles = 0;
static char brscanned[MAX_PATHNAME];        // The folder listed ("" = none yet)
static bool brrescan = false;
static bool brfailed = false;               // The folder could not be read
static int brsel = -1;
static int brscroll = 0;
static char brselect[MAX_FILENAME];         // Entry to select after the next scan
// Keys for the list, acted on when the page is drawn
static int brmove = 0;                      // UP -1 / DOWN +1 (summed)
static int brjump = 0;                      // HOME -1 / END +1
static bool brenter = false, brparent = false;
static Uint32 previewtime = 0;

static bool isslash(char c)
{
  return (c == '/') || (c == '\\');
}

static int nocasecmp(const char *a, const char *b)
{
  for (;; a++, b++)
  {
    int ca = tolower((unsigned char)*a), cb = tolower((unsigned char)*b);
    if ((ca != cb) || (!ca)) return ca - cb;
  }
}

static bool hasext(const char *name, const char *ext)
{
  size_t n = strlen(name), e = strlen(ext);
  return (n > e) && (!nocasecmp(name + n - e, ext));
}

static void joinpath(char *out, int size, const char *dir, const char *name)
{
  size_t len = strlen(dir);
  if ((len) && (isslash(dir[len - 1]))) snprintf(out, size, "%s%s", dir, name);
  else snprintf(out, size, "%s%c%s", dir, PATHSEP, name);
}

// Directories first, then by name
static int entrycmp(const void *a, const void *b)
{
  const BrowserEntry *ea = (const BrowserEntry *)a, *eb = (const BrowserEntry *)b;
  if (ea->dir != eb->dir) return ea->dir ? -1 : 1;
  return nocasecmp(ea->name, eb->name);
}

// The instrument's name and envelope from the file's header
static void readheader(const char *path, BrowserEntry *e)
{
  unsigned char head[13 + MAX_INSTRNAMELEN];
  FILE *f = fopen(path, "rb");
  size_t n;

  if (!f) return;
  n = fread(head, 1, sizeof head, f);
  fclose(f);
  if ((n < 13) || (memcmp(head, "GTI", 3)) || (!strchr("2345!", head[3]))) return;
  e->valid = true;
  e->ad = head[4];
  e->sr = head[5];
  // GTI2-5: the name follows AD, SR and 7 more bytes
  if ((head[3] != '!') && (n == sizeof head))
  {
    memcpy(e->insname, head + 13, MAX_INSTRNAMELEN);
    e->insname[MAX_INSTRNAMELEN] = 0;
  }
}

static void scanfolder(void)
{
  DIR *d;
  struct dirent *de;
  int i;

  snprintf(brscanned, sizeof brscanned, "%s", browserfolder);
  brcount = brfiles = 0;
  brsel = -1;
  brscroll = 0;
  brrescan = false;
  brfailed = true;
  if (!brentries) brentries = (BrowserEntry *)malloc(sizeof(BrowserEntry) * BR_MAXENTRIES);
  if (!brentries) return;
  d = opendir(browserfolder);
  if (!d) return;
  brfailed = false;
  while (((de = readdir(d)) != NULL) && (brcount < BR_MAXENTRIES))
  {
    char full[MAX_PATHNAME * 2];
    struct stat st;
    BrowserEntry *e = &brentries[brcount];

    if (de->d_name[0] == '.') continue;
    joinpath(full, sizeof full, browserfolder, de->d_name);
    if (stat(full, &st)) continue;
    memset(e, 0, sizeof *e);
    snprintf(e->name, sizeof e->name, "%s", de->d_name);
    if (st.st_mode & S_IFDIR) e->dir = true;
    else if (hasext(de->d_name, ".ins"))
    {
      readheader(full, e);
      brfiles++;
    }
    else continue;
    brcount++;
  }
  closedir(d);
  qsort(brentries, brcount, sizeof(BrowserEntry), entrycmp);
  // After going to the parent folder: the folder come from
  for (i = 0; (brselect[0]) && (i < brcount); i++)
  {
    if (!strcmp(brentries[i].name, brselect))
    {
      brsel = i;
      brscroll = i - BR_ROWS / 2;
    }
  }
  brselect[0] = 0;
}

static void setfolder(const char *path)
{
  if (strlen(path) >= MAX_PATHNAME) return;
  strcpy(browserfolder, path);
}

static void parentfolder(void)
{
  char path[MAX_PATHNAME];
  int len;

  strcpy(path, browserfolder);
  len = (int)strlen(path);
  while ((len > 1) && (isslash(path[len - 1]))) path[--len] = 0;
  while ((len > 0) && (!isslash(path[len - 1]))) len--;
  if (len <= 0) return;                     // No folder above
  snprintf(brselect, sizeof brselect, "%s", path + len);
  path[len] = 0;
  // Keep the slash of a root ("/", "C:\"), drop any other
  if ((len > 1) && (path[len - 2] != ':')) path[len - 1] = 0;
  setfolder(path);
}

// Checksum of everything loading an instrument may change
static unsigned songsum(void)
{
  const unsigned char *parts[4] = { (const unsigned char *)instr, &ltable[0][0], &rtable[0][0], &pattern[0][0] };
  const size_t sizes[4] = { sizeof instr, sizeof ltable, sizeof rtable, sizeof pattern };
  unsigned h = 2166136261u;
  int p;
  size_t i;

  for (p = 0; p < 4; p++)
  {
    for (i = 0; i < sizes[p]; i++) h = (h ^ parts[p][i]) * 16777619u;
  }
  return h;
}

static void putsongback(void)
{
  memcpy(instr, pv.instr, sizeof instr);
  memcpy(ltable, pv.ltable, sizeof ltable);
  memcpy(rtable, pv.rtable, sizeof rtable);
  memcpy(pattern, pv.pattern, sizeof pattern);
}

static void loadinto(const char *path, int slot)
{
  if (strlen(path) >= MAX_FILENAME) return;
  editorInfo.einum = slot;
  strcpy(instrfilename, path);
  loadinstrument();
}

// The song as it was before the try-out, unless something else has changed
// it meanwhile (song loaded...): then it stays as it is
static void cancelpreview(void)
{
  if (!pv.active) return;
  if (heldsource == SRC_PREVIEW) noteoff(SRC_PREVIEW, 0);
  if (songsum() == pv.sum) putsongback();
  pv.active = false;
}

// The instrument tried stays, as an undo step of its own
static void keeppreview(void)
{
  char path[MAX_PATHNAME];
  int c;

  if (!pv.active) return;
  pv.active = false;
  if (songsum() != pv.sum) return;
  strcpy(path, pv.path);
  putsongback();
  undobegin();
  for (c = 0; c < MAX_INSTR; c++) undoinstrument(c);
  for (c = 0; c < MAX_TABLES; c++) undotable(c);
  for (c = 0; c < MAX_PATT; c++) undopattern(c);
  loadinto(path, pv.slot);
  undoend();
}

static void playpreview(void)
{
  int k;

  if ((heldnote) && (heldsource != SRC_PREVIEW)) return;    // A key is held
  if (heldnote) noteoff(SRC_PREVIEW, 0);
  for (k = 0; k < PATCH_OSC; k++) oscmute[k] = false;
  noteon(FIRSTNOTE + editorInfo.epoctave * 12, SRC_PREVIEW, 0);
  previewtime = SDL_GetTicks();
}

// Tries the instrument in the file
static void previewfile(const char *path)
{
  int slot = editorInfo.einum;

  if ((pv.active) && (!strcmp(pv.path, path)))
  {
    playpreview();
    return;
  }
  cancelpreview();
  if ((slot < 1) || (slot >= MAX_INSTR)) slot = 1;
  if (strlen(path) >= MAX_FILENAME) return;
  memcpy(pv.instr, instr, sizeof instr);
  memcpy(pv.ltable, ltable, sizeof ltable);
  memcpy(pv.rtable, rtable, sizeof rtable);
  memcpy(pv.pattern, pattern, sizeof pattern);
  pv.slot = slot;
  snprintf(pv.path, sizeof pv.path, "%s", path);
  loadinto(path, slot);
  pv.sum = songsum();
  pv.active = true;
  playpreview();
}

// Loads the instrument in the file for good (CTRL+Z undoes)
static void loadfile(const char *path)
{
  int slot = pv.active ? pv.slot : editorInfo.einum;

  if ((slot < 1) || (slot >= MAX_INSTR)) slot = 1;
  if ((pv.active) && (!strcmp(pv.path, path)))
  {
    // The one being tried: keep it
    keeppreview();
    gui_addrecentfile(path);
    gui_message("Loaded instrument %02X from %s", slot, gt_basename(path));
    return;
  }
  cancelpreview();
  closeedit();
  openedit();
  gt_loadinstrument(path, slot);
  closeedit();
}

// Acts on the entry: a folder is opened, an instrument tried or loaded
static void openentry(int i, bool load)
{
  char full[MAX_PATHNAME * 2];
  BrowserEntry *e;

  if ((i < 0) || (i >= brcount)) return;
  e = &brentries[i];
  joinpath(full, sizeof full, browserfolder, e->name);
  if (e->dir)
  {
    if (load) setfolder(full);
    return;
  }
  if (!e->valid)
  {
    gui_message("%s is not a GoatTracker instrument", e->name);
    return;
  }
  if (load) loadfile(full);
  else previewfile(full);
}

static void selectentry(int i)
{
  if (brcount <= 0) return;
  i = clampint(i, 0, brcount - 1);
  if (i == brsel) return;
  brsel = i;
  if (brsel < brscroll) brscroll = brsel;
  if (brsel >= brscroll + BR_ROWS) brscroll = brsel - BR_ROWS + 1;
  if (!brentries[brsel].dir) openentry(brsel, false);
}

static void drawbrowserpage(void)
{
  ImGuiIO &io = ImGui::GetIO();
  const int x1 = BR_W - 2;                  // Last column inside the box
  int i, r, y;
  char text[MAX_PATHNAME + 8];

  // The folder: the one used last, else the instrument dialogs' folder
  if (!browserfolder[0]) setfolder(gt_folder(FOLDER_INSTR));
  if ((!browserfolder[0]) && (getcwd(text, sizeof text))) setfolder(text);
  if ((brrescan) || (strcmp(brscanned, browserfolder))) scanfolder();

  // Keys
  if (brjump) selectentry(brjump < 0 ? 0 : brcount - 1);
  else if (brmove) selectentry(brsel < 0 ? (brmove > 0 ? 0 : brcount - 1) : brsel + brmove);
  brjump = brmove = 0;
  if (brenter) openentry(brsel, true);
  if (brparent) parentfolder();
  brenter = brparent = false;

  box(0, PAGE_Y, BR_W, PAGE_H, LIGHTBLUE, "INSTRUMENT BROWSER", WHITE);

  // Folder line: the end of the path if it is long
  print(2, PAGE_Y + 1, "FOLDER", LIGHTBLUE);
  {
    int room = x1 - 9 + 1;
    const char *p = browserfolder;
    if ((int)strlen(p) > room)
    {
      p += strlen(p) - (room - 3);
      snprintf(text, sizeof text, "...%s", p);
    }
    else snprintf(text, sizeof text, "%s", p);
    // The C64 has no backslash (it would show as a pound sign)
    for (i = 0; text[i]; i++)
    {
      if (text[i] == '\\') text[i] = '/';
    }
    print(9, PAGE_Y + 1, text, WHITE);
  }
  if (button(WID(ID_BROWSER, 1), 2, PAGE_Y + 2, " SET FOLDER ", false, WHITE, "CHOOSE THE FOLDER TO LOOK IN"))
    cmd_post(GTCMD_BROWSEFOLDER);
  if (button(WID(ID_BROWSER, 2), 15, PAGE_Y + 2, " PARENT ", false, WHITE, "THE FOLDER ABOVE THIS ONE (BACKSPACE)"))
    parentfolder();
  if (button(WID(ID_BROWSER, 3), 24, PAGE_Y + 2, " REFRESH ", false, WHITE, "READ THE FOLDER AGAIN"))
    brrescan = true;
  snprintf(text, sizeof text, "%d INSTRUMENT%s", brfiles, brfiles == 1 ? "" : "S");
  print(x1 - (int)strlen(text) + 1, PAGE_Y + 2, text, GREY);
  print(2, BR_LISTY - 1, "NAME", GREY);
  print(34, BR_LISTY - 1, "INSTRUMENT", GREY);
  print(51, BR_LISTY - 1, "AD SR", GREY);

  // The list
  if (over(1.0f, (float)BR_LISTY, (float)(x1), (float)BR_ROWS) && (in.wheel != 0.0f))
    brscroll -= (int)(in.wheel > 0.0f ? 3 : -3);
  if (brscroll > brcount - BR_ROWS) brscroll = brcount - BR_ROWS;
  if (brscroll < 0) brscroll = 0;
  if (!brcount)
    print(2, BR_LISTY + 1, brfailed ? "THE FOLDER CAN'T BE READ" : "NO INSTRUMENTS OR FOLDERS HERE", GREY);
  for (r = 0; r < BR_ROWS; r++)
  {
    BrowserEntry *e;
    bool hovered, sel;
    int fg;
    char name[MAX_FILENAME];
    char full[MAX_PATHNAME * 2];

    i = brscroll + r;
    if (i >= brcount) break;
    e = &brentries[i];
    y = BR_LISTY + r;
    hovered = over(1.0f, (float)y, (float)(x1 - 1), 1.0f);
    sel = (i == brsel);
    if (hovered)
    {
      tip(e->dir ? "DOUBLE-CLICK TO OPEN THE FOLDER" : e->valid ? "CLICK TO TRY, DOUBLE-CLICK TO LOAD INTO THE PATCH" :
        "NOT A GOATTRACKER INSTRUMENT");
      if (in.clicked)
      {
        // A click tries an instrument (again), a double-click (a second
        // click on the same row soon after) loads it or opens the folder
        static Uint32 lastclick = 0;
        static int lastrow = -1;
        Uint32 now = SDL_GetTicks();
        bool twice = (lastrow == i) && (now - lastclick <= (Uint32)(io.MouseDoubleClickTime * 1000.0f + 100.0f));
        brsel = i;
        sel = true;
        lastrow = twice ? -1 : i;
        lastclick = now;
        if (twice) openentry(i, true);
        else if (!e->dir) openentry(i, false);
      }
    }

    snprintf(name, sizeof name, "%s", e->name);
    if (e->dir) strncat(name, "/", sizeof name - strlen(name) - 1);
    else if (hasext(name, ".ins")) name[strlen(name) - 4] = 0;
    name[31] = 0;
    fg = e->dir ? LIGHTBLUE : e->valid ? WHITE : DARKGREY;
    joinpath(full, sizeof full, browserfolder, e->name);
    if ((pv.active) && (!e->dir) && (!strcmp(full, pv.path))) fg = YELLOW;
    if (hovered && !sel) fg = YELLOW;
    if (sel) fill(1, y, x1 - 1, 1, SC_SPACE, WHITE, LIGHTBLUE);
    print(2, y, name, sel ? BLUE : fg, false, sel ? LIGHTBLUE : -1);
    if (e->dir) print(51, y, "DIR", sel ? BLUE : GREY, false, sel ? LIGHTBLUE : -1);
    else if (e->valid)
    {
      print(34, y, e->insname, sel ? BLUE : GREY, false, sel ? LIGHTBLUE : -1);
      printfmt(51, y, sel ? BLUE : GREY, "%02X %02X", e->ad, e->sr);
      if (sel) color(51, y, 5, 1, BLUE, LIGHTBLUE);
    }
  }

  // Scroll bar
  if (brcount > BR_ROWS)
  {
    int top = brscroll * BR_ROWS / brcount;
    int len = BR_ROWS * BR_ROWS / brcount;
    if (len < 1) len = 1;
    if (top + len > BR_ROWS) top = BR_ROWS - len;
    fill(x1, BR_LISTY, 1, BR_ROWS, SC_SPACE, DARKGREY, DARKGREY);
    fill(x1, BR_LISTY + top, 1, len, SC_SPACE, LIGHTBLUE, LIGHTBLUE);
    if (over((float)x1, (float)BR_LISTY, 1.0f, (float)BR_ROWS))
    {
      tip("SCROLL (OR USE THE WHEEL)");
      if (in.clicked) brscroll += (in.y < BR_LISTY + top) ? -BR_ROWS : (in.y >= BR_LISTY + top + len) ? BR_ROWS : 0;
    }
  }

  // The panel: what is selected, what happens
  box(BR_W, PAGE_Y, COLS - BR_W, PAGE_H, LIGHTBLUE, "TRY OUT", WHITE);
  {
    const int px = BR_W + 2;
    BrowserEntry *e = ((brsel >= 0) && (brsel < brcount)) ? &brentries[brsel] : NULL;
    int target = pv.active ? pv.slot : editorInfo.einum;

    // (19 columns inside the box)
    if ((e) && (!e->dir) && (e->valid))
    {
      print(px, PAGE_Y + 2, e->insname[0] ? e->insname : "(NO NAME)", WHITE);
      printfmt(px, PAGE_Y + 4, LIGHTBLUE, "ATTACK  %X", e->ad >> 4);
      printfmt(px, PAGE_Y + 5, LIGHTBLUE, "DECAY   %X", e->ad & 15);
      printfmt(px, PAGE_Y + 6, LIGHTBLUE, "SUSTAIN %X", e->sr >> 4);
      printfmt(px, PAGE_Y + 7, LIGHTBLUE, "RELEASE %X", e->sr & 15);
    }
    else print(px, PAGE_Y + 2, e && e->dir ? "A FOLDER" : "NOTHING SELECTED", GREY);

    if (pv.active)
    {
      printfmt(px, PAGE_Y + 9, YELLOW, "TRYING IN INS %02X", target);
      print(px, PAGE_Y + 10, "NOT KEPT YET", GREY);
    }
    else printfmt(px, PAGE_Y + 9, GREY, "LOADS INTO INS %02X", target);

    if (button(WID(ID_BROWSER, 10), px, PAGE_Y + 12, " LOAD ", false, WHITE, "KEEP THE SELECTED INSTRUMENT IN THE PATCH (ENTER)",
      (e) && (!e->dir) && (e->valid)))
      openentry(brsel, true);
    if (button(WID(ID_BROWSER, 11), px + 7, PAGE_Y + 12, " PLAY ", false, WHITE, "PLAY THE INSTRUMENT BEING TRIED AGAIN", pv.active))
      playpreview();
    if (button(WID(ID_BROWSER, 12), px, PAGE_Y + 14, " PUT BACK ", false, WHITE, "STOP TRYING: THE PATCH AS IT WAS", pv.active))
      cancelpreview();

    print(px, PAGE_Y + 17, "UP/DOWN   TRY", GREY);
    print(px, PAGE_Y + 18, "ENTER     LOAD", GREY);
    print(px, PAGE_Y + 19, "DBL-CLICK LOAD/OPEN", GREY);
    print(px, PAGE_Y + 20, "BACKSPACE PARENT", GREY);
    print(px, PAGE_Y + 21, "HOME END  TOP/END", GREY);
    print(px, PAGE_Y + 22, "Z-M Q-P   PLAY", GREY);
    print(px, PAGE_Y + 23, "PGUP/PGDN OCTAVE", GREY);
    print(px, PAGE_Y + 25, "LEAVING THIS PAGE", GREY);
    print(px, PAGE_Y + 26, "PUTS BACK WHAT WAS", GREY);
    print(px, PAGE_Y + 27, "TRIED BUT NOT KEPT", GREY);
  }
}

/*
 * The frame
 */

#include "designerframe.h"

static unsigned char *framepixels = NULL;   // designerframe.png decoded
static int framew = 0, frameh = 0;
static bool framefailed = false;
static SDL_Texture *frametexture = NULL;    // At the size shown
static SDL_Renderer *framerenderer = NULL;
static int frametexw = 0, frameteth = 0;

static void freeframe(void)
{
  if (frametexture) SDL_DestroyTexture(frametexture);
  frametexture = NULL;
  free(framepixels);
  framepixels = NULL;
}

// Draws the frame over a-b (ImGui points: the retro screen's place), pixel
// for pixel, and the designer's lavender border on it. Returns the border's
// area
static void drawframe(SDL_Renderer *renderer, ImVec2 a, ImVec2 b, ImVec2 *screen0, ImVec2 *screen1)
{
  ImGuiIO &io = ImGui::GetIO();
  ImDrawList *dl = ImGui::GetBackgroundDrawList();
  float sx = io.DisplayFramebufferScale.x > 0.0f ? io.DisplayFramebufferScale.x : 1.0f;
  float sy = io.DisplayFramebufferScale.y > 0.0f ? io.DisplayFramebufferScale.y : 1.0f;
  int w = (int)((b.x - a.x) * sx + 0.5f);
  int h = (int)((b.y - a.y) * sy + 0.5f);
  float u = (b.x - a.x) / FRAME_W;      // One template pixel
  float v = (b.y - a.y) / FRAME_H;
  ImU32 border = IM_COL32((BORDERCOLOR >> 16) & 0xff, (BORDERCOLOR >> 8) & 0xff, BORDERCOLOR & 0xff, 255);

  // In whole device pixels, so that the border's edges stay sharp
  *screen0 = ImVec2(floorf((a.x + BORDER_X0 * u) * sx + 0.5f) / sx, floorf((a.y + BORDER_Y0 * v) * sy + 0.5f) / sy);
  *screen1 = ImVec2(floorf((a.x + BORDER_X1 * u) * sx + 0.5f) / sx, floorf((a.y + BORDER_Y1 * v) * sy + 0.5f) / sy);

  if ((!framepixels) && (!framefailed))
  {
    framepixels = decodepng(designerframe_png, sizeof designerframe_png, &framew, &frameh);
    framefailed = !framepixels;
  }
  if ((frametexture) && ((framerenderer != renderer) || (frametexw != w) || (frameteth != h)))
  {
    SDL_DestroyTexture(frametexture);
    frametexture = NULL;
  }
  if ((!frametexture) && (framepixels) && (w > 0) && (h > 0))
  {
    frametexture = scaledtexture(renderer, framepixels, framew, frameh, w, h);
    framerenderer = renderer;
    frametexw = w;
    frameteth = h;
  }
  // Without the image: a plain white panel
  if (frametexture) dl->AddImage(ImTextureRef((ImTextureID)(intptr_t)frametexture), a, b);
  else dl->AddRectFilled(a, b, IM_COL32(255, 255, 255, 255), 8.0f * u);
  dl->AddRectFilled(*screen0, *screen1, border);
}

static void readinput(void)
{
  ImGuiIO &io = ImGui::GetIO();
  bool popup = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup);

  in.x = c64::mousex();
  in.y = c64::mousey();
  in.enabled = (!popup) && (c64::mouseinside());
  in.down = (!popup) && (io.MouseDown[0]);
  in.clicked = (in.enabled) && (io.MouseClicked[0]);
  in.wheel = in.enabled ? io.MouseWheel : 0.0f;
  in.shift = io.KeyShift;
}

static void movefocus(void)
{
  int i, cur = -1;

  if (!nfocus) return;
  for (i = 0; i < nfocus; i++)
  {
    if (focuslist[i] == focus) cur = i;
  }
  if (cur < 0) cur = (nav == NAV_RIGHT) ? 0 : nfocus - 1;
  else cur = (cur + (nav == NAV_RIGHT ? 1 : nfocus - 1)) % nfocus;
  focus = focuslist[cur];
}

void gui::drawdesigner(SDL_Renderer *renderer)
{
  ImGuiIO &io = ImGui::GetIO();
  ImDrawList *bg = ImGui::GetBackgroundDrawList();
  ImVec2 s0, s1, c0, c1;
  float pad;
  int titlecol;

  drawnlastframe = true;
  // The browser's try-out ends when its page is left or another patch is
  // chosen; its note is let go after a moment
  if ((pv.active) && ((page != PAGE_BROWSER) || (editorInfo.einum != pv.slot))) cancelpreview();
  if ((heldsource == SRC_PREVIEW) && (SDL_GetTicks() - previewtime >= PREVIEWMS)) noteoff(SRC_PREVIEW, 0);
  bindpatch();
  if (seqosc >= PATCH_OSC) seqosc = 0;

  // The frame fills the retro screen's place, black around it; the tracker's
  // title bar on top of it, where it is on the retro screen (drawn first to
  // learn that place, into the channel drawn last)
  bg->AddRectFilled(ImVec2(0.0f, 0.0f), io.DisplaySize, IM_COL32(0, 0, 0, 255));
  retro_printtitlebar();
  bg->ChannelsSplit(2);
  bg->ChannelsSetCurrent(1);
  titlecol = drawretrotitlebar(renderer, &s0, &s1);
  bg->ChannelsSetCurrent(0);
  drawframe(renderer, s0, s1, &c0, &c1);
  bg->ChannelsMerge();
  // The violet screen in the lavender border, like a C64's screen
  pad = (c1.x - c0.x) * BORDERW;
  c0 = ImVec2(c0.x + pad, c0.y + pad);
  c1 = ImVec2(c1.x - pad, c1.y - pad);

  // Its colors on the violet screen
  setcolor(BLUE, SCREENCOLOR);
  setcolor(LIGHTBLUE, LABELCOLOR);
  setcolor(GREY, FAINTCOLOR);
  setcolor(DARKGREY, DIMCOLOR);
  setcolor(LIGHTRED, WARNCOLOR);

  layout(renderer, c0, c1);
  readinput();

  // A click on the title bar: its settings, or "Tracker"
  if ((titlecol >= 0) && (io.MouseClicked[0]) && (!ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup)))
    retro_titlebarclick(titlecol);

  // One navigation key a frame
  nav = NAV_NONE;
  if (navhead != navtail)
  {
    nav = navqueue[navhead];
    navshift = navshiftqueue[navhead];
    navhead = (navhead + 1) % 16;
    keyboardmode = true;
  }
  if (in.clicked) keyboardmode = false;
  // A click is a step of its own, apart from a knob's
  if ((in.clicked) && (editopen) && (!active)) closeedit();
  hint = NULL;
  focushint = NULL;
  nfocus = 0;

  clear(BLUE, LIGHTBLUE);
  drawheader();
  drawtabs();
  switch (page)
  {
    case PAGE_PATCH: drawpatchpage(); break;
    case PAGE_WAVE: drawwavepage(); break;
    case PAGE_PULSE: drawpulsepage(); break;
    case PAGE_FILTER: drawfilterpage(); break;
    case PAGE_EMU: drawemupage(); break;
    case PAGE_BROWSER: drawbrowserpage(); break;
  }
  hline(0, HINT_Y - 1, COLS, LIGHTBLUE);
  drawhint();

  if ((nav == NAV_LEFT) || (nav == NAV_RIGHT)) movefocus();
  if (!in.down) active = 0;
  if ((editopen) && (!active) && (SDL_GetTicks() - cctime >= CCPAUSE)) closeedit();

  present(renderer);
  drawlogo();
}

/*
 * Opening and closing, keys
 */

void gui::opendesigner(void)
{
  if (showdesigner) return;
  savededitmode = editorInfo.editmode;
  // The tracker's current instrument (INSTRUMENT NUM.), wherever the cursor
  // is
  if (editorInfo.einum < 1) editorInfo.einum = 1;
  editorInfo.editmode = EDIT_INSTRUMENT;
  showdesigner = true;
  drawnlastframe = false;
  nameedit = false;
  active = 0;
  navhead = navtail = 0;
  {
    int k;
    for (k = 0; k < PATCH_OSC; k++) oscmute[k] = false;
    for (k = 0; k < NUM_PAGES; k++) selstep[k] = -1;
  }
  loadrom(configdirectory());
  macmenu_setdesigner(1);
}

void gui::closedesigner(void)
{
  if (!showdesigner) return;
  cancelpreview();
  if (heldnote) noteoff(heldsource, heldkey);
  commitname();
  closeedit();
  bindpatch();
  if (firstosc() >= 0) editorInfo.einum = osc[firstosc()];
  editorInfo.editmode = savededitmode;
  showdesigner = false;
  drawnlastframe = false;
  retroblockmouse();
  c64::freescreen();
  freeframe();
  macmenu_setdesigner(0);
}

static void pushnav(int key, bool shift)
{
  int next = (navtail + 1) % 16;
  if (next == navhead) return;
  navqueue[navtail] = key;
  navshiftqueue[navtail] = shift;
  navtail = next;
}

void gui::designerkey(const SDL_KeyboardEvent &k, bool down)
{
  SDL_Keycode sym = k.keysym.sym;
  Uint16 mod = k.keysym.mod;
  bool shift = (mod & KMOD_SHIFT) != 0;
  int note;

  if (!down)
  {
    noteoff(SRC_KEY, (int)sym);
    return;
  }

  if (nameedit)
  {
    switch (sym)
    {
      case SDLK_BACKSPACE:
      {
        size_t len = strlen(namebuf);
        if (len) namebuf[len - 1] = 0;
        break;
      }
      case SDLK_RETURN: case SDLK_KP_ENTER: commitname(); break;
      case SDLK_ESCAPE: nameedit = false; break;
    }
    return;
  }

  // CTRL / CMD: undo; other combinations do nothing here
  if (mod & (KMOD_CTRL | KMOD_GUI))
  {
    if ((sym == SDLK_z) && (!k.repeat))
    {
      // An instrument being tried is put back first (that is the undo); a
      // knob's step still open ends first
      if (pv.active)
      {
        cancelpreview();
        return;
      }
      if (!active) closeedit();
      cmd_post(GTCMD_UNDO);
    }
    return;
  }

  // The browser's list (ENTER presses a control that has the keyboard focus)
  if ((page == PAGE_BROWSER) && (!(mod & KMOD_ALT)))
  {
    switch (sym)
    {
      case SDLK_UP: brmove--; return;
      case SDLK_DOWN: brmove++; return;
      case SDLK_HOME: brjump = -1; return;
      case SDLK_END: brjump = 1; return;
      case SDLK_BACKSPACE: if (!k.repeat) brparent = true; return;
      case SDLK_RETURN: case SDLK_KP_ENTER:
        if ((!keyboardmode) || (!focus))
        {
          if (!k.repeat) brenter = true;
          return;
        }
        break;
    }
  }

  switch (sym)
  {
    case SDLK_ESCAPE: if (!k.repeat) cmd_post(GTCMD_DESIGNER); return;
    case SDLK_F1: if (!k.repeat) cmd_post(GTCMD_PLAYSONG); return;
    case SDLK_F2: if (!k.repeat) cmd_post(GTCMD_PLAYPOS); return;
    case SDLK_F3: if (!k.repeat) cmd_post(GTCMD_PLAYPATTERN); return;
    case SDLK_F4: if (!k.repeat) cmd_post(GTCMD_STOP); return;
    case SDLK_TAB:
      page = (page + (shift ? NUM_PAGES - 1 : 1)) % NUM_PAGES;
      focus = 0;
      return;
    case SDLK_PAGEUP: if (editorInfo.epoctave < 7) editorInfo.epoctave++; return;
    case SDLK_PAGEDOWN: if (editorInfo.epoctave > 0) editorInfo.epoctave--; return;
    case SDLK_UP: pushnav(NAV_UP, shift); return;
    case SDLK_DOWN: pushnav(NAV_DOWN, shift); return;
    case SDLK_LEFT: pushnav(NAV_LEFT, shift); return;
    case SDLK_RIGHT: pushnav(NAV_RIGHT, shift); return;
    case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE:
      if (!k.repeat) pushnav(NAV_ENTER, shift);
      return;
  }

  // Note keys (as in the pattern editor)
  if ((k.repeat) || (mod & (KMOD_SHIFT | KMOD_ALT))) return;
  note = notekeytonote((int)sym);
  if (note > 0) noteon(note, SRC_KEY, (int)sym);
}

void gui::designertext(int ch)
{
  size_t len;
  size_t maxlen;

  if (!nameedit) return;
  if ((ch < 32) || (ch >= 127)) return;
  len = strlen(namebuf);
  maxlen = multiosc() ? MAX_INSTRNAMELEN - 3 : MAX_INSTRNAMELEN - 1;
  if (len >= maxlen) return;
  namebuf[len] = (char)ch;
  namebuf[len + 1] = 0;
}

extern "C" int gui_designershown(void)
{
  return showdesigner ? 1 : 0;
}

bool gui::designerwanted(int textscreen)
{
  if (!showdesigner) return false;
  // Not over a classic full-screen dialog (file selector, help...)
  if ((textscreen) && (!((retromode) && (retroatmainscreen)))) return false;
  return true;
}

extern "C" int gui_designeractive(void)
{
  return (showdesigner) && (drawnlastframe);
}

extern "C" void gui_designermidi(int noteon_, int noteoff_)
{
  if (noteon_) noteon(noteon_, SRC_MIDI, 0);
  if ((noteoff_) && (noteoff_ == heldnote)) noteoff(SRC_MIDI, 0);
}

// MIDI knobs: the envelope of the oscillators that are heard (on and not
// muted), the filter's cutoff, resonance, type and voice. A knob turned
// without a pause is one undo step
extern "C" void gui_designermidicc(int cc, int value)
{
  static const char *names[4] = { "ATTACK", "DECAY", "SUSTAIN", "RELEASE" };
  int part = -1;
  int k;

  if (nameedit) return;
  bindpatch();
  switch (cc)
  {
    case 73: part = 0; break;
    case 75: part = 1; break;
    case 70: case 76: case 20: part = 2; break;
    case 72: part = 3; break;
    case 74: case 71: case 4: case 7: break;
    default: return;
  }

  if (part >= 0)
  {
    int v = (value & 0x7f) >> 3;
    int shift = (part & 1) ? 0 : 4;
    bool any = false;
    for (k = 0; k < PATCH_OSC; k++)
    {
      unsigned char *reg;
      if ((!osc[k]) || (oscmute[k])) continue;
      reg = (part < 2) ? &instr[osc[k]].ad : &instr[osc[k]].sr;
      if (((*reg >> shift) & 15) == v) continue;
      openedit();
      *reg = (unsigned char)((*reg & ~(15 << shift)) | (v << shift));
      any = true;
    }
    if (any)
    {
      cctime = SDL_GetTicks();
      gui_message("%s %X (MIDI CC %d)", names[part], v, cc);
    }
    return;
  }

  // The filter part (a new one on the first oscillator if the patch has
  // none). Type and voice: the knob's range in three, LP / BP / HP and SID
  // voice 1 / 2 / 3 (only that one goes through the filter)
  if (firstosc() < 0) return;
  if ((cc == 4) || (cc == 7))
  {
    static const char *types[3] = { "LP", "BP", "HP" };
    int owner = filterowner();
    int fi = (owner >= 0) ? osc[owner] : 0;
    int row = fi ? patch_filterrow(fi) : -1;
    int sel = (value & 0x7f) * 3 / 128;
    int v = (cc == 4) ? (0x10 << sel) : (1 << sel);
    int old = (row < 0) ? -1 : (cc == 4) ? (ltable[FTBL][row] & 0x70) : (rtable[FTBL][row] & 7);

    if (old == v) return;
    openedit();
    fi = needfilter(1 << firstosc());
    row = fi ? patch_filterrow(fi) : -1;
    if (row < 0) return;
    if (cc == 4) ltable[FTBL][row] = (unsigned char)((ltable[FTBL][row] & ~0x70) | v);
    else rtable[FTBL][row] = (unsigned char)((rtable[FTBL][row] & ~7) | v);
    cctime = SDL_GetTicks();
    if (cc == 4) gui_message("FILTER %s (MIDI CC 4)", types[sel]);
    else gui_message("FILTER VOICE %d (MIDI CC 7)", sel + 1);
    return;
  }
  {
    int owner = filterowner();
    int fi = (owner >= 0) ? osc[owner] : 0;
    int row = fi ? ((cc == 74) ? patch_cutoffrow(fi) : patch_filterrow(fi)) : -1;
    int v = (cc == 74) ? ((value & 0x7f) * 255 + 63) / 127 : (value & 0x7f) >> 3;
    int old = (row < 0) ? -1 : (cc == 74) ? rtable[FTBL][row] : (rtable[FTBL][row] >> 4);

    if (old == v) return;
    openedit();
    fi = needfilter(1 << firstosc());
    row = fi ? ((cc == 74) ? patch_cutoffrow(fi) : patch_filterrow(fi)) : -1;
    if (row < 0) return;
    if (cc == 74) rtable[FTBL][row] = (unsigned char)v;
    else rtable[FTBL][row] = (unsigned char)((rtable[FTBL][row] & 0x0f) | (v << 4));
    cctime = SDL_GetTicks();
    if (cc == 74) gui_message("CUTOFF %02X (MIDI CC 74)", v);
    else gui_message("RESONANCE %X (MIDI CC 71)", v);
  }
}
