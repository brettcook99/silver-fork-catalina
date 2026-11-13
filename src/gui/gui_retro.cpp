// GoatTracker GUI: the retro interface. The classic 100x37 text screen
// (scrbuffer) is drawn with the original 8x14 character set and palette into
// a texture, which is scaled with whole-pixel character cells to fill the
// window, as the original GoatTracker did.

#include "gui_internal.h"

using namespace gui;

#define FONT_XSIZE 8
#define FONT_YSIZE 14
#define BASE_XSIZE (MAX_COLUMNS * FONT_XSIZE)
#define BASE_YSIZE (MAX_ROWS * FONT_YSIZE)

static unsigned char chardata[4096];
static Uint32 palette[16];           // ARGB
static bool loaded = false;
static SDL_Texture *texture = NULL;
static SDL_Renderer *texturerenderer = NULL;
static Uint32 *pixels = NULL;        // BASE_XSIZE * BASE_YSIZE
static unsigned *prevscreen = NULL;  // The screen as last drawn into pixels
static bool mouseblocked = false;

// Level meters on the main screen: a gradient bar behind each channel's
// instrument & command digits ("04000"), rising from the bottom of the
// pattern view with the voice's envelope, up to METERROWS rows. The colors
// at the bottom, middle and top, and how opaque they are, are set with
// View > Meter Colors (retrometercolors); by default nearly white at the
// bottom, dark blue at the top
#define METERROWS 7
#define METERCOLUMN 10                      // In each channel's 15 columns
#define METERWIDTH 5
#define METERBOTTOM (PATTROWY + VISIBLEPATTROWS) // The row below the patterns
#define METERHEIGHT (METERROWS * FONT_YSIZE)
#define METERFALL 300.0f                    // Pixels per second

static float meterlevel[MAX_CHN];           // Height shown, in pixels
static bool meterdrawn = false;             // Drawn into pixels last time
static Uint32 metertime = 0;
static Uint32 metercolor[METERHEIGHT];      // From the bottom up
static unsigned metermade[3];               // retrometercolors they were made of

static void makemetercolors(void);

// The background the pixels were drawn for (retrogradient)
static unsigned drawngradient = 0;

// 6-bit VGA palette value to 8 bits, as the classic version did
static Uint32 expand6(int c)
{
  if (c < 0) c = 0;
  if (c > 63) c = 63;
  return (Uint32)((c << 2) | (c & 3));
}

static void setpalette(const unsigned char *rgb6)
{
  int c;

  // Color 0 is always black
  palette[0] = 0xff000000;
  for (c = 1; c < 16; c++)
    palette[c] = 0xff000000 | (expand6(rgb6[c * 3]) << 16) | (expand6(rgb6[c * 3 + 1]) << 8) | expand6(rgb6[c * 3 + 2]);
}

// custom.pal in the current directory (JASC-PAL with 256 colors) replaces
// the palette, as in the classic version
static void loadexternalpalette(void)
{
  FILE *handle = fopen("custom.pal", "rt");
  unsigned char rgb6[48];
  char line[100];
  int colors;
  int p = 0;

  if (!handle) return;
  line[0] = 0;
  if ((!fgets(line, sizeof line, handle)) || (strncmp("JASC-PAL", line, 8)))
  {
    fclose(handle);
    return;
  }
  if ((!fgets(line, sizeof line, handle)) || (!fgets(line, sizeof line, handle)) ||
      (sscanf(line, "%d", &colors) != 1) || (colors != 256))
  {
    fclose(handle);
    return;
  }
  while ((p < 48) && (fgets(line, sizeof line, handle)))
  {
    int r, g, b;
    if (sscanf(line, "%d %d %d", &r, &g, &b) == 3)
    {
      rgb6[p++] = (unsigned char)(r / 4);
      rgb6[p++] = (unsigned char)(g / 4);
      rgb6[p++] = (unsigned char)(b / 4);
    }
  }
  fclose(handle);
  if (p == 48) setpalette(rgb6);
}

static bool loadfontandpalette(void)
{
  unsigned char rgb6[768];
  int handle;

  memset(chardata, 0, sizeof chardata);
  memset(rgb6, 0, sizeof rgb6);

  handle = io_open((char *)"chargen.bin");
  if (handle == -1) return false;
  io_read(handle, chardata, sizeof chardata);
  io_close(handle);

  handle = io_open((char *)"palette.bin");
  if (handle == -1) return false;
  io_read(handle, rgb6, sizeof rgb6);
  io_close(handle);

  setpalette(rgb6);
  loadexternalpalette();
  return true;
}

ImU32 gui::retrocolor(int index)
{
  Uint32 c;

  if (!loaded)
  {
    if (!loadfontandpalette()) return IM_COL32(128, 128, 128, 255);
    loaded = true;
  }
  c = palette[index & 15];
  return IM_COL32((c >> 16) & 0xff, (c >> 8) & 0xff, c & 0xff, 255);
}

float gui::retrotextwidth(const char *text, float pixel)
{
  return (float)strlen(text) * FONT_XSIZE * pixel;
}

float gui::drawretrotext(ImDrawList *dl, ImVec2 pos, float pixel, float pixelh, ImU32 col, const char *text, bool bold)
{
  float x = pos.x;

  if (!loaded)
  {
    if (!loadfontandpalette()) return 0.0f;
    loaded = true;
  }
  for (; *text; text++)
  {
    const unsigned char *glyph = chardata + ((unsigned char)*text) * 16 + 1;
    int py, px;

    for (py = 0; py < FONT_YSIZE; py++)
    {
      // Pixel n of the row is bit 8-n of row: bit 0 is the extra column of
      // bold text, each pixel then also set one to the right
      unsigned row = (unsigned)glyph[py] << 1;
      int width = FONT_XSIZE + (bold ? 1 : 0);
      if (bold) row |= row >> 1;
      // Runs of set pixels as one rectangle each
      px = 0;
      while (px < width)
      {
        int start;
        while ((px < width) && (!((row >> (8 - px)) & 1))) px++;
        if (px >= width) break;
        start = px;
        while ((px < width) && ((row >> (8 - px)) & 1)) px++;
        dl->AddRectFilled(ImVec2(x + start * pixel, pos.y + py * pixelh), ImVec2(x + px * pixel, pos.y + (py + 1) * pixelh), col);
      }
    }
    x += FONT_XSIZE * pixel;
  }
  return x - pos.x;
}

void gui::retrofitcell(int maxx, int maxy, int *w, int *h)
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

void gui::freeretroscreen(void)
{
  if (texture) SDL_DestroyTexture(texture);
  texture = NULL;
  texturerenderer = NULL;
  free(pixels);
  pixels = NULL;
  free(prevscreen);
  prevscreen = NULL;
}

static bool createtexture(SDL_Renderer *renderer)
{
  if ((texture) && (texturerenderer == renderer)) return true;
  freeretroscreen();

  if (!loaded)
  {
    if (!loadfontandpalette()) return false;
    loaded = true;
  }
  pixels = (Uint32 *)malloc(BASE_XSIZE * BASE_YSIZE * sizeof(Uint32));
  prevscreen = (unsigned *)malloc(MAX_COLUMNS * MAX_ROWS * sizeof(unsigned));
  if ((!pixels) || (!prevscreen))
  {
    freeretroscreen();
    return false;
  }
  texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, BASE_XSIZE, BASE_YSIZE);
  if (!texture)
  {
    freeretroscreen();
    return false;
  }
  // Sharp pixels at any size (the interface uses smooth scaling otherwise).
  // With the gradient background the black background pixels are clear
  SDL_SetTextureScaleMode(texture, SDL_ScaleModeNearest);
  SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
  texturerenderer = renderer;
  makemetercolors();
  meterdrawn = false;
  // Everything is drawn the first time
  memset(prevscreen, 0xff, MAX_COLUMNS * MAX_ROWS * sizeof(unsigned));
  return true;
}

// The meter gradient from the bottom color through the middle one to the top
// one, opacity too. A pixel that isn't fully opaque lets the background
// (black, or the gradient background) show through when the texture is drawn
static void makemetercolors(void)
{
  int i, k;

  memcpy(metermade, retrometercolors, sizeof metermade);
  for (i = 0; i < METERHEIGHT; i++)
  {
    float t = (float)i / (float)(METERHEIGHT - 1) * 2.0f;
    int s = (t < 1.0f) ? 0 : 1;
    float f = t - (float)s;
    Uint32 c = 0;

    // Alpha, red, green, blue
    for (k = 24; k >= 0; k -= 8)
    {
      int a = (int)((retrometercolors[s] >> k) & 0xff);
      int b = (int)((retrometercolors[s + 1] >> k) & 0xff);
      c |= (Uint32)((int)(a + (b - a) * f + 0.5f) & 0xff) << k;
    }
    metercolor[i] = c;
  }
}

// The meter heights for this frame: they jump up with the envelope and fall
// back smoothly. Only on the retro main screen
static void updatemeterlevels(void)
{
  Uint32 now = SDL_GetTicks();
  float fall = (metertime) ? (float)(now - metertime) * METERFALL / 1000.0f : 0.0f;
  int c;

  metertime = now;
  if (fall > METERHEIGHT) fall = METERHEIGHT;
  for (c = 0; c < MAX_CHN; c++)
  {
    float target = 0.0f;

    // Sounding: the envelope, unless the voice has no waveform, the volume
    // is 0 or the channel is muted
    if ((retromainshown) && (!chn[c].mute) && (sidreg[0x04 + c * 7] & 0xf0) && (sidreg[0x18] & 0x0f))
      target = (float)sid_envelope(c) * METERHEIGHT / 255.0f;
    if (!retromainshown) meterlevel[c] = 0.0f;
    meterlevel[c] -= fall;
    if (meterlevel[c] < target) meterlevel[c] = target;
    if (meterlevel[c] < 0.0f) meterlevel[c] = 0.0f;
  }
}

static bool metershown(void)
{
  int c;

  for (c = 0; c < MAX_CHN; c++)
  {
    if (meterlevel[c] >= 1.0f) return true;
  }
  return false;
}

// The characters under the meters are drawn again (without them) when the
// meters are or were shown
static void invalidatemeters(void)
{
  int c, x, y;

  for (c = 0; c < MAX_CHN; c++)
  {
    for (y = METERBOTTOM - METERROWS; y < METERBOTTOM; y++)
    {
      for (x = 0; x < METERWIDTH; x++)
        prevscreen[y * MAX_COLUMNS + c * 15 + METERCOLUMN + x] = 0xffffffff;
    }
  }
}

// Paints the meters into the background pixels of the characters (those
// without a background color of their own: marks and the cursor stay)
static void drawmeters(void)
{
  int c, i, x;

  for (c = 0; c < MAX_CHN; c++)
  {
    int h = (int)meterlevel[c];

    for (i = 0; i < h; i++)
    {
      int py = METERBOTTOM * FONT_YSIZE - 1 - i;
      int row = py / FONT_YSIZE;
      int line = py % FONT_YSIZE;
      Uint32 *dptr = pixels + py * BASE_XSIZE + (c * 15 + METERCOLUMN) * FONT_XSIZE;

      for (x = 0; x < METERWIDTH; x++)
      {
        unsigned cell = scrbuffer[row * MAX_COLUMNS + c * 15 + METERCOLUMN + x];
        unsigned char bits = chardata[(cell & 0xff) * 16 + 1 + line];
        int px;

        if (!((cell >> 20) & 0xf))
        {
          for (px = 0; px < FONT_XSIZE; px++)
          {
            if (!((bits >> (7 - px)) & 1)) dptr[px] = metercolor[i];
          }
        }
        dptr += FONT_XSIZE;
      }
    }
  }
}

// Characters out of their cell (CELL_HALFLEFT, CELL_DOWN)
static bool shifted(unsigned cell)
{
  return (cell != 0xffffffff) && (cell & (CELL_HALFLEFT | CELL_DOWNMASK));
}

static bool lowered(unsigned cell)
{
  return (cell != 0xffffffff) && (cell & CELL_DOWNMASK);
}

// Draws a character into the pixel buffer: its background and, if glyph is
// set, the character
static void drawcell(int x, int y, unsigned cell, bool glyph)
{
  const unsigned char *g = chardata + (cell & 0xff) * 16 + 1;
  Uint32 fg = palette[(cell >> 16) & 0xf];
  Uint32 bg = palette[(cell >> 20) & 0xf];
  Uint32 *dptr = pixels + (y * FONT_YSIZE) * BASE_XSIZE + x * FONT_XSIZE;
  int px, py;

  // Black background: clear over the gradient
  if ((retrogradient) && (!((cell >> 20) & 0xf))) bg = 0;
  for (py = 0; py < FONT_YSIZE; py++)
  {
    unsigned char bits = glyph ? g[py] : 0;
    for (px = 0; px < FONT_XSIZE; px++)
      dptr[px] = ((bits >> (7 - px)) & 1) ? fg : bg;
    dptr += BASE_XSIZE;
  }
}

// Draws a character out of its cell: half a character to the left
// (CELL_HALFLEFT) and / or thirds of a row lower (CELL_DOWN, into the row
// below), over the backgrounds drawn already
static void drawshifted(int x, int y, unsigned cell)
{
  const unsigned char *g = chardata + (cell & 0xff) * 16 + 1;
  Uint32 fg = palette[(cell >> 16) & 0xf];
  int x0 = x * FONT_XSIZE - ((cell & CELL_HALFLEFT) ? FONT_XSIZE / 2 : 0);
  int y0 = y * FONT_YSIZE + ((int)CELL_DOWNTHIRDS(cell) * FONT_YSIZE + 1) / 3;
  int px, py;

  for (py = 0; (py < FONT_YSIZE) && (y0 + py < BASE_YSIZE); py++)
  {
    Uint32 *dptr = pixels + (y0 + py) * BASE_XSIZE;
    for (px = 0; px < FONT_XSIZE; px++)
    {
      if ((x0 + px >= 0) && ((g[py] >> (7 - px)) & 1)) dptr[x0 + px] = fg;
    }
  }
}

// Draws the changed characters into the pixel buffer and updates the
// texture with the rows that changed. Characters out of their cell reach
// into their neighbors: a row with such characters (now or before) is drawn
// whole when something in it changed, and when lowered text changed the row
// below it is drawn again too. The characters out of their cell are drawn
// last, over the rows they reach into
static void updatetexture(void)
{
  bool changed[MAX_ROWS], whole[MAX_ROWS], down[MAX_ROWS], redraw[MAX_ROWS];
  int firstrow = MAX_ROWS;
  int lastrow = -1;
  int x, y;
  bool meters;

  // Switching the background draws everything again
  if (retrogradient != drawngradient)
  {
    memset(prevscreen, 0xff, MAX_COLUMNS * MAX_ROWS * sizeof(unsigned));
    drawngradient = retrogradient;
  }

  // New colors (View > Meter Colors) are drawn at once
  if (memcmp(metermade, retrometercolors, sizeof metermade)) makemetercolors();

  updatemeterlevels();
  meters = metershown();
  if ((meters) || (meterdrawn)) invalidatemeters();

  for (y = 0; y < MAX_ROWS; y++)
  {
    unsigned *row = scrbuffer + y * MAX_COLUMNS;
    unsigned *prev = prevscreen + y * MAX_COLUMNS;

    changed[y] = whole[y] = down[y] = false;
    for (x = 0; x < MAX_COLUMNS; x++)
    {
      if (row[x] != prev[x]) changed[y] = true;
      if ((shifted(row[x])) || (shifted(prev[x]))) whole[y] = true;
      if ((lowered(row[x])) || (lowered(prev[x]))) down[y] = true;
    }
  }
  for (y = 0; y < MAX_ROWS; y++)
  {
    // Below lowered text that changed: drawn whole again
    bool below = (y > 0) && (down[y - 1]) && (changed[y - 1]);
    redraw[y] = (changed[y]) || (below);
    if (below) whole[y] = true;
  }

  for (y = 0; y < MAX_ROWS; y++)
  {
    unsigned *row = scrbuffer + y * MAX_COLUMNS;
    unsigned *prev = prevscreen + y * MAX_COLUMNS;

    if (!redraw[y]) continue;
    if (y < firstrow) firstrow = y;
    if (y > lastrow) lastrow = y;
    for (x = 0; x < MAX_COLUMNS; x++)
    {
      if ((!whole[y]) && (row[x] == prev[x])) continue;
      prev[x] = row[x];
      drawcell(x, y, row[x], !shifted(row[x]));
    }
  }
  // The characters out of their cell, of the rows drawn and of those whose
  // lowered text reaches into a row drawn
  for (y = 0; y < MAX_ROWS; y++)
  {
    unsigned *row = scrbuffer + y * MAX_COLUMNS;

    if ((!redraw[y]) && ((!down[y]) || (y + 1 >= MAX_ROWS) || (!redraw[y + 1]))) continue;
    for (x = 0; x < MAX_COLUMNS; x++)
    {
      if (shifted(row[x])) drawshifted(x, y, row[x]);
    }
    if ((down[y]) && (y + 1 < MAX_ROWS) && (y + 1 > lastrow)) lastrow = y + 1;
    if (y < firstrow) firstrow = y;
  }

  if (meters) drawmeters();
  meterdrawn = meters;

  if (lastrow >= firstrow)
  {
    SDL_Rect r;
    r.x = 0;
    r.y = firstrow * FONT_YSIZE;
    r.w = BASE_XSIZE;
    r.h = (lastrow - firstrow + 1) * FONT_YSIZE;
    SDL_UpdateTexture(texture, &r, pixels + r.y * BASE_XSIZE, BASE_XSIZE * sizeof(Uint32));
  }
}

// The gradient background (View > Gradient Background) behind the whole
// window, margins too: black down to GRADIENTSTART of the height, then
// rising (fast at first, as in Scott's picture) to GRADIENTEND at the bottom
#define GRADIENTSTART 0.68f
#define GRADIENTBANDS 32
static const int gradientend[3] = { 0x0b, 0x0e, 0x1b };

static ImU32 gradientcolor(float t)
{
  float f = 1.0f - powf(1.0f - t, 1.4f);

  return IM_COL32((int)(gradientend[0] * f + 0.5f), (int)(gradientend[1] * f + 0.5f), (int)(gradientend[2] * f + 0.5f), 255);
}

static void drawgradient(void)
{
  ImGuiIO &io = ImGui::GetIO();
  ImDrawList *dl = ImGui::GetBackgroundDrawList();
  float w = io.DisplaySize.x;
  float h = io.DisplaySize.y;
  float y0 = h * GRADIENTSTART;
  int i;

  dl->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(w, y0), IM_COL32(0, 0, 0, 255));
  for (i = 0; i < GRADIENTBANDS; i++)
  {
    float t0 = (float)i / GRADIENTBANDS;
    float t1 = (float)(i + 1) / GRADIENTBANDS;
    ImU32 c0 = gradientcolor(t0);
    ImU32 c1 = gradientcolor(t1);

    dl->AddRectFilledMultiColor(ImVec2(0.0f, y0 + (h - y0) * t0), ImVec2(w, y0 + (h - y0) * t1), c0, c0, c1, c1);
  }
}

// Where the text screen goes in the window (ImGui points): whole-pixel
// character cells in the window's real pixels, centered with black margins
static void screenrect(SDL_Renderer *renderer, ImVec2 *p0, ImVec2 *p1)
{
  ImGuiIO &io = ImGui::GetIO();
  float sx = io.DisplayFramebufferScale.x > 0.0f ? io.DisplayFramebufferScale.x : 1.0f;
  float sy = io.DisplayFramebufferScale.y > 0.0f ? io.DisplayFramebufferScale.y : 1.0f;
  int outw, outh, cellw, cellh, gridx, gridy;

  if (SDL_GetRendererOutputSize(renderer, &outw, &outh) != 0)
  {
    outw = (int)(io.DisplaySize.x * sx);
    outh = (int)(io.DisplaySize.y * sy);
  }
  retrofitcell(outw, outh, &cellw, &cellh);
  gridx = (outw - cellw * MAX_COLUMNS) / 2;
  gridy = (outh - cellh * MAX_ROWS) / 2;
  if (gridx < 0) gridx = 0;
  if (gridy < 0) gridy = 0;

  // ImGui draws in points
  *p0 = ImVec2(gridx / sx, gridy / sy);
  *p1 = ImVec2((gridx + cellw * MAX_COLUMNS) / sx, (gridy + cellh * MAX_ROWS) / sy);
}

void gui::retroblockmouse(void)
{
  mouseblocked = true;
}

int gui::drawretrotitlebar(SDL_Renderer *renderer, ImVec2 *screen0, ImVec2 *screen1)
{
  ImGuiIO &io = ImGui::GetIO();
  ImVec2 p0, p1;
  float cw, ch;

  screenrect(renderer, &p0, &p1);
  *screen0 = p0;
  *screen1 = p1;
  cw = (p1.x - p0.x) / MAX_COLUMNS;
  ch = (p1.y - p0.y) / MAX_ROWS;
  if ((!scrbuffer) || (!createtexture(renderer))) return -1;
  updatetexture();
  ImGui::GetBackgroundDrawList()->AddImage(ImTextureRef((ImTextureID)(intptr_t)texture), p0, ImVec2(p1.x, p0.y + ch),
    ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f / MAX_ROWS));

  if ((!ImGui::IsMousePosValid()) || (io.MousePos.x < p0.x) || (io.MousePos.x >= p1.x) || (io.MousePos.y < p0.y) || (io.MousePos.y >= p0.y + ch))
    return -1;
  return (int)((io.MousePos.x - p0.x) / cw);
}

void gui::drawretroscreen(SDL_Renderer *renderer)
{
  ImGuiIO &io = ImGui::GetIO();
  ImVec2 p0, p1;

  if ((!scrbuffer) || (!createtexture(renderer))) return;
  updatetexture();

  screenrect(renderer, &p0, &p1);
  if (retrogradient) drawgradient();
  ImGui::GetBackgroundDrawList()->AddImage(ImTextureRef((ImTextureID)(intptr_t)texture), p0, p1);

  // Mouse position as character cells. Positions in the margins go to the
  // nearest edge character
  {
    float cw = (p1.x - p0.x) / MAX_COLUMNS;
    float ch = (p1.y - p0.y) / MAX_ROWS;
    int mx = (io.MousePos.x < p0.x) ? 0 : (int)((io.MousePos.x - p0.x) / cw);
    int my = (io.MousePos.y < p0.y) ? 0 : (int)((io.MousePos.y - p0.y) / ch);
    if (mx >= MAX_COLUMNS) mx = MAX_COLUMNS - 1;
    if (my >= MAX_ROWS) my = MAX_ROWS - 1;
    textscreenmousex = mx;
    textscreenmousey = my;
  }

  // Clicks and the wheel go to the text screen, except while a dialog is
  // open on top of it. The click that closes a dialog doesn't reach the text
  // screen either: buttons count again once they all have been released
  tsmousebuttons = 0;
  if (ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup))
  {
    tsmousewheel = 0;
    mouseblocked = true;
    return;
  }
  if (mouseblocked)
  {
    if ((io.MouseDown[0]) || (io.MouseDown[1]) || (io.MouseDown[2])) return;
    mouseblocked = false;
  }
  if (io.MouseDown[0]) tsmousebuttons |= MOUSEB_LEFT;
  if (io.MouseDown[1]) tsmousebuttons |= MOUSEB_RIGHT;
  if (io.MouseDown[2]) tsmousebuttons |= MOUSEB_MIDDLE;
}
