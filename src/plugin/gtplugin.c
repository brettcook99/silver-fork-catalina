/*
 * GoatTracker plugin build: the core library's side of the plugin interface
 * (gtcore_api.h), except the audio (gtplugin_audio.c)
 *
 * The GoatTracker thread runs the tracker's main() just as the standalone
 * program does, with SDL's offscreen video driver: the window is drawn into
 * memory by SDL's software renderer, and the plugin's editor shows that
 * picture. Keys, mouse and dropped files from the editor are queued here and
 * turned into SDL events on the GoatTracker thread, so the tracker's own
 * event handling sees the same events as in the standalone program.
 */

#include "goattrk2.h"
#include "gui/gui.h"
#include "gtcore_api.h"
#include "gtplugin.h"
#include <setjmp.h>
#include <stdarg.h>

#ifdef _WIN32
#include <windows.h>
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif

extern unsigned residdelay;
extern unsigned customclockrate;
extern float equaldivisionsperoctave;
extern char specialnotenames[];
extern char scalatuningfilepath[];

char gtplugin_resourcedir[MAX_PATHNAME];
char gtplugin_configdir[MAX_PATHNAME];

static gtcore_callbacks callbacks;
static SDL_Thread *gtthread = NULL;
static SDL_atomic_t quitflag;
static SDL_atomic_t startdone;
static SDL_atomic_t threaddone;
static SDL_atomic_t visible;
static volatile int startok = 0;
static volatile Uint32 quitstart = 0;
static jmp_buf quitjump;
static volatile int quitjumpset = 0;
static unsigned instanceid = 0;

/*
 * Editor size
 */

static SDL_mutex *viewmutex = NULL;
static int viewlw = 1200;
static int viewlh = 777;
static float viewscale = 1.0f;
static int viewchanged = 0;

// Logical size -> physical pixels
static void physicalsize(int lw, int lh, float scale, int *pw, int *ph)
{
  *pw = (int)(lw * scale + 0.5f);
  *ph = (int)(lh * scale + 0.5f);
  if (*pw < 64) *pw = 64;
  if (*ph < 64) *ph = 64;
}

void gtplugin_viewsize(int *pw, int *ph, float *scale)
{
  SDL_LockMutex(viewmutex);
  physicalsize(viewlw, viewlh, viewscale, pw, ph);
  *scale = viewscale;
  SDL_UnlockMutex(viewmutex);
}

int gtplugin_editorvisible(void)
{
  return SDL_AtomicGet(&visible);
}

void gtplugin_requestresize(int w, int h)
{
  if (callbacks.requestresize) callbacks.requestresize(callbacks.user, w, h);
}

/*
 * Files
 */

void gtplugin_configpath(const char *name, char *out, int outsize)
{
  size_t len = strlen(gtplugin_configdir);
  int slash = (len) && ((gtplugin_configdir[len - 1] == '/') || (gtplugin_configdir[len - 1] == '\\'));

  snprintf(out, outsize, "%s%s%s", gtplugin_configdir, ((len) && (!slash)) ? "/" : "", name);
}

void gtplugin_tempname(const char *name, char *out, int outsize)
{
  const char *tempdir = getenv("TMPDIR");

  if ((!tempdir) || (!tempdir[0])) tempdir = getenv("TEMP");
  if ((!tempdir) || (!tempdir[0])) tempdir = getenv("TMP");
  if ((!tempdir) || (!tempdir[0])) tempdir = gtplugin_configdir;
#ifndef _WIN32
  if ((!tempdir) || (!tempdir[0])) tempdir = "/tmp";
#endif
  snprintf(out, outsize, "%s%sgt%d-%08x-%s", tempdir,
    ((tempdir[0]) && ((tempdir[strlen(tempdir) - 1] == '/') || (tempdir[strlen(tempdir) - 1] == '\\'))) ? "" : "/",
    (int)getpid(), instanceid, name);
}

// A line in gtcore.log in the config folder
void gtplugin_log(const char *fmt, ...)
{
  char path[MAX_PATHNAME];
  FILE *handle;
  va_list ap;

  gtplugin_configpath("gtcore.log", path, sizeof path);
  handle = fopen(path, "at");
  if (!handle) return;
  va_start(ap, fmt);
  vfprintf(handle, fmt, ap);
  va_end(ap);
  fputc(10, handle);
  fclose(handle);
}

/*
 * Quitting
 */

int gtplugin_quitting(void)
{
  return SDL_AtomicGet(&quitflag);
}

void gtplugin_checkquit(void)
{
  if (!SDL_AtomicGet(&quitflag)) return;
  // Every loop of the program (the main loop, the classic dialogs) ends on
  // these
  exitprogram = 1;
  win_quitted = 1;
  // A loop that doesn't: leave it anyway (between two frames)
  if ((quitjumpset) && (SDL_GetTicks() - quitstart > 1500)) longjmp(quitjump, 1);
}

/*
 * Input from the editor
 */

#define INPUTQUEUESIZE 512
#define MAXHELDKEYS 16

static SDL_mutex *inputmutex = NULL;
static gtcore_inputevent inputqueue[INPUTQUEUESIZE];
static int inputread = 0;
static int inputwrite = 0;
static int heldsym[MAXHELDKEYS];
static int heldscan[MAXHELDKEYS];
static int numheld = 0;
static Uint32 mousebuttons = 0;
static int lastmousex = 0;
static int lastmousey = 0;
static int mouseinside = 0;
static float wheelaccumx = 0.0f;
static float wheelaccumy = 0.0f;

// File dialog answers (GTIN_FILEDIALOG)
static volatile int dialogid = 0;
static volatile int dialogdone = 0;
static char dialogpath[MAX_PATHNAME];

static char *copytext(const char *text)
{
  char *copy;
  size_t len;

  if (!text) return NULL;
  len = strlen(text);
  copy = (char *)malloc(len + 1);
  if (copy) memcpy(copy, text, len + 1);
  return copy;
}

static void holdkey(int sym, int scan, int down)
{
  int c;

  for (c = 0; c < numheld; c++)
  {
    if ((heldsym[c] == sym) && (heldscan[c] == scan))
    {
      if (!down)
      {
        heldsym[c] = heldsym[numheld - 1];
        heldscan[c] = heldscan[numheld - 1];
        numheld--;
      }
      return;
    }
  }
  if ((down) && (numheld < MAXHELDKEYS))
  {
    heldsym[numheld] = sym;
    heldscan[numheld] = scan;
    numheld++;
  }
}

static void pushkey(Uint32 windowid, Uint32 type, int sym, int scan, int mods, int repeat)
{
  SDL_Event e;

  memset(&e, 0, sizeof e);
  e.type = type;
  e.key.timestamp = SDL_GetTicks();
  e.key.windowID = windowid;
  e.key.state = (type == SDL_KEYDOWN) ? SDL_PRESSED : SDL_RELEASED;
  e.key.repeat = repeat ? 1 : 0;
  e.key.keysym.sym = (SDL_Keycode)sym;
  e.key.keysym.scancode = (SDL_Scancode)scan;
  e.key.keysym.mod = (Uint16)mods;
  SDL_SetModState((SDL_Keymod)mods);
  SDL_PushEvent(&e);
}

static void pushwindowevent(Uint32 windowid, Uint8 event)
{
  SDL_Event e;

  memset(&e, 0, sizeof e);
  e.type = SDL_WINDOWEVENT;
  e.window.timestamp = SDL_GetTicks();
  e.window.windowID = windowid;
  e.window.event = event;
  SDL_PushEvent(&e);
}

static void pushmotion(Uint32 windowid, int x, int y)
{
  SDL_Event e;

  if (!mouseinside)
  {
    mouseinside = 1;
    pushwindowevent(windowid, SDL_WINDOWEVENT_ENTER);
  }
  memset(&e, 0, sizeof e);
  e.type = SDL_MOUSEMOTION;
  e.motion.timestamp = SDL_GetTicks();
  e.motion.windowID = windowid;
  e.motion.state = mousebuttons;
  e.motion.x = x;
  e.motion.y = y;
  e.motion.xrel = x - lastmousex;
  e.motion.yrel = y - lastmousey;
  lastmousex = x;
  lastmousey = y;
  SDL_PushEvent(&e);
}

static void releasekeys(Uint32 windowid)
{
  while (numheld)
  {
    numheld--;
    pushkey(windowid, SDL_KEYUP, heldsym[numheld], heldscan[numheld], 0, 0);
  }
  SDL_SetModState(KMOD_NONE);
}

static void feedinput(Uint32 windowid, const gtcore_inputevent *in)
{
  SDL_Event e;

  switch (in->type)
  {
    case GTIN_KEYDOWN:
    holdkey(in->sym, in->scancode, 1);
    pushkey(windowid, SDL_KEYDOWN, in->sym, in->scancode, in->mods, in->repeat);
    // The typed text right after its key (the tracker joins the two)
    if ((in->text) && (in->text[0]))
    {
      memset(&e, 0, sizeof e);
      e.type = SDL_TEXTINPUT;
      e.text.timestamp = SDL_GetTicks();
      e.text.windowID = windowid;
      SDL_strlcpy(e.text.text, in->text, sizeof e.text.text);
      SDL_PushEvent(&e);
    }
    break;

    case GTIN_KEYUP:
    holdkey(in->sym, in->scancode, 0);
    pushkey(windowid, SDL_KEYUP, in->sym, in->scancode, in->mods, 0);
    break;

    case GTIN_MODS:
    SDL_SetModState((SDL_Keymod)in->mods);
    break;

    case GTIN_MOUSEMOVE:
    pushmotion(windowid, in->x, in->y);
    break;

    case GTIN_MOUSEDOWN:
    case GTIN_MOUSEUP:
    // The position first: ImGui takes it from motion events only
    pushmotion(windowid, in->x, in->y);
    if ((in->button < 1) || (in->button > 5)) break;
    if (in->type == GTIN_MOUSEDOWN) mousebuttons |= SDL_BUTTON(in->button);
    else mousebuttons &= ~SDL_BUTTON(in->button);
    memset(&e, 0, sizeof e);
    e.type = (in->type == GTIN_MOUSEDOWN) ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
    e.button.timestamp = SDL_GetTicks();
    e.button.windowID = windowid;
    e.button.button = (Uint8)in->button;
    e.button.state = (in->type == GTIN_MOUSEDOWN) ? SDL_PRESSED : SDL_RELEASED;
    e.button.clicks = (Uint8)(in->clicks > 0 ? in->clicks : 1);
    e.button.x = in->x;
    e.button.y = in->y;
    SDL_PushEvent(&e);
    break;

    case GTIN_WHEEL:
    pushmotion(windowid, in->x, in->y);
    wheelaccumx += in->wheelx;
    wheelaccumy += in->wheely;
    memset(&e, 0, sizeof e);
    e.type = SDL_MOUSEWHEEL;
    e.wheel.timestamp = SDL_GetTicks();
    e.wheel.windowID = windowid;
    e.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
    e.wheel.preciseX = in->wheelx;
    e.wheel.preciseY = in->wheely;
    // Whole notches for the tracker's own wheel handling
    e.wheel.x = (int)wheelaccumx;
    e.wheel.y = (int)wheelaccumy;
    wheelaccumx -= (float)e.wheel.x;
    wheelaccumy -= (float)e.wheel.y;
    e.wheel.mouseX = in->x;
    e.wheel.mouseY = in->y;
    SDL_PushEvent(&e);
    break;

    case GTIN_DROPFILE:
    if ((in->text) && (in->text[0]))
    {
      memset(&e, 0, sizeof e);
      e.type = SDL_DROPFILE;
      e.drop.timestamp = SDL_GetTicks();
      // A window ID: the file is opened (without one it would be played too)
      e.drop.windowID = windowid;
      e.drop.file = SDL_strdup(in->text);
      if (SDL_PushEvent(&e) <= 0) SDL_free(e.drop.file);
    }
    break;

    case GTIN_FOCUS:
    if (!in->focus) releasekeys(windowid);
    pushwindowevent(windowid, in->focus ? SDL_WINDOWEVENT_FOCUS_GAINED : SDL_WINDOWEVENT_FOCUS_LOST);
    break;

    case GTIN_LEAVE:
    if (mouseinside)
    {
      mouseinside = 0;
      pushwindowevent(windowid, SDL_WINDOWEVENT_LEAVE);
    }
    break;

    case GTIN_FILEDIALOG:
    if (in->id == dialogid)
    {
      if (in->text) SDL_strlcpy(dialogpath, in->text, sizeof dialogpath);
      else dialogpath[0] = 0;
      dialogdone = 1;
    }
    break;
  }
}

void gtplugin_feedevents(SDL_Window *window)
{
  gtcore_inputevent local[64];
  int count = 0;
  int resize = 0;
  int pw = 0, ph = 0;
  float scale;
  Uint32 windowid;
  int c;

  if (!window) return;
  windowid = SDL_GetWindowID(window);

  // A new editor size
  SDL_LockMutex(viewmutex);
  if (viewchanged)
  {
    physicalsize(viewlw, viewlh, viewscale, &pw, &ph);
    viewchanged = 0;
    resize = 1;
  }
  scale = viewscale;
  SDL_UnlockMutex(viewmutex);
  (void)scale;
  if (resize)
  {
    int cw, ch;
    SDL_GetWindowSize(window, &cw, &ch);
    if ((cw != pw) || (ch != ph)) SDL_SetWindowSize(window, pw, ph);
  }

  // The queued input, a batch at a time (a key and its text stay together)
  for (;;)
  {
    SDL_LockMutex(inputmutex);
    count = 0;
    while ((inputread != inputwrite) && (count < 64))
    {
      local[count++] = inputqueue[inputread];
      inputread = (inputread + 1) % INPUTQUEUESIZE;
    }
    SDL_UnlockMutex(inputmutex);
    if (!count) break;
    for (c = 0; c < count; c++)
    {
      feedinput(windowid, &local[c]);
      free((void *)local[c].text);
    }
  }
}

/*
 * The editor's picture
 */

static SDL_mutex *framemutex = NULL;
static Uint32 *framepixels = NULL;
static int framew = 0;
static int frameh = 0;
static int framecap = 0;
static unsigned frameserial = 0;
static SDL_atomic_t cursorshape;

void gtplugin_publishframe(SDL_Window *window)
{
  SDL_Surface *s;
  int y;

  if (!SDL_AtomicGet(&visible)) return;
  s = SDL_GetWindowSurface(window);
  if ((!s) || (!s->pixels) || (s->format->BytesPerPixel != 4)) return;
  if (SDL_MUSTLOCK(s)) SDL_LockSurface(s);

  SDL_LockMutex(framemutex);
  if (s->w * s->h > framecap)
  {
    Uint32 *p = (Uint32 *)realloc(framepixels, (size_t)s->w * s->h * 4);
    if (p)
    {
      framepixels = p;
      framecap = s->w * s->h;
    }
  }
  if (s->w * s->h <= framecap)
  {
    // The offscreen window's pixels are 0x00RRGGBB: no alpha
    for (y = 0; y < s->h; y++)
    {
      const Uint32 *src = (const Uint32 *)((const Uint8 *)s->pixels + y * s->pitch);
      Uint32 *dest = framepixels + y * s->w;
      int x;
      for (x = 0; x < s->w; x++) dest[x] = src[x] | 0xff000000;
    }
    framew = s->w;
    frameh = s->h;
    frameserial++;
    if (!frameserial) frameserial = 1;
  }
  SDL_UnlockMutex(framemutex);

  if (SDL_MUSTLOCK(s)) SDL_UnlockSurface(s);
}

void gtplugin_setcursor(int cursor)
{
  SDL_AtomicSet(&cursorshape, cursor);
}

/*
 * Clipboard (text fields): kept here, and given to the host for the system
 * clipboard
 */

static char clipboard[1024] = "";

void gtplugin_setclipboard(const char *text)
{
  SDL_strlcpy(clipboard, text ? text : "", sizeof clipboard);
  if (callbacks.setclipboard) callbacks.setclipboard(callbacks.user, clipboard);
}

const char *gtplugin_getclipboard(void)
{
  return clipboard;
}

/*
 * Engine lock: whole-song loads and SID re-inits keep the audio thread out
 */

SDL_mutex *gtplugin_enginemutex = NULL;

void gtplugin_lockengine(void)
{
  SDL_LockMutex(gtplugin_enginemutex);
}

void gtplugin_unlockengine(void)
{
  SDL_UnlockMutex(gtplugin_enginemutex);
}

/*
 * Project state: the song (.sng file contents) and the settings that go with
 * it
 */

#define STATEMAGIC "GTPS"
#define STATEVERSION 1

static SDL_mutex *statemutex = NULL;
static SDL_cond *statecond = NULL;
// The newest state, kept up to date by the GoatTracker thread
static unsigned char *statecache = NULL;
static int statecachesize = 0;
// A state from the host waiting to be loaded
static unsigned char *statepending = NULL;
static int statependingsize = 0;
static volatile int staterequest = 0;
static unsigned long long lastsonghash = 0;
static unsigned long long lastsettingshash = 0;
static Uint32 lastdirtycheck = 0;

static unsigned long long hashtext(const char *text)
{
  unsigned long long h = 14695981039346656037ULL;
  while (*text)
  {
    h ^= (unsigned char)*text++;
    h *= 1099511628211ULL;
  }
  return h;
}

// The settings as "name=value" lines
static int settingstext(char *out, int outsize)
{
  int len = 0;
  int c;

#define ADD(...) do { if (len < outsize) len += snprintf(out + len, outsize - len, __VA_ARGS__); } while (0)
  ADD("multiplier=%u\n", editorInfo.multiplier);
  ADD("ntsc=%u\n", editorInfo.ntsc);
  ADD("sidmodel=%u\n", editorInfo.sidmodel);
  ADD("adparam=%u\n", editorInfo.adparam);
  ADD("finevibrato=%u\n", editorInfo.finevibrato);
  ADD("optimizepulse=%u\n", editorInfo.optimizepulse);
  ADD("optimizerealtime=%u\n", editorInfo.optimizerealtime);
  ADD("interpolate=%u\n", interpolate);
  ADD("residdelay=%u\n", residdelay);
  ADD("clockrate=%u\n", customclockrate);
  {
    float *f = &filterparams.distortionrate;
    for (c = 0; c < 10; c++) ADD("filter%d=%.9g\n", c, f[c]);
  }
  ADD("basepitch=%.9g\n", basepitch);
  ADD("edo=%.9g\n", equaldivisionsperoctave);
  ADD("notenames=%s\n", specialnotenames);
  ADD("scala=%s\n", scalatuningfilepath);
  ADD("stepsize=%d\n", stepsize);
  ADD("recordmode=%d\n", recordmode);
  ADD("followmode=%d\n", followmode);
  ADD("midichord=%d\n", midichord);
  ADD("editstep=%d\n", editstep);
  ADD("octave=%d\n", editorInfo.epoctave);
  ADD("instrument=%d\n", editorInfo.einum);
  ADD("subtune=%d\n", editorInfo.esnum);
  ADD("sync=%d\n", gtsync_enabled);
  ADD("startbar=%d\n", gtsync_startbar);
  ADD("route=%d\n", gtsync_route);
  ADD("recordoff=%d\n", gtsync_recordoff);
  ADD("songfile=%s\n", loadedsongfilename);
#undef ADD
  if (len >= outsize) len = outsize - 1;
  return len;
}

static void put32(unsigned char *p, unsigned v)
{
  p[0] = v & 0xff;
  p[1] = (v >> 8) & 0xff;
  p[2] = (v >> 16) & 0xff;
  p[3] = (v >> 24) & 0xff;
}

static unsigned get32(const unsigned char *p)
{
  return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24);
}

static unsigned char *readfile(const char *path, int *size)
{
  FILE *handle = fopen(path, "rb");
  unsigned char *data;
  long len;

  *size = 0;
  if (!handle) return NULL;
  fseek(handle, 0, SEEK_END);
  len = ftell(handle);
  fseek(handle, 0, SEEK_SET);
  if (len <= 0)
  {
    fclose(handle);
    return NULL;
  }
  data = (unsigned char *)malloc(len);
  if ((data) && (fread(data, 1, len, handle) != (size_t)len))
  {
    free(data);
    data = NULL;
  }
  fclose(handle);
  if (data) *size = (int)len;
  return data;
}

// The song as .sng file contents, with the tracker's own save routine
static unsigned char *songbytes(int *size)
{
  char savedname[MAX_FILENAME];
  char savedloaded[MAX_FILENAME];
  char temppath[MAX_PATHNAME];
  unsigned char *data = NULL;

  *size = 0;
  gtplugin_tempname("state.sng", temppath, sizeof temppath);
  if (strlen(temppath) >= MAX_FILENAME - 1) return NULL;
  strcpy(savedname, songfilename);
  strcpy(savedloaded, loadedsongfilename);
  strcpy(songfilename, temppath);
  if (savesong()) data = readfile(temppath, size);
  remove(temppath);
  strcpy(songfilename, savedname);
  strcpy(loadedsongfilename, savedloaded);
  return data;
}

// Builds the state into the cache
static void makestate(void)
{
  char settings[4096];
  int settingslen = settingstext(settings, sizeof settings);
  int songsize = 0;
  unsigned char *song = songbytes(&songsize);
  int total = 8 + 8 + settingslen + 8 + songsize;
  unsigned char *state = (unsigned char *)malloc(total);
  unsigned char *p = state;

  if (!state)
  {
    free(song);
    return;
  }
  memcpy(p, STATEMAGIC, 4);
  put32(p + 4, STATEVERSION);
  p += 8;
  memcpy(p, "SETT", 4);
  put32(p + 4, settingslen);
  memcpy(p + 8, settings, settingslen);
  p += 8 + settingslen;
  memcpy(p, "SONG", 4);
  put32(p + 4, songsize);
  if (songsize) memcpy(p + 8, song, songsize);
  free(song);

  SDL_LockMutex(statemutex);
  free(statecache);
  statecache = state;
  statecachesize = total;
  SDL_UnlockMutex(statemutex);

  lastsonghash = gui_songhash();
  lastsettingshash = hashtext(settings);
}

static int validstate(const unsigned char *data, int size)
{
  return (data) && (size >= 8) && (!memcmp(data, STATEMAGIC, 4));
}

static int settingint(const char *text, const char *name, int *value)
{
  char key[64];
  const char *p;

  snprintf(key, sizeof key, "\n%s=", name);
  p = strstr(text, key);
  if (!p) return 0;
  *value = atoi(p + strlen(key));
  return 1;
}

static int settingfloat(const char *text, const char *name, float *value)
{
  char key[64];
  const char *p;

  snprintf(key, sizeof key, "\n%s=", name);
  p = strstr(text, key);
  if (!p) return 0;
  *value = (float)atof(p + strlen(key));
  return 1;
}

static int settingstring(const char *text, const char *name, char *value, int size)
{
  char key[64];
  const char *p;
  int len = 0;

  snprintf(key, sizeof key, "\n%s=", name);
  p = strstr(text, key);
  if (!p) return 0;
  p += strlen(key);
  while ((p[len]) && (p[len] != '\n') && (len < size - 1))
  {
    value[len] = p[len];
    len++;
  }
  value[len] = 0;
  return 1;
}

#define SETUINT(name, var) do { int v_; if (settingint(text, name, &v_)) var = (unsigned)v_; } while (0)
#define SETINT(name, var) do { int v_; if (settingint(text, name, &v_)) var = v_; } while (0)

// Loads a state from the host (on the GoatTracker thread, at the main screen)
static void applystate(const unsigned char *data, int size)
{
  const unsigned char *p = data + 8;
  const unsigned char *end = data + size;
  char *text = NULL;
  const unsigned char *song = NULL;
  int songsize = 0;

  while (p + 8 <= end)
  {
    unsigned len = get32(p + 4);
    if (p + 8 + len > end) break;
    if (!memcmp(p, "SETT", 4))
    {
      free(text);
      text = (char *)malloc(len + 2);
      if (text)
      {
        // A newline first: every setting is found as "\nname="
        text[0] = '\n';
        memcpy(text + 1, p + 8, len);
        text[len + 1] = 0;
      }
    }
    if (!memcmp(p, "SONG", 4))
    {
      song = p + 8;
      songsize = (int)len;
    }
    p += 8 + len;
  }

  if (!gtsync_following()) stopsong();
  if ((song) && (songsize > 4))
  {
    char temppath[MAX_PATHNAME];
    char savedname[MAX_FILENAME];
    FILE *handle;

    gtplugin_tempname("load.sng", temppath, sizeof temppath);
    handle = fopen(temppath, "wb");
    if ((handle) && (strlen(temppath) < MAX_FILENAME))
    {
      fwrite(song, 1, songsize, handle);
      fclose(handle);
      strcpy(savedname, songfilename);
      strcpy(songfilename, temppath);
      // The audio thread doesn't read the song while it changes
      gtplugin_lockengine();
      loadsong();
      gtplugin_unlockengine();
      strcpy(songfilename, savedname);
      loadedsongfilename[0] = 0;
    }
    else if (handle) fclose(handle);
    remove(temppath);
  }

  if (text)
  {
    float f;
    float *fp = &filterparams.distortionrate;
    char name[16];
    int c;

    SETUINT("multiplier", editorInfo.multiplier);
    SETUINT("ntsc", editorInfo.ntsc);
    SETUINT("sidmodel", editorInfo.sidmodel);
    SETUINT("adparam", editorInfo.adparam);
    SETUINT("finevibrato", editorInfo.finevibrato);
    SETUINT("optimizepulse", editorInfo.optimizepulse);
    SETUINT("optimizerealtime", editorInfo.optimizerealtime);
    SETUINT("interpolate", interpolate);
    SETUINT("residdelay", residdelay);
    SETUINT("clockrate", customclockrate);
    for (c = 0; c < 10; c++)
    {
      snprintf(name, sizeof name, "filter%d", c);
      if (settingfloat(text, name, &f)) fp[c] = f;
    }
    if (settingfloat(text, "basepitch", &f))
    {
      float edo = 12.0f;
      settingfloat(text, "edo", &edo);
      setbasepitch(f);
      equaldivisionsperoctave = edo;
      if ((basepitch > 0.0f) && (edo != 12.0f)) calculatefreqtable();
    }
    if (settingstring(text, "scala", scalatuningfilepath, MAX_PATHNAME))
    {
      if ((scalatuningfilepath[0]) && (scalatuningfilepath[0] != '0')) readscalatuningfile();
    }
    if (settingstring(text, "notenames", specialnotenames, 186))
    {
      if (specialnotenames[1]) setspecialnotenames();
    }
    SETINT("stepsize", stepsize);
    if (stepsize < 1) stepsize = 4;
    SETINT("recordmode", recordmode);
    SETINT("followmode", followmode);
    SETINT("midichord", midichord);
    SETINT("editstep", editstep);
    SETINT("octave", editorInfo.epoctave);
    SETINT("instrument", editorInfo.einum);
    SETINT("subtune", editorInfo.esnum);
    SETINT("sync", gtsync_enabled);
    SETINT("startbar", gtsync_startbar);
    SETINT("route", gtsync_route);
    SETINT("recordoff", gtsync_recordoff);
    settingstring(text, "songfile", loadedsongfilename, MAX_FILENAME);

    // Validate
    editorInfo.sidmodel &= 1;
    editorInfo.adparam &= 0xffff;
    if (editorInfo.multiplier > 16) editorInfo.multiplier = 16;
    if (interpolate > 3) interpolate = 3;
    if (residdelay > 63) residdelay = 63;
    if (customclockrate < 100) customclockrate = 0;
    if ((editorInfo.einum < 1) || (editorInfo.einum >= MAX_INSTR)) editorInfo.einum = 1;
    if ((editorInfo.esnum < 0) || (editorInfo.esnum >= MAX_SONGS)) editorInfo.esnum = 0;
    if ((midichord < 1) || (midichord > 3)) midichord = 1;
    if ((editstep < 0) || (editstep > MAX_EDITSTEP)) editstep = 0;
    if (gtsync_startbar < 1) gtsync_startbar = 1;
    recordmode = recordmode ? 1 : 0;
    followmode = followmode ? 1 : 0;
    free(text);
  }

  // New SID settings
  do_sound_init();
  undoInitAllAreas();
  gui_marksongsaved();
  // The editor shows the loaded song from its start
  editorInfo.eppos = 0;
  editorInfo.epview = -VISIBLEPATTROWS / 2;
  editorInfo.eseditpos = 0;
  editorInfo.esview = 0;
  makestate();
}

void gtplugin_service(void)
{
  GTRECORDNOTE rn;
  static int passtop = -1;
  static int passcount = -1;

  // Notes recorded from the host's MIDI tracks
  while (gtrecord_pop(&rn))
  {
    unsigned char *row;
    int top;
    GTUNDO_OBJECT *ed;

    if ((rn.patt < 0) || (rn.patt >= MAX_PATT) || (rn.row < 0) || (rn.row >= pattlen[rn.patt])) continue;
    row = &pattern[rn.patt][rn.row * 4];
    top = currentUndoPosition;
    ed = undoCreateEditorInfo();
    if (rn.note == KEYOFF)
    {
      // Only on an empty row: a note recorded there stays
      if (row[0] == REST) row[0] = KEYOFF;
    }
    else
    {
      row[0] = (unsigned char)rn.note;
      if (rn.instr) row[1] = (unsigned char)rn.instr;
    }
    undoAreaSetCheckForChange(UNDO_AREA_PATTERN, rn.patt, UNDO_AREA_DIRTY_CHECK);
    if (undoValidateUndoAreas(ed))
    {
      // A recording pass is one undo step
      if ((!rn.newpass) && (passtop == top) && (passcount == undoCounter - 1))
        undoJoinStep(top);
      passtop = currentUndoPosition;
      passcount = undoCounter;
    }
    else undoFreeUndoObject(ed);
  }

  // A project state from the host, loaded at the main screen
  if ((statepending) && (retroatmainscreen))
  {
    unsigned char *data;
    int size;

    SDL_LockMutex(statemutex);
    data = statepending;
    size = statependingsize;
    statepending = NULL;
    statependingsize = 0;
    SDL_UnlockMutex(statemutex);
    if (validstate(data, size)) applystate(data, size);
    free(data);
  }

  // The host wants the state now
  if (staterequest)
  {
    makestate();
    SDL_LockMutex(statemutex);
    staterequest = 0;
    SDL_CondBroadcast(statecond);
    SDL_UnlockMutex(statemutex);
  }

  // Changes: the host is told that the project changed, and the state is
  // made again
  if (SDL_GetTicks() - lastdirtycheck >= 250)
  {
    char settings[4096];
    unsigned long long sh, th;

    lastdirtycheck = SDL_GetTicks();
    settingstext(settings, sizeof settings);
    sh = gui_songhash();
    th = hashtext(settings);
    if ((sh != lastsonghash) || (th != lastsettingshash))
    {
      makestate();
      if (callbacks.markdirty) callbacks.markdirty(callbacks.user);
    }
  }
}

/*
 * The GoatTracker thread
 */

void gtplugin_started(void)
{
  makestate();
  startok = 1;
  SDL_AtomicSet(&startdone, 1);
}

static int gtthreadmain(void *data)
{
  static char *argv[] = { (char *)"goattrk2", NULL };
  (void)data;

  gtplugin_setuithread();
  quitjumpset = 1;
  if (!setjmp(quitjump)) gt_main(1, argv);
  quitjumpset = 0;

  // Why it didn't start (for the host's log / the test program)
  if (!startok) gtplugin_log("GoatTracker didn't start: %s", SDL_GetError());

  closescreen();
  gtplugin_sounduninit();
  SDL_Quit();
  SDL_AtomicSet(&startdone, 1);
  SDL_AtomicSet(&threaddone, 1);
  return 0;
}

/*
 * The interface
 */

GTCORE_EXPORT int gtcore_apiversion(void)
{
  return GTCORE_API_VERSION;
}

GTCORE_EXPORT void gtcore_defaultsize(int *w, int *h)
{
  // 1.5 times the classic 800x518 screen
  *w = 1200;
  *h = 777;
}

GTCORE_EXPORT int gtcore_create(const char *resourcedir, const char *configdir, const gtcore_callbacks *cb, int w, int h, float scale)
{
  Uint32 start;

  if (gtthread) return 0;
  SDL_strlcpy(gtplugin_resourcedir, resourcedir ? resourcedir : "", MAX_PATHNAME);
  SDL_strlcpy(gtplugin_configdir, configdir ? configdir : "", MAX_PATHNAME);
  if (cb) callbacks = *cb;
  else memset(&callbacks, 0, sizeof callbacks);
  instanceid = (unsigned)(SDL_GetPerformanceCounter() ^ (Uint64)(size_t)&callbacks) & 0x7fffffff;

  viewmutex = SDL_CreateMutex();
  inputmutex = SDL_CreateMutex();
  framemutex = SDL_CreateMutex();
  statemutex = SDL_CreateMutex();
  statecond = SDL_CreateCond();
  gtplugin_enginemutex = SDL_CreateMutex();
  if ((!viewmutex) || (!inputmutex) || (!framemutex) || (!statemutex) || (!statecond) || (!gtplugin_enginemutex)) return 0;

  if ((w > 0) && (h > 0))
  {
    viewlw = w;
    viewlh = h;
  }
  if (scale > 0.1f) viewscale = scale;

  SDL_AtomicSet(&quitflag, 0);
  SDL_AtomicSet(&startdone, 0);
  SDL_AtomicSet(&threaddone, 0);
  SDL_AtomicSet(&visible, 0);

  // A big stack: the tracker has large local arrays (macOS threads get
  // 512 KB by default)
  gtthread = SDL_CreateThreadWithStackSize(gtthreadmain, "GoatTracker", 8 * 1024 * 1024, NULL);
  if (!gtthread) return 0;

  // Started (or failed) within a few seconds
  start = SDL_GetTicks();
  while ((!SDL_AtomicGet(&startdone)) && (SDL_GetTicks() - start < 10000)) SDL_Delay(5);
  return startok;
}

GTCORE_EXPORT int gtcore_destroy(void)
{
  Uint32 start;
  int done;

  if (!gtthread) return 1;
  quitstart = SDL_GetTicks();
  SDL_AtomicSet(&quitflag, 1);
  start = SDL_GetTicks();
  while ((!SDL_AtomicGet(&threaddone)) && (SDL_GetTicks() - start < 4000)) SDL_Delay(5);
  done = SDL_AtomicGet(&threaddone);
  if (!done)
  {
    // Stuck: the library stays loaded
    SDL_DetachThread(gtthread);
    gtthread = NULL;
    return 0;
  }
  SDL_WaitThread(gtthread, NULL);
  gtthread = NULL;

  SDL_LockMutex(inputmutex);
  while (inputread != inputwrite)
  {
    free((void *)inputqueue[inputread].text);
    inputread = (inputread + 1) % INPUTQUEUESIZE;
  }
  SDL_UnlockMutex(inputmutex);
  free(framepixels);
  framepixels = NULL;
  free(statecache);
  statecache = NULL;
  free(statepending);
  statepending = NULL;
  SDL_DestroyMutex(viewmutex);
  SDL_DestroyMutex(inputmutex);
  SDL_DestroyMutex(framemutex);
  SDL_DestroyMutex(statemutex);
  SDL_DestroyCond(statecond);
  SDL_DestroyMutex(gtplugin_enginemutex);
  viewmutex = inputmutex = framemutex = statemutex = gtplugin_enginemutex = NULL;
  statecond = NULL;
  return 1;
}

GTCORE_EXPORT void gtcore_input(const gtcore_inputevent *in)
{
  int next;

  if ((!in) || (!inputmutex)) return;
  if (in->type == GTIN_RESIZE)
  {
    SDL_LockMutex(viewmutex);
    if ((in->w > 0) && (in->h > 0))
    {
      viewlw = in->w;
      viewlh = in->h;
    }
    if (in->scale > 0.1f) viewscale = in->scale;
    viewchanged = 1;
    SDL_UnlockMutex(viewmutex);
    return;
  }
  SDL_LockMutex(inputmutex);
  next = (inputwrite + 1) % INPUTQUEUESIZE;
  if (next != inputread)
  {
    inputqueue[inputwrite] = *in;
    inputqueue[inputwrite].text = copytext(in->text);
    inputwrite = next;
  }
  SDL_UnlockMutex(inputmutex);
}

GTCORE_EXPORT void gtcore_editorvisible(int on)
{
  SDL_AtomicSet(&visible, on ? 1 : 0);
}

GTCORE_EXPORT unsigned gtcore_getframe(unsigned serial, void *pixels, int pitch, int maxw, int maxh, int *w, int *h)
{
  unsigned result = 0;

  if (!framemutex) return 0;
  SDL_LockMutex(framemutex);
  *w = framew;
  *h = frameh;
  if ((frameserial) && (frameserial != serial) && (pixels))
  {
    int cw = framew < maxw ? framew : maxw;
    int ch = frameh < maxh ? frameh : maxh;
    int y;
    for (y = 0; y < ch; y++)
      memcpy((Uint8 *)pixels + y * pitch, framepixels + y * framew, (size_t)cw * 4);
    result = frameserial;
  }
  SDL_UnlockMutex(framemutex);
  return result;
}

GTCORE_EXPORT int gtcore_getcursor(void)
{
  return SDL_AtomicGet(&cursorshape);
}

GTCORE_EXPORT int gtcore_getstate(void **data, int *size)
{
  unsigned char *copy = NULL;
  int copysize = 0;

  *data = NULL;
  *size = 0;
  if (!statemutex) return 0;

  // The newest state: the GoatTracker thread makes it between two frames.
  // If it is busy, the one made last
  SDL_LockMutex(statemutex);
  if ((!statepending) && (SDL_AtomicGet(&startdone)) && (!SDL_AtomicGet(&threaddone)))
  {
    staterequest = 1;
    SDL_CondWaitTimeout(statecond, statemutex, 150);
  }
  // A state not loaded yet is the newest
  if (statepending)
  {
    copy = (unsigned char *)malloc(statependingsize);
    if (copy)
    {
      memcpy(copy, statepending, statependingsize);
      copysize = statependingsize;
    }
  }
  else if (statecache)
  {
    copy = (unsigned char *)malloc(statecachesize);
    if (copy)
    {
      memcpy(copy, statecache, statecachesize);
      copysize = statecachesize;
    }
  }
  SDL_UnlockMutex(statemutex);

  *data = copy;
  *size = copysize;
  return copy != NULL;
}

GTCORE_EXPORT void gtcore_free(void *data)
{
  free(data);
}

GTCORE_EXPORT int gtcore_setstate(const void *data, int size)
{
  unsigned char *copy;

  if ((!statemutex) || (!validstate((const unsigned char *)data, size))) return 0;
  copy = (unsigned char *)malloc(size);
  if (!copy) return 0;
  memcpy(copy, data, size);
  SDL_LockMutex(statemutex);
  free(statepending);
  statepending = copy;
  statependingsize = size;
  SDL_UnlockMutex(statemutex);
  return 1;
}
