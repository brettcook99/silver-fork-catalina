#include "CoreLoader.h"

#if JUCE_WINDOWS
#include <windows.h>
#else
#include <dlfcn.h>
#endif

static int anchor = 0;

juce::File CoreLoader::pluginBinary()
{
#if JUCE_WINDOWS
  HMODULE module = nullptr;
  wchar_t path[MAX_PATH * 4] = {};
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
    (LPCWSTR)&anchor, &module);
  GetModuleFileNameW(module, path, (DWORD)(sizeof path / sizeof path[0]));
  return juce::File(juce::String(path));
#else
  Dl_info info;
  if (dladdr((const void *)&anchor, &info) && info.dli_fname) return juce::File(juce::String::fromUTF8(info.dli_fname));
  return juce::File::getSpecialLocation(juce::File::currentExecutableFile);
#endif
}

// Windows: <name>.vst3/Contents/x86_64-win/<binary>, resources in
// Contents/Resources. macOS: <name>.vst3/Contents/MacOS/<binary>, the library
// in Contents/Frameworks, resources in Contents/Resources
juce::File CoreLoader::resourceFolder()
{
  return pluginBinary().getParentDirectory().getSiblingFile("Resources");
}

juce::File CoreLoader::coreLibrary()
{
#if JUCE_WINDOWS
  return pluginBinary().getSiblingFile("gtcore.dll");
#else
  return pluginBinary().getParentDirectory().getSiblingFile("Frameworks").getChildFile("gtcore.dylib");
#endif
}

juce::File CoreLoader::configFolder()
{
#if JUCE_WINDOWS
  // %APPDATA%\GoatTracker Ultimate
  auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("GoatTracker Ultimate");
#else
  // The standalone program's settings folder, so that the plugin has the
  // same settings (keys, colors, folders)
  auto dir = juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile(".goattrk");
#endif
  dir.createDirectory();
  return dir;
}

void CoreLoader::removeStaleCopies(const juce::File &folder)
{
  // Copies left behind by instances that have ended (in use ones can't be
  // deleted on Windows; on macOS deleting a loaded library is harmless, but
  // only old ones are removed so that another running host keeps its own)
  for (auto &f : folder.findChildFiles(juce::File::findFiles, false, "gtcore-*"))
  {
    if ((juce::Time::getCurrentTime() - f.getLastModificationTime()).inHours() >= 1)
      f.deleteFile();
  }
}

bool CoreLoader::load(const gtcore_callbacks &callbacks, int w, int h, float scale)
{
  static std::atomic<int> counter { 0 };

  if (started) return true;
  error.clear();

  auto source = coreLibrary();
  if (!source.existsAsFile())
  {
    error = "The GoatTracker core library is missing: " + source.getFullPathName();
    return false;
  }

  auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("GoatTrackerUltimate");
  folder.createDirectory();
  removeStaleCopies(folder);
  copy = folder.getChildFile("gtcore-" + juce::String(juce::Time::currentTimeMillis()) + "-" + juce::String(counter++) + "-" +
    juce::String::toHexString(juce::Random::getSystemRandom().nextInt()) + source.getFileExtension());
  if (!source.copyFileTo(copy))
  {
    error = "Can't copy the core library to " + copy.getFullPathName();
    return false;
  }

#if JUCE_WINDOWS
  handle = (void *)LoadLibraryW(copy.getFullPathName().toWideCharPointer());
#else
  handle = dlopen(copy.getFullPathName().toRawUTF8(), RTLD_NOW | RTLD_LOCAL);
#endif
  if (!handle)
  {
    error = "Can't load the core library " + copy.getFullPathName();
    copy.deleteFile();
    return false;
  }
#if !JUCE_WINDOWS
  // macOS keeps the loaded file: the name can go now
  copy.deleteFile();
#endif

  auto sym = [this](const char *name) -> void *
  {
#if JUCE_WINDOWS
    return (void *)GetProcAddress((HMODULE)handle, name);
#else
    return dlsym(handle, name);
#endif
  };
  bool ok = true;
#define GTSYM(field, name) do { *(void **)&field = sym(name); if (!field) ok = false; } while (0)
  GTSYM(apiversion, "gtcore_apiversion");
  GTSYM(defaultsize, "gtcore_defaultsize");
  GTSYM(create, "gtcore_create");
  GTSYM(destroy, "gtcore_destroy");
  GTSYM(prepare, "gtcore_prepare");
  GTSYM(process, "gtcore_process");
  GTSYM(input, "gtcore_input");
  GTSYM(editorvisible, "gtcore_editorvisible");
  GTSYM(getframe, "gtcore_getframe");
  GTSYM(getcursor, "gtcore_getcursor");
  GTSYM(getstate, "gtcore_getstate");
  GTSYM(freedata, "gtcore_free");
  GTSYM(setstate, "gtcore_setstate");
#undef GTSYM
  if ((!ok) || (apiversion() != GTCORE_API_VERSION))
  {
    error = "The core library doesn't match this plugin";
    unload();
    return false;
  }

  auto resources = resourceFolder().getFullPathName() + juce::File::getSeparatorString();
  auto config = configFolder().getFullPathName() + juce::File::getSeparatorString();
  if (!create(resources.toRawUTF8(), config.toRawUTF8(), &callbacks, w, h, scale))
  {
    error = "GoatTracker didn't start (see gtcore.log in " + configFolder().getFullPathName() + ")";
    // Its thread may still be running: the library stays loaded
    if (destroy()) unload();
    else handle = nullptr;
    return false;
  }
  started = true;
  return true;
}

void CoreLoader::unload()
{
  if (handle)
  {
#if JUCE_WINDOWS
    FreeLibrary((HMODULE)handle);
#else
    dlclose(handle);
#endif
    handle = nullptr;
  }
  copy.deleteFile();
}

bool CoreLoader::shutdown()
{
  if (started)
  {
    started = false;
    // Unloaded only when GoatTracker's thread has finished; otherwise the
    // library stays loaded (its file is removed later)
    if (destroy())
    {
      unload();
      return true;
    }
    handle = nullptr;
    return false;
  }
  unload();
  return true;
}

CoreLoader::~CoreLoader()
{
  shutdown();
}
