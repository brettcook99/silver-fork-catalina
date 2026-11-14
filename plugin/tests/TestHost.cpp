// A small plugin host for testing GoatTracker Ultimate without a DAW.
//
//   gttesthost <plugin.vst3> [options]
//
// Opens the plugin's editor in a window and plays it through the default
// audio device, with a simulated DAW transport.
//   --bpm=N          the transport's tempo (default 120)
//   --play           the transport runs from the start
//   --screenshot=f   saves the editor's picture to f (PNG) after --wait ms
//                    and quits
//   --wait=N         milliseconds before the screenshot (default 3000)
//   --keys=a,b,...   key presses posted to the plugin's window after 1.5 s
//                    (Windows: letters, digits, f1-f12, space, return, esc,
//                    tab, up, down, left, right, home, end, del, kpdot)
//   --click=x,y      a left click at the plugin window's pixel x, y
//   --state=f        saves the plugin's state to f before quitting
//   --load=f         loads a state saved with --state first
// Keys: the window's F12 toggles the simulated transport (play / stop).

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

#if JUCE_WINDOWS
#include <windows.h>

// The plugin's editor is a window of its own inside ours (made by the
// plugin): the deepest child window
static BOOL CALLBACK collectchild(HWND h, LPARAM param)
{
  ((std::vector<HWND> *)param)->push_back(h);
  return TRUE;
}

static HWND plugineditorwindow(HWND top)
{
  std::vector<HWND> children;
  EnumChildWindows(top, collectchild, (LPARAM)&children);
  HWND best = nullptr;
  for (auto h : children)
  {
    RECT r;
    GetWindowRect(h, &r);
    if ((r.right - r.left > 100) && (r.bottom - r.top > 100)) best = h;
  }
  return best;
}

// What the screen shows of a window (with the plugin's own windows in it)
static juce::Image capturewindow(HWND h)
{
  RECT r;
  GetWindowRect(h, &r);
  const int w = r.right - r.left, ht = r.bottom - r.top;
  juce::Image image(juce::Image::RGB, w, ht, true, juce::SoftwareImageType());
  HDC screen = GetDC(nullptr);
  HDC mem = CreateCompatibleDC(screen);
  HBITMAP bmp = CreateCompatibleBitmap(screen, w, ht);
  HGDIOBJ old = SelectObject(mem, bmp);
  BitBlt(mem, 0, 0, w, ht, screen, r.left, r.top, SRCCOPY);
  BITMAPINFO bi {};
  bi.bmiHeader.biSize = sizeof bi.bmiHeader;
  bi.bmiHeader.biWidth = w;
  bi.bmiHeader.biHeight = -ht;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  std::vector<juce::uint32> px((size_t)w * ht);
  GetDIBits(mem, bmp, 0, (UINT)ht, px.data(), &bi, DIB_RGB_COLORS);
  SelectObject(mem, old);
  DeleteObject(bmp);
  DeleteDC(mem);
  ReleaseDC(nullptr, screen);
  juce::Image::BitmapData data(image, juce::Image::BitmapData::writeOnly);
  for (int y = 0; y < ht; y++)
    for (int x = 0; x < w; x++)
    {
      auto p = px[(size_t)y * w + x];
      data.setPixelColour(x, y, juce::Colour((juce::uint8)(p >> 16), (juce::uint8)(p >> 8), (juce::uint8)p));
    }
  return image;
}

// A key press sent to the plugin's window as Windows sends it
static UINT vkfor(const juce::String &name)
{
  auto n = name.toLowerCase();
  if (n.length() == 1)
  {
    auto c = (char)juce::CharacterFunctions::toUpperCase(n[0]);
    if (((c >= 'A') && (c <= 'Z')) || ((c >= '0') && (c <= '9'))) return (UINT)c;
  }
  if (n.startsWith("f") && (n.length() <= 3) && (n.substring(1).getIntValue() > 0)) return VK_F1 + (UINT)(n.substring(1).getIntValue() - 1);
  if (n == "space") return VK_SPACE;
  if (n == "return") return VK_RETURN;
  if (n == "esc") return VK_ESCAPE;
  if (n == "tab") return VK_TAB;
  if (n == "up") return VK_UP;
  if (n == "down") return VK_DOWN;
  if (n == "left") return VK_LEFT;
  if (n == "right") return VK_RIGHT;
  if (n == "home") return VK_HOME;
  if (n == "end") return VK_END;
  if (n == "del") return VK_DELETE;
  if (n == "kpdot") return VK_DECIMAL;
  return 0;
}

// A left click at the plugin window's pixel x, y
static void sendclick(HWND h, int x, int y)
{
  LPARAM pos = MAKELPARAM(x, y);
  PostMessageW(h, WM_MOUSEMOVE, 0, pos);
  PostMessageW(h, WM_LBUTTONDOWN, MK_LBUTTON, pos);
  PostMessageW(h, WM_LBUTTONUP, 0, pos);
}

static void sendkey(HWND h, UINT vk)
{
  UINT scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
  PostMessageW(h, WM_KEYDOWN, vk, 1 | (scan << 16));
  PostMessageW(h, WM_KEYUP, vk, 1 | (scan << 16) | (1u << 30) | (1u << 31));
}
#endif

namespace
{
  juce::String option(const juce::StringArray &args, const juce::String &name, const juce::String &def = {})
  {
    for (auto &a : args)
      if (a.startsWith("--" + name + "=")) return a.fromFirstOccurrenceOf("=", false, false).unquoted();
    return def;
  }

  bool flag(const juce::StringArray &args, const juce::String &name)
  {
    return args.contains("--" + name);
  }
}

// The simulated DAW transport
class TestPlayHead : public juce::AudioPlayHead
{
public:
  juce::Optional<PositionInfo> getPosition() const override
  {
    PositionInfo info;
    info.setBpm(bpm.load());
    info.setIsPlaying(playing.load());
    info.setPpqPosition(ppq.load());
    info.setTimeSignature(TimeSignature { 4, 4 });
    return info;
  }

  void advance(int samples, double rate)
  {
    if (playing) ppq = ppq.load() + samples * bpm.load() / 60.0 / rate;
  }

  std::atomic<double> bpm { 120.0 };
  std::atomic<bool> playing { false };
  std::atomic<double> ppq { 0.0 };
};

// Runs the plugin with the transport
class HostProcessor : public juce::AudioIODeviceCallback
{
public:
  HostProcessor(juce::AudioPluginInstance &p, TestPlayHead &h) : plugin(p), head(h) {}

  void audioDeviceIOCallbackWithContext(const float *const *, int, float *const *out, int numOut, int samples,
    const juce::AudioIODeviceCallbackContext &) override
  {
    buffer.setSize(juce::jmax(2, plugin.getTotalNumOutputChannels()), samples, false, false, true);
    buffer.clear();
    juce::MidiBuffer midi;
    plugin.processBlock(buffer, midi);
    head.advance(samples, rate);
    for (int c = 0; c < numOut; c++)
      if (out[c]) juce::FloatVectorOperations::copy(out[c], buffer.getReadPointer(juce::jmin(c, buffer.getNumChannels() - 1)), samples);
  }

  void audioDeviceAboutToStart(juce::AudioIODevice *device) override
  {
    rate = device->getCurrentSampleRate();
    plugin.setPlayHead(&head);
    plugin.prepareToPlay(rate, device->getCurrentBufferSizeSamples());
  }

  void audioDeviceStopped() override { plugin.releaseResources(); }

private:
  juce::AudioPluginInstance &plugin;
  TestPlayHead &head;
  juce::AudioBuffer<float> buffer;
  double rate = 44100.0;
};

class MainWindow : public juce::DocumentWindow, private juce::Timer
{
public:
  MainWindow(std::unique_ptr<juce::AudioPluginInstance> p, const juce::StringArray &a)
    : DocumentWindow("GoatTracker test host", juce::Colours::black, DocumentWindow::closeButton | DocumentWindow::minimiseButton),
      plugin(std::move(p)), args(a), host(*plugin, head)
  {
    head.bpm = option(args, "bpm", "120").getDoubleValue();
    head.playing = flag(args, "play");

    auto load = option(args, "load");
    if (load.isNotEmpty())
    {
      juce::MemoryBlock mb;
      juce::File(load).loadFileAsData(mb);
      plugin->setStateInformation(mb.getData(), (int)mb.getSize());
    }

    devices.initialiseWithDefaultDevices(0, 2);
    devices.addAudioCallback(&host);

    editor = plugin->createEditorIfNeeded();
    setContentNonOwned(editor, true);
    setResizable(true, false);
    setUsingNativeTitleBar(true);
    centreWithSize(getWidth(), getHeight());
    setVisible(true);
    started = juce::Time::getMillisecondCounter();
    startTimer(100);
  }

  ~MainWindow() override
  {
    devices.removeAudioCallback(&host);
    if (editor)
    {
      setContentNonOwned(nullptr, false);
      delete editor;
    }
    plugin = nullptr;
  }

  void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }

  bool keyPressed(const juce::KeyPress &key) override
  {
    if (key.getKeyCode() == juce::KeyPress::F12Key)
    {
      head.playing = !head.playing.load();
      return true;
    }
    return false;
  }

private:
  void timerCallback() override
  {
    auto elapsed = (int)(juce::Time::getMillisecondCounter() - started);

#if JUCE_WINDOWS
    auto keys = option(args, "keys");
    if ((keys.isNotEmpty()) && (!keysSent) && (elapsed > 1500))
    {
      keysSent = true;
      if (auto h = plugineditorwindow((HWND)getWindowHandle()))
      {
        SetFocus(h);
        for (auto &k : juce::StringArray::fromTokens(keys, ",", ""))
          if (auto vk = vkfor(k)) sendkey(h, vk);
      }
    }
#endif

#if JUCE_WINDOWS
    auto click = option(args, "click");
    if ((click.isNotEmpty()) && (!clicked) && (elapsed > 1800))
    {
      clicked = true;
      if (auto h = plugineditorwindow((HWND)getWindowHandle()))
        sendclick(h, click.upToFirstOccurrenceOf(",", false, false).getIntValue(), click.fromFirstOccurrenceOf(",", false, false).getIntValue());
    }
#endif

    auto shot = option(args, "screenshot");
    if ((shot.isNotEmpty()) && (elapsed > option(args, "wait", "3000").getIntValue()))
    {
#if JUCE_WINDOWS
      toFront(true);
      auto h = plugineditorwindow((HWND)getWindowHandle());
      auto image = capturewindow(h ? h : (HWND)getWindowHandle());
#else
      auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
#endif
      juce::File file(shot);
      file.deleteFile();
      juce::FileOutputStream out(file);
      juce::PNGImageFormat().writeImageToStream(image, out);
      out.flush();
      auto state = option(args, "state");
      if (state.isNotEmpty())
      {
        juce::MemoryBlock mb;
        plugin->getStateInformation(mb);
        juce::File(state).replaceWithData(mb.getData(), mb.getSize());
      }
      stopTimer();
      juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }
  }

  std::unique_ptr<juce::AudioPluginInstance> plugin;
  juce::StringArray args;
  TestPlayHead head;
  HostProcessor host;
  juce::AudioDeviceManager devices;
  juce::AudioProcessorEditor *editor = nullptr;
  juce::uint32 started = 0;
  bool keysSent = false;
  bool clicked = false;
};

class TestHostApp : public juce::JUCEApplication
{
public:
  const juce::String getApplicationName() override { return "gttesthost"; }
  const juce::String getApplicationVersion() override { return "1.0"; }
  bool moreThanOneInstanceAllowed() override { return true; }

  void initialise(const juce::String &commandLine) override
  {
    auto args = juce::StringArray::fromTokens(commandLine, true);
    if (args.isEmpty())
    {
      juce::Logger::writeToLog("Usage: gttesthost <plugin.vst3> [options]");
      quit();
      return;
    }
    formats.addFormat(new juce::VST3PluginFormat());
    juce::OwnedArray<juce::PluginDescription> types;
    for (auto *format : formats.getFormats())
      format->findAllTypesForFile(types, args[0].unquoted());
    if (types.isEmpty())
    {
      juce::Logger::writeToLog("No plugin in " + args[0]);
      setApplicationReturnValue(1);
      quit();
      return;
    }
    juce::String error;
    auto instance = formats.createPluginInstance(*types[0], 44100.0, 512, error);
    if (!instance)
    {
      juce::Logger::writeToLog("Can't create the plugin: " + error);
      setApplicationReturnValue(1);
      quit();
      return;
    }
    window = std::make_unique<MainWindow>(std::move(instance), args);
  }

  void shutdown() override { window = nullptr; }

private:
  juce::AudioPluginFormatManager formats;
  std::unique_ptr<MainWindow> window;
};

START_JUCE_APPLICATION(TestHostApp)
