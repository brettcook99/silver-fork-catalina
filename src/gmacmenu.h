#ifndef GMACMENU_H
#define GMACMENU_H

// macOS menu bar: File, Edit, Control, View and Help menus. A clicked item
// queues its command (see gcommand.h). The shortcut keys themselves arrive
// as normal key presses and are handled by the GUI, so they work the same
// with and without the menu bar.

#ifdef __APPLE__
#ifdef __cplusplus
extern "C" {
#endif
// Adds the menus to the menu bar. Call after the window is open
void macmenu_init(void);
// Tells the menus whether record (edit) mode and follow playback are on, to
// show check marks
void macmenu_setstate(int record, int follow);
// Tells the menus whether the retro interface is used (it has no panels to
// show or hide)
void macmenu_setretro(int retro);
#ifdef __cplusplus
}
#endif
#else
#define macmenu_init()
#define macmenu_setstate(record, follow)
#define macmenu_setretro(retro)
#endif

#endif
