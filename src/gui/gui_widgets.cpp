// GoatTracker GUI: theme, focus handling, undo helpers and small widgets

#include "gui_internal.h"

namespace gui
{
  Theme theme;
  ImFont *fontui = NULL;
  ImFont *fontbold = NULL;
  ImFont *fontmono = NULL;
  float uisize = 14.0f;
  float monosize = 14.0f;
  float voicelevel[MAX_CHN] = { 0.0f, 0.0f, 0.0f };
}

using namespace gui;

static ImU32 rgb(int r, int g, int b, int a = 255)
{
  return IM_COL32(r, g, b, a);
}

ImU32 gui::alpha(ImU32 col, float a)
{
  int ca = (int)(((col >> IM_COL32_A_SHIFT) & 0xff) * a);
  if (ca > 255) ca = 255;
  if (ca < 0) ca = 0;
  return (col & ~IM_COL32_A_MASK) | ((ImU32)ca << IM_COL32_A_SHIFT);
}

ImU32 gui::mix(ImU32 a, ImU32 b, float t)
{
  ImVec4 ca = ImGui::ColorConvertU32ToFloat4(a);
  ImVec4 cb = ImGui::ColorConvertU32ToFloat4(b);
  return ImGui::ColorConvertFloat4ToU32(ImVec4(ca.x + (cb.x - ca.x) * t, ca.y + (cb.y - ca.y) * t,
    ca.z + (cb.z - ca.z) * t, ca.w + (cb.w - ca.w) * t));
}

static ImVec4 v4(ImU32 col)
{
  return ImGui::ColorConvertU32ToFloat4(col);
}

void gui::applytheme(void)
{
  ImGuiStyle &style = ImGui::GetStyle();
  ImVec4 *c = style.Colors;

  // Dark graphite, in the spirit of a DAW
  theme.bgdeep = rgb(20, 20, 22);
  theme.bgpanel = rgb(31, 32, 35);
  theme.bgraised = rgb(42, 43, 47);
  theme.bgfield = rgb(24, 25, 27);
  theme.bgrowalt = rgb(255, 255, 255, 8);
  theme.bgrowbar = rgb(255, 255, 255, 18);
  theme.border = rgb(12, 12, 14);
  theme.separator = rgb(52, 53, 58);
  theme.text = rgb(230, 230, 233);
  theme.textdim = rgb(154, 155, 163);
  theme.textfaint = rgb(88, 89, 98);
  theme.accent = rgb(58, 142, 246);
  theme.accentsoft = rgb(58, 142, 246, 70);
  theme.playhead = rgb(242, 199, 68);
  theme.playheadsoft = rgb(242, 199, 68, 38);
  theme.record = rgb(229, 72, 77);
  theme.mute = rgb(79, 163, 247);
  theme.solo = rgb(242, 199, 68);
  theme.chn[0] = rgb(76, 195, 138);
  theme.chn[1] = rgb(79, 163, 247);
  theme.chn[2] = rgb(240, 160, 75);
  theme.note = rgb(240, 240, 242);
  theme.rest = rgb(70, 71, 80);
  theme.keyoff = rgb(229, 136, 77);
  theme.instr = rgb(143, 182, 247);
  theme.command = rgb(199, 146, 234);
  theme.cmddata = rgb(215, 181, 240);
  theme.lcdbg = rgb(14, 15, 17);
  theme.lcdtext = rgb(221, 230, 243);
  theme.lcddim = rgb(107, 114, 128);
  theme.warning = rgb(229, 136, 77);

  style.WindowPadding = ImVec2(10.0f, 8.0f);
  style.FramePadding = ImVec2(8.0f, 4.0f);
  style.ItemSpacing = ImVec2(8.0f, 6.0f);
  style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
  style.ScrollbarSize = 11.0f;
  style.GrabMinSize = 10.0f;
  style.WindowBorderSize = 1.0f;
  style.ChildBorderSize = 0.0f;
  style.PopupBorderSize = 1.0f;
  style.FrameBorderSize = 0.0f;
  style.TabBorderSize = 0.0f;
  style.WindowRounding = 6.0f;
  style.ChildRounding = 4.0f;
  style.FrameRounding = 5.0f;
  style.PopupRounding = 6.0f;
  style.ScrollbarRounding = 6.0f;
  style.GrabRounding = 5.0f;
  style.TabRounding = 5.0f;
  style.TabBarOverlineSize = 2.0f;
  style.DockingSeparatorSize = 2.0f;
  style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
  style.FontSizeBase = uisize;

  c[ImGuiCol_Text] = v4(theme.text);
  c[ImGuiCol_TextDisabled] = v4(theme.textfaint);
  c[ImGuiCol_WindowBg] = v4(theme.bgpanel);
  c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_PopupBg] = v4(rgb(38, 39, 43, 250));
  c[ImGuiCol_Border] = v4(theme.border);
  c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_FrameBg] = v4(theme.bgfield);
  c[ImGuiCol_FrameBgHovered] = v4(rgb(36, 37, 41));
  c[ImGuiCol_FrameBgActive] = v4(rgb(44, 45, 50));
  c[ImGuiCol_TitleBg] = v4(theme.bgraised);
  c[ImGuiCol_TitleBgActive] = v4(theme.bgraised);
  c[ImGuiCol_TitleBgCollapsed] = v4(theme.bgraised);
  c[ImGuiCol_MenuBarBg] = v4(theme.bgraised);
  c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_ScrollbarGrab] = v4(rgb(70, 71, 78));
  c[ImGuiCol_ScrollbarGrabHovered] = v4(rgb(92, 93, 102));
  c[ImGuiCol_ScrollbarGrabActive] = v4(rgb(110, 111, 120));
  c[ImGuiCol_CheckMark] = v4(theme.accent);
  c[ImGuiCol_SliderGrab] = v4(theme.accent);
  c[ImGuiCol_SliderGrabActive] = v4(rgb(96, 166, 250));
  c[ImGuiCol_Button] = v4(rgb(52, 53, 59));
  c[ImGuiCol_ButtonHovered] = v4(rgb(64, 65, 72));
  c[ImGuiCol_ButtonActive] = v4(rgb(78, 79, 88));
  c[ImGuiCol_Header] = v4(rgb(58, 142, 246, 60));
  c[ImGuiCol_HeaderHovered] = v4(rgb(255, 255, 255, 18));
  c[ImGuiCol_HeaderActive] = v4(rgb(58, 142, 246, 90));
  c[ImGuiCol_Separator] = v4(theme.separator);
  c[ImGuiCol_SeparatorHovered] = v4(theme.accent);
  c[ImGuiCol_SeparatorActive] = v4(theme.accent);
  c[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_ResizeGripHovered] = v4(alpha(theme.accent, 0.5f));
  c[ImGuiCol_ResizeGripActive] = v4(theme.accent);
  c[ImGuiCol_InputTextCursor] = v4(theme.accent);
  c[ImGuiCol_Tab] = v4(theme.bgraised);
  c[ImGuiCol_TabHovered] = v4(rgb(56, 57, 63));
  c[ImGuiCol_TabSelected] = v4(theme.bgpanel);
  c[ImGuiCol_TabSelectedOverline] = v4(theme.accent);
  c[ImGuiCol_TabDimmed] = v4(theme.bgraised);
  c[ImGuiCol_TabDimmedSelected] = v4(theme.bgpanel);
  c[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_DockingPreview] = v4(alpha(theme.accent, 0.45f));
  c[ImGuiCol_DockingEmptyBg] = v4(theme.bgdeep);
  c[ImGuiCol_TableHeaderBg] = v4(theme.bgraised);
  c[ImGuiCol_TableBorderStrong] = v4(theme.separator);
  c[ImGuiCol_TableBorderLight] = v4(rgb(44, 45, 50));
  c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_TableRowBgAlt] = v4(theme.bgrowalt);
  c[ImGuiCol_TextSelectedBg] = v4(theme.accentsoft);
  c[ImGuiCol_NavCursor] = v4(theme.accent);
  c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.45f);
}

/*
 * Panels and the editors' keyboard focus
 */

static int prevframeeditmode = -1;
static int frameeditmodechanged = 0;
static int frameserial = -1;

const char *gui::editmodewindow(int editmode)
{
  switch (editmode)
  {
    case EDIT_PATTERN: return WIN_PATTERN;
    case EDIT_ORDERLIST: return WIN_TRACKS;
    case EDIT_INSTRUMENT: return WIN_INSPECTOR;
    case EDIT_TABLES: return WIN_TABLES;
    case EDIT_NAMES: return WIN_SONG;
  }
  return WIN_PATTERN;
}

bool gui::panelfocus(int editmode)
{
  // Once per frame: did the editor change (F5-F8, TAB, menus)?
  if (frameserial != ImGui::GetFrameCount())
  {
    frameserial = ImGui::GetFrameCount();
    frameeditmodechanged = (editorInfo.editmode != prevframeeditmode);
    prevframeeditmode = editorInfo.editmode;
  }

  if (editmode < 0) return false;

  if ((frameeditmodechanged) && (editorInfo.editmode == editmode))
    ImGui::SetWindowFocus();

  // A click anywhere in the panel moves the keyboard to its editor
  if ((ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) &&
      ((ImGui::IsMouseClicked(ImGuiMouseButton_Left)) || (ImGui::IsMouseClicked(ImGuiMouseButton_Right))))
  {
    if (!ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup))
    {
      editorInfo.editmode = editmode;
      prevframeeditmode = editmode;
    }
  }

  return editorInfo.editmode == editmode;
}

void gui::drawfocusframe(void)
{
  ImGuiWindow *w = ImGui::GetCurrentWindow();
  ImDrawList *dl = ImGui::GetWindowDrawList();
  ImRect r = w->Rect();

  dl->PushClipRect(r.Min, r.Max, false);
  dl->AddRect(ImVec2(r.Min.x + 1.0f, r.Min.y + 1.0f), ImVec2(r.Max.x - 1.0f, r.Max.y - 1.0f),
    alpha(theme.accent, 0.85f), 4.0f, 0, 2.0f);
  dl->PopClipRect();
}

/*
 * Undo for mouse edits
 */

static GTUNDO_OBJECT *undoobject = NULL;

void gui::undobegin(void)
{
  if (!undoobject) undoobject = undoCreateEditorInfo();
}

void gui::undoinstrument(int i)
{
  undoAreaSetCheckForChange(UNDO_AREA_INSTRUMENTS, i, UNDO_AREA_DIRTY_CHECK);
}

void gui::undotable(int t)
{
  undoAreaSetCheckForChange(UNDO_AREA_TABLES + t, 0, UNDO_AREA_DIRTY_CHECK);
  undoAreaSetCheckForChange(UNDO_AREA_TABLES + t, 1, UNDO_AREA_DIRTY_CHECK);
}

void gui::undopattern(int p)
{
  undoAreaSetCheckForChange(UNDO_AREA_PATTERN, p, UNDO_AREA_DIRTY_CHECK);
  undoAreaSetCheckForChange(UNDO_AREA_PATTERN_LEN, 0, UNDO_AREA_DIRTY_CHECK);
}

void gui::undoorderlist(void)
{
  int c;
  for (c = 0; c < MAX_CHN; c++)
    undoAreaSetCheckForChange(UNDO_AREA_ORDERLIST, c + (editorInfo.esnum * MAX_CHN), UNDO_AREA_DIRTY_CHECK);
  undoAreaSetCheckForChange(UNDO_AREA_ORDERLIST_LEN, 0, UNDO_AREA_DIRTY_CHECK);
}

void gui::undoend(void)
{
  if (!undoobject) return;
  if (undoValidateUndoAreas(undoobject) == 0) undoFreeUndoObject(undoobject);
  undoobject = NULL;
}

void gui::sendkey(int rawkeycode, int keychar, bool shift)
{
  if ((shift) && (keychar >= 'a') && (keychar <= 'z')) keychar = toupper(keychar);
  input_pushkey(rawkeycode, keychar, shift ? KMOD_GT_SHIFT : 0);
}

/*
 * Icons, drawn with shapes so that they are sharp at any size
 */

void gui::drawicon(ImDrawList *dl, Icon icon, ImVec2 c, float s, ImU32 col)
{
  float h = s * 0.5f;

  switch (icon)
  {
    case ICON_PLAY:
    dl->AddTriangleFilled(ImVec2(c.x - h * 0.7f, c.y - h), ImVec2(c.x - h * 0.7f, c.y + h), ImVec2(c.x + h * 0.9f, c.y), col);
    break;

    case ICON_REWIND:
    // Bar + triangle pointing to it: play from the beginning
    dl->AddRectFilled(ImVec2(c.x - h, c.y - h), ImVec2(c.x - h * 0.6f, c.y + h), col, 1.0f);
    dl->AddTriangleFilled(ImVec2(c.x - h * 0.3f, c.y - h), ImVec2(c.x - h * 0.3f, c.y + h), ImVec2(c.x + h, c.y), col);
    break;

    case ICON_PLAYPOS:
    // Triangle with a position marker below
    dl->AddTriangleFilled(ImVec2(c.x - h * 0.6f, c.y - h), ImVec2(c.x - h * 0.6f, c.y + h * 0.5f), ImVec2(c.x + h * 0.8f, c.y - h * 0.25f), col);
    dl->AddRectFilled(ImVec2(c.x - h, c.y + h * 0.75f), ImVec2(c.x + h, c.y + h), col, 1.0f);
    break;

    case ICON_PLAYPATTERN:
    // Triangle inside a loop bracket
    dl->AddRect(ImVec2(c.x - h, c.y - h), ImVec2(c.x + h, c.y + h), col, 3.0f, 0, 1.5f);
    dl->AddTriangleFilled(ImVec2(c.x - h * 0.35f, c.y - h * 0.5f), ImVec2(c.x - h * 0.35f, c.y + h * 0.5f), ImVec2(c.x + h * 0.5f, c.y), col);
    break;

    case ICON_STOP:
    dl->AddRectFilled(ImVec2(c.x - h * 0.8f, c.y - h * 0.8f), ImVec2(c.x + h * 0.8f, c.y + h * 0.8f), col, 2.0f);
    break;

    case ICON_RECORD:
    dl->AddCircleFilled(c, h * 0.85f, col, 24);
    break;

    case ICON_FOLLOW:
    // Playhead line with an arrow: the view follows the playback
    dl->AddLine(ImVec2(c.x - h * 0.6f, c.y - h), ImVec2(c.x - h * 0.6f, c.y + h), col, 2.0f);
    dl->AddTriangleFilled(ImVec2(c.x - h * 0.1f, c.y - h * 0.6f), ImVec2(c.x - h * 0.1f, c.y + h * 0.6f), ImVec2(c.x + h, c.y), col);
    break;

    case ICON_PREV:
    dl->AddTriangleFilled(ImVec2(c.x + h * 0.5f, c.y - h * 0.7f), ImVec2(c.x + h * 0.5f, c.y + h * 0.7f), ImVec2(c.x - h * 0.5f, c.y), col);
    break;

    case ICON_NEXT:
    dl->AddTriangleFilled(ImVec2(c.x - h * 0.5f, c.y - h * 0.7f), ImVec2(c.x - h * 0.5f, c.y + h * 0.7f), ImVec2(c.x + h * 0.5f, c.y), col);
    break;

    case ICON_UP:
    dl->AddTriangleFilled(ImVec2(c.x - h * 0.7f, c.y + h * 0.4f), ImVec2(c.x + h * 0.7f, c.y + h * 0.4f), ImVec2(c.x, c.y - h * 0.5f), col);
    break;

    case ICON_DOWN:
    dl->AddTriangleFilled(ImVec2(c.x - h * 0.7f, c.y - h * 0.4f), ImVec2(c.x + h * 0.7f, c.y - h * 0.4f), ImVec2(c.x, c.y + h * 0.5f), col);
    break;

    case ICON_PLUS:
    dl->AddLine(ImVec2(c.x - h * 0.7f, c.y), ImVec2(c.x + h * 0.7f, c.y), col, 2.0f);
    dl->AddLine(ImVec2(c.x, c.y - h * 0.7f), ImVec2(c.x, c.y + h * 0.7f), col, 2.0f);
    break;

    case ICON_MINUS:
    dl->AddLine(ImVec2(c.x - h * 0.7f, c.y), ImVec2(c.x + h * 0.7f, c.y), col, 2.0f);
    break;

    case ICON_SPEAKER:
    dl->AddRectFilled(ImVec2(c.x - h, c.y - h * 0.35f), ImVec2(c.x - h * 0.45f, c.y + h * 0.35f), col);
    dl->AddTriangleFilled(ImVec2(c.x - h * 0.6f, c.y), ImVec2(c.x + h * 0.2f, c.y - h * 0.9f), ImVec2(c.x + h * 0.2f, c.y + h * 0.9f), col);
    dl->AddBezierQuadratic(ImVec2(c.x + h * 0.5f, c.y - h * 0.5f), ImVec2(c.x + h * 0.85f, c.y), ImVec2(c.x + h * 0.5f, c.y + h * 0.5f), col, 1.5f);
    break;

    case ICON_GOTO:
    dl->AddLine(ImVec2(c.x - h * 0.8f, c.y), ImVec2(c.x + h * 0.5f, c.y), col, 1.8f);
    dl->AddTriangleFilled(ImVec2(c.x + h * 0.1f, c.y - h * 0.55f), ImVec2(c.x + h * 0.1f, c.y + h * 0.55f), ImVec2(c.x + h * 0.9f, c.y), col);
    break;
  }
}

bool gui::iconbutton(const char *id, Icon icon, bool active, ImU32 activecol, ImVec2 size, const char *tip)
{
  ImDrawList *dl = ImGui::GetWindowDrawList();
  ImVec2 p = ImGui::GetCursorScreenPos();
  bool pressed = ImGui::InvisibleButton(id, size);
  bool hovered = ImGui::IsItemHovered();
  bool held = ImGui::IsItemActive();
  ImU32 bg = active ? alpha(activecol, 0.22f) : IM_COL32(255, 255, 255, 0);
  ImU32 fg = active ? activecol : theme.text;

  if (hovered) bg = active ? alpha(activecol, 0.32f) : IM_COL32(255, 255, 255, 22);
  if (held) bg = IM_COL32(255, 255, 255, 36);
  dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), bg, 5.0f);
  drawicon(dl, icon, ImVec2(p.x + size.x * 0.5f, p.y + size.y * 0.5f), size.y * 0.42f, fg);
  if ((tip) && (hovered)) tooltip(tip);
  return pressed;
}

bool gui::togglechip(const char *label, bool on, const char *tip, ImU32 oncol)
{
  ImU32 col = oncol ? oncol : theme.accent;
  bool pressed;

  ImGui::PushStyleColor(ImGuiCol_Button, on ? v4(alpha(col, 0.85f)) : v4(IM_COL32(52, 53, 59, 255)));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, on ? v4(col) : v4(IM_COL32(64, 65, 72, 255)));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, v4(mix(col, IM_COL32(255, 255, 255, 255), 0.2f)));
  ImGui::PushStyleColor(ImGuiCol_Text, on ? v4(IM_COL32(16, 17, 19, 255)) : v4(theme.textdim));
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(7.0f, 2.0f));
  pressed = ImGui::Button(label);
  ImGui::PopStyleVar();
  ImGui::PopStyleColor(4);
  if ((tip) && (ImGui::IsItemHovered())) tooltip(tip);
  return pressed;
}

bool gui::knob(const char *id, const char *label, int *value, int minv, int maxv, float radius, const char *tip)
{
  ImGuiIO &io = ImGui::GetIO();
  ImDrawList *dl = ImGui::GetWindowDrawList();
  ImVec2 p = ImGui::GetCursorScreenPos();
  float labelh = ImGui::GetFontSize();
  ImVec2 size(radius * 2.0f + 8.0f, radius * 2.0f + labelh + 6.0f);
  ImVec2 c(p.x + size.x * 0.5f, p.y + radius + 2.0f);
  static float dragaccum = 0.0f;
  bool changed = false;
  const float a0 = 2.35619449f;   // 135 degrees
  const float a1 = 7.06858347f;   // 405 degrees
  float t;
  char text[8];

  ImGui::InvisibleButton(id, size);
  if (ImGui::IsItemActivated()) dragaccum = 0.0f;
  if (ImGui::IsItemActive())
  {
    // Drag up/down; finer with SHIFT
    dragaccum -= io.MouseDelta.y / ((io.KeyShift) ? 24.0f : 8.0f);
    while (dragaccum >= 1.0f) { if (*value < maxv) { (*value)++; changed = true; } dragaccum -= 1.0f; }
    while (dragaccum <= -1.0f) { if (*value > minv) { (*value)--; changed = true; } dragaccum += 1.0f; }
  }
  if (ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY))
  {
    if (io.MouseWheel > 0.0f) { if (*value < maxv) { (*value)++; changed = true; } }
    if (io.MouseWheel < 0.0f) { if (*value > minv) { (*value)--; changed = true; } }
  }

  t = (maxv > minv) ? (float)(*value - minv) / (float)(maxv - minv) : 0.0f;
  dl->AddCircleFilled(c, radius, IM_COL32(24, 25, 28, 255), 32);
  dl->PathArcTo(c, radius - 3.0f, a0, a1, 32);
  dl->PathStroke(IM_COL32(60, 61, 68, 255), 0, 3.0f);
  if (t > 0.0f)
  {
    dl->PathArcTo(c, radius - 3.0f, a0, a0 + (a1 - a0) * t, 32);
    dl->PathStroke(theme.accent, 0, 3.0f);
  }
  {
    float a = a0 + (a1 - a0) * t;
    dl->AddLine(ImVec2(c.x + cosf(a) * radius * 0.25f, c.y + sinf(a) * radius * 0.25f),
      ImVec2(c.x + cosf(a) * (radius - 6.0f), c.y + sinf(a) * (radius - 6.0f)), theme.text, 2.0f);
  }
  if ((ImGui::IsItemHovered()) || (ImGui::IsItemActive()))
    dl->AddCircle(c, radius + 1.0f, alpha(theme.accent, 0.5f), 32, 1.0f);

  snprintf(text, sizeof text, "%X", *value);
  {
    ImVec2 ts = ImGui::CalcTextSize(label);
    dl->AddText(ImVec2(c.x - ts.x * 0.5f, p.y + radius * 2.0f + 4.0f), theme.textdim, label);
  }
  if ((ImGui::IsItemHovered()) || (ImGui::IsItemActive()))
  {
    char buf[64];
    snprintf(buf, sizeof buf, "%s: %s (%X)", label, tip ? tip : "", *value);
    ImGui::SetTooltip("%s", buf);
  }
  return changed;
}

int gui::hexvalue(const char *id, unsigned value, int digits, int cursordigit, bool focused)
{
  ImDrawList *dl = ImGui::GetWindowDrawList();
  float cw;
  float h = ImGui::GetFontSize() + 6.0f;
  ImVec2 p = ImGui::GetCursorScreenPos();
  ImVec2 size;
  char text[16];
  int clicked = -1;
  int c;

  ImGui::PushFont(fontmono, 0.0f);
  cw = ImGui::CalcTextSize("0").x;
  size = ImVec2(cw * digits + 10.0f, h);
  if (ImGui::InvisibleButton(id, size))
  {
    float x = ImGui::GetIO().MousePos.x - (p.x + 5.0f);
    clicked = (int)(x / cw);
    if (clicked < 0) clicked = 0;
    if (clicked >= digits) clicked = digits - 1;
  }
  dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), ImGui::IsItemHovered() ? IM_COL32(36, 37, 41, 255) : theme.bgfield, 4.0f);
  if ((cursordigit >= 0) && (cursordigit < digits))
  {
    ImVec2 a(p.x + 5.0f + cw * cursordigit - 1.0f, p.y + 2.0f);
    dl->AddRectFilled(a, ImVec2(a.x + cw + 2.0f, p.y + size.y - 2.0f), focused ? theme.accent : alpha(theme.accent, 0.35f), 2.0f);
  }
  snprintf(text, sizeof text, "%0*X", digits, value);
  for (c = 0; c < digits; c++)
  {
    char d[2] = { text[c], 0 };
    ImU32 col = ((c == cursordigit) && (focused)) ? IM_COL32(255, 255, 255, 255) : theme.text;
    dl->AddText(ImVec2(p.x + 5.0f + cw * c, p.y + 3.0f), col, d);
  }
  ImGui::PopFont();
  return clicked;
}

void gui::sectiontitle(const char *title)
{
  ImDrawList *dl = ImGui::GetWindowDrawList();
  ImVec2 p;
  float w = ImGui::GetContentRegionAvail().x;

  ImGui::Spacing();
  p = ImGui::GetCursorScreenPos();
  ImGui::PushFont(fontbold, ImGui::GetStyle().FontSizeBase * 0.85f);
  ImGui::PushStyleColor(ImGuiCol_Text, v4(theme.textdim));
  ImGui::TextUnformatted(title);
  ImGui::PopStyleColor();
  {
    ImVec2 ts = ImGui::CalcTextSize(title);
    float y = p.y + ts.y * 0.5f;
    dl->AddLine(ImVec2(p.x + ts.x + 8.0f, y), ImVec2(p.x + w, y), theme.separator, 1.0f);
  }
  ImGui::PopFont();
}

void gui::tooltip(const char *text)
{
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));
  if (ImGui::BeginTooltip())
  {
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
  }
  ImGui::PopStyleVar();
}

/*
 * Voice meters: an estimate of each voice's envelope from the SID registers
 */

static const float attackms[16] = { 2, 8, 16, 24, 38, 56, 68, 80, 100, 250, 500, 800, 1000, 3000, 5000, 8000 };

void gui::updatemeters(float dt)
{
  static int phase[MAX_CHN] = { 0, 0, 0 };   // 0 = release, 1 = attack, 2 = decay/sustain
  static int prevgate[MAX_CHN] = { 0, 0, 0 };
  int v;

  if (dt <= 0.0f) return;
  if (dt > 0.1f) dt = 0.1f;

  for (v = 0; v < MAX_CHN; v++)
  {
    unsigned char wave = sidreg[v * 7 + 4];
    unsigned char ad = sidreg[v * 7 + 5];
    unsigned char sr = sidreg[v * 7 + 6];
    int gate = wave & 1;
    float sustain = (float)(sr >> 4) / 15.0f;
    float level = voicelevel[v];

    if ((gate) && (!prevgate[v])) phase[v] = 1;
    if (!gate) phase[v] = 0;
    prevgate[v] = gate;

    switch (phase[v])
    {
      case 1:
      level += dt * 1000.0f / attackms[ad >> 4];
      if (level >= 1.0f)
      {
        level = 1.0f;
        phase[v] = 2;
      }
      break;

      case 2:
      if (level > sustain)
      {
        level -= dt * 1000.0f / (attackms[ad & 15] * 3.0f);
        if (level < sustain) level = sustain;
      }
      break;

      default:
      level -= dt * 1000.0f / (attackms[sr & 15] * 3.0f);
      if (level < 0.0f) level = 0.0f;
      break;
    }
    voicelevel[v] = level;
  }
}
