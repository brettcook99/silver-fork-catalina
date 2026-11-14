#include "PluginEditor.h"
#include "KeyMap.h"

#if JUCE_WINDOWS
#include <windows.h>
#endif

namespace
{
  const int minWidth = 800;   // the classic screen at 1x: 100 x 37 characters
  const int minHeight = 518;  // of 8 x 14 pixels

  // A path GoatTracker can open: it uses the C library's fopen(), which on
  // Windows takes the ANSI code page. Paths with other characters are given
  // in their short (8.3) form
  juce::String openablePath(const juce::File &file)
  {
    auto path = file.getFullPathName();
#if JUCE_WINDOWS
    bool ascii = true;
    for (auto c : path)
      if (c >= 128) ascii = false;
    if (!ascii)
    {
      wchar_t shortpath[MAX_PATH * 4] = {};
      if (GetShortPathNameW(path.toWideCharPointer(), shortpath, (DWORD)(sizeof shortpath / sizeof shortpath[0])))
        path = juce::String(shortpath);
    }
#endif
    return path;
  }

  juce::MouseCursor::StandardCursorType cursorType(int c)
  {
    switch (c)
    {
      case GTCURSOR_TEXT: return juce::MouseCursor::IBeamCursor;
      case GTCURSOR_RESIZEALL: return juce::MouseCursor::DraggingHandCursor;
      case GTCURSOR_RESIZENS: return juce::MouseCursor::UpDownResizeCursor;
      case GTCURSOR_RESIZEEW: return juce::MouseCursor::LeftRightResizeCursor;
      case GTCURSOR_RESIZENESW: return juce::MouseCursor::TopRightCornerResizeCursor;
      case GTCURSOR_RESIZENWSE: return juce::MouseCursor::TopLeftCornerResizeCursor;
      case GTCURSOR_HAND: return juce::MouseCursor::PointingHandCursor;
      case GTCURSOR_NOTALLOWED: return juce::MouseCursor::NoCursor;
      case GTCURSOR_NONE: return juce::MouseCursor::NoCursor;
      default: return juce::MouseCursor::NormalCursor;
    }
  }
}

GoatTrackerEditor::GoatTrackerEditor(GoatTrackerProcessor &p) : AudioProcessorEditor(&p), processor(p)
{
  int w = processor.editorWidth;
  int h = processor.editorHeight;

  coreOk = processor.coreReady();
  if ((w < minWidth) || (h < minHeight))
  {
    w = 1200;
    h = 777;
    if (coreOk) processor.getCore().defaultsize(&w, &h);
  }

  setWantsKeyboardFocus(true);
  setMouseClickGrabsKeyboardFocus(true);
  setResizable(true, true);
  setResizeLimits(minWidth, minHeight, 4800, 3200);
  setSize(w, h);

  // CTRL / CMD + and - in GoatTracker: the editor's next size
  juce::Component::SafePointer<GoatTrackerEditor> safe(this);
  processor.onResizeRequest = [safe](int nw, int nh)
  {
    if (auto *ed = safe.getComponent()) ed->setSize(juce::jmax(minWidth, nw), juce::jmax(minHeight, nh));
  };

  if (coreOk)
  {
    processor.getCore().editorvisible(1);
    sendSize();
  }
  startTimerHz(60);
}

GoatTrackerEditor::~GoatTrackerEditor()
{
  processor.onResizeRequest = nullptr;
  if (coreOk)
  {
    gtcore_inputevent in {};
    in.type = GTIN_FOCUS;
    in.focus = 0;
    send(in);
    processor.getCore().editorvisible(0);
  }
}

// Physical pixels per logical pixel: the one the last paint() saw, or an
// estimate before that
float GoatTrackerEditor::physicalScale()
{
  if (paintScale > 0.0f) return paintScale;
  float s = (float)juce::Component::getApproximateScaleFactorForComponent(this);
  if (auto *display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(getScreenBounds()))
    s *= (float)display->scale;
  if ((s < 0.5f) || (s > 8.0f)) s = 1.0f;
  return s;
}

void GoatTrackerEditor::send(gtcore_inputevent &in)
{
  if (coreOk) processor.getCore().input(&in);
}

void GoatTrackerEditor::sendSize()
{
  gtcore_inputevent in {};
  in.type = GTIN_RESIZE;
  in.w = getWidth();
  in.h = getHeight();
  in.scale = lastScale = physicalScale();
  send(in);
}

void GoatTrackerEditor::resized()
{
  processor.editorWidth = getWidth();
  processor.editorHeight = getHeight();
  if (coreOk) sendSize();
}

void GoatTrackerEditor::timerCallback()
{
  if (!coreOk) return;

  // Moved to a display with another scale
  if (std::abs(physicalScale() - lastScale) > 0.01f) sendSize();

  int w = 0, h = 0;
  auto &core = processor.getCore();
  core.getframe(0, nullptr, 0, 0, 0, &w, &h);
  if ((w > 0) && (h > 0))
  {
    if ((frame.getWidth() != w) || (frame.getHeight() != h))
    {
      frame = juce::Image(juce::Image::ARGB, w, h, false, juce::SoftwareImageType());
      frameSerial = 0;
    }
    {
      juce::Image::BitmapData data(frame, juce::Image::BitmapData::writeOnly);
      auto serial = core.getframe(frameSerial, data.data, data.lineStride, w, h, &w, &h);
      if (serial) frameSerial = serial;
      else w = 0;
    }
    if (w) repaint();
  }

  int c = core.getcursor();
  if (c != lastCursor)
  {
    lastCursor = c;
    setMouseCursor(juce::MouseCursor(cursorType(c)));
  }

  // Key-ups: JUCE only says that some key went up
  keyStateChanged(false);
}

void GoatTrackerEditor::paint(juce::Graphics &g)
{
  // The scale the picture is shown at: GoatTracker draws that many pixels
  {
    float s = g.getInternalContext().getPhysicalPixelScaleFactor();
    if ((s >= 0.5f) && (s <= 8.0f)) paintScale = s;
  }
  g.fillAll(juce::Colours::black);
  if (!coreOk)
  {
    g.setColour(juce::Colours::white);
    g.setFont(16.0f);
    g.drawFittedText("GoatTracker could not start:\n" + processor.getCore().getError(), getLocalBounds().reduced(20),
      juce::Justification::centred, 6);
    return;
  }
  if (frame.isValid())
  {
    // The picture has the display's real pixels
    const float s = lastScale > 0.0f ? lastScale : 1.0f;
    g.saveState();
    g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);
    g.addTransform(juce::AffineTransform::scale(1.0f / s));
    g.drawImageAt(frame, 0, 0);
    g.restoreState();
  }
}

/*
 * Keys
 */

bool GoatTrackerEditor::keyPressed(const juce::KeyPress &key)
{
  auto k = toSdlKey(key);
  if (!k.sym) return false;

  bool repeat = false;
  for (auto &h : heldKeys)
    if (h.juceCode == key.getKeyCode()) repeat = true;
  if (!repeat) heldKeys.push_back({ key.getKeyCode(), k.sym, k.scancode });

  gtcore_inputevent in {};
  in.type = GTIN_KEYDOWN;
  in.sym = k.sym;
  in.scancode = k.scancode;
  in.mods = toSdlMods(key.getModifiers());
  in.repeat = repeat ? 1 : 0;

  // The typed character (not with CTRL / CMD: those are commands)
  juce::String text;
  auto ch = key.getTextCharacter();
  if ((ch >= 32) && (ch != 127) && (!key.getModifiers().isCtrlDown()) && (!key.getModifiers().isCommandDown()))
    text = juce::String::charToString(ch);
  in.text = text.isNotEmpty() ? text.toRawUTF8() : nullptr;
  send(in);
  return true;
}

bool GoatTrackerEditor::keyStateChanged(bool isKeyDown)
{
  if (isKeyDown) return false;
  for (size_t i = 0; i < heldKeys.size();)
  {
    if (!juce::KeyPress::isKeyCurrentlyDown(heldKeys[i].juceCode))
    {
      gtcore_inputevent in {};
      in.type = GTIN_KEYUP;
      in.sym = heldKeys[i].sym;
      in.scancode = heldKeys[i].scancode;
      in.mods = toSdlMods(juce::ModifierKeys::getCurrentModifiersRealtime());
      send(in);
      heldKeys.erase(heldKeys.begin() + (long)i);
    }
    else i++;
  }
  return false;
}

void GoatTrackerEditor::modifierKeysChanged(const juce::ModifierKeys &mods)
{
  gtcore_inputevent in {};
  in.type = GTIN_MODS;
  in.mods = toSdlMods(mods);
  send(in);
}

void GoatTrackerEditor::focusGained(FocusChangeType)
{
  gtcore_inputevent in {};
  in.type = GTIN_FOCUS;
  in.focus = 1;
  send(in);
}

void GoatTrackerEditor::focusLost(FocusChangeType)
{
  heldKeys.clear();
  gtcore_inputevent in {};
  in.type = GTIN_FOCUS;
  in.focus = 0;
  send(in);
}

/*
 * Mouse
 */

void GoatTrackerEditor::mouseEvent(int type, const juce::MouseEvent &e, int button)
{
  gtcore_inputevent in {};
  in.type = type;
  in.x = juce::roundToInt(e.position.x);
  in.y = juce::roundToInt(e.position.y);
  in.button = button;
  in.clicks = e.getNumberOfClicks();
  in.mods = toSdlMods(e.mods);
  send(in);
}

void GoatTrackerEditor::mouseMove(const juce::MouseEvent &e)
{
  mouseEvent(GTIN_MOUSEMOVE, e, 0);
}

void GoatTrackerEditor::mouseDrag(const juce::MouseEvent &e)
{
  mouseEvent(GTIN_MOUSEMOVE, e, 0);
}

void GoatTrackerEditor::mouseDown(const juce::MouseEvent &e)
{
  // SDL's button numbers: 1 left, 2 middle, 3 right
  int button = e.mods.isLeftButtonDown() ? 1 : e.mods.isMiddleButtonDown() ? 2 : e.mods.isRightButtonDown() ? 3 : 0;
#if JUCE_MAC
  // CTRL+click is the right button on a Mac (as in the standalone program)
  if ((button == 1) && (e.mods.isCtrlDown())) button = 3;
#endif
  if (!button) return;
  grabKeyboardFocus();
  buttonsDown |= 1 << button;
  mouseEvent(GTIN_MOUSEDOWN, e, button);
}

void GoatTrackerEditor::mouseUp(const juce::MouseEvent &e)
{
  // Every button that was down goes up (JUCE reports one mouseUp)
  for (int b = 1; b <= 3; b++)
  {
    if (buttonsDown & (1 << b))
    {
      bool stillDown = (b == 1) ? juce::ModifierKeys::getCurrentModifiersRealtime().isLeftButtonDown()
                     : (b == 2) ? juce::ModifierKeys::getCurrentModifiersRealtime().isMiddleButtonDown()
                                : juce::ModifierKeys::getCurrentModifiersRealtime().isRightButtonDown();
      if (!stillDown)
      {
        buttonsDown &= ~(1 << b);
        mouseEvent(GTIN_MOUSEUP, e, b);
      }
    }
  }
}

void GoatTrackerEditor::mouseExit(const juce::MouseEvent &)
{
  gtcore_inputevent in {};
  in.type = GTIN_LEAVE;
  send(in);
}

void GoatTrackerEditor::mouseWheelMove(const juce::MouseEvent &e, const juce::MouseWheelDetails &wheel)
{
  // JUCE gives about 0.47 per wheel notch (Windows: 120 / 256)
  const float notch = 120.0f / 256.0f;
  float dy = wheel.deltaY / notch;
  float dx = -wheel.deltaX / notch;
  if (wheel.isReversed)
  {
    dy = -dy;
    dx = -dx;
  }
  // A wheel that clicks: at least one notch per event
  if (!wheel.isSmooth)
  {
    if ((dy > 0.0f) && (dy < 1.0f)) dy = 1.0f;
    if ((dy < 0.0f) && (dy > -1.0f)) dy = -1.0f;
  }
  gtcore_inputevent in {};
  in.type = GTIN_WHEEL;
  in.x = juce::roundToInt(e.position.x);
  in.y = juce::roundToInt(e.position.y);
  in.wheelx = dx;
  in.wheely = dy;
  in.mods = toSdlMods(e.mods);
  send(in);
}

/*
 * Files dragged from the desktop
 */

bool GoatTrackerEditor::isInterestedInFileDrag(const juce::StringArray &files)
{
  return coreOk && (files.size() > 0);
}

void GoatTrackerEditor::filesDropped(const juce::StringArray &files, int, int)
{
  // Opened as in the standalone program (the type comes from the extension
  // or the file's contents: .sng, .ins, .mid, .sid, .fermatax)
  for (auto &f : files)
  {
    juce::File file(f);
    if (!file.existsAsFile()) continue;
    auto path = openablePath(file);
    gtcore_inputevent in {};
    in.type = GTIN_DROPFILE;
    in.text = path.toRawUTF8();
    send(in);
  }
  grabKeyboardFocus();
}
