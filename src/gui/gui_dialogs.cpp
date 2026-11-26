// GoatTracker GUI: dialogs and the classic text screen overlay

#include "gui_internal.h"
#ifdef GT_PLUGIN
#include "../plugin/gtplugin.h"
#endif

using namespace gui;

static bool wantalert = false;
static char alerttitle[128] = "";
static char alerttext[512] = "";

static bool wantquit = false;
static bool wantclear = false;
static bool wantclearall = false;
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
#ifdef GT_PLUGIN
  // The plugin doesn't quit: the host closes it
  cmd_post(GTCMD_QUIT);
  return;
#endif
  if (gui_songmodified())
    cmd_post(GTCMD_QUIT);       // Asks to save the changes
  else
    wantquit = true;
}

extern "C" void gui_askclear(void)
{
  wantclear = true;
}

extern "C" void gui_askclearall(void)
{
  wantclear = true;
  wantclearall = true;
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
    title("Quit GoatTracker Ultimate?");
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
    cs = cp = ci = ct = cn = wantclearall;
    wantclearall = false;
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
    ImGui::Checkbox(ci ? "Tables##tables" : "Tables (the instruments kept keep theirs)##tables", &ct);
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
    ImGui::SliderInt("##subtune", &o->subtune, 0, songs, o->subtune ? "%d" : "all");
    if (ImGui::IsItemHovered()) tooltip("All: every subtune of the SID becomes a subtune of the song (up to 32). A number: only that one");

    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(theme.textdim), "Length");
    ImGui::SameLine(labelw);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
    ImGui::SliderInt("##seconds", &o->seconds, 10, 600, "%d seconds");
    if (ImGui::IsItemHovered()) tooltip("How much of each subtune is recorded, when it doesn't loop");

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

    {
      bool loops = o->loops != 0;
      ImGui::SetCursorPosX(labelw);
      if (ImGui::Checkbox("Loop where the tune repeats", &loops)) o->loops = loops ? 1 : 0;
      if (ImGui::IsItemHovered())
        tooltip("Each subtune is played up to 10 minutes to find where it starts over. It then ends there and loops back, like the original: the whole tune, in fewer patterns");
    }
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

/*
 * MIDI import: which MIDI channels go to which tracks
 */

static bool wantmidiimport = false;
static char midiimportpath[MAX_PATHNAME] = "";
static MIDISCAN midiscan;

extern "C" void gui_openmidiimport(const char *path, const MIDISCAN *scan)
{
  snprintf(midiimportpath, sizeof midiimportpath, "%s", path ? path : "");
  midiscan = *scan;
  // The choice last made, unless the file has no notes for it. The tracks
  // at first: the cursor's
  if ((midiimportchannel >= 1) && (midiimportchannel <= 16) && (!midiscan.notes[midiimportchannel - 1]))
    midiimportchannel = 0;
  if (!(midiimporttracks & ((1u << MAX_CHN) - 1))) midiimporttracks = 1u << editorInfo.epchn;
  if ((midiimportmode == MIDIIMPORT_ALL) && (!midiscan.notes[0]) && (!midiscan.notes[1]) && (!midiscan.notes[2]))
    midiimportmode = MIDIIMPORT_TRACKS;
  wantmidiimport = true;
}

static void midiimportdialog(void)
{
  MIDI2SNG_OPTIONS *o = &midiimportoptions;
  float labelw;
  char text[160];
  int c;
  bool any = false;

  if (wantmidiimport)
  {
    ImGui::OpenPopup("##midiimport");
    wantmidiimport = false;
  }
  if (!beginmodal("##midiimport", ImGui::GetFontSize() * 30.0f)) return;
  labelw = ImGui::GetFontSize() * 8.5f;

  title("Import MIDI file");
  ImGui::PushFont(fontbold, 0.0f);
  ImGui::TextWrapped("%s", midiscan.name[0] ? midiscan.name : gt_basename(midiimportpath));
  ImGui::PopFont();
  if (midiscan.name[0]) body(gt_basename(midiimportpath));

  // The MIDI channels that have notes
  snprintf(text, sizeof text, "Notes on MIDI channel");
  for (c = 0; c < 16; c++)
  {
    if (!midiscan.notes[c]) continue;
    snprintf(text + strlen(text), sizeof text - strlen(text), "%s %d (%d)", any ? "," : "", c + 1, midiscan.notes[c]);
    any = true;
  }
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(theme.textfaint));
  ImGui::TextWrapped("%s", any ? text : "No notes in this file");
  if (midiscan.bytracks)
    ImGui::TextWrapped("All the notes are on one MIDI channel: the file's tracks are read as channels 1, 2, 3...");
  ImGui::PopStyleColor();

  sectiontitle("IMPORT");
  {
    bool none = (!midiscan.notes[0]) && (!midiscan.notes[1]) && (!midiscan.notes[2]);
    ImGui::BeginDisabled(none);
    ImGui::RadioButton("All: MIDI channels 1-3 to tracks 1-3", &midiimportmode, MIDIIMPORT_ALL);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      tooltip("Channel 1 to track 1, 2 to 2, 3 to 3. A track whose channel has no notes keeps what it has");
  }
  // To tracks: the whole file (any MIDI channel, or one chosen) to the
  // tracks ticked. One track: one note at a time. 2-3: spread over them
  {
    int ntracks = 0;

    ImGui::RadioButton("To track", &midiimportmode, MIDIIMPORT_TRACKS);
    if (ImGui::IsItemHovered())
      tooltip("The file's notes to the tracks ticked, whatever their MIDI channel: replaces those tracks only");
    for (c = 0; c < MAX_CHN; c++)
    {
      bool on = (midiimporttracks & (1u << c)) != 0;
      ImGui::SameLine();
      snprintf(text, sizeof text, "%d##mtrack%d", c + 1, c);
      if (ImGui::Checkbox(text, &on))
      {
        unsigned m = on ? (midiimporttracks | (1u << c)) : (midiimporttracks & ~(1u << c));
        if (m & ((1u << MAX_CHN) - 1)) midiimporttracks = m;
        midiimportmode = MIDIIMPORT_TRACKS;
      }
      if (ImGui::IsItemHovered())
        tooltip("Tick 2 or 3 tracks to spread the notes over them: each new note goes to the track free the longest, notes played together to different tracks");
      if (midiimporttracks & (1u << c)) ntracks++;
    }

    // The MIDI channel the notes come from (only the channels with notes)
    ImGui::SameLine();
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(theme.textdim), "from");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
    if (midiimportchannel) snprintf(text, sizeof text, "MIDI channel %d", midiimportchannel);
    else snprintf(text, sizeof text, "Any channel");
    if (ImGui::BeginCombo("##mchannel", text))
    {
      if (ImGui::Selectable("Any channel", midiimportchannel == 0))
      {
        midiimportchannel = 0;
        midiimportmode = MIDIIMPORT_TRACKS;
      }
      for (c = 0; c < 16; c++)
      {
        if (!midiscan.notes[c]) continue;
        snprintf(text, sizeof text, "MIDI channel %d (%d notes)", c + 1, midiscan.notes[c]);
        if (ImGui::Selectable(text, midiimportchannel == c + 1))
        {
          midiimportchannel = c + 1;
          midiimportmode = MIDIIMPORT_TRACKS;
        }
      }
      ImGui::EndCombo();
    }

    ImGui::RadioButton("Mixed: all MIDI channels shared out to the 3 tracks", &midiimportmode, MIDIIMPORT_MIXED);
    if (ImGui::IsItemHovered())
      tooltip("Chords too: up to 3 notes at once, from any MIDI channel (the import as it was before)");

    if (midiimportmode == MIDIIMPORT_MIXED)
      body("All 3 tracks of the subtune are replaced. The instruments and the other subtunes stay. CTRL+Z undoes the import.");
    else if (midiimportmode == MIDIIMPORT_ALL)
      body("One note at a time on each track (of notes starting together, the highest). Only the tracks that get notes are replaced: the other tracks, the instruments and the other subtunes stay. CTRL+Z undoes the import.");
    else if (ntracks <= 1)
      body("Every note goes to the track ticked, one at a time (of notes starting together, the highest), whatever its MIDI channel. Only that track is replaced: the other tracks, the song tempo (if the other tracks have notes), the instruments and the other subtunes stay. CTRL+Z undoes the import.");
    else
      body("The notes are spread over the tracks ticked: each new note goes to the track that has been free the longest (so that notes ring on), notes played together to different tracks. Only those tracks are replaced; the song tempo stays if the other tracks have notes. CTRL+Z undoes the import.");
  }

  sectiontitle("GRID");
  ImGui::AlignTextToFramePadding();
  ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(theme.textdim), "Rhythm grid");
  ImGui::SameLine(labelw);
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
  ImGui::SliderInt("##mrows", &o->rowsperbeat, 1, 16, "%d rows per beat");
  if (ImGui::IsItemHovered()) tooltip("Pattern rows per quarter note: notes are moved to the nearest row");

  ImGui::AlignTextToFramePadding();
  ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(theme.textdim), "Pattern length");
  ImGui::SameLine(labelw);
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
  ImGui::SliderInt("##mplen", &o->patternlength, 16, MAX_PATTROWS, "%d rows");

  {
    const char *labels[] = { "Import", "Cancel" };
    int r = buttons(labels, 2, 1);
    if (r == 0)
    {
      cmd_postfile(GTCMD_IMPORTMIDINOW, midiimportpath);
      ImGui::CloseCurrentPopup();
    }
    if (r == 1) ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

/*
 * ProTracker module import: which module channels go to tracks 1-3
 */

static bool wantmodimport = false;
static char modimportpath[MAX_PATHNAME] = "";
static MODSCAN modscan;

extern "C" void gui_openmodimport(const char *path, const MODSCAN *scan)
{
  snprintf(modimportpath, sizeof modimportpath, "%s", path ? path : "");
  modscan = *scan;
  wantmodimport = true;
}

// The name of a choice: none, one channel or two merged
static void modsourcename(unsigned m, char *text, int size)
{
  int c, first = -1, second = -1;

  for (c = 0; c < modscan.channels; c++)
  {
    if (!(m & (1u << c))) continue;
    if (first < 0) first = c;
    else if (second < 0) second = c;
  }
  if (first < 0) snprintf(text, size, "Nothing (empty)");
  else if (second < 0) snprintf(text, size, "Channel %d (%d notes)", first + 1, modscan.notes[first]);
  else snprintf(text, size, "Channels %d + %d merged", first + 1, second + 1);
}

static void modimportdialog(void)
{
  float labelw;
  char text[160];
  int c, d, t;
  bool any = false;

  if (wantmodimport)
  {
    ImGui::OpenPopup("##modimport");
    wantmodimport = false;
  }
  if (!beginmodal("##modimport", ImGui::GetFontSize() * 30.0f)) return;
  labelw = ImGui::GetFontSize() * 5.0f;

  title("Import ProTracker module");
  ImGui::PushFont(fontbold, 0.0f);
  ImGui::TextWrapped("%s", modscan.name[0] ? modscan.name : gt_basename(modimportpath));
  ImGui::PopFont();
  if (modscan.name[0]) body(gt_basename(modimportpath));
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(theme.textfaint));
  ImGui::TextWrapped("%d channels, %d positions, %d patterns, %d samples", modscan.channels, modscan.positions,
    modscan.patterns, modscan.samples);
  ImGui::PopStyleColor();

  sectiontitle("TRACKS");
  for (t = 0; t < MAX_CHN; t++)
  {
    unsigned cur = modimportoptions.source[t];

    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(theme.textdim), "Track %d", t + 1);
    ImGui::SameLine(labelw);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);
    {
      char preview[64];
      modsourcename(cur, preview, sizeof preview);
      snprintf(text, sizeof text, "##modtrack%d", t);
      if (ImGui::BeginCombo(text, preview))
      {
        if (ImGui::Selectable("Nothing (empty)", cur == 0)) modimportoptions.source[t] = 0;
        for (c = 0; c < modscan.channels; c++)
        {
          modsourcename(1u << c, preview, sizeof preview);
          ImGui::PushID(c);
          if (ImGui::Selectable(preview, cur == (1u << c))) modimportoptions.source[t] = 1u << c;
          ImGui::PopID();
        }
        ImGui::Separator();
        for (c = 0; c < modscan.channels; c++)
        {
          for (d = c + 1; d < modscan.channels; d++)
          {
            unsigned m = (1u << c) | (1u << d);
            modsourcename(m, preview, sizeof preview);
            ImGui::PushID(100 + c * 16 + d);
            if (ImGui::Selectable(preview, cur == m)) modimportoptions.source[t] = m;
            ImGui::PopID();
          }
        }
        ImGui::EndCombo();
      }
    }
    if (modimportoptions.source[t]) any = true;
  }

  body("GoatTracker has 3 tracks: each gets one channel of the module, or two merged (where both have a note on "
    "a row, the lower channel's note plays). The module becomes a new song. Its samples become instruments of the "
    "same number and name with a SID sound in their place (noise, saw or pulse: a start for the SID Designer); "
    "speed and BPM become the tempo, portamento and vibrato their commands, C00 a keyoff. Arpeggios, volume and "
    "the other effects are left out and counted.");

  {
    const char *labels[] = { "Import", "Cancel" };
    int r;

    ImGui::BeginDisabled(!any);
    r = buttons(labels, 2, 1);
    ImGui::EndDisabled();
    if ((r == 0) && (any))
    {
      cmd_postfile(GTCMD_IMPORTMODNOW, modimportpath);
      ImGui::CloseCurrentPopup();
    }
    if (r == 1) ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

/*
 * VIEW menu of the retro menu bar, About, level meter colors
 */

static bool wantviewmenu = false;
static bool wantabout = false;
static bool wantmetercolors = false;

extern "C" void gui_openviewmenu(void)
{
  wantviewmenu = true;
}

void gui::openabout(void)
{
  wantabout = true;
}

void gui::openmetercolors(void)
{
  wantmetercolors = true;
}

// The retro screen has no menu bar on Windows / Linux: VIEW in the menu bar
// shown while the right button is held on the title bar opens this
static void viewmenu(void)
{
  if (wantviewmenu)
  {
    ImGui::OpenPopup("##viewmenu");
    wantviewmenu = false;
  }
  if (ImGui::BeginPopup("##viewmenu"))
  {
    if (ImGui::MenuItem(showdesigner ? "Tracker" : "SID Designer", "CTRL+NUMPAD 7")) cmd_post(GTCMD_DESIGNER);
    ImGui::Separator();
    if (ImGui::MenuItem("Gradient Background", NULL, retrogradient != 0)) cmd_post(GTCMD_RETROGRADIENT);
    if (ImGui::MenuItem("Meter Colors...")) cmd_post(GTCMD_METERCOLORS);
#ifndef GT_PLUGIN
    ImGui::Separator();
    if (ImGui::MenuItem("Preferences...")) cmd_post(GTCMD_PREFERENCES);
#endif
    ImGui::Separator();
    if (ImGui::MenuItem("About GoatTracker Ultimate")) cmd_post(GTCMD_ABOUT);
    ImGui::EndPopup();
  }
}

/*
 * The pattern editor's menu (right click on a pattern): cut, copy, paste and
 * clear the rows marked (with nothing marked the track clicked), undo, redo
 */

static bool wantpatternmenu = false;

extern "C" void gui_openpatternmenu(void)
{
  wantpatternmenu = true;
}

#ifdef __APPLE__
#define MENUCMD "CMD"
#else
#define MENUCMD "CTRL"
#endif

static void patternmenu(void)
{
  if (wantpatternmenu)
  {
    ImGui::OpenPopup("##patternmenu");
    wantpatternmenu = false;
  }
  // The text about as tall as the retro screen's characters (as the
  // plugin's file menus)
  ImVec2 cell0 = gui::retrocellpos(0, 1);
  ImVec2 cell1 = gui::retrocellpos(0, 2);
  float size = (cell1.y - cell0.y) * 0.8f;
  if (size < ImGui::GetFontSize()) size = ImGui::GetFontSize();
  ImGui::PushFont(NULL, size);
  bool open = ImGui::BeginPopup("##patternmenu");
  ImGui::PopFont();
  if (open)
  {
    char what[64];

    ImGui::PushFont(NULL, size);

    if (editorInfo.epmarkchn != -1)
    {
      int first = editorInfo.epmarkstart < editorInfo.epmarkend ? editorInfo.epmarkstart : editorInfo.epmarkend;
      int last = editorInfo.epmarkstart < editorInfo.epmarkend ? editorInfo.epmarkend : editorInfo.epmarkstart;
      snprintf(what, sizeof what, "Track %d, rows %d-%d", editorInfo.epmarkchn + 1, first, last);
    }
    else
      snprintf(what, sizeof what, "Track %d (pattern %02X)", editorInfo.epchn + 1, editorUndoInfo.editorInfo[editorInfo.epchn].epnum);
    ImGui::TextDisabled("%s", what);
    ImGui::Separator();
    if (ImGui::MenuItem("Cut")) cmd_post(GTCMD_PATTCUT);
    if (ImGui::MenuItem("Copy")) cmd_post(GTCMD_PATTCOPY);
    if (ImGui::MenuItem("Paste", NULL, false, patterncopyrows > 0)) cmd_post(GTCMD_PATTPASTE);
    if (ImGui::MenuItem("Clear")) cmd_post(GTCMD_PATTCLEAR);
    ImGui::Separator();
    if (ImGui::MenuItem("Undo", MENUCMD "+Z", false, undoCanUndo() != 0)) cmd_post(GTCMD_UNDO);
    if (ImGui::MenuItem("Redo", MENUCMD "+SHIFT+Z", false, undoCanRedo() != 0)) cmd_post(GTCMD_REDO);
    ImGui::PopFont();
    ImGui::EndPopup();
  }
}

#ifndef GT_PLUGIN
/*
 * Preferences (standalone): the audio output and the MIDI devices. A change
 * opens the output / the devices again at once
 */

static bool wantpreferences = false;

void gui::openpreferences(void)
{
  wantpreferences = true;
}

static const unsigned prefrates[] = { 44100, 48000 };
static const unsigned prefbuffers[] = { 32, 64, 128, 256, 512 };

static void preferencesdialog(void)
{
  static char devices[32][128];
  static int ndevices = 0;
  static char mididevices[32][MIDI_DEVICENAMELEN];
  static int nmididevices = 0;
  static bool failed = false;
  bool changed = false;
  bool midichanged = false;
  char text[256];
  int c;

  if (wantpreferences)
  {
    ImGui::OpenPopup("##preferences");
    wantpreferences = false;
    failed = false;
    ndevices = SDL_GetNumAudioDevices(0);
    if (ndevices < 0) ndevices = 0;
    if (ndevices > 32) ndevices = 32;
    for (c = 0; c < ndevices; c++)
    {
      const char *name = SDL_GetAudioDeviceName(c, 0);
      snprintf(devices[c], sizeof devices[c], "%s", name ? name : "?");
    }
    nmididevices = midi_listdevices(mididevices, 32);
  }
  if (beginmodal("##preferences", ImGui::GetFontSize() * 30.0f))
  {
    const char *labels[] = { "Close" };
    float w = ImGui::GetFontSize() * 16.0f;

    title("Preferences");
    ImGui::TextUnformatted("Audio output");
    ImGui::Spacing();

    // The device
    ImGui::SetNextItemWidth(w);
    if (ImGui::BeginCombo("Output device", audiodevice[0] ? audiodevice : "System default"))
    {
      if (ImGui::Selectable("System default", !audiodevice[0]))
      {
        audiodevice[0] = 0;
        changed = true;
      }
      for (c = 0; c < ndevices; c++)
      {
        if (ImGui::Selectable(devices[c], !strcmp(audiodevice, devices[c])))
        {
          snprintf(audiodevice, MAX_PATHNAME, "%s", devices[c]);
          changed = true;
        }
      }
      ImGui::EndCombo();
    }

    // The sample rate
    snprintf(text, sizeof text, "%u Hz", mr);
    ImGui::SetNextItemWidth(w);
    if (ImGui::BeginCombo("Sample rate", text))
    {
      for (c = 0; c < (int)(sizeof prefrates / sizeof prefrates[0]); c++)
      {
        char item[32];
        snprintf(item, sizeof item, "%u Hz", prefrates[c]);
        if (ImGui::Selectable(item, mr == prefrates[c]))
        {
          mr = prefrates[c];
          changed = true;
        }
      }
      ImGui::EndCombo();
    }

    // The buffer size: 32-512 samples (the one chosen ticked), or the
    // default length in milliseconds. A size from an older version's list
    // (1024-4096) stays until another is chosen
    if (audiobuffer) snprintf(text, sizeof text, "%u samples (%.1f ms)", audiobuffer, audiobuffer * 1000.0 / mr);
    else snprintf(text, sizeof text, "Default (%u ms)", b);
    ImGui::SetNextItemWidth(w);
    if (ImGui::BeginCombo("Buffer size", text))
    {
      char item[64];
      for (c = 0; c < (int)(sizeof prefbuffers / sizeof prefbuffers[0]); c++)
      {
        snprintf(item, sizeof item, "%u", prefbuffers[c]);
        if ((ImGui::MenuItem(item, NULL, audiobuffer == prefbuffers[c])) && (audiobuffer != prefbuffers[c]))
        {
          audiobuffer = prefbuffers[c];
          changed = true;
        }
      }
      ImGui::Separator();
      snprintf(item, sizeof item, "Default (%u ms)", b);
      if ((ImGui::MenuItem(item, NULL, !audiobuffer)) && (audiobuffer))
      {
        audiobuffer = 0;
        changed = true;
      }
      ImGui::EndCombo();
    }

    if (changed) failed = !reopenaudio();

    ImGui::Spacing();
    if (snd_sndinitted)
      snprintf(text, sizeof text, "%s %u Hz, %u samples per buffer (%.1f ms). A smaller buffer plays notes sooner; if the sound "
        "crackles, choose a bigger one.", failed ? "That output couldn't be opened: the system's default output plays now," : "Now:",
        snd_mixrate, snd_obtainedsamples, snd_mixrate ? snd_obtainedsamples * 1000.0 / snd_mixrate : 0.0);
    else
      snprintf(text, sizeof text, "No audio output could be opened.");
    body(text);

    // The MIDI devices: the input device's notes and knobs are used (every
    // device's if none is chosen), the controller device's buttons are the
    // transport and it plays no notes (a device chosen as both is the
    // controller)
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextUnformatted("MIDI");
    ImGui::Spacing();
    ImGui::SetNextItemWidth(w);
    if (ImGui::BeginCombo("MIDI input device", midi_inputdevice[0] ? midi_inputdevice : "All devices"))
    {
      if (ImGui::Selectable("All devices", !midi_inputdevice[0]))
      {
        midi_inputdevice[0] = 0;
        midichanged = true;
      }
      for (c = 0; c < nmididevices; c++)
      {
        ImGui::PushID(c);
        if (ImGui::Selectable(mididevices[c], !strcmp(midi_inputdevice, mididevices[c])))
        {
          snprintf(midi_inputdevice, MIDI_DEVICENAMELEN, "%s", mididevices[c]);
          midichanged = true;
        }
        ImGui::PopID();
      }
      ImGui::EndCombo();
    }
    ImGui::SetNextItemWidth(w);
    if (ImGui::BeginCombo("MIDI controller device", midi_controllerdevice[0] ? midi_controllerdevice : "None"))
    {
      if (ImGui::Selectable("None", !midi_controllerdevice[0]))
      {
        midi_controllerdevice[0] = 0;
        midichanged = true;
      }
      for (c = 0; c < nmididevices; c++)
      {
        ImGui::PushID(c);
        if (ImGui::Selectable(mididevices[c], !strcmp(midi_controllerdevice, mididevices[c])))
        {
          snprintf(midi_controllerdevice, MIDI_DEVICENAMELEN, "%s", mididevices[c]);
          midichanged = true;
        }
        ImGui::PopID();
      }
      ImGui::EndCombo();
    }
    if (midichanged) midi_setdevices();
    ImGui::Spacing();
    body("The input device (a keyboard) plays and enters notes, and its knobs work. The controller device's buttons "
      "are the transport and play no notes: PLAY (A#5) plays the song, CYCLE (D5) the pattern, STOP (A5) stops, "
      "RECORD (B5) switches edit / jam mode, the cursor buttons (C6, C#6, D6, D#6: up, down, left, right) move the "
      "cursor; in the SID Designer they choose the tab and try the BROWSER's instruments. These are the notes a "
      "Mackie Control sends (C3 = middle C).");
    if (buttons(labels, 1, 0) >= 0) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
}
#else
void gui::openpreferences(void)
{
}
#endif

static void aboutdialog(void)
{
  if (wantabout)
  {
    ImGui::OpenPopup("##about");
    wantabout = false;
  }
  if (beginmodal("##about", ImGui::GetFontSize() * 24.0f))
  {
    const char *labels[] = { "OK" };
    title(programname);
    ImGui::TextWrapped("%s", ABOUTTEXT);
    ImGui::Spacing();
    body("Based on GoatTracker 2 by Lasse \xc3\x96\xc3\xb6rni and contributors. reSID by Dag Lem, reSID-fp by Antti S. Lankila.");
    if (buttons(labels, 1, 0) >= 0) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
}

// Level meter colors: bottom, middle and top of the gradient, each with its
// opacity. Changes show at once; Cancel puts the old colors back
static ImVec4 metertovec(unsigned argb)
{
  return ImVec4(((argb >> 16) & 0xff) / 255.0f, ((argb >> 8) & 0xff) / 255.0f, (argb & 0xff) / 255.0f, ((argb >> 24) & 0xff) / 255.0f);
}

static unsigned vectometer(const ImVec4 &v)
{
  unsigned r = (unsigned)(v.x * 255.0f + 0.5f);
  unsigned g = (unsigned)(v.y * 255.0f + 0.5f);
  unsigned b = (unsigned)(v.z * 255.0f + 0.5f);
  unsigned a = (unsigned)(v.w * 255.0f + 0.5f);
  return (a << 24) | (r << 16) | (g << 8) | b;
}

// A sample of the meter: the gradient over a checkerboard (where it shows
// through) with pattern digits on top, as on the retro screen
static void meterpreview(ImVec2 size)
{
  ImDrawList *dl = ImGui::GetWindowDrawList();
  ImVec2 a = ImGui::GetCursorScreenPos();
  ImVec2 b(a.x + size.x, a.y + size.y);
  float cell = size.x / 6.0f;
  int x, y, i;
  const int bands = 24;

  ImGui::Dummy(size);
  dl->PushClipRect(a, b, true);
  for (y = 0; y * cell < size.y; y++)
  {
    for (x = 0; x < 6; x++)
    {
      ImU32 c = ((x + y) & 1) ? IM_COL32(44, 44, 52, 255) : IM_COL32(0, 0, 0, 255);
      dl->AddRectFilled(ImVec2(a.x + x * cell, a.y + y * cell), ImVec2(a.x + (x + 1) * cell, a.y + (y + 1) * cell), c);
    }
  }
  // Bottom (0) to top (1), in bands
  for (i = 0; i < bands; i++)
  {
    ImVec4 c0, c1;
    float t0 = (float)i / bands;
    float t1 = (float)(i + 1) / bands;
    float y0 = b.y - t0 * size.y;
    float y1 = b.y - t1 * size.y;
    for (int k = 0; k < 2; k++)
    {
      float t = k ? t1 : t0;
      int s = (t < 0.5f) ? 0 : 1;
      float f = t * 2.0f - s;
      ImVec4 p = metertovec(retrometercolors[s]);
      ImVec4 q = metertovec(retrometercolors[s + 1]);
      ImVec4 v(p.x + (q.x - p.x) * f, p.y + (q.y - p.y) * f, p.z + (q.z - p.z) * f, p.w + (q.w - p.w) * f);
      if (k) c1 = v;
      else c0 = v;
    }
    dl->AddRectFilledMultiColor(ImVec2(a.x, y1), ImVec2(b.x, y0), ImGui::ColorConvertFloat4ToU32(c1), ImGui::ColorConvertFloat4ToU32(c1),
      ImGui::ColorConvertFloat4ToU32(c0), ImGui::ColorConvertFloat4ToU32(c0));
  }
  for (y = 0; y < 7; y++)
  {
    float lh = size.y / 7.0f;
    dl->AddText(fontmono, lh * 0.8f, ImVec2(a.x + cell * 0.4f, a.y + y * lh + lh * 0.1f), retrocolor(CNORMAL), "05000");
  }
  dl->PopClipRect();
  dl->AddRect(a, b, theme.border);
}

// Ready-made meter colors (bottom, middle, top). Choosing one sets the colors
// but keeps the opacity of each
struct MeterPreset
{
  const char *name;
  unsigned rgb[3];
};

static const MeterPreset meterpresets[] =
{
  { "Default", { DEFAULT_METERBOTTOM & 0xffffff, DEFAULT_METERMIDDLE & 0xffffff, DEFAULT_METERTOP & 0xffffff } },
  { "LED (green, yellow, red)", { 0x00c040, 0xffe000, 0xff2020 } },
  { "Fire", { 0xffe060, 0xff7000, 0xa00000 } },
  { "Ice", { 0xffffff, 0x40d0ff, 0x1030c0 } },
  { "C64 (light blue, blue)", { 0xc9b4ff, 0x755df9, 0x332974 } },
  { "Rainbow (cyan, yellow, red)", { 0x00ffff, 0xffff6f, 0xff5b57 } },
  { "Green screen", { 0xc0ffc0, 0x30d030, 0x005000 } },
  { "Amber", { 0xffe0a0, 0xffa000, 0x804000 } },
  { "Grey", { 0xffffff, 0x9a9a9a, 0x404040 } },
};
#define NUM_METERPRESETS ((int)(sizeof meterpresets / sizeof meterpresets[0]))

// A preset button: the gradient it gives, bottom to top. Returns true when
// clicked
static bool presetswatch(const char *id, const unsigned *argb, ImVec2 size, const char *tiptext)
{
  ImDrawList *dl = ImGui::GetWindowDrawList();
  ImVec2 a = ImGui::GetCursorScreenPos();
  ImVec2 b(a.x + size.x, a.y + size.y);
  ImVec2 m(b.x, a.y + size.y * 0.5f);
  bool clicked = ImGui::InvisibleButton(id, size);
  bool hovered = ImGui::IsItemHovered();

  if (argb)
  {
    ImU32 c0 = argb[0] | 0xff000000u, c1 = argb[1] | 0xff000000u, c2 = argb[2] | 0xff000000u;
    // ImU32 is ABGR: swap red and blue
    c0 = (c0 & 0xff00ff00u) | ((c0 >> 16) & 0xff) | ((c0 & 0xff) << 16);
    c1 = (c1 & 0xff00ff00u) | ((c1 >> 16) & 0xff) | ((c1 & 0xff) << 16);
    c2 = (c2 & 0xff00ff00u) | ((c2 >> 16) & 0xff) | ((c2 & 0xff) << 16);
    dl->AddRectFilledMultiColor(a, m, c2, c2, c1, c1);
    dl->AddRectFilledMultiColor(ImVec2(a.x, m.y), b, c1, c1, c0, c0);
  }
  else dl->AddRectFilled(a, b, IM_COL32(0, 0, 0, 255));      // An empty slot
  dl->AddRect(a, b, hovered ? theme.text : theme.border, 0.0f, 0, hovered ? 2.0f : 1.0f);
  if ((hovered) && (tiptext)) ImGui::SetTooltip("%s", tiptext);
  return clicked;
}

static void metercolorsdialog(void)
{
  static unsigned saved[3];
  static int editing = 0;
  static const char *const names[3] = { "Bottom", "Middle", "Top" };

  if (wantmetercolors)
  {
    ImGui::OpenPopup("##metercolors");
    wantmetercolors = false;
    memcpy(saved, retrometercolors, sizeof saved);
    editing = 0;
  }
  if (beginmodal("##metercolors", ImGui::GetFontSize() * 30.0f))
  {
    const char *labels[] = { "OK", "Cancel", "Defaults" };
    float fs = ImGui::GetFontSize();
    ImVec4 col = metertovec(retrometercolors[editing]);
    int result;
    int c;

    title("Level meter colors");
    body("The meters of the retro screen rise behind each channel's instrument and command digits. Pick the color at "
      "the bottom, in the middle and at the top; the A slider (the bar right of the colors) sets how much the background shows through.");
    ImGui::Spacing();

    // Presets: ready-made ones, then the user's own
    {
      ImVec2 sw(fs * 1.9f, fs * 2.6f);
      float gap = fs * 0.45f;
      ImGui::TextUnformatted("Presets");
      for (c = 0; c < NUM_METERPRESETS; c++)
      {
        ImGui::PushID(c);
        if (c) ImGui::SameLine(0.0f, gap);
        if (presetswatch("##preset", meterpresets[c].rgb, sw, meterpresets[c].name))
        {
          int s;
          for (s = 0; s < 3; s++) retrometercolors[s] = (retrometercolors[s] & 0xff000000u) | meterpresets[c].rgb[s];
        }
        ImGui::PopID();
      }
      ImGui::Spacing();
      ImGui::TextUnformatted("My presets");
      ImGui::SameLine();
      ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(theme.textdim), "(Save keeps the colors above in a slot)");
      for (c = 0; c < METERPRESETS; c++)
      {
        bool empty = !(retrometerpresets[c][0] | retrometerpresets[c][1] | retrometerpresets[c][2]);
        char tiptext[40];
        ImGui::PushID(100 + c);
        if (c) ImGui::SameLine(0.0f, fs * 1.2f);
        ImGui::BeginGroup();
        snprintf(tiptext, sizeof tiptext, empty ? "Slot %d: empty" : "My preset %d (with its opacity)", c + 1);
        if ((presetswatch("##mine", empty ? NULL : retrometerpresets[c], sw, tiptext)) && (!empty))
          memcpy(retrometercolors, retrometerpresets[c], sizeof retrometercolors);
        ImGui::SameLine(0.0f, gap * 0.6f);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (sw.y - ImGui::GetFrameHeight()) * 0.5f);
        if (ImGui::SmallButton("Save"))
        {
          memcpy(retrometerpresets[c], retrometercolors, sizeof retrometercolors);
          // A preset of all-clear black would read as empty
          if (!(retrometerpresets[c][0] | retrometerpresets[c][1] | retrometerpresets[c][2])) retrometerpresets[c][0] = 1;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Keep the current colors in slot %d", c + 1);
        ImGui::EndGroup();
        ImGui::PopID();
      }
    }
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Which color: a swatch button for each
    for (c = 0; c < 3; c++)
    {
      ImVec4 v = metertovec(retrometercolors[c]);
      if (c) ImGui::SameLine();
      ImGui::PushID(c);
      if (ImGui::ColorButton("##swatch", v, ImGuiColorEditFlags_AlphaPreviewHalf, ImVec2(fs * 1.6f, fs * 1.6f))) editing = c;
      ImGui::SameLine(0.0f, 4.0f);
      ImGui::SetCursorPosY(ImGui::GetCursorPosY() + fs * 0.2f);
      if (ImGui::Selectable(names[c], editing == c, 0, ImVec2(fs * 4.0f, 0.0f))) editing = c;
      ImGui::PopID();
    }
    ImGui::Spacing();

    // The picker: saturation / value square, hue bar, opacity bar, values
    ImGui::PushItemWidth(fs * 15.0f);
    if (ImGui::ColorPicker4("##picker", &col.x, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf |
      ImGuiColorEditFlags_PickerHueBar | ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_DisplayHex | ImGuiColorEditFlags_Uint8 |
      ImGuiColorEditFlags_NoSidePreview))
      retrometercolors[editing] = vectometer(col);
    ImGui::PopItemWidth();
    ImGui::SameLine(0.0f, fs * 1.2f);
    {
      int alpha = (int)((retrometercolors[editing] >> 24) & 0xff);
      ImGui::BeginGroup();
      meterpreview(ImVec2(fs * 3.6f, fs * 7.0f));
      ImGui::Text("Opacity %d%%", (alpha * 100 + 127) / 255);
      ImGui::EndGroup();
    }

    result = buttons(labels, 3, 1);
    if (result == 0) ImGui::CloseCurrentPopup();
    if (result == 1)
    {
      memcpy(retrometercolors, saved, sizeof saved);
      ImGui::CloseCurrentPopup();
    }
    if (result == 2)
    {
      retrometercolors[0] = DEFAULT_METERBOTTOM;
      retrometercolors[1] = DEFAULT_METERMIDDLE;
      retrometercolors[2] = DEFAULT_METERTOP;
    }
    ImGui::EndPopup();
  }
}

#ifdef GT_PLUGIN
/*
 * DAW SYNC (plugin): following the host's tempo and position, MIDI channels
 * 1-3 to voices 1-3
 */

static bool wantdawsync = false;

extern "C" void gui_opendawsync(void)
{
  wantdawsync = true;
}

/*
 * The plugin's menus on the title bar: LOAD, SAVE, IMPORT, EXPORT, EDIT,
 * CLEAR. The files are chosen in the system's file dialogs, each kind of
 * file starting in the folder used last for it. EDIT: undo / redo (a host
 * may keep CMD+Z and CMD+SHIFT+Z for itself)
 */

static int wantfilemenu = -1;
static int filemenuwhich = 0;
static int filemenucolumn = 0;
static int filemenushown = -1;
static int filemenuhover = -1;     // the command of the entry under the mouse
static bool filemenuclose = false; // closing asked for
// LOAD > Open Recent: the file of the entry under the mouse (filemenuhover
// is then GTCMD_OPENFILE), whether its list should close (the mouse on
// another entry of LOAD)
static char filemenupath[MAX_PATHNAME] = "";
static bool recentclose = false;

extern "C" void gui_openfilemenu(int which, int column)
{
  wantfilemenu = which;
  filemenucolumn = column;
}

extern "C" int gui_filemenushown(void)
{
  return (wantfilemenu >= 0) ? wantfilemenu : filemenushown;
}

extern "C" int gui_filemenuhovered(void)
{
  return (filemenushown >= 0) ? filemenuhover : -1;
}

extern "C" const char *gui_filemenuhoveredpath(void)
{
  return filemenupath;
}

extern "C" void gui_closefilemenu(void)
{
  filemenuclose = true;
  wantfilemenu = -1;
}

// An entry: chosen with a (left) click, or with the right button let go over
// it (gui_filemenuhovered())
static void fmitem(const char *label, const char *shortcut, int cmd, bool enabled = true)
{
  if (ImGui::MenuItem(label, shortcut, false, enabled)) cmd_post(cmd);
  // (By its place: while the right button is held from the title bar ImGui
  // doesn't count the entry as hovered)
  if ((enabled) && (ImGui::IsMouseHoveringRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), false)))
  {
    filemenuhover = cmd;
    recentclose = true;
    // Shown as hovered there too
    if (!ImGui::IsItemHovered())
    {
      ImVec4 c = ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered);
      c.w *= 0.6f;
      ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImGui::GetColorU32(c));
    }
  }
}

// LOAD > Open Recent: the files opened, loaded, imported or saved last (the
// plugin's own list, newest first). Its list opens to the right when the
// mouse is on it (also while the right button is held from the title bar,
// when ImGui counts nothing as hovered, so the list is a popup of its own);
// an entry opens the file as LOAD > Song does
static void recentmenu(void)
{
  int n = gui_recentfilecount();
  bool enabled = n > 0;
  ImVec2 a, b;
  int c;

  if (ImGui::MenuItem("Open Recent", ">", false, enabled)) recentclose = false;
  a = ImGui::GetItemRectMin();
  b = ImGui::GetItemRectMax();
  if ((enabled) && (ImGui::IsMouseHoveringRect(a, b, false)))
  {
    recentclose = false;
    if (!ImGui::IsItemHovered())
    {
      ImVec4 col = ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered);
      col.w *= 0.6f;
      ImGui::GetWindowDrawList()->AddRectFilled(a, b, ImGui::GetColorU32(col));
    }
    if (!ImGui::IsPopupOpen("##recent")) ImGui::OpenPopup("##recent");
  }
  ImGui::SetNextWindowPos(ImVec2(b.x, a.y - ImGui::GetStyle().WindowPadding.y), ImGuiCond_Appearing);
  if (ImGui::BeginPopup("##recent"))
  {
    if ((recentclose) || (filemenuclose))
    {
      recentclose = false;
      ImGui::CloseCurrentPopup();
      ImGui::EndPopup();
      return;
    }
    for (c = 0; c < n; c++)
    {
      const char *path = gui_recentfile(c);

      ImGui::PushID(c);
      if (ImGui::MenuItem(gt_basename(path)))
      {
        cmd_postfile(GTCMD_OPENFILE, path);
        // The LOAD dropdown closes too
        filemenuclose = true;
      }
      if (ImGui::IsMouseHoveringRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), false))
      {
        filemenuhover = GTCMD_OPENFILE;
        snprintf(filemenupath, sizeof filemenupath, "%s", path);
        if (!ImGui::IsItemHovered())
        {
          ImVec4 col = ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered);
          col.w *= 0.6f;
          ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImGui::GetColorU32(col));
        }
        else ImGui::SetTooltip("%s", path);
      }
      ImGui::PopID();
    }
    ImGui::EndPopup();
  }
}

static void filemenu(void)
{
  char id[24];

  // Each menu its own popup: opening one closes another one open
  if (wantfilemenu >= 0)
  {
    filemenuwhich = wantfilemenu;
    wantfilemenu = -1;
    snprintf(id, sizeof id, "##filemenu%d", filemenuwhich);
    ImGui::OpenPopup(id);
  }
  snprintf(id, sizeof id, "##filemenu%d", filemenuwhich);
  // Under its title (the text screen's second row), the text about as tall
  // as the screen's characters
  ImVec2 cell0 = gui::retrocellpos(filemenucolumn - 1, 1);
  ImVec2 cell1 = gui::retrocellpos(filemenucolumn - 1, 2);
  float size = (cell1.y - cell0.y) * 0.8f;
  if (size < ImGui::GetFontSize()) size = ImGui::GetFontSize();
  ImGui::SetNextWindowPos(cell0, ImGuiCond_Appearing);
  ImGui::PushFont(NULL, size);
  bool open = ImGui::BeginPopup(id);
  ImGui::PopFont();
  filemenushown = open ? filemenuwhich : -1;
  filemenuhover = -1;
  if (!open) filemenuclose = false;
  if (open)
  {
    if (filemenuclose)
    {
      filemenuclose = false;
      filemenushown = -1;
      ImGui::CloseCurrentPopup();
      ImGui::EndPopup();
      return;
    }
    ImGui::PushFont(NULL, size);
    switch (filemenuwhich)
    {
      case 0:
      fmitem("Song...", "F10", GTCMD_OPEN);
      recentmenu();
      fmitem("Instrument...", NULL, GTCMD_LOADINSTR);
      ImGui::Separator();
      fmitem("Merge Song...", NULL, GTCMD_MERGE);
      break;

      case 1:
      fmitem("Song", "F11", GTCMD_SAVE);
      fmitem("Song As...", NULL, GTCMD_SAVEAS);
      fmitem("Instrument...", NULL, GTCMD_SAVEINSTR);
      break;

      case 2:
      fmitem("SID...", NULL, GTCMD_IMPORTSID);
      fmitem("MIDI...", NULL, GTCMD_IMPORTMIDI);
      fmitem("SID-Wizard (.swm)...", NULL, GTCMD_IMPORTSWM);
      fmitem("ProTracker (.mod)...", NULL, GTCMD_IMPORTMOD);
      fmitem("Fermatax (chipsynth C64 patch)...", NULL, GTCMD_IMPORTPATCH);
      break;

      case 3:
      fmitem("SID...", NULL, GTCMD_EXPORTSID);
      fmitem("MIDI...", NULL, GTCMD_EXPORTMIDI);
      fmitem("SID-Wizard (.swm)...", NULL, GTCMD_EXPORTSWM);
      fmitem("ProTracker (.mod)...", NULL, GTCMD_EXPORTMOD);
      fmitem("Fermatax (chipsynth C64 patch)...", NULL, GTCMD_EXPORTPATCH);
      break;

      case 4:
      fmitem("Undo", MENUCMD "+Z", GTCMD_UNDO, undoCanUndo() != 0);
      fmitem("Redo", MENUCMD "+SHIFT+Z", GTCMD_REDO, undoCanRedo() != 0);
      break;

      default:
      fmitem("Clear Track", NULL, GTCMD_CLEARTRACK);
      fmitem("Clear Pattern", NULL, GTCMD_CLEARPATTERNS);
      ImGui::Separator();
      fmitem("Clear All...", NULL, GTCMD_CLEARALL);
      break;
    }
    ImGui::PopFont();
    ImGui::EndPopup();
  }
}

// The song's own tempo (BPM) at the PAL / NTSC frame rate, as the title bar
// shows it without the host
extern "C" double retro_songbpm(void);

static void dawsyncdialog(void)
{
  if (wantdawsync)
  {
    ImGui::OpenPopup("##dawsync");
    wantdawsync = false;
  }
  if (beginmodal("##dawsync", ImGui::GetFontSize() * 30.0f))
  {
    const char *labels[] = { "Close" };
    bool sync = gtsync_enabled != 0;
    bool recordoff = gtsync_recordoff != 0;
    int startbar = gtsync_startbar;
    double hostbpm = gtsync_hostbpm();
    char text[256];

    title("DAW Sync");
    if (ImGui::Checkbox("Follow the DAW's tempo and position", &sync)) gtsync_enabled = sync ? 1 : 0;
    snprintf(text, sizeof text, "While the DAW plays, the song plays from the DAW's position at its tempo: one pattern "
      "row = 1/%d beat (the highlight step). The DAW's start, stop and loop control the playback.", stepsize > 0 ? stepsize : 4);
    body(text);
    ImGui::Spacing();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
    if (ImGui::InputInt("The song starts at bar", &startbar))
    {
      if (startbar < 1) startbar = 1;
      if (startbar > 9999) startbar = 9999;
      gtsync_startbar = startbar;
    }
    ImGui::Spacing();
    body("MIDI INPUT (bottom of the screen): with MIDI INPUT 1, 2 or 3, MIDI channels 1, 2 and 3 play voices 1, 2 "
      "and 3, all three at once, each on 1-3 PATTERNS of its own (on 2 or 3 its notes are shared out to the next "
      "voices too, so one DAW track can play chords); other MIDI channels go to the cursor's voice. MIDI CURSOR "
      "sends every MIDI channel to the cursor's voice (or 2-3 from it). In edit mode the notes are recorded into the "
      "patterns while the DAW plays; stopped they are entered at the cursor's row. In jam mode they are only played. "
      "The middle mouse button and NUMPAD . switch between edit and jam mode. The sustain pedal (CC 64) works as on "
      "a synth: while it is down the notes of the keys let go keep sounding, until it comes up (with Record "
      "note-offs the --- goes where the pedal came up).");
    if (ImGui::Checkbox("Record note-offs (as ---)", &recordoff)) gtsync_recordoff = recordoff ? 1 : 0;
    ImGui::Spacing();
    if (hostbpm > 0.0)
      snprintf(text, sizeof text, "DAW: %.2f BPM, %s. The song's own tempo: %.1f BPM (the DAW at this tempo plays it "
        "exactly as GoatTracker does).", hostbpm, gtsync_hostplaying() ? "playing" : "stopped", retro_songbpm());
    else
      snprintf(text, sizeof text, "The DAW gives no tempo. The song's own tempo: %.1f BPM.", retro_songbpm());
    body(text);
    if (buttons(labels, 1, 0) >= 0) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
}
#endif

void gui::drawdialogs(void)
{
#ifdef GT_PLUGIN
  dawsyncdialog();
  filemenu();
#endif
  viewmenu();
  patternmenu();
#ifndef GT_PLUGIN
  preferencesdialog();
#endif
  aboutdialog();
  metercolorsdialog();
  exportdialog();
  sidimportdialog();
  midiimportdialog();
  modimportdialog();
  // One at a time: the first one wanted opens
  savedialog();
  quitdialog();
  cleardialog();
  hrdialog();
  alertdialog();
}

/*
 * The startup window: modern or retro interface
 */

// Small picture of the modern interface: bars, panels, arrangement regions
// and the pattern grid
static void previewmodern(ImDrawList *dl, ImVec2 a, ImVec2 b)
{
  float w = b.x - a.x;
  float h = b.y - a.y;
  float barh = h * 0.12f;
  float leftw = w * 0.22f;
  float rightw = w * 0.24f;
  float arrh = h * 0.30f;
  ImVec2 c0(a.x + leftw + 3.0f, a.y + barh + 3.0f);
  ImVec2 c1(b.x - rightw - 3.0f, b.y - 3.0f);
  int c, r;

  dl->AddRectFilled(a, b, theme.bgdeep, 6.0f);
  dl->AddRectFilled(a, ImVec2(b.x, a.y + barh), theme.bgraised, 6.0f, ImDrawFlags_RoundCornersTop);
  dl->AddRectFilled(ImVec2(a.x + w * 0.40f, a.y + barh * 0.22f), ImVec2(a.x + w * 0.62f, a.y + barh * 0.78f), theme.lcdbg, 2.0f);
  dl->AddCircleFilled(ImVec2(a.x + barh * 0.6f, a.y + barh * 0.5f), barh * 0.22f, theme.text);
  dl->AddCircleFilled(ImVec2(a.x + barh * 1.4f, a.y + barh * 0.5f), barh * 0.22f, theme.record);
  // Library / inspector and tables / song panels
  dl->AddRectFilled(ImVec2(a.x + 3.0f, a.y + barh + 3.0f), ImVec2(a.x + leftw, b.y - 3.0f), theme.bgpanel, 3.0f);
  dl->AddRectFilled(ImVec2(b.x - rightw, a.y + barh + 3.0f), ImVec2(b.x - 3.0f, b.y - 3.0f), theme.bgpanel, 3.0f);
  for (r = 0; r < 6; r++)
  {
    float y = a.y + barh + 10.0f + r * h * 0.07f;
    dl->AddRectFilled(ImVec2(a.x + 8.0f, y), ImVec2(a.x + leftw - 8.0f - (r % 3) * 6.0f, y + 3.0f), theme.textfaint, 1.0f);
    dl->AddRectFilled(ImVec2(b.x - rightw + 6.0f, y), ImVec2(b.x - 8.0f - (r % 2) * 10.0f, y + 3.0f), theme.textfaint, 1.0f);
  }
  // Arrangement: a region strip for each voice
  dl->AddRectFilled(c0, ImVec2(c1.x, c0.y + arrh), theme.bgpanel, 3.0f);
  for (c = 0; c < MAX_CHN; c++)
  {
    float y = c0.y + 5.0f + c * (arrh - 10.0f) / MAX_CHN;
    float rh = (arrh - 10.0f) / MAX_CHN - 3.0f;
    for (r = 0; r < 4; r++)
    {
      float x0 = c0.x + 5.0f + r * (c1.x - c0.x - 10.0f) / 4.0f;
      float x1 = x0 + (c1.x - c0.x - 10.0f) / 4.0f - 2.0f;
      dl->AddRectFilled(ImVec2(x0, y), ImVec2(x1, y + rh), alpha(theme.chn[c], 0.75f), 2.0f);
    }
  }
  // Pattern editor grid
  {
    ImVec2 p0(c0.x, c0.y + arrh + 3.0f);
    dl->AddRectFilled(p0, c1, theme.bgfield, 3.0f);
    for (r = 0; r < 9; r++)
    {
      float y = p0.y + 6.0f + r * (c1.y - p0.y - 12.0f) / 9.0f;
      if (r == 4) dl->AddRectFilled(ImVec2(p0.x + 2.0f, y - 2.0f), ImVec2(c1.x - 2.0f, y + 5.0f), theme.accentsoft);
      for (c = 0; c < MAX_CHN; c++)
      {
        float x = p0.x + 6.0f + c * (c1.x - p0.x - 12.0f) / MAX_CHN;
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + (c1.x - p0.x) * 0.16f, y + 3.0f), (r % 2) ? theme.textfaint : theme.note, 1.0f);
      }
    }
  }
}

// Small picture of the retro interface: the classic text screen in its own
// colors
static void previewretro(ImDrawList *dl, ImVec2 a, ImVec2 b)
{
  static const char *const lines[] =
  {
    "GoatTracker Ultimate",
    "",
    "CHN1 PATT.00  CHN2 PATT.01",
    "00 C-4 01 000 00 ... 00 000",
    "01 ... 00 000 01 G-3 02 000",
    "02 D#4 01 000 02 ... 00 000",
    "03 ... 00 000 03 ... 00 000",
    "04 F-4 01 304 04 C-3 02 000",
    "05 ... 00 000 05 ... 00 000",
    "06 G-4 01 000 06 ... 00 000",
  };
  int count = (int)(sizeof lines / sizeof lines[0]);
  float lineh = (b.y - a.y - 8.0f) / count;
  float size = lineh * 0.82f;
  int c;

  dl->AddRectFilled(a, b, IM_COL32(0, 0, 0, 255), 6.0f);
  // Title bar
  dl->AddRectFilled(ImVec2(a.x, a.y), ImVec2(b.x, a.y + 4.0f + lineh), retrocolor(1), 6.0f, ImDrawFlags_RoundCornersTop);
  dl->PushClipRect(a, b, true);
  for (c = 0; c < count; c++)
  {
    ImU32 col = retrocolor(CNORMAL);
    ImVec2 p(a.x + 6.0f, a.y + 4.0f + c * lineh);
    if ((c == 0) || (c == 2)) col = retrocolor(CTITLE);
    if (c == 5)
    {
      // Edit cursor row
      dl->AddRectFilled(ImVec2(a.x + 2.0f, p.y), ImVec2(b.x - 2.0f, p.y + lineh), retrocolor(1));
      col = retrocolor(CEDIT);
    }
    if (c == 7) col = retrocolor(CPLAYING);
    dl->AddText(fontmono, size, p, col, lines[c]);
  }
  dl->PopClipRect();
}

int gui::drawchooser(int *selected)
{
  ImGuiViewport *vp = ImGui::GetMainViewport();
  ImDrawList *dl;
  float fs = ImGui::GetFontSize();
  float cardw = fs * 19.0f;
  float cardh = fs * 17.0f;
  float gap = fs * 1.5f;
  float totalw = cardw * 2.0f + gap;
  float totalh = cardh + fs * 10.0f;
  float left, top;
  int result = UI_ASK;
  int c;
  static const char *const names[2] = { "Modern", "Retro" };
  static const char *const texts[2] =
  {
    "Panels you can arrange, the song as regions on a timeline, mouse-friendly editors and dialogs.",
    "The classic GoatTracker screen: 100 x 37 characters in the original font and colors, keyboard first.",
  };
  static const char *const keys[2] = { "M", "R" };

  ImGui::SetNextWindowPos(vp->Pos);
  ImGui::SetNextWindowSize(vp->Size);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::ColorConvertU32ToFloat4(theme.bgdeep));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::Begin("##chooser", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
    ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav);
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor();
  dl = ImGui::GetWindowDrawList();

  left = (vp->Size.x - totalw) * 0.5f;
  top = (vp->Size.y - totalh) * 0.5f;
  if (left < fs) left = fs;
  if (top < fs) top = fs;

  // Heading
  ImGui::SetCursorPos(ImVec2(left, top));
  ImGui::PushFont(fontbold, ImGui::GetStyle().FontSizeBase * 1.7f);
  ImGui::TextUnformatted("GoatTracker Ultimate");
  ImGui::PopFont();
  ImGui::SetCursorPosX(left);
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(theme.textdim));
  ImGui::TextUnformatted("Choose the interface. Both edit the same songs with the same keys.");
  ImGui::PopStyleColor();
  ImGui::Dummy(ImVec2(0.0f, fs * 0.8f));

  // The two choices
  {
    float y = ImGui::GetCursorPosY();
    for (c = 0; c < 2; c++)
    {
      int choice = c ? UI_RETRO : UI_MODERN;
      bool sel = (*selected == choice);
      ImVec2 a, b, pa, pb;
      ImU32 border;

      ImGui::SetCursorPos(ImVec2(left + c * (cardw + gap), y));
      ImGui::PushID(c);
      if (ImGui::InvisibleButton("choice", ImVec2(cardw, cardh)))
      {
        *selected = choice;
        result = choice;
      }
      if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
      a = ImGui::GetItemRectMin();
      b = ImGui::GetItemRectMax();
      border = sel ? theme.accent : (ImGui::IsItemHovered() ? theme.textfaint : theme.border);
      dl->AddRectFilled(a, b, ImGui::IsItemHovered() ? mix(theme.bgpanel, theme.bgraised, 0.5f) : theme.bgpanel, 10.0f);
      dl->AddRect(a, b, border, 10.0f, 0, sel ? 2.5f : 1.0f);

      pa = ImVec2(a.x + fs, a.y + fs);
      pb = ImVec2(b.x - fs, a.y + cardh * 0.58f);
      if (c) previewretro(dl, pa, pb);
      else previewmodern(dl, pa, pb);

      dl->AddText(fontbold, fs * 1.25f, ImVec2(a.x + fs, pb.y + fs * 0.7f), theme.text, names[c]);
      {
        char hint[16];
        ImVec2 ks;
        snprintf(hint, sizeof hint, "%s", keys[c]);
        ks = fontui->CalcTextSizeA(fs, FLT_MAX, 0.0f, hint);
        dl->AddRect(ImVec2(b.x - fs - ks.x - 10.0f, pb.y + fs * 0.75f), ImVec2(b.x - fs, pb.y + fs * 0.75f + ks.y + 4.0f), theme.textfaint, 4.0f);
        dl->AddText(fontui, fs, ImVec2(b.x - fs - ks.x - 5.0f, pb.y + fs * 0.75f + 2.0f), theme.textdim, hint);
      }
      dl->AddText(fontui, fs, ImVec2(a.x + fs, pb.y + fs * 2.4f), theme.textdim, texts[c], NULL, cardw - fs * 2.0f);
      ImGui::PopID();
    }
    ImGui::SetCursorPos(ImVec2(left, y + cardh + fs * 1.2f));
  }

  // The choice is for this time only
  ImGui::SetCursorPosX(left);
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(theme.textfaint));
#ifdef __APPLE__
  ImGui::TextUnformatted("For this time only: GoatTracker Ultimate starts retro unless Shift or Option is held.");
#else
  ImGui::TextUnformatted("For this time only: GoatTracker Ultimate starts retro unless Shift is held.");
#endif
  ImGui::PopStyleColor();

  // Continue button
  ImGui::SameLine();
  {
    const char *label = "Continue";
    float bw = ImGui::CalcTextSize(label).x + 40.0f;
    ImGui::SetCursorPosX(left + totalw - bw);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - fs * 0.6f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(theme.accent));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::ColorConvertU32ToFloat4(mix(theme.accent, IM_COL32(255, 255, 255, 255), 0.15f)));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::ColorConvertU32ToFloat4(mix(theme.accent, IM_COL32(0, 0, 0, 255), 0.15f)));
    if (ImGui::Button(label, ImVec2(bw, 0.0f))) result = *selected;
    ImGui::PopStyleColor(3);
  }

  // Keyboard: arrows choose, Return continues, M / R pick directly
  if ((ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) || (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) || (ImGui::IsKeyPressed(ImGuiKey_Tab)))
    *selected = (*selected == UI_RETRO) ? UI_MODERN : UI_RETRO;
  if ((ImGui::IsKeyPressed(ImGuiKey_Enter, false)) || (ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)))
    result = *selected;
  if (ImGui::IsKeyPressed(ImGuiKey_M, false)) result = *selected = UI_MODERN;
  if (ImGui::IsKeyPressed(ImGuiKey_R, false)) result = *selected = UI_RETRO;

  ImGui::End();
  return result;
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
