#ifndef GRETRO_H
#define GRETRO_H

// The retro interface: the classic GoatTracker main screen on the 100x37
// text screen, and its mouse controls

// Draws the main screen (patterns, orderlist, instrument, tables, names and
// the status lines) on the text screen
void retro_printstatus(void);
// Handles the mouse on the main screen (mousex/mousey/mouseb/mousewheel)
void retro_mousecommands(void);

// The pages: the main screen, and the instrument page ([EDIT] right of the
// instrument; blank for now)
#define RETROPAGE_MAIN 0
#define RETROPAGE_INSTRUMENT 1
extern int retropage;
void retro_openinstrpage(void);
void retro_closepage(void);

#endif
