// GoatTracker GUI: control bar (transport, display, settings), status bar
// and the in-window menu bar used on Windows and Linux

#include "gui_internal.h"

using namespace gui;

static void editorsetting(unsigned *value, unsigned newvalue, bool reinitsound)
{
  if (*value == newvalue) return;
  undoCreateEditorInfoBackup();
  *value = newvalue;
  undoAddEditorSettingsToList();
  if (reinitsound) do_sound_init();
}

// One labelled value on the display
static void lcdfield(ImDrawList *dl, ImVec2 p, const char *label, const char *value, float bigsize, ImU32 col)
{
  dl->AddText(fontbold, ImGui::GetFontSize() * 0.68f, p, theme.lcddim, label);
  dl->AddText(fontmono, bigsize, ImVec2(p.x, p.y + ImGui::GetFontSize() * 0.78f), col, value);
}

static float lcdwidth(const char *value, float bigsize)
{
  return fontmono->CalcTextSizeA(bigsize, FLT_MAX, 0.0f, value).x;
}

// A small arrow button on the display. Returns -1, 0 or 1
static int lcdarrows(const char *id, ImVec2 p, float h)
{
  int result = 0;
  ImVec2 size(h * 0.9f, h * 0.55f);

  ImGui::SetCursorScreenPos(p);
  ImGui::PushID(id);
  if (iconbutton("up", ICON_UP, false, 0, size, NULL)) result = 1;
  ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + size.y));
  if (iconbutton("down", ICON_DOWN, false, 0, size, NULL)) result = -1;
  ImGui::PopID();
  return result;
}

void gui::drawcontrolbar(float height)
{
  ImDrawList *dl = ImGui::GetWindowDrawList();
  ImVec2 origin = ImGui::GetWindowPos();
  float fs = ImGui::GetFontSize();
  float bh = height - 18.0f;
  float bw = bh * 1.15f;
  float x = origin.x + 10.0f;
  float y = origin.y + (height - bh) * 0.5f;
  bool playing = isplaying() != 0;
  char text[64];

  // Bottom edge
  dl->AddLine(ImVec2(origin.x, origin.y + height - 1.0f), ImVec2(origin.x + ImGui::GetWindowWidth(), origin.y + height - 1.0f), theme.border);

  // Transport
  ImGui::SetCursorScreenPos(ImVec2(x, y));
  if (iconbutton("##playsong", ICON_REWIND, playing && (lastsonginit == PLAY_BEGINNING), theme.chn[0], ImVec2(bw, bh), "Play from the beginning (F1)"))
    cmd_post(GTCMD_PLAYSONG);
  x += bw + 4.0f;
  ImGui::SetCursorScreenPos(ImVec2(x, y));
  if (iconbutton("##playpos", ICON_PLAY, playing && (lastsonginit == PLAY_POS), theme.chn[0], ImVec2(bw, bh), "Play from the current position (F2)\nSPACE in the pattern editor plays from the cursor"))
    cmd_post(GTCMD_PLAYPOS);
  x += bw + 4.0f;
  ImGui::SetCursorScreenPos(ImVec2(x, y));
  if (iconbutton("##playpatt", ICON_PLAYPATTERN, playing && (lastsonginit == PLAY_PATTERN), theme.chn[0], ImVec2(bw, bh), "Play the current pattern (F3)"))
    cmd_post(GTCMD_PLAYPATTERN);
  x += bw + 4.0f;
  ImGui::SetCursorScreenPos(ImVec2(x, y));
  if (iconbutton("##stop", ICON_STOP, false, 0, ImVec2(bw, bh), "Stop (F4)"))
    cmd_post(GTCMD_STOP);
  x += bw + 14.0f;
  ImGui::SetCursorScreenPos(ImVec2(x, y));
  if (iconbutton("##record", ICON_RECORD, recordmode != 0, theme.record, ImVec2(bw, bh),
    recordmode ? "Edit mode: keys enter notes into the pattern (CMD+R switches to jam mode)" :
                 "Jam mode: keys only play notes (CMD+R switches to edit mode)"))
    cmd_post(GTCMD_RECORD);
  x += bw + 4.0f;
  ImGui::SetCursorScreenPos(ImVec2(x, y));
  if (iconbutton("##follow", ICON_FOLLOW, followmode && !followsuspended, theme.accent, ImVec2(bw, bh),
    followsuspended ? "Follow playback is paused because you scrolled. Click to follow again" :
                      "Follow playback (CMD+F): the editors move with the song"))
  {
    if (followsuspended)
    {
      followsuspended = 0;
      if (!followplay) cmd_post(GTCMD_FOLLOW);
    }
    else cmd_post(GTCMD_FOLLOW);
  }
  x += bw + 4.0f;
  ImGui::SetCursorScreenPos(ImVec2(x, y));
#ifdef __APPLE__
  if (iconbutton("##designer", ICON_DESIGNER, false, 0, ImVec2(bw, bh), "Instrument designer: a C64 screen laid out like chipsynth C64 (CMD+D, OPTION+numpad 5)"))
#else
  if (iconbutton("##designer", ICON_DESIGNER, false, 0, ImVec2(bw, bh), "Instrument designer: a C64 screen laid out like chipsynth C64 (CTRL+D, ALT+numpad 5)"))
#endif
    cmd_post(GTCMD_DESIGNER);
  x += bw + 12.0f;

  // MIDI chord input: notes held together on the MIDI keyboard go to 1-3
  // tracks. Click = more, right-click = fewer
  ImGui::SetCursorScreenPos(ImVec2(x, y + (bh - (fs + 4.0f)) * 0.5f));
  snprintf(text, sizeof text, "CHORD %d##midichord", midichord);
  if (togglechip(text, midichord > 1,
    midichord > 1 ? "MIDI chords: the notes you hold down together on the MIDI keyboard go to this many tracks, "
                    "starting at the cursor's track, all on one row. Click for more, right-click for fewer" :
                    "MIDI chords off: every MIDI note goes to the cursor's track. Click to send the notes of a chord "
                    "to 2 or 3 tracks, right-click for fewer"))
    setmidichord(midichord < MAX_CHN ? midichord + 1 : 1);
  if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
    setmidichord(midichord > 1 ? midichord - 1 : MAX_CHN);
  x += ImGui::GetItemRectSize().x + 16.0f;

  // The display
  {
    float lcdh = height - 12.0f;
    float bigsize = fs * 1.55f;
    float lcdx = x;
    float lcdy = origin.y + 6.0f;
    float fx;
    int pos, row, c;
    int playchn = 0;
    ImU32 col = theme.lcdtext;
    float lcdw;
    char timestr[16];

    if (playing)
    {
      pos = chn[playchn].songptr - 1;
      if (pos < 0) pos = 0;
      row = chn[playchn].pattptr / 4;
      if (row > pattlen[chn[playchn].pattnum]) row = pattlen[chn[playchn].pattnum];
    }
    else
    {
      pos = editorInfo.eseditpos;
      row = editorInfo.eppos;
    }
    snprintf(timestr, sizeof timestr, "%02d:%02d", timemin, timesec);

    lcdw = lcdwidth("00:00", bigsize) + lcdwidth("000", bigsize) * 2.0f + lcdwidth("00", bigsize) * 3.0f + fs * 13.0f;
    dl->AddRectFilled(ImVec2(lcdx, lcdy), ImVec2(lcdx + lcdw, lcdy + lcdh), theme.lcdbg, 7.0f);
    dl->AddRect(ImVec2(lcdx, lcdy), ImVec2(lcdx + lcdw, lcdy + lcdh), IM_COL32(255, 255, 255, 14), 7.0f);

    fx = lcdx + fs * 0.9f;
    float fy = lcdy + (lcdh - (fs * 0.78f + bigsize)) * 0.5f;
    lcdfield(dl, ImVec2(fx, fy), "TIME", timestr, bigsize, playing ? theme.playhead : col);
    fx += lcdwidth("00:00", bigsize) + fs * 1.4f;
    snprintf(text, sizeof text, "%03d", pos);
    lcdfield(dl, ImVec2(fx, fy), "POSITION", text, bigsize, col);
    fx += lcdwidth("000", bigsize) + fs * 1.4f;
    snprintf(text, sizeof text, (patterndispmode & 1) ? "%02X" : "%03d", row);
    lcdfield(dl, ImVec2(fx, fy), "ROW", text, bigsize, col);
    fx += lcdwidth("000", bigsize) + fs * 1.4f;

    snprintf(text, sizeof text, "%02X", editorInfo.esnum);
    lcdfield(dl, ImVec2(fx, fy), "SUBTUNE", text, bigsize, col);
    c = lcdarrows("##subtune", ImVec2(fx + lcdwidth("00", bigsize) + 4.0f, fy + fs * 0.7f), bigsize);
    if (c > 0) cmd_post(GTCMD_NEXTSUBTUNE);
    if (c < 0) cmd_post(GTCMD_PREVSUBTUNE);
    fx += lcdwidth("00", bigsize) + fs * 2.6f;

    snprintf(text, sizeof text, "%d", editorInfo.epoctave);
    lcdfield(dl, ImVec2(fx, fy), "OCTAVE", text, bigsize, col);
    c = lcdarrows("##octave", ImVec2(fx + lcdwidth("0", bigsize) + 4.0f, fy + fs * 0.7f), bigsize);
    if (c > 0) cmd_post(GTCMD_OCTAVEUP);
    if (c < 0) cmd_post(GTCMD_OCTAVEDOWN);
    fx += lcdwidth("00", bigsize) + fs * 2.6f;

    snprintf(text, sizeof text, "%02X", editorInfo.einum);
    lcdfield(dl, ImVec2(fx, fy), "INSTR", text, bigsize, theme.instr);
    fx += lcdwidth("00", bigsize) + fs * 0.6f;
    if (instr[editorInfo.einum].name[0])
    {
      char name[MAX_INSTRNAMELEN + 1];
      memcpy(name, instr[editorInfo.einum].name, MAX_INSTRNAMELEN);
      name[MAX_INSTRNAMELEN] = 0;
      dl->AddText(fontui, fs * 0.85f, ImVec2(fx, fy + fs * 0.78f + (bigsize - fs * 0.85f) * 0.5f), theme.lcddim, name);
    }
    x = lcdx + lcdw + 16.0f;
  }

  // Settings on the right
  {
    float right = origin.x + ImGui::GetWindowWidth() - 10.0f;
    float meterw = fs * 5.0f;
    float rowy1 = origin.y + 8.0f;
    float rowy2 = origin.y + height * 0.5f + 1.0f;
    float sx;
    int v;

    // Voice meters
    sx = right - meterw;
    for (v = 0; v < MAX_CHN; v++)
    {
      float my = rowy1 + v * (height - 16.0f) / 3.0f;
      float mh = (height - 16.0f) / 3.0f - 4.0f;
      float level = chn[v].mute ? 0.0f : voicelevel[v];
      dl->AddRectFilled(ImVec2(sx, my), ImVec2(sx + meterw, my + mh), IM_COL32(16, 17, 19, 255), 2.0f);
      if (level > 0.0f)
        dl->AddRectFilled(ImVec2(sx, my), ImVec2(sx + meterw * level, my + mh), chn[v].mute ? theme.textfaint : theme.chn[v], 2.0f);
    }
    ImGui::SetCursorScreenPos(ImVec2(sx, rowy1));
    ImGui::InvisibleButton("##meters", ImVec2(meterw, height - 16.0f));
    if (ImGui::IsItemHovered()) tooltip("Voice activity (estimated from the SID envelopes)");

    // Engine settings, two rows
    sx -= 12.0f;
    {
      const char *labels[] = { editorInfo.ntsc ? "NTSC" : "PAL", editorInfo.sidmodel ? "8580" : "6581" };
      float w0 = ImGui::CalcTextSize("NTSC").x + 16.0f;
      float w1 = ImGui::CalcTextSize("8580").x + 16.0f;
      float bx = sx - w0 - w1 - 6.0f;

      ImGui::SetCursorScreenPos(ImVec2(bx, rowy1));
      if (ImGui::Button(labels[0], ImVec2(w0, 0))) editorsetting(&editorInfo.ntsc, editorInfo.ntsc ^ 1, true);
      if (ImGui::IsItemHovered()) tooltip("Timing: PAL (50 Hz) or NTSC (60 Hz). Click to switch");
      ImGui::SameLine(0.0f, 6.0f);
      if (ImGui::Button(labels[1], ImVec2(w1, 0))) editorsetting(&editorInfo.sidmodel, editorInfo.sidmodel ^ 1, true);
      if (ImGui::IsItemHovered()) tooltip("Emulated SID chip: 6581 or 8580 (SHIFT+F8). Click to switch");

      ImGui::SetCursorScreenPos(ImVec2(bx, rowy2));
      if (editorInfo.multiplier) snprintf(text, sizeof text, "%ux", editorInfo.multiplier);
      else snprintf(text, sizeof text, "25Hz");
      ImGui::SetNextItemWidth(w0);
      if (ImGui::BeginCombo("##speed", text, ImGuiComboFlags_NoArrowButton))
      {
        unsigned m;
        for (m = 0; m <= 16; m++)
        {
          char item[16];
          if (m) snprintf(item, sizeof item, "%ux speed", m);
          else snprintf(item, sizeof item, "25 Hz");
          if (ImGui::Selectable(item, editorInfo.multiplier == m))
            editorsetting(&editorInfo.multiplier, m, true);
        }
        ImGui::EndCombo();
      }
      if (ImGui::IsItemHovered()) tooltip("Speed multiplier: how many times per frame the player runs (SHIFT+F5/F6)");
      ImGui::SameLine(0.0f, 6.0f);
      snprintf(text, sizeof text, "HR %04X", editorInfo.adparam);
      if (ImGui::Button(text, ImVec2(w1 + 26.0f, 0))) gui_edithradsr();
      if (ImGui::IsItemHovered()) tooltip("Hard restart ADSR (SHIFT+F7). Click to edit");

      // reSID options
      bx -= 12.0f;
      {
        float cw = ImGui::CalcTextSize("INT").x + 16.0f;
        ImGui::SetCursorScreenPos(ImVec2(bx - cw, rowy1));
        if (togglechip("INT##int", (interpolate & 1) != 0, "reSID interpolation (better sound quality)"))
        {
          interpolate ^= 1;
          interpolate &= 1 + (interpolate << 1);
          stopsong();
          do_sound_init();
        }
        ImGui::SetCursorScreenPos(ImVec2(bx - cw, rowy2));
        if (togglechip("FP##fp", (interpolate & 2) != 0, "reSID-fp: distortion / nonlinearity of the filter (SHIFT+F9)"))
        {
          interpolate ^= 2;
          interpolate |= interpolate >> 1;
          stopsong();
          do_sound_init();
        }
      }
    }
  }
}

void gui::drawstatusbar(float height)
{
  ImDrawList *dl = ImGui::GetWindowDrawList();
  ImVec2 origin = ImGui::GetWindowPos();
  float w = ImGui::GetWindowWidth();
  float fs = ImGui::GetFontSize();
  float ty = origin.y + (height - fs) * 0.5f;
  float x = origin.x + 10.0f;
  const char *modename = "";
  char text[300];
  int modified = gui_songmodified();

  dl->AddLine(origin, ImVec2(origin.x + w, origin.y), theme.border);

  switch (editorInfo.editmode)
  {
    case EDIT_PATTERN: modename = "Pattern Editor"; break;
    case EDIT_ORDERLIST: modename = "Arrangement"; break;
    case EDIT_INSTRUMENT: modename = "Instrument"; break;
    case EDIT_TABLES: modename = "Tables"; break;
    case EDIT_NAMES: modename = "Song Info"; break;
  }

  // Keyboard focus, edit/jam mode
  dl->AddCircleFilled(ImVec2(x + 4.0f, ty + fs * 0.5f), 4.0f, recordmode ? theme.record : theme.textfaint, 12);
  x += 14.0f;
  snprintf(text, sizeof text, "%s  ·  %s", recordmode ? "EDIT" : "JAM", modename);
  dl->AddText(fontbold, fs * 0.9f, ImVec2(x, ty), theme.textdim, text);
  x += fontbold->CalcTextSizeA(fs * 0.9f, FLT_MAX, 0.0f, text).x + 18.0f;

  if (autoadvance)
  {
    snprintf(text, sizeof text, "Auto-advance %d", autoadvance);
    dl->AddText(ImVec2(x, ty), theme.warning, text);
    x += ImGui::CalcTextSize(text).x + 18.0f;
  }
  if (!editorInfo.etlock)
  {
    dl->AddText(ImVec2(x, ty), theme.warning, "Tables unlocked");
    x += ImGui::CalcTextSize("Tables unlocked").x + 18.0f;
  }

  // Message (fades out)
  {
    double age = ImGui::GetTime() - statusmessagetime;
    if ((statusmessage[0]) && (age < 8.0))
    {
      float a = (age < 6.0) ? 1.0f : (float)(1.0 - (age - 6.0) / 2.0);
      dl->AddText(ImVec2(x, ty), alpha(theme.text, a), statusmessage);
    }
  }

  // File name on the right
  {
    const char *name = loadedsongfilename[0] ? gt_basename(loadedsongfilename) : "Untitled";
    float tw;
    int devices = midi_devicecount();

    if ((midiinput) && (devices))
      snprintf(text, sizeof text, "MIDI in: %d     %s%s", devices, name, modified ? "  (edited)" : "");
    else
      snprintf(text, sizeof text, "%s%s", name, modified ? "  (edited)" : "");
    tw = ImGui::CalcTextSize(text).x;
    if (modified) dl->AddCircleFilled(ImVec2(origin.x + w - tw - 22.0f, ty + fs * 0.5f), 3.5f, theme.text, 12);
    dl->AddText(ImVec2(origin.x + w - tw - 12.0f, ty), theme.textdim, text);
  }
}

#ifndef __APPLE__
// Menu bar inside the window (on macOS the system menu bar is used)
void gui::drawmenubar(void)
{
  if (!ImGui::BeginMainMenuBar()) return;

  if (ImGui::BeginMenu("File"))
  {
    if (ImGui::MenuItem("New Song")) cmd_post(GTCMD_NEW);
    if (ImGui::MenuItem("Open...", "F10")) cmd_post(GTCMD_OPEN);
    if (ImGui::BeginMenu("Open Recent", recentcount() > 0))
    {
      int c;
      for (c = 0; c < recentcount(); c++)
      {
        const char *path = recentpath(c);
        ImGui::PushID(c);
        if (ImGui::MenuItem(gt_basename(path))) cmd_postfile(GTCMD_OPENFILE, path);
        if (ImGui::IsItemHovered()) tooltip(path);
        ImGui::PopID();
      }
      ImGui::EndMenu();
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Save", "F11")) cmd_post(GTCMD_SAVE);
    if (ImGui::MenuItem("Save As...")) cmd_post(GTCMD_SAVEAS);
    ImGui::Separator();
    if (ImGui::MenuItem("Merge Song...", "SHIFT+F10")) cmd_post(GTCMD_MERGE);
    if (ImGui::BeginMenu("Import"))
    {
      if (ImGui::MenuItem("SID File...")) cmd_post(GTCMD_IMPORTSID);
      if (ImGui::MenuItem("MIDI File...")) cmd_post(GTCMD_IMPORTMIDI);
      if (ImGui::MenuItem("chipsynth C64 Patch (.fermatax)...")) cmd_post(GTCMD_IMPORTPATCH);
      ImGui::EndMenu();
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Load Instrument...")) cmd_post(GTCMD_LOADINSTR);
    if (ImGui::MenuItem("Save Instrument...")) cmd_post(GTCMD_SAVEINSTR);
    ImGui::Separator();
    if (ImGui::MenuItem("Export SID / PRG / BIN...", "F9")) cmd_post(GTCMD_EXPORT);
    ImGui::Separator();
    if (ImGui::MenuItem("Quit", "ESC")) cmd_post(GTCMD_QUIT);
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Edit"))
  {
    if (ImGui::MenuItem("Undo", "CTRL+Z")) cmd_post(GTCMD_UNDO);
    ImGui::Separator();
    if (ImGui::MenuItem("Cut", "SHIFT+X")) cmd_post(GTCMD_CUT);
    if (ImGui::MenuItem("Copy", "SHIFT+C")) cmd_post(GTCMD_COPY);
    if (ImGui::MenuItem("Paste", "SHIFT+V")) cmd_post(GTCMD_PASTE);
    if (ImGui::MenuItem("Delete", "DEL")) cmd_post(GTCMD_DELETE);
    if (ImGui::MenuItem("Select All", "SHIFT+L")) cmd_post(GTCMD_SELECTALL);
    ImGui::Separator();
    if (ImGui::MenuItem("Transpose Up", "SHIFT+Q")) cmd_post(GTCMD_TRANSPOSEUP);
    if (ImGui::MenuItem("Transpose Down", "SHIFT+A")) cmd_post(GTCMD_TRANSPOSEDOWN);
    if (ImGui::MenuItem("Transpose Octave Up", "SHIFT+W")) cmd_post(GTCMD_TRANSPOSEOCTUP);
    if (ImGui::MenuItem("Transpose Octave Down", "SHIFT+S")) cmd_post(GTCMD_TRANSPOSEOCTDOWN);
    ImGui::Separator();
    if (ImGui::MenuItem("Hard Restart ADSR...", "SHIFT+F7")) cmd_post(GTCMD_HRADSR);
    if (ImGui::MenuItem("Clear / Optimize Song...", "SHIFT+ESC")) cmd_post(GTCMD_CLEARSONG);
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Control"))
  {
    if (ImGui::MenuItem("Play / Stop", "SPACE")) cmd_post(GTCMD_PLAYSTOP);
    if (ImGui::MenuItem("Play From Beginning", "F1")) cmd_post(GTCMD_PLAYSONG);
    if (ImGui::MenuItem("Play From Current Position", "F2")) cmd_post(GTCMD_PLAYPOS);
    if (ImGui::MenuItem("Play Pattern", "F3")) cmd_post(GTCMD_PLAYPATTERN);
    if (ImGui::MenuItem("Stop", "F4")) cmd_post(GTCMD_STOP);
    ImGui::Separator();
    if (ImGui::MenuItem("Record (Edit Mode)", NULL, recordmode != 0)) cmd_post(GTCMD_RECORD);
    if (ImGui::MenuItem("Follow Playback", NULL, followmode != 0)) cmd_post(GTCMD_FOLLOW);
    ImGui::Separator();
    if (ImGui::MenuItem("Pattern Editor", "F5")) cmd_post(GTCMD_EDITPATTERN);
    if (ImGui::MenuItem("Arrangement (Orderlist)", "F6")) cmd_post(GTCMD_EDITORDER);
    if (ImGui::MenuItem("Instrument / Tables", "F7")) cmd_post(GTCMD_EDITINSTR);
    if (ImGui::MenuItem("Song Info", "F8")) cmd_post(GTCMD_EDITNAMES);
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("View"))
  {
    if (ImGui::MenuItem("Instrument Designer", "CTRL+D")) cmd_post(GTCMD_DESIGNER);
    ImGui::Separator();
    ImGui::MenuItem("Library", NULL, &showlibrary);
    ImGui::MenuItem("Inspector", NULL, &showinspector);
    ImGui::MenuItem("Arrangement", NULL, &showtracks);
    ImGui::MenuItem("Tables", NULL, &showtables);
    ImGui::MenuItem("Song Info", NULL, &showsong);
    if (ImGui::MenuItem("Reset Layout")) cmd_post(GTCMD_RESETLAYOUT);
    ImGui::Separator();
    if (ImGui::MenuItem("Bigger Text", "CTRL+=")) cmd_post(GTCMD_ZOOMIN);
    if (ImGui::MenuItem("Smaller Text", "CTRL+-")) cmd_post(GTCMD_ZOOMOUT);
    if (ImGui::MenuItem("Default Text Size", "CTRL+0")) cmd_post(GTCMD_ZOOMRESET);
    if (ImGui::MenuItem("Full Screen", "ALT+ENTER")) cmd_post(GTCMD_FULLSCREEN);
    ImGui::Separator();
    if (ImGui::MenuItem("Ask for Interface at Next Launch")) cmd_post(GTCMD_ASKINTERFACE);
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Help"))
  {
    if (ImGui::MenuItem("GoatTracker Ultimate Help", "F12")) cmd_post(GTCMD_HELP);
    ImGui::EndMenu();
  }
  ImGui::EndMainMenuBar();
}
#else
void gui::drawmenubar(void)
{
}
#endif
