#include "KeyMap.h"

// SDL2's scan codes (USB HID usage numbers) and key codes. Keys that type a
// character have that (unshifted) character as their key code, the others
// their scan code | (1 << 30)
namespace
{
  constexpr int keycodeOf(int scancode) { return scancode | (1 << 30); }

  enum
  {
    SC_A = 4, SC_1 = 30, SC_0 = 39, SC_RETURN = 40, SC_ESCAPE = 41, SC_BACKSPACE = 42, SC_TAB = 43, SC_SPACE = 44,
    SC_MINUS = 45, SC_EQUALS = 46, SC_LEFTBRACKET = 47, SC_RIGHTBRACKET = 48, SC_BACKSLASH = 49, SC_SEMICOLON = 51,
    SC_APOSTROPHE = 52, SC_GRAVE = 53, SC_COMMA = 54, SC_PERIOD = 55, SC_SLASH = 56, SC_F1 = 58, SC_F13 = 104,
    SC_PRINTSCREEN = 70, SC_SCROLLLOCK = 71, SC_PAUSE = 72, SC_INSERT = 73, SC_HOME = 74, SC_PAGEUP = 75,
    SC_DELETE = 76, SC_END = 77, SC_PAGEDOWN = 78, SC_RIGHT = 79, SC_LEFT = 80, SC_DOWN = 81, SC_UP = 82,
    SC_KP_DIVIDE = 84, SC_KP_MULTIPLY = 85, SC_KP_MINUS = 86, SC_KP_PLUS = 87, SC_KP_ENTER = 88, SC_KP_1 = 89,
    SC_KP_0 = 98, SC_KP_PERIOD = 99, SC_NONUSBACKSLASH = 100, SC_KP_EQUALS = 103
  };

  enum
  {
    KMOD_LSHIFT = 0x0001, KMOD_LCTRL = 0x0040, KMOD_LALT = 0x0100, KMOD_LGUI = 0x0400
  };

  SdlKey printable(int c)
  {
    SdlKey k;
    if ((c >= 'A') && (c <= 'Z')) c += 'a' - 'A';
    if ((c >= 'a') && (c <= 'z'))
    {
      k.sym = c;
      k.scancode = SC_A + (c - 'a');
      return k;
    }
    if ((c >= '1') && (c <= '9'))
    {
      k.sym = c;
      k.scancode = SC_1 + (c - '1');
      return k;
    }
    // Shifted characters of a US keyboard: their keys
    switch (c)
    {
      case '!': c = '1'; break;
      case '@': c = '2'; break;
      case '#': c = '3'; break;
      case '$': c = '4'; break;
      case '%': c = '5'; break;
      case '^': c = '6'; break;
      case '&': c = '7'; break;
      case '*': c = '8'; break;
      case '(': c = '9'; break;
      case ')': c = '0'; break;
      case '_': c = '-'; break;
      case '+': c = '='; break;
      case '{': c = '['; break;
      case '}': c = ']'; break;
      case '|': c = '\\'; break;
      case ':': c = ';'; break;
      case '"': c = '\''; break;
      case '~': c = '`'; break;
      case '<': c = ','; break;
      case '>': c = '.'; break;
      case '?': c = '/'; break;
    }
    if ((c >= '1') && (c <= '9')) return printable(c);
    k.sym = c;
    switch (c)
    {
      case '0': k.scancode = SC_0; break;
      case '-': k.scancode = SC_MINUS; break;
      case '=': k.scancode = SC_EQUALS; break;
      case '[': k.scancode = SC_LEFTBRACKET; break;
      case ']': k.scancode = SC_RIGHTBRACKET; break;
      case '\\': k.scancode = SC_BACKSLASH; break;
      case ';': k.scancode = SC_SEMICOLON; break;
      case '\'': k.scancode = SC_APOSTROPHE; break;
      case '`': k.scancode = SC_GRAVE; break;
      case ',': k.scancode = SC_COMMA; break;
      case '.': k.scancode = SC_PERIOD; break;
      case '/': k.scancode = SC_SLASH; break;
      case ' ': k.scancode = SC_SPACE; break;
      default:
        // A character of another keyboard layout: its code, no scan code
        if ((c > 32) && (c < 256)) k.scancode = 0;
        else k.sym = 0;
        break;
    }
    return k;
  }

  SdlKey special(int scancode, int sym = 0)
  {
    SdlKey k;
    k.scancode = scancode;
    k.sym = sym ? sym : keycodeOf(scancode);
    return k;
  }
}

SdlKey toSdlKey(const juce::KeyPress &key)
{
  using KP = juce::KeyPress;
  const int code = key.getKeyCode();

  if (code == KP::returnKey) return special(SC_RETURN, '\r');
  if (code == KP::escapeKey) return special(SC_ESCAPE, 27);
  if (code == KP::backspaceKey) return special(SC_BACKSPACE, 8);
  if (code == KP::tabKey) return special(SC_TAB, 9);
  if (code == KP::spaceKey) return special(SC_SPACE, ' ');
  if (code == KP::deleteKey) return special(SC_DELETE, 127);
  if (code == KP::insertKey) return special(SC_INSERT);
  if (code == KP::homeKey) return special(SC_HOME);
  if (code == KP::endKey) return special(SC_END);
  if (code == KP::pageUpKey) return special(SC_PAGEUP);
  if (code == KP::pageDownKey) return special(SC_PAGEDOWN);
  if (code == KP::upKey) return special(SC_UP);
  if (code == KP::downKey) return special(SC_DOWN);
  if (code == KP::leftKey) return special(SC_LEFT);
  if (code == KP::rightKey) return special(SC_RIGHT);

  const int fkeys[] = { KP::F1Key, KP::F2Key, KP::F3Key, KP::F4Key, KP::F5Key, KP::F6Key, KP::F7Key, KP::F8Key,
    KP::F9Key, KP::F10Key, KP::F11Key, KP::F12Key };
  for (int i = 0; i < 12; i++)
    if (code == fkeys[i]) return special(SC_F1 + i);
  const int fkeys2[] = { KP::F13Key, KP::F14Key, KP::F15Key, KP::F16Key };
  for (int i = 0; i < 4; i++)
    if (code == fkeys2[i]) return special(SC_F13 + i);

  if (code == KP::numberPad0) return special(SC_KP_0);
  if ((code >= KP::numberPad1) && (code <= KP::numberPad9)) return special(SC_KP_1 + (code - KP::numberPad1));
  if (code == KP::numberPadAdd) return special(SC_KP_PLUS);
  if (code == KP::numberPadSubtract) return special(SC_KP_MINUS);
  if (code == KP::numberPadMultiply) return special(SC_KP_MULTIPLY);
  if (code == KP::numberPadDivide) return special(SC_KP_DIVIDE);
  if (code == KP::numberPadDecimalPoint) return special(SC_KP_PERIOD);
  if (code == KP::numberPadEquals) return special(SC_KP_EQUALS);
  if (code == KP::numberPadDelete) return special(SC_KP_PERIOD);

  if ((code > 0) && (code < 256)) return printable(code);
  return {};
}

int toSdlMods(const juce::ModifierKeys &mods)
{
  int m = 0;
  if (mods.isShiftDown()) m |= KMOD_LSHIFT;
  if (mods.isCtrlDown()) m |= KMOD_LCTRL;
  if (mods.isAltDown()) m |= KMOD_LALT;
#if JUCE_MAC
  // JUCE's "command" is CMD on macOS (and CTRL elsewhere)
  if (mods.isCommandDown()) m |= KMOD_LGUI;
#endif
  return m;
}
