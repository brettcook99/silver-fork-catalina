#ifndef GRETRO_H
#define GRETRO_H

// The retro interface: the classic GoatTracker main screen on the 100x37
// text screen, and its mouse controls

// Draws the main screen (patterns, orderlist, instrument, tables, names and
// the status lines) on the text screen
void retro_printstatus(void);
// Handles the mouse on the main screen (mousex/mousey/mouseb/mousewheel)
void retro_mousecommands(void);

#endif
