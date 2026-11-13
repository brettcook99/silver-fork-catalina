//
// GOATTRACKER retro interface: the classic main screen and its mouse
// controls, as in GoatTracker v2.82 before the modern interface. The screen
// is drawn on the 100x37 text screen, which the GUI layer shows in the
// original font and colors (gui/gui_retro.cpp).
//

#define GRETRO_C

#include "goattrk2.h"
#include "gui/gui.h"

static char timechar[] = {':', ' '};

// The columns: the patterns at the left; from column RX on the orderlist,
// the instrument, the tables and the song info; between them the effects
// panel
#define RX 59

// The effects panel: the 16 pattern commands and the 9 instrument
// parameters, one on each pattern row, centered on column FXCENTER. A left
// click picks one: it turns white (the others stay darker grey) and its
// value shows in green above the list. The mouse wheel over the panel sets
// the value. A command is written into the pattern of the cursor's channel:
// while the song plays on the row the playback reads next (the playing row,
// so that it is heard at once), with the song stopped on the cursor row. A
// parameter changes the current instrument (heard from its next note on).
// Another click on the effect, or jam mode, lets it go
#define FXFIRSTCOLUMN (MAX_CHN * 15 + 2)   // Clicks & wheel: to column RX - 2
#define FXCENTER 52
#define FXVALUEY (PATTROWY - 2)
#define FXCOMMANDS 16
#define FXNUM (FXCOMMANDS + 9)
#define FXPAUSE 600                        // ms: a pause ends the wheel's undo step

static const char *fxname[FXNUM] =
{
  "0XY", "1XY", "2XY", "3XY", "4XY", "5XY", "6XY", "7XY",
  "8XY", "9XY", "AXY", "BXY", "CXY", "DXY", "EXY", "FXY",
  "AD", "SR", "WP", "PP", "FP", "VP", "VD", "GATE", "WAVE"
};

static int fxsel = -1;                     // The effect picked, -1 = none
static unsigned char fxvalue[FXCOMMANDS];  // Each command's value set last
static int fxlast = -1;                    // What the wheel set last, when,
static unsigned fxtime = 0;                // and the undo list after it
static int fxundotop = -1;
static int fxundocount = -1;

extern int psnum;                          // The subtune playing (gplay.c)

static int clampvalue(int value, int min, int max)
{
  if (value < min) return min;
  if (value > max) return max;
  return value;
}

// The pattern row a command goes to (see above). Returns 0 if there is none
static int fxrow(int *patt, int *row)
{
  int c = editorInfo.epchn;
  CHN *cptr = &chn[c];

  if (!isplaying())
  {
    *patt = editorUndoInfo.editorInfo[c].epnum;
    *row = editorInfo.eppos;
  }
  else if (playstarting()) return 0;
  else if (cptr->pattptr != 0x7fffffff)
  {
    *patt = cptr->pattnum;
    *row = cptr->pattptr / 4;
  }
  else
  {
    // After a pattern's last row: the first row of the next pattern, found
    // as the sequencer will find it
    unsigned char *order = songorder[psnum][c];
    int sp = cptr->songptr;

    *patt = cptr->pattnum;
    *row = 0;
    if (cptr->advance)
    {
      if (order[sp] == LOOPSONG) sp = order[sp + 1];
      if (sp >= songlen[psnum][c]) return 0;
      if ((order[sp] >= TRANSDOWN) && (order[sp] < LOOPSONG)) sp++;
      if ((order[sp] >= REPEAT) && (order[sp] < TRANSDOWN)) sp++;
      if (order[sp] >= MAX_PATT) return 0;
      *patt = order[sp];
    }
  }
  return (*row >= 0) && (*row < pattlen[*patt]);
}

// The value of the effect picked: an instrument parameter's own; a
// command's from the cursor row if the song is stopped and the row has that
// command, otherwise the value set last
static int fxcurrent(void)
{
  int patt, row;

  if (fxsel >= FXCOMMANDS) return (&instr[editorInfo.einum].ad)[fxsel - FXCOMMANDS];
  if ((!isplaying()) && (fxrow(&patt, &row)) && (pattern[patt][row * 4 + 2] == fxsel))
    return pattern[patt][row * 4 + 3];
  return fxvalue[fxsel];
}

static void fxprint(void)
{
  int k;

  if (!recordmode) fxsel = -1;

  if (fxsel >= 0) sprintf(textbuffer, "%02X", fxcurrent());
  else sprintf(textbuffer, "  ");
  printtext(FXCENTER - 1, FXVALUEY, CEDIT, textbuffer);

  for (k = 0; k < FXNUM; k++)
    printtext(FXCENTER - (int)strlen(fxname[k]) / 2, PATTROWY + k, (k == fxsel) ? CTITLE : CNORMAL, fxname[k]);
}

// A left click on an effect picks it (in edit mode), on the one picked lets
// it go
static void fxclick(void)
{
  int k = mousey - PATTROWY;

  if ((mousex < FXFIRSTCOLUMN) || (mousex > RX - 2) || (k < 0) || (k >= FXNUM)) return;
  if (k == fxsel)
  {
    fxsel = -1;
    return;
  }
  fxsel = k;
  if (!recordmode)
  {
    recordmode = 1;
    gui_message("Edit mode: the effect's value is entered");
  }
}

// The mouse wheel over the effects panel sets the value of the effect
// picked (up = bigger). Turned without a pause it is one undo step. Returns
// nonzero if the wheel was over the panel
static int fxwheel(int d)
{
  unsigned now = SDL_GetTicks();
  GTUNDO_OBJECT *ed;
  int top, v;

  if ((mousex < FXFIRSTCOLUMN) || (mousex > RX - 2) || (mousey < 2) || (mousey > PATTROWY + VISIBLEPATTROWS)) return 0;
  if (fxsel < 0) return 1;

  v = clampvalue(fxcurrent() + d, 0, 0xff);
  top = currentUndoPosition;
  ed = undoCreateEditorInfo();
  if (fxsel < FXCOMMANDS)
  {
    int patt, row;

    fxvalue[fxsel] = (unsigned char)v;
    if (fxrow(&patt, &row))
    {
      pattern[patt][row * 4 + 2] = (unsigned char)fxsel;
      pattern[patt][row * 4 + 3] = (unsigned char)v;
      undoAreaSetCheckForChange(UNDO_AREA_PATTERN, patt, UNDO_AREA_DIRTY_CHECK);
    }
  }
  // Instrument 0 is "no instrument" and can't be edited
  else if (editorInfo.einum)
  {
    INSTR *iptr = &instr[editorInfo.einum];

    (&iptr->ad)[fxsel - FXCOMMANDS] = (unsigned char)v;
    // As when typing: the gate timer can't be 0
    if (!(iptr->gatetimer & 0x3f)) iptr->gatetimer |= 1;
    undoAreaSetCheckForChange(UNDO_AREA_INSTRUMENTS, editorInfo.einum, UNDO_AREA_DIRTY_CHECK);
  }

  if (undoValidateUndoAreas(ed))
  {
    if ((fxlast == fxsel) && (fxundotop == top) && (fxundocount == undoCounter - 1) && (now - fxtime < FXPAUSE))
      undoJoinStep(top);
    fxundotop = currentUndoPosition;
    fxundocount = undoCounter;
    fxlast = fxsel;
    fxtime = now;
  }
  else undoFreeUndoObject(ed);
  return 1;
}

// The bottom rows: OCTAVE, PLAYING/STOPPED, MIDI INPUT and STEPS, each two
// columns after the one before, then the message area
#define INPUTCOLUMN 19
#define INPUTWIDTH 10
#define STEPSCOLUMN (INPUTCOLUMN + INPUTWIDTH + 2)
#define STEPSWIDTH 5
#define MSGCOLUMN (STEPSCOLUMN + STEPSWIDTH + 2)
#define MSGWIDTH (77 - MSGCOLUMN)

// "POS xx" in the orderlist title
#define POSCOLUMN (RX + 27)
#define POSWIDTH 6

// The title bar (top row): the program and song name, then from column TBX
// on the settings that a click switches: fine vibrato, pulse and realtime
// optimization, PAL/NTSC, SID model, hard restart ADSR, speed multiplier,
// interpolation and reSID-fp. At the right end "SID Designer" (on the SID
// designer: "Tracker") switches between the two
#define TBX 48
#define TB_FV (TBX + 0)
#define TB_PO (TBX + 3)
#define TB_RO (TBX + 6)
#define TB_CLOCK (TBX + 9)
#define TB_MODEL (TBX + 14)
#define TB_HR (TBX + 19)        // "HR:" then the 4 digits
#define TB_MULT (TBX + 27)
#define TB_INT (TBX + 32)
#define TB_FP (TBX + 36)

static const char *titlebarlabel(void)
{
  return gui_designershown() ? "Tracker" : "SID Designer";
}

// Starts playing like F1-F3 (SHIFT: the other follow way)
static void retroplay(int mode)
{
  initsong(editorInfo.esnum, mode);
  followplay = followmode ^ shiftpressed;
  followsuspended = 0;
}

// Prints the title bar on the top row. The SID designer shows it too (in
// both interfaces)
void retro_printtitlebar(void)
{
  int cc = cursorcolortable[cursorflash];
  int fg_active = 7;
  int fg_inactive = 8;
  const char *label = titlebarlabel();

  printblankc(0, 0, 15+16, MAX_COLUMNS);

  if (!strlen(loadedsongfilename))
    sprintf(textbuffer, "%s", programname);
  else
    sprintf(textbuffer, "%s - %s", programname, gt_basename(loadedsongfilename));
  textbuffer[TBX - 1] = 0;
  printtext(0, 0, 15+16, textbuffer);

  printtext(TB_FV, 0, fg_inactive+16, "FV PO RO");

  if (editorInfo.finevibrato)
    printtext(TB_FV, 0, fg_active+16, "FV");

  if (editorInfo.optimizepulse)
    printtext(TB_PO, 0, fg_active+16, "PO");

  if (editorInfo.optimizerealtime)
    printtext(TB_RO, 0, fg_active+16, "RO");

  if (editorInfo.ntsc)
    printtext(TB_CLOCK, 0, fg_active+16, "NTSC");
  else
    printtext(TB_CLOCK, 0, fg_active+16, " PAL");

  if (!editorInfo.sidmodel)
    printtext(TB_MODEL, 0, fg_active+16, "6581");
  else
    printtext(TB_MODEL, 0, fg_active+16, "8580");

  sprintf(textbuffer, "HR:%04X", editorInfo.adparam);
  printtext(TB_HR, 0, fg_active+16, textbuffer);
  if (eamode) printbg(TB_HR + 3 + editorInfo.eacolumn, 0, cc, 1);

  if (editorInfo.multiplier)
  {
    sprintf(textbuffer, "%2dx", editorInfo.multiplier);
    printtext(TB_MULT, 0, fg_active+16, textbuffer);
  }
  else printtext(TB_MULT, 0, fg_active+16, "25Hz");

  printtext(TB_INT, 0, fg_inactive+16, "INT FP");

  if (interpolate & 1) {
    printtext(TB_INT, 0, fg_active+16, "INT");
  }

  if (interpolate & 2) {
    printtext(TB_FP, 0, fg_active+16, "FP");
  }

  printtext(MAX_COLUMNS - (int)strlen(label), 0, 15+16, label);
}

// A left click on the title bar at column x: switches the setting there, or
// between the tracker and the SID designer
void retro_titlebarclick(int x)
{
  if ((x >= TB_FV) && (x <= TB_FV + 1))
  {
    undoCreateEditorInfoBackup();
    editorInfo.finevibrato ^= 1;
    undoAddEditorSettingsToList();
  }
  if ((x >= TB_PO) && (x <= TB_PO + 1))
  {
    undoCreateEditorInfoBackup();
    editorInfo.optimizepulse ^= 1;
    undoAddEditorSettingsToList();
  }
  if ((x >= TB_RO) && (x <= TB_RO + 1))
  {
    undoCreateEditorInfoBackup();
    editorInfo.optimizerealtime ^= 1;
    undoAddEditorSettingsToList();
  }
  if ((x >= TB_CLOCK) && (x <= TB_CLOCK + 3))
  {
    undoCreateEditorInfoBackup();
    editorInfo.ntsc ^= 1;
    undoAddEditorSettingsToList();
    do_sound_init();
  }
  if ((x >= TB_MODEL) && (x <= TB_MODEL + 3))
  {
    undoCreateEditorInfoBackup();
    editorInfo.sidmodel ^= 1;
    undoAddEditorSettingsToList();
    do_sound_init();
  }
  if ((x >= TB_HR + 3) && (x <= TB_HR + 6)) gui_edithradsr();
  if ((x >= TB_MULT) && (x <= TB_MULT + 1))
  {
    undoCreateEditorInfoBackup();
    prevmultiplier();
    undoAddEditorSettingsToList();
  }
  if ((x >= TB_MULT + 2) && (x <= TB_MULT + 3))
  {
    undoCreateEditorInfoBackup();
    nextmultiplier();
    undoAddEditorSettingsToList();
  }
  if ((x >= TB_INT) && (x <= TB_INT + 2))
  {
    interpolate ^= 1;
    interpolate &= 1 + (interpolate << 1);  // turn off FP if INT is 0
    stopsong();
    do_sound_init();
  }
  if ((x >= TB_FP) && (x <= TB_FP + 1))
  {
    interpolate ^= 2;
    interpolate |= interpolate >> 1;  // turn on INT if FP is 1
    stopsong();
    do_sound_init();
  }
  if (x >= MAX_COLUMNS - (int)strlen(titlebarlabel())) cmd_post(GTCMD_DESIGNER);
}

void retro_printstatus(void)
{
  int c, d, color, color2;
  int cc = cursorcolortable[cursorflash];
  menu = 0;

  if ((mouseb > MOUSEB_LEFT) && (mousey <= 1) && (!eamode)) menu = 1;

  if (!menu)
    retro_printtitlebar();
  else
  {
    // VIEW = a menu with the SID designer and the view settings
    printblankc(0, 0, 15+16, MAX_COLUMNS);
    printtext(0, 0, 15+16, " PLAY | PLAYPOS | PLAYPATT | STOP | LOAD | SAVE | PACK/RL | HELP | CLEAR | QUIT | A-A+ | FULL | VIEW");
  }

  // Following the playback is done by followplayback() in the main loop

  for (c = 0; c < MAX_CHN; c++)
  {
    sprintf(textbuffer, "CHN %d PATT.%02X", c+1, editorUndoInfo.editorInfo[c].epnum);
    printtext(2+c*15, 2, CTITLE, textbuffer);

    for (d = 0; d < VISIBLEPATTROWS; d++)
    {
      int p = editorInfo.epview+d;
      color = CNORMAL;
      if ((editorUndoInfo.editorInfo[c].epnum == chn[c].pattnum) && (isplaying()))
      {
        int chnrow = chn[c].pattptr / 4;
        if (chnrow > pattlen[chn[c].pattnum]) chnrow = pattlen[chn[c].pattnum];
        if (chnrow == p) color = CPLAYING;
      }

      if (chn[c].mute) color = CMUTE;
      if (p == editorInfo.eppos) color = CEDIT;
      if ((p < 0) || (p > pattlen[editorUndoInfo.editorInfo[c].epnum]))
      {
        sprintf(textbuffer, "             ");
      }
      else
      {
        if (!(patterndispmode & 1))
        {
          if (p < 100)
            sprintf(textbuffer, " %02d", p);
          else
            sprintf(textbuffer, "%03d", p);
        }
        else
          sprintf(textbuffer, " %02X", p);

        if (pattern[editorUndoInfo.editorInfo[c].epnum][p*4] == ENDPATT)
        {
          sprintf(&textbuffer[3], " PATT. END");
          if (color == CNORMAL) color = CCOMMAND;
        }
        else
        {
          sprintf(&textbuffer[3], " %s %02X%01X%02X",
            notename[pattern[editorUndoInfo.editorInfo[c].epnum][p*4]-FIRSTNOTE],
            pattern[editorUndoInfo.editorInfo[c].epnum][p*4+1],
            pattern[editorUndoInfo.editorInfo[c].epnum][p*4+2],
            pattern[editorUndoInfo.editorInfo[c].epnum][p*4+3]);
          if (patterndispmode & 2)
          {
            if (!pattern[editorUndoInfo.editorInfo[c].epnum][p*4+1])
              memset(&textbuffer[8], '.', 2);
            if (!pattern[editorUndoInfo.editorInfo[c].epnum][p*4+2])
              memset(&textbuffer[10], '.', 3);
          }
        }
      }
      textbuffer[3] = 0;
      if (p%stepsize)
      {
        printtext(2+c*15, PATTROWY+d, CNORMAL, textbuffer);
      }
      else
      {
        printtext(2+c*15, PATTROWY+d, CCOMMAND, textbuffer);
      }
      if (color == CNORMAL)
        color2 = CCOMMAND;
      else
        color2 = color;
      printtext(6+c*15, PATTROWY+d, color2, &textbuffer[4]);
      printtext(10+c*15, PATTROWY+d, color, &textbuffer[8]);
      printtext(12+c*15, PATTROWY+d, color2, &textbuffer[10]);
      printtext(13+c*15, PATTROWY+d, color, &textbuffer[11]);
      if (c == editorInfo.epmarkchn)
      {
        if (editorInfo.epmarkstart <= editorInfo.epmarkend)
        {
          if ((p >= editorInfo.epmarkstart) && (p <= editorInfo.epmarkend))
            printbg(2+c*15+4, PATTROWY+d, 1, 9);
        }
        else
        {
          if ((p <= editorInfo.epmarkstart) && (p >= editorInfo.epmarkend))
            printbg(2+c*15+4, PATTROWY+d, 1, 9);
        }
      }
      if ((color == CEDIT) && (editorInfo.editmode == EDIT_PATTERN) && (editorInfo.epchn == c))
      {
        switch(editorInfo.epcolumn)
        {
          case 0:
          if (!eamode) printbg(2+c*15+4, PATTROWY+d, cc, 3);
          break;

          default:
          if (!eamode) printbg(2+c*15+7+editorInfo.epcolumn, PATTROWY+d, cc, 1);
          break;
        }
      }
    }
  }

  fxprint();

  sprintf(textbuffer, "CHN ORDERLIST (SUBTUNE %02X, POS %02X)", editorInfo.esnum, editorInfo.eseditpos);
  printtext(RX, 2, CTITLE, textbuffer);
  for (c = 0; c < MAX_CHN; c++)
  {
    sprintf(textbuffer, " %d ", c+1);
    printtext(RX, 3+c, 15, textbuffer);
    for (d = 0; d < VISIBLEORDERLIST; d++)
    {
      int p = editorInfo.esview+d;
      color = CNORMAL;
      if (isplaying())
      {
        int chnpos = chn[c].songptr;
        chnpos--;
        if (chnpos < 0) chnpos = 0;
        if ((p == chnpos) && (chn[c].advance)) color = CPLAYING;
      }
      if (p == editorUndoInfo.editorInfo[c].espos) color = CEDIT;
      if ((editorUndoInfo.editorInfo[c].esend) && (p == editorUndoInfo.editorInfo[c].esend)) color = CEDIT;

      if ((p < 0) || (p > (songlen[editorInfo.esnum][c]+1)) || (p > MAX_SONGLEN+1))
      {
        sprintf(textbuffer, "   ");
      }
      else
      {
        if (songorder[editorInfo.esnum][c][p] < LOOPSONG)
        {
          if ((songorder[editorInfo.esnum][c][p] < REPEAT) || (p >= songlen[editorInfo.esnum][c]))
          {
            sprintf(textbuffer, "%02X ", songorder[editorInfo.esnum][c][p]);
            if ((p >= songlen[editorInfo.esnum][c]) && (color == CNORMAL)) color = CCOMMAND;
          }
          else
          {
            if (songorder[editorInfo.esnum][c][p] >= TRANSUP)
            {
              sprintf(textbuffer, "+%01X ", songorder[editorInfo.esnum][c][p]&0xf);
              if (color == CNORMAL) color = CCOMMAND;
            }
            else
            {
              if (songorder[editorInfo.esnum][c][p] >= TRANSDOWN)
              {
                sprintf(textbuffer, "-%01X ", 16-(songorder[editorInfo.esnum][c][p] & 0x0f));
                if (color == CNORMAL) color = CCOMMAND;
              }
              else
              {
                sprintf(textbuffer, "R%01X ", (songorder[editorInfo.esnum][c][p]+1) & 0x0f);
                if (color == CNORMAL) color = CCOMMAND;
              }
            }
          }
        }
        if (songorder[editorInfo.esnum][c][p] == LOOPSONG)
        {
          sprintf(textbuffer, "RST");
          if (color == CNORMAL) color = CCOMMAND;
        }
      }
      printtext(RX+4+d*3, 3+c, color, textbuffer);
      if (c == editorInfo.esmarkchn)
      {
        if (editorInfo.esmarkstart <= editorInfo.esmarkend)
        {
          if ((p >= editorInfo.esmarkstart) && (p <= editorInfo.esmarkend))
          {
            if (p != editorInfo.esmarkend)
              printbg(RX+4+d*3, 3+c, 1, 3);
            else
              printbg(RX+4+d*3, 3+c, 1, 2);
          }
        }
        else
        {
          if ((p <= editorInfo.esmarkstart) && (p >= editorInfo.esmarkend))
          {
            if (p != editorInfo.esmarkstart)
              printbg(RX+4+d*3, 3+c, 1, 3);
            else
              printbg(RX+4+d*3, 3+c, 1, 2);
          }
        }
      }
      if ((p == editorInfo.eseditpos) && (editorInfo.editmode == EDIT_ORDERLIST) && (editorInfo.eschn == c))
      {
        if (!eamode) printbg(RX+4+d*3+editorInfo.escolumn, 3+c, cc, 1);
      }
    }
  }

  sprintf(textbuffer, "INSTRUMENT NUM. %02X  %-16s", editorInfo.einum, instr[editorInfo.einum].name);
  printtext(RX, 7, CTITLE, textbuffer);

  sprintf(textbuffer, "Attack/Decay    %02X", instr[editorInfo.einum].ad);
  if (editorInfo.eipos == 0) color = CEDIT; else color = CNORMAL;
  printtext(RX, 8, color, textbuffer);

  sprintf(textbuffer, "Sustain/Release %02X", instr[editorInfo.einum].sr);
  if (editorInfo.eipos == 1) color = CEDIT; else color = CNORMAL;
  printtext(RX, 9, color, textbuffer);

  sprintf(textbuffer, "Wavetable Pos   %02X", instr[editorInfo.einum].ptr[WTBL]);
  if (editorInfo.eipos == 2) color = CEDIT; else color = CNORMAL;
  printtext(RX, 10, color, textbuffer);

  sprintf(textbuffer, "Pulsetable Pos  %02X", instr[editorInfo.einum].ptr[PTBL]);
  if (editorInfo.eipos == 3) color = CEDIT; else color = CNORMAL;
  printtext(RX, 11, color, textbuffer);

  sprintf(textbuffer, "Filtertable Pos %02X", instr[editorInfo.einum].ptr[FTBL]);
  if (editorInfo.eipos == 4) color = CEDIT; else color = CNORMAL;
  printtext(RX, 12, color, textbuffer);

  sprintf(textbuffer, "Vibrato Param   %02X", instr[editorInfo.einum].ptr[STBL]);
  if (editorInfo.eipos == 5) color = CEDIT; else color = CNORMAL;
  printtext(RX+20, 8, color, textbuffer);

  sprintf(textbuffer, "Vibrato Delay   %02X", instr[editorInfo.einum].vibdelay);
  if (editorInfo.eipos == 6) color = CEDIT; else color = CNORMAL;
  printtext(RX+20, 9, color, textbuffer);

  sprintf(textbuffer, "HR/Gate Timer   %02X", instr[editorInfo.einum].gatetimer);
  if (editorInfo.eipos == 7) color = CEDIT; else color = CNORMAL;
  printtext(RX+20, 10, color, textbuffer);

  sprintf(textbuffer, "1stFrame Wave   %02X", instr[editorInfo.einum].firstwave);
  if (editorInfo.eipos == 8) color = CEDIT; else color = CNORMAL;
  printtext(RX+20, 11, color, textbuffer);

  if (editorInfo.editmode == EDIT_INSTRUMENT)
  {
    if (editorInfo.eipos < 9)
    {
      if (!eamode) printbg(RX+16+editorInfo.eicolumn+20*(editorInfo.eipos/5), 8+(editorInfo.eipos%5), cc, 1);
    }
    else
    {
      if (!eamode) printbg(RX+20+strlen(instr[editorInfo.einum].name), 7, cc, 1);
    }
  }

  sprintf(textbuffer, "WAVE TBL  PULSETBL  FILT.TBL  SPEEDTBL");
  printtext(RX, 14, CTITLE, textbuffer);

  for (c = 0; c < MAX_TABLES; c++)
  {
    for (d = 0; d < VISIBLETABLEROWS; d++)
    {
      int p = editorInfo.etview[c]+d;

      color = CNORMAL;
      switch (c)
      {
        case WTBL:
        if (ltable[c][p] >= WAVECMD) color = CCOMMAND;
        break;

        case PTBL:
        if (ltable[c][p] >= 0x80) color = CCOMMAND;
        break;

        case FTBL:
        if ((ltable[c][p] >= 0x80) || ((!ltable[c][p]) && (rtable[c][p]))) color = CCOMMAND;
        break;
      }
      if ((p == editorInfo.etpos) && (editorInfo.etnum == c)) color = CEDIT;
      sprintf(textbuffer, "%02X:%02X %02X", p+1, ltable[c][p], rtable[c][p]);
      printtext(RX+10*c, 15+d, color, textbuffer);

      if (editorInfo.etmarknum == c)
      {
        if (editorInfo.etmarkstart <= editorInfo.etmarkend)
        {
          if ((p >= editorInfo.etmarkstart) && (p <= editorInfo.etmarkend))
            printbg(RX+10*c+3, 15+d, 1, 5);
        }
        else
        {
          if ((p <= editorInfo.etmarkstart) && (p >= editorInfo.etmarkend))
            printbg(RX+10*c+3, 15+d, 1, 5);
        }
      }
    }
  }

  if (editorInfo.editmode == EDIT_TABLES)
  {
    if (!eamode) printbg(RX+3+editorInfo.etnum*10+(editorInfo.etcolumn & 1)+(editorInfo.etcolumn/2)*3, 15+editorInfo.etpos-editorInfo.etview[editorInfo.etnum], cc, 1);
  }

  printtext(RX, 31, CTITLE, "NAME   ");
  sprintf(textbuffer, "%-32s", songname);
  printtext(RX+7, 31, CEDIT, textbuffer);

  printtext(RX, 32, CTITLE, "AUTHOR ");
  sprintf(textbuffer, "%-32s", authorname);
  printtext(RX+7, 32, CEDIT, textbuffer);

  printtext(RX, 33, CTITLE, "COPYR. ");
  sprintf(textbuffer, "%-32s", copyrightname);
  printtext(RX+7, 33, CEDIT, textbuffer);

  if ((editorInfo.editmode == EDIT_NAMES) && (!eamode))
  {
    switch(editorInfo.enpos)
    {
      case 0:
      printbg(RX+7+strlen(songname), 31, cc, 1);
      break;
      case 1:
      printbg(RX+7+strlen(authorname), 32, cc, 1);
      break;
      case 2:
      printbg(RX+7+strlen(copyrightname), 33, cc, 1);
      break;
    }
  }
  sprintf(textbuffer, "OCTAVE %d", editorInfo.epoctave);
  printtext(0, 35, CTITLE, textbuffer);

  switch(autoadvance)
  {
    case 0:
    color = 10;
    break;

    case 1:
    color = 14;
    break;

    case 2:
    color = 12;
    break;
  }

  if (recordmode) printtext(0, 36, color, "EDITMODE");
  else printtext(0, 36, color, "JAM MODE");

  if (isplaying()) printtext(10, 35, CTITLE, "PLAYING");
  else printtext(10, 35, CTITLE, "STOPPED");

  // MIDI input: how many patterns (channels) the notes played on the MIDI
  // keyboard go to. Click or wheel to change
  printtext(INPUTCOLUMN, 35, CTITLE, "MIDI INPUT");
  if (midichord > 1)
    sprintf(textbuffer, "%d PATTERNS", midichord);
  else
    sprintf(textbuffer, "%d PATTERN ", midichord);
  printtext(INPUTCOLUMN, 36, CEDIT, textbuffer);

  // STEPS: rows skipped after a note is entered. Click or wheel to change
  printtext(STEPSCOLUMN, 35, CTITLE, "STEPS");
  sprintf(textbuffer, "%-2d", editstep);
  printtext(STEPSCOLUMN, 36, CEDIT, textbuffer);
  if (editorInfo.multiplier)
  {
    if (!editorInfo.ntsc)
      sprintf(textbuffer, " %02d%c%02d ", timemin, timechar[timeframe/(25* editorInfo.multiplier) & 1], timesec);
    else
      sprintf(textbuffer, " %02d%c%02d ", timemin, timechar[timeframe/(30* editorInfo.multiplier) & 1], timesec);
  }
  else
  {
    if (!editorInfo.ntsc)
      sprintf(textbuffer, " %02d%c%02d ", timemin, timechar[(timeframe/13) & 1], timesec);
    else
      sprintf(textbuffer, " %02d%c%02d ", timemin, timechar[(timeframe/15) & 1], timesec);
  }

  printtext(10, 36, CEDIT, textbuffer);

  // Messages (file opened, saved, imported...) where the credits used to be.
  // A long one goes on two lines
  printblankc(MSGCOLUMN, 35, CNORMAL, MSGWIDTH);
  printblankc(MSGCOLUMN, 36, CNORMAL, MSGWIDTH);
  {
    const char *msg = gui_currentmessage();
    if (msg)
    {
      char line[MSGWIDTH + 1];
      int len = (int)strlen(msg);

      if (len <= MSGWIDTH)
      {
        snprintf(line, sizeof line, "%s", msg);
        printtext(MSGCOLUMN, 36, CTITLE, line);
      }
      else
      {
        int cut = MSGWIDTH;
        while ((cut > MSGWIDTH / 2) && (msg[cut] != ' ')) cut--;
        if (msg[cut] != ' ') cut = MSGWIDTH;
        snprintf(line, sizeof line, "%.*s", cut, msg);
        printtext(MSGCOLUMN, 35, CTITLE, line);
        while (msg[cut] == ' ') cut++;
        snprintf(line, sizeof line, "%s", msg + cut);
        printtext(MSGCOLUMN, 36, CTITLE, line);
      }
    }
  }

  printtext(80, 35, CTITLE, " CHN1   CHN2   CHN3 ");
  for (c = 0; c < MAX_CHN; c++)
  {
    int chnpos = chn[c].songptr;
    int chnrow = chn[c].pattptr/4;
    chnpos--;
    if (chnpos < 0) chnpos = 0;
    if (chnrow > pattlen[chn[c].pattnum]) chnrow = pattlen[chn[c].pattnum];
    if (chnrow >= 100) chnrow -= 100;

    sprintf(textbuffer, "%03d/%02d",
      chnpos,chnrow);
    printtext(80+7*c, 36, CEDIT, textbuffer);
  }

  if (editorInfo.etlock) printtext(78, 36, CTITLE, " ");
    else printtext(78, 36, CTITLE, "U");

  // The level meters (gui/gui_retro.cpp) may be drawn over the patterns
  retromainshown = 1;
}

// Moves the orderlist cursor steps song positions on (wheel over "POS xx").
// The pattern editor shows the patterns at the new position, as SHIFT+RETURN
// does in the orderlist
static void retrosongpos(int steps)
{
  int len = songlen[editorInfo.esnum][editorInfo.eschn];
  int pos = editorInfo.eseditpos;
  int c, d;

  if (len < 1) return;
  if (pos >= len) pos = len - 1;
  pos = clampvalue(pos + steps, 0, len - 1);
  editorInfo.eseditpos = pos;
  editorInfo.escolumn = 0;
  if (editorInfo.eseditpos - editorInfo.esview < 0)
    editorInfo.esview = editorInfo.eseditpos;
  if (editorInfo.eseditpos - editorInfo.esview >= VISIBLEORDERLIST)
    editorInfo.esview = editorInfo.eseditpos - VISIBLEORDERLIST + 1;

  for (c = 0; c < MAX_CHN; c++)
  {
    for (d = pos; d < songlen[editorInfo.esnum][c]; d++)
    {
      if (songorder[editorInfo.esnum][c][d] < MAX_PATT)
      {
        editorUndoInfo.editorInfo[c].epnum = songorder[editorInfo.esnum][c][d];
        break;
      }
    }
  }
  editorInfo.epmarkchn = -1;
  if (editorInfo.eppos > pattlen[editorUndoInfo.editorInfo[editorInfo.epchn].epnum])
    editorInfo.eppos = pattlen[editorUndoInfo.editorInfo[editorInfo.epchn].epnum];
}

// Scrolls a table (wheel down = on). The edit cursor stays in view. With the
// table views locked together they all scroll
static void retrotablescroll(int num, int steps)
{
  int view = clampvalue(editorInfo.etview[num] + steps * WHEELSCROLLROWS, 0, MAX_TABLELEN - VISIBLETABLEROWS);
  int c;

  if (editorInfo.etlock)
  {
    for (c = 0; c < MAX_TABLES; c++) editorInfo.etview[c] = view;
  }
  else editorInfo.etview[num] = view;

  if ((editorInfo.etlock) || (num == editorInfo.etnum))
  {
    view = editorInfo.etview[editorInfo.etnum];
    editorInfo.etpos = clampvalue(editorInfo.etpos, view, view + VISIBLETABLEROWS - 1);
  }
}

// Mouse wheel over a table value changes it (up = bigger), over its left
// digit in steps of $10. Over the row numbers (and between the tables) the
// table scrolls. Returns nonzero if the wheel was over a table
static int retrotablewheel(int d)
{
  int num, x, pos, column, v;
  unsigned char *ptr;

  if ((mousey < 15) || (mousey >= 15 + VISIBLETABLEROWS) || (mousex < RX) || (mousex >= RX + MAX_TABLES * 10))
    return 0;

  // Each table is "nn:LL RR" and two spaces
  num = (mousex - RX) / 10;
  x = (mousex - RX) % 10;
  if ((x < 3) || (x > 7))
  {
    retrotablescroll(num, mousewheel);
    return 1;
  }
  if (x == 5) return 1;

  pos = editorInfo.etview[num] + mousey - 15;
  if ((pos < 0) || (pos >= MAX_TABLELEN)) return 1;
  column = (x < 5) ? x - 3 : x - 4;
  ptr = (column < 2) ? &ltable[num][pos] : &rtable[num][pos];
  v = clampvalue(*ptr + d * ((column & 1) ? 1 : 0x10), 0, 0xff);
  *ptr = (unsigned char)v;
  undoAreaSetCheckForChange(UNDO_AREA_TABLES + num, (column < 2) ? 0 : 1, UNDO_AREA_DIRTY_CHECK);

  // The value changed gets the table cursor (its row is highlighted)
  editorInfo.etnum = num;
  editorInfo.etpos = pos;
  editorInfo.etcolumn = column;
  return 1;
}

// Mouse wheel over a value changes it (up = bigger): the values that the
// mouse buttons change (pattern, subtune & instrument numbers, octave, MIDI
// input, steps), the instrument parameters, the table values, the song
// position ("POS xx"; while playing, the playback goes there too) and the
// pattern numbers in the orderlist, and the effects panel's value. Returns
// nonzero if the wheel was over one of them
static int retrowheel(void)
{
  int d = -mousewheel;
  int c;

  // The effects panel
  if (fxwheel(d)) return 1;

  // Pattern number of a channel ("PATT.xx")
  for (c = 0; c < MAX_CHN; c++)
  {
    if ((mousey == 2) && (mousex >= 8 + c * 15) && (mousex <= 14 + c * 15))
    {
      int chn = editorInfo.epchn;
      editorInfo.epchn = c;
      for (; d > 0; d--) nextpattern();
      for (; d < 0; d++) prevpattern();
      editorInfo.epchn = chn;
      if (editorInfo.eppos > pattlen[editorUndoInfo.editorInfo[editorInfo.epchn].epnum])
        editorInfo.eppos = pattlen[editorUndoInfo.editorInfo[editorInfo.epchn].epnum];
      return 1;
    }
  }

  // Subtune number
  if ((mousey == 2) && (mousex >= RX + 23) && (mousex <= RX + 24))
  {
    for (; d > 0; d--) nextsong();
    for (; d < 0; d++) prevsong();
    return 1;
  }

  // "POS xx": goes through the song positions. While the song plays, the
  // playback jumps to the start of the position (and follows as before)
  if ((mousey == 2) && (mousex >= POSCOLUMN) && (mousex < POSCOLUMN + POSWIDTH))
  {
    retrosongpos(d);
    if (isplaying())
    {
      playfromrow(0, followplay);
      followsuspended = 0;
    }
    return 1;
  }

  // Table values
  if (retrotablewheel(d)) return 1;

  // Pattern numbers in the orderlist. The channel's pattern editor shows the
  // pattern chosen (as RETURN on it does)
  if ((mousey >= 3) && (mousey < 3 + MAX_CHN) && (mousex >= RX + 4) && (mousex < RX + 4 + VISIBLEORDERLIST * 3))
  {
    int chn = mousey - 3;
    int pos = editorInfo.esview + (mousex - RX - 4) / 3;
    unsigned char *entry;
    int patt;

    if ((pos < 0) || (pos >= songlen[editorInfo.esnum][chn])) return 1;
    entry = &songorder[editorInfo.esnum][chn][pos];
    if (*entry >= MAX_PATT) return 1;

    patt = clampvalue(*entry + d, 0, MAX_PATT - 1);
    *entry = (unsigned char)patt;
    undoAreaSetCheckForChange(UNDO_AREA_ORDERLIST, chn + editorInfo.esnum * MAX_CHN, UNDO_AREA_DIRTY_CHECK);

    editorUndoInfo.editorInfo[chn].epnum = patt;
    if ((editorInfo.epchn == chn) && (editorInfo.eppos > pattlen[patt])) editorInfo.eppos = pattlen[patt];
    if (editorInfo.epmarkchn == chn) editorInfo.epmarkchn = -1;
    return 1;
  }

  // Instrument number
  if ((mousey == 7) && (mousex >= RX) && (mousex <= RX + 17))
  {
    for (; d > 0; d--) nextinstr();
    for (; d < 0; d++) previnstr();
    return 1;
  }

  // Instrument parameters. Over the left digit in steps of $10. In the
  // attack/decay and sustain/release the digits are values of their own
  {
    int param = -1;
    int high = 0;

    if ((mousey >= 8) && (mousey <= 12) && (mousex >= RX) && (mousex <= RX + 17))
    {
      param = mousey - 8;
      high = (mousex == RX + 16);
    }
    if ((mousey >= 8) && (mousey <= 11) && (mousex >= RX + 20) && (mousex <= RX + 37))
    {
      param = mousey - 8 + 5;
      high = (mousex == RX + 36);
    }
    if (param >= 0)
    {
      // Instrument 0 is "no instrument" and can't be edited
      if (editorInfo.einum)
      {
        INSTR *iptr = &instr[editorInfo.einum];
        unsigned char *ptr = &iptr->ad + param;
        int v = *ptr;

        if (param < 2)
        {
          int hi = v >> 4;
          int lo = v & 0x0f;
          if (high) hi = clampvalue(hi + d, 0, 15);
          else lo = clampvalue(lo + d, 0, 15);
          v = (hi << 4) | lo;
        }
        else v = clampvalue(v + d * (high ? 0x10 : 1), 0, 0xff);
        *ptr = (unsigned char)v;
        // As when typing: the gate timer can't be 0
        if (!(iptr->gatetimer & 0x3f)) iptr->gatetimer |= 1;
        undoAreaSetCheckForChange(UNDO_AREA_INSTRUMENTS, editorInfo.einum, UNDO_AREA_DIRTY_CHECK);

        // The line of the value changed is highlighted
        editorInfo.eipos = param;
        editorInfo.eicolumn = high ? 0 : 1;
      }
      return 1;
    }
  }

  // Octave
  if ((mousey == 35) && (mousex <= 7))
  {
    editorInfo.epoctave = clampvalue(editorInfo.epoctave + d, 0, 7);
    return 1;
  }

  // MIDI input (notes on 1-3 patterns)
  if ((mousey >= 35) && (mousex >= INPUTCOLUMN) && (mousex < INPUTCOLUMN + INPUTWIDTH))
  {
    setmidichord(clampvalue(midichord + d, 1, MAX_CHN));
    return 1;
  }

  // STEPS
  if ((mousey >= 35) && (mousex >= STEPSCOLUMN) && (mousex < STEPSCOLUMN + STEPSWIDTH))
  {
    seteditstep(clampvalue(editstep + d, 0, MAX_EDITSTEP));
    return 1;
  }

  return 0;
}

void retro_mousecommands(void)
{
  int c;

  // Mouse wheel over a value changes it. Over the patterns it scrolls the
  // pattern editor. While the song plays, the view then stops following the
  // playback (as in the modern interface) until playback is started again
  // or stops
  if ((mousewheel) && (!retrowheel()) && (mousey >= 2) && (mousey <= PATTROWY + VISIBLEPATTROWS) && (mousex < MAX_CHN * 15))
  {
    if ((isplaying()) && (followplay)) followsuspended = 1;
    patternscroll(mousewheel);
  }

  if (!mouseb) return;

  // Middle button, anywhere: switch between edit mode and jam mode. It does
  // nothing else
  if (mouseb & MOUSEB_MIDDLE)
  {
    if (!(prevmouseb & MOUSEB_MIDDLE))
    {
      recordmode ^= 1;
      gui_message(recordmode ? "Edit mode: notes are entered" : "Jam mode: notes are only played");
    }
    return;
  }

  // The effects panel: pick an effect or let it go
  if ((mouseb & MOUSEB_LEFT) && (!prevmouseb)) fxclick();

  // Pattern editpos & pattern number selection
  for (c = 0; c < MAX_CHN; c++)
  {
    if ((mousey == 2) && (mousex >= 13 + c * 15) && (mousex <= 14 + c * 15))
    {
      if ((!prevmouseb) || (mouseheld > HOLDDELAY))
      {
        if (mouseb & MOUSEB_LEFT)
        {
          editorInfo.epchn = c;
          nextpattern();
        }
        if (mouseb & MOUSEB_RIGHT)
        {
          editorInfo.epchn = c;
          prevpattern();
        }
      }
    }
    else
    {
      if ((mousey >= 2) && (mousey <= PATTROWY + VISIBLEPATTROWS) && (mousex >= 6 + c * 15) && (mousex <= 14 + c * 15))
      {
        int x = mousex - 6 - c * 15;
        int newpos = mousey - PATTROWY + editorInfo.epview;
        if (newpos < 0) newpos = 0;
        if (newpos > pattlen[editorUndoInfo.editorInfo[editorInfo.epchn].epnum]) newpos = pattlen[editorUndoInfo.editorInfo[editorInfo.epchn].epnum];

        editorInfo.editmode = EDIT_PATTERN;

        if ((mouseb & MOUSEB_RIGHT) && (!prevmouseb))
        {
          if ((editorInfo.epmarkchn != c) || (newpos != editorInfo.epmarkend))
          {
            editorInfo.epmarkchn = c;
            editorInfo.epmarkstart = editorInfo.epmarkend = newpos;
          }
        }

        if (mouseb & MOUSEB_LEFT)
        {
          editorInfo.epchn = c;
          if (x < 4) editorInfo.epcolumn = 0;
          if (x >= 4) editorInfo.epcolumn = x - 3;
        }

        if (!prevmouseb)
        {
          if (mouseb & MOUSEB_LEFT)
            editorInfo.eppos = newpos;
        }
        else
        {
          if (mouseb & MOUSEB_LEFT)
          {
            if (mousey < PATTROWY) editorInfo.eppos--;
            if (mousey == PATTROWY + VISIBLEPATTROWS) editorInfo.eppos++;
          }
        }
        if (editorInfo.eppos < 0) editorInfo.eppos = 0;
        if (editorInfo.eppos > pattlen[editorUndoInfo.editorInfo[editorInfo.epchn].epnum]) editorInfo.eppos = pattlen[editorUndoInfo.editorInfo[editorInfo.epchn].epnum];

        if (mouseb & MOUSEB_RIGHT) editorInfo.epmarkend = newpos;
      }
    }
  }

  // Song editpos & songnumber selection. Only the orderlist rows: the rows
  // below are the instrument's (the 6-channel layout this came from had 6
  // orderlist rows, and a click there chose a channel that doesn't exist)
  if ((mousey >= 3) && (mousey < 3 + MAX_CHN) && (mousex >= RX))
  {
    int newpos = editorInfo.esview + (mousex - RX - 4) / 3;
    int newcolumn = (mousex - RX - 4) % 3;
    int newchn = mousey - 3;
    if (newcolumn < 0) newcolumn = 0;
    if (newcolumn > 1) newcolumn = 1;
    if (newpos < 0)
    {
      newpos = 0;
      newcolumn = 0;
    }
    if (newpos == songlen[editorInfo.esnum][editorInfo.eschn])
    {
      newpos++;
      newcolumn = 0;
    }
    if (newpos > songlen[editorInfo.esnum][editorInfo.eschn] + 1)
    {
      newpos = songlen[editorInfo.esnum][editorInfo.eschn] + 1;
      newcolumn = 1;
    }

    editorInfo.editmode = EDIT_ORDERLIST;

    if ((mouseb & MOUSEB_RIGHT) && (!prevmouseb) && (newpos < songlen[editorInfo.esnum][editorInfo.eschn]))
    {
      if ((editorInfo.esmarkchn != newchn) || (newpos != editorInfo.esmarkend))
      {
        editorInfo.esmarkchn = newchn;
        editorInfo.esmarkstart = editorInfo.esmarkend = newpos;
      }
    }

    if (mouseb & MOUSEB_LEFT)
    {
      editorInfo.eschn = newchn;
      editorInfo.eseditpos = newpos;
      editorInfo.escolumn = newcolumn;
    }

    if ((mouseb & MOUSEB_RIGHT) && (newpos < songlen[editorInfo.esnum][editorInfo.eschn])) editorInfo.esmarkend = newpos;
  }
  if (((!prevmouseb) || (mouseheld > HOLDDELAY)) && (mousey == 2) && (mousex >= RX + 23) && (mousex <= RX + 24))
  {
    if (mouseb & MOUSEB_LEFT) nextsong();
    if (mouseb & MOUSEB_RIGHT) prevsong();
  }

  // Instrument editpos & instrument number selection
  if ((mousey >= 8) && (mousey <= 12) && (mousex >= RX + 16) && (mousex <= RX + 17))
  {
    editorInfo.editmode = EDIT_INSTRUMENT;
    editorInfo.eipos = mousey - 8;
    editorInfo.eicolumn = mousex - RX - 16;
  }
  if ((mousey >= 8) && (mousey <= 11) && (mousex >= RX + 36) && (mousex <= RX + 37))
  {
    editorInfo.editmode = EDIT_INSTRUMENT;
    editorInfo.eipos = mousey - 8 + 5;
    editorInfo.eicolumn = mousex - RX - 36;
  }
  if ((mousey == 7) && (mousex >= RX + 20))
  {
    editorInfo.editmode = EDIT_INSTRUMENT;
    editorInfo.eipos = 9;
  }
  if (((!prevmouseb) || (mouseheld > HOLDDELAY)) && (mousey == 7) && (mousex >= RX + 16) && (mousex <= RX + 17))
  {
    if (mouseb & MOUSEB_LEFT) nextinstr();
    if (mouseb & MOUSEB_RIGHT) previnstr();
  }

  // Table editpos
  for (c = 0; c < MAX_TABLES; c++)
  {
    if ((mousey >= 14) && (mousey <= 30) && (mousex >= RX + 3 + c * 10) && (mousex <= RX + 7 + c * 10))
    {
      int newpos = mousey - 15 + editorInfo.etview[editorInfo.etnum];
      if (newpos < 0) newpos = 0;
      if (newpos >= MAX_TABLELEN) newpos = MAX_TABLELEN - 1;

      editorInfo.editmode = EDIT_TABLES;

      if ((mouseb & MOUSEB_RIGHT) && (!prevmouseb))
      {
        if ((editorInfo.etmarknum != editorInfo.etnum) || (newpos != editorInfo.etmarkend))
        {
          editorInfo.etmarknum = c;
          editorInfo.etmarkstart = editorInfo.etmarkend = newpos;
        }
      }
      if (mouseb & MOUSEB_LEFT)
      {
        editorInfo.etnum = c;
        editorInfo.etpos = mousey - 15 + editorInfo.etview[editorInfo.etnum];
        editorInfo.etcolumn = mousex - RX - 3 - c * 10;
      }
      if (editorInfo.etcolumn >= 2) editorInfo.etcolumn--;
      if (editorInfo.etpos < 0) editorInfo.etpos = 0;
      if (editorInfo.etpos > MAX_TABLELEN - 1) editorInfo.etpos = MAX_TABLELEN - 1;

      if (mouseb & MOUSEB_RIGHT) editorInfo.etmarkend = newpos;
    }
  }

  // Name editpos
  if ((mousey >= 31) && (mousey <= 33) && (mousex >= RX + 7))
  {
    editorInfo.editmode = EDIT_NAMES;
    editorInfo.enpos = mousey - 31;
  }

  // Status panel
  if ((!prevmouseb) && (mousex == 7) && (mousey == 23 + 3 + 9))
  {
    if (mouseb & (MOUSEB_LEFT))
      if (editorInfo.epoctave < 7) editorInfo.epoctave++;
    if (mouseb & (MOUSEB_RIGHT))
      if (editorInfo.epoctave > 0) editorInfo.epoctave--;
  }
  if ((!prevmouseb) && (mousex <= 7) && (mousey == 24 + 3 + 9))
  {
    recordmode ^= 1;
  }
  // MIDI input (notes on 1-3 patterns): left button = more, right = fewer
  if ((!prevmouseb) && (mousex >= INPUTCOLUMN) && (mousex < INPUTCOLUMN + INPUTWIDTH) && (mousey >= 23 + 3 + 9))
  {
    if (mouseb & MOUSEB_LEFT) setmidichord(midichord < MAX_CHN ? midichord + 1 : 1);
    if (mouseb & MOUSEB_RIGHT) setmidichord(midichord > 1 ? midichord - 1 : MAX_CHN);
  }
  // STEPS: left button = more, right = fewer (held down: repeats)
  if (((!prevmouseb) || (mouseheld > HOLDDELAY)) && (mousex >= STEPSCOLUMN) && (mousex < STEPSCOLUMN + STEPSWIDTH) && (mousey >= 23 + 3 + 9))
  {
    if (mouseb & MOUSEB_LEFT) seteditstep(editstep < MAX_EDITSTEP ? editstep + 1 : 0);
    if (mouseb & MOUSEB_RIGHT) seteditstep(editstep > 0 ? editstep - 1 : MAX_EDITSTEP);
  }
  for (c = 0; c < MAX_CHN; c++)
  {
    if ((!prevmouseb) && (mousey >= 23 + 3 + 9) && (mousex >= 80 + 7 * c) && (mousex <= 85 + 7 * c))
      mutechannel(c);
  }

  // Titlebar actions
  if (!menu)
  {
    if ((mousey == 0) && (!prevmouseb) && (mouseb == MOUSEB_LEFT)) retro_titlebarclick(mousex);
  }
  else
  {
    // The menu bar (shown while the right mouse button is held on the title
    // bar): click an item with the left button
    if ((!mousey) && (mouseb & MOUSEB_LEFT) && (!(prevmouseb & MOUSEB_LEFT)))
    {
      if ((mousex >= 0) && (mousex <= 5))
        retroplay(PLAY_BEGINNING);
      if ((mousex >= 7) && (mousex <= 15))
        retroplay(PLAY_POS);
      if ((mousex >= 17) && (mousex <= 26))
        retroplay(PLAY_PATTERN);
      if ((mousex >= 28) && (mousex <= 33))
        stopsong();
      if ((mousex >= 35) && (mousex <= 40))
        load();
      if ((mousex >= 42) && (mousex <= 47))
        save();
      if ((mousex >= 49) && (mousex <= 57))
        runcommand(GTCMD_EXPORT, NULL);
      if ((mousex >= 59) && (mousex <= 64))
        onlinehelp(0, 0);
      if ((mousex >= 66) && (mousex <= 72))
        gui_askclear();
      if ((mousex >= 74) && (mousex <= 79))
        gui_askquit();
      if ((mousex >= 81) && (mousex <= 83))
        changetextsize(-1);
      if ((mousex >= 84) && (mousex <= 87))
        changetextsize(1);
      if ((mousex >= 89) && (mousex <= 94))
        togglefullscreen();
      if (mousex >= 96)
        gui_openviewmenu();
    }
  }
}
