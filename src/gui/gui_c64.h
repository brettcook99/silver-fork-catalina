// A Commodore 64 style text screen for the instrument designer: 80x50
// characters of 8x8 pixels in the C64's 16 colors, with a border around it.
// Characters are C64 screen codes (see c64font.h); 128-255 are reverse video.
// Unlike a real C64 each character may also have its own background color.

#ifndef GUI_C64_H
#define GUI_C64_H

#include "gui_internal.h"

namespace c64
{
  enum { COLS = 80, ROWS = 50, CHARSIZE = 8 };

  enum Color
  {
    BLACK, WHITE, RED, CYAN, PURPLE, GREEN, BLUE, YELLOW,
    ORANGE, BROWN, LIGHTRED, DARKGREY, GREY, LIGHTGREEN, LIGHTBLUE, LIGHTGREY
  };

  // Screen codes of the graphics characters
  enum
  {
    SC_AT = 0,
    SC_POUND = 28,
    SC_ARROWUP = 30,
    SC_ARROWLEFT = 31,
    SC_SPACE = 32,
    SC_HLINE = 64,
    SC_SPADE = 65,
    SC_ROUND_TR = 73,       // ╮
    SC_ROUND_BL = 74,       // ╰
    SC_ROUND_BR = 75,       // ╯
    SC_DIAG_DOWN = 77,      // ╲
    SC_DIAG_UP = 78,        // ╱
    SC_BALL = 81,
    SC_ROUND_TL = 85,       // ╭
    SC_CIRCLE = 87,
    SC_DIAMOND = 90,
    SC_CROSS = 91,
    SC_VLINE = 93,
    SC_TRI_UR = 95,
    SC_HALF_LEFT = 97,
    SC_HALF_LOWER = 98,
    SC_EIGHTH_UPPER = 99,
    SC_EIGHTH_LOWER = 100,
    SC_EIGHTH_LEFT = 101,
    SC_CHECKER = 102,
    SC_EIGHTH_RIGHT = 103,
    SC_TRI_UL = 105,
    SC_QUARTER_RIGHT = 106,
    SC_TEE_RIGHT = 107,     // ├
    SC_QUAD_LR = 108,
    SC_CORNER_BL = 109,     // └
    SC_CORNER_TR = 110,     // ┐
    SC_QUARTER_LOWER = 111,
    SC_CORNER_TL = 112,     // ┌
    SC_TEE_UP = 113,        // ┴
    SC_TEE_DOWN = 114,      // ┬
    SC_TEE_LEFT = 115,      // ┤
    SC_QUARTER_LEFT = 116,
    SC_3EIGHTHS_LEFT = 117,
    SC_3EIGHTHS_RIGHT = 118,
    SC_QUARTER_UPPER = 119,
    SC_3EIGHTHS_UPPER = 120,
    SC_3EIGHTHS_LOWER = 121,
    SC_CHECKMARK = 122,
    SC_QUAD_LL = 123,
    SC_QUAD_UR = 124,
    SC_CORNER_BR = 125,     // ┘
    SC_QUAD_UL = 126,
    SC_QUAD_ULLR = 127,
    SC_REVERSE = 128,
    SC_BLOCK = 160          // Reverse space: a full block
  };

  // Screen code of an ASCII character (letters in upper case)
  int screencode(int ascii);

  // Starts a new screen: everything blank in background color bg, text in fg
  void clear(int bg, int fg);
  void setborder(int color);
  int background(void);

  // Puts one character. bg -1 = the screen background
  void put(int x, int y, int code, int fg, int bg = -1);
  // Prints ASCII text (reverse: in reverse video). Returns the column after it
  int print(int x, int y, const char *text, int fg, bool reverse = false, int bg = -1);
  int printfmt(int x, int y, int fg, const char *fmt, ...);
  void fill(int x, int y, int w, int h, int code, int fg, int bg = -1);
  // Sets the colors of an area, keeping the characters
  void color(int x, int y, int w, int h, int fg, int bg = -1);
  // Reverses an area (reverse video on/off)
  void invert(int x, int y, int w, int h);
  int codeat(int x, int y);

  // A box with rounded corners, the title on its top edge
  void box(int x, int y, int w, int h, int fg, const char *title = NULL, int titlefg = -1);
  void hline(int x, int y, int w, int fg);
  // Bars made of eighth blocks: frac 0-1 of h rows (from the bottom) or w
  // columns (from the left). Empty parts are drawn in track color (-1 = none)
  void vbar(int x, int y, int w, int h, float frac, int fg, int track = -1);
  void hbar(int x, int y, int w, float frac, int fg, int track = -1);

  // Works out the size and position of the screen in the window and the
  // mouse position on it. Call before drawing a frame
  void layout(SDL_Renderer *renderer);
  // Draws the screen and the border (behind all ImGui windows)
  void present(SDL_Renderer *renderer);
  void freescreen(void);

  // The mouse position in characters (with fractions), and whether it is on
  // the screen
  float mousex(void);
  float mousey(void);
  bool mouseinside(void);
  // Size of one character in ImGui points
  float charsize(void);

  // Loads a real C64 character ROM (4096 or 2048 bytes) from the given
  // directory if one is there, in place of the built-in characters
  void loadrom(const char *dir);
  // True if the characters come from such a ROM
  bool usingrom(void);
}

#endif
