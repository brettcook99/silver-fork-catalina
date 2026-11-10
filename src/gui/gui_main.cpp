// GoatTracker user interface: window, event loop, fonts, layout

#include "gui_internal.h"
#include "../imgui/imgui_impl_sdl2.h"
#include "../imgui/imgui_impl_sdlrenderer2.h"

#include <stdarg.h>

static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static bool initted = false;
static char configdir[MAX_PATHNAME] = "";
static char inipath[MAX_PATHNAME] = "";
static char recentlistpath[MAX_PATHNAME] = "";
static int fullscreen = 0;
static Uint32 lastframeticks = 0;

namespace gui
{
  bool showtracks = true;
  bool showpattern = true;
  bool showlibrary = true;
  bool showinspector = true;
  bool showtables = true;
  bool showsong = true;
  bool showhelp = false;
  bool resetlayout = false;

  char statusmessage[256] = "";
  double statusmessagetime = -100.0;

  int tsmousebuttons = 0;
  int tsmousewheel = 0;
}

using namespace gui;

/*
 * Configuration directory (~/.goattrk), recent files
 */

#define MAX_RECENT 10
static char recentfiles[MAX_RECENT][MAX_PATHNAME];
static int numrecent = 0;

static void initconfigdir(void)
{
#ifdef __WIN32__
  char *base = SDL_GetBasePath();
  if (base)
  {
    snprintf(configdir, MAX_PATHNAME, "%s", base);
    SDL_free(base);
  }
#else
  const char *home = getenv("HOME");
  if (home)
  {
    snprintf(configdir, MAX_PATHNAME, "%s/.goattrk/", home);
    mkdir(configdir, S_IRUSR | S_IWUSR | S_IXUSR);
  }
#endif
  snprintf(inipath, MAX_PATHNAME, "%simgui.ini", configdir);
  snprintf(recentlistpath, MAX_PATHNAME, "%srecent.txt", configdir);
}

static void loadrecent(void)
{
  FILE *handle = fopen(recentlistpath, "rt");
  char line[MAX_PATHNAME];

  numrecent = 0;
  if (!handle) return;
  while ((numrecent < MAX_RECENT) && (fgets(line, MAX_PATHNAME, handle)))
  {
    size_t len = strlen(line);
    while ((len) && ((line[len - 1] == '\n') || (line[len - 1] == '\r'))) line[--len] = 0;
    if (len) strcpy(recentfiles[numrecent++], line);
  }
  fclose(handle);
}

static void saverecent(void)
{
  FILE *handle = fopen(recentlistpath, "wt");
  int c;

  if (!handle) return;
  for (c = 0; c < numrecent; c++) fprintf(handle, "%s\n", recentfiles[c]);
  fclose(handle);
}

extern "C" void gui_addrecentfile(const char *path)
{
  int c;

  if ((!path) || (!path[0]) || (strlen(path) >= MAX_PATHNAME)) return;
  for (c = 0; c < numrecent; c++)
  {
    if (!strcmp(recentfiles[c], path))
    {
      // Move to the top
      for (; c > 0; c--) strcpy(recentfiles[c], recentfiles[c - 1]);
      strcpy(recentfiles[0], path);
      saverecent();
      return;
    }
  }
  if (numrecent < MAX_RECENT) numrecent++;
  for (c = numrecent - 1; c > 0; c--) strcpy(recentfiles[c], recentfiles[c - 1]);
  strcpy(recentfiles[0], path);
  saverecent();
}

int gui::recentcount(void)
{
  return numrecent;
}

const char *gui::recentpath(int index)
{
  if ((index < 0) || (index >= numrecent)) return NULL;
  return recentfiles[index];
}

extern "C" int gui_recentfilecount(void)
{
  return numrecent;
}

extern "C" const char *gui_recentfile(int index)
{
  return gui::recentpath(index);
}

/*
 * Has the song been modified since it was loaded / saved?
 */

static unsigned long long savedhash = 0;

static unsigned long long hashbytes(unsigned long long h, const void *data, size_t size)
{
  const unsigned char *p = (const unsigned char *)data;
  while (size--)
  {
    h ^= *p++;
    h *= 1099511628211ULL;
  }
  return h;
}

static unsigned long long songhash(void)
{
  unsigned long long h = 14695981039346656037ULL;
  h = hashbytes(h, songorder, sizeof songorder);
  h = hashbytes(h, pattern, sizeof pattern);
  h = hashbytes(h, instr, sizeof instr);
  h = hashbytes(h, ltable, sizeof ltable);
  h = hashbytes(h, rtable, sizeof rtable);
  h = hashbytes(h, songname, sizeof songname);
  h = hashbytes(h, authorname, sizeof authorname);
  h = hashbytes(h, copyrightname, sizeof copyrightname);
  return h;
}

extern "C" void gui_marksongsaved(void)
{
  savedhash = songhash();
}

extern "C" int gui_songmodified(void)
{
  return songhash() != savedhash;
}

/*
 * Status bar message
 */

extern "C" void gui_message(const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(statusmessage, sizeof statusmessage, fmt, ap);
  va_end(ap);
  statusmessagetime = initted ? ImGui::GetTime() : 0.0;
}

/*
 * Fonts
 */

static bool findfile(const char *name, char *out, int outsize)
{
  char *base = SDL_GetBasePath();
  const char *prefixes[] = { "fonts/", "../fonts/", "../../fonts/", "../Resources/fonts/", NULL };
  int c;

  for (c = 0; prefixes[c]; c++)
  {
    FILE *handle;
    snprintf(out, outsize, "%s%s%s", base ? base : "", prefixes[c], name);
    handle = fopen(out, "rb");
    if (handle)
    {
      fclose(handle);
      if (base) SDL_free(base);
      return true;
    }
  }
  if (base) SDL_free(base);
  return false;
}

static ImFont *loadfont(const char *name, float size)
{
  char path[MAX_PATHNAME * 2];
  ImFontConfig cfg;
  ImGuiIO &io = ImGui::GetIO();

  if (!findfile(name, path, sizeof path)) return NULL;
  cfg.OversampleH = 2;
  cfg.OversampleV = 1;
  return io.Fonts->AddFontFromFileTTF(path, size, &cfg);
}

static void loadfonts(void)
{
  ImGuiIO &io = ImGui::GetIO();

  fontui = loadfont("Inter-Regular.ttf", uisize);
  if (!fontui)
  {
    // Fonts missing: use the built-in font for everything
    fontui = io.Fonts->AddFontDefault();
    fontbold = fontui;
    fontmono = fontui;
    return;
  }
  fontbold = loadfont("Inter-SemiBold.ttf", uisize);
  if (!fontbold) fontbold = fontui;
  fontmono = loadfont("JetBrainsMono-Regular.ttf", monosize);
  if (!fontmono) fontmono = io.Fonts->AddFontDefault();
}

/*
 * Window
 */

static void loadicon(void)
{
#ifndef __APPLE__
  // On macOS the app bundle's icon is used
  int handle = io_open((char *)"goattrk2.bmp");
  if (handle != -1)
  {
    int size = io_lseek(handle, 0, SEEK_END);
    char *buffer;

    io_lseek(handle, 0, SEEK_SET);
    buffer = (char *)malloc(size);
    if (buffer)
    {
      SDL_RWops *rw;
      SDL_Surface *icon;

      io_read(handle, buffer, size);
      rw = SDL_RWFromMem(buffer, size);
      icon = SDL_LoadBMP_RW(rw, 1);
      if (icon)
      {
        SDL_SetWindowIcon(window, icon);
        SDL_FreeSurface(icon);
      }
      free(buffer);
    }
    io_close(handle);
  }
#endif
}

extern "C" int gui_init(void)
{
  int w = (int)windowedxsize;
  int h = (int)windowedysize;
  SDL_Rect usable;
  Uint32 flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;

  if (initted) return 1;

  initconfigdir();
  loadrecent();

  SDL_SetHint(SDL_HINT_MAC_CTRL_CLICK_EMULATE_RIGHT_CLICK, "1");
  SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
#ifdef SDL_HINT_RENDER_SCALE_QUALITY
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
#endif

  // Default window size: fits the desktop
  if (SDL_GetDisplayUsableBounds(0, &usable) != 0)
  {
    usable.w = 1440;
    usable.h = 900;
  }
  if ((w < 640) || (h < 400))
  {
    w = usable.w * 9 / 10;
    h = usable.h * 9 / 10;
    if (w > 1680) w = 1680;
    if (h > 1050) h = 1050;
  }
  if (w > usable.w) w = usable.w;
  if (h > usable.h) h = usable.h;

  fullscreen = win_fullscreen ? 1 : 0;
  if (fullscreen) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;

  window = SDL_CreateWindow("GoatTracker", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, flags);
  if (!window) return 0;
  SDL_SetWindowMinimumSize(window, 960, 600);
  loadicon();

  renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
  if (!renderer) return 0;

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  io.ConfigWindowsMoveFromTitleBarOnly = true;
  io.ConfigDockingWithShift = false;
  io.IniFilename = inipath[0] ? inipath : NULL;
  io.ConfigErrorRecoveryEnableAssert = false;
  io.ConfigErrorRecoveryEnableTooltip = false;

  loadfonts();
  applytheme();

  ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
  ImGui_ImplSDLRenderer2_Init(renderer);

  // Files dropped on the window (and opened from the Finder on macOS)
  SDL_EventState(SDL_DROPFILE, SDL_ENABLE);
  SDL_StartTextInput();

  initted = true;

  // macOS menu bar (does nothing elsewhere)
  macmenu_init();
  return 1;
}

extern "C" void gui_shutdown(void)
{
  if (!initted) return;
  initted = false;
  ImGui_ImplSDLRenderer2_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();
  if (renderer) SDL_DestroyRenderer(renderer);
  if (window) SDL_DestroyWindow(window);
  renderer = NULL;
  window = NULL;
}

extern "C" void gui_togglefullscreen(void)
{
  if (!window) return;
  fullscreen ^= 1;
  win_fullscreen = fullscreen;
  SDL_SetWindowFullscreen(window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}

static const unsigned textsizes[] = { 70, 80, 90, 100, 110, 120, 135, 150, 175, 200 };
#define NUM_TEXTSIZES ((int)(sizeof textsizes / sizeof textsizes[0]))

extern "C" void gui_changetextsize(int dir)
{
  int c;
  int current = 3;

  if (!dir)
  {
    textsizepercent = 100;
    return;
  }
  for (c = 0; c < NUM_TEXTSIZES; c++)
  {
    if (textsizes[c] <= textsizepercent) current = c;
  }
  current += dir;
  if (current < 0) current = 0;
  if (current >= NUM_TEXTSIZES) current = NUM_TEXTSIZES - 1;
  textsizepercent = textsizes[current];
}

extern "C" int gui_command(int cmd)
{
  switch (cmd)
  {
    case GTCMD_HELP: showhelp = !showhelp; return 1;
    case GTCMD_ZOOMIN: gui_changetextsize(1); return 1;
    case GTCMD_ZOOMOUT: gui_changetextsize(-1); return 1;
    case GTCMD_ZOOMRESET: gui_changetextsize(0); return 1;
    case GTCMD_FULLSCREEN: gui_togglefullscreen(); return 1;
    case GTCMD_RESETLAYOUT:
      resetlayout = true;
      showtracks = showpattern = showlibrary = showinspector = showtables = showsong = true;
      return 1;
    case GTCMD_TOGGLELIBRARY: showlibrary = !showlibrary; return 1;
    case GTCMD_TOGGLEINSPECTOR: showinspector = !showinspector; return 1;
    case GTCMD_TOGGLETRACKS: showtracks = !showtracks; return 1;
    case GTCMD_TOGGLETABLES: showtables = !showtables; return 1;
    case GTCMD_TOGGLESONG: showsong = !showsong; return 1;
  }
  return 0;
}

/*
 * Events
 */

static int modbits(Uint16 mod)
{
  int mods = 0;
  if (mod & KMOD_SHIFT) mods |= KMOD_GT_SHIFT;
  if (mod & KMOD_CTRL) mods |= KMOD_GT_CTRL;
  if (mod & KMOD_ALT) mods |= KMOD_GT_ALT;
  if (mod & KMOD_GUI) mods |= KMOD_GT_META;
  return mods;
}

static bool editorsblocked(void)
{
  ImGuiIO &io = ImGui::GetIO();
  if (io.WantTextInput) return true;
  if (ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup)) return true;
  return false;
}

static void handlekeydown(const SDL_KeyboardEvent &k, int textscreen)
{
  SDL_Keycode sym = k.keysym.sym;
  Uint16 mod = k.keysym.mod;
  int ch = 0;

  switch (sym)
  {
    // Modifier keys alone do nothing
    case SDLK_LSHIFT: case SDLK_RSHIFT: case SDLK_LCTRL: case SDLK_RCTRL:
    case SDLK_LALT: case SDLK_RALT: case SDLK_LGUI: case SDLK_RGUI:
    case SDLK_CAPSLOCK: case SDLK_NUMLOCKCLEAR: case SDLK_MODE:
    return;
  }

  // ALT/OPTION+ENTER: fullscreen
  if (((sym == SDLK_RETURN) || (sym == SDLK_KP_ENTER)) && (mod & KMOD_ALT))
  {
    if (!k.repeat) gui_togglefullscreen();
    return;
  }

  // CTRL/CMD + = - 0: text size
  if ((mod & (KMOD_CTRL | KMOD_GUI)) && (!(mod & KMOD_ALT)))
  {
    int cmd = 0;
    switch (sym)
    {
      case SDLK_EQUALS: case SDLK_PLUS: case SDLK_KP_PLUS: cmd = GTCMD_ZOOMIN; break;
      case SDLK_MINUS: case SDLK_KP_MINUS: cmd = GTCMD_ZOOMOUT; break;
      case SDLK_0: case SDLK_KP_0: cmd = GTCMD_ZOOMRESET; break;
    }
    if (cmd)
    {
      cmd_post(cmd);
      return;
    }
  }

#ifdef __APPLE__
  // CMD shortcuts of the File and View menus
  if ((mod & KMOD_GUI) && (!(mod & KMOD_ALT)))
  {
    bool shift = (mod & KMOD_SHIFT) != 0;
    bool ctrl = (mod & KMOD_CTRL) != 0;
    int cmd = 0;

    switch (sym)
    {
      case SDLK_n: cmd = GTCMD_NEW; break;
      case SDLK_o: cmd = shift ? GTCMD_MERGE : GTCMD_OPEN; break;
      case SDLK_s: cmd = shift ? GTCMD_SAVEAS : GTCMD_SAVE; break;
      case SDLK_i: cmd = shift ? GTCMD_IMPORTSID : GTCMD_IMPORTMIDI; break;
      case SDLK_e: cmd = GTCMD_EXPORT; break;
      case SDLK_f: if (ctrl) cmd = GTCMD_FULLSCREEN; break;
      case SDLK_SLASH: if (shift) cmd = GTCMD_HELP; break;
    }
    if (cmd)
    {
      if ((!k.repeat) && (!textscreen)) cmd_post(cmd);
      return;
    }
  }
#endif

  // Typing into a text field or answering a dialog
  if ((!textscreen) && (editorsblocked())) return;

  switch (sym)
  {
    case SDLK_RETURN: case SDLK_KP_ENTER: ch = 13; break;
    case SDLK_BACKSPACE: ch = 8; break;
    case SDLK_TAB: ch = 9; break;
    case SDLK_ESCAPE: ch = 27; break;
    case SDLK_DELETE: ch = 127; break;
  }
  input_pushkey((int)sym, ch, modbits(mod));
}

// First character of an UTF-8 string, if it fits in 8 bits
static int utf8first(const char *text)
{
  const unsigned char *t = (const unsigned char *)text;

  if (t[0] < 0x80) return t[0];
  if (((t[0] & 0xe0) == 0xc0) && ((t[1] & 0xc0) == 0x80))
  {
    int c = ((t[0] & 0x1f) << 6) | (t[1] & 0x3f);
    if (c < 256) return c;
  }
  return 0;
}

static void pollevents(int textscreen)
{
  SDL_Event e;

  tsmousewheel = 0;
  while (SDL_PollEvent(&e))
  {
    ImGui_ImplSDL2_ProcessEvent(&e);

    switch (e.type)
    {
      case SDL_QUIT:
      win_quitted = 1;
      break;

      case SDL_WINDOWEVENT:
      if (e.window.windowID != SDL_GetWindowID(window)) break;
      if (e.window.event == SDL_WINDOWEVENT_CLOSE) win_quitted = 1;
      if ((e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) && (!fullscreen))
      {
        if (!(SDL_GetWindowFlags(window) & (SDL_WINDOW_MAXIMIZED | SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP)))
        {
          windowedxsize = e.window.data1;
          windowedysize = e.window.data2;
        }
      }
      break;

      case SDL_DROPFILE:
      if (e.drop.file)
      {
        cmd_postfile(GTCMD_OPENFILE, e.drop.file);
        SDL_free(e.drop.file);
      }
      break;

      case SDL_KEYDOWN:
      handlekeydown(e.key, textscreen);
      break;

      case SDL_TEXTINPUT:
      if ((textscreen) || (!editorsblocked()))
      {
        int ch = utf8first(e.text.text);
        if (ch >= 32) input_pushtext(ch, modbits(SDL_GetModState()));
      }
      break;

      case SDL_MOUSEWHEEL:
      if (e.wheel.y > 0) tsmousewheel--;
      if (e.wheel.y < 0) tsmousewheel++;
      break;
    }
  }
}

/*
 * Layout
 */

static void buildlayout(ImGuiID dockid, ImVec2 size)
{
  ImGuiID center, left, right, top, lefttop, leftbottom, righttop, rightbottom;

  ImGui::DockBuilderRemoveNode(dockid);
  ImGui::DockBuilderAddNode(dockid, ImGuiDockNodeFlags_DockSpace);
  ImGui::DockBuilderSetNodeSize(dockid, size);

  center = dockid;
  left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.21f, NULL, &center);
  right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.30f, NULL, &center);
  top = ImGui::DockBuilderSplitNode(center, ImGuiDir_Up, 0.30f, NULL, &center);
  lefttop = ImGui::DockBuilderSplitNode(left, ImGuiDir_Up, 0.36f, NULL, &leftbottom);
  righttop = ImGui::DockBuilderSplitNode(right, ImGuiDir_Up, 0.74f, NULL, &rightbottom);

  ImGui::DockBuilderDockWindow(WIN_LIBRARY, lefttop);
  ImGui::DockBuilderDockWindow(WIN_INSPECTOR, leftbottom);
  ImGui::DockBuilderDockWindow(WIN_TRACKS, top);
  ImGui::DockBuilderDockWindow(WIN_PATTERN, center);
  ImGui::DockBuilderDockWindow(WIN_TABLES, righttop);
  ImGui::DockBuilderDockWindow(WIN_SONG, rightbottom);
  ImGui::DockBuilderFinish(dockid);
}

static void drawall(int textscreen)
{
  ImGuiViewport *vp = ImGui::GetMainViewport();
  ImGuiStyle &style = ImGui::GetStyle();
  float controlh;
  float statush;
  ImVec2 pos, size;
  ImGuiWindowFlags barflags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings |
    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus;

#ifndef __APPLE__
  // macOS has its own menu bar
  drawmenubar();
#endif

  controlh = ImGui::GetFontSize() * 4.4f;
  statush = ImGui::GetFontSize() + style.FramePadding.y * 2.0f + 4.0f;

  // Control bar (transport, display, settings)
  ImGui::SetNextWindowPos(vp->WorkPos);
  ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, controlh));
  ImGui::PushStyleColor(ImGuiCol_WindowBg, theme.bgraised);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 6.0f));
  if (ImGui::Begin("##controlbar", NULL, barflags))
    drawcontrolbar(controlh);
  ImGui::End();

  // Status bar
  ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y + vp->WorkSize.y - statush));
  ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, statush));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 2.0f));
  if (ImGui::Begin("##statusbar", NULL, barflags))
    drawstatusbar(statush);
  ImGui::End();
  ImGui::PopStyleVar(4);
  ImGui::PopStyleColor();

  // Dock space for the panels between them
  pos = ImVec2(vp->WorkPos.x, vp->WorkPos.y + controlh);
  size = ImVec2(vp->WorkSize.x, vp->WorkSize.y - controlh - statush);
  ImGui::SetNextWindowPos(pos);
  ImGui::SetNextWindowSize(size);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, theme.bgdeep);
  if (ImGui::Begin("##dockhost", NULL, barflags | ImGuiWindowFlags_NoNavFocus))
  {
    ImGuiID dockid = ImGui::GetID("MainDockSpace");
    if ((resetlayout) || (!ImGui::DockBuilderGetNode(dockid)))
    {
      buildlayout(dockid, size);
      resetlayout = false;
    }
    ImGui::DockSpace(dockid, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_NoWindowMenuButton);
  }
  ImGui::End();
  ImGui::PopStyleColor();
  ImGui::PopStyleVar(3);

  if (showtracks) drawtracks(&showtracks);
  if (showlibrary) drawlibrary(&showlibrary);
  if (showinspector) drawinspector(&showinspector);
  if (showtables) drawtables(&showtables);
  if (showsong) drawsong(&showsong);
  // The pattern editor is always there
  drawpatterneditor(NULL);
  if (showhelp) drawhelp(&showhelp);

  drawdialogs();

  if (textscreen) drawtextscreen();
}

// Testing aid: with the environment variable GT_SCREENSHOT=file.bmp the
// window is saved to that file after GT_SCREENSHOT_FRAMES frames (default
// 30) and the program quits. GT_SCREENSHOT_CMDS can list commands to run
// first (comma-separated GTCMD numbers), e.g. to open a dialog
static void testscreenshot(void)
{
  static int frames = 0;
  const char *path = getenv("GT_SCREENSHOT");
  const char *wait = getenv("GT_SCREENSHOT_FRAMES");
  int target = wait ? atoi(wait) : 30;
  int w, h;
  SDL_Surface *surface;

  if ((!path) || (!path[0])) return;
  frames++;
  if (frames == 2)
  {
    const char *cmds = getenv("GT_SCREENSHOT_CMDS");
    while ((cmds) && (*cmds))
    {
      int cmd = atoi(cmds);
      if (cmd) cmd_post(cmd);
      while ((*cmds) && (*cmds != ',')) cmds++;
      if (*cmds == ',') cmds++;
    }
  }
  if (frames != target) return;
  if (SDL_GetRendererOutputSize(renderer, &w, &h) != 0) return;
  surface = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
  if (!surface) return;
  if (SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_ARGB8888, surface->pixels, surface->pitch) == 0)
    SDL_SaveBMP(surface, path);
  SDL_FreeSurface(surface);
  exitprogram = 1;
}

extern "C" void gui_frame(int textscreen)
{
  Uint32 now;

  if (!initted) return;
  ImGuiIO &io = ImGui::GetIO();

  pollevents(textscreen);

  ImGui::GetStyle().FontScaleMain = textsizepercent / 100.0f;

  ImGui_ImplSDLRenderer2_NewFrame();
  ImGui_ImplSDL2_NewFrame();
  ImGui::NewFrame();

  updatemeters(io.DeltaTime);
  drawall(textscreen);

  ImGui::Render();
  {
    ImU32 bg = theme.bgdeep;
    SDL_SetRenderDrawColor(renderer, bg & 0xff, (bg >> 8) & 0xff, (bg >> 16) & 0xff, 255);
  }
  SDL_RenderClear(renderer);
  ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
  testscreenshot();
  SDL_RenderPresent(renderer);

  // Without vsync (or when minimized) don't run faster than about 100 fps
  now = SDL_GetTicks();
  if (SDL_GetWindowFlags(window) & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN))
    SDL_Delay(20);
  else if ((lastframeticks) && (now - lastframeticks < 10))
    SDL_Delay(10 - (now - lastframeticks));
  lastframeticks = SDL_GetTicks();
}

extern "C" int gui_textscreenmousebuttons(void)
{
  return tsmousebuttons;
}

extern "C" int gui_textscreenmousewheel(void)
{
  return tsmousewheel;
}
