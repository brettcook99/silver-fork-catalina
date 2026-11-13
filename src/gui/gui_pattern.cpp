// GoatTracker GUI: the pattern editor
//
// Three channel columns showing the patterns the channels edit. The grid
// scrolls freely with the mouse wheel / trackpad (the edit cursor stays
// where it is). Moving the cursor with the keyboard centers it, and with
// follow playback on the playing row stays centered while the song plays,
// until the view is scrolled by hand.

#include "gui_internal.h"

using namespace gui;

// Character positions of the columns inside a channel:
// "C-4 01 F06" = note 0-2, instrument 4-5, command 7, data 8-9
static const int columnchar[6] = { 0, 4, 5, 7, 8, 9 };

static bool dragging = false;
static int dragchn = 0;
static int dragstartrow = 0;
static int lastcursorpos = -1;
static int lastcursorchn = -1;
static int lastcursorpatt = -1;
static bool cursorsetbymouse = false;

static int pattnum(int c)
{
  return editorUndoInfo.editorInfo[c].epnum;
}

// Is channel c soloed (the only one not muted)?
static bool soloed(int c)
{
  int d;
  if (chn[c].mute) return false;
  for (d = 0; d < MAX_CHN; d++)
  {
    if ((d != c) && (!chn[d].mute)) return false;
  }
  return true;
}

static void togglesolo(int c)
{
  int d;
  bool wassolo = soloed(c);
  for (d = 0; d < MAX_CHN; d++)
    chn[d].mute = wassolo ? 0 : (d != c);
}

static int playingrow(int c)
{
  int row;
  if ((!isplaying()) || (chn[c].pattnum != pattnum(c))) return -1;
  row = chn[c].pattptr / 4;
  if (row > pattlen[chn[c].pattnum]) row = pattlen[chn[c].pattnum];
  return row;
}

static void channelheader(int c, ImVec2 p, float w, float h, bool focused)
{
  ImDrawList *dl = ImGui::GetWindowDrawList();
  int patt = pattnum(c);
  float fs = ImGui::GetFontSize();
  float bh = h - 8.0f;
  char text[32];
  bool active = (editorInfo.epchn == c);

  dl->AddRectFilled(p, ImVec2(p.x + w - 4.0f, p.y + h), theme.bgraised, 5.0f);
  dl->AddRectFilled(p, ImVec2(p.x + 4.0f, p.y + h), chn[c].mute ? theme.textfaint : theme.chn[c], 5.0f, ImDrawFlags_RoundCornersLeft);
  if ((active) && (focused))
    dl->AddRect(p, ImVec2(p.x + w - 4.0f, p.y + h), alpha(theme.accent, 0.6f), 5.0f);

  ImGui::PushID(c);

  // Channel number
  snprintf(text, sizeof text, "%d", c + 1);
  dl->AddText(fontbold, fs * 1.1f, ImVec2(p.x + 12.0f, p.y + (h - fs * 1.1f) * 0.5f), theme.text, text);

  // Pattern number with arrows
  {
    float x = p.x + 12.0f + fs * 1.2f;
    ImGui::SetCursorScreenPos(ImVec2(x, p.y + 4.0f));
    if (iconbutton("##prev", ICON_PREV, false, 0, ImVec2(bh * 0.8f, bh), "Previous pattern ([)"))
    {
      editorInfo.epchn = c;
      prevpattern();
    }
    x += bh * 0.8f + 2.0f;
    snprintf(text, sizeof text, "%02X", patt);
    ImGui::PushFont(fontmono, 0.0f);
    dl->AddText(ImVec2(x + 2.0f, p.y + (h - ImGui::GetFontSize()) * 0.5f), theme.chn[c], text);
    x += ImGui::CalcTextSize("00").x + 4.0f;
    ImGui::PopFont();
    ImGui::SetCursorScreenPos(ImVec2(x, p.y + 4.0f));
    if (iconbutton("##next", ICON_NEXT, false, 0, ImVec2(bh * 0.8f, bh), "Next pattern (])"))
    {
      editorInfo.epchn = c;
      nextpattern();
    }
    x += bh * 0.8f + 8.0f;
    snprintf(text, sizeof text, "%d rows", pattlen[patt]);
    if (x + ImGui::CalcTextSize(text).x < p.x + w - bh * 2.6f)
      dl->AddText(ImVec2(x, p.y + (h - fs) * 0.5f), theme.textdim, text);
  }

  // Mute & solo
  {
    float x = p.x + w - 4.0f - (bh + 4.0f) * 2.0f - 2.0f;
    ImGui::SetCursorScreenPos(ImVec2(x, p.y + 4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_Button, chn[c].mute ? ImGui::ColorConvertU32ToFloat4(theme.mute) : ImVec4(0.2f, 0.2f, 0.22f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, chn[c].mute ? ImVec4(0.05f, 0.05f, 0.06f, 1.0f) : ImGui::ColorConvertU32ToFloat4(theme.textdim));
    if (ImGui::Button("M", ImVec2(bh, bh))) mutechannel(c);
    ImGui::PopStyleColor(2);
    if (ImGui::IsItemHovered()) tooltip("Mute (SHIFT+F4 mutes the current channel)");
    ImGui::SameLine(0.0f, 4.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, soloed(c) ? ImGui::ColorConvertU32ToFloat4(theme.solo) : ImVec4(0.2f, 0.2f, 0.22f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, soloed(c) ? ImVec4(0.05f, 0.05f, 0.06f, 1.0f) : ImGui::ColorConvertU32ToFloat4(theme.textdim));
    if (ImGui::Button("S", ImVec2(bh, bh))) togglesolo(c);
    ImGui::PopStyleColor(2);
    if (ImGui::IsItemHovered()) tooltip("Solo");
    ImGui::PopStyleVar();
  }

  ImGui::PopID();
}

static void contextmenu(void)
{
  if (!ImGui::BeginPopup("##patterncontext")) return;

  bool marked = (editorInfo.epmarkchn != -1);
  if (ImGui::MenuItem("Play From Here", "SPACE")) cmd_post(GTCMD_PLAYCURSOR);
  ImGui::Separator();
  if (ImGui::MenuItem(marked ? "Cut Selection" : "Cut Pattern", "SHIFT+X")) cmd_post(GTCMD_CUT);
  if (ImGui::MenuItem(marked ? "Copy Selection" : "Copy Pattern", "SHIFT+C")) cmd_post(GTCMD_COPY);
  if (ImGui::MenuItem("Paste", "SHIFT+V")) cmd_post(GTCMD_PASTE);
  if (ImGui::MenuItem("Select Whole Pattern", "SHIFT+L")) cmd_post(GTCMD_SELECTALL);
  if (ImGui::MenuItem("Invert Selection / Pattern", "SHIFT+I")) cmd_post(GTCMD_INVERTSEL);
  ImGui::Separator();
  if (ImGui::MenuItem("Transpose Up", "SHIFT+Q")) cmd_post(GTCMD_TRANSPOSEUP);
  if (ImGui::MenuItem("Transpose Down", "SHIFT+A")) cmd_post(GTCMD_TRANSPOSEDOWN);
  if (ImGui::MenuItem("Transpose Octave Up", "SHIFT+W")) cmd_post(GTCMD_TRANSPOSEOCTUP);
  if (ImGui::MenuItem("Transpose Octave Down", "SHIFT+S")) cmd_post(GTCMD_TRANSPOSEOCTDOWN);
  ImGui::Separator();
  if (ImGui::MenuItem("Insert Row", "CTRL+BACKSPACE")) cmd_post(GTCMD_INSERTROW);
  if (ImGui::MenuItem("Delete Row", "BACKSPACE")) cmd_post(GTCMD_DELETEROW);
  ImGui::Separator();
  if (ImGui::MenuItem("Split Pattern Here", "SHIFT+K")) cmd_post(GTCMD_SPLITPATTERN);
  if (ImGui::MenuItem("Join With Next Pattern", "SHIFT+J")) cmd_post(GTCMD_JOINPATTERN);
  if (ImGui::MenuItem("Shrink Pattern (Half Length)", "SHIFT+O")) cmd_post(GTCMD_SHRINKPATTERN);
  if (ImGui::MenuItem("Expand Pattern (Double Length)", "SHIFT+P")) cmd_post(GTCMD_EXPANDPATTERN);
  ImGui::EndPopup();
}

void gui::drawpatterneditor(bool *open)
{
  ImGuiIO &io = ImGui::GetIO();
  bool focused;
  float cw, rh, fs, headerh, rownumw, chw, gridx;
  int c, rows;

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));
  if (!ImGui::Begin(WIN_PATTERN, open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
  {
    ImGui::End();
    ImGui::PopStyleVar();
    return;
  }
  ImGui::PopStyleVar();
  focused = panelfocus(EDIT_PATTERN);

  fs = ImGui::GetFontSize();
  ImGui::PushFont(fontmono, 0.0f);
  cw = ImGui::CalcTextSize("0").x;
  rh = ImGui::GetFontSize() + 5.0f;
  ImGui::PopFont();

  headerh = fs * 2.0f + 4.0f;
  rownumw = cw * 3.0f + 18.0f;
  {
    float avail = ImGui::GetContentRegionAvail().x - rownumw;
    float minw = cw * 10.0f + 30.0f;
    chw = avail / MAX_CHN;
    if (chw < minw) chw = minw;
    if (chw > cw * 26.0f) chw = cw * 26.0f;
  }

  // Channel headers
  {
    ImVec2 p = ImGui::GetCursorScreenPos();
    for (c = 0; c < MAX_CHN; c++)
      channelheader(c, ImVec2(p.x + rownumw + c * chw, p.y), chw, headerh, focused);
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + headerh + 6.0f));
  }

  // Rows: the longest pattern + its end mark
  rows = 0;
  for (c = 0; c < MAX_CHN; c++)
  {
    if (pattlen[pattnum(c)] + 1 > rows) rows = pattlen[pattnum(c)] + 1;
  }

  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(theme.bgfield));
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
  ImGui::BeginChild("##grid", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_NoMove);
  {
    ImDrawList *dl = ImGui::GetWindowDrawList();
    float viewh = ImGui::GetWindowHeight();
    float pad = viewh * 0.5f - rh * 0.5f;
    float scrolly;
    ImVec2 origin;
    int first, last, p;
    int curpatt = pattnum(editorInfo.epchn);
    bool wheel = (io.MouseWheel != 0.0f) && ImGui::IsWindowHovered();

    if (pad < 0.0f) pad = 0.0f;
    // Space above and below so that any row can be in the middle
    ImGui::Dummy(ImVec2(rownumw + chw * MAX_CHN, pad * 2.0f + rows * rh));

    // Scrolling by hand while following the playback stops the following
    if ((wheel) && (isplaying()) && (followplay)) followsuspended = 1;

    // Keep the cursor / playing row in the middle
    if ((isplaying()) && (followplay) && (!followsuspended))
    {
      int prow = chn[editorInfo.epchn].pattptr / 4;
      if (prow > pattlen[chn[editorInfo.epchn].pattnum]) prow = pattlen[chn[editorInfo.epchn].pattnum];
      ImGui::SetScrollY(prow * rh);
    }
    else if ((editorInfo.eppos != lastcursorpos) || (editorInfo.epchn != lastcursorchn) || (curpatt != lastcursorpatt))
    {
      if (!cursorsetbymouse) ImGui::SetScrollY(editorInfo.eppos * rh);
    }
    cursorsetbymouse = false;
    lastcursorpos = editorInfo.eppos;
    lastcursorchn = editorInfo.epchn;
    lastcursorpatt = curpatt;

    scrolly = ImGui::GetScrollY();
    origin = ImGui::GetWindowPos();
    gridx = origin.x;
    {
      float top = origin.y + pad - scrolly;
      first = (int)((scrolly - pad) / rh) - 1;
      last = (int)((scrolly - pad + viewh) / rh) + 1;
      if (first < 0) first = 0;
      if (last > rows - 1) last = rows - 1;

      ImGui::PushFont(fontmono, 0.0f);

      for (p = first; p <= last; p++)
      {
        float y = top + p * rh;
        float ty = y + (rh - ImGui::GetFontSize()) * 0.5f;
        bool stepRow = (stepsize > 0) && ((p % stepsize) == 0);
        bool barRow = (stepsize > 0) && ((p % (stepsize * 4)) == 0);
        char text[16];

        // Row bands
        if (barRow) dl->AddRectFilled(ImVec2(gridx, y), ImVec2(gridx + rownumw + chw * MAX_CHN, y + rh), theme.bgrowbar);
        else if (stepRow) dl->AddRectFilled(ImVec2(gridx, y), ImVec2(gridx + rownumw + chw * MAX_CHN, y + rh), theme.bgrowalt);
        if (p == editorInfo.eppos)
          dl->AddRectFilled(ImVec2(gridx, y), ImVec2(gridx + rownumw + chw * MAX_CHN, y + rh), alpha(theme.accent, focused ? 0.16f : 0.08f));

        // Row number
        if (patterndispmode & 1) snprintf(text, sizeof text, "%02X", p);
        else snprintf(text, sizeof text, "%03d", p);
        dl->AddText(ImVec2(gridx + 8.0f, ty), stepRow ? theme.textdim : theme.textfaint, text);

        for (c = 0; c < MAX_CHN; c++)
        {
          int patt = pattnum(c);
          float x = gridx + rownumw + c * chw + 10.0f;
          float cx0 = gridx + rownumw + c * chw;
          float fade = chn[c].mute ? 0.35f : 1.0f;
          unsigned char *row;
          int note, ins, cmd, data;

          if (p > pattlen[patt]) continue;
          row = &pattern[patt][p * 4];

          // Playing row
          if (playingrow(c) == p)
          {
            dl->AddRectFilled(ImVec2(cx0 + 2.0f, y), ImVec2(cx0 + chw - 4.0f, y + rh), theme.playheadsoft, 3.0f);
            dl->AddRectFilled(ImVec2(cx0 + 2.0f, y + 2.0f), ImVec2(cx0 + 5.0f, y + rh - 2.0f), theme.playhead, 1.0f);
          }

          // Selection
          if (editorInfo.epmarkchn == c)
          {
            int a = editorInfo.epmarkstart < editorInfo.epmarkend ? editorInfo.epmarkstart : editorInfo.epmarkend;
            int b = editorInfo.epmarkstart < editorInfo.epmarkend ? editorInfo.epmarkend : editorInfo.epmarkstart;
            if ((p >= a) && (p <= b))
              dl->AddRectFilled(ImVec2(cx0 + 2.0f, y), ImVec2(cx0 + chw - 4.0f, y + rh), theme.accentsoft);
          }

          if (row[0] == ENDPATT)
          {
            // End of pattern
            float ly = y + rh * 0.5f;
            float lx;
            for (lx = cx0 + 8.0f; lx < cx0 + chw - 10.0f; lx += 6.0f)
              dl->AddLine(ImVec2(lx, ly), ImVec2(lx + 3.0f, ly), alpha(theme.chn[c], 0.5f * fade), 1.0f);
            dl->AddText(ImVec2(x + cw * 3.0f, ty), alpha(theme.textfaint, fade), " end ");
          }
          else
          {
            note = row[0];
            ins = row[1];
            cmd = row[2];
            data = row[3];

            if ((note >= FIRSTNOTE) && (note <= KEYON))
            {
              ImU32 col = theme.note;
              if (note == REST) col = theme.rest;
              if (note == KEYOFF) col = theme.keyoff;
              if (note == KEYON) col = theme.keyoff;
              dl->AddText(ImVec2(x, ty), alpha(col, fade), notename[note - FIRSTNOTE]);
            }
            else
              dl->AddText(ImVec2(x, ty), alpha(theme.rest, fade), "???");

            if (ins) snprintf(text, sizeof text, "%02X", ins);
            else strcpy(text, "..");
            dl->AddText(ImVec2(x + cw * 4.0f, ty), alpha(ins ? theme.instr : theme.rest, fade), text);

            if ((cmd) || (data))
            {
              snprintf(text, sizeof text, "%X", cmd);
              dl->AddText(ImVec2(x + cw * 7.0f, ty), alpha(theme.command, fade), text);
              snprintf(text, sizeof text, "%02X", data);
              dl->AddText(ImVec2(x + cw * 8.0f, ty), alpha(theme.cmddata, fade), text);
            }
            else
              dl->AddText(ImVec2(x + cw * 7.0f, ty), alpha(theme.rest, fade), "...");
          }

          // Cursor
          if ((c == editorInfo.epchn) && (p == editorInfo.eppos))
          {
            int col = editorInfo.epcolumn;
            float cx, cwidth;
            if ((col < 0) || (col > 5)) col = 0;
            cx = x + cw * columnchar[col];
            cwidth = (col == 0) ? cw * 3.0f : cw;
            if (focused)
              dl->AddRect(ImVec2(cx - 2.0f, y + 1.0f), ImVec2(cx + cwidth + 2.0f, y + rh - 1.0f), theme.accent, 3.0f, 0, 2.0f);
            else
              dl->AddRect(ImVec2(cx - 2.0f, y + 1.0f), ImVec2(cx + cwidth + 2.0f, y + rh - 1.0f), alpha(theme.accent, 0.45f), 3.0f, 0, 1.0f);
          }
        }
      }

      // Channel separators
      for (c = 0; c <= MAX_CHN; c++)
      {
        float sx = gridx + rownumw + c * chw - 2.0f;
        dl->AddLine(ImVec2(sx, origin.y), ImVec2(sx, origin.y + viewh), theme.separator, 1.0f);
      }
      ImGui::PopFont();

      // Mouse
      if ((ImGui::IsWindowHovered()) && (!ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup)))
      {
        float mx = io.MousePos.x - (gridx + rownumw);
        int mc = (int)(mx / chw);
        int mrow = (int)floorf((io.MousePos.y - top) / rh);
        int mcol = 0;

        if ((mx >= 0.0f) && (mc >= 0) && (mc < MAX_CHN))
        {
          float charpos = (mx - mc * chw - 10.0f) / cw;
          int k;
          for (k = 0; k < 6; k++)
          {
            if (charpos >= columnchar[k] - 0.5f) mcol = k;
          }
          if (charpos < 3.5f) mcol = 0;
          if (mrow < 0) mrow = 0;
          if (mrow > pattlen[pattnum(mc)]) mrow = pattlen[pattnum(mc)];

          if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
          {
            editorInfo.editmode = EDIT_PATTERN;
            if ((io.KeyShift) && (mc == editorInfo.epchn))
            {
              // Extend the selection from the cursor
              editorInfo.epmarkchn = mc;
              editorInfo.epmarkstart = editorInfo.eppos;
              editorInfo.epmarkend = mrow;
            }
            else
            {
              editorInfo.epchn = mc;
              editorInfo.eppos = mrow;
              editorInfo.epcolumn = mcol;
              editorInfo.epmarkchn = -1;
              dragging = true;
              dragchn = mc;
              dragstartrow = mrow;
            }
            cursorsetbymouse = true;
          }
          if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
          {
            int a = editorInfo.epmarkstart < editorInfo.epmarkend ? editorInfo.epmarkstart : editorInfo.epmarkend;
            int b = editorInfo.epmarkstart < editorInfo.epmarkend ? editorInfo.epmarkend : editorInfo.epmarkstart;
            editorInfo.editmode = EDIT_PATTERN;
            // Keep the selection if clicking inside it
            if ((editorInfo.epmarkchn != mc) || (mrow < a) || (mrow > b))
            {
              editorInfo.epmarkchn = -1;
              editorInfo.epchn = mc;
              editorInfo.eppos = mrow;
              editorInfo.epcolumn = mcol;
            }
            cursorsetbymouse = true;
            ImGui::OpenPopup("##patterncontext");
          }
        }
      }

      // Drag to select (also when the mouse leaves the grid)
      if (dragging)
      {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) dragging = false;
        else
        {
          int mrow = (int)floorf((io.MousePos.y - top) / rh);
          if (mrow < 0) mrow = 0;
          if (mrow > pattlen[pattnum(dragchn)]) mrow = pattlen[pattnum(dragchn)];
          if (mrow != dragstartrow)
          {
            editorInfo.epmarkchn = dragchn;
            editorInfo.epmarkstart = dragstartrow;
            editorInfo.epmarkend = mrow;
          }
          // Scroll when dragging past the edges
          if (io.MousePos.y < origin.y + rh) ImGui::SetScrollY(scrolly - rh * 0.5f);
          if (io.MousePos.y > origin.y + viewh - rh) ImGui::SetScrollY(scrolly + rh * 0.5f);
          cursorsetbymouse = true;
        }
      }
    }
    contextmenu();
  }
  ImGui::EndChild();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();

  if (focused) drawfocusframe();
  ImGui::End();
}
