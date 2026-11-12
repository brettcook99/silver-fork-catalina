// GoatTracker GUI: the arrangement (the orderlists of the three channels)
//
// Each channel is a track lane. Patterns are regions whose width is their
// playing time, so what plays at the same time lines up. Transpose and
// repeat commands are small chips in front of the pattern they affect.

#include "gui_internal.h"

using namespace gui;

static float pixelsperrow = 3.0f;
static bool followplayhead = true;

// One entry of an orderlist as laid out on the timeline
struct Segment
{
  int index;        // Position in the orderlist
  int value;        // The orderlist byte
  float x0, x1;     // Horizontal extent
  int kind;         // 0 = pattern, 1 = transpose, 2 = repeat, 3 = loop/end
  int repeat;       // How many times a pattern plays
  int transpose;    // Transpose in effect for a pattern
};

#define MAX_SEGMENTS (MAX_SONGLEN + 4)

static int layoutlane(int c, Segment *seg, float chipw, float *endx)
{
  int s = editorInfo.esnum;
  int len = songlen[s][c];
  int n = 0;
  int i;
  float x = 0.0f;
  int repeat = 1;
  int transpose = 0;

  for (i = 0; i < len; i++)
  {
    int v = songorder[s][c][i];
    Segment &g = seg[n++];
    g.index = i;
    g.value = v;
    g.repeat = 1;
    g.transpose = transpose;

    if ((v >= TRANSDOWN) && (v < LOOPSONG))
    {
      g.kind = 1;
      transpose = v - TRANSUP;
      g.x0 = x;
      g.x1 = x + chipw;
      x = g.x1 + 2.0f;
    }
    else if ((v >= REPEAT) && (v < TRANSDOWN))
    {
      g.kind = 2;
      repeat = v - REPEAT + 1;
      g.x0 = x;
      g.x1 = x + chipw;
      x = g.x1 + 2.0f;
    }
    else
    {
      int rowsplayed;
      g.kind = 0;
      g.repeat = repeat;
      g.transpose = transpose;
      rowsplayed = (v < MAX_PATT) ? pattlen[v] : 1;
      if (rowsplayed < 1) rowsplayed = 1;
      g.x0 = x;
      g.x1 = x + rowsplayed * repeat * pixelsperrow;
      if (g.x1 - g.x0 < chipw) g.x1 = g.x0 + chipw;
      x = g.x1 + 1.0f;
      repeat = 1;
    }
  }

  // Loop / end marker: points at the restart position
  {
    Segment &g = seg[n++];
    g.index = len + 1;
    g.value = songorder[s][c][len + 1];
    g.kind = 3;
    g.repeat = 1;
    g.transpose = 0;
    g.x0 = x + 4.0f;
    g.x1 = g.x0 + chipw * 1.6f;
    x = g.x1;
  }
  *endx = x;
  return n;
}

// Mini preview of the notes of a pattern inside its region
static void drawpreview(ImDrawList *dl, int patt, float x0, float x1, float y0, float y1, int repeat, ImU32 col)
{
  int len = pattlen[patt];
  int r, p;
  float w;
  int lo = 255, hi = 0;

  if ((len < 1) || (patt >= MAX_PATT)) return;
  for (p = 0; p < len; p++)
  {
    int note = pattern[patt][p * 4];
    if ((note >= FIRSTNOTE) && (note <= LASTNOTE))
    {
      if (note < lo) lo = note;
      if (note > hi) hi = note;
    }
  }
  if (lo > hi) return;
  if (hi - lo < 12)
  {
    int mid = (hi + lo) / 2;
    lo = mid - 6;
    hi = mid + 6;
  }

  w = (x1 - x0) / (float)(len * repeat);
  for (r = 0; r < repeat; r++)
  {
    for (p = 0; p < len; p++)
    {
      int note = pattern[patt][p * 4];
      if ((note >= FIRSTNOTE) && (note <= LASTNOTE))
      {
        // Note length: until the next note, rest or keyoff
        int q = p + 1;
        float nx, ny;
        while ((q < len) && (pattern[patt][q * 4] == REST)) q++;
        nx = x0 + (r * len + p) * w;
        ny = y1 - (float)(note - lo) / (float)(hi - lo) * (y1 - y0);
        dl->AddLine(ImVec2(nx, ny), ImVec2(nx + (q - p) * w - 1.0f, ny), col, 1.5f);
      }
    }
  }
}

void gui::drawtracks(bool *open)
{
  ImGuiIO &io = ImGui::GetIO();
  bool focused;
  float fs, headerw, laneh, chipw;
  static Segment seg[MAX_CHN][MAX_SEGMENTS];
  int nseg[MAX_CHN];
  float maxx = 0.0f;
  int c;

  if (!ImGui::Begin(WIN_TRACKS, open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
  {
    ImGui::End();
    return;
  }
  focused = panelfocus(EDIT_ORDERLIST);
  fs = ImGui::GetFontSize();

  // Toolbar: subtune, zoom
  {
    char text[32];
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(theme.textdim), "Subtune");
    ImGui::SameLine();
    if (iconbutton("##prevsub", ICON_PREV, false, 0, ImVec2(fs * 1.4f, fs * 1.4f), "Previous subtune")) prevsong();
    ImGui::SameLine(0.0f, 2.0f);
    snprintf(text, sizeof text, "%02X", editorInfo.esnum);
    ImGui::PushFont(fontmono, 0.0f);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(text);
    ImGui::PopFont();
    ImGui::SameLine(0.0f, 2.0f);
    if (iconbutton("##nextsub", ICON_NEXT, false, 0, ImVec2(fs * 1.4f, fs * 1.4f), "Next subtune")) nextsong();

    ImGui::SameLine(0.0f, 20.0f);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(theme.textdim), "Zoom");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(fs * 8.0f);
    ImGui::SliderFloat("##zoom", &pixelsperrow, 0.5f, 12.0f, "", ImGuiSliderFlags_Logarithmic);
    ImGui::SameLine(0.0f, 16.0f);
    ImGui::Checkbox("Follow playhead", &followplayhead);

    // Position of the cursor in the orderlist
    ImGui::SameLine(0.0f, 20.0f);
    {
      int v = songorder[editorInfo.esnum][editorInfo.eschn][editorInfo.eseditpos];
      const char *what = "Pattern";
      if ((v >= TRANSDOWN) && (v < LOOPSONG)) what = "Transpose";
      else if ((v >= REPEAT) && (v < TRANSDOWN)) what = "Repeat";
      if (editorInfo.eseditpos > songlen[editorInfo.esnum][editorInfo.eschn]) what = "Restart position";
      snprintf(text, sizeof text, "Ch %d  Pos %02X  %s", editorInfo.eschn + 1, editorInfo.eseditpos, what);
      ImGui::AlignTextToFramePadding();
      ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(theme.textfaint), "%s", text);
    }
  }

  headerw = fs * 6.5f;
  chipw = fs * 1.9f;
  {
    float avail = ImGui::GetContentRegionAvail().y;
    laneh = (avail - 4.0f) / MAX_CHN;
    if (laneh < fs * 2.2f) laneh = fs * 2.2f;
    if (laneh > fs * 5.0f) laneh = fs * 5.0f;
  }

  for (c = 0; c < MAX_CHN; c++)
  {
    float endx;
    nseg[c] = layoutlane(c, seg[c], chipw, &endx);
    if (endx > maxx) maxx = endx;
  }

  // Track headers (fixed) + the timeline (scrolls horizontally)
  {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    for (c = 0; c < MAX_CHN; c++)
    {
      float y = p.y + c * laneh;
      char text[16];
      dl->AddRectFilled(ImVec2(p.x, y), ImVec2(p.x + headerw - 6.0f, y + laneh - 3.0f), theme.bgraised, 5.0f);
      dl->AddRectFilled(ImVec2(p.x, y), ImVec2(p.x + 4.0f, y + laneh - 3.0f), chn[c].mute ? theme.textfaint : theme.chn[c], 5.0f, ImDrawFlags_RoundCornersLeft);
      snprintf(text, sizeof text, "Voice %d", c + 1);
      dl->AddText(fontbold, fs, ImVec2(p.x + 12.0f, y + 6.0f), (editorInfo.eschn == c) && focused ? theme.text : theme.textdim, text);
      ImGui::PushID(c);
      ImGui::SetCursorScreenPos(ImVec2(p.x + 12.0f, y + laneh - fs * 1.5f - 6.0f));
      ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 0.0f));
      ImGui::PushStyleColor(ImGuiCol_Button, chn[c].mute ? ImGui::ColorConvertU32ToFloat4(theme.mute) : ImVec4(0.2f, 0.2f, 0.22f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_Text, chn[c].mute ? ImVec4(0.05f, 0.05f, 0.06f, 1.0f) : ImGui::ColorConvertU32ToFloat4(theme.textdim));
      if (laneh > fs * 2.9f)
      {
        if (ImGui::Button("M", ImVec2(fs * 1.5f, fs * 1.5f))) mutechannel(c);
        if (ImGui::IsItemHovered()) tooltip("Mute");
      }
      ImGui::PopStyleColor(2);
      ImGui::PopStyleVar();
      ImGui::PopID();
    }
    ImGui::SetCursorScreenPos(ImVec2(p.x + headerw, p.y));
  }

  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(theme.bgfield));
  ImGui::BeginChild("##timeline", ImVec2(0.0f, laneh * MAX_CHN), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoMove);
  {
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 wp = ImGui::GetWindowPos();
    float scrollx = ImGui::GetScrollX();
    float vieww = ImGui::GetWindowWidth();
    float x0 = wp.x + 6.0f - scrollx;
    int hoverchn = -1;
    int hoverseg = -1;

    ImGui::Dummy(ImVec2(maxx + vieww * 0.5f, laneh * MAX_CHN - 14.0f));

    // Vertical wheel scrolls the timeline sideways; CTRL/CMD+wheel zooms
    if (ImGui::IsWindowHovered())
    {
      if ((io.KeyCtrl) || (io.KeySuper))
      {
        if (io.MouseWheel != 0.0f)
        {
          pixelsperrow *= (io.MouseWheel > 0.0f) ? 1.15f : 1.0f / 1.15f;
          if (pixelsperrow < 0.5f) pixelsperrow = 0.5f;
          if (pixelsperrow > 12.0f) pixelsperrow = 12.0f;
        }
      }
      else if ((io.MouseWheel != 0.0f) && (io.MouseWheelH == 0.0f))
        ImGui::SetScrollX(scrollx - io.MouseWheel * fs * 4.0f);
    }

    // Beat grid
    {
      float step = pixelsperrow * (stepsize > 0 ? stepsize : 4) * 4.0f;
      float gx;
      if (step < 8.0f) step *= 4.0f;
      for (gx = x0; gx < wp.x + vieww; gx += step)
      {
        if (gx < wp.x) continue;
        dl->AddLine(ImVec2(gx, wp.y), ImVec2(gx, wp.y + laneh * MAX_CHN), IM_COL32(255, 255, 255, 7));
      }
    }

    for (c = 0; c < MAX_CHN; c++)
    {
      float y0 = wp.y + c * laneh + 3.0f;
      float y1 = y0 + laneh - 9.0f;
      int i;
      int playpos = -1;

      if (isplaying())
      {
        playpos = chn[c].songptr - 1;
        if (playpos < 0) playpos = 0;
      }

      for (i = 0; i < nseg[c]; i++)
      {
        Segment &g = seg[c][i];
        float a = x0 + g.x0;
        float b = x0 + g.x1;
        bool selected = (editorInfo.eschn == c) && (editorInfo.eseditpos == g.index);
        bool marked = false;
        char text[16];

        if ((b < wp.x) || (a > wp.x + vieww)) continue;

        if (editorInfo.esmarkchn == c)
        {
          int ms = editorInfo.esmarkstart < editorInfo.esmarkend ? editorInfo.esmarkstart : editorInfo.esmarkend;
          int me = editorInfo.esmarkstart < editorInfo.esmarkend ? editorInfo.esmarkend : editorInfo.esmarkstart;
          marked = (g.index >= ms) && (g.index <= me) && (g.kind != 3);
        }

        if ((io.MousePos.x >= a) && (io.MousePos.x < b) && (io.MousePos.y >= y0) && (io.MousePos.y < y1) && (ImGui::IsWindowHovered()))
        {
          hoverchn = c;
          hoverseg = i;
        }

        switch (g.kind)
        {
          case 0:
          {
            ImU32 col = chn[c].mute ? theme.textfaint : theme.chn[c];
            float labelh = fs + 4.0f;
            dl->AddRectFilled(ImVec2(a, y0), ImVec2(b, y1), alpha(col, 0.30f), 4.0f);
            dl->AddRectFilled(ImVec2(a, y0), ImVec2(b, y0 + labelh), alpha(col, 0.75f), 4.0f, ImDrawFlags_RoundCornersTop);
            if (g.value < MAX_PATT)
              drawpreview(dl, g.value, a + 3.0f, b - 3.0f, y0 + labelh + 3.0f, y1 - 4.0f, g.repeat, alpha(mix(col, IM_COL32(255, 255, 255, 255), 0.5f), 0.9f));
            dl->PushClipRect(ImVec2(a, y0), ImVec2(b, y1), true);
            if (g.repeat > 1) snprintf(text, sizeof text, "%02X x%d", g.value, g.repeat);
            else snprintf(text, sizeof text, "%02X", g.value);
            dl->AddText(fontmono, fs * 0.92f, ImVec2(a + 5.0f, y0 + 2.0f), IM_COL32(12, 13, 15, 255), text);
            if (g.transpose)
            {
              char t2[8];
              snprintf(t2, sizeof t2, "%+d", g.transpose);
              dl->AddText(fontmono, fs * 0.8f, ImVec2(b - fs * 2.2f, y0 + 3.0f), IM_COL32(12, 13, 15, 200), t2);
            }
            dl->PopClipRect();
            if (marked) dl->AddRectFilled(ImVec2(a, y0), ImVec2(b, y1), theme.accentsoft, 4.0f);
            if (selected)
              dl->AddRect(ImVec2(a - 1.0f, y0 - 1.0f), ImVec2(b + 1.0f, y1 + 1.0f), focused ? theme.accent : alpha(theme.accent, 0.5f), 5.0f, 0, 2.0f);

            // Playhead
            if ((playpos == g.index) && (chn[c].pattnum == g.value))
            {
              int row = chn[c].pattptr / 4;
              float px = a + row * pixelsperrow * (b - a) / ((g.value < MAX_PATT ? (pattlen[g.value] > 0 ? pattlen[g.value] : 1) : 1) * g.repeat * pixelsperrow);
              dl->AddLine(ImVec2(px, y0 - 2.0f), ImVec2(px, y1 + 2.0f), theme.playhead, 2.0f);
              if ((followplayhead) && (followplay) && (!followsuspended) && (c == 0))
              {
                if ((px < wp.x + vieww * 0.15f) || (px > wp.x + vieww * 0.85f))
                  ImGui::SetScrollX(px - wp.x + scrollx - vieww * 0.3f);
              }
            }
            break;
          }

          case 1:
          case 2:
          {
            ImU32 col = (g.kind == 1) ? theme.command : theme.instr;
            if (g.kind == 1)
            {
              int t = g.value - TRANSUP;
              snprintf(text, sizeof text, "%+d", t);
            }
            else snprintf(text, sizeof text, "x%d", g.value - REPEAT + 1);
            dl->AddRectFilled(ImVec2(a, y0 + 4.0f), ImVec2(b, y1 - 4.0f), alpha(col, 0.22f), 4.0f);
            dl->AddRect(ImVec2(a, y0 + 4.0f), ImVec2(b, y1 - 4.0f), alpha(col, 0.7f), 4.0f);
            {
              ImVec2 ts = fontmono->CalcTextSizeA(fs * 0.85f, FLT_MAX, 0.0f, text);
              dl->AddText(fontmono, fs * 0.85f, ImVec2(a + (b - a - ts.x) * 0.5f, (y0 + y1 - ts.y) * 0.5f), col, text);
            }
            if (marked) dl->AddRectFilled(ImVec2(a, y0), ImVec2(b, y1), theme.accentsoft, 4.0f);
            if (selected)
              dl->AddRect(ImVec2(a - 1.0f, y0 + 3.0f), ImVec2(b + 1.0f, y1 - 3.0f), focused ? theme.accent : alpha(theme.accent, 0.5f), 5.0f, 0, 2.0f);
            break;
          }

          case 3:
          {
            // Song end: loops back to the restart position
            snprintf(text, sizeof text, "%02X", g.value);
            dl->AddRectFilled(ImVec2(a, y0 + 4.0f), ImVec2(b, y1 - 4.0f), IM_COL32(255, 255, 255, 12), 4.0f);
            drawicon(dl, ICON_PREV, ImVec2(a + fs * 0.6f, (y0 + y1) * 0.5f), fs * 0.7f, theme.textdim);
            dl->AddText(fontmono, fs * 0.85f, ImVec2(a + fs * 1.1f, (y0 + y1 - fs * 0.85f) * 0.5f), theme.textdim, text);
            if (selected)
              dl->AddRect(ImVec2(a - 1.0f, y0 + 3.0f), ImVec2(b + 1.0f, y1 - 3.0f), focused ? theme.accent : alpha(theme.accent, 0.5f), 5.0f, 0, 2.0f);
            break;
          }
        }
      }
    }

    // Mouse: click selects, double-click opens the pattern, right-click menu
    if (hoverseg >= 0)
    {
      Segment &g = seg[hoverchn][hoverseg];
      char tip[96];

      switch (g.kind)
      {
        case 0: snprintf(tip, sizeof tip, "Pattern %02X, %d rows%s\nDouble-click to edit", g.value, g.value < MAX_PATT ? pattlen[g.value] : 0, g.repeat > 1 ? " (repeated)" : ""); break;
        case 1: snprintf(tip, sizeof tip, "Transpose %+d halfsteps for the patterns after this", g.value - TRANSUP); break;
        case 2: snprintf(tip, sizeof tip, "Repeat: the next pattern plays %d times", g.value - REPEAT + 1); break;
        default: snprintf(tip, sizeof tip, "End of the song: continues from position %02X", g.value); break;
      }
      if (!ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup)) tooltip(tip);

      if ((ImGui::IsMouseClicked(ImGuiMouseButton_Left)) || (ImGui::IsMouseClicked(ImGuiMouseButton_Right)))
      {
        editorInfo.editmode = EDIT_ORDERLIST;
        if ((io.KeyShift) && (hoverchn == editorInfo.eschn) && (g.kind != 3) && (ImGui::IsMouseClicked(ImGuiMouseButton_Left)))
        {
          editorInfo.esmarkchn = hoverchn;
          editorInfo.esmarkstart = editorInfo.eseditpos;
          editorInfo.esmarkend = g.index;
        }
        else
        {
          editorInfo.eschn = hoverchn;
          editorInfo.eseditpos = g.index;
          editorInfo.escolumn = 0;
          if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) editorInfo.esmarkchn = -1;
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) ImGui::OpenPopup("##trackcontext");
      }
      if ((ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) && (g.kind == 0) && (g.value < MAX_PATT))
      {
        // Open the pattern in the pattern editor
        editorUndoInfo.editorInfo[hoverchn].epnum = g.value;
        editorInfo.epchn = hoverchn;
        editorInfo.eppos = 0;
        editorInfo.editmode = EDIT_PATTERN;
      }
    }

    if (ImGui::BeginPopup("##trackcontext"))
    {
      if (ImGui::MenuItem("Edit Pattern", "RETURN")) sendkey(KEY_ENTER, 13, false);
      if (ImGui::MenuItem("Set Start Position for F2", "CTRL+RETURN")) input_pushkey(KEY_ENTER, 13, KMOD_GT_CTRL);
      if (ImGui::MenuItem("Set End Position for F2", "BACKSPACE")) sendkey(KEY_BACKSPACE, 8, false);
      ImGui::Separator();
      if (ImGui::MenuItem("Insert Position", "INS")) sendkey(KEY_INS, 0, false);
      if (ImGui::MenuItem("Delete Position", "DEL")) sendkey(KEY_DEL, 127, false);
      if (ImGui::MenuItem("Insert Repeat", "SHIFT+R")) sendkey(KEY_R, 'r', true);
      if (ImGui::MenuItem("Insert Transpose Up", "+")) sendkey(0, '+', false);
      if (ImGui::MenuItem("Insert Transpose Down", "-")) sendkey(0, '-', false);
      ImGui::Separator();
      if (ImGui::MenuItem("Cut", "SHIFT+X")) sendkey(KEY_X, 'x', true);
      if (ImGui::MenuItem("Copy", "SHIFT+C")) sendkey(KEY_C, 'c', true);
      if (ImGui::MenuItem("Paste", "SHIFT+V")) sendkey(KEY_V, 'v', true);
      if (ImGui::MenuItem("Select Whole Orderlist", "SHIFT+L")) sendkey(KEY_L, 'l', true);
      ImGui::EndPopup();
    }
  }
  ImGui::EndChild();
  ImGui::PopStyleColor();

  if (focused) drawfocusframe();
  ImGui::End();
}
