// The SID designer's C64 style text screen (see gui_c64.h). The
// characters are drawn into a texture like the retro interface's screen:
// only the characters that changed are drawn again. The texture is made an
// integer number of times larger than the 640x400 screen and scaled from
// there, so that the C64's square pixels stay even at any window size.

#include "gui_c64.h"
#include "c64font.h"

#include <stdarg.h>

namespace c64
{
  // The C64 colors (Pepto's PAL palette)
  static const Uint32 c64palette[16] =
  {
    0xff000000, 0xffffffff, 0xff68372b, 0xff70a4b2, 0xff6f3d86, 0xff588d43, 0xff352879, 0xffb8c76f,
    0xff6f4f25, 0xff433900, 0xff9a6759, 0xff444444, 0xff6c6c6c, 0xff9ad284, 0xff6c5eb5, 0xff959595
  };
  // The colors in use (setcolor() may change some)
  static Uint32 palette[16] =
  {
    0xff000000, 0xffffffff, 0xff68372b, 0xff70a4b2, 0xff6f3d86, 0xff588d43, 0xff352879, 0xffb8c76f,
    0xff6f4f25, 0xff433900, 0xff9a6759, 0xff444444, 0xff6c6c6c, 0xff9ad284, 0xff6c5eb5, 0xff959595
  };

  static unsigned char font[256][8];
  static bool fontready = false;
  static bool romloaded = false;

  // One character: code | fg << 8 | bg << 12
  static unsigned short cells[ROWS][COLS];
  static unsigned short drawn[ROWS][COLS];
  static int screenbg = BLUE;
  static int screenfg = LIGHTBLUE;

  static SDL_Texture *texture = NULL;
  static SDL_Renderer *texturerenderer = NULL;
  static Uint32 *pixels = NULL;
  static int texscale = 0;            // Texture pixels per C64 pixel

  // Where the screen is, in ImGui points
  static ImVec2 screenpos(0.0f, 0.0f);
  static float cellsize = 8.0f;
  static bool sharp = true;           // Integer scaling: no filtering
  static float mx = -1.0f;
  static float my = -1.0f;
  static bool inside = false;
}

using namespace c64;

static void makefont(void)
{
  int c, y, x;

  if (fontready) return;
  for (c = 0; c < 128; c++)
  {
    for (y = 0; y < 8; y++)
    {
      const char *row = c64fontart[c * 8 + y];
      unsigned char bits = 0;
      for (x = 0; x < 8; x++)
      {
        if (row[x] == '#') bits |= (unsigned char)(0x80 >> x);
      }
      font[c][y] = bits;
    }
  }
  for (c = 0; c < 128; c++)
  {
    for (y = 0; y < 8; y++) font[c + 128][y] = (unsigned char)~font[c][y];
  }
  fontready = true;
}

void c64::loadrom(const char *dir)
{
  static const char *names[] = { "chargen", "chargen.rom", "chargen.bin", "c64chargen.bin", "characters.901225-01.bin" };
  unsigned char data[4096];
  unsigned c;

  makefont();
  if ((romloaded) || (!dir)) return;
  for (c = 0; c < sizeof names / sizeof names[0]; c++)
  {
    char path[MAX_PATHNAME];
    FILE *handle;
    long size;

    snprintf(path, sizeof path, "%s%s", dir, names[c]);
    handle = fopen(path, "rb");
    if (!handle) continue;
    fseek(handle, 0, SEEK_END);
    size = ftell(handle);
    fseek(handle, 0, SEEK_SET);
    if (((size == 4096) || (size == 2048)) && (fread(data, 1, 1024, handle) == 1024))
    {
      int ch, y;
      for (ch = 0; ch < 128; ch++)
      {
        for (y = 0; y < 8; y++)
        {
          font[ch][y] = data[ch * 8 + y];
          font[ch + 128][y] = (unsigned char)~data[ch * 8 + y];
        }
      }
      romloaded = true;
      // Everything is drawn again
      memset(drawn, 0xff, sizeof drawn);
    }
    fclose(handle);
    if (romloaded) return;
  }
}

bool c64::usingrom(void)
{
  return romloaded;
}

int c64::screencode(int ch)
{
  if ((ch >= 'a') && (ch <= 'z')) return ch - 'a' + 1;
  if ((ch >= '@') && (ch <= 'Z')) return ch - '@';
  if ((ch >= ' ') && (ch <= '?')) return ch;
  switch (ch)
  {
    case '[': case '{': return 27;
    case ']': case '}': return 29;
    case '^': return SC_ARROWUP;
    case '\\': return SC_POUND;
    case '_': return SC_EIGHTH_LOWER;
    case '|': return SC_VLINE;
    case '`': return '\'';
    case '~': return '-';
  }
  return SC_SPACE;
}

void c64::clear(int bg, int fg)
{
  int x, y;
  unsigned short cell;

  makefont();
  screenbg = bg & 15;
  screenfg = fg & 15;
  cell = (unsigned short)(SC_SPACE | (screenfg << 8) | (screenbg << 12));
  for (y = 0; y < ROWS; y++)
  {
    for (x = 0; x < COLS; x++) cells[y][x] = cell;
  }
}

void c64::setcolor(int index, int rgb)
{
  Uint32 c;

  index &= 15;
  c = (rgb < 0) ? c64palette[index] : (0xff000000u | (Uint32)(rgb & 0xffffff));
  if (palette[index] == c) return;
  palette[index] = c;
  // Everything is drawn again with the new color
  memset(drawn, 0xff, sizeof drawn);
}

ImU32 c64::colorof(int index)
{
  Uint32 c = palette[index & 15];
  return IM_COL32((c >> 16) & 0xff, (c >> 8) & 0xff, c & 0xff, 255);
}

ImVec2 c64::screenmin(void)
{
  return screenpos;
}

ImVec2 c64::screenmax(void)
{
  ImGuiIO &io = ImGui::GetIO();
  float sx = io.DisplayFramebufferScale.x > 0.0f ? io.DisplayFramebufferScale.x : 1.0f;
  float sy = io.DisplayFramebufferScale.y > 0.0f ? io.DisplayFramebufferScale.y : 1.0f;
  return ImVec2(screenpos.x + cellsize * COLS, screenpos.y + cellsize * sx / sy * ROWS);
}

int c64::background(void)
{
  return screenbg;
}

void c64::put(int x, int y, int code, int fg, int bg)
{
  if ((x < 0) || (y < 0) || (x >= COLS) || (y >= ROWS)) return;
  if (bg < 0) bg = screenbg;
  if (fg < 0) fg = screenfg;
  cells[y][x] = (unsigned short)((code & 0xff) | ((fg & 15) << 8) | ((bg & 15) << 12));
}

int c64::print(int x, int y, const char *text, int fg, bool reverse, int bg)
{
  while ((text) && (*text))
  {
    int code = screencode((unsigned char)*text++);
    put(x++, y, reverse ? (code | SC_REVERSE) : code, fg, bg);
  }
  return x;
}

int c64::printfmt(int x, int y, int fg, const char *fmt, ...)
{
  char text[256];
  va_list ap;

  va_start(ap, fmt);
  vsnprintf(text, sizeof text, fmt, ap);
  va_end(ap);
  return print(x, y, text, fg);
}

void c64::fill(int x, int y, int w, int h, int code, int fg, int bg)
{
  int cx, cy;
  for (cy = y; cy < y + h; cy++)
  {
    for (cx = x; cx < x + w; cx++) put(cx, cy, code, fg, bg);
  }
}

void c64::color(int x, int y, int w, int h, int fg, int bg)
{
  int cx, cy;
  for (cy = y; cy < y + h; cy++)
  {
    for (cx = x; cx < x + w; cx++)
    {
      if ((cx < 0) || (cy < 0) || (cx >= COLS) || (cy >= ROWS)) continue;
      put(cx, cy, cells[cy][cx] & 0xff, fg, bg);
    }
  }
}

void c64::invert(int x, int y, int w, int h)
{
  int cx, cy;
  for (cy = y; cy < y + h; cy++)
  {
    for (cx = x; cx < x + w; cx++)
    {
      if ((cx < 0) || (cy < 0) || (cx >= COLS) || (cy >= ROWS)) continue;
      cells[cy][cx] ^= SC_REVERSE;
    }
  }
}

int c64::codeat(int x, int y)
{
  if ((x < 0) || (y < 0) || (x >= COLS) || (y >= ROWS)) return SC_SPACE;
  return cells[y][x] & 0xff;
}

void c64::hline(int x, int y, int w, int fg)
{
  fill(x, y, w, 1, SC_HLINE, fg);
}

void c64::box(int x, int y, int w, int h, int fg, const char *title, int titlefg)
{
  int c;

  if ((w < 2) || (h < 2)) return;
  put(x, y, SC_ROUND_TL, fg);
  put(x + w - 1, y, SC_ROUND_TR, fg);
  put(x, y + h - 1, SC_ROUND_BL, fg);
  put(x + w - 1, y + h - 1, SC_ROUND_BR, fg);
  for (c = 1; c < w - 1; c++)
  {
    put(x + c, y, SC_HLINE, fg);
    put(x + c, y + h - 1, SC_HLINE, fg);
  }
  for (c = 1; c < h - 1; c++)
  {
    put(x, y + c, SC_VLINE, fg);
    put(x + w - 1, y + c, SC_VLINE, fg);
  }
  if ((title) && (title[0]))
  {
    put(x + 1, y, SC_SPACE, fg);
    c = print(x + 2, y, title, titlefg >= 0 ? titlefg : fg);
    put(c, y, SC_SPACE, fg);
  }
}

// Eighth blocks filling a character from the bottom (1-7 eighths)
static const int vfill[9] =
{
  SC_SPACE, SC_EIGHTH_LOWER, SC_QUARTER_LOWER, SC_3EIGHTHS_LOWER, SC_HALF_LOWER,
  SC_3EIGHTHS_UPPER | SC_REVERSE, SC_QUARTER_UPPER | SC_REVERSE, SC_EIGHTH_UPPER | SC_REVERSE, SC_BLOCK
};

// ... and from the left
static const int hfill[9] =
{
  SC_SPACE, SC_EIGHTH_LEFT, SC_QUARTER_LEFT, SC_3EIGHTHS_LEFT, SC_HALF_LEFT,
  SC_3EIGHTHS_RIGHT | SC_REVERSE, SC_QUARTER_RIGHT | SC_REVERSE, SC_EIGHTH_RIGHT | SC_REVERSE, SC_BLOCK
};

void c64::vbar(int x, int y, int w, int h, float frac, int fg, int track)
{
  int eighths, row, cx;

  if (frac < 0.0f) frac = 0.0f;
  if (frac > 1.0f) frac = 1.0f;
  eighths = (int)(frac * h * 8 + 0.5f);
  for (row = 0; row < h; row++)
  {
    int e = eighths - row * 8;
    int code;
    if (e > 8) e = 8;
    if (e < 0) e = 0;
    code = vfill[e];
    for (cx = x; cx < x + w; cx++) put(cx, y + h - 1 - row, code, fg, track);
  }
}

void c64::hbar(int x, int y, int w, float frac, int fg, int track)
{
  int eighths, col;

  if (frac < 0.0f) frac = 0.0f;
  if (frac > 1.0f) frac = 1.0f;
  eighths = (int)(frac * w * 8 + 0.5f);
  for (col = 0; col < w; col++)
  {
    int e = eighths - col * 8;
    if (e > 8) e = 8;
    if (e < 0) e = 0;
    put(x + col, y, hfill[e], fg, track);
  }
}

/*
 * Drawing
 */

void c64::freescreen(void)
{
  if (texture) SDL_DestroyTexture(texture);
  texture = NULL;
  texturerenderer = NULL;
  free(pixels);
  pixels = NULL;
  texscale = 0;
}

static bool maketexture(SDL_Renderer *renderer, int scale)
{
  int w = COLS * CHARSIZE * scale;
  int h = ROWS * CHARSIZE * scale;

  if ((texture) && (texturerenderer == renderer) && (texscale == scale)) return true;
  freescreen();
  pixels = (Uint32 *)malloc((size_t)w * h * sizeof(Uint32));
  if (!pixels) return false;
  texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
  if (!texture)
  {
    freescreen();
    return false;
  }
  texturerenderer = renderer;
  texscale = scale;
  memset(drawn, 0xff, sizeof drawn);
  return true;
}

static void updatetexture(void)
{
  int pitch = COLS * CHARSIZE * texscale;
  int firstrow = ROWS;
  int lastrow = -1;
  int x, y;

  for (y = 0; y < ROWS; y++)
  {
    for (x = 0; x < COLS; x++)
    {
      unsigned short cell = cells[y][x];
      const unsigned char *glyph;
      Uint32 fg, bg;
      int py, px, sy, sx;

      if (cell == drawn[y][x]) continue;
      drawn[y][x] = cell;
      if (y < firstrow) firstrow = y;
      if (y > lastrow) lastrow = y;

      glyph = font[cell & 0xff];
      fg = palette[(cell >> 8) & 15];
      bg = palette[(cell >> 12) & 15];
      for (py = 0; py < CHARSIZE; py++)
      {
        unsigned char bits = glyph[py];
        for (sy = 0; sy < texscale; sy++)
        {
          Uint32 *d = pixels + ((y * CHARSIZE + py) * texscale + sy) * pitch + x * CHARSIZE * texscale;
          for (px = 0; px < CHARSIZE; px++)
          {
            Uint32 c = ((bits >> (7 - px)) & 1) ? fg : bg;
            for (sx = 0; sx < texscale; sx++) *d++ = c;
          }
        }
      }
    }
  }

  if (lastrow >= firstrow)
  {
    SDL_Rect r;
    r.x = 0;
    r.y = firstrow * CHARSIZE * texscale;
    r.w = pitch;
    r.h = (lastrow - firstrow + 1) * CHARSIZE * texscale;
    SDL_UpdateTexture(texture, &r, pixels + (size_t)r.y * pitch, pitch * (int)sizeof(Uint32));
  }
}

void c64::layout(SDL_Renderer *renderer, ImVec2 a, ImVec2 b)
{
  ImGuiIO &io = ImGui::GetIO();
  float sx = io.DisplayFramebufferScale.x > 0.0f ? io.DisplayFramebufferScale.x : 1.0f;
  float sy = io.DisplayFramebufferScale.y > 0.0f ? io.DisplayFramebufferScale.y : 1.0f;
  float areaw = (b.x - a.x) * sx;
  float areah = (b.y - a.y) * sy;
  float scale, pixel;
  int sw = COLS * CHARSIZE;
  int sh = ROWS * CHARSIZE;

  (void)renderer;
  if ((areaw < 1.0f) || (areah < 1.0f)) return;

  // In the area's real pixels
  scale = areaw / sw;
  if (areah / sh < scale) scale = areah / sh;

  // Whole pixels when that wastes hardly any room, otherwise a smooth scale
  // (from a texture made larger first, so the characters stay crisp)
  pixel = floorf(scale);
  if ((pixel >= 1.0f) && (pixel >= scale * 0.97f))
  {
    scale = pixel;
    sharp = true;
  }
  else sharp = (scale == floorf(scale));

  cellsize = scale * CHARSIZE / sx;
  screenpos = ImVec2(floorf((a.x * sx + (areaw - sw * scale) * 0.5f)) / sx, floorf((a.y * sy + (areah - sh * scale) * 0.5f)) / sy);

  if (cellsize > 0.0f)
  {
    mx = (io.MousePos.x - screenpos.x) / cellsize;
    my = (io.MousePos.y - screenpos.y) / (scale * CHARSIZE / sy);
  }
  inside = (mx >= 0.0f) && (my >= 0.0f) && (mx < COLS) && (my < ROWS) && (ImGui::IsMousePosValid());
}

void c64::present(SDL_Renderer *renderer)
{
  ImGuiIO &io = ImGui::GetIO();
  ImDrawList *dl = ImGui::GetBackgroundDrawList();
  float sx = io.DisplayFramebufferScale.x > 0.0f ? io.DisplayFramebufferScale.x : 1.0f;
  float sy = io.DisplayFramebufferScale.y > 0.0f ? io.DisplayFramebufferScale.y : 1.0f;
  float cellh = cellsize * sx / sy;
  int scale;

  // The texture: as many times larger as the screen is drawn (up to 4x)
  scale = (int)ceilf(cellsize * sx / CHARSIZE - 0.001f);
  if (scale < 1) scale = 1;
  if (scale > 4) scale = 4;
  if (!maketexture(renderer, scale)) return;
  SDL_SetTextureScaleMode(texture, sharp ? SDL_ScaleModeNearest : SDL_ScaleModeLinear);
  updatetexture();
  dl->AddImage(ImTextureRef((ImTextureID)(intptr_t)texture), screenpos,
    ImVec2(screenpos.x + cellsize * COLS, screenpos.y + cellh * ROWS));
}

float c64::mousex(void)
{
  return mx;
}

float c64::mousey(void)
{
  return my;
}

bool c64::mouseinside(void)
{
  return inside;
}

float c64::charsize(void)
{
  return cellsize;
}
