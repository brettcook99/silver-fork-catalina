// GoatTracker GUI: the help window

#include "gui_internal.h"

using namespace gui;

struct HelpLine
{
  const char *key;
  const char *text;
};

struct HelpSection
{
  const char *title;
  const HelpLine *lines;
};

#ifdef __APPLE__
#define CMD "CMD"
#define FINDERTEXT ", or double-click them in the Finder"
#define OPENPLAYTEXT "A song or SID file double-clicked in the Finder (or given on the command line) is loaded and starts playing. Dropping a file onto the window only loads it."
#else
#define CMD "CTRL"
#define FINDERTEXT ""
#define OPENPLAYTEXT "A song or SID file given on the command line (or double-clicked, when GoatTracker Ultimate opens that kind of file) is loaded and starts playing. Dropping a file onto the window only loads it."
#endif

static const HelpLine interfacelines[] = {
  { "Panels", "Arrangement (top), Pattern Editor (middle), Library and Inspector (left), Tables and Song (right). Drag a panel's tab to move or dock it elsewhere; View > Reset Layout restores the default." },
  { "Keyboard focus", "The panel with the blue frame gets the keys. Click a panel, or use F5-F8 / TAB, to move the focus." },
  { "Mouse wheel", "Scrolls the panel under the pointer without moving the edit cursor. Scrolling the pattern editor while the song plays stops following the playback until you click the Follow button or stop. CTRL+SHIFT+wheel (or " CMD "+SHIFT+wheel) selects the instrument, anywhere." },
  { "Arrangement", "Patterns are regions as long as they play. Click to select, double-click to edit the pattern, SHIFT+click to mark, right-click for more. " CMD "+wheel zooms." },
  { "Pattern editor", "Click a cell to move the cursor, drag to select rows, SHIFT+click to extend, right-click for editing commands. M / S mute and solo a voice." },
  { "Instruments", "Pick an instrument in the Library (double-click to rename). The Inspector has knobs for the envelope, waveform buttons, and the arrows jump to the instrument's tables. Hold the speaker / Hold to Play to hear it." },
  { "Tables", "Hover a row to see what it does. Click to edit, drag to mark, right-click for table commands." },
  { "MIDI input", "The CHORD button in the control bar (or ALT/OPTION + numpad 1-3) spreads MIDI notes over 2 or 3 tracks, from the cursor's track: notes played one by one go to the next track in turn, so they ring on together, and keys struck together (a chord) go to the tracks on one row. Click for more tracks, right-click for fewer; CHORD 1 is the usual input." },
  { "Steps", "The STEP button sets how many rows are skipped after a note is entered: 1 puts notes on every 2nd row, 2 on every 3rd, up to 16. Click for more, right-click for fewer, or the wheel." },
  { "Files", "File > Open (F10) opens songs and imports SID, MIDI and chipsynth files. You can also drop files onto the window" FINDERTEXT "." },
  { "Opening and playing", OPENPLAYTEXT },
  { "Text size", CMD " + = bigger, " CMD " + - smaller, " CMD " + 0 default. ALT/OPTION+ENTER toggles full screen." },
  { "Retro interface", "GoatTracker Ultimate starts with the classic GoatTracker screen. Hold Shift (or Option on macOS) while it starts to choose this interface for that time." },
  { "SID Designer", "A C64 screen laid out like chipsynth C64 for designing instruments: CTRL+numpad 7 (from anywhere), the rainbow button in the control bar, View > SID Designer, " CMD "+D or ALT/OPTION+numpad 5. See below." },
  { NULL, NULL }
};

static const HelpLine designerlines[] = {
  { "Opening / closing", "CTRL+numpad 7, " CMD "+D, ALT/OPTION+numpad 5, Tracker at the right of the title bar or ESC (RUN/STOP). It opens on the current instrument (from the patterns: the one the cursor's row plays with); the tracker returns to it. The title bar's settings work there too." },
  { "Patch", "Up to three oscillators, each an instrument played on SID voice 1, 2 or 3 together, so that sync and ring modulation work between them. The oscillators of the patch BASS are the instruments named BASS:1, BASS:2 and BASS:3 (as chipsynth patches are imported). Click an oscillator's light to add it (a copy of the first, in the next empty instrument) or mute it." },
  { "PATCH page", "Waveforms, FILT / SYNC / RING / TEST, hard restart and legato, pulse width and the envelope (drag the bars, SHIFT+drag for fine steps, or the wheel), transpose / fixed pitch, vibrato and the gate timer. The filter panel sets cutoff, resonance, type and the voices it filters; click the chip display to switch 6581 / 8580 and PAL / NTSC." },
  { "WAVE / PULSE / FILTER", "The instrument's wave, pulse and filter table rows as steps: waveform bits and arpeggio notes, pulse width and filter sweeps with a graph of the result, where the table loops, presets and sweep makers. SHARED: other instruments use the same rows; UNSHARE gives this one its own copy." },
  { "EMU page", "SID chip, clock, player speed, hard restart, reSID mode, the SID registers and the voices." },
  { "BROWSER page", "The instruments (.ins) in a folder: SET FOLDER chooses it, double-click a folder to open it, BACKSPACE goes up. UP / DOWN or a click tries an instrument (it plays a note); RETURN or a double-click loads it into the patch. Leaving the page puts back an instrument tried but not loaded." },
  { "MIDI knobs", "Control changes: attack CC 73, decay CC 75, sustain CC 70 or 76, release CC 72 (the oscillators heard), filter cutoff CC 74 and resonance CC 71. A turn of a knob is one undo step." },
  { "CLEAR", "Makes the patch an init patch: one oscillator playing a plain sawtooth with full sustain. The other oscillators' instruments are emptied and the table rows only the patch used are deleted. " CMD "+Z undoes it." },
  { "Keys", "Note keys play the patch (MIDI too), PAGE UP / DOWN = octave, TAB = next page, cursor LEFT / RIGHT = next control, UP / DOWN = change it, RETURN / SPACE = press it, F1-F4 = play / stop the song, " CMD "+Z = undo." },
  { "Characters", "Built in. A C64 character ROM file named chargen (e.g. from VICE) in the settings folder is used instead." },
  { NULL, NULL }
};

static const HelpLine generallines[] = {
  { "F1", "Play from the beginning" },
  { "F2", "Play from the current position" },
  { "F3", "Play the current pattern" },
  { "F4", "Stop (SHIFT+F4 mutes the current channel)" },
  { "SHIFT+F1-F3", "Play with the opposite follow mode" },
  { "F5 / F6 / F7 / F8", "Pattern editor / Arrangement / Instrument & tables / Song info" },
  { "TAB", "Next editor (SHIFT+TAB previous)" },
  { "F9", "Export (pack, relocate & save SID, PRG or BIN)" },
  { "F10 / F11", "Open / save (in the instrument editor: the instrument)" },
  { "F12", "This help" },
  { "CTRL+NUMPAD 7", "SID Designer and back (" CMD "+D, ALT+NUMPAD 5 too)" },
  { "CTRL+[ / ] / \\", "Mute or unmute channel 1 / 2 / 3 (" CMD " too on macOS)" },
  { "SHIFT+F5 / F6", "Speed multiplier down / up" },
  { "SHIFT+F7", "Hard restart ADSR" },
  { "SHIFT+F8", "Switch between 6581 and 8580" },
  { "SHIFT+F9", "Toggle reSID interpolation" },
  { "NUMPAD 0", "Play from the start of the pattern / stop" },
  { "NUMPAD .", "Edit mode / jam mode (the middle mouse button too)" },
  { "ALT+NUMPAD 1-3", "MIDI input on 1-3 tracks (OPTION on macOS)" },
  { "CTRL+NUMPAD 1-3", "Go to track 1 / 2 / 3 in the pattern editor" },
  { "SHIFT+, .", "Move the song start position & restart" },
  { "- +", "Select instrument" },
  { "/ *", "Select octave" },
  { CMD "+Z", "Undo" },
  { CMD "+1-9", "Same as F1-F9" },
  { CMD "+F", "Follow playback" },
  { "SHIFT+ESC", "Clear / optimize the song" },
  { "ESC", "Quit" },
  { NULL, NULL }
};

static const HelpLine patternlines[] = {
  { "Note keys", "Enter notes like on a piano (MIDI keyboard too)" },
  { "0-9, A-F", "Instrument number, command and its data" },
  { "SPACE", "Play from the cursor / stop, in every panel but the song info (SHIFT+SPACE: without follow)" },
  { "BACKSPACE", "Insert a rest" },
  { "RETURN", "Keyoff (SHIFT+RETURN: keyon)" },
  { "INS / DEL", "Insert / delete a row (on the end mark: change the length)" },
  { "< >", "Select instrument (like - +)" },
  { "[ ]", "Select pattern" },
  { "BACKQUOTE", "Select channel" },
  { "NUMPAD 1-8", "Go to row 0, 8, 16 ... 56" },
  { "SHIFT+CURSOR", "Mark" },
  { "SHIFT+Q / A", "Transpose half step up / down" },
  { "SHIFT+W / S", "Transpose octave up / down" },
  { "SHIFT+E / R", "Copy / paste commands" },
  { "SHIFT+H", "Make hifi vibrato / portamento speed" },
  { "SHIFT+I", "Invert selection / pattern" },
  { "SHIFT+J / K", "Join / split pattern" },
  { "SHIFT+L", "Mark / unmark the whole pattern" },
  { "SHIFT+M / N", "Highlighting step" },
  { "SHIFT+O / P", "Shrink / expand pattern" },
  { "SHIFT+X / C / V", "Cut / copy / paste" },
  { "SHIFT+Z", "Cycle auto-advance mode" },
  { "SHIFT+1 / 2 / 3", "Mute channel" },
  { NULL, NULL }
};

static const HelpLine orderlines[] = {
  { "0-9, A-F", "Enter pattern numbers" },
  { "SPACE", "Play from this position / stop" },
  { "CTRL+RETURN", "Set the start position for F2 (SHIFT too: all channels; CMD+RETURN on macOS)" },
  { "BACKSPACE", "Set the end position for F2" },
  { "RETURN", "Edit the pattern (SHIFT: all channels)" },
  { "< >", "Select subtune" },
  { "- +", "Insert transpose down / up" },
  { "SHIFT+R", "Insert repeat" },
  { "SHIFT+CURSOR", "Mark" },
  { "SHIFT+L", "Mark / unmark the whole orderlist" },
  { "SHIFT+X / C / V", "Cut / copy / paste" },
  { "SHIFT+1 / 2 / 3", "Swap the orderlist with channel 1 / 2 / 3" },
  { NULL, NULL }
};

static const HelpLine instrumentlines[] = {
  { "0-9, A-F", "Enter values" },
  { "CTRL+RETURN", "Play a test note (SHIFT too: silence; CMD+RETURN on macOS)" },
  { "SPACE", "Play the song from the pattern cursor / stop" },
  { "RETURN", "Go to the table" },
  { "BACKQUOTE", "Select table" },
  { "SHIFT+CURSOR", "Mark table rows" },
  { "SHIFT+Q / W", "Transpose speedtable speed half step / octave up" },
  { "SHIFT+A / S", "Transpose speedtable speed half step / octave down" },
  { "SHIFT+L", "Convert pulse/filter limit to time" },
  { "SHIFT+N", "Edit name / negate value or note" },
  { "SHIFT+O", "Optimize table (remove unused rows)" },
  { "SHIFT+R", "Absolute / relative note" },
  { "SHIFT+S", "Smart instrument paste" },
  { "SHIFT+U", "Unlock / lock table scrolling" },
  { "SHIFT+X / C / V", "Cut / copy / paste instrument or table" },
  { "SHIFT+DEL", "Delete instrument and its tables" },
  { "SHIFT+RETURN", "Convert vibrato parameter" },
  { NULL, NULL }
};

static const HelpLine commandlines[] = {
  { "0XY", "Do nothing" },
  { "1XY", "Portamento up. XY = speedtable position of the speed" },
  { "2XY", "Portamento down. XY = speedtable position" },
  { "3XY", "Toneportamento to the note. XY = speedtable position, 00 = tie note" },
  { "4XY", "Vibrato. XY = speedtable position (left: speed, right: depth)" },
  { "5XY", "Set attack / decay to XY" },
  { "6XY", "Set sustain / release to XY" },
  { "7XY", "Set waveform to XY" },
  { "8XY", "Set wavetable position (00 stops it)" },
  { "9XY", "Set pulsetable position (00 stops it)" },
  { "AXY", "Set filtertable position (00 stops it)" },
  { "BXY", "Filter control: X = resonance, Y = channel bits. 00 = filter off" },
  { "CXY", "Set filter cutoff" },
  { "DXY", "Master volume Y (if X = 0), else written to the timing mark" },
  { "EXY", "Funktempo: XY = speedtable position of the two tempos" },
  { "FXY", "Set tempo: 03-7F all channels, 83-FF this channel. 00/01 = funktempo" },
  { NULL, NULL }
};

static const HelpLine tablelines[] = {
  { "Wave left", "00 = no change, 01-0F = wait, 10-DF = waveform, E0-EF = silent waveform, F0-FE = pattern command, FF = jump" },
  { "Wave right", "00-5F relative note up, 60-7F down, 80 = unchanged, 81-DF absolute note" },
  { "Pulse left", "01-7F = modulate for N frames (right: speed), 8X-FX = set width (X = high bits, right = low bits), FF = jump" },
  { "Filter left", "00 = set cutoff (right), 01-7F = modulate (right: speed), 80-F0 = filter type & resonance / channels (right), FF = jump" },
  { "Speedtable", "Vibrato: left speed, right depth. Portamento: 16-bit speed. Funktempo: two tempos. High bit in left = note-independent" },
  { "Jump", "FF with right side 00 stops the table, otherwise jumps to that row" },
  { NULL, NULL }
};

static const HelpSection sections[] = {
  { "Using the interface", interfacelines },
  { "SID Designer", designerlines },
  { "General keys", generallines },
  { "Pattern editor", patternlines },
  { "Arrangement (orderlist)", orderlines },
  { "Instrument & tables", instrumentlines },
  { "Pattern commands", commandlines },
  { "Tables", tablelines },
  { NULL, NULL }
};

static bool linematches(const HelpLine &l, const char *filter)
{
  ImGuiTextFilter f;
  if (!filter[0]) return true;
  strncpy(f.InputBuf, filter, sizeof f.InputBuf - 1);
  f.InputBuf[sizeof f.InputBuf - 1] = 0;
  f.Build();
  return f.PassFilter(l.key) || f.PassFilter(l.text);
}

void gui::drawhelp(bool *open)
{
  static char filter[64] = "";
  int s;

  ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 46.0f, ImGui::GetFontSize() * 36.0f), ImGuiCond_FirstUseEver);
  {
    ImGuiViewport *vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
  }
  if (!ImGui::Begin(WIN_HELP, open, ImGuiWindowFlags_NoDocking))
  {
    ImGui::End();
    return;
  }
  if (ImGui::IsWindowAppearing()) ImGui::SetWindowFocus();

  ImGui::SetNextItemWidth(-FLT_MIN);
  ImGui::InputTextWithHint("##filter", "Search keys and commands", filter, sizeof filter);
  ImGui::BeginChild("##helptext");

  for (s = 0; sections[s].title; s++)
  {
    const HelpLine *l;
    bool any = false;

    for (l = sections[s].lines; l->key; l++)
    {
      if (linematches(*l, filter)) { any = true; break; }
    }
    if (!any) continue;

    sectiontitle(sections[s].title);
    if (ImGui::BeginTable(sections[s].title, 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit))
    {
      ImGui::TableSetupColumn("key", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 9.5f);
      ImGui::TableSetupColumn("text", ImGuiTableColumnFlags_WidthStretch);
      for (l = sections[s].lines; l->key; l++)
      {
        if (!linematches(*l, filter)) continue;
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::PushFont(fontmono, 0.0f);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(theme.instr), "%s", l->key);
        ImGui::PopFont();
        ImGui::TableSetColumnIndex(1);
        ImGui::TextWrapped("%s", l->text);
      }
      ImGui::EndTable();
    }
  }
  ImGui::EndChild();
  ImGui::End();
}
