// JUCE key presses -> SDL key codes, scan codes and modifiers, which the
// GoatTracker core expects (it is the standalone program's SDL code)

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

struct SdlKey
{
  int sym = 0;       // SDL_Keycode (0 = not a key GoatTracker knows)
  int scancode = 0;  // SDL_Scancode
};

SdlKey toSdlKey(const juce::KeyPress &key);
int toSdlMods(const juce::ModifierKeys &mods);
