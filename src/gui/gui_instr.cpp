// GoatTracker GUI: the instrument library and the instrument inspector

#include "gui_internal.h"

using namespace gui;

static char searchtext[32] = "";
static int renaming = 0;
static char renamebuffer[MAX_INSTRNAMELEN + 1];
static bool previewing = false;

static bool instrumentempty(int i)
{
  INSTR *p = &instr[i];
  return (!p->ad) && (!p->sr) && (!p->ptr[0]) && (!p->ptr[1]) && (!p->ptr[2]) && (!p->ptr[3]) &&
    (!p->vibdelay) && (!p->gatetimer) && (!p->firstwave) && (!p->name[0]);
}

static void instrumentname(int i, char *out)
{
  memcpy(out, instr[i].name, MAX_INSTRNAMELEN);
  out[MAX_INSTRNAMELEN] = 0;
}

static bool matches(const char *name, const char *search)
{
  size_t n = strlen(search);
  const char *p;

  if (!n) return true;
  for (p = name; *p; p++)
  {
    size_t c;
    for (c = 0; c < n; c++)
    {
      if (!p[c]) return false;
      if (tolower((unsigned char)p[c]) != tolower((unsigned char)search[c])) break;
    }
    if (c == n) return true;
  }
  return false;
}

// Plays the current instrument while the item before this call is held
static void previewbutton(int i)
{
  if (ImGui::IsItemActivated())
  {
    playtestnote(FIRSTNOTE + editorInfo.epoctave * 12, i, editorInfo.epchn);
    previewing = true;
  }
  if ((ImGui::IsItemDeactivated()) && (previewing))
  {
    releasenote(editorInfo.epchn);
    previewing = false;
  }
}

static void instrumentkey(int rawkeycode, int keychar, bool shift)
{
  editorInfo.editmode = EDIT_INSTRUMENT;
  sendkey(rawkeycode, keychar, shift);
}

void gui::drawlibrary(bool *open)
{
  float fs;
  int i;

  if (!ImGui::Begin(WIN_LIBRARY, open))
  {
    ImGui::End();
    return;
  }
  panelfocus(-1);
  fs = ImGui::GetFontSize();

  ImGui::SetNextItemWidth(-FLT_MIN);
  ImGui::InputTextWithHint("##search", "Search instruments", searchtext, sizeof searchtext);

  ImGui::BeginChild("##instruments", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
  for (i = 1; i < MAX_INSTR; i++)
  {
    char name[MAX_INSTRNAMELEN + 1];
    char label[48];
    bool empty = instrumentempty(i);
    bool selected = (editorInfo.einum == i);
    ImVec2 p;
    float rowh = fs + 10.0f;

    instrumentname(i, name);
    if ((searchtext[0]) && (!matches(name, searchtext))) continue;

    ImGui::PushID(i);
    p = ImGui::GetCursorScreenPos();

    if (renaming == i)
    {
      ImGui::SetNextItemWidth(-FLT_MIN);
      if (ImGui::IsWindowAppearing() || !ImGui::IsAnyItemActive()) ImGui::SetKeyboardFocusHere();
      if (ImGui::InputText("##rename", renamebuffer, sizeof renamebuffer, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
      {
        undobegin();
        undoinstrument(i);
        memset(instr[i].name, 0, MAX_INSTRNAMELEN);
        strncpy(instr[i].name, renamebuffer, MAX_INSTRNAMELEN - 1);
        undoend();
        renaming = 0;
      }
      if ((ImGui::IsItemDeactivated()) || (ImGui::IsKeyPressed(ImGuiKey_Escape))) renaming = 0;
      ImGui::PopID();
      continue;
    }

    snprintf(label, sizeof label, "##item%d", i);
    ImGui::SetNextItemAllowOverlap();
    if (ImGui::Selectable(label, selected, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0.0f, rowh)))
    {
      editorInfo.einum = i;
      if (editorInfo.editmode != EDIT_TABLES) editorInfo.editmode = EDIT_INSTRUMENT;
      if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
      {
        renaming = i;
        strcpy(renamebuffer, name);
      }
    }
    if (ImGui::BeginPopupContextItem("##instrcontext"))
    {
      editorInfo.einum = i;
      if (ImGui::MenuItem("Rename")) { renaming = i; strcpy(renamebuffer, name); }
      ImGui::Separator();
      if (ImGui::MenuItem("Cut", "SHIFT+X")) instrumentkey(KEY_X, 'x', true);
      if (ImGui::MenuItem("Copy", "SHIFT+C")) instrumentkey(KEY_C, 'c', true);
      if (ImGui::MenuItem("Paste", "SHIFT+V")) instrumentkey(KEY_V, 'v', true);
      if (ImGui::MenuItem("Smart Paste (copies tables too)", "SHIFT+S")) instrumentkey(KEY_S, 's', true);
      if (ImGui::MenuItem("Delete Instrument and Its Tables", "SHIFT+DEL")) instrumentkey(KEY_DEL, 127, true);
      ImGui::Separator();
      if (ImGui::MenuItem("Load Instrument...")) cmd_post(GTCMD_LOADINSTR);
      if (ImGui::MenuItem("Save Instrument...")) cmd_post(GTCMD_SAVEINSTR);
      if (ImGui::MenuItem("Import chipsynth C64 Patch...")) cmd_post(GTCMD_IMPORTPATCH);
      ImGui::EndPopup();
    }

    {
      ImDrawList *dl = ImGui::GetWindowDrawList();
      float ty = p.y + (rowh - fs) * 0.5f;
      char num[4];
      snprintf(num, sizeof num, "%02X", i);
      dl->AddText(fontmono, ImGui::GetFontSize(), ImVec2(p.x + 4.0f, ty), selected ? theme.text : theme.instr, num);
      if (name[0])
        dl->AddText(ImVec2(p.x + fs * 2.4f, ty), selected ? theme.text : theme.textdim, name);
      else
        dl->AddText(ImVec2(p.x + fs * 2.4f, ty), theme.textfaint, empty ? "-" : "(no name)");

      // Preview on hover
      if ((ImGui::IsItemHovered()) || (selected))
      {
        float bw = rowh - 4.0f;
        ImGui::SetCursorScreenPos(ImVec2(p.x + ImGui::GetContentRegionAvail().x - bw, p.y + 2.0f));
        iconbutton("##preview", ICON_SPEAKER, false, 0, ImVec2(bw, bw), "Hold to hear the instrument");
        previewbutton(i);
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + rowh));
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
      }
    }
    ImGui::PopID();
  }
  ImGui::EndChild();
  ImGui::End();
}

/*
 * Inspector
 */

static const float envms[16] = { 2, 8, 16, 24, 38, 56, 68, 80, 100, 250, 500, 800, 1000, 3000, 5000, 8000 };

static void envelopegraph(ImVec2 size, int ad, int sr)
{
  ImDrawList *dl = ImGui::GetWindowDrawList();
  ImVec2 p = ImGui::GetCursorScreenPos();
  float ta = sqrtf(envms[ad >> 4]);
  float td = sqrtf(envms[ad & 15] * 3.0f);
  float th = sqrtf(400.0f);
  float tr = sqrtf(envms[sr & 15] * 3.0f);
  float total = ta + td + th + tr;
  float s = (float)(sr >> 4) / 15.0f;
  float x0 = p.x + 4.0f;
  float w = size.x - 8.0f;
  float y0 = p.y + 4.0f;
  float h = size.y - 8.0f;
  ImVec2 pts[5];

  dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), theme.bgfield, 5.0f);
  pts[0] = ImVec2(x0, y0 + h);
  pts[1] = ImVec2(x0 + w * ta / total, y0);
  pts[2] = ImVec2(x0 + w * (ta + td) / total, y0 + h * (1.0f - s));
  pts[3] = ImVec2(x0 + w * (ta + td + th) / total, y0 + h * (1.0f - s));
  pts[4] = ImVec2(x0 + w, y0 + h);
  {
    int c;
    for (c = 0; c < 4; c++)
      dl->AddQuadFilled(pts[c], pts[c + 1], ImVec2(pts[c + 1].x, y0 + h), ImVec2(pts[c].x, y0 + h), alpha(theme.accent, 0.18f));
  }
  dl->AddPolyline(pts, 5, theme.accent, 0, 2.0f);
  dl->AddText(fontui, ImGui::GetFontSize() * 0.75f, ImVec2(x0 + 2.0f, p.y + 2.0f), theme.textfaint, "ENVELOPE");
  ImGui::Dummy(size);
}

// One row of the inspector: label + hex value that the keyboard edits
// (eipos = field). Returns true if the value was clicked
static void fieldrow(const char *label, int field, unsigned value, bool focused, const char *tip)
{
  int cursor = -1;
  int clicked;

  if (editorInfo.eipos == field) cursor = editorInfo.eicolumn;
  ImGui::AlignTextToFramePadding();
  ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(editorInfo.eipos == field && focused ? theme.text : theme.textdim), "%s", label);
  ImGui::SameLine(ImGui::GetFontSize() * 7.5f);
  ImGui::PushID(field);
  clicked = hexvalue("##v", value, 2, cursor, focused);
  ImGui::PopID();
  if ((tip) && (ImGui::IsItemHovered())) tooltip(tip);
  if (clicked >= 0)
  {
    editorInfo.editmode = EDIT_INSTRUMENT;
    editorInfo.eipos = field;
    editorInfo.eicolumn = clicked;
  }
}

static void gotobutton(const char *id, int table, int ptr)
{
  ImGui::SameLine();
  ImGui::PushID(id);
  if (iconbutton("##goto", ICON_GOTO, false, 0, ImVec2(ImGui::GetFontSize() * 1.6f, ImGui::GetFontSize() + 6.0f), ptr ? "Show in the table editor" : "No table: click to go to the end of the table"))
  {
    int pos = ptr ? ptr - 1 : gettablelen(table);
    if (pos >= MAX_TABLELEN) pos = MAX_TABLELEN - 1;
    gototable(table, pos);
  }
  ImGui::PopID();
}

void gui::drawinspector(bool *open)
{
  bool focused;
  int i = editorInfo.einum;
  INSTR *p;
  float fs;
  char text[64];

  if (!ImGui::Begin(WIN_INSPECTOR, open))
  {
    ImGui::End();
    return;
  }
  focused = panelfocus(EDIT_INSTRUMENT);
  fs = ImGui::GetFontSize();
  p = &instr[i];

  // Instrument selector and name
  if (iconbutton("##prev", ICON_PREV, false, 0, ImVec2(fs * 1.5f, fs * 1.6f), "Previous instrument (-)")) previnstr();
  ImGui::SameLine(0.0f, 2.0f);
  snprintf(text, sizeof text, "%02X", i);
  ImGui::PushFont(fontmono, uisize * 1.3f);
  ImGui::AlignTextToFramePadding();
  ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(theme.instr), "%s", text);
  ImGui::PopFont();
  ImGui::SameLine(0.0f, 2.0f);
  if (iconbutton("##next", ICON_NEXT, false, 0, ImVec2(fs * 1.5f, fs * 1.6f), "Next instrument (+)")) nextinstr();
  ImGui::SameLine();
  if (i)
  {
    char name[MAX_INSTRNAMELEN + 1];
    instrumentname(i, name);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::InputTextWithHint("##name", "Instrument name", name, sizeof name))
    {
      undobegin();
      undoinstrument(i);
      memset(p->name, 0, MAX_INSTRNAMELEN);
      strncpy(p->name, name, MAX_INSTRNAMELEN - 1);
      undoend();
    }
  }
  else
  {
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(theme.textfaint), "Instrument 00 does nothing: pick 01-3F");
  }

  // Hold to hear
  ImGui::Button("Hold to Play", ImVec2(-FLT_MIN, 0.0f));
  previewbutton(i);

  ImGui::BeginChild("##inspectorbody", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);

  // Envelope
  sectiontitle("ENVELOPE");
  {
    int a = p->ad >> 4, d = p->ad & 15, s = p->sr >> 4, r = p->sr & 15;
    float radius = fs * 1.25f;
    bool changed = false;
    bool activated = false;
    bool deactivated = false;

    changed |= knob("##a", "Attack", &a, 0, 15, radius, "attack time, 0 = 2 ms ... F = 8 s");
    activated |= ImGui::IsItemActivated(); deactivated |= ImGui::IsItemDeactivated();
    ImGui::SameLine();
    changed |= knob("##d", "Decay", &d, 0, 15, radius, "decay time, 0 = 6 ms ... F = 24 s");
    activated |= ImGui::IsItemActivated(); deactivated |= ImGui::IsItemDeactivated();
    ImGui::SameLine();
    changed |= knob("##s", "Sustain", &s, 0, 15, radius, "sustain level, 0 = silent ... F = loudest");
    activated |= ImGui::IsItemActivated(); deactivated |= ImGui::IsItemDeactivated();
    ImGui::SameLine();
    changed |= knob("##r", "Release", &r, 0, 15, radius, "release time, 0 = 6 ms ... F = 24 s");
    activated |= ImGui::IsItemActivated(); deactivated |= ImGui::IsItemDeactivated();

    if ((i) && ((changed) || (activated)))
    {
      undobegin();
      undoinstrument(i);
      p->ad = (unsigned char)((a << 4) | d);
      p->sr = (unsigned char)((s << 4) | r);
    }
    // One undo step per drag (and for wheel changes)
    if ((deactivated) || ((changed) && (!ImGui::IsMouseDown(ImGuiMouseButton_Left)))) undoend();

    envelopegraph(ImVec2(ImGui::GetContentRegionAvail().x, fs * 3.6f), p->ad, p->sr);
    fieldrow("Attack/Decay", 0, p->ad, focused, "AD register: attack (left digit) and decay (right digit)");
    fieldrow("Sustain/Release", 1, p->sr, focused, "SR register: sustain level (left digit) and release (right digit)");
  }

  // First frame waveform
  sectiontitle("FIRST FRAME");
  {
    static const struct { const char *label; int bit; const char *tip; } bits[] = {
      { "TRI", 0x10, "Triangle" }, { "SAW", 0x20, "Sawtooth" }, { "PUL", 0x40, "Pulse" }, { "NOI", 0x80, "Noise" },
      { "GATE", 0x01, "Gate bit" }, { "SYNC", 0x02, "Oscillator sync" }, { "RING", 0x04, "Ring modulation" }, { "TEST", 0x08, "Test bit (resets the oscillator, as in the usual 09 hard restart)" }
    };
    int b;
    bool special = (p->firstwave == 0x00) || (p->firstwave >= 0xfe);

    for (b = 0; b < 8; b++)
    {
      bool on = (!special) && ((p->firstwave & bits[b].bit) != 0);
      if (b == 4) ImGui::NewLine();
      else if (b) ImGui::SameLine(0.0f, 4.0f);
      ImGui::PushID(b);
      if ((togglechip(bits[b].label, on, bits[b].tip)) && (i))
      {
        undobegin();
        undoinstrument(i);
        p->firstwave = (unsigned char)((special ? 0 : p->firstwave) ^ bits[b].bit);
        undoend();
      }
      ImGui::PopID();
    }
    if (special)
    {
      const char *what = (p->firstwave == 0x00) ? "Waveform and gate unchanged" : (p->firstwave == 0xfe) ? "Waveform unchanged, gate off" : "Waveform unchanged, gate on";
      ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(theme.warning), "%s", what);
    }
    fieldrow("1st frame wave", 8, p->firstwave, focused, "Waveform on the first frame of a note, usually 09 (gate + test). 00 = unchanged, FE = gate off, FF = gate on");
  }

  // Tables
  sectiontitle("TABLES");
  fieldrow("Wavetable", 2, p->ptr[WTBL], focused, "Start position in the wavetable");
  gotobutton("w", WTBL, p->ptr[WTBL]);
  fieldrow("Pulsetable", 3, p->ptr[PTBL], focused, "Start position in the pulsetable (00 = pulse left as it is)");
  gotobutton("p", PTBL, p->ptr[PTBL]);
  fieldrow("Filtertable", 4, p->ptr[FTBL], focused, "Start position in the filtertable (00 = filter left as it is)");
  gotobutton("f", FTBL, p->ptr[FTBL]);

  // Vibrato
  sectiontitle("VIBRATO");
  fieldrow("Vibrato param", 5, p->ptr[STBL], focused, "Instrument vibrato: position in the speedtable (left = speed, right = depth)");
  gotobutton("s", STBL, p->ptr[STBL]);
  fieldrow("Vibrato delay", 6, p->vibdelay, focused, "Ticks until the vibrato starts, 00 = no vibrato");

  // Gate timer / hard restart
  sectiontitle("GATE & HARD RESTART");
  {
    bool hr = !(p->gatetimer & 0x80);
    bool gateoff = !(p->gatetimer & 0x40);
    int timer = p->gatetimer & 0x3f;

    if ((ImGui::Checkbox("Hard restart", &hr)) && (i))
    {
      undobegin();
      undoinstrument(i);
      p->gatetimer ^= 0x80;
      undoend();
    }
    if (ImGui::IsItemHovered()) tooltip("Hard restart before each note: makes notes start reliably (bit 80 off)");
    ImGui::SameLine();
    if ((ImGui::Checkbox("Gate off", &gateoff)) && (i))
    {
      undobegin();
      undoinstrument(i);
      p->gatetimer ^= 0x40;
      undoend();
    }
    if (ImGui::IsItemHovered()) tooltip("Gate off before the next note (bit 40 off). Turn off for legato");
    ImGui::SetNextItemWidth(fs * 8.0f);
    if ((ImGui::SliderInt("Timer", &timer, 0, 63, "%d ticks")) && (i))
    {
      undobegin();
      undoinstrument(i);
      p->gatetimer = (unsigned char)((p->gatetimer & 0xc0) | (timer & 0x3f));
    }
    if (ImGui::IsItemDeactivated()) undoend();
    if (ImGui::IsItemHovered()) tooltip("Ticks before the next note when the gate off / hard restart happens. At most tempo - 1");
    fieldrow("HR/gate timer", 7, p->gatetimer, focused, "80 = no hard restart, 40 = no gate off, 00-3F = timer");
  }

  ImGui::EndChild();

  if (focused) drawfocusframe();
  ImGui::End();
}
