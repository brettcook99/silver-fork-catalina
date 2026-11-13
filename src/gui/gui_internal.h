// Shared declarations of the GoatTracker GUI files (C++ only)

#ifndef GUI_INTERNAL_H
#define GUI_INTERNAL_H

#include "gt.h"
#include "../imgui/imgui.h"
#include "../imgui/imgui_internal.h"

// Panel (window) names. They are also the IDs in the saved layout
#define WIN_TRACKS "Arrangement"
#define WIN_PATTERN "Pattern Editor"
#define WIN_LIBRARY "Library"
#define WIN_INSPECTOR "Inspector"
#define WIN_TABLES "Tables"
#define WIN_SONG "Song"
#define WIN_HELP "GoatTracker Help"

namespace gui
{
  // Colors of the interface
  struct Theme
  {
    ImU32 bgdeep;       // Behind everything
    ImU32 bgpanel;      // Panel background
    ImU32 bgraised;     // Headers, bars
    ImU32 bgfield;      // Value fields, grid
    ImU32 bgrowalt;     // Highlighted grid rows (every step)
    ImU32 bgrowbar;     // Highlighted grid rows (every bar)
    ImU32 border;
    ImU32 separator;
    ImU32 text;
    ImU32 textdim;
    ImU32 textfaint;
    ImU32 accent;       // Selection, cursor, focus
    ImU32 accentsoft;   // Selected area fill
    ImU32 playhead;     // Playing row / playhead
    ImU32 playheadsoft;
    ImU32 record;       // Record button
    ImU32 mute;
    ImU32 solo;
    ImU32 chn[MAX_CHN]; // Channel (track) colors
    ImU32 note;
    ImU32 rest;
    ImU32 keyoff;
    ImU32 instr;
    ImU32 command;
    ImU32 cmddata;
    ImU32 lcdbg;
    ImU32 lcdtext;
    ImU32 lcddim;
    ImU32 warning;
  };
  extern Theme theme;

  // Fonts: interface, interface bold, monospaced (grids, values)
  extern ImFont *fontui;
  extern ImFont *fontbold;
  extern ImFont *fontmono;
  // Base sizes before the text size setting is applied
  extern float uisize;
  extern float monosize;

  void applytheme(void);
  ImU32 alpha(ImU32 col, float a);
  ImU32 mix(ImU32 a, ImU32 b, float t);

  // Which panel the editors' keyboard focus belongs to
  const char *editmodewindow(int editmode);
  // Call at the start of every panel (after Begin): focuses the panel when
  // its editor got the keyboard (F5-F8, TAB) and sets the editor when the
  // panel is clicked. Returns true if the panel has the keyboard focus
  bool panelfocus(int editmode);
  // Draws the accent frame of the focused panel
  void drawfocusframe(void);

  // Undo for changes made with the mouse: mark the areas that may change,
  // change them, then finish
  void undobegin(void);
  void undoinstrument(int i);
  void undotable(int t);
  void undopattern(int p);
  void undoorderlist(void);
  void undoend(void);

  // Small widgets
  enum Icon
  {
    ICON_REWIND, ICON_PLAY, ICON_PLAYPOS, ICON_PLAYPATTERN, ICON_STOP, ICON_RECORD,
    ICON_FOLLOW, ICON_PREV, ICON_NEXT, ICON_UP, ICON_DOWN, ICON_PLUS, ICON_MINUS, ICON_SPEAKER, ICON_GOTO,
    ICON_DESIGNER
  };
  bool iconbutton(const char *id, Icon icon, bool active, ImU32 activecol, ImVec2 size, const char *tooltip);
  void drawicon(ImDrawList *dl, Icon icon, ImVec2 center, float size, ImU32 col);
  bool togglechip(const char *label, bool on, const char *tooltip, ImU32 oncol = 0);
  bool knob(const char *id, const char *label, int *value, int minv, int maxv, float radius, const char *tooltip);
  // A 2-digit hex value with the keyboard cursor drawn on digit cursordigit
  // (-1 = none). Returns the digit clicked (0/1) or -1
  int hexvalue(const char *id, unsigned value, int digits, int cursordigit, bool focused);
  void sectiontitle(const char *title);
  void tooltip(const char *text);

  // Pushes a key press for the editors, as if typed (rawkey = KEY_xxx)
  void sendkey(int rawkey, int key, bool shift);

  // Panels
  void drawcontrolbar(float height);
  void drawstatusbar(float height);
  void drawpatterneditor(bool *open);
  void drawtracks(bool *open);
  void drawlibrary(bool *open);
  void drawinspector(bool *open);
  void drawtables(bool *open);
  void drawsong(bool *open);
  void drawhelp(bool *open);
  void drawdialogs(void);
  void drawtextscreen(void);
  void drawmenubar(void);

  // The retro interface: the classic text screen with the original font and
  // palette, scaled to fill the window. Also sets the text screen mouse state
  void drawretroscreen(SDL_Renderer *renderer);
  void freeretroscreen(void);
  // Largest character cell (in pixels, 8:14 shape) that fits the 100x37
  // screen into maxx * maxy pixels
  void retrofitcell(int maxx, int maxy, int *w, int *h);
  // Color 0-15 of the classic palette
  ImU32 retrocolor(int index);
  // Draws text in the retro screen's font (8x14 characters), each font pixel
  // pixel x pixel points (pixelh tall), top left at pos. bold: one pixel
  // wider strokes. Returns the width
  float drawretrotext(ImDrawList *dl, ImVec2 pos, float pixel, float pixelh, ImU32 col, const char *text, bool bold = false);
  float retrotextwidth(const char *text, float pixel);
  // Draws only the text screen's top row (the title bar) where it is on the
  // retro screen, for the SID designer. screen0-screen1 get the whole retro
  // screen's place (ImGui points). Returns the column under the mouse
  // pointer, -1 if it is not on the title bar
  int drawretrotitlebar(SDL_Renderer *renderer, ImVec2 *screen0, ImVec2 *screen1);
  // The retro screen gets no mouse buttons until they all have been
  // released (a click that closed the SID designer doesn't click there too)
  void retroblockmouse(void);

  // The instrument designer (gui_designer.cpp), a full-window mode that looks
  // like a C64 screen. designerwanted() tells whether it is to be drawn this
  // frame (it is not drawn over a classic full-screen dialog)
  extern bool showdesigner;
  void opendesigner(void);
  void closedesigner(void);
  bool designerwanted(int textscreen);
  void drawdesigner(SDL_Renderer *renderer);
  // Key presses and releases, typed characters while it is shown
  void designerkey(const SDL_KeyboardEvent &k, bool down);
  void designertext(int ch);

  // Images built into the program (gui_image.cpp). decodepng() reads a PNG
  // (8-bit RGB or RGBA, not interlaced) into RGBA (free() it), NULL if it
  // can't. scaledtexture() makes an opaque texture of the image (over black)
  // at exactly w x h pixels
  unsigned char *decodepng(const unsigned char *data, size_t size, int *w, int *h);
  SDL_Texture *scaledtexture(SDL_Renderer *renderer, const unsigned char *rgba, int sw, int sh, int w, int h);

  // About GoatTracker Ultimate, the level meter colors dialog
  void openabout(void);
  void openmetercolors(void);

  // The settings folder (ends with a path separator)
  const char *configdirectory(void);

  // The startup window asking for the modern or the retro interface.
  // selected is the highlighted choice. Returns UI_MODERN or UI_RETRO when
  // chosen, UI_ASK until then
  int drawchooser(int *selected);

  // Panel visibility
  extern bool showtracks, showpattern, showlibrary, showinspector, showtables, showsong, showhelp;
  // Requests a new default layout on the next frame
  extern bool resetlayout;

  // The status bar message
  extern char statusmessage[256];
  extern double statusmessagetime;

  // Envelope level of each SID voice (0-1) for the meters, estimated from
  // the SID registers
  extern float voicelevel[MAX_CHN];
  void updatemeters(float dt);

  // Text screen mouse state
  extern int tsmousebuttons;
  extern int tsmousewheel;

  // Recent files
  int recentcount(void);
  const char *recentpath(int index);
}

// For gmacmenu.m
extern "C" int gui_recentfilecount(void);
extern "C" const char *gui_recentfile(int index);

#endif
