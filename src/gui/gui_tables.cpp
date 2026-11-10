// GoatTracker GUI: the wave, pulse, filter and speed tables

#include "gui_internal.h"

using namespace gui;

static const char *tablenames[MAX_TABLES] = { "Wave", "Pulse", "Filter", "Speed" };
// Character positions of the 4 editable digits in "NN  LL RR"
static const int digitchar[4] = { 4, 5, 7, 8 };

static int lastetpos = -1;
static int lastetnum = -1;
static bool tablesetbymouse = false;
static bool tabledragging = false;
static int tabledragnum = 0;
static int tabledragstart = 0;

// Explains a table row in words
static void decoderow(int t, int pos, char *out, int size)
{
  int l = ltable[t][pos];
  int r = rtable[t][pos];

  if (l == 0xff)
  {
    if (r) snprintf(out, size, "Jump to %02X", r);
    else snprintf(out, size, "Stop");
    return;
  }

  switch (t)
  {
    case WTBL:
    {
      char note[32];
      if (r < 0x60) snprintf(note, sizeof note, "note %+d", r);
      else if (r < 0x80) snprintf(note, sizeof note, "note %+d", r - 0x80);
      else if (r == 0x80) snprintf(note, sizeof note, "pitch unchanged");
      else if (r <= 0xdf) snprintf(note, sizeof note, "note %s", notename[r - 0x80]);
      else snprintf(note, sizeof note, "note %02X", r);

      if (l == 0) snprintf(out, size, "Waveform unchanged, %s", note);
      else if (l < 0x10) snprintf(out, size, "Wait %d frame%s, %s", l, l > 1 ? "s" : "", note);
      else if (l < 0xe0)
      {
        const char *w = "";
        switch (l & 0xf0)
        {
          case 0x10: w = "triangle"; break;
          case 0x20: w = "saw"; break;
          case 0x30: w = "tri+saw"; break;
          case 0x40: w = "pulse"; break;
          case 0x50: w = "tri+pulse"; break;
          case 0x60: w = "saw+pulse"; break;
          case 0x70: w = "tri+saw+pulse"; break;
          case 0x80: w = "noise"; break;
          default: w = "combined"; break;
        }
        snprintf(out, size, "Waveform %02X (%s%s%s%s), %s", l, w, (l & 1) ? ", gate" : "", (l & 2) ? ", sync" : "", (l & 4) ? ", ring" : "", note);
      }
      else if (l < 0xf0) snprintf(out, size, "Silent waveform %X, %s", l & 0x0f, note);
      else snprintf(out, size, "Pattern command %X with parameter %02X", l & 0x0f, r);
      break;
    }

    case PTBL:
    if (l >= 0x80) snprintf(out, size, "Set pulse width %03X", ((l & 0x0f) << 8) | r);
    else if (l) snprintf(out, size, "Change pulse width by %+d for %d frames", (signed char)r, l);
    else snprintf(out, size, "Empty");
    break;

    case FTBL:
    if (l == 0) snprintf(out, size, "Set cutoff %02X", r);
    else if (l < 0x80) snprintf(out, size, "Change cutoff by %+d for %d frames", (signed char)r, l);
    else
    {
      char type[24] = "";
      if (l & 0x10) strcat(type, "low");
      if (l & 0x20) strcat(type, type[0] ? "+band" : "band");
      if (l & 0x40) strcat(type, type[0] ? "+high" : "high");
      if (!type[0]) strcpy(type, "no");
      snprintf(out, size, "Filter: %s pass, resonance %X, voices %s%s%s", type, r >> 4,
        (r & 1) ? "1" : "", (r & 2) ? "2" : "", (r & 4) ? "3" : "");
    }
    break;

    case STBL:
    snprintf(out, size, "Vibrato speed %02X, depth %02X  |  portamento %04X  |  funktempo %d, %d", l, r, (l << 8) | r, l, r);
    break;
  }
}

static void tablekey(int rawkeycode, int keychar, bool shift)
{
  editorInfo.editmode = EDIT_TABLES;
  sendkey(rawkeycode, keychar, shift);
}

static void contextmenu(void)
{
  if (!ImGui::BeginPopup("##tablecontext")) return;
  if (ImGui::MenuItem("Cut", "SHIFT+X")) tablekey(KEY_X, 'x', true);
  if (ImGui::MenuItem("Copy", "SHIFT+C")) tablekey(KEY_C, 'c', true);
  if (ImGui::MenuItem("Paste", "SHIFT+V")) tablekey(KEY_V, 'v', true);
  ImGui::Separator();
  if (ImGui::MenuItem("Insert Row", "INS")) tablekey(KEY_INS, 0, false);
  if (ImGui::MenuItem("Delete Row", "DEL")) tablekey(KEY_DEL, 127, false);
  ImGui::Separator();
  if (ImGui::MenuItem("Negate Value / Note", "SHIFT+N")) tablekey(KEY_N, 'n', true);
  if (ImGui::MenuItem("Absolute <-> Relative Note", "SHIFT+R")) tablekey(KEY_R, 'r', true);
  if (ImGui::MenuItem("Limit -> Time Modulation Step", "SHIFT+L")) tablekey(KEY_L, 'l', true);
  if (ImGui::MenuItem("Optimize Table (Remove Unused)", "SHIFT+O")) tablekey(KEY_O, 'o', true);
  ImGui::Separator();
  if (ImGui::MenuItem(editorInfo.etlock ? "Unlock Table Scrolling" : "Lock Table Scrolling", "SHIFT+U")) tablekey(KEY_U, 'u', true);
  ImGui::EndPopup();
}

static void drawtable(int t, float colw, float height, bool focused)
{
  ImGuiIO &io = ImGui::GetIO();
  ImDrawList *dl;
  float cw, rh, scrolly, viewh;
  ImVec2 origin;
  int first, last, pos;
  int startrow = instr[editorInfo.einum].ptr[t] ? instr[editorInfo.einum].ptr[t] - 1 : -1;
  int partlen = (startrow >= 0) ? gettablepartlen(t, startrow) : 0;
  int len = gettablelen(t);
  char name[32];

  ImGui::PushID(t);
  ImGui::BeginGroup();

  // Header
  {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList *hl = ImGui::GetWindowDrawList();
    float fs = ImGui::GetFontSize();
    snprintf(name, sizeof name, "%s", tablenames[t]);
    hl->AddRectFilled(p, ImVec2(p.x + colw, p.y + fs + 8.0f), theme.bgraised, 5.0f);
    hl->AddText(fontbold, fs, ImVec2(p.x + 8.0f, p.y + 4.0f), (editorInfo.etnum == t) && focused ? theme.text : theme.textdim, name);
    // How many rows are used, if there is room
    snprintf(name, sizeof name, "%d", len);
    {
      ImVec2 ts = ImGui::CalcTextSize(name);
      float titlew = fontbold->CalcTextSizeA(fs, FLT_MAX, 0.0f, tablenames[t]).x;
      if (p.x + 8.0f + titlew + 8.0f < p.x + colw - ts.x - 8.0f)
        hl->AddText(ImVec2(p.x + colw - ts.x - 8.0f, p.y + 4.0f), theme.textfaint, name);
    }
    ImGui::Dummy(ImVec2(colw, fs + 8.0f));
  }

  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(theme.bgfield));
  ImGui::BeginChild("##rows", ImVec2(colw, height), ImGuiChildFlags_None, ImGuiWindowFlags_NoMove);
  dl = ImGui::GetWindowDrawList();
  ImGui::PushFont(fontmono, 0.0f);
  cw = ImGui::CalcTextSize("0").x;
  rh = ImGui::GetFontSize() + 4.0f;
  viewh = ImGui::GetWindowHeight();

  ImGui::Dummy(ImVec2(colw - 8.0f, MAX_TABLELEN * rh));

  // Keep the cursor visible when it moves with the keyboard
  if ((editorInfo.etnum == t) && ((editorInfo.etpos != lastetpos) || (editorInfo.etnum != lastetnum)) && (!tablesetbymouse))
  {
    float cy = editorInfo.etpos * rh;
    float sy = ImGui::GetScrollY();
    if ((cy < sy) || (cy + rh > sy + viewh)) ImGui::SetScrollY(cy - viewh * 0.4f);
  }

  scrolly = ImGui::GetScrollY();
  origin = ImGui::GetWindowPos();
  first = (int)(scrolly / rh);
  last = (int)((scrolly + viewh) / rh) + 1;
  if (last > MAX_TABLELEN - 1) last = MAX_TABLELEN - 1;

  for (pos = first; pos <= last; pos++)
  {
    float y = origin.y + pos * rh - scrolly;
    float x = origin.x + 6.0f;
    float ty = y + 2.0f;
    int l = ltable[t][pos];
    int r = rtable[t][pos];
    ImU32 col = theme.text;
    char text[8];
    int c;
    bool used = (pos < len);

    // The current instrument's part of the table
    if ((startrow >= 0) && (pos >= startrow) && (pos < startrow + partlen))
      dl->AddRectFilled(ImVec2(origin.x, y), ImVec2(origin.x + colw, y + rh), alpha(theme.instr, 0.10f));
    if (pos == startrow)
      dl->AddRectFilled(ImVec2(origin.x, y + 1.0f), ImVec2(origin.x + 3.0f, y + rh - 1.0f), theme.instr);

    // Other instruments starting here
    for (c = 1; c < MAX_INSTR; c++)
    {
      if ((c != editorInfo.einum) && (instr[c].ptr[t] == pos + 1))
      {
        dl->AddCircleFilled(ImVec2(origin.x + colw - 14.0f, y + rh * 0.5f), 2.5f, alpha(theme.instr, 0.55f), 8);
        break;
      }
    }

    // Selection
    if (editorInfo.etmarknum == t)
    {
      int a = editorInfo.etmarkstart < editorInfo.etmarkend ? editorInfo.etmarkstart : editorInfo.etmarkend;
      int b = editorInfo.etmarkstart < editorInfo.etmarkend ? editorInfo.etmarkend : editorInfo.etmarkstart;
      if ((pos >= a) && (pos <= b))
        dl->AddRectFilled(ImVec2(origin.x, y), ImVec2(origin.x + colw, y + rh), theme.accentsoft);
    }

    if ((editorInfo.etnum == t) && (pos == editorInfo.etpos))
      dl->AddRectFilled(ImVec2(origin.x, y), ImVec2(origin.x + colw, y + rh), alpha(theme.accent, focused ? 0.16f : 0.08f));

    // Commands in a different color
    if (l == 0xff) col = theme.keyoff;
    else if ((t == WTBL) && (l >= WAVECMD)) col = theme.command;
    else if ((t == PTBL) && (l >= 0x80)) col = theme.command;
    else if ((t == FTBL) && ((l >= 0x80) || ((!l) && (r)))) col = theme.command;
    if (!used) col = theme.textfaint;

    snprintf(text, sizeof text, "%02X", pos + 1);
    dl->AddText(ImVec2(x, ty), (pos % 4) ? theme.textfaint : theme.textdim, text);
    snprintf(text, sizeof text, "%02X", l);
    dl->AddText(ImVec2(x + cw * 4.0f, ty), col, text);
    snprintf(text, sizeof text, "%02X", r);
    dl->AddText(ImVec2(x + cw * 7.0f, ty), used ? alpha(col, 0.85f) : col, text);

    // Cursor
    if ((editorInfo.etnum == t) && (pos == editorInfo.etpos))
    {
      int d = editorInfo.etcolumn & 3;
      float cx = x + cw * digitchar[d];
      dl->AddRect(ImVec2(cx - 1.0f, y + 1.0f), ImVec2(cx + cw + 1.0f, y + rh - 1.0f),
        focused ? theme.accent : alpha(theme.accent, 0.45f), 2.0f, 0, focused ? 2.0f : 1.0f);
    }
  }
  ImGui::PopFont();

  // Mouse
  if ((ImGui::IsWindowHovered()) && (!ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup)))
  {
    int mrow = (int)((io.MousePos.y - origin.y + scrolly) / rh);
    float charpos = (io.MousePos.x - origin.x - 6.0f) / cw;
    int d = 0;
    int c;
    char tip[160];

    if (mrow < 0) mrow = 0;
    if (mrow > MAX_TABLELEN - 1) mrow = MAX_TABLELEN - 1;
    for (c = 0; c < 4; c++)
    {
      if (charpos >= digitchar[c] - 0.5f) d = c;
    }

    decoderow(t, mrow, tip, sizeof tip);
    tooltip(tip);

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
      editorInfo.editmode = EDIT_TABLES;
      if ((io.KeyShift) && (editorInfo.etnum == t))
      {
        editorInfo.etmarknum = t;
        editorInfo.etmarkstart = editorInfo.etpos;
        editorInfo.etmarkend = mrow;
      }
      else
      {
        editorInfo.etnum = t;
        editorInfo.etpos = mrow;
        editorInfo.etcolumn = d;
        editorInfo.etmarknum = -1;
        tabledragging = true;
        tabledragnum = t;
        tabledragstart = mrow;
      }
      tablesetbymouse = true;
    }
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
      editorInfo.editmode = EDIT_TABLES;
      editorInfo.etnum = t;
      if (editorInfo.etmarknum != t)
      {
        editorInfo.etpos = mrow;
        editorInfo.etcolumn = d;
      }
      tablesetbymouse = true;
      ImGui::OpenPopup("##tablecontext");
    }
  }
  if ((tabledragging) && (tabledragnum == t))
  {
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) tabledragging = false;
    else
    {
      int mrow = (int)((io.MousePos.y - origin.y + scrolly) / rh);
      if (mrow < 0) mrow = 0;
      if (mrow > MAX_TABLELEN - 1) mrow = MAX_TABLELEN - 1;
      if (mrow != tabledragstart)
      {
        editorInfo.etmarknum = t;
        editorInfo.etmarkstart = tabledragstart;
        editorInfo.etmarkend = mrow;
      }
      tablesetbymouse = true;
    }
  }
  contextmenu();

  ImGui::EndChild();
  ImGui::PopStyleColor();
  ImGui::EndGroup();
  ImGui::PopID();
}

void gui::drawtables(bool *open)
{
  bool focused;
  float avail, colw, height, fs;
  int t;

  if (!ImGui::Begin(WIN_TABLES, open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
  {
    ImGui::End();
    return;
  }
  focused = panelfocus(EDIT_TABLES);
  fs = ImGui::GetFontSize();

  avail = ImGui::GetContentRegionAvail().x;
  colw = (avail - ImGui::GetStyle().ItemSpacing.x * 3.0f) / 4.0f;
  height = ImGui::GetContentRegionAvail().y - fs * 3.8f - 16.0f;
  if (height < fs * 4.0f) height = fs * 4.0f;

  for (t = 0; t < MAX_TABLES; t++)
  {
    if (t) ImGui::SameLine();
    drawtable(t, colw, height, focused);
  }
  tablesetbymouse = false;
  lastetpos = editorInfo.etpos;
  lastetnum = editorInfo.etnum;

  // What the row at the cursor does
  {
    char text[160];
    char head[32];
    decoderow(editorInfo.etnum, editorInfo.etpos, text, sizeof text);
    snprintf(head, sizeof head, "%s %02X:", tablenames[editorInfo.etnum & 3], editorInfo.etpos + 1);
    ImGui::Spacing();
    ImGui::PushFont(fontbold, 0.0f);
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(theme.textdim), "%s", head);
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
  }

  if (focused) drawfocusframe();
  ImGui::End();
}
