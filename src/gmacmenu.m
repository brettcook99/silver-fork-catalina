//
// GOATTRACKER v2 macOS menu bar
//
// File, Edit, Control and Help menus are added between the application
// menu and the Window menu that SDL creates. Clicking an item queues its key
// (see gmacmenu.h); getkey() then handles it like a keypress.
//

#import <Cocoa/Cocoa.h>
#include "SDL_keysym.h"
#include "gmacmenu.h"

static int pendingitem = 0;
static int recordstate = 1;
static int followstate = 1;

// The tracker handles the shortcut keys itself (they also arrive as SDL key
// events), so these menus must not act on them a second time. They only
// show the shortcuts and handle clicks
@interface GTMenu : NSMenu
@end

@implementation GTMenu
- (BOOL)performKeyEquivalent:(NSEvent *)event
{
  (void)event;
  return NO;
}
@end

@interface GTMenuTarget : NSObject
- (void)itemSelected:(id)sender;
@end

@implementation GTMenuTarget
- (void)itemSelected:(id)sender
{
  pendingitem = (int)[sender tag];
}

- (BOOL)validateMenuItem:(NSMenuItem *)item
{
  int tag = (int)[item tag];

  if (tag == (SDLK_r | MACMENU_CMD))
    [item setState:recordstate ? NSControlStateValueOn : NSControlStateValueOff];
  if (tag == (SDLK_f | MACMENU_CMD))
    [item setState:followstate ? NSControlStateValueOn : NSControlStateValueOff];

  // Tag 0 = not available (Redo)
  return tag != 0;
}
@end

static GTMenuTarget *target = nil;

// Adds an item. key is the key equivalent shown in the menu ("" = none) and
// tag is what the item does (a key with modifier flags, 0 = disabled)
static void additem(NSMenu *menu, NSString *title, NSString *key, int tag)
{
  NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:title action:@selector(itemSelected:) keyEquivalent:key];
  NSEventModifierFlags mask = 0;

  if (tag & MACMENU_CMD) mask |= NSEventModifierFlagCommand;
  if (tag & MACMENU_SHIFT) mask |= NSEventModifierFlagShift;
  [item setKeyEquivalentModifierMask:mask];
  [item setTarget:target];
  [item setTag:tag];
  [menu addItem:item];
  [item release];
}

static NSMenu *newmenu(NSString *title)
{
  NSMenu *menu = [[GTMenu alloc] initWithTitle:title];
  [menu setAutoenablesItems:YES];
  return menu;
}

// Puts a menu into the menu bar at position index
static void insertmenu(NSMenu *mainmenu, NSMenu *menu, NSInteger index)
{
  NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:[menu title] action:nil keyEquivalent:@""];
  [item setSubmenu:menu];
  [mainmenu insertItem:item atIndex:index];
  [item release];
}

void macmenu_init(void)
{
  static int initted = 0;

  if (initted) return;
  initted = 1;

  @autoreleasepool
  {
    NSMenu *mainmenu = [NSApp mainMenu];
    NSMenu *file, *edit, *control, *help;
    NSInteger index = -1;

    if (!mainmenu) return;

    target = [[GTMenuTarget alloc] init];

    file = newmenu(@"File");
    additem(file, @"Load Song/Instrument…", @"o", SDLK_o | MACMENU_CMD);
    additem(file, @"Merge Song…", @"o", SDLK_o | MACMENU_CMD | MACMENU_SHIFT);
    additem(file, @"Save Song/Instrument…", @"s", SDLK_s | MACMENU_CMD);
    [file addItem:[NSMenuItem separatorItem]];
    additem(file, @"Import MIDI File…", @"i", SDLK_i | MACMENU_CMD);

    edit = newmenu(@"Edit");
    additem(edit, @"Undo", @"z", SDLK_z | MACMENU_CMD);
    additem(edit, @"Redo", @"z", 0);
    [[edit itemAtIndex:1] setKeyEquivalentModifierMask:NSEventModifierFlagCommand | NSEventModifierFlagShift];
    [edit addItem:[NSMenuItem separatorItem]];
    additem(edit, @"Cut", @"x", SDLK_x | MACMENU_CMD);
    additem(edit, @"Copy", @"c", SDLK_c | MACMENU_CMD);
    additem(edit, @"Paste", @"v", SDLK_v | MACMENU_CMD);
    additem(edit, @"Delete", @"", SDLK_DELETE);
    additem(edit, @"Select All", @"a", SDLK_a | MACMENU_CMD);

    control = newmenu(@"Control");
    additem(control, @"Play From Beginning", @"1", SDLK_1 | MACMENU_CMD);
    additem(control, @"Play From Current Position", @"2", SDLK_2 | MACMENU_CMD);
    additem(control, @"Play Pattern", @"3", SDLK_3 | MACMENU_CMD);
    additem(control, @"Stop", @"4", SDLK_4 | MACMENU_CMD);
    additem(control, @"Play / Stop", @" ", MACMENU_PLAYSTOP);
    [control addItem:[NSMenuItem separatorItem]];
    additem(control, @"Record (Edit Mode)", @"r", SDLK_r | MACMENU_CMD);
    additem(control, @"Follow Playback", @"f", SDLK_f | MACMENU_CMD);
    [control addItem:[NSMenuItem separatorItem]];
    additem(control, @"Go To Pattern Editor", @"5", SDLK_5 | MACMENU_CMD);
    additem(control, @"Go To Song Editor", @"6", SDLK_6 | MACMENU_CMD);
    additem(control, @"Go To Instrument/Table Editor", @"7", SDLK_7 | MACMENU_CMD);
    additem(control, @"Go To Songname Editor", @"8", SDLK_8 | MACMENU_CMD);
    [control addItem:[NSMenuItem separatorItem]];
    additem(control, @"Export Song", @"9", SDLK_9 | MACMENU_CMD);

    help = newmenu(@"Help");
    additem(help, @"GoatTracker Help", @"", SDLK_F12);

    // File, Edit and Control go before the Window menu, Help at the end
    if ([NSApp windowsMenu]) index = [mainmenu indexOfItemWithSubmenu:[NSApp windowsMenu]];
    if (index < 0) index = [mainmenu indexOfItemWithTitle:@"Window"];
    if (index < 0) index = [mainmenu numberOfItems];
    insertmenu(mainmenu, file, index);
    insertmenu(mainmenu, edit, index + 1);
    insertmenu(mainmenu, control, index + 2);
    insertmenu(mainmenu, help, [mainmenu numberOfItems]);
    [NSApp setHelpMenu:help];

    [file release];
    [edit release];
    [control release];
    [help release];
  }
}

int macmenu_getitem(void)
{
  int item = pendingitem;
  pendingitem = 0;
  return item;
}

void macmenu_setstate(int record, int follow)
{
  recordstate = record;
  followstate = follow;
}
