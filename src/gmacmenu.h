#ifndef GMACMENU_H
#define GMACMENU_H

// macOS menu bar: File, Edit, Control and Help menus. A clicked menu item
// works exactly like its keyboard shortcut: the tracker gets the same key.
// The shortcut keys themselves are handled by the tracker (see getkey()),
// so they work the same with and without the menu bar.
//
// A menu item is a key: an SDL key code plus these modifier flags
#define MACMENU_CMD 0x1000
#define MACMENU_SHIFT 0x2000
#define MACMENU_KEYMASK 0x0fff
// ...or the Play / Stop command, which has no key of its own outside the
// pattern editor
#define MACMENU_PLAYSTOP 0x4000

#ifdef __APPLE__
// Adds the menus to the menu bar. Call after the window is open
void macmenu_init(void);
// Returns the menu item clicked since the last call, 0 = none
int macmenu_getitem(void);
// Tells the menus whether record (edit) mode and follow playback are on, to
// show check marks
void macmenu_setstate(int record, int follow);
#else
#define macmenu_init()
#define macmenu_getitem() 0
#define macmenu_setstate(record, follow)
#endif

#endif
