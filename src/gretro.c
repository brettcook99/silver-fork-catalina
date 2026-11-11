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

// Starts playing like F1-F3 (SHIFT: the other follow way)
static void retroplay(int mode)
{
  initsong(editorInfo.esnum, mode);
  followplay = followmode ^ shiftpressed;
  followsuspended = 0;
}

void retro_printstatus(void)
{
  int c, d, color, color2;
  int cc = cursorcolortable[cursorflash];
  menu = 0;

  if ((mouseb > MOUSEB_LEFT) && (mousey <= 1) && (!eamode)) menu = 1;

  printblankc(0, 0, 15+16, MAX_COLUMNS);

  if (!menu)
  {
    if (!strlen(loadedsongfilename))
      sprintf(textbuffer, "%s", programname);
    else
      sprintf(textbuffer, "%s - %s", programname, gt_basename(loadedsongfilename));
    textbuffer[49] = 0;
    printtext(0, 0, 15+16, textbuffer);

    int fg_active = 7;
    int fg_inactive = 8;

    printtext(40+10, 0, fg_inactive+16, "FV PO RO");

    if (editorInfo.finevibrato)
      printtext(40+10, 0, fg_active+16, "FV");

    if (editorInfo.optimizepulse)
      printtext(43+10, 0, fg_active+16, "PO");

    if (editorInfo.optimizerealtime)
      printtext(46+10, 0, fg_active+16, "RO");

    if (editorInfo.ntsc)
      printtext(49+10, 0, fg_active+16, "NTSC");
    else
      printtext(49+10, 0, fg_active+16, " PAL");

    if (!editorInfo.sidmodel)
      printtext(54+10, 0, fg_active+16, "6581");
    else
      printtext(54+10, 0, fg_active+16, "8580");

    sprintf(textbuffer, "HR:%04X", editorInfo.adparam);
    printtext(59+10, 0, fg_active+16, textbuffer);
    if (eamode) printbg(62+10+ editorInfo.eacolumn, 0, cc, 1);

    if (editorInfo.multiplier)
    {
      sprintf(textbuffer, "%2dx", editorInfo.multiplier);
      printtext(67+10, 0, fg_active+16, textbuffer);
    }
    else printtext(67+10, 0, fg_active+16, "25Hz");

    printtext(72+10, 0, fg_inactive+16, "INT FP");

    if (interpolate & 1) {
      printtext(72+10, 0, fg_active+16, "INT");
    }

    if (interpolate & 2) {
      printtext(76+10, 0, fg_active+16, "FP");
    }

    printtext(72+20, 0, 15+16, "F12=HELP");
  }
  else
  {
    // C64 = the instrument designer
    printtext(0, 0, 15+16, " PLAY | PLAYPOS | PLAYPATT | STOP | LOAD | SAVE | PACK/RL | HELP | CLEAR | QUIT | A- A+ | FULL | C64");
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
        printtext(2+c*15, 3+d, CNORMAL, textbuffer);
      }
      else
      {
        printtext(2+c*15, 3+d, CCOMMAND, textbuffer);
      }
      if (color == CNORMAL)
        color2 = CCOMMAND;
      else
        color2 = color;
      printtext(6+c*15, 3+d, color2, &textbuffer[4]);
      printtext(10+c*15, 3+d, color, &textbuffer[8]);
      printtext(12+c*15, 3+d, color2, &textbuffer[10]);
      printtext(13+c*15, 3+d, color, &textbuffer[11]);
      if (c == editorInfo.epmarkchn)
      {
        if (editorInfo.epmarkstart <= editorInfo.epmarkend)
        {
          if ((p >= editorInfo.epmarkstart) && (p <= editorInfo.epmarkend))
            printbg(2+c*15+4, 3+d, 1, 9);
        }
        else
        {
          if ((p <= editorInfo.epmarkstart) && (p >= editorInfo.epmarkend))
            printbg(2+c*15+4, 3+d, 1, 9);
        }
      }
      if ((color == CEDIT) && (editorInfo.editmode == EDIT_PATTERN) && (editorInfo.epchn == c))
      {
        switch(editorInfo.epcolumn)
        {
          case 0:
          if (!eamode) printbg(2+c*15+4, 3+d, cc, 3);
          break;

          default:
          if (!eamode) printbg(2+c*15+7+editorInfo.epcolumn, 3+d, cc, 1);
          break;
        }
      }
    }
  }

  sprintf(textbuffer, "CHN ORDERLIST (SUBTUNE %02X, POS %02X)", editorInfo.esnum, editorInfo.eseditpos);
  printtext(40+10, 2, CTITLE, textbuffer);
  for (c = 0; c < MAX_CHN; c++)
  {
    sprintf(textbuffer, " %d ", c+1);
    printtext(40+10, 3+c, 15, textbuffer);
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
      printtext(44+10+d*3, 3+c, color, textbuffer);
      if (c == editorInfo.esmarkchn)
      {
        if (editorInfo.esmarkstart <= editorInfo.esmarkend)
        {
          if ((p >= editorInfo.esmarkstart) && (p <= editorInfo.esmarkend))
          {
            if (p != editorInfo.esmarkend)
              printbg(44+10+d*3, 3+c, 1, 3);
            else
              printbg(44+10+d*3, 3+c, 1, 2);
          }
        }
        else
        {
          if ((p <= editorInfo.esmarkstart) && (p >= editorInfo.esmarkend))
          {
            if (p != editorInfo.esmarkstart)
              printbg(44+10+d*3, 3+c, 1, 3);
            else
              printbg(44+10+d*3, 3+c, 1, 2);
          }
        }
      }
      if ((p == editorInfo.eseditpos) && (editorInfo.editmode == EDIT_ORDERLIST) && (editorInfo.eschn == c))
      {
        if (!eamode) printbg(44+10+d*3+editorInfo.escolumn, 3+c, cc, 1);
      }
    }
  }

  sprintf(textbuffer, "INSTRUMENT NUM. %02X  %-16s", editorInfo.einum, instr[editorInfo.einum].name);
  printtext(40+10, 7, CTITLE, textbuffer);

  sprintf(textbuffer, "Attack/Decay    %02X", instr[editorInfo.einum].ad);
  if (editorInfo.eipos == 0) color = CEDIT; else color = CNORMAL;
  printtext(40+10, 8, color, textbuffer);

  sprintf(textbuffer, "Sustain/Release %02X", instr[editorInfo.einum].sr);
  if (editorInfo.eipos == 1) color = CEDIT; else color = CNORMAL;
  printtext(40+10, 9, color, textbuffer);

  sprintf(textbuffer, "Wavetable Pos   %02X", instr[editorInfo.einum].ptr[WTBL]);
  if (editorInfo.eipos == 2) color = CEDIT; else color = CNORMAL;
  printtext(40+10, 10, color, textbuffer);

  sprintf(textbuffer, "Pulsetable Pos  %02X", instr[editorInfo.einum].ptr[PTBL]);
  if (editorInfo.eipos == 3) color = CEDIT; else color = CNORMAL;
  printtext(40+10, 11, color, textbuffer);

  sprintf(textbuffer, "Filtertable Pos %02X", instr[editorInfo.einum].ptr[FTBL]);
  if (editorInfo.eipos == 4) color = CEDIT; else color = CNORMAL;
  printtext(40+10, 12, color, textbuffer);

  sprintf(textbuffer, "Vibrato Param   %02X", instr[editorInfo.einum].ptr[STBL]);
  if (editorInfo.eipos == 5) color = CEDIT; else color = CNORMAL;
  printtext(60+10, 8, color, textbuffer);

  sprintf(textbuffer, "Vibrato Delay   %02X", instr[editorInfo.einum].vibdelay);
  if (editorInfo.eipos == 6) color = CEDIT; else color = CNORMAL;
  printtext(60+10, 9, color, textbuffer);

  sprintf(textbuffer, "HR/Gate Timer   %02X", instr[editorInfo.einum].gatetimer);
  if (editorInfo.eipos == 7) color = CEDIT; else color = CNORMAL;
  printtext(60+10, 10, color, textbuffer);

  sprintf(textbuffer, "1stFrame Wave   %02X", instr[editorInfo.einum].firstwave);
  if (editorInfo.eipos == 8) color = CEDIT; else color = CNORMAL;
  printtext(60+10, 11, color, textbuffer);

  if (editorInfo.editmode == EDIT_INSTRUMENT)
  {
    if (editorInfo.eipos < 9)
    {
      if (!eamode) printbg(56+10+editorInfo.eicolumn+20*(editorInfo.eipos/5), 8+(editorInfo.eipos%5), cc, 1);
    }
    else
    {
      if (!eamode) printbg(60+10+strlen(instr[editorInfo.einum].name), 7, cc, 1);
    }
  }

  sprintf(textbuffer, "WAVE TBL  PULSETBL  FILT.TBL  SPEEDTBL");
  printtext(40+10, 14, CTITLE, textbuffer);

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
      printtext(40+10+10*c, 15+d, color, textbuffer);

      if (editorInfo.etmarknum == c)
      {
        if (editorInfo.etmarkstart <= editorInfo.etmarkend)
        {
          if ((p >= editorInfo.etmarkstart) && (p <= editorInfo.etmarkend))
            printbg(40+10+10*c+3, 15+d, 1, 5);
        }
        else
        {
          if ((p <= editorInfo.etmarkstart) && (p >= editorInfo.etmarkend))
            printbg(40+10+10*c+3, 15+d, 1, 5);
        }
      }
    }
  }

  if (editorInfo.editmode == EDIT_TABLES)
  {
    if (!eamode) printbg(43+10+editorInfo.etnum*10+(editorInfo.etcolumn & 1)+(editorInfo.etcolumn/2)*3, 15+editorInfo.etpos-editorInfo.etview[editorInfo.etnum], cc, 1);
  }

  printtext(40+10, 31, CTITLE, "NAME   ");
  sprintf(textbuffer, "%-32s", songname);
  printtext(47+10, 31, CEDIT, textbuffer);

  printtext(40+10, 32, CTITLE, "AUTHOR ");
  sprintf(textbuffer, "%-32s", authorname);
  printtext(47+10, 32, CEDIT, textbuffer);

  printtext(40+10, 33, CTITLE, "COPYR. ");
  sprintf(textbuffer, "%-32s", copyrightname);
  printtext(47+10, 33, CEDIT, textbuffer);

  if ((editorInfo.editmode == EDIT_NAMES) && (!eamode))
  {
    switch(editorInfo.enpos)
    {
      case 0:
      printbg(47+10+strlen(songname), 31, cc, 1);
      break;
      case 1:
      printbg(47+10+strlen(authorname), 32, cc, 1);
      break;
      case 2:
      printbg(47+10+strlen(copyrightname), 33, cc, 1);
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

  // Messages (file opened, saved, imported...) where the credits used to be
  printblankc(20, 35, CNORMAL, 57);
  printblankc(20, 36, CNORMAL, 57);
  {
    const char *msg = gui_currentmessage();
    if (msg)
    {
      char line[58];
      snprintf(line, sizeof line, "%s", msg);
      printtext(20, 36, CTITLE, line);
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
}

void retro_mousecommands(void)
{
  int c;

  // Mouse wheel over the patterns scrolls the pattern editor. While the song
  // plays, the view then stops following the playback (as in the modern
  // interface) until playback is started again or stops
  if ((mousewheel) && (mousey >= 2) && (mousey <= 2 + VISIBLEPATTROWS + 1) && (mousex < MAX_CHN * 15))
  {
    if ((isplaying()) && (followplay)) followsuspended = 1;
    patternscroll(mousewheel);
  }

  if (!mouseb) return;

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
      if ((mousey >= 2) && (mousey <= 34) && (mousex >= 6 + c * 15) && (mousex <= 14 + c * 15))
      {
        int x = mousex - 6 - c * 15;
        int newpos = mousey - 3 + editorInfo.epview;
        if (newpos < 0) newpos = 0;
        if (newpos > pattlen[editorUndoInfo.editorInfo[editorInfo.epchn].epnum]) newpos = pattlen[editorUndoInfo.editorInfo[editorInfo.epchn].epnum];

        editorInfo.editmode = EDIT_PATTERN;

        if ((mouseb & (MOUSEB_RIGHT | MOUSEB_MIDDLE)) && (!prevmouseb))
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
            if (mousey == 2) editorInfo.eppos--;
            if (mousey == 34) editorInfo.eppos++;
          }
        }
        if (editorInfo.eppos < 0) editorInfo.eppos = 0;
        if (editorInfo.eppos > pattlen[editorUndoInfo.editorInfo[editorInfo.epchn].epnum]) editorInfo.eppos = pattlen[editorUndoInfo.editorInfo[editorInfo.epchn].epnum];

        if (mouseb & (MOUSEB_RIGHT | MOUSEB_MIDDLE)) editorInfo.epmarkend = newpos;
      }
    }
  }

  // Song editpos & songnumber selection
  if ((mousey >= 3) && (mousey <= 8) && (mousex >= 40 + 10))
  {
    int newpos = editorInfo.esview + (mousex - 44 - 10) / 3;
    int newcolumn = (mousex - 44 - 10) % 3;
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

    if ((mouseb & (MOUSEB_RIGHT | MOUSEB_MIDDLE)) && (!prevmouseb) && (newpos < songlen[editorInfo.esnum][editorInfo.eschn]))
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

    if ((mouseb & (MOUSEB_RIGHT | MOUSEB_MIDDLE)) && (newpos < songlen[editorInfo.esnum][editorInfo.eschn])) editorInfo.esmarkend = newpos;
  }
  if (((!prevmouseb) || (mouseheld > HOLDDELAY)) && (mousey == 2) && (mousex >= 63 + 10) && (mousex <= 64 + 10))
  {
    if (mouseb & MOUSEB_LEFT) nextsong();
    if (mouseb & MOUSEB_RIGHT) prevsong();
  }

  // Instrument editpos & instrument number selection
  if ((mousey >= 8) && (mousey <= 12) && (mousex >= 56 + 10) && (mousex <= 57 + 10))
  {
    editorInfo.editmode = EDIT_INSTRUMENT;
    editorInfo.eipos = mousey - 8;
    editorInfo.eicolumn = mousex - 56 - 10;
  }
  if ((mousey >= 8) && (mousey <= 11) && (mousex >= 76 + 10) && (mousex <= 77 + 10))
  {
    editorInfo.editmode = EDIT_INSTRUMENT;
    editorInfo.eipos = mousey - 8 + 5;
    editorInfo.eicolumn = mousex - 76 - 10;
  }
  if ((mousey == 7) && (mousex >= 60 + 10))
  {
    editorInfo.editmode = EDIT_INSTRUMENT;
    editorInfo.eipos = 9;
  }
  if (((!prevmouseb) || (mouseheld > HOLDDELAY)) && (mousey == 7) && (mousex >= 56 + 10) && (mousex <= 57 + 10))
  {
    if (mouseb & MOUSEB_LEFT) nextinstr();
    if (mouseb & MOUSEB_RIGHT) previnstr();
  }

  // Table editpos
  for (c = 0; c < MAX_TABLES; c++)
  {
    if ((mousey >= 14) && (mousey <= 30) && (mousex >= 43 + 10 + c * 10) && (mousex <= 47 + 10 + c * 10))
    {
      int newpos = mousey - 15 + editorInfo.etview[editorInfo.etnum];
      if (newpos < 0) newpos = 0;
      if (newpos >= MAX_TABLELEN) newpos = MAX_TABLELEN - 1;

      editorInfo.editmode = EDIT_TABLES;

      if ((mouseb & (MOUSEB_RIGHT | MOUSEB_MIDDLE)) && (!prevmouseb))
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
        editorInfo.etcolumn = mousex - 43 - 10 - c * 10;
      }
      if (editorInfo.etcolumn >= 2) editorInfo.etcolumn--;
      if (editorInfo.etpos < 0) editorInfo.etpos = 0;
      if (editorInfo.etpos > MAX_TABLELEN - 1) editorInfo.etpos = MAX_TABLELEN - 1;

      if (mouseb & (MOUSEB_RIGHT | MOUSEB_MIDDLE)) editorInfo.etmarkend = newpos;
    }
  }

  // Name editpos
  if ((mousey >= 31) && (mousey <= 33) && (mousex >= 47 + 10))
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
  for (c = 0; c < MAX_CHN; c++)
  {
    if ((!prevmouseb) && (mousey >= 23 + 3 + 9) && (mousex >= 80 + 7 * c) && (mousex <= 85 + 7 * c))
      mutechannel(c);
  }

  // Titlebar actions
  if (!menu)
  {
    if ((mousey == 0) && (!prevmouseb) && (mouseb == MOUSEB_LEFT))
    {
      if ((mousex >= 40 + 10) && (mousex <= 41 + 10))
      {
        undoCreateEditorInfoBackup();
        editorInfo.finevibrato ^= 1;
        undoAddEditorSettingsToList();
      }
      if ((mousex >= 43 + 10) && (mousex <= 44 + 10))
      {
        undoCreateEditorInfoBackup();
        editorInfo.optimizepulse ^= 1;
        undoAddEditorSettingsToList();
      }
      if ((mousex >= 46 + 10) && (mousex <= 47 + 10))
      {
        undoCreateEditorInfoBackup();
        editorInfo.optimizerealtime ^= 1;
        undoAddEditorSettingsToList();
      }
      if ((mousex >= 49 + 10) && (mousex <= 52 + 10))
      {
        undoCreateEditorInfoBackup();
        editorInfo.ntsc ^= 1;
        undoAddEditorSettingsToList();
        do_sound_init();
      }
      if ((mousex >= 54 + 10) && (mousex <= 57 + 10))
      {
        undoCreateEditorInfoBackup();
        editorInfo.sidmodel ^= 1;
        undoAddEditorSettingsToList();
        do_sound_init();
      }
      if ((mousex >= 62 + 10) && (mousex <= 65 + 10)) gui_edithradsr();
      if ((mousex >= 67 + 10) && (mousex <= 68 + 10))
      {
        undoCreateEditorInfoBackup();
        prevmultiplier();
        undoAddEditorSettingsToList();
      }
      if ((mousex >= 69 + 10) && (mousex <= 70 + 10))
      {
        undoCreateEditorInfoBackup();
        nextmultiplier();
        undoAddEditorSettingsToList();
      }
      if ((mousex >= 72 + 10) && (mousex <= 74 + 10))
      {
        interpolate ^= 1;
        interpolate &= 1 + (interpolate << 1);  // turn off FP if INT is 0
        stopsong();
        do_sound_init();
      }
      if ((mousex >= 76 + 10) && (mousex <= 77 + 10))
      {
        interpolate ^= 2;
        interpolate |= interpolate >> 1;  // turn on INT if FP is 1
        stopsong();
        do_sound_init();
      }
      // F12=HELP
      if (mousex >= 72 + 20) onlinehelp(0, 0);
    }
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
        cmd_post(GTCMD_DESIGNER);
    }
  }
}
