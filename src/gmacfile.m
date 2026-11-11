// macOS native open & save panels for GoatTracker

#import <Cocoa/Cocoa.h>
#include <string.h>
#include "gmacfile.h"

// "sng;sid" -> @[@"sng", @"sid"], or nil for any file
static NSArray *extensionlist(const char *exts)
{
  NSMutableArray *list;
  NSArray *parts;

  if ((!exts) || (!exts[0])) return nil;
  list = [NSMutableArray array];
  parts = [[NSString stringWithUTF8String:exts] componentsSeparatedByString:@";"];
  for (NSString *part in parts)
  {
    if ([part length]) [list addObject:part];
  }
  return [list count] ? list : nil;
}

static int copypath(NSURL *url, char *path, int pathsize)
{
  const char *p;

  if ((!url) || (pathsize <= 0)) return 0;
  p = [[url path] fileSystemRepresentation];
  if ((!p) || ((int)strlen(p) >= pathsize)) return 0;
  strcpy(path, p);
  return 1;
}

static void restorewindow(NSWindow *window)
{
  // Give the keyboard back to the tracker window
  if (window) [window makeKeyAndOrderFront:nil];
}

int macfile_open(const char *title, const char *exts, const char *startdir, char *path, int pathsize)
{
  @autoreleasepool
  {
    NSWindow *window = [NSApp keyWindow];
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    NSArray *types = extensionlist(exts);
    NSModalResponse result;

    if (title) [panel setMessage:[NSString stringWithUTF8String:title]];
    [panel setCanChooseFiles:YES];
    [panel setCanChooseDirectories:NO];
    [panel setAllowsMultipleSelection:NO];
    [panel setResolvesAliases:YES];
    if (types) [panel setAllowedFileTypes:types];
    if ((startdir) && (startdir[0]))
      [panel setDirectoryURL:[NSURL fileURLWithPath:[NSString stringWithUTF8String:startdir] isDirectory:YES]];

    result = [panel runModal];
    restorewindow(window);
    if (result != NSModalResponseOK) return 0;
    return copypath([panel URL], path, pathsize);
  }
}

int macfile_modifiersheld(void)
{
  NSEventModifierFlags flags = [NSEvent modifierFlags];
  return (flags & (NSEventModifierFlagShift | NSEventModifierFlagOption)) ? 1 : 0;
}

int macfile_save(const char *title, const char *ext, const char *suggestedname, const char *startdir, char *path, int pathsize)
{
  @autoreleasepool
  {
    NSWindow *window = [NSApp keyWindow];
    NSSavePanel *panel = [NSSavePanel savePanel];
    NSModalResponse result;

    if (title) [panel setMessage:[NSString stringWithUTF8String:title]];
    [panel setCanCreateDirectories:YES];
    [panel setExtensionHidden:NO];
    if ((ext) && (ext[0]))
      [panel setAllowedFileTypes:@[[NSString stringWithUTF8String:ext]]];
    if ((suggestedname) && (suggestedname[0]))
    {
      // Just the name: a full path would put the folders into the name field
      NSString *name = [[NSString stringWithUTF8String:suggestedname] lastPathComponent];
      [panel setNameFieldStringValue:name];
    }
    if ((startdir) && (startdir[0]))
      [panel setDirectoryURL:[NSURL fileURLWithPath:[NSString stringWithUTF8String:startdir] isDirectory:YES]];

    result = [panel runModal];
    restorewindow(window);
    if (result != NSModalResponseOK) return 0;
    return copypath([panel URL], path, pathsize);
  }
}
