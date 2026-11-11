//
// GOATTRACKER v2 display state: note names, the song timer and following the
// playback position. The modern interface is drawn by the GUI layer (gui/),
// the retro interface's main screen by gretro.c.
//

#define GDISPLAY_C

#include "goattrk2.h"

char *notename[] =
 {"C-0", "C#0", "D-0", "D#0", "E-0", "F-0", "F#0", "G-0", "G#0", "A-0", "A#0", "B-0",
  "C-1", "C#1", "D-1", "D#1", "E-1", "F-1", "F#1", "G-1", "G#1", "A-1", "A#1", "B-1",
  "C-2", "C#2", "D-2", "D#2", "E-2", "F-2", "F#2", "G-2", "G#2", "A-2", "A#2", "B-2",
  "C-3", "C#3", "D-3", "D#3", "E-3", "F-3", "F#3", "G-3", "G#3", "A-3", "A#3", "B-3",
  "C-4", "C#4", "D-4", "D#4", "E-4", "F-4", "F#4", "G-4", "G#4", "A-4", "A#4", "B-4",
  "C-5", "C#5", "D-5", "D#5", "E-5", "F-5", "F#5", "G-5", "G#5", "A-5", "A#5", "B-5",
  "C-6", "C#6", "D-6", "D#6", "E-6", "F-6", "F#6", "G-6", "G#6", "A-6", "A#6", "B-6",
  "C-7", "C#7", "D-7", "D#7", "E-7", "F-7", "F#7", "G-7", "G#7", "...", "---", "+++"};

int timemin = 0;
int timesec = 0;
int timeframe = 0;

// Set when the user scrolls the pattern editor during playback: the view
// stops following the playback (like "catch" in a DAW) until playback stops
// or follow is switched on again
int followsuspended = 0;

// Called by the classic dialogs when they close. The retro interface draws
// its main screen again; the modern interface has no main screen on the
// text screen
void printmainscreen(void)
{
  if (retromode)
  {
    clearscreen();
    retro_printstatus();
    return;
  }
  textscreenactive = 0;
}

// Once per frame: the cursor flash, and the retro interface's main screen
void displayupdate(void)
{
  if (cursorflashdelay >= 6)
  {
    cursorflashdelay %= 6;
    cursorflash++;
    cursorflash &= 3;
  }
  if (retromode) retro_printstatus();
}

// Moves the pattern and orderlist edit positions along with the playback
// when follow playback is on. Called once per frame.
void followplayback(void)
{
  int c;

  if (!isplaying())
  {
    followsuspended = 0;
    return;
  }
  if ((!followplay) || (followsuspended)) return;

  for (c = 0; c < MAX_CHN; c++)
  {
    int newpos = chn[c].pattptr / 4;
    if (chn[c].advance) editorUndoInfo.editorInfo[c].epnum = chn[c].pattnum;

    if (newpos > pattlen[editorUndoInfo.editorInfo[c].epnum]) newpos = pattlen[editorUndoInfo.editorInfo[c].epnum];

    if (c == editorInfo.epchn)
    {
      editorInfo.eppos = newpos;
      editorInfo.epview = newpos-VISIBLEPATTROWS/2;
    }

    newpos = chn[c].songptr;
    newpos--;
    if (newpos < 0) newpos = 0;
    if (newpos > songlen[editorInfo.esnum][c]) newpos = songlen[editorInfo.esnum][c];

    if ((c == editorInfo.eschn) && (chn[c].advance))
    {
      editorInfo.eseditpos = newpos;
      if (newpos - editorInfo.esview < 0)
      {
        editorInfo.esview = newpos;
      }
      if (newpos - editorInfo.esview >= VISIBLEORDERLIST)
      {
        editorInfo.esview = newpos - VISIBLEORDERLIST + 1;
      }
    }
  }
}

void resettime(void)
{
  timemin = 0;
  timesec = 0;
  timeframe = 0;
}

void incrementtime(void)
{
  {
    timeframe++;
    if (!editorInfo.ntsc)
    {
      if (((editorInfo.multiplier) && (timeframe >= PALFRAMERATE* editorInfo.multiplier))
          || ((!editorInfo.multiplier) && (timeframe >= PALFRAMERATE/2)))
      {
        timeframe = 0;
        timesec++;
      }
    }
    else
    {
      if (((editorInfo.multiplier) && (timeframe >= NTSCFRAMERATE* editorInfo.multiplier))
          || ((!editorInfo.multiplier) && (timeframe >= NTSCFRAMERATE/2)))
      {
        timeframe = 0;
        timesec++;
      }
    }
    if (timesec == 60)
    {
      timesec = 0;
      timemin++;
      timemin %= 60;
    }
  }
}
