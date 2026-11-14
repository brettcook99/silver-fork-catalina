// Loads a private copy of the GoatTracker core library (gtcore.dll /
// gtcore.dylib) for one plugin instance. GoatTracker keeps its song, editor
// and SID in global variables, so every instance needs its own copy of them:
// the library is copied to a file of its own in the temporary folder and
// loaded from there (the system loads the same file only once per process).

#pragma once

#include <juce_core/juce_core.h>
#include "gtcore_api.h"

class CoreLoader
{
public:
  CoreLoader() = default;
  ~CoreLoader();

  // Copies and loads the library and starts GoatTracker. Returns false (with
  // getError()) if that failed
  bool load(const gtcore_callbacks &callbacks, int w, int h, float scale);
  bool isLoaded() const { return started; }
  // Stops GoatTracker. Returns false if its thread didn't finish (the
  // library then stays loaded)
  bool shutdown();
  const juce::String &getError() const { return error; }

  // The folder holding the plugin's binary, its resources (fonts) and the
  // core library
  static juce::File pluginBinary();
  static juce::File resourceFolder();
  static juce::File coreLibrary();
  static juce::File configFolder();

  // The library's functions (valid after load())
  int (*apiversion)(void) = nullptr;
  void (*defaultsize)(int *, int *) = nullptr;
  int (*create)(const char *, const char *, const gtcore_callbacks *, int, int, float) = nullptr;
  int (*destroy)(void) = nullptr;
  void (*prepare)(double, int) = nullptr;
  void (*process)(float *, float *, int, const gtcore_transport *, const gtcore_midi *, int) = nullptr;
  void (*input)(const gtcore_inputevent *) = nullptr;
  void (*editorvisible)(int) = nullptr;
  unsigned (*getframe)(unsigned, void *, int, int, int, int *, int *) = nullptr;
  int (*getcursor)(void) = nullptr;
  int (*getstate)(void **, int *) = nullptr;
  void (*freedata)(void *) = nullptr;
  int (*setstate)(const void *, int) = nullptr;

private:
  void unload();
  static void removeStaleCopies(const juce::File &folder);

  void *handle = nullptr;
  juce::File copy;
  std::atomic<bool> started { false };
  juce::String error;

  JUCE_DECLARE_NON_COPYABLE(CoreLoader)
};
