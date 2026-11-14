/*
 * GOATTRACKER "console": input state for the editors and the classic text
 * screen used by the remaining full-screen dialogs.
 *
 * SDL2 version. The window, rendering and the SDL event loop are in the GUI
 * layer (gui/); this module only queues the key events the GUI passes in and
 * turns them into the key/rawkey/shiftpressed... globals the editors use.
 */

#define GCONSOLE_C

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "goattrk2.h"
#include "gui/gui.h"
#ifdef GT_PLUGIN
#include "plugin/gtplugin.h"
#endif

unsigned *scrbuffer = NULL;
int textscreenactive = 0;
int retromode = 0;
int retroatmainscreen = 0;
// Set when the retro interface's main screen has been drawn on the text
// screen; cleared when a page or a classic dialog is drawn over it. The level
// meters are drawn only on the main screen
int retromainshown = 0;
int textscreenmousex = 0;
int textscreenmousey = 0;

int key = 0;
int rawkey = 0;
int shiftpressed = 0;
int shiftonlypressed = 0;
int ctrlpressed = 0;
int altpressed = 0;
int metapressed = 0;
int cursorflashdelay = 0;
int midinoteon = 0;
int midinoteoff = 0;
unsigned midinotetime = 0;
// The MIDI channel (0-15) of the note in midinoteon / midinoteoff
int midinotechannel = 0;
int menucommand = 0;
int mouseb = 0;
int prevmouseb = 0;
int mousewheel = 0;
int mousex = 0;
int mousey = 0;
int mouseheld = 0;

// Window size settings (saved in the config file)
unsigned bigwindow = 1;           // Initial size as a multiple of 1x (-w option)
unsigned bigwindowcmdline = 0;    // -w was given on the command line
unsigned windowedxsize = 0;       // Size of the window when not fullscreen,
unsigned windowedysize = 0;       // 0 = automatic
unsigned windowposset = 0;        // Where it was last (not fullscreen or
int windowedxpos = 0;             // maximized; windowposset 0 = not known:
int windowedypos = 0;             // centered)
unsigned textsizepercent = 100;   // Text size (CTRL/CMD + and -)
unsigned retrogradient = 0;       // Retro screen background: 0 = black,
                                  // 1 = gradient (View menu)
// Retro level meter colors at the bottom, middle and top, 0xAARRGGBB (AA =
// opacity: 00 clear, FF solid). View > Meter Colors
unsigned retrometercolors[3] = { DEFAULT_METERBOTTOM, DEFAULT_METERMIDDLE, DEFAULT_METERTOP };
// The user's own meter color presets (bottom, middle, top), all 0 = empty
unsigned retrometerpresets[METERPRESETS][3];

/*
 * Key event queue
 */

#define KEYQUEUESIZE 64

typedef struct
{
  int rawkey;
  int key;
  int mods;
  int textpending;   // A keydown that may still get its typed character
} KEYEVENT;

static KEYEVENT keyqueue[KEYQUEUESIZE];
static int keyqueuehead = 0;
static int keyqueuetail = 0;

static int keyqueuecount(void)
{
  return (keyqueuetail - keyqueuehead + KEYQUEUESIZE) % KEYQUEUESIZE;
}

void input_pushkey(int newrawkey, int newkey, int mods)
{
  KEYEVENT *e;

  if (keyqueuecount() >= KEYQUEUESIZE - 1) return;
  e = &keyqueue[keyqueuetail];
  e->rawkey = newrawkey;
  e->key = newkey;
  e->mods = mods;
  // Keys that produce a character get it from the text input event that
  // follows the keydown
  e->textpending = (newkey == 0);
  keyqueuetail = (keyqueuetail + 1) % KEYQUEUESIZE;
}

void input_pushtext(int newkey, int mods)
{
  if (keyqueuecount())
  {
    KEYEVENT *last = &keyqueue[(keyqueuetail + KEYQUEUESIZE - 1) % KEYQUEUESIZE];
    if (last->textpending)
    {
      last->key = newkey;
      last->textpending = 0;
      return;
    }
  }
  input_pushkey(0, newkey, mods);
  keyqueue[(keyqueuetail + KEYQUEUESIZE - 1) % KEYQUEUESIZE].textpending = 0;
}

void input_clearkeys(void)
{
  keyqueuehead = keyqueuetail = 0;
}

int input_nextkey(void)
{
  KEYEVENT e;

  key = 0;
  rawkey = 0;
  shiftpressed = 0;
  shiftonlypressed = 0;
  ctrlpressed = 0;
  altpressed = 0;
  metapressed = 0;
  menucommand = 0;

  if (!keyqueuecount()) return 0;
  e = keyqueue[keyqueuehead];
  keyqueuehead = (keyqueuehead + 1) % KEYQUEUESIZE;

  key = e.key;
  rawkey = e.rawkey;
  // CTRL works like SHIFT in the editors (as in all GoatTracker versions)
  if (e.mods & (KMOD_GT_SHIFT | KMOD_GT_CTRL)) shiftpressed = 1;
  if (e.mods & KMOD_GT_SHIFT) shiftonlypressed = 1;
  if (e.mods & KMOD_GT_CTRL) ctrlpressed = 1;
  if (e.mods & KMOD_GT_ALT) altpressed = 1;
  if (e.mods & KMOD_GT_META) metapressed = 1;

  // CMD key shortcuts (macOS). They work like the keys they stand for.
  // Other CMD combinations are ignored, so that e.g. CMD+H (hide) or CMD+M
  // (minimize) don't also enter a note. File, view and other commands are
  // handled by the GUI before the key gets here.
  if ((metapressed) && (!ctrlpressed) && (!altpressed) && (rawkey))
  {
    switch (rawkey)
    {
      case SDLK_1:
      case SDLK_2:
      case SDLK_3:
      case SDLK_4:
      case SDLK_5:
      case SDLK_6:
      case SDLK_7:
      case SDLK_8:
      case SDLK_9:
      // CMD+1-9 = F1-F9, also with SHIFT
      rawkey = SDLK_F1 + rawkey - SDLK_1;
      break;

      case SDLK_x:
      case SDLK_c:
      case SDLK_v:
      // Cut, copy & paste = SHIFT+X,C,V in every editor
      if (shiftpressed) rawkey = 0;
      else shiftpressed = 1;
      break;

      case SDLK_z:
      if (!shiftpressed) menucommand = MENUCMD_UNDO;
      rawkey = 0;
      break;

      case SDLK_RETURN:
      case SDLK_KP_ENTER:
      // CMD+RETURN = CTRL+RETURN (test note, start position)
      ctrlpressed = 1;
      shiftpressed = 1;
      break;

      case SDLK_LEFT:
      case SDLK_RIGHT:
      case SDLK_UP:
      case SDLK_DOWN:
      // CMD + cursor keys = CTRL + cursor keys (nudging notes in the pattern
      // editor): macOS keeps CTRL + cursor keys for Mission Control
      ctrlpressed = 1;
      shiftpressed = 1;
      break;

      case SDLK_f:
      menucommand = MENUCMD_FOLLOW;
      rawkey = 0;
      break;

      case SDLK_a:
      menucommand = MENUCMD_SELECTALL;
      rawkey = 0;
      break;

      default:
      rawkey = 0;
      break;
    }
    key = 0;
    metapressed = 0;
  }

  if (rawkey == SDLK_KP_ENTER)
  {
    key = KEY_ENTER;
    rawkey = SDLK_RETURN;
  }

  if (rawkey == SDLK_KP_0) key = '0';
  if (rawkey == SDLK_KP_1) key = '1';
  if (rawkey == SDLK_KP_2) key = '2';
  if (rawkey == SDLK_KP_3) key = '3';
  if (rawkey == SDLK_KP_4) key = '4';
  if (rawkey == SDLK_KP_5) key = '5';
  if (rawkey == SDLK_KP_6) key = '6';
  if (rawkey == SDLK_KP_7) key = '7';
  if (rawkey == SDLK_KP_8) key = '8';
  if (rawkey == SDLK_KP_9) key = '9';

  if (rawkey == SDLK_INSERT)
    // some keyboard/systems provide a key code that gets interpreted as additional keystroke
    // in some cases (table editor). Observed with a Dell USB keyboard on macOS 11.
    key = 0;

  // macbook keyboard insert using alt/option+backspace or fn+shift+backspace.
  // In the pattern editor SHIFT+DEL clears the note and command instead (DEL
  // and BACKSPACE are swapped there; CTRL+BACKSPACE inserts)
  if ((rawkey == SDLK_BACKSPACE && altpressed) ||
      (rawkey == SDLK_DELETE && shiftpressed && editorInfo.editmode != EDIT_PATTERN))
  {
    rawkey = SDLK_INSERT;
    key = 0;
  }

  return 1;
}

// MIDI CC 60 in the tracker: the instrument knob. Value 1 = the next
// instrument, 65 = the one before (01-3F)
static void instrumentknob(int value)
{
  int steps = midi_relativesteps(value);

  if (!steps) return;
  while ((steps > 0) && (editorInfo.einum < MAX_INSTR - 1))
  {
    nextinstr();
    steps--;
  }
  while ((steps < 0) && (editorInfo.einum > 1))
  {
    previnstr();
    steps++;
  }
  gui_message("Instrument %02X (MIDI CC 60)", editorInfo.einum);
}

int input_nextmidi(void)
{
  int midinote;
  int event;
  int note;

  midinoteon = 0;
  midinoteoff = 0;

  // MIDI note 60 (middle C) is C-4
  for (;;)
  {
    event = midi_getevent(&midinote);
    if (!event) return 0;
    // Control changes (knobs) go to the SID designer at once, if it is
    // shown. In the tracker CC 60 chooses the instrument, the others are
    // dropped
    if (event == MIDI_CONTROL)
    {
      if (gui_designeractive()) gui_designermidicc(midinote, midi_eventvalue());
      else if (midinote == 60) instrumentknob(midi_eventvalue());
      continue;
    }
    note = FIRSTNOTE + midinote - 12;
    if ((note >= FIRSTNOTE) && (note <= LASTNOTE)) break;
  }
  if (event == MIDI_NOTEON) midinoteon = note;
  else midinoteoff = note;
  midinotetime = midi_eventtime();
  midinotechannel = midi_eventchannel();
  return 1;
}

/*
 * Screen (window) setup
 */

int initscreen(void)
{
  if (win_openwindow("GoatTracker Ultimate", NULL) != BME_OK) return 0;

  scrbuffer = (unsigned*)malloc(MAX_COLUMNS * MAX_ROWS * sizeof(unsigned));
  if (!scrbuffer) return 0;
  clearscreen();

  if (!gui_init()) return 0;
#ifndef GT_PLUGIN
  // The plugin closes its window itself (an atexit() function would run
  // when the host unloads it)
  atexit(closescreen);
#endif
  return 1;
}

void closescreen(void)
{
  gui_shutdown();
  textscreenactive = 0;
  if (scrbuffer)
  {
    free(scrbuffer);
    scrbuffer = NULL;
  }
}

void changetextsize(int dir)
{
  gui_changetextsize(dir);
}

void togglefullscreen(void)
{
  gui_togglefullscreen();
}

/*
 * The classic text screen
 */

static void setcharcolor(unsigned *dptr, short ch, short color)
{
  *dptr = (ch & 0xff) | (color << 16);
}

static void setcolor(unsigned *dptr, short color)
{
  *dptr = (*dptr & 0xffff) | (color << 16);
}

void clearscreen(void)
{
  int c;
  unsigned *dptr = scrbuffer;

  retromainshown = 0;
  if (!dptr) return;
  for (c = 0; c < MAX_ROWS * MAX_COLUMNS; c++)
  {
    setcharcolor(dptr, 0x20, 0x7);
    dptr++;
  }
}

void printtext(int x, int y, int color, const char *text)
{
  unsigned *dptr;

  if (!scrbuffer) return;
  if ((y < 0) || (y >= MAX_ROWS)) return;
  textscreenactive = 1;
  dptr = scrbuffer + (x + y * MAX_COLUMNS);
  while (*text)
  {
    if ((x >= 0) && (x < MAX_COLUMNS)) setcharcolor(dptr, *text, color);
    dptr++;
    text++;
    x++;
  }
}

void printtextc(int y, int color, const char *text)
{
  int x = (MAX_COLUMNS - strlen(text)) / 2;

  printtext(x, y, color, text);
}

void printtextcp(int cp, int y, int color, const char *text)
{
  int x = cp - (strlen(text) / 2);

  printtext(x, y, color, text);
}

void printtextcentered(int centerx, int y, int color, const char *text)
{
  printtextcentereddown(centerx, y, 0, color, text);
}

void printtextcentereddown(int centerx, int y, int thirds, int color, const char *text)
{
  int len = (int)strlen(text);
  int x = centerx - len / 2;
  unsigned flags = CELL_DOWN((thirds < 0) ? 0 : (thirds > 2) ? 2 : thirds);
  unsigned *dptr;

  printtext(x, y, color, text);
  if (len & 1) flags |= CELL_HALFLEFT;
  if ((!flags) || (!scrbuffer) || (y < 0) || (y >= MAX_ROWS)) return;
  dptr = scrbuffer + y * MAX_COLUMNS;
  for (; len; len--, x++)
  {
    if ((x >= 0) && (x < MAX_COLUMNS)) dptr[x] |= flags;
  }
}

void printblank(int x, int y, int length)
{
  printblankc(x, y, 0x7, length);
}

void printblankc(int x, int y, int color, int length)
{
  unsigned *dptr;

  if (!scrbuffer) return;
  if ((y < 0) || (y >= MAX_ROWS)) return;
  dptr = scrbuffer + (x + y * MAX_COLUMNS);
  while (length--)
  {
    if ((x >= 0) && (x < MAX_COLUMNS)) setcharcolor(dptr, 0x20, color);
    dptr++;
    x++;
  }
}

void drawbox(int x, int y, int color, int sx, int sy)
{
  unsigned *dptr;
  unsigned *dptr2;
  int counter;

  if (!scrbuffer) return;
  if ((y < 0) || (y + sy > MAX_ROWS) || (x < 0) || (x + sx > MAX_COLUMNS)) return;
  textscreenactive = 1;
  retromainshown = 0;
  dptr = scrbuffer + (x + y * MAX_COLUMNS);
  dptr2 = scrbuffer + ((x + sx - 1) + y * MAX_COLUMNS);
  counter = sy;

  while (counter--)
  {
    setcharcolor(dptr, '|', color);
    setcharcolor(dptr2, '|', color);
    dptr += MAX_COLUMNS;
    dptr2 += MAX_COLUMNS;
  }

  dptr = scrbuffer + (x + y * MAX_COLUMNS);
  dptr2 = scrbuffer + (x + (y + sy - 1) * MAX_COLUMNS);
  counter = sx;

  while (counter--)
  {
    setcharcolor(dptr, '-', color);
    setcharcolor(dptr2, '-', color);
    dptr++;
    dptr2++;
  }

  dptr = scrbuffer + (x + y * MAX_COLUMNS);
  setcharcolor(dptr, '+', color);

  dptr = scrbuffer + ((x + sx - 1) + y * MAX_COLUMNS);
  setcharcolor(dptr, '+', color);

  dptr = scrbuffer + (x + (y + sy - 1) * MAX_COLUMNS);
  setcharcolor(dptr, '+', color);

  dptr = scrbuffer + ((x + sx - 1) + (y + sy - 1) * MAX_COLUMNS);
  setcharcolor(dptr, '+', color);
}

void printbg(int x, int y, int color, int length)
{
  unsigned *dptr;

  if (!scrbuffer) return;
  if ((y < 0) || (y >= MAX_ROWS)) return;
  dptr = scrbuffer + (x + y * MAX_COLUMNS);
  while (length--)
  {
    if ((x >= 0) && (x < MAX_COLUMNS)) setcolor(dptr, 15 | (color << 4));
    dptr++;
    x++;
  }
}

// Shows the text screen: runs one frame of the user interface with the text
// screen drawn on top
void fliptoscreen(void)
{
  if (!textscreenactive) return;
  gui_frame(1);
}

// Classic dialogs call this in their loops: run one frame (showing the text
// screen) and take one key or mouse event
void getkey(void)
{
  int hadkey;

  cursorflashdelay += win_getspeed(50);

#ifdef GT_PLUGIN
  gtplugin_checkquit();
#endif
  gui_frame(textscreenactive);

  prevmouseb = mouseb;
  mouseb = gui_textscreenmousebuttons();
  mousewheel = gui_textscreenmousewheel();
  mousex = textscreenmousex;
  mousey = textscreenmousey;
  if (mouseb) mouseheld++;
  else mouseheld = 0;

  hadkey = input_nextkey();
  midinoteon = 0;
  midinoteoff = 0;
  if (!hadkey) input_nextmidi();
}
