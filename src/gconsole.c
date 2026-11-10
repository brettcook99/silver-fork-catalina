/*
 * GOATTRACKER "console" output routines
 */

#define GCONSOLE_C

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "goattrk2.h"

// Size of one character of the font (chargen.bin) in pixels
#define FONT_XSIZE 8
#define FONT_YSIZE 14

// Size of the whole screen at 1x scale (the smallest window size)
#define BASE_XSIZE (MAX_COLUMNS * FONT_XSIZE)
#define BASE_YSIZE (MAX_ROWS * FONT_YSIZE)

int gfxinitted = 0;
unsigned *scrbuffer = NULL;
unsigned *prevscrbuffer = NULL;
unsigned char *chardata = NULL;
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
int menucommand = 0;
int mouseb = 0;
int prevmouseb = 0;
unsigned mousex = 0;
unsigned mousey = 0;
unsigned mousepixelx = 0;
unsigned mousepixely = 0;
int mouseheld = 0;
int region[MAX_ROWS];

// Window size settings (saved in the config file)
unsigned bigwindow = 1;           // Initial size as a multiple of 1x (-w option)
unsigned bigwindowcmdline = 0;    // -w was given on the command line
unsigned windowedxsize = 0;       // Size of the window when not fullscreen,
unsigned windowedysize = 0;       // 0 = automatic

// Current layout of the character grid inside the window: every character
// cell is cellw * cellh pixels and the grid is centered in the window
static int cellw = FONT_XSIZE;
static int cellh = FONT_YSIZE;
static int gridx = 0;
static int gridy = 0;
static int layoutxsize = 0;
static int layoutysize = 0;

// The font scaled to the current cell size: one byte per pixel, 1 = foreground
static unsigned char *glyphcache = NULL;

// Size of the desktop, 0 if unknown
static int desktopxsize = 0;
static int desktopysize = 0;

// Text size steps (percent of 1x) used by CTRL/CMD + and -
static const int zoomsteps[] = {100, 125, 150, 175, 200, 225, 250, 275, 300, 350, 400, 500, 600};
#define NUM_ZOOMSTEPS ((int)(sizeof zoomsteps / sizeof zoomsteps[0]))

void loadexternalpalette(void);
void initicon(void);
static void initwindowsize(void);
static int setvideomode(void);
static int updatelayout(void);
static void applyvideochanges(void);

inline void setcharcolor(unsigned *dptr, short ch, short color)
{
  *dptr = (ch & 0xff) | (color << 16);
}

inline void setcolor(unsigned *dptr, short color)
{
  *dptr = (*dptr & 0xffff) | (color << 16);
}

int initscreen(void)
{
  int handle;
  const SDL_VideoInfo *info;

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) < 0)
    return 0;
  win_openwindow("GoatTracker", NULL);
  // Use the system mouse pointer: it stays sharp at any window size
  win_setmousemode(MOUSE_ALWAYS_VISIBLE);
  initicon();

  // The desktop size can only be asked before the first video mode is set
  info = SDL_GetVideoInfo();
  if ((info) && (info->current_w > 0) && (info->current_h > 0))
  {
    desktopxsize = info->current_w;
    desktopysize = info->current_h;
  }

  scrbuffer = (unsigned*)malloc(MAX_COLUMNS * MAX_ROWS * sizeof(unsigned));
  prevscrbuffer = (unsigned*)malloc(MAX_COLUMNS * MAX_ROWS * sizeof(unsigned));
  if ((!scrbuffer) || (!prevscrbuffer)) return 0;

  memset(region, 0, sizeof region);

  chardata = (unsigned char*)malloc(4096);
  if (!chardata) return 0;
  handle = io_open("chargen.bin");
  if (handle == -1) return 0;
  io_read(handle, &chardata[0], 4096);
  io_close(handle);

  gfx_loadpalette("palette.bin");
  loadexternalpalette();

  initwindowsize();
  if (!setvideomode()) return 0;
  gfx_setpalette();

  gfxinitted = 1;
  clearscreen();
  atexit(closescreen);

  // macOS menu bar (does nothing elsewhere)
  macmenu_init();
  return 1;
}

void loadexternalpalette(void)
{
  FILE *ext_f;
  if ((ext_f = fopen("custom.pal", "rt")))
  {
    int p = 0;
    char ln[100];
    strcpy(ln, "");
    fgets(ln, sizeof(ln), ext_f);

    if (strncmp("JASC-PAL", ln, 8) == 0)
    {
      int colors;
      fgets(ln, sizeof(ln), ext_f);
      fgets(ln, sizeof(ln), ext_f);
      if (sscanf(ln, "%d", &colors) == 1 && colors == 256)
      {
        while (!feof(ext_f))
        {
          int r, g, b;
          if (!fgets(ln, sizeof(ln), ext_f)) break;
          if (sscanf(ln, "%d %d %d", &r, &g, &b) == 3)
          {
            // JASC palette is 8-bit and goat palette is 6-bit
            gfx_palette[p++] = r / 4;
            gfx_palette[p++] = g / 4;
            gfx_palette[p++] = b / 4;
          }

          if (p >= 768) break;
        }
        gfx_calcpalette(64, 0, 0, 0);
      }
    }

    fclose(ext_f);
  }
}

void initicon(void)
{
#ifdef __APPLE__
  // On macOS the window icon replaces the Dock icon, so leave the app
  // bundle's icon (GoatTracker2.icns) in place instead.
  return;
#else
  int handle = io_open("goattrk2.bmp");
  if (handle != -1)
  {
    SDL_RWops *rw;
    SDL_Surface *icon;
    char *iconbuffer;
    int size;

    size = io_lseek(handle, 0, SEEK_END);
    io_lseek(handle, 0, SEEK_SET);
    iconbuffer = (char*)malloc(size);
    if (iconbuffer)
    {
      io_read(handle, iconbuffer, size);
      io_close(handle);
      rw = SDL_RWFromMem(iconbuffer, size);
      icon = SDL_LoadBMP_RW(rw, 0);
      SDL_WM_SetIcon(icon, 0);
      free(iconbuffer);
    }
  }
#endif
}
void closescreen(void)
{
  if (scrbuffer)
  {
    free(scrbuffer);
    scrbuffer = NULL;
  }
  if (prevscrbuffer)
  {
    free(prevscrbuffer);
    prevscrbuffer = NULL;
  }
  if (chardata)
  {
    free(chardata);
    chardata = NULL;
  }
  if (glyphcache)
  {
    free(glyphcache);
    glyphcache = NULL;
  }

  gfxinitted = 0;
}

/*
 * Window size & scaling
 */

// Largest character cell that fits maxx * maxy pixels with the whole grid,
// keeping the 8:14 shape of the font (rounded to whole pixels)
static void fitcell(int maxx, int maxy, int *w, int *h)
{
  int maxw = maxx / MAX_COLUMNS;
  int maxh = maxy / MAX_ROWS;

  if (maxw < 1) maxw = 1;
  if (maxh < 1) maxh = 1;

  *h = maxh;
  *w = (maxh * FONT_XSIZE * 2 + FONT_YSIZE) / (FONT_YSIZE * 2);
  if (*w > maxw)
  {
    *w = maxw;
    *h = (maxw * FONT_YSIZE * 2 + FONT_XSIZE) / (FONT_XSIZE * 2);
    if (*h > maxh) *h = maxh;
  }
  if (*w < 1) *w = 1;
  if (*h < 1) *h = 1;
}

// Window size for a text size in percent of 1x
static void zoomsize(int percent, unsigned *xsize, unsigned *ysize)
{
  *xsize = MAX_COLUMNS * ((FONT_XSIZE * percent + 50) / 100);
  *ysize = MAX_ROWS * ((FONT_YSIZE * percent + 50) / 100);
}

// Largest window that fits the given percentage of the desktop
static void fitsize(int xpercent, int ypercent, unsigned *xsize, unsigned *ysize)
{
  int w, h;

  if ((!desktopxsize) || (!desktopysize))
  {
    // Desktop size unknown: no limit other than the largest text size
    zoomsize(zoomsteps[NUM_ZOOMSTEPS-1], xsize, ysize);
    return;
  }

  fitcell(desktopxsize * xpercent / 100, desktopysize * ypercent / 100, &w, &h);
  if ((w < FONT_XSIZE) || (h < FONT_YSIZE))
  {
    w = FONT_XSIZE;
    h = FONT_YSIZE;
  }
  *xsize = w * MAX_COLUMNS;
  *ysize = h * MAX_ROWS;
}

// Window size used when nothing else is set: large, but leaves room for
// the title bar, menu bar, dock or taskbar
static void defaultsize(unsigned *xsize, unsigned *ysize)
{
  fitsize(90, 80, xsize, ysize);
}

// Keep the window at least 1x (unless the desktop is smaller than that)
static void clampminsize(unsigned *xsize, unsigned *ysize)
{
  unsigned minx = BASE_XSIZE;
  unsigned miny = BASE_YSIZE;

  if ((desktopxsize) && ((unsigned)desktopxsize < minx)) minx = desktopxsize;
  if ((desktopysize) && ((unsigned)desktopysize < miny)) miny = desktopysize;
  if (*xsize < minx) *xsize = minx;
  if (*ysize < miny) *ysize = miny;
}

static void initwindowsize(void)
{
  if (bigwindow < 1) bigwindow = 1;

  if (bigwindowcmdline)
    zoomsize(bigwindow * 100, &windowedxsize, &windowedysize);
  else if ((windowedxsize) && (windowedysize))
    ; // Use the size saved in the config file
  else if (bigwindow > 1)
    zoomsize(bigwindow * 100, &windowedxsize, &windowedysize);
  else
    defaultsize(&windowedxsize, &windowedysize);

  // A size too big for this desktop (for example saved while using a larger
  // monitor) is replaced by the largest size that fits
  if ((desktopxsize) && (desktopysize) &&
      ((windowedxsize > (unsigned)desktopxsize) || (windowedysize > (unsigned)desktopysize)))
    fitsize(92, 88, &windowedxsize, &windowedysize);

  clampminsize(&windowedxsize, &windowedysize);
}

// Set the video mode: fullscreen at desktop resolution, or a resizable window
static int setvideomode(void)
{
  if (win_fullscreen)
  {
    unsigned xsize = desktopxsize ? desktopxsize : windowedxsize;
    unsigned ysize = desktopysize ? desktopysize : windowedysize;

    if (gfx_init(xsize, ysize, 60, GFX_FULLSCREEN)) goto MODESET;
    win_fullscreen = 0;
  }

  if (gfx_init(windowedxsize, windowedysize, 60, GFX_WINDOW | GFX_RESIZABLE)) goto MODESET;

  windowedxsize = BASE_XSIZE;
  windowedysize = BASE_YSIZE;
  if (gfx_init(windowedxsize, windowedysize, 60, GFX_WINDOW | GFX_RESIZABLE)) goto MODESET;

  gfx_screen = NULL;
  return 0;

  MODESET:
  gfx_redraw = 1;
  updatelayout();
  return 1;
}

// Recalculate the character grid layout and the scaled font if the window
// size has changed. Returns 0 if there is nothing to draw on
static int updatelayout(void)
{
  int w, h, c, px, py;
  int xsize, ysize;
  unsigned char *dptr;

  if ((!gfx_screen) || (!chardata)) return 0;

  xsize = gfx_screen->w;
  ysize = gfx_screen->h;
  if ((xsize == layoutxsize) && (ysize == layoutysize) && (glyphcache)) return 1;

  // Too small to hold one pixel per character
  if ((xsize < MAX_COLUMNS) || (ysize < MAX_ROWS)) return 0;

  fitcell(xsize, ysize, &w, &h);

  if (glyphcache) free(glyphcache);
  glyphcache = (unsigned char*)malloc(256 * w * h);
  if (!glyphcache) return 0;

  // Scale the font with nearest-neighbour sampling at pixel centers. Font
  // rows 1-14 of each 16-byte character are used, bit 7 is the leftmost pixel
  dptr = glyphcache;
  for (c = 0; c < 256; c++)
  {
    for (py = 0; py < h; py++)
    {
      unsigned char bits = chardata[c * 16 + 1 + ((2 * py + 1) * FONT_YSIZE) / (2 * h)];
      for (px = 0; px < w; px++)
        *dptr++ = (bits >> (7 - ((2 * px + 1) * FONT_XSIZE) / (2 * w))) & 1;
    }
  }

  cellw = w;
  cellh = h;
  gridx = (xsize - w * MAX_COLUMNS) / 2;
  gridy = (ysize - h * MAX_ROWS) / 2;
  layoutxsize = xsize;
  layoutysize = ysize;
  gfx_redraw = 1;
  return 1;
}

// Apply a fullscreen toggle or a window resize requested by the user
static void applyvideochanges(void)
{
  if ((!gfxinitted) || (!gfx_screen))
  {
    win_fullscreenpending = 0;
    win_resizepending = 0;
    return;
  }

  if (win_fullscreenpending)
  {
    win_fullscreenpending = 0;
    win_resizepending = 0;
    setvideomode();
    return;
  }

  if (win_resizepending)
  {
    win_resizepending = 0;
    if (gfx_fullscreen) return;

    windowedxsize = win_resizexsize;
    windowedysize = win_resizeysize;
    clampminsize(&windowedxsize, &windowedysize);
    if ((windowedxsize != gfx_screen->w) || (windowedysize != gfx_screen->h))
      setvideomode();
  }
}

// Change the text size: dir > 0 bigger, dir < 0 smaller, 0 = default size.
// In windowed mode this resizes the window; in fullscreen the screen is
// always as large as the display allows
void changetextsize(int dir)
{
  unsigned xsize = 0;
  unsigned ysize = 0;
  unsigned w, h;
  int c;

  if ((!gfxinitted) || (!gfx_screen) || (gfx_fullscreen)) return;

  if (!dir)
    defaultsize(&xsize, &ysize);
  else if (dir > 0)
  {
    unsigned maxx, maxy;

    fitsize(92, 88, &maxx, &maxy);
    for (c = 0; c < NUM_ZOOMSTEPS; c++)
    {
      zoomsize(zoomsteps[c], &w, &h);
      if ((int)(h / MAX_ROWS) > cellh)
      {
        xsize = w;
        ysize = h;
        break;
      }
    }
    // Next step doesn't fit: go to the largest size that does
    if ((!xsize) || (xsize > maxx) || (ysize > maxy))
    {
      xsize = maxx;
      ysize = maxy;
    }
    if ((int)(ysize / MAX_ROWS) <= cellh) return;
  }
  else
  {
    for (c = NUM_ZOOMSTEPS-1; c >= 0; c--)
    {
      zoomsize(zoomsteps[c], &w, &h);
      if ((int)(h / MAX_ROWS) < cellh)
      {
        xsize = w;
        ysize = h;
        break;
      }
    }
    if (!xsize) return; // Already at the smallest size
  }

  clampminsize(&xsize, &ysize);
  windowedxsize = xsize;
  windowedysize = ysize;
  if ((windowedxsize != gfx_screen->w) || (windowedysize != gfx_screen->h))
    setvideomode();
}

void togglefullscreen(void)
{
  if (!gfxinitted) return;

  win_fullscreen ^= 1;
  win_fullscreenpending = 0;
  win_resizepending = 0;
  setvideomode();
}

/*
 * Text output
 */

void clearscreen(void)
{
  int c;
  unsigned *dptr = scrbuffer;

  if (!gfxinitted) return;

  for (c = 0; c < MAX_ROWS * MAX_COLUMNS; c++)
  {
    setcharcolor(dptr, 0x20, 0x7);
    dptr++;
  }
}

void printtext(int x, int y, int color, const char *text)
{
  unsigned *dptr = scrbuffer + (x + y * MAX_COLUMNS);

  if (!gfxinitted) return;
  if (y < 0) return;
  if (y >= MAX_ROWS) return;
  while (*text)
  {
    setcharcolor(dptr, *text, color);
    dptr++;
    text++;
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


void printblank(int x, int y, int length)
{
  unsigned *dptr = scrbuffer + (x + y * MAX_COLUMNS);

  if (!gfxinitted) return;
  if (y < 0) return;
  if (y >= MAX_ROWS) return;
  while (length--)
  {
    setcharcolor(dptr, 0x20, 0x7);
    dptr++;
  }
}

void printblankc(int x, int y, int color, int length)
{
  unsigned *dptr = scrbuffer + (x + y * MAX_COLUMNS);

  if (!gfxinitted) return;
  if (y < 0) return;
  if (y >= MAX_ROWS) return;
  while (length--)
  {
    setcharcolor(dptr, 0x20, color);
    dptr++;
  }
}

void drawbox(int x, int y, int color, int sx, int sy)
{
  unsigned *dptr;
  unsigned *dptr2;
  int counter;

  if (!gfxinitted) return;
  if (y < 0) return;
  if (y >= MAX_ROWS) return;
  if (y+sy > MAX_ROWS) return;
  if ((!sx) || (!sy)) return;

  dptr = scrbuffer + (x + y * MAX_COLUMNS);
  dptr2 = scrbuffer + ((x+sx-1) + y * MAX_COLUMNS);
  counter = sy;

  while (counter--)
  {
    setcharcolor(dptr, '|', color);
    setcharcolor(dptr2, '|', color);
    dptr += MAX_COLUMNS;
    dptr2 += MAX_COLUMNS;
  }

  dptr = scrbuffer + (x + y * MAX_COLUMNS);
  dptr2 = scrbuffer + (x + (y+sy-1) * MAX_COLUMNS);
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

  dptr = scrbuffer + ((x+sx-1) + y * MAX_COLUMNS);
  setcharcolor(dptr, '+', color);

  dptr = scrbuffer + (x + (y+sy-1) * MAX_COLUMNS);
  setcharcolor(dptr, '+', color);

  dptr = scrbuffer + ((x+sx-1) + (y+sy-1) * MAX_COLUMNS);
  setcharcolor(dptr, '+', color);
}

void printbg(int x, int y, int color, int length)
{
  unsigned *dptr = scrbuffer + (x + y * MAX_COLUMNS);

  if (!gfxinitted) return;
  if (y < 0) return;
  if (y >= MAX_ROWS) return;
  while (length--)
  {
    setcolor(dptr, 15 | (color << 4));
    dptr++;
  }
}

void fliptoscreen(void)
{
  unsigned *sptr = scrbuffer;
  unsigned *cmpptr = prevscrbuffer;
  int x,y;
  int fullredraw = 0;

  if (!gfxinitted) return;
  if (!updatelayout()) return;

  // If redraw requested, clear the whole window (also the margins around
  // the character grid) and mark the whole screen changed
  if (gfx_redraw)
  {
    gfx_redraw = 0;
    fullredraw = 1;
    SDL_FillRect(gfx_screen, NULL, 0);
    memset(prevscrbuffer, 0xff, MAX_COLUMNS*MAX_ROWS*sizeof(unsigned));
  }

  if (!gfx_lock())
  {
    if (fullredraw) gfx_redraw = 1;
    return;
  }

  // Now redraw text on changed areas
  for (y = 0; y < MAX_ROWS; y++)
  {
    unsigned char *rowptr = (unsigned char*)gfx_screen->pixels + (gridy + y*cellh) * gfx_screen->pitch + gridx;

    for (x = 0; x < MAX_COLUMNS; x++)
    {
      // Check if char changed
      if (*sptr != *cmpptr)
      {
        unsigned char *gptr = glyphcache + (*sptr & 0xff) * cellw * cellh;
        unsigned char *dptr = rowptr + x*cellw;
        unsigned char bgcolor = ((*sptr) >> 20) & 0xf;
        unsigned char fgcolor = ((*sptr) >> 16) & 0xf;
        int px, py;

        *cmpptr = *sptr;
        region[y] = 1;

        for (py = 0; py < cellh; py++)
        {
          for (px = 0; px < cellw; px++)
            dptr[px] = gptr[px] ? fgcolor : bgcolor;
          gptr += cellw;
          dptr += gfx_screen->pitch;
        }
      }
      sptr++;
      cmpptr++;
    }
  }

  gfx_unlock();

  // Redraw changed screen regions
  if (fullredraw)
  {
    SDL_UpdateRect(gfx_screen, 0, 0, 0, 0);
    memset(region, 0, sizeof region);
  }
  else
  {
    for (y = 0; y < MAX_ROWS; y++)
    {
      if (region[y])
      {
        SDL_UpdateRect(gfx_screen, gridx, gridy + y*cellh, MAX_COLUMNS*cellw, cellh);
        region[y] = 0;
      }
    }
  }
}

void getkey(void)
{
  int c;
  int mx, my;
  win_asciikey = 0;
  menucommand = 0;
  cursorflashdelay += win_getspeed(50);

  applyvideochanges();
  updatelayout();

  prevmouseb = mouseb;

  // Convert mouse position to a character position. Positions in the
  // margins around the grid go to the nearest edge character
  mou_getpos(&mousepixelx, &mousepixely);
  mouseb = mou_getbuttons();
  mx = (int)mousepixelx - gridx;
  my = (int)mousepixely - gridy;
  mx = (mx < 0) ? 0 : mx / cellw;
  my = (my < 0) ? 0 : my / cellh;
  if (mx >= MAX_COLUMNS) mx = MAX_COLUMNS - 1;
  if (my >= MAX_ROWS) my = MAX_ROWS - 1;
  mousex = mx;
  mousey = my;

  if (mouseb) mouseheld++;
  else mouseheld = 0;

  key = win_asciikey;
  rawkey = 0;
  for (c = 0; c < MAX_KEYS; c++)
  {
    if (win_keytable[c])
    {
      if ((c != KEY_LEFTSHIFT) && (c != KEY_RIGHTSHIFT) &&
          (c != KEY_CTRL) && (c != KEY_RIGHTCTRL))
      {
        rawkey = c;
        win_keytable[c] = 0;
        break;
      }
    }
  }

  // MIDI input: one note event per frame, as a GoatTracker note. MIDI note
  // 60 (middle C) is C-4. Events wait in the queue while a key is handled
  midinoteon = 0;
  midinoteoff = 0;
  if ((!key) && (!rawkey))
  {
    int midinote;
    int event = midi_getevent(&midinote);
    int note = FIRSTNOTE + midinote - 12;

    if ((event) && (note >= FIRSTNOTE) && (note <= LASTNOTE))
    {
      if (event == MIDI_NOTEON) midinoteon = note;
      else midinoteoff = note;
    }
  }

  shiftpressed = 0;
  ctrlpressed = 0;
  altpressed = 0;
  metapressed = 0;
  shiftonlypressed = 0;

  if ((win_keystate[KEY_LEFTSHIFT])||(win_keystate[KEY_RIGHTSHIFT])||
      (win_keystate[KEY_CTRL])||(win_keystate[KEY_RIGHTCTRL]))
    shiftpressed = 1;

  if ((win_keystate[KEY_LEFTSHIFT]) || (win_keystate[KEY_RIGHTSHIFT]))
	  shiftonlypressed = 1;

  if ((win_keystate[KEY_CTRL]) || (win_keystate[KEY_RIGHTCTRL]))
	  ctrlpressed = 1;

  if (win_keystate[KEY_ALT] || win_keystate[KEY_RIGHTALT])
      altpressed = 1;

  if (win_keystate[SDLK_LMETA] || win_keystate[SDLK_RMETA] ||
      win_keystate[SDLK_LSUPER] || win_keystate[SDLK_RSUPER])
    metapressed = 1;

  // A clicked menu bar item (macOS) works like its key combination
  {
    int item = macmenu_getitem();

    if (item & MACMENU_PLAYSTOP)
    {
      menucommand = MENUCMD_PLAYSTOP;
      key = 0;
      rawkey = 0;
    }
    else if (item)
    {
      rawkey = item & MACMENU_KEYMASK;
      key = 0;
      shiftpressed = (item & MACMENU_SHIFT) ? 1 : 0;
      shiftonlypressed = shiftpressed;
      metapressed = (item & MACMENU_CMD) ? 1 : 0;
      ctrlpressed = 0;
      altpressed = 0;
    }
  }

  // Text size: CTRL/CMD + = bigger, - smaller, 0 default. The key is
  // consumed so that it doesn't also change instrument or enter a note.
  // The typed character is checked too: a very short tap can be released
  // within one frame, which loses the rawkey but not the character
  if ((ctrlpressed || metapressed) && (!altpressed))
  {
    int dir = 2; // 2 = not a text size key

    switch (rawkey)
    {
      case SDLK_EQUALS:
      case SDLK_PLUS:
      case SDLK_KP_PLUS:
      dir = 1;
      break;

      case SDLK_MINUS:
      case SDLK_KP_MINUS:
      dir = -1;
      break;

      case SDLK_0:
      case SDLK_KP0:
      dir = 0;
      break;

      case 0:
      if ((key == '=') || (key == '+')) dir = 1;
      if (key == '-') dir = -1;
      if (key == '0') dir = 0;
      break;
    }

    if (dir != 2)
    {
      changetextsize(dir);
      key = 0;
      rawkey = 0;
      return;
    }
  }

  // CMD key shortcuts (macOS), also shown in the menu bar. They work like
  // the keys they stand for. Other CMD combinations are ignored, so that
  // e.g. CMD+H (hide) or CMD+M (minimize) don't also enter a note
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

      case SDLK_o:
      // Load (with SHIFT: merge song)
      rawkey = SDLK_F10;
      break;

      case SDLK_s:
      rawkey = SDLK_F11;
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

      case SDLK_r:
      menucommand = MENUCMD_RECORD;
      rawkey = 0;
      break;

      case SDLK_f:
      menucommand = MENUCMD_FOLLOW;
      rawkey = 0;
      break;

      case SDLK_a:
      menucommand = MENUCMD_SELECTALL;
      rawkey = 0;
      break;

      case SDLK_i:
      menucommand = MENUCMD_IMPORTMIDI;
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

  if (rawkey == SDLK_KP0) key = '0';
  if (rawkey == SDLK_KP1) key = '1';
  if (rawkey == SDLK_KP2) key = '2';
  if (rawkey == SDLK_KP3) key = '3';
  if (rawkey == SDLK_KP4) key = '4';
  if (rawkey == SDLK_KP5) key = '5';
  if (rawkey == SDLK_KP6) key = '6';
  if (rawkey == SDLK_KP7) key = '7';
  if (rawkey == SDLK_KP8) key = '8';
  if (rawkey == SDLK_KP9) key = '9';

  if (rawkey == SDLK_INSERT)
    // some keyboard/systems provide a key code that gets interpreted as additional keystroke
    // in some cases (table editor). Observed with a Dell USB keyboard on macOS 11.
    key = 0;

  if ((rawkey == SDLK_BACKSPACE && altpressed) ||
      (rawkey == SDLK_DELETE && shiftpressed))
    // macbook keyboard insert using alt/option+backspace or fn+shift+backspace
    rawkey = SDLK_INSERT;

  if (rawkey == SDLK_DELETE && key == SDLK_LEFTPAREN)
    // macbook keyboard delete using fn+backspace
  {
    rawkey = SDLK_DELETE;
    key = KEY_DEL;
  }
}
