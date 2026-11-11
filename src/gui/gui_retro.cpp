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
  // Sharp pixels at any size (the interface uses smooth scaling otherwise)
  SDL_SetTextureScaleMode(texture, SDL_ScaleModeNearest);
  texturerenderer = renderer;
  // Everything is drawn the first time
  memset(prevscreen, 0xff, MAX_COLUMNS * MAX_ROWS * sizeof(unsigned));
  return true;
}

// Draws the changed characters into the pixel buffer and updates the
// texture with the rows that changed
static void updatetexture(void)
{
  int firstrow = MAX_ROWS;
  int lastrow = -1;
  int x, y;

  for (y = 0; y < MAX_ROWS; y++)
  {
    for (x = 0; x < MAX_COLUMNS; x++)
    {
      unsigned cell = scrbuffer[y * MAX_COLUMNS + x];
      const unsigned char *glyph;
      Uint32 fg, bg;
      Uint32 *dptr;
      int px, py;

      if (cell == prevscreen[y * MAX_COLUMNS + x]) continue;
      prevscreen[y * MAX_COLUMNS + x] = cell;
      if (y < firstrow) firstrow = y;
      if (y > lastrow) lastrow = y;

      glyph = chardata + (cell & 0xff) * 16 + 1;
      fg = palette[(cell >> 16) & 0xf];
      bg = palette[(cell >> 20) & 0xf];
      dptr = pixels + (y * FONT_YSIZE) * BASE_XSIZE + x * FONT_XSIZE;
      for (py = 0; py < FONT_YSIZE; py++)
      {
        unsigned char bits = glyph[py];
        for (px = 0; px < FONT_XSIZE; px++)
          dptr[px] = ((bits >> (7 - px)) & 1) ? fg : bg;
        dptr += BASE_XSIZE;
      }
    }
  }

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

void gui::drawretroscreen(SDL_Renderer *renderer)
{
  ImGuiIO &io = ImGui::GetIO();
  float sx = io.DisplayFramebufferScale.x > 0.0f ? io.DisplayFramebufferScale.x : 1.0f;
  float sy = io.DisplayFramebufferScale.y > 0.0f ? io.DisplayFramebufferScale.y : 1.0f;
  int outw, outh, cellw, cellh, gridx, gridy;
  ImVec2 p0, p1;

  if ((!scrbuffer) || (!createtexture(renderer))) return;
  updatetexture();

  // Whole-pixel character cells in the window's real pixels, centered with
  // black margins
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
  p0 = ImVec2(gridx / sx, gridy / sy);
  p1 = ImVec2((gridx + cellw * MAX_COLUMNS) / sx, (gridy + cellh * MAX_ROWS) / sy);
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
