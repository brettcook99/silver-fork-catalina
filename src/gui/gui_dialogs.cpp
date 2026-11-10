// GoatTracker GUI: dialogs and the classic text screen overlay

#include "gui_internal.h"

using namespace gui;

static bool wantalert = false;
static char alerttitle[128] = "";
static char alerttext[512] = "";

static bool wantquit = false;
static bool wantclear = false;
static bool wanthr = false;
static bool wantsave = false;

// "Save changes?" bookkeeping
static int pendingcmd = 0;
static char pendingpath[MAX_PATHNAME] = "";
static bool confirmedonce = false;
static bool waitingforsave = false;

extern "C" void gui_alert(const char *title, const char *text)
{
  snprintf(alerttitle, sizeof alerttitle, "%s", title ? title : "");
  snprintf(alerttext, sizeof alerttext, "%s", text ? text : "");
  wantalert = true;
}

extern "C" void gui_askquit(void)
{
  if (gui_songmodified())
    cmd_post(GTCMD_QUIT);       // Asks to save the changes
  else
    wantquit = true;
}

extern "C" void gui_askclear(void)
{
  wantclear = true;
}

extern "C" void gui_edithradsr(void)
{
  wanthr = true;
}

extern "C" int gui_asksavechanges(int cmd, const char *path)
{
  if (confirmedonce)
  {
    confirmedonce = false;
    return 0;
  }
  if (!gui_songmodified()) return 0;
  pendingcmd = cmd;
  snprintf(pendingpath, sizeof pendingpath, "%s", path ? path : "");
  wantsave = true;
  return 1;
}

extern "C" void gui_savefinished(int ok)
{
  if (!waitingforsave) return;
  waitingforsave = false;
  if (ok)
  {
    confirmedonce = true;
    cmd_postfile(pendingcmd, pendingpath);
  }
}

static bool wantexport = false;

extern "C" void gui_openexport(void)
{
  wantexport = true;
}

/*
 * Dialog helpers
 */

static void centernext(void)
{
  ImGuiViewport *vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
}

static bool beginmodal(const char *id, float width)
{
  centernext();
  ImGui::SetNextWindowSize(ImVec2(width, 0.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0f, 18.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
  bool open = ImGui::BeginPopupModal(id, NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
  ImGui::PopStyleVar(2);
  return open;
}

static void title(const char *text)
{
  ImGui::PushFont(fontbold, ImGui::GetStyle().FontSizeBase * 1.15f);
  ImGui::TextWrapped("%s", text);
  ImGui::PopFont();
  ImGui::Spacing();
}

static void body(const char *text)
{
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(theme.textdim));
  ImGui::TextWrapped("%s", text);
  ImGui::PopStyleColor();
}

// Buttons on the right; the first one is the default (accent, Return).
// Returns the index of the button pressed, -1 if none. ESC = cancelindex
static int buttons(const char *const *labels, int count, int cancelindex)
{
  ImGuiStyle &style = ImGui::GetStyle();
  float total = 0.0f;
  float bw[4];
  int c;
  int result = -1;

  ImGui::Spacing();
  ImGui::Spacing();
  for (c = 0; c < count; c++)
  {
    bw[c] = ImGui::CalcTextSize(labels[c]).x + 28.0f;
    if (bw[c] < 84.0f) bw[c] = 84.0f;
    total += bw[c] + (c ? style.ItemSpacing.x : 0.0f);
  }
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - total);
  // Right to left reading order: default button last (rightmost), as on macOS
  for (c = count - 1; c >= 0; c--)
  {
    bool isdefault = (c == 0);
    if (c != count - 1) ImGui::SameLine();
    if (isdefault)
    {
      ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(theme.accent));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::ColorConvertU32ToFloat4(mix(theme.accent, IM_COL32(255, 255, 255, 255), 0.15f)));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::ColorConvertU32ToFloat4(mix(theme.accent, IM_COL32(0, 0, 0, 255), 0.15f)));
    }
    if (ImGui::Button(labels[c], ImVec2(bw[c], 0.0f))) result = c;
    if (isdefault) ImGui::PopStyleColor(3);
  }
  if ((ImGui::IsKeyPressed(ImGuiKey_Enter, false)) || (ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))) result = 0;
  if ((cancelindex >= 0) && (ImGui::IsKeyPressed(ImGuiKey_Escape, false))) result = cancelindex;
  return result;
}

/*
 * The dialogs
 */

static void alertdialog(void)
{
  if (wantalert)
  {
    ImGui::OpenPopup("##alert");
    wantalert = false;
  }
  if (beginmodal("##alert", ImGui::GetFontSize() * 26.0f))
  {
    const char *labels[] = { "OK" };
    title(alerttitle);
    if (alerttext[0]) body(alerttext);
    if (buttons(labels, 1, 0) >= 0) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
}

static void quitdialog(void)
{
  if (wantquit)
  {
    ImGui::OpenPopup("##quit");
    wantquit = false;
  }
  if (beginmodal("##quit", ImGui::GetFontSize() * 24.0f))
  {
    const char *labels[] = { "Quit", "Cancel" };
    int r;
    title("Quit GoatTracker?");
    body("All changes are saved.");
    r = buttons(labels, 2, 1);
    if (r == 0)
    {
      exitprogram = 1;
      ImGui::CloseCurrentPopup();
    }
    if (r == 1) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
}

static void savedialog(void)
{
  if (wantsave)
  {
    ImGui::OpenPopup("##savechanges");
    wantsave = false;
  }
  if (beginmodal("##savechanges", ImGui::GetFontSize() * 28.0f))
  {
    const char *labels[] = { "Save", "Cancel", "Don't Save" };
    char text[MAX_PATHNAME + 80];
    const char *name = loadedsongfilename[0] ? gt_basename(loadedsongfilename) : "Untitled";
    int r;

    snprintf(text, sizeof text, "Do you want to save the changes made to \"%s\"?", name);
    title(text);
    body("Your changes will be lost if you don't save them.");
    r = buttons(labels, 3, 1);
    if (r == 0)
    {
      waitingforsave = true;
      cmd_post(GTCMD_SAVE);
      ImGui::CloseCurrentPopup();
    }
    if (r == 1) ImGui::CloseCurrentPopup();
    if (r == 2)
    {
      confirmedonce = true;
      cmd_postfile(pendingcmd, pendingpath);
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}

static void cleardialog(void)
{
  static bool cs = false, cp = false, ci = false, ct = false, cn = false;
  static int plen = 64;

  if (wantclear)
  {
    ImGui::OpenPopup("##clear");
    wantclear = false;
    cs = cp = ci = ct = cn = false;
    plen = defaultpatternlength;
  }
  if (beginmodal("##clear", ImGui::GetFontSize() * 28.0f))
  {
    const char *labels[] = { "Clear", "Cancel", "Optimize Everything" };
    int r;

    title("Clear or optimize the song");
    body("Optimizing removes unused patterns, instruments and table entries. Or choose what to clear:");
    ImGui::Spacing();
    ImGui::Checkbox("Orderlists", &cs);
    ImGui::Checkbox("Patterns", &cp);
    if (cp)
    {
      ImGui::SameLine();
      ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.0f);
      ImGui::SliderInt("##plen", &plen, 1, MAX_PATTROWS, "new length %d rows");
    }
    ImGui::Checkbox("Instruments", &ci);
    ImGui::Checkbox("Tables", &ct);
    ImGui::Checkbox("Song name, author & copyright", &cn);
    r = buttons(labels, 3, 1);
    if (r == 0)
    {
      clearsongparts(0, cs, cp, ci, ct, cn, plen);
      ImGui::CloseCurrentPopup();
    }
    if (r == 1) ImGui::CloseCurrentPopup();
    if (r == 2)
    {
      clearsongparts(1, 0, 0, 0, 0, 0, plen);
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}

static void hrdialog(void)
{
  static int a, d, s, r;

  if (wanthr)
  {
    ImGui::OpenPopup("##hradsr");
    wanthr = false;
    a = (editorInfo.adparam >> 12) & 15;
    d = (editorInfo.adparam >> 8) & 15;
    s = (editorInfo.adparam >> 4) & 15;
    r = editorInfo.adparam & 15;
  }
  if (beginmodal("##hradsr", ImGui::GetFontSize() * 24.0f))
  {
    const char *labels[] = { "OK", "Cancel" };
    float radius = ImGui::GetFontSize() * 1.3f;
    int result;
    char text[32];

    title("Hard restart ADSR");
    body("The envelope used for the hard restart before each note. The usual value is 0F00.");
    ImGui::Spacing();
    knob("##a", "Attack", &a, 0, 15, radius, "attack");
    ImGui::SameLine();
    knob("##d", "Decay", &d, 0, 15, radius, "decay");
    ImGui::SameLine();
    knob("##s", "Sustain", &s, 0, 15, radius, "sustain");
    ImGui::SameLine();
    knob("##r", "Release", &r, 0, 15, radius, "release");
    snprintf(text, sizeof text, "%X%X%X%X", a, d, s, r);
    ImGui::SameLine();
    ImGui::PushFont(fontmono, ImGui::GetStyle().FontSizeBase * 1.4f);
    ImGui::TextUnformatted(text);
    ImGui::PopFont();
    result = buttons(labels, 2, 1);
    if (result == 0)
    {
      sethradsr((a << 12) | (d << 8) | (s << 4) | r);
      ImGui::CloseCurrentPopup();
    }
    if (result == 1) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
}

/*
 * Export (packer / relocator) options
 */

static const char *zeropagenote(int adr, bool ghostregs)
{
  if (ghostregs) return "ghost registers use 27 bytes from here";
  if (adr < 0x90) return "used by the BASIC interpreter";
  if (adr < 0xfb) return "used by KERNAL routines";
  if (adr < 0xfe) return "unused, safe";
  return "$FF is used by the BASIC interpreter";
}

static void exportdialog(void)
{
  static const struct { int bit; const char *label; const char *tip; } options[] = {
    { PLAYER_BUFFERED, "Buffered SID writes", "Writes all SID registers at once at the end of the frame" },
    { PLAYER_SOUNDEFFECTS, "Sound effect support", "Lets a game play sound effects on a channel (needs buffered writes)" },
    { PLAYER_VOLUME, "Volume change support", "Lets the program change the master volume" },
    { PLAYER_AUTHORINFO, "Store author info", "Puts the author name into the player" },
    { PLAYER_ZPGHOSTREGS, "Zeropage ghost registers", "Keeps the SID register copies in zeropage (needs buffered writes)" },
    { PLAYER_NOOPTIMIZATION, "Disable optimization", "Keeps all player features even if the song doesn't use them" },
    { PLAYER_FULLBUFFERED, "Full SID buffering", "Buffers every register write (needs buffered writes)" },
  };
  int c;

  if (wantexport)
  {
    ImGui::OpenPopup("##export");
    wantexport = false;
  }
  if (!beginmodal("##export", ImGui::GetFontSize() * 30.0f)) return;

  title("Export song");
  body("Packs the song with its player and relocates it, ready for a C64 program or a SID player.");
  ImGui::Spacing();

  sectiontitle("FORMAT");
  ImGui::RadioButton("SID (SIDPlay)", &fileformat, FORMAT_SID);
  ImGui::SameLine();
  ImGui::RadioButton("PRG (C64)", &fileformat, FORMAT_PRG);
  ImGui::SameLine();
  ImGui::RadioButton("BIN (raw)", &fileformat, FORMAT_BIN);

  sectiontitle("ADDRESSES");
  {
    int page = (playeradr >> 8) & 0xff;
    int zp = zeropageadr & 0xff;
    bool ghost = (playerversion & PLAYER_ZPGHOSTREGS) != 0;
    char text[64];

    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(theme.textdim), "Start address");
    ImGui::SameLine(ImGui::GetFontSize() * 9.0f);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
    snprintf(text, sizeof text, "$%02X00", page);
    if (ImGui::SliderInt("##page", &page, 0x02, 0xff, text)) playeradr = page << 8;

    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(theme.textdim), "Zeropage");
    ImGui::SameLine(ImGui::GetFontSize() * 9.0f);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
    snprintf(text, sizeof text, "$%02X-$%02X", zp, zp + (ghost ? 26 : 1));
    if (ImGui::SliderInt("##zp", &zp, 0x02, ghost ? 0xe5 : 0xfe, text)) zeropageadr = zp;
    ImGui::SameLine();
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(theme.textfaint), "%s", zeropagenote(zp, ghost));
  }

  sectiontitle("PLAYER OPTIONS");
  for (c = 0; c < (int)(sizeof options / sizeof options[0]); c++)
  {
    bool on = (playerversion & options[c].bit) != 0;
    if (ImGui::Checkbox(options[c].label, &on))
    {
      playerversion ^= options[c].bit;
      // Some options need buffered writes
      if (options[c].bit == PLAYER_BUFFERED)
      {
        if (!(playerversion & PLAYER_BUFFERED))
          playerversion &= ~(PLAYER_SOUNDEFFECTS | PLAYER_ZPGHOSTREGS | PLAYER_FULLBUFFERED);
      }
      else if (playerversion & (PLAYER_SOUNDEFFECTS | PLAYER_ZPGHOSTREGS | PLAYER_FULLBUFFERED))
        playerversion |= PLAYER_BUFFERED;
    }
    if (ImGui::IsItemHovered()) tooltip(options[c].tip);
  }

  {
    const char *labels[] = { "Export...", "Cancel" };
    int r = buttons(labels, 2, 1);
    if (r == 0)
    {
      cmd_post(GTCMD_EXPORTNOW);
      ImGui::CloseCurrentPopup();
    }
    if (r == 1) ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

/*
 * SID import
 */

static bool wantsidimport = false;
static char sidimportpath[MAX_PATHNAME] = "";
static SIDINFO sidimportinfo;
static bool sidimportinfook = false;

extern "C" void gui_opensidimport(const char *path)
{
  snprintf(sidimportpath, sizeof sidimportpath, "%s", path ? path : "");
  sidimportinfook = sidinfo_read(sidimportpath, &sidimportinfo) != 0;
  wantsidimport = true;
}

static void sidimportdialog(void)
{
  SID2SNG_OPTIONS *o = &sidimportoptions;
  float labelw;

  if (wantsidimport)
  {
    ImGui::OpenPopup("##sidimport");
    wantsidimport = false;
  }
  if (!beginmodal("##sidimport", ImGui::GetFontSize() * 30.0f)) return;
  labelw = ImGui::GetFontSize() * 8.5f;

  title("Import SID file");
  if (sidimportinfook)
  {
    const char *clocks[] = { "unknown", "PAL", "NTSC", "PAL / NTSC" };
    const char *models[] = { "unknown", "6581", "8580", "6581 / 8580" };
    ImGui::PushFont(fontbold, 0.0f);
    ImGui::TextWrapped("%s", sidimportinfo.title[0] ? sidimportinfo.title : gt_basename(sidimportpath));
    ImGui::PopFont();
    body(sidimportinfo.author);
    if (sidimportinfo.released[0]) body(sidimportinfo.released);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(theme.textfaint));
    ImGui::Text("%s v%d  |  %d subtune%s  |  %s  |  %s  |  $%04X-$%04X",
      sidimportinfo.rsid ? "RSID" : "PSID", sidimportinfo.version, sidimportinfo.songs,
      sidimportinfo.songs == 1 ? "" : "s", clocks[sidimportinfo.clock & 3], models[sidimportinfo.model & 3],
      sidimportinfo.loadaddress, sidimportinfo.loadaddress + sidimportinfo.datasize - 1);
    ImGui::PopStyleColor();
  }
  else body(gt_basename(sidimportpath));

  sectiontitle("CONVERSION");
  ImGui::RadioButton("Automatic", &o->mode, SID2SNG_AUTO);
  if (ImGui::IsItemHovered()) tooltip("Exact for SIDs made with GoatTracker, otherwise approximate");
  ImGui::SameLine();
  ImGui::RadioButton("Exact (GoatTracker SIDs)", &o->mode, SID2SNG_EXACT);
  if (ImGui::IsItemHovered()) tooltip("Turns a SID exported by GoatTracker back into its song: all subtunes, patterns, instruments and tables");
  ImGui::SameLine();
  ImGui::RadioButton("Approximate", &o->mode, SID2SNG_APPROX);
  if (ImGui::IsItemHovered()) tooltip("Plays any SID on an emulated C64 and writes down its notes and sounds. A starting point for editing, not an exact copy");

  if (o->mode != SID2SNG_EXACT)
  {
    int songs = sidimportinfook ? sidimportinfo.songs : 1;
    body("For other SIDs (approximate):");

    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(theme.textdim), "Subtune");
    ImGui::SameLine(labelw);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
    if (songs < 1) songs = 1;
    if (o->subtune > songs) o->subtune = 0;
    ImGui::SliderInt("##subtune", &o->subtune, 0, songs, o->subtune ? "%d" : "default");

    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(theme.textdim), "Length");
    ImGui::SameLine(labelw);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
    ImGui::SliderInt("##seconds", &o->seconds, 10, 600, "%d seconds");

    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(theme.textdim), "Rhythm grid");
    ImGui::SameLine(labelw);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
    ImGui::SliderInt("##rows", &o->rowsperbeat, 0, 8, o->rowsperbeat ? "%d rows per note" : "automatic");

    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(theme.textdim), "Pattern length");
    ImGui::SameLine(labelw);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
    ImGui::SliderInt("##plen", &o->patternlength, 16, MAX_PATTROWS, "%d rows");
  }

  {
    const char *labels[] = { "Import", "Cancel" };
    int r = buttons(labels, 2, 1);
    if (r == 0)
    {
      cmd_postfile(GTCMD_IMPORTSIDNOW, sidimportpath);
      ImGui::CloseCurrentPopup();
    }
    if (r == 1) ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

void gui::drawdialogs(void)
{
  exportdialog();
  sidimportdialog();
  // One at a time: the first one wanted opens
  savedialog();
  quitdialog();
  cleardialog();
  hrdialog();
  alertdialog();
}

/*
 * The classic text screen, drawn as a modal panel over the interface while
 * a classic dialog (packer options, file selector on Windows/Linux) runs
 */

static ImU32 textcolor(int index)
{
  static const ImU32 palette[16] = {
    IM_COL32(0, 0, 0, 0),        IM_COL32(42, 74, 122, 255),  IM_COL32(58, 142, 246, 255), IM_COL32(92, 93, 102, 255),
    IM_COL32(120, 90, 160, 255), IM_COL32(76, 195, 138, 255), IM_COL32(80, 160, 170, 255), IM_COL32(154, 155, 163, 255),
    IM_COL32(205, 207, 214, 255), IM_COL32(143, 182, 247, 255), IM_COL32(76, 195, 138, 255), IM_COL32(199, 146, 234, 255),
    IM_COL32(242, 199, 68, 255), IM_COL32(215, 181, 240, 255), IM_COL32(229, 136, 77, 255), IM_COL32(255, 255, 255, 255)
  };
  return palette[index & 15];
}

void gui::drawtextscreen(void)
{
  ImGuiViewport *vp = ImGui::GetMainViewport();
  ImGuiIO &io = ImGui::GetIO();
  ImDrawList *dl;
  float size, cw, ch, w, h;
  ImVec2 p;
  int x, y;

  if (!scrbuffer) return;

  ImGui::SetNextWindowPos(vp->Pos);
  ImGui::SetNextWindowSize(vp->Size);
  ImGui::SetNextWindowFocus();
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.55f));
  ImGui::Begin("##textscreen", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoNav);
  ImGui::PopStyleColor();
  ImGui::PopStyleVar(2);

  // Font size that fits the 100x37 screen into the window
  {
    float adv = fontmono->CalcTextSizeA(100.0f, FLT_MAX, 0.0f, "0").x / 100.0f;
    float byw = vp->Size.x * 0.94f / (MAX_COLUMNS * adv);
    float byh = vp->Size.y * 0.92f / (MAX_ROWS * 1.3f);
    size = byw < byh ? byw : byh;
    cw = adv * size;
    ch = size * 1.3f;
  }
  w = cw * MAX_COLUMNS;
  h = ch * MAX_ROWS;
  p = ImVec2(vp->Pos.x + (vp->Size.x - w) * 0.5f, vp->Pos.y + (vp->Size.y - h) * 0.5f);
  dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(ImVec2(p.x - 14.0f, p.y - 12.0f), ImVec2(p.x + w + 14.0f, p.y + h + 12.0f), theme.bgpanel, 10.0f);
  dl->AddRect(ImVec2(p.x - 14.0f, p.y - 12.0f), ImVec2(p.x + w + 14.0f, p.y + h + 12.0f), theme.separator, 10.0f);

  for (y = 0; y < MAX_ROWS; y++)
  {
    for (x = 0; x < MAX_COLUMNS; x++)
    {
      unsigned cell = scrbuffer[y * MAX_COLUMNS + x];
      int chr = cell & 0xff;
      int color = (cell >> 16) & 0xff;
      int fg = color & 15;
      int bg = (color >> 4) & 15;
      ImVec2 c(p.x + x * cw, p.y + y * ch);

      if (bg)
        dl->AddRectFilled(c, ImVec2(c.x + cw, c.y + ch), textcolor(bg));
      if ((chr > 32) && (chr < 127))
      {
        char s[2] = { (char)chr, 0 };
        dl->AddText(fontmono, size, ImVec2(c.x, c.y + (ch - size) * 0.5f), textcolor(fg), s);
      }
    }
  }

  // Mouse as character cells
  {
    int mx = (int)((io.MousePos.x - p.x) / cw);
    int my = (int)((io.MousePos.y - p.y) / ch);
    if (mx < 0) mx = 0;
    if (my < 0) my = 0;
    if (mx >= MAX_COLUMNS) mx = MAX_COLUMNS - 1;
    if (my >= MAX_ROWS) my = MAX_ROWS - 1;
    textscreenmousex = mx;
    textscreenmousey = my;
    tsmousebuttons = 0;
    if (io.MouseDown[0]) tsmousebuttons |= MOUSEB_LEFT;
    if (io.MouseDown[1]) tsmousebuttons |= MOUSEB_RIGHT;
    if (io.MouseDown[2]) tsmousebuttons |= MOUSEB_MIDDLE;
  }

  ImGui::End();
}
