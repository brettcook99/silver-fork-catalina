// GoatTracker GUI: song information and settings

#include "gui_internal.h"

using namespace gui;

static void packersetting(const char *label, unsigned *value, const char *tip)
{
  bool on = (*value != 0);
  if (ImGui::Checkbox(label, &on))
  {
    undoCreateEditorInfoBackup();
    *value = on ? 1 : 0;
    undoAddEditorSettingsToList();
  }
  if (ImGui::IsItemHovered()) tooltip(tip);
}

void gui::drawsong(bool *open)
{
  static int prevmode = -1;
  bool focused;
  bool justfocused;
  float labelw;
  int c;

  if (!ImGui::Begin(WIN_SONG, open))
  {
    ImGui::End();
    return;
  }
  focused = panelfocus(EDIT_NAMES);
  justfocused = focused && (prevmode != EDIT_NAMES);
  prevmode = editorInfo.editmode;
  labelw = ImGui::GetFontSize() * 5.5f;

  sectiontitle("SONG");
  {
    const char *labels[3] = { "Name", "Author", "Copyright" };
    char *fields[3] = { songname, authorname, copyrightname };
    for (c = 0; c < 3; c++)
    {
      ImGui::AlignTextToFramePadding();
      ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(theme.textdim), "%s", labels[c]);
      ImGui::SameLine(labelw);
      ImGui::SetNextItemWidth(-FLT_MIN);
      ImGui::PushID(c);
      // F8 (songname editor) puts the keyboard into the field
      if ((justfocused) && (editorInfo.enpos == c)) ImGui::SetKeyboardFocusHere();
      ImGui::InputText("##field", fields[c], MAX_STR);
      if (ImGui::IsItemActivated()) editorInfo.enpos = c;
      ImGui::PopID();
    }
  }

  sectiontitle("ARRANGEMENT");
  {
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(theme.textdim), "Subtune %02X", editorInfo.esnum);
    ImGui::SameLine();
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(theme.textfaint), "  orderlist lengths %d / %d / %d",
      songlen[editorInfo.esnum][0], songlen[editorInfo.esnum][1], songlen[editorInfo.esnum][2]);
  }

  sectiontitle("EDITING");
  {
    int step = stepsize;
    int mode = (int)keypreset;
    const char *modes[] = { "Protracker", "DMC", "Janko" };
    bool hexrows = (patterndispmode & 1) != 0;

    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(theme.textdim), "Highlight");
    ImGui::SameLine(labelw);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0f);
    if (ImGui::SliderInt("##step", &step, 1, 16, "every %d rows")) stepsize = step;
    if (ImGui::IsItemHovered()) tooltip("Row highlighting in the pattern editor (SHIFT+M, N)");

    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(theme.textdim), "Note keys");
    ImGui::SameLine(labelw);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0f);
    if (ImGui::Combo("##keys", &mode, modes, 3)) keypreset = (unsigned)mode;
    if (ImGui::IsItemHovered()) tooltip("How the computer keyboard plays notes");

    if (ImGui::Checkbox("Hexadecimal row numbers", &hexrows))
      patterndispmode = (patterndispmode & ~1u) | (hexrows ? 1u : 0u);
  }

  sectiontitle("PLAYER (EXPORT) OPTIONS");
  packersetting("Fine vibrato", &editorInfo.finevibrato, "Finevibrato conversion when exporting (FV)");
  ImGui::SameLine();
  packersetting("Pulse skipping", &editorInfo.optimizepulse, "Pulse optimization / skipping (PO)");
  packersetting("Realtime effect skipping", &editorInfo.optimizerealtime, "Realtime effect optimization / skipping (RO)");

  if (focused) drawfocusframe();
  ImGui::End();
}
