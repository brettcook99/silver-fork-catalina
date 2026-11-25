#ifndef GRETRO_H
#define GRETRO_H

// The retro interface: the classic GoatTracker main screen on the 100x37
// text screen, and its mouse controls

// Draws the main screen (patterns, orderlist, instrument, tables, names and
// the status lines) on the text screen
void retro_printstatus(void);
// Handles the mouse on the main screen (mousex/mousey/mouseb/mousewheel)
void retro_mousecommands(void);
// The pattern row right-clicked last (the pattern menu pastes there when
// nothing is marked)
extern int retro_pattmenurow;
// MIDI CC 90 (value 0-127): the value of the effect picked in the effects
// panel, as the mouse wheel sets it
void retro_fxknob(int value);
// MIDI CC 90 with the mouse on SPEED (plugin, RESO X2): sets SPEED. Returns
// 0 if the mouse isn't there
int retro_speedknob(int value);

// The title bar (top row), also shown on the SID designer, and a left click
// on it at column x
void retro_printtitlebar(void);
void retro_titlebarclick(int x);

#endif
