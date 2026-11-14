/*
 * GoatTracker core library interface (plugin builds)
 *
 * The plugin shell (VST3 / AAX / AU, built with JUCE) loads a private copy of
 * the core library (gtcore.dll / gtcore.dylib) for each plugin instance, so
 * that every instance has its own song, editor and SID: GoatTracker keeps all
 * of that in global variables. Only plain C types cross this interface, and
 * memory is always freed by the side that allocated it.
 *
 * Threads:
 * - gtcore_create() starts the GoatTracker thread, which runs the program as
 *   the standalone version does (its main loop and the classic dialogs' loops)
 *   and draws its window into memory
 * - gtcore_process() is called from the host's audio thread
 * - everything else from the host's main (message) thread
 */

#ifndef GTCORE_API_H
#define GTCORE_API_H

#ifdef __cplusplus
extern "C" {
#endif

#define GTCORE_API_VERSION 1

#if defined(_WIN32)
#define GTCORE_EXPORT __declspec(dllexport)
#else
#define GTCORE_EXPORT __attribute__((visibility("default")))
#endif

typedef struct
{
  void *user;
  // The editor asks for another size (logical pixels), e.g. CTRL+ / CTRL-
  void (*requestresize)(void *user, int w, int h);
  // The song or the settings changed: the host should save the project
  void (*markdirty)(void *user);
  // Text copied in a text field, for the system clipboard (may be NULL)
  void (*setclipboard)(void *user, const char *utf8);
  // A native file dialog (macOS; may be NULL: the built-in file selector is
  // used then). The answer comes back as a GTIN_FILEDIALOG input with the
  // same id. filter: extensions separated by ';', e.g. "sng" or "mid;midi"
  void (*filedialog)(void *user, int id, int save, const char *title, const char *folder, const char *filter);
} gtcore_callbacks;

typedef struct
{
  int valid;            // 0: no host transport information
  int playing;          // the host's transport runs
  int recording;
  double bpm;           // tempo
  double ppq;           // position (quarter notes) at the block's first sample
  int looping;          // the host loops between loopstart and loopend
  double loopstart, loopend;
  int timesignum, timesigden;
} gtcore_transport;

typedef struct
{
  int offset;           // sample within the block
  unsigned char status; // MIDI status byte (channel in the low 4 bits)
  unsigned char data1, data2;
} gtcore_midi;

// Input from the editor window
enum
{
  GTIN_KEYDOWN = 1,     // sym, scancode, mods, repeat, text (typed text, may be NULL)
  GTIN_KEYUP,           // sym, scancode, mods
  GTIN_MOUSEMOVE,       // x, y
  GTIN_MOUSEDOWN,       // x, y, button, clicks
  GTIN_MOUSEUP,         // x, y, button
  GTIN_WHEEL,           // x, y, wheelx, wheely (notches; up/away = positive)
  GTIN_DROPFILE,        // text = path
  GTIN_FOCUS,           // focus = 1 gained, 0 lost
  GTIN_LEAVE,           // the mouse left the window
  GTIN_RESIZE,          // w, h (logical pixels), scale (physical per logical)
  GTIN_FILEDIALOG,      // id, text = chosen path (NULL = cancelled)
  GTIN_MODS             // mods: modifier keys changed
};

typedef struct
{
  int type;
  int sym;              // SDL_Keycode
  int scancode;         // SDL_Scancode
  int mods;             // SDL KMOD_* bits
  int repeat;
  int x, y;             // mouse position in logical pixels
  int button;           // 1 = left, 2 = middle, 3 = right (SDL_BUTTON_*)
  int clicks;
  float wheelx, wheely;
  int w, h;
  float scale;
  int focus;
  int id;
  const char *text;     // UTF-8, copied by gtcore
} gtcore_inputevent;

// ImGui mouse cursor shapes (gtcore_getcursor())
enum
{
  GTCURSOR_ARROW = 0, GTCURSOR_TEXT, GTCURSOR_RESIZEALL, GTCURSOR_RESIZENS, GTCURSOR_RESIZEEW,
  GTCURSOR_RESIZENESW, GTCURSOR_RESIZENWSE, GTCURSOR_HAND, GTCURSOR_NOTALLOWED, GTCURSOR_NONE
};

GTCORE_EXPORT int gtcore_apiversion(void);

// The editor's default size (logical pixels)
GTCORE_EXPORT void gtcore_defaultsize(int *w, int *h);

// Starts GoatTracker. resourcedir holds the fonts/ folder; configdir is
// where goattrk2.cfg, recent.txt etc. are kept. w, h, scale: the editor's
// size as a GTIN_RESIZE input. Returns 0 if it failed
GTCORE_EXPORT int gtcore_create(const char *resourcedir, const char *configdir, const gtcore_callbacks *cb, int w, int h, float scale);

// Stops GoatTracker. Returns 1 if the library may be unloaded now (its
// thread has finished), 0 if it must be left loaded
GTCORE_EXPORT int gtcore_destroy(void);

// Audio setup (not while gtcore_process() runs)
GTCORE_EXPORT void gtcore_prepare(double samplerate, int maxblock);

// Renders samples of stereo output. midi: the block's MIDI events, sorted by
// offset
GTCORE_EXPORT void gtcore_process(float *left, float *right, int samples, const gtcore_transport *tp, const gtcore_midi *midi, int nmidi);

GTCORE_EXPORT void gtcore_input(const gtcore_inputevent *in);

// Whether the editor window is open (nothing is drawn while it is closed)
GTCORE_EXPORT void gtcore_editorvisible(int visible);

// The newest picture of the editor window: 32-bit pixels 0xAARRGGBB. If it
// changed since serial, it is copied (up to maxw x maxh pixels; pitch in
// bytes) and the new serial is returned, else 0. *w, *h: its size
GTCORE_EXPORT unsigned gtcore_getframe(unsigned serial, void *pixels, int pitch, int maxw, int maxh, int *w, int *h);

GTCORE_EXPORT int gtcore_getcursor(void);

// The plugin state (song and settings) for the host's project. *data is
// freed with gtcore_free(). Returns 0 if there is none
GTCORE_EXPORT int gtcore_getstate(void **data, int *size);
GTCORE_EXPORT void gtcore_free(void *data);
// Loads a state saved by gtcore_getstate(). Returns 0 if it isn't one
GTCORE_EXPORT int gtcore_setstate(const void *data, int size);

#ifdef __cplusplus
}
#endif

#endif
