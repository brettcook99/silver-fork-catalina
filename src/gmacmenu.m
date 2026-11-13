//
// GOATTRACKER v2 macOS menu bar
//
// File, Edit, Control, View and Help menus are added between the
// application menu and the Window menu that SDL creates. Clicking an item
// queues its command (gcommand.h); the main loop runs it.
//

#import <Cocoa/Cocoa.h>
#include "gmacmenu.h"
#include "gcommand.h"

// Retro screen background (gconsole.c): 0 = black, 1 = gradient
extern unsigned retrogradient;

// The credit line of the About panel (ABOUTTEXT in goattrk2.h)
extern const char abouttext[];

// Recent files list, kept by the GUI
extern int gui_recentfilecount(void);
extern const char *gui_recentfile(int index);

static int recordstate = 1;
static int followstate = 1;
static int retrostate = 0;
static int designerstate = 0;

// The GUI handles the shortcut keys itself (they also arrive as SDL key
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

@interface GTMenuTarget : NSObject <NSMenuDelegate>
- (void)itemSelected:(id)sender;
- (void)recentSelected:(id)sender;
- (void)aboutSelected:(id)sender;
@end

@implementation GTMenuTarget
- (void)itemSelected:(id)sender
{
  // A shortcut key that reaches the menu anyway (performKeyEquivalent above
  // is not asked for every submenu) must not run its command a second time:
  // the key has already arrived as an SDL key event. Running it twice broke
  // e.g. CMD+C (the second copy, with the selection gone, copied the whole
  // pattern), CMD+V (pasted that and went to the end of the pattern) and the
  // CMD+A/R/F toggles. Clicks, and RETURN in a menu opened from the keyboard,
  // still work
  NSEvent *event = [NSApp currentEvent];
  if ((event) && ([event type] == NSEventTypeKeyDown) && ([[sender keyEquivalent] length]) &&
      ([event modifierFlags] & (NSEventModifierFlagCommand | NSEventModifierFlagFunction)))
    return;

  cmd_post((int)[sender tag]);
}

// The standard About panel with the credit line right under the version,
// then the original authors
- (void)aboutSelected:(id)sender
{
  NSString *credit = [NSString stringWithFormat:@"%@\n\n", [NSString stringWithUTF8String:abouttext]];
  NSString *basedon = @"Based on GoatTracker 2 by Lasse Öörni and contributors.\nreSID by Dag Lem, reSID-fp by Antti S. Lankila.";
  NSMutableParagraphStyle *center = [[[NSMutableParagraphStyle alloc] init] autorelease];
  NSDictionary *creditattrs, *attrs;
  NSMutableAttributedString *credits;

  (void)sender;
  [center setAlignment:NSTextAlignmentCenter];
  creditattrs = [NSDictionary dictionaryWithObjectsAndKeys:[NSFont boldSystemFontOfSize:[NSFont systemFontSize]], NSFontAttributeName,
    center, NSParagraphStyleAttributeName, [NSColor labelColor], NSForegroundColorAttributeName, nil];
  attrs = [NSDictionary dictionaryWithObjectsAndKeys:[NSFont systemFontOfSize:[NSFont smallSystemFontSize]], NSFontAttributeName,
    center, NSParagraphStyleAttributeName, [NSColor secondaryLabelColor], NSForegroundColorAttributeName, nil];
  credits = [[[NSMutableAttributedString alloc] initWithString:credit attributes:creditattrs] autorelease];
  [credits appendAttributedString:[[[NSAttributedString alloc] initWithString:basedon attributes:attrs] autorelease]];
  // An empty copyright line: the credit line is already at the top
  [NSApp orderFrontStandardAboutPanelWithOptions:[NSDictionary dictionaryWithObjectsAndKeys:
    @" ", @"Copyright", credits, @"Credits", nil]];
}

- (void)recentSelected:(id)sender
{
  NSString *path = [sender representedObject];
  if (path) cmd_postfile(GTCMD_OPENFILE, [path fileSystemRepresentation]);
}

- (BOOL)validateMenuItem:(NSMenuItem *)item
{
  int tag = (int)[item tag];

  if (tag == GTCMD_RECORD)
    [item setState:recordstate ? NSControlStateValueOn : NSControlStateValueOff];
  if (tag == GTCMD_FOLLOW)
    [item setState:followstate ? NSControlStateValueOn : NSControlStateValueOff];
  if (tag == GTCMD_DESIGNER)
    [item setState:designerstate ? NSControlStateValueOn : NSControlStateValueOff];
  if (tag == GTCMD_RETROGRADIENT)
    [item setState:retrogradient ? NSControlStateValueOn : NSControlStateValueOff];

  // Tag 0 = not available
  if (([item action] == @selector(itemSelected:)) && (tag == 0)) return NO;

  // The panels are only in the modern interface
  if ((retrostate) && ((tag == GTCMD_TOGGLELIBRARY) || (tag == GTCMD_TOGGLEINSPECTOR) || (tag == GTCMD_TOGGLETRACKS) ||
      (tag == GTCMD_TOGGLETABLES) || (tag == GTCMD_TOGGLESONG) || (tag == GTCMD_RESETLAYOUT)))
    return NO;
  // The background gradient and the level meters are the retro screen's
  if ((!retrostate) && ((tag == GTCMD_RETROGRADIENT) || (tag == GTCMD_METERCOLORS)))
    return NO;
  return YES;
}

// Fills the Open Recent menu each time it opens
- (void)menuNeedsUpdate:(NSMenu *)menu
{
  int c;
  int count = gui_recentfilecount();

  [menu removeAllItems];
  for (c = 0; c < count; c++)
  {
    const char *path = gui_recentfile(c);
    NSString *fullpath;
    NSMenuItem *item;

    if ((!path) || (!path[0])) continue;
    fullpath = [NSString stringWithUTF8String:path];
    if (!fullpath) continue;
    item = [[NSMenuItem alloc] initWithTitle:[fullpath lastPathComponent] action:@selector(recentSelected:) keyEquivalent:@""];
    [item setTarget:self];
    [item setRepresentedObject:fullpath];
    [item setToolTip:fullpath];
    [menu addItem:item];
    [item release];
  }
  if (![menu numberOfItems])
  {
    NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:@"No Recent Files" action:nil keyEquivalent:@""];
    [item setEnabled:NO];
    [menu addItem:item];
    [item release];
  }
}
@end

static GTMenuTarget *target = nil;

#define MOD_CMD 1
#define MOD_SHIFT 2
#define MOD_OPTION 4
#define MOD_CTRL 8

// Adds an item. key is the key equivalent shown in the menu ("" = none),
// mods the modifiers shown with it and cmd the command it runs
static void additem(NSMenu *menu, NSString *title, NSString *key, int mods, int cmd)
{
  NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:title action:@selector(itemSelected:) keyEquivalent:key];
  NSEventModifierFlags mask = 0;

  if (mods & MOD_CMD) mask |= NSEventModifierFlagCommand;
  if (mods & MOD_SHIFT) mask |= NSEventModifierFlagShift;
  if (mods & MOD_OPTION) mask |= NSEventModifierFlagOption;
  if (mods & MOD_CTRL) mask |= NSEventModifierFlagControl;
  [item setKeyEquivalentModifierMask:mask];
  [item setTarget:target];
  [item setTag:cmd];
  [menu addItem:item];
  [item release];
}

// An item with a function key (F1...) as its shortcut
static void addfkeyitem(NSMenu *menu, NSString *title, int fkey, int mods, int cmd)
{
  unichar ch = (unichar)(NSF1FunctionKey + fkey - 1);
  additem(menu, title, [NSString stringWithCharacters:&ch length:1], mods, cmd);
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

static void addsubmenu(NSMenu *menu, NSMenu *submenu)
{
  NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:[submenu title] action:nil keyEquivalent:@""];
  [item setSubmenu:submenu];
  [menu addItem:item];
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
    NSMenu *file, *recent, *import, *edit, *control, *view, *help;
    NSInteger index = -1;

    if (!mainmenu) return;

    target = [[GTMenuTarget alloc] init];

    file = newmenu(@"File");
    additem(file, @"New Song", @"n", MOD_CMD, GTCMD_NEW);
    additem(file, @"Open…", @"o", MOD_CMD, GTCMD_OPEN);
    recent = newmenu(@"Open Recent");
    [recent setDelegate:target];
    addsubmenu(file, recent);
    [file addItem:[NSMenuItem separatorItem]];
    additem(file, @"Save", @"s", MOD_CMD, GTCMD_SAVE);
    additem(file, @"Save As…", @"s", MOD_CMD | MOD_SHIFT, GTCMD_SAVEAS);
    [file addItem:[NSMenuItem separatorItem]];
    additem(file, @"Merge Song…", @"o", MOD_CMD | MOD_SHIFT, GTCMD_MERGE);
    import = newmenu(@"Import");
    additem(import, @"SID File…", @"i", MOD_CMD | MOD_SHIFT, GTCMD_IMPORTSID);
    additem(import, @"MIDI File…", @"i", MOD_CMD, GTCMD_IMPORTMIDI);
    additem(import, @"chipsynth C64 Patch (.fermatax)…", @"", 0, GTCMD_IMPORTPATCH);
    addsubmenu(file, import);
    [file addItem:[NSMenuItem separatorItem]];
    additem(file, @"Load Instrument…", @"", 0, GTCMD_LOADINSTR);
    additem(file, @"Save Instrument…", @"", 0, GTCMD_SAVEINSTR);
    [file addItem:[NSMenuItem separatorItem]];
    additem(file, @"Export SID / PRG / BIN…", @"e", MOD_CMD, GTCMD_EXPORT);

    edit = newmenu(@"Edit");
    additem(edit, @"Undo", @"z", MOD_CMD, GTCMD_UNDO);
    [edit addItem:[NSMenuItem separatorItem]];
    additem(edit, @"Cut", @"x", MOD_CMD, GTCMD_CUT);
    additem(edit, @"Copy", @"c", MOD_CMD, GTCMD_COPY);
    additem(edit, @"Paste", @"v", MOD_CMD, GTCMD_PASTE);
    additem(edit, @"Delete", @"", 0, GTCMD_DELETE);
    additem(edit, @"Select All", @"a", MOD_CMD, GTCMD_SELECTALL);
    [edit addItem:[NSMenuItem separatorItem]];
    additem(edit, @"Transpose Up", @"", 0, GTCMD_TRANSPOSEUP);
    additem(edit, @"Transpose Down", @"", 0, GTCMD_TRANSPOSEDOWN);
    additem(edit, @"Transpose Octave Up", @"", 0, GTCMD_TRANSPOSEOCTUP);
    additem(edit, @"Transpose Octave Down", @"", 0, GTCMD_TRANSPOSEOCTDOWN);
    [edit addItem:[NSMenuItem separatorItem]];
    additem(edit, @"Hard Restart ADSR…", @"", 0, GTCMD_HRADSR);
    additem(edit, @"Clear / Optimize Song…", @"", 0, GTCMD_CLEARSONG);

    control = newmenu(@"Control");
    additem(control, @"Play / Stop", @" ", 0, GTCMD_PLAYSTOP);
    addfkeyitem(control, @"Play From Beginning", 1, 0, GTCMD_PLAYSONG);
    addfkeyitem(control, @"Play From Current Position", 2, 0, GTCMD_PLAYPOS);
    addfkeyitem(control, @"Play Pattern", 3, 0, GTCMD_PLAYPATTERN);
    addfkeyitem(control, @"Stop", 4, 0, GTCMD_STOP);
    [control addItem:[NSMenuItem separatorItem]];
    additem(control, @"Record (Edit Mode)", @"", 0, GTCMD_RECORD);
    additem(control, @"Follow Playback", @"f", MOD_CMD, GTCMD_FOLLOW);
    [control addItem:[NSMenuItem separatorItem]];
    additem(control, @"Octave Up", @"", 0, GTCMD_OCTAVEUP);
    additem(control, @"Octave Down", @"", 0, GTCMD_OCTAVEDOWN);
    additem(control, @"Next Subtune", @"", 0, GTCMD_NEXTSUBTUNE);
    additem(control, @"Previous Subtune", @"", 0, GTCMD_PREVSUBTUNE);
    [control addItem:[NSMenuItem separatorItem]];
    addfkeyitem(control, @"Pattern Editor", 5, 0, GTCMD_EDITPATTERN);
    addfkeyitem(control, @"Arrangement (Orderlist)", 6, 0, GTCMD_EDITORDER);
    addfkeyitem(control, @"Instrument / Tables", 7, 0, GTCMD_EDITINSTR);
    addfkeyitem(control, @"Song Info", 8, 0, GTCMD_EDITNAMES);

    view = newmenu(@"View");
    additem(view, @"SID Designer", @"d", MOD_CMD, GTCMD_DESIGNER);
    [view addItem:[NSMenuItem separatorItem]];
    additem(view, @"Library", @"", 0, GTCMD_TOGGLELIBRARY);
    additem(view, @"Inspector", @"", 0, GTCMD_TOGGLEINSPECTOR);
    additem(view, @"Arrangement", @"", 0, GTCMD_TOGGLETRACKS);
    additem(view, @"Tables", @"", 0, GTCMD_TOGGLETABLES);
    additem(view, @"Song Info", @"", 0, GTCMD_TOGGLESONG);
    additem(view, @"Reset Layout", @"", 0, GTCMD_RESETLAYOUT);
    [view addItem:[NSMenuItem separatorItem]];
    additem(view, @"Bigger Text", @"=", MOD_CMD, GTCMD_ZOOMIN);
    additem(view, @"Smaller Text", @"-", MOD_CMD, GTCMD_ZOOMOUT);
    additem(view, @"Default Text Size", @"0", MOD_CMD, GTCMD_ZOOMRESET);
    [view addItem:[NSMenuItem separatorItem]];
    additem(view, @"Enter Full Screen", @"f", MOD_CMD | MOD_CTRL, GTCMD_FULLSCREEN);
    [view addItem:[NSMenuItem separatorItem]];
    additem(view, @"Gradient Background", @"", 0, GTCMD_RETROGRADIENT);
    additem(view, @"Meter Colors…", @"", 0, GTCMD_METERCOLORS);

    help = newmenu(@"Help");
    additem(help, @"GoatTracker Ultimate Help", @"?", MOD_CMD, GTCMD_HELP);

    // File, Edit, Control and View go before the Window menu, Help at the end
    if ([NSApp windowsMenu]) index = [mainmenu indexOfItemWithSubmenu:[NSApp windowsMenu]];
    if (index < 0) index = [mainmenu indexOfItemWithTitle:@"Window"];
    if (index < 0) index = [mainmenu numberOfItems];
    insertmenu(mainmenu, file, index);
    insertmenu(mainmenu, edit, index + 1);
    insertmenu(mainmenu, control, index + 2);
    insertmenu(mainmenu, view, index + 3);
    insertmenu(mainmenu, help, [mainmenu numberOfItems]);
    [NSApp setHelpMenu:help];

    // The application menu's About item (made by SDL) shows the credits too
    if ([mainmenu numberOfItems] > 0)
    {
      NSMenu *appmenu = [[mainmenu itemAtIndex:0] submenu];
      NSInteger c;
      for (c = 0; (appmenu) && (c < [appmenu numberOfItems]); c++)
      {
        NSMenuItem *item = [appmenu itemAtIndex:c];
        if ([item action] == @selector(orderFrontStandardAboutPanel:))
        {
          [item setTarget:target];
          [item setAction:@selector(aboutSelected:)];
        }
      }
    }

    [file release];
    [recent release];
    [import release];
    [edit release];
    [control release];
    [view release];
    [help release];
  }
}

void macmenu_setstate(int record, int follow)
{
  recordstate = record;
  followstate = follow;
}

void macmenu_setretro(int retro)
{
  retrostate = retro;
}

void macmenu_setdesigner(int on)
{
  designerstate = on;
}
