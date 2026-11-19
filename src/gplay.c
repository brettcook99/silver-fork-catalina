//
// GOATTRACKER v2 playroutine
//

#define GPLAY_C

#include "goattrk2.h"

unsigned char freqtbllo[] = {
  0x17,0x27,0x39,0x4b,0x5f,0x74,0x8a,0xa1,0xba,0xd4,0xf0,0x0e,
  0x2d,0x4e,0x71,0x96,0xbe,0xe8,0x14,0x43,0x74,0xa9,0xe1,0x1c,
  0x5a,0x9c,0xe2,0x2d,0x7c,0xcf,0x28,0x85,0xe8,0x52,0xc1,0x37,
  0xb4,0x39,0xc5,0x5a,0xf7,0x9e,0x4f,0x0a,0xd1,0xa3,0x82,0x6e,
  0x68,0x71,0x8a,0xb3,0xee,0x3c,0x9e,0x15,0xa2,0x46,0x04,0xdc,
  0xd0,0xe2,0x14,0x67,0xdd,0x79,0x3c,0x29,0x44,0x8d,0x08,0xb8,
  0xa1,0xc5,0x28,0xcd,0xba,0xf1,0x78,0x53,0x87,0x1a,0x10,0x71,
  0x42,0x89,0x4f,0x9b,0x74,0xe2,0xf0,0xa6,0x0e,0x33,0x20,0xff,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};

unsigned char freqtblhi[] = {
  0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x02,
  0x02,0x02,0x02,0x02,0x02,0x02,0x03,0x03,0x03,0x03,0x03,0x04,
  0x04,0x04,0x04,0x05,0x05,0x05,0x06,0x06,0x06,0x07,0x07,0x08,
  0x08,0x09,0x09,0x0a,0x0a,0x0b,0x0c,0x0d,0x0d,0x0e,0x0f,0x10,
  0x11,0x12,0x13,0x14,0x15,0x17,0x18,0x1a,0x1b,0x1d,0x1f,0x20,
  0x22,0x24,0x27,0x29,0x2b,0x2e,0x31,0x34,0x37,0x3a,0x3e,0x41,
  0x45,0x49,0x4e,0x52,0x57,0x5c,0x62,0x68,0x6e,0x75,0x7c,0x83,
  0x8b,0x93,0x9c,0xa5,0xaf,0xb9,0xc4,0xd0,0xdd,0xea,0xf8,0xff,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};

CHN chn[MAX_CHN];
EDITOR_UNDO_INFO editorUndoInfo;

unsigned char filterctrl = 0;
unsigned char filtertype = 0;
unsigned char filtercutoff = 0;
unsigned char filtertime = 0;
unsigned char filterptr = 0;
unsigned char funktable[2];
unsigned char masterfader = 0x0f;
int psnum = 0;
int songinit = 0;
// A note played while the song plays sounds at least this many frames
// before the pattern's next note (see playroutine())
#define JAMMINFRAMES 3
// A note played from the keyboard or MIDI the old way (HardSID / CatWeasel
// output; an emulated SID starts it at once, see playnow_run()) starts
// after this many hardrestart frames (fewer if the instrument's gate timer
// is shorter): enough for the envelope to start again cleanly, and quicker
// than waiting the instrument's whole gate timer (often 2-3 frames, 40-60
// ms) as a pattern note does
#define JAMHRFRAMES 1

// Notes played from the keyboard or MIDI start at once when the sound code
// starts them (see gplay.h, playnow_run()): playnowon. The editor's thread
// asks for them by mail: per channel the note |
// instrument << 8 | NOWMAIL_NOTE, and / or NOWMAIL_RELEASE (let go, after
// the note if it comes with one)
int playnowon = 0;
#define NOWMAIL_NOTE 0x10000
#define NOWMAIL_RELEASE 0x20000
static SDL_atomic_t nowmail[MAX_CHN];
// A recorded note's row that the player mustn't play again (see
// playrecordskip()): RECSKIP_VALID | pattern << 8 | row, per channel. It
// counts for the next row read only
#define RECSKIP_VALID 0x10000
static SDL_atomic_t recskip[MAX_CHN];
// The states of a note played at once (CHN nowstate): in its hardrestart,
// then playing with the channel on its own frame clock
#define NOW_HR 1
#define NOW_OWN 2
// Samples spent in the hardrestart (NOW_HR) / until the channel's next own
// frame (NOW_OWN), and whether that is the note's first after its start
static double nowcount[MAX_CHN];
static int nowfirst[MAX_CHN];
// The hardrestart lasts until the envelope is down (at the hardrestart
// ADSR's release rate 0 that takes 7 ms at most), but no longer than this
#define NOWHRSECONDS 0.008
// While it waits, the sound code renders this many samples at a time
#define NOWPOLLSAMPLES 16
static void nowmailframes(void);
static void nowtogrid(int c);
static void playchannel(int c);
static void chnregs(int c, CHN *cptr);
static void runchannels(int mask);

int lastsonginit = 0;
// SPACE / numpad 0 started the playback: the editor goes back there when it
// is stopped (gpattern.c). Every other start of playback clears it
int playreturnpoint = 0;
int startpattpos = 0;
int playloop = 0;
// Positions channels jump to (playjump()), -1 = none
static volatile int jumpto[MAX_CHN] = { -1, -1, -1 };

void sequencer(int c, CHN *cptr);

// Playing from a pattern or a position before the song has been played from
// its start: instead of the default tempo, the one the tempo instrument ($3F)
// starts the song with
static void starttempoinstr(CHN *cptr)
{
  int deftempo = editorInfo.multiplier ? 6 * editorInfo.multiplier - 1 : 6 - 1;

  if ((cptr->tempo == deftempo) && (instr[MAX_INSTR-1].ad >= 2) && (!(instr[MAX_INSTR-1].ptr[WTBL])))
    cptr->tempo = instr[MAX_INSTR-1].ad - 1;
}

void initchannels(void)
{
  int c;
  CHN *cptr = &chn[0];

  memset(chn, 0, sizeof chn);

  for (c = 0; c < MAX_CHN; c++)
  {
    chn[c].trans = 0;
    chn[c].instr = 1;
    if (editorInfo.multiplier)
      cptr->tempo = 6* editorInfo.multiplier-1;
    else
      cptr->tempo = 6-1;
    cptr++;
  }

  if (editorInfo.multiplier)
  {
    funktable[0] = 9* editorInfo.multiplier-1;
    funktable[1] = 6* editorInfo.multiplier-1;
  }
  else
  {
    funktable[0] = 9-1;
    funktable[1] = 6-1;
  }
}

void initsong(int num, int mode)
{
  sound_suspend();
  // Not in the middle of a player frame (see stopsong())
  sound_lockplayer();
  songinit = PLAY_STOPPED;
  psnum = num;
  songinit = mode;
  startpattpos = 0;
  playreturnpoint = 0;
  sound_unlockplayer();
  sound_flush();
}

void initsongpos(int num, int mode, int pattpos)
{
  sound_suspend();
  sound_lockplayer();
  songinit = PLAY_STOPPED;
  psnum = num;
  songinit = mode;
  startpattpos = pattpos;
  playreturnpoint = 0;
  sound_unlockplayer();
  sound_flush();
}

void stopsong(void)
{
  // The sound (the host's audio thread in the plugin) plays the song in
  // another thread: the player's state changes between its frames, not in
  // the middle of one (a stopped channel could read a row there while it has
  // none: a crash). The lock may be held already (stopsong() from the
  // player)
  if (songinit != PLAY_STOPPED)
  {
    sound_suspend();
    sound_lockplayer();
    songinit = PLAY_STOP;
    sound_unlockplayer();
    sound_flush();
  }
}

void rewindsong(void)
{
  if (lastsonginit == PLAY_BEGINNING) lastsonginit = PLAY_POS;
  initsong(psnum, lastsonginit);
}

// Frames until a played note of instrument ins starts (see JAMHRFRAMES)
static int jamframes(int ins)
{
  int t = instr[ins].gatetimer & 0x3f;

  if (t > JAMHRFRAMES) t = JAMHRFRAMES;
  return t + 1;
}

int playhighlightrows(int *first, int *last)
{
  int m = editorInfo.epmarkchn;
  int p;

  if ((m < 0) || (m >= MAX_CHN)) return 0;
  p = editorUndoInfo.editorInfo[m].epnum;
  *first = (editorInfo.epmarkstart <= editorInfo.epmarkend) ? editorInfo.epmarkstart : editorInfo.epmarkend;
  *last = (editorInfo.epmarkstart <= editorInfo.epmarkend) ? editorInfo.epmarkend : editorInfo.epmarkstart;
  if (*last >= pattlen[p]) *last = pattlen[p] - 1;
  return (*first >= 0) && (*first <= *last);
}

void playjump(int c, int pos)
{
  if ((c >= 0) && (c < MAX_CHN)) jumpto[c] = pos;
}

// A note played the old way: it starts on the player's frames, after the
// hardrestart frames (see playroutine())
static void playnoteframes(int note, int ins, int chnnum)
{
  chn[chnnum].nowstate = 0;
  if (!(instr[ins].gatetimer & 0x40))
  {
    chn[chnnum].gate = 0xfe; // Keyoff
    if (!(instr[ins].gatetimer & 0x80))
    {
      sidreg[0x5+chnnum*7] = editorInfo.adparam>>8; // Hardrestart
      sidreg[0x6+chnnum*7] = editorInfo.adparam&0xff;
    }
  }

  if (songinit == PLAY_STOPPED)
  {
    chn[chnnum].instr = ins;
    chn[chnnum].newnote = note;
    chn[chnnum].tick = jamframes(ins);
    chn[chnnum].gatetimer = instr[ins].gatetimer & 0x3f;
  }
  else
  {
    // While the song plays the note starts after the hardrestart frames, not
    // on the channel's next row, and the pattern's note of that row can't
    // take its place (see playroutine())
    chn[chnnum].jamnote = note;
    chn[chnnum].jaminstr = ins;
    chn[chnnum].jamtime = jamframes(ins);
  }
}

void playtestnote(int note, int ins, int chnnum)
{
  if (note == KEYON) return;
  if ((note == REST) || (note == KEYOFF))
  {
    releasenote(chnnum);
    return;
  }
  if ((chnnum < 0) || (chnnum >= MAX_CHN)) return;
  if ((ins < 0) || (ins >= MAX_INSTR)) ins = 1;

  // At once: the sound code starts it (a release asked for before it is
  // forgotten)
  if (playnowon)
  {
    SDL_AtomicSet(&nowmail[chnnum], (note & 0xff) | ((ins & 0xff) << 8) | NOWMAIL_NOTE);
    return;
  }
  playnoteframes(note, ins, chnnum);
}

void releasenote(int chnnum)
{
  if ((chnnum < 0) || (chnnum >= MAX_CHN)) return;
  // At once too (after the note, if it hasn't started yet)
  if (playnowon)
  {
    int old;

    do old = SDL_AtomicGet(&nowmail[chnnum]);
    while (!SDL_AtomicCAS(&nowmail[chnnum], old, old | NOWMAIL_RELEASE));
    return;
  }
  chn[chnnum].gate = 0xfe;
}

void mutechannel(int chnnum)
{
  chn[chnnum].mute ^= 1;
}

int playfilterptr(void)
{
  return filterptr;
}

int isplaying(void)
{
  return (songinit != PLAY_STOPPED);
}

// Nonzero while a start of playback waits for the player: the channels
// still show where the playback was before
int playstarting(void)
{
  return (songinit > PLAY_PLAYING) && (songinit < PLAY_STOP);
}

// Starts the channel's new note (cptr->newnote) with instrument iptr: its
// first wave, wave/pulse/filter table positions and ADSR. With toneporta
// (tone portamento command on the row) only the note changes
static void startnewnote(int c, CHN *cptr, INSTR *iptr, int toneporta)
{
  cptr->note = cptr->newnote-FIRSTNOTE;
  cptr->command = 0;
  cptr->vibdelay = iptr->vibdelay;
  cptr->cmddata = iptr->ptr[STBL];
  if (toneporta) return;

  if (iptr->firstwave)
  {
    if (iptr->firstwave >= 0xfe) cptr->gate = iptr->firstwave;
    else
    {
      cptr->wave = iptr->firstwave;
      cptr->gate = 0xff;
    }
  }

  cptr->ptr[WTBL] = iptr->ptr[WTBL];

  if (cptr->ptr[WTBL])
  {
    // Stop the song in case of jumping into a jump
    if (ltable[WTBL][cptr->ptr[WTBL]-1] == 0xff)
      stopsong();
  }
  if (iptr->ptr[PTBL])
  {
    cptr->ptr[PTBL] = iptr->ptr[PTBL];
    cptr->pulsetime = 0;
    if (cptr->ptr[PTBL])
    {
      // Stop the song in case of jumping into a jump
      if (ltable[PTBL][cptr->ptr[PTBL]-1] == 0xff)
        stopsong();
    }
  }
  if (iptr->ptr[FTBL])
  {
    filterptr = iptr->ptr[FTBL];
    filtertime = 0;
    if (filterptr)
    {
      // Stop the song in case of jumping into a jump
      if (ltable[FTBL][filterptr-1] == 0xff)
        stopsong();
    }
  }
  sidreg[0x5+7*c] = iptr->ad;
  sidreg[0x6+7*c] = iptr->sr;
}

void playroutine(void)
{
  CHN *cptr = &chn[0];
  int c;
  int mask;

  // Notes played while the sound doesn't start them at once (HardSID): the
  // old way, on these frames
  if (!playnowon) nowmailframes();

  if (songinit == PLAY_STOP)
    followplay = 0;

  if ((songinit > 0) && (songinit < PLAY_STOPPED))
  {
    lastsonginit = songinit;

    filterctrl = 0;
    filterptr = 0;

    resettime();

    if ((songinit == 0x02) || (songinit == 0x03))
    {
      if ((editorUndoInfo.editorInfo[0].espos >= songlen[psnum][0]) || (editorUndoInfo.editorInfo[1].espos >= songlen[psnum][1]) || (editorUndoInfo.editorInfo[2].espos >= songlen[psnum][2]))
         songinit = 0x01;
    }

    for (c = 0; c < MAX_CHN; c++)
    {
      cptr->songptr = 0;
      cptr->command = 0;
      cptr->cmddata = 0;
      cptr->newcommand = 0;
      cptr->newcmddata = 0;
      cptr->advance = 1;
      cptr->wave = 0;
      cptr->ptr[WTBL] = 0;
      cptr->newnote = 0;
      cptr->jamnote = 0;
      cptr->jamtime = 0;
      cptr->repeat = 0;
      cptr->looping = 0;
      cptr->lastvalid = 0;
      // A note played at once goes on with the player's frames (one in its
      // hardrestart still starts)
      if (cptr->nowstate == NOW_OWN) cptr->nowstate = 0;
      cptr->nowlead = 0;
      // No row of this playback has been read or started yet
      cptr->readvalid = 0;
      cptr->playvalid = 0;
      cptr->skipvalid = 0;
      SDL_AtomicSet(&recskip[c], 0);
      jumpto[c] = -1;
      if (editorInfo.multiplier)
        cptr->tick = 6* editorInfo.multiplier-1;
      else
        cptr->tick = 6-1;
      cptr->gatetimer = instr[1].gatetimer & 0x3f;
      cptr->pattptr = 0x7fffffff;
      if (cptr->tempo < 2) cptr->tempo = 0;

      switch (songinit)
      {
        case PLAY_BEGINNING:
        if (editorInfo.multiplier)
        {
          funktable[0] = 9* editorInfo.multiplier-1;
          funktable[1] = 6* editorInfo.multiplier-1;
          cptr->tempo = 6* editorInfo.multiplier-1;
        }
        else
        {
          funktable[0] = 9-1;
          funktable[1] = 6-1;
          cptr->tempo = 6-1;
        }
        if ((instr[MAX_INSTR-1].ad >= 2) && (!(instr[MAX_INSTR-1].ptr[WTBL])))
          cptr->tempo = instr[MAX_INSTR-1].ad - 1;
        cptr->trans = 0;
        cptr->instr = 1;
        sequencer(c, cptr);
        break;

        case PLAY_PATTERN:
        starttempoinstr(cptr);
        cptr->advance = 0;
        cptr->pattptr = startpattpos * 4;
        cptr->pattnum = editorUndoInfo.editorInfo[c].epnum;
        if (cptr->pattptr >= (pattlen[cptr->pattnum] * 4))
          cptr->pattptr = 0;
        break;

        case PLAY_POS:
        starttempoinstr(cptr);
        cptr->songptr = editorUndoInfo.editorInfo[c].espos;
        sequencer(c, cptr);
        break;
      }
      cptr++;
    }
    if (songinit != PLAY_STOP)
      songinit = 0;
    else
      songinit = PLAY_STOPPED;
    if ((!songlen[psnum][0]) || (!songlen[psnum][1]) || (!songlen[psnum][2]))
      songinit = PLAY_STOPPED; // Zero length song

    startpattpos = 0;
  }
  else
  {
    if (filterptr)
    {
      // Filter jump
      if (ltable[FTBL][filterptr-1] == 0xff)
      {
        filterptr = rtable[FTBL][filterptr-1];
        if (!filterptr) goto FILTERSTOP;
      }

      if (!filtertime)
      {
        // Filter set
        if (ltable[FTBL][filterptr-1] >= 0x80)
        {
          filtertype = ltable[FTBL][filterptr-1] & 0x70;
          filterctrl = rtable[FTBL][filterptr-1];
          filterptr++;
          // Can be combined with cutoff set
          if (ltable[FTBL][filterptr-1] == 0x00)
          {
            filtercutoff = rtable[FTBL][filterptr-1];
            filterptr++;
          }
        }
        else
        {
          // New modulation step
          if (ltable[FTBL][filterptr-1])
            filtertime = ltable[FTBL][filterptr-1];
          else
          {
            // Cutoff set
            filtercutoff = rtable[FTBL][filterptr-1];
            filterptr++;
          }
        }
      }
      // Filter modulation
      if (filtertime)
      {
        filtercutoff += rtable[FTBL][filterptr-1];
        filtertime--;
        if (!filtertime) filterptr++;
      }
    }
    FILTERSTOP:
    sidreg[0x15] = 0x00;
    sidreg[0x16] = filtercutoff;
    sidreg[0x17] = filterctrl;
    sidreg[0x18] = filtertype | masterfader;

    // The channels. One that plays a note played at once runs on its own
    // clock, or waits for the player's frames when it has run ahead of them
    // (only while the song plays: the rows must stay in time)
    mask = 0;
    for (c = 0; c < MAX_CHN; c++)
    {
      if (songinit == PLAY_STOPPED) chn[c].nowlead = 0;
      if (chn[c].nowstate)
      {
        if (songinit != PLAY_STOPPED) chn[c].nowlead--;
      }
      else if (chn[c].nowlead > 0) chn[c].nowlead--;
      else mask |= 1 << c;
    }
    runchannels(mask);
  }
  if (songinit != PLAY_STOPPED) incrementtime();
}

// One frame of the channels in mask (bit per channel): row timing, notes,
// tables and effects
static void runchannels(int mask)
{
  INSTR *iptr;
  CHN *cptr = &chn[0];
  int c;

  // (The channel loop as it was in runplayer())
  {
    for (c = 0; c < MAX_CHN; c++)
    {
      int jam = 0;

      if (!(mask & (1 << c)))
      {
        cptr++;
        continue;
      }

      // The channel goes to another orderlist position (playjump()): the
      // pattern there from the same row on, so that it stays in time with
      // the other channels. Between two patterns the next one read is the
      // one there. A loop starts again from there
      if (jumpto[c] >= 0)
      {
        int pos = jumpto[c];

        jumpto[c] = -1;
        if ((songinit != PLAY_STOPPED) && (cptr->advance) && (pos < songlen[psnum][c]))
        {
          unsigned pattptr = cptr->pattptr;

          cptr->songptr = pos;
          cptr->repeat = 0;
          cptr->looping = 0;
          cptr->lastvalid = 0;
          if (pattptr != 0x7fffffff)
          {
            cptr->pattptr = 0x7fffffff;
            sequencer(c, cptr);
            if (pattlen[cptr->pattnum]) cptr->pattptr = ((pattptr / 4) % pattlen[cptr->pattnum]) * 4;
          }
        }
      }

      iptr = &instr[cptr->instr];

      // Reset tempo in jammode
      if ((songinit == PLAY_STOPPED) && (cptr->tempo < 2))
      {
        if (editorInfo.multiplier)
          cptr->tempo = 6* editorInfo.multiplier-1;
        else
          cptr->tempo = 6-1;
      }

      // A note played from the keyboard or MIDI while the song plays starts
      // on this frame (see playtestnote())
      if (cptr->jamtime)
      {
        cptr->jamtime--;
        if (!cptr->jamtime) jam = 1;
      }

      if (cptr->rowframe < 0xff) cptr->rowframe++;

      // Decrease tick
      cptr->tick--;
      if (!cptr->tick) goto TICK0;

      // Tick N
      // Reload counter
      if (cptr->tick >= 0x80)
      {
        if (cptr->tempo >= 2)
          cptr->tick = cptr->tempo;
        else
        {
          // Set funktempo, switch between 2 values
          cptr->tick = funktable[cptr->tempo];
          cptr->tempo ^= 1;
        }
        cptr->rowlen = cptr->tick + 1;
        // Check for illegally high gatetimer and stop the song in this case
        if (chn->gatetimer > cptr->tick)
          stopsong();
      }

      // The played note starts between rows (no effects on this frame). In
      // the hardrestart frames just before a row (where the row is read) it
      // waits for the row instead, and takes the place of the row's note. So
      // it does when the row's note would cut it within JAMMINFRAMES
      if (jam)
      {
        int wait = (cptr->tick <= cptr->gatetimer);

        if ((!wait) && (cptr->tick <= cptr->gatetimer + JAMMINFRAMES) && (cptr->pattptr != 0x7fffffff))
        {
          unsigned char *row = &pattern[cptr->pattnum][cptr->pattptr];
          if ((row[0] <= LASTNOTE) && (row[2] != CMD_TONEPORTA)) wait = 1;
        }
        if (wait)
        {
          cptr->jamtime = cptr->tick;
          jam = 0;
        }
        else
        {
          cptr->instr = cptr->jaminstr;
          cptr->newnote = cptr->jamnote;
          cptr->jamnote = 0;
          startnewnote(c, cptr, &instr[cptr->instr], 0);
          cptr->newnote = 0;
          goto NEXTCHN;
        }
      }
      goto WAVEEXEC;

      // Tick 0
      TICK0:
      // Advance in sequencer
      sequencer(c, cptr);

      // The row read before starts now
      if (cptr->readvalid)
      {
        cptr->playpatt = cptr->readpatt;
        cptr->playrow = cptr->readrow;
        cptr->playtrans = cptr->readtrans;
        cptr->playvalid = 1;
        cptr->readvalid = 0;
      }
      cptr->rowframe = 0;
      cptr->rowlen = (cptr->tempo >= 2) ? cptr->tempo + 1 : funktable[cptr->tempo & 1] + 1;

      // A played note starting on a row goes before the pattern's note
      if (jam)
      {
        cptr->instr = cptr->jaminstr;
        cptr->newnote = cptr->jamnote;
        cptr->jamnote = 0;
        iptr = &instr[cptr->instr];
      }

      // Get gatetimer compare-value
      cptr->gatetimer = iptr->gatetimer & 0x3f;

      // New note init
      if (cptr->newnote)
        startnewnote(c, cptr, iptr, (!jam) && (cptr->newcommand == CMD_TONEPORTA));

      // Tick 0 effects

      switch (cptr->newcommand)
      {
        case CMD_DONOTHING:
        cptr->command = 0;
        cptr->cmddata = iptr->ptr[STBL];
        break;

        case CMD_PORTAUP:
        case CMD_PORTADOWN:
        cptr->vibtime = 0;
        cptr->command = cptr->newcommand;
        cptr->cmddata = cptr->newcmddata;
        break;

        case CMD_TONEPORTA:
        case CMD_VIBRATO:
        cptr->command = cptr->newcommand;
        cptr->cmddata = cptr->newcmddata;
        break;

        case CMD_SETAD:
        sidreg[0x5+7*c] = cptr->newcmddata;
        break;

        case CMD_SETSR:
        sidreg[0x6+7*c] = cptr->newcmddata;
        break;

        case CMD_SETWAVE:
        cptr->wave = cptr->newcmddata;
        break;

        case CMD_SETWAVEPTR:
        cptr->ptr[WTBL] = cptr->newcmddata;
        cptr->wavetime = 0;
        if (cptr->ptr[WTBL])
        {
          // Stop the song in case of jumping into a jump
          if (ltable[WTBL][cptr->ptr[WTBL]-1] == 0xff)
            stopsong();
        }
        break;

        case CMD_SETPULSEPTR:
        cptr->ptr[PTBL] = cptr->newcmddata;
        cptr->pulsetime = 0;
        if (cptr->ptr[PTBL])
        {
          // Stop the song in case of jumping into a jump
          if (ltable[PTBL][cptr->ptr[PTBL]-1] == 0xff)
            stopsong();
        }
        break;

        case CMD_SETFILTERPTR:
        filterptr = cptr->newcmddata;
        filtertime = 0;
        if (filterptr)
        {
          // Stop the song in case of jumping into a jump
          if (ltable[FTBL][filterptr-1] == 0xff)
            stopsong();
        }
        break;

        case CMD_SETFILTERCTRL:
        filterctrl = cptr->newcmddata;
        if (!filterctrl) filterptr = 0;
        break;

        case CMD_SETFILTERCUTOFF:
        filtercutoff = cptr->newcmddata;
        break;

        case CMD_SETMASTERVOL:
        if (cptr->newcmddata < 0x10)
          masterfader = cptr->newcmddata;
        break;

        case CMD_FUNKTEMPO:
        if (cptr->newcmddata)
        {
          funktable[0] = ltable[STBL][cptr->newcmddata-1]-1;
          funktable[1] = rtable[STBL][cptr->newcmddata-1]-1;
        }
        chn[0].tempo = 0;
        chn[1].tempo = 0;
        chn[2].tempo = 0;
        break;

        case CMD_SETTEMPO:
        {
          unsigned char newtempo = cptr->newcmddata & 0x7f;

          if (newtempo >= 3) newtempo--;
          if (cptr->newcmddata >= 0x80)
            cptr->tempo = newtempo;
          else
          {
            chn[0].tempo = newtempo;
            chn[1].tempo = newtempo;
            chn[2].tempo = newtempo;
          }
        }
        break;
      }
      if (cptr->newnote)
      {
        cptr->newnote = 0;
        if ((jam) || (cptr->newcommand != CMD_TONEPORTA)) goto NEXTCHN;
      }

      WAVEEXEC:
      if (cptr->ptr[WTBL])
      {
        unsigned char wave = ltable[WTBL][cptr->ptr[WTBL]-1];
        unsigned char note = rtable[WTBL][cptr->ptr[WTBL]-1];

        if (wave > WAVELASTDELAY)
        {
          // Normal waveform values
          if (wave < WAVESILENT) cptr->wave = wave;
          // Values without waveform selected
          if ((wave >= WAVESILENT) && (wave <= WAVELASTSILENT)) cptr->wave = wave & 0xf;
          // Command execution from wavetable
          if ((wave >= WAVECMD) && (wave <= WAVELASTCMD))
          {
            unsigned char param = rtable[WTBL][cptr->ptr[WTBL]-1];
            switch (wave & 0xf)
            {
              case CMD_DONOTHING:
              case CMD_SETWAVEPTR:
              case CMD_FUNKTEMPO:
              stopsong();
              break;

              case CMD_PORTAUP:
              {
                unsigned short speed = 0;
                if (param)
                {
                  speed = (ltable[STBL][param-1] << 8) | rtable[STBL][param-1];
                }
                if (speed >= 0x8000)
                {
                  speed = freqtbllo[cptr->lastnote + 1] | (freqtblhi[cptr->lastnote + 1] << 8);
                  speed -= freqtbllo[cptr->lastnote] | (freqtblhi[cptr->lastnote] << 8);
                  speed >>= rtable[STBL][param-1];
                }
                cptr->freq += speed;
              }
              break;

              case CMD_PORTADOWN:
              {
                unsigned short speed = 0;
                if (param)
                {
                  speed = (ltable[STBL][param-1] << 8) | rtable[STBL][param-1];
                }
                if (speed >= 0x8000)
                {
                  speed = freqtbllo[cptr->lastnote + 1] | (freqtblhi[cptr->lastnote + 1] << 8);
                  speed -= freqtbllo[cptr->lastnote] | (freqtblhi[cptr->lastnote] << 8);
                  speed >>= rtable[STBL][param-1];
                }
                cptr->freq -= speed;
              }
              break;

              case CMD_TONEPORTA:
              {
                unsigned short targetfreq = freqtbllo[cptr->note] | (freqtblhi[cptr->note] << 8);
                unsigned short speed = 0;

                if (!param)
                {
                  cptr->freq = targetfreq;
                  cptr->vibtime = 0;
                }
                else
                {
                  speed = (ltable[STBL][param-1] << 8) | rtable[STBL][param-1];
                  if (speed >= 0x8000)
                  {
                    speed = freqtbllo[cptr->lastnote + 1] | (freqtblhi[cptr->lastnote + 1] << 8);
                    speed -= freqtbllo[cptr->lastnote] | (freqtblhi[cptr->lastnote] << 8);
                    speed >>= rtable[STBL][param-1];
                  }
                  if (cptr->freq < targetfreq)
                  {
                    cptr->freq += speed;
                    if (cptr->freq > targetfreq)
                    {
                      cptr->freq = targetfreq;
                      cptr->vibtime = 0;
                    }
                  }
                  if (cptr->freq > targetfreq)
                  {
                    cptr->freq -= speed;
                    if (cptr->freq < targetfreq)
                    {
                      cptr->freq = targetfreq;
                      cptr->vibtime = 0;
                    }
                  }
                }
              }
              break;

              case CMD_VIBRATO:
              {
                unsigned short speed = 0;
                unsigned char cmpvalue = 0;

                if (param)
                {
                  cmpvalue = ltable[STBL][param-1];
                  speed = rtable[STBL][param-1];
                }
                if (cmpvalue >= 0x80)
                {
                  cmpvalue &= 0x7f;
                  speed = freqtbllo[cptr->lastnote + 1] | (freqtblhi[cptr->lastnote + 1] << 8);
                  speed -= freqtbllo[cptr->lastnote] | (freqtblhi[cptr->lastnote] << 8);
                  speed >>= rtable[STBL][param-1];
                }

                if ((cptr->vibtime < 0x80) && (cptr->vibtime > cmpvalue))
                  cptr->vibtime ^= 0xff;
                cptr->vibtime += 0x02;
                if (cptr->vibtime & 0x01)
                  cptr->freq -= speed;
                else
                  cptr->freq += speed;
              }
              break;

              case CMD_SETAD:
              sidreg[0x5+7*c] = param;
              break;

              case CMD_SETSR:
              sidreg[0x6+7*c] = param;;
              break;

              case CMD_SETWAVE:
              cptr->wave = param;
              break;

              case CMD_SETPULSEPTR:
              cptr->ptr[PTBL] = param;
              cptr->pulsetime = 0;
              if (cptr->ptr[PTBL])
              {
                // Stop the song in case of jumping into a jump
                if (ltable[PTBL][cptr->ptr[PTBL]-1] == 0xff)
                  stopsong();
              }
              break;

              case CMD_SETFILTERPTR:
              filterptr = param;
              filtertime = 0;
              if (filterptr)
              {
                // Stop the song in case of jumping into a jump
                if (ltable[FTBL][filterptr-1] == 0xff)
                stopsong();
              }
              break;

              case CMD_SETFILTERCTRL:
              filterctrl = param;
              if (!filterctrl) filterptr = 0;
              break;

              case CMD_SETFILTERCUTOFF:
              filtercutoff = param;
              break;

              case CMD_SETMASTERVOL:
              if (param < 0x10)
                masterfader = param;
              break;
            }
          }
        }
        else
        {
          // Wavetable delay
          if (cptr->wavetime != wave)
          {
            cptr->wavetime++;
            goto TICKNEFFECTS;
          }
        }

        cptr->wavetime = 0;
        cptr->ptr[WTBL]++;
        // Wavetable jump
        if (ltable[WTBL][cptr->ptr[WTBL]-1] == 0xff)
        {
          cptr->ptr[WTBL] = rtable[WTBL][cptr->ptr[WTBL]-1];
        }

        if ((wave >= WAVECMD) && (wave <= WAVELASTCMD))
          goto PULSEEXEC;

        if (note != 0x80)
        {
          if (note < 0x80)
            note += cptr->note;
          note &= 0x7f;
          cptr->freq = freqtbllo[note] | (freqtblhi[note]<<8);
          cptr->vibtime = 0;
          cptr->lastnote = note;
          goto PULSEEXEC;
        }
      }

      // Tick N command
      TICKNEFFECTS:
      // RESO: the optimizations work once per row of the song without it,
      // not on the rows in between, so that it sounds the same
      if ((!editorInfo.optimizerealtime) || (cptr->tick) || ((resomode > 1) && (cptr->playrow % resomode)))
      {
        switch(cptr->command)
        {
          case CMD_PORTAUP:
          {
            unsigned short speed = 0;
            if (cptr->cmddata)
            {
              speed = (ltable[STBL][cptr->cmddata-1] << 8) | rtable[STBL][cptr->cmddata-1];
            }
            if (speed >= 0x8000)
            {
              speed = freqtbllo[cptr->lastnote + 1] | (freqtblhi[cptr->lastnote + 1] << 8);
              speed -= freqtbllo[cptr->lastnote] | (freqtblhi[cptr->lastnote] << 8);
              speed >>= rtable[STBL][cptr->cmddata-1];
            }
            cptr->freq += speed;
          }
          break;

          case CMD_PORTADOWN:
          {
            unsigned short speed = 0;
            if (cptr->cmddata)
            {
              speed = (ltable[STBL][cptr->cmddata-1] << 8) | rtable[STBL][cptr->cmddata-1];
            }
            if (speed >= 0x8000)
            {
              speed = freqtbllo[cptr->lastnote + 1] | (freqtblhi[cptr->lastnote + 1] << 8);
              speed -= freqtbllo[cptr->lastnote] | (freqtblhi[cptr->lastnote] << 8);
              speed >>= rtable[STBL][cptr->cmddata-1];
            }
            cptr->freq -= speed;
          }
          break;

          case CMD_DONOTHING:
          if ((!cptr->cmddata) || (!cptr->vibdelay))
            break;
          if (cptr->vibdelay > 1)
          {
            cptr->vibdelay--;
            break;
          }
          case CMD_VIBRATO:
          {
            unsigned short speed = 0;
            unsigned char cmpvalue = 0;

            if (cptr->cmddata)
            {
              cmpvalue = ltable[STBL][cptr->cmddata-1];
              speed = rtable[STBL][cptr->cmddata-1];
            }
            if (cmpvalue >= 0x80)
            {
              cmpvalue &= 0x7f;
              speed = freqtbllo[cptr->lastnote + 1] | (freqtblhi[cptr->lastnote + 1] << 8);
              speed -= freqtbllo[cptr->lastnote] | (freqtblhi[cptr->lastnote] << 8);
              speed >>= rtable[STBL][cptr->cmddata-1];
            }

            if ((cptr->vibtime < 0x80) && (cptr->vibtime > cmpvalue))
              cptr->vibtime ^= 0xff;
            cptr->vibtime += 0x02;
            if (cptr->vibtime & 0x01)
              cptr->freq -= speed;
            else
              cptr->freq += speed;
          }
          break;

          case CMD_TONEPORTA:
          {
            unsigned short targetfreq = freqtbllo[cptr->note] | (freqtblhi[cptr->note] << 8);
            unsigned short speed = 0;

            if (!cptr->cmddata)
            {
              cptr->freq = targetfreq;
              cptr->vibtime = 0;
            }
            else
            {
              speed = (ltable[STBL][cptr->cmddata-1] << 8) | rtable[STBL][cptr->cmddata-1];
              if (speed >= 0x8000)
              {
                speed = freqtbllo[cptr->lastnote + 1] | (freqtblhi[cptr->lastnote + 1] << 8);
                speed -= freqtbllo[cptr->lastnote] | (freqtblhi[cptr->lastnote] << 8);
                speed >>= rtable[STBL][cptr->cmddata-1];
              }
              if (cptr->freq < targetfreq)
              {
                cptr->freq += speed;
                if (cptr->freq > targetfreq)
                {
                  cptr->freq = targetfreq;
                  cptr->vibtime = 0;
                }
              }
              if (cptr->freq > targetfreq)
              {
                cptr->freq -= speed;
                if (cptr->freq < targetfreq)
                {
                  cptr->freq = targetfreq;
                  cptr->vibtime = 0;
                }
              }
            }
          }
          break;
        }
      }

      PULSEEXEC:
      if (editorInfo.optimizepulse)
      {
        if ((songinit != PLAY_STOPPED) && (cptr->tick == cptr->gatetimer) && ((resomode <= 1) ||
          ((cptr->pattptr != 0x7fffffff) && (!((cptr->pattptr / 4) % resomode))))) goto GETNEWNOTES;
      }

      if (cptr->ptr[PTBL])
      {
        // Skip pulse when sequencer has been executed (RESO: on the row where
        // the last row of the song without it starts)
        if (editorInfo.optimizepulse)
        {
          if ((!cptr->tick) && (cptr->pattptr == ((resomode > 1) ? (unsigned)(pattlen[cptr->pattnum] - resomode + 1) * 4 : 0))) goto NEXTCHN;
        }

        // Pulsetable jump
        if (ltable[PTBL][cptr->ptr[PTBL]-1] == 0xff)
        {
          cptr->ptr[PTBL] = rtable[PTBL][cptr->ptr[PTBL]-1];
          if (!cptr->ptr[PTBL]) goto PULSEEXEC;
        }

        if (!cptr->pulsetime)
        {
          // Set pulse
          if (ltable[PTBL][cptr->ptr[PTBL]-1] >= 0x80)
          {
            cptr->pulse = (ltable[PTBL][cptr->ptr[PTBL]-1] & 0xf) << 8;
            cptr->pulse |= rtable[PTBL][cptr->ptr[PTBL]-1];
            cptr->ptr[PTBL]++;
          }
          else
          {
            cptr->pulsetime = ltable[PTBL][cptr->ptr[PTBL]-1];
          }
        }
        // Pulse modulation
        if (cptr->pulsetime)
        {
          unsigned char speed = rtable[PTBL][cptr->ptr[PTBL]-1];
          if (speed < 0x80)
          {
            cptr->pulse += speed;
            cptr->pulse &= 0xfff;
          }
          else
          {
            cptr->pulse += speed;
            cptr->pulse -= 0x100;
            cptr->pulse &= 0xfff;
          }
          cptr->pulsetime--;
          if (!cptr->pulsetime) cptr->ptr[PTBL]++;
        }
      }
      if ((songinit == PLAY_STOPPED) || (cptr->tick != cptr->gatetimer)) goto NEXTCHN;

      // New notes processing
      GETNEWNOTES:
      // After a pattern's last row there is no row until the next pattern is
      // taken on tick 0
      if (cptr->pattptr == 0x7fffffff) goto NEXTCHN;
      {
        unsigned char newnote;
        int row = cptr->pattptr / 4;
        int first, last;

        newnote = pattern[cptr->pattnum][cptr->pattptr];
        cptr->readpatt = cptr->pattnum;
        cptr->readrow = row;
        cptr->readtrans = cptr->trans;
        cptr->readvalid = 1;
        // A note played just before the row was read: it isn't played again
        if ((cptr->skipvalid) && (cptr->skippatt == cptr->pattnum) && (cptr->skiprow == row))
        {
          cptr->skipvalid = 0;
          if (newnote <= LASTNOTE) newnote = REST;
        }
        {
          int m = SDL_AtomicSet(&recskip[c], 0);

          if ((m & RECSKIP_VALID) && (((m >> 8) & 0xff) == cptr->pattnum) && ((m & 0xff) == row) && (newnote <= LASTNOTE))
            newnote = REST;
        }
        if (pattern[cptr->pattnum][cptr->pattptr+1])
          cptr->instr = pattern[cptr->pattnum][cptr->pattptr+1];
        cptr->newcommand = pattern[cptr->pattnum][cptr->pattptr+2];
        cptr->newcmddata = pattern[cptr->pattnum][cptr->pattptr+3];
        cptr->pattptr += 4;
        if (pattern[cptr->pattnum][cptr->pattptr] == ENDPATT)
          cptr->pattptr = 0x7fffffff;
        // LOOP HIGHLIGHT: after the last marked row the first one, in the
        // pattern the editor shows for this channel
        if ((playloop == PLAYLOOP_HIGHLIGHT) && (cptr->pattnum == editorUndoInfo.editorInfo[c].epnum) &&
          (playhighlightrows(&first, &last)) && (row == last) && (first < pattlen[cptr->pattnum]))
          cptr->pattptr = first * 4;

        if (newnote == KEYOFF)
          cptr->gate = 0xfe;
        if (newnote == KEYON)
          cptr->gate = 0xff;
        if (newnote <= LASTNOTE)
        {
          cptr->newnote = newnote+cptr->trans;
          if ((cptr->newcommand) != CMD_TONEPORTA)
          {
            if (!(instr[cptr->instr].gatetimer & 0x40))
            {
              cptr->gate = 0xfe;
              if (!(instr[cptr->instr].gatetimer & 0x80))
              {
                sidreg[0x5+7*c] = editorInfo.adparam>>8;
                sidreg[0x6+7*c] = editorInfo.adparam&0xff;
              }
            }
          }
        }
      }
      NEXTCHN:
      chnregs(c, cptr);
      cptr++;
    }
  }
}

// The channel's registers as a frame leaves them
static void chnregs(int c, CHN *cptr)
{
  if (cptr->mute)
    sidreg[0x4+7*c] = cptr->wave = 0x08;
  else
  {
    sidreg[0x0+7*c] = cptr->freq & 0xff;
    sidreg[0x1+7*c] = cptr->freq >> 8;
    sidreg[0x2+7*c] = cptr->pulse & 0xfe;
    sidreg[0x3+7*c] = cptr->pulse >> 8;
    sidreg[0x4+7*c] = cptr->wave & cptr->gate;
  }
}

// One frame of channel c, on its own clock (see playnow_run())
static void playchannel(int c)
{
  runchannels(1 << c);
}

/*
 * Notes played at once
 *
 * A note played from the keyboard or MIDI doesn't wait for the player's
 * next frame and then its hardrestart frames, as a pattern's note is
 * prepared (which made it sound 40-60 ms late, unevenly). The sound code
 * calls playnow_run() between the frames, and the note starts at the
 * sample its request came at:
 * - the hardrestart (gate off, hardrestart ADSR) is written at once, and
 *   the voice's envelope clock restarted (so that the release starts at
 *   once, as after enough hardrestart frames). It lasts until the envelope
 *   is down: none if the voice was silent, 7 ms at most at release rate 0
 * - then the note starts (its first frame: first wave, gate, ADSR) and the
 *   channel runs on its own frame clock from there. Its first frame (on
 *   which a pattern's note waits a whole frame) lasts as long as the
 *   attack: the wavetable starts with the envelope at its top, as after a
 *   whole frame, 2 ms after the start with attack 0
 * - while the song plays, the channel goes back to the player's frames
 *   before it starts its next row, so that the song stays in time
 */

// Cycles per envelope step of each attack rate
static const unsigned short attackcycles[16] =
  {9, 32, 63, 95, 149, 220, 267, 313, 392, 977, 1954, 3126, 3907, 11720, 19532, 31251};

// The channel's registers are written to the SID now
static void nowwrite(int c)
{
  chnregs(c, &chn[c]);
  sid_writevoice(c, editorInfo.adparam);
}

// The tick the channel's next frame comes to
static int nownexttick(CHN *cptr)
{
  int t = (cptr->tick - 1) & 0xff;

  if (t >= 0x80) t = (cptr->tempo >= 2) ? cptr->tempo : funktable[cptr->tempo & 1];
  return t;
}

// The channel goes on with the player's frames: those it has missed run now
// (if it is behind them)
static void nowtogrid(int c)
{
  CHN *cptr = &chn[c];
  int ran = 0;

  if (!cptr->nowstate) return;
  while ((songinit != PLAY_STOPPED) && (cptr->nowlead < 0))
  {
    playchannel(c);
    cptr->nowlead++;
    ran = 1;
  }
  if (ran) sid_writevoice(c, editorInfo.adparam);
  cptr->nowstate = 0;
}

// While the song plays: a note played on channel c takes the place of the
// note of the row about to start (read already, or read within JAMMINFRAMES
// frames), as the old way's note waited for that row
static void nowrowplace(int c)
{
  CHN *cptr = &chn[c];

  if ((cptr->tick >= 0x80) || (!cptr->tick)) return;
  if (cptr->tick <= cptr->gatetimer)
  {
    cptr->newnote = 0;
    return;
  }
  if ((cptr->tick <= cptr->gatetimer + JAMMINFRAMES) && (cptr->pattptr != 0x7fffffff))
  {
    unsigned char *row = &pattern[cptr->pattnum][cptr->pattptr];

    if ((row[0] <= LASTNOTE) && (row[2] != CMD_TONEPORTA))
    {
      cptr->skippatt = cptr->pattnum;
      cptr->skiprow = cptr->pattptr / 4;
      cptr->skipvalid = 1;
    }
  }
}

// A requested note: its hardrestart starts now (one going on goes on)
static void nowhardrestart(int c, int note, int ins)
{
  CHN *cptr = &chn[c];
  INSTR *iptr = &instr[ins];

  if (!cptr->nowstate) cptr->nowlead = 0;
  cptr->nownote = note;
  cptr->nowinstr = ins;
  cptr->nowrelease = 0;
  // An old way's note waiting is replaced
  cptr->jamnote = 0;
  cptr->jamtime = 0;
  if (cptr->nowstate == NOW_HR) return;
  cptr->nowstate = NOW_HR;
  nowcount[c] = 0.0;
  if (iptr->gatetimer & 0x40) return;
  cptr->gate = 0xfe;
  if (!(iptr->gatetimer & 0x80))
  {
    sidreg[0x5+7*c] = editorInfo.adparam>>8;
    sidreg[0x6+7*c] = editorInfo.adparam&0xff;
  }
  nowwrite(c);
  sid_restartenvelope(c);
}

// The hardrestart is over: the note starts, and the channel's own frames
static void nowstart(int c, double spf, double rate)
{
  CHN *cptr = &chn[c];
  INSTR *iptr = &instr[cptr->nowinstr];
  int clock = sid_clockrate();
  int env = sid_envelope(c);
  double first;

  if (songinit != PLAY_STOPPED) nowrowplace(c);
  else cptr->gatetimer = iptr->gatetimer & 0x3f;
  cptr->instr = cptr->nowinstr;
  cptr->newnote = cptr->nownote;
  startnewnote(c, cptr, iptr, 0);
  cptr->newnote = 0;
  nowwrite(c);
  sid_restartenvelope(c);
  cptr->nowstate = NOW_OWN;
  nowfirst[c] = 1;

  // The first frame lasts as long as the attack (from where the envelope
  // is), at most a frame; a legato note's (no hardrestart) only a moment
  if ((env < 0) || (env > 255)) env = 0;
  if ((iptr->gatetimer & 0x40) || (clock <= 0)) first = 1.0;
  else first = (double)attackcycles[iptr->ad >> 4] * (255 - env) * rate / clock;
  if (first > spf) first = spf;
  if (first < 1.0) first = 1.0;
  nowcount[c] = first;
}

// Let go now
static void nowrelease(int c)
{
  chn[c].gate = 0xfe;
  nowwrite(c);
}

int playnow_run(int maxsamples, double spf, double rate)
{
  int n = maxsamples;
  int c;

  if (n < 1) n = 1;
  if (spf < 1.0) spf = 1.0;
  for (c = 0; c < MAX_CHN; c++)
  {
    CHN *cptr = &chn[c];
    int mail = SDL_AtomicSet(&nowmail[c], 0);

    if (mail & NOWMAIL_NOTE) nowhardrestart(c, mail & 0xff, (mail >> 8) & 0xff);
    if (mail & NOWMAIL_RELEASE)
    {
      if (cptr->nowstate == NOW_HR) cptr->nowrelease = 1;
      else nowrelease(c);
    }

    if (cptr->nowstate == NOW_HR)
    {
      INSTR *iptr = &instr[cptr->nowinstr];

      if ((iptr->gatetimer & 0x40) || (!sid_envelope(c)) || (nowcount[c] >= NOWHRSECONDS * rate))
        nowstart(c, spf, rate);
      else
      {
        if (n > NOWPOLLSAMPLES) n = NOWPOLLSAMPLES;
        continue;
      }
    }

    // The channel's own frames due now. While the song plays it goes back to
    // the player's frames before a frame that starts a row (tick 0), so that
    // a pattern's note starts in time; but not before the note's first frame
    // after its start (its sound would wait). A row may be read up to a
    // frame early then: its hardrestart lasts that much longer
    while ((cptr->nowstate == NOW_OWN) && (nowcount[c] < 0.5))
    {
      if (cptr->nowrelease)
      {
        cptr->nowrelease = 0;
        cptr->gate = 0xfe;
      }
      if ((songinit != PLAY_STOPPED) && (!nowfirst[c]) && (!nownexttick(cptr)))
      {
        nowtogrid(c);
        break;
      }
      nowfirst[c] = 0;
      playchannel(c);
      sid_writevoice(c, editorInfo.adparam);
      if (songinit != PLAY_STOPPED) cptr->nowlead++;
      nowcount[c] += spf;
    }
    if (cptr->nowstate == NOW_OWN)
    {
      int k = (int)ceil(nowcount[c] - 0.5);
      if (k < n) n = k;
    }
  }
  if (n < 1) n = 1;
  return n;
}

void playnow_advance(int samples)
{
  int c;

  for (c = 0; c < MAX_CHN; c++)
  {
    if (chn[c].nowstate == NOW_HR) nowcount[c] += samples;
    else if (chn[c].nowstate == NOW_OWN) nowcount[c] -= samples;
  }
}

// Without the sound code starting notes at once: the notes asked for start
// the old way, on the player's frames
static void nowmailframes(void)
{
  int c;

  for (c = 0; c < MAX_CHN; c++)
  {
    int mail = SDL_AtomicSet(&nowmail[c], 0);

    if (chn[c].nowstate) nowtogrid(c);
    if (mail & NOWMAIL_NOTE) playnoteframes(mail & 0xff, (mail >> 8) & 0xff, c);
    if (mail & NOWMAIL_RELEASE) chn[c].gate = 0xfe;
  }
}

void sequencer(int c, CHN *cptr)
{
  if ((songinit != PLAY_STOPPED) && (cptr->pattptr == 0x7fffffff))
  {
    cptr->pattptr = startpattpos * 4;
    if (!cptr->advance) goto SEQDONE;
    // LOOP: after the loop's last pattern the sequencer goes back to its
    // state before the first one. A loop set while a pattern plays starts
    // with that pattern
    if (playloop > 0)
    {
      if (!cptr->looping)
      {
        cptr->looping = 1;
        if (cptr->lastvalid)
        {
          cptr->loopsongptr = cptr->lastsongptr;
          cptr->looptrans = cptr->lasttrans;
          cptr->looprepeat = cptr->lastrepeat;
          cptr->loopcount = 1;
        }
        else
        {
          cptr->loopsongptr = cptr->songptr;
          cptr->looptrans = cptr->trans;
          cptr->looprepeat = cptr->repeat;
          cptr->loopcount = 0;
        }
      }
      if (cptr->loopcount >= playloop)
      {
        cptr->songptr = cptr->loopsongptr;
        cptr->trans = cptr->looptrans;
        cptr->repeat = cptr->looprepeat;
        cptr->loopcount = 0;
      }
      cptr->loopcount++;
    }
    else cptr->looping = 0;
    cptr->lastsongptr = cptr->songptr;
    cptr->lasttrans = cptr->trans;
    cptr->lastrepeat = cptr->repeat;
    cptr->lastvalid = 1;
    // Song loop
    if (songorder[psnum][c][cptr->songptr] == LOOPSONG)
    {
      cptr->songptr = songorder[psnum][c][cptr->songptr+1];
      if (cptr->songptr >= songlen[psnum][c])
      {
        stopsong();
        cptr->songptr = 0;
        goto SEQDONE;
      }
    }
    // Transpose
    if ((songorder[psnum][c][cptr->songptr] >= TRANSDOWN) && (songorder[psnum][c][cptr->songptr] < LOOPSONG))
    {
      cptr->trans = songorder[psnum][c][cptr->songptr]-TRANSUP;
      cptr->songptr++;
    }
    // Repeat
    if ((songorder[psnum][c][cptr->songptr] >= REPEAT) && (songorder[psnum][c][cptr->songptr] < TRANSDOWN))
    {
      cptr->repeat = songorder[psnum][c][cptr->songptr]-REPEAT;
      cptr->songptr++;
    }
    // Pattern number
    cptr->pattnum = songorder[psnum][c][cptr->songptr];
    if (cptr->repeat)
      cptr->repeat--;
    else
      cptr->songptr++;

    // Check for illegal pattern now
    if (cptr->pattnum >= MAX_PATT)
    {
      stopsong();
      cptr->pattnum = 0;
    }
    if (cptr->pattptr >= (pattlen[cptr->pattnum] * 4))
      cptr->pattptr = 0;
      
    // Check for playback endpos
    if ((lastsonginit != PLAY_BEGINNING) && (editorUndoInfo.editorInfo[c].esend > 0) && (editorUndoInfo.editorInfo[c].esend > editorUndoInfo.editorInfo[c].espos) && (cptr->songptr > editorUndoInfo.editorInfo[c].esend) && (editorUndoInfo.editorInfo[c].espos < songlen[psnum][c]))
      cptr->songptr = editorUndoInfo.editorInfo[c].espos;
  }
  SEQDONE: {}
}

int playnextpattern(int c, int *patt, int *trans)
{
  const unsigned char *ol;
  int sp, tr;

  if ((c < 0) || (c >= MAX_CHN) || (playloop)) return 0;
  ol = songorder[psnum][c];
  sp = chn[c].songptr;
  tr = chn[c].trans;
  if (sp >= MAX_SONGLEN) return 0;
  if (ol[sp] == LOOPSONG)
  {
    sp = ol[sp + 1];
    if (sp >= songlen[psnum][c]) return 0;
  }
  if ((ol[sp] >= TRANSDOWN) && (ol[sp] < LOOPSONG))
  {
    tr = ol[sp] - TRANSUP;
    sp++;
  }
  if ((ol[sp] >= REPEAT) && (ol[sp] < TRANSDOWN)) sp++;
  if (ol[sp] >= MAX_PATT) return 0;
  *patt = ol[sp];
  *trans = tr;
  return 1;
}

int playrecordrow(int c, double framefraction, int *patt, int *row, int *trans, int *skip)
{
  CHN *cp;
  double pos;

  *skip = 0;
  if ((c < 0) || (c >= MAX_CHN) || (songinit == PLAY_STOPPED)) return 0;
  cp = &chn[c];
  if (!cp->playvalid) return 0;
  pos = (cp->rowframe + framefraction) / (cp->rowlen ? cp->rowlen : 1);
  if (pos < 0.5)
  {
    *patt = cp->playpatt;
    *row = cp->playrow;
    *trans = cp->playtrans;
    return 1;
  }
  // The next row: read already (it isn't played again this time), or later
  if (cp->readvalid)
  {
    *patt = cp->readpatt;
    *row = cp->readrow;
    *trans = cp->readtrans;
    return 2;
  }
  // The row the player reads next. (On a pattern's last row the next
  // pattern has been taken from the orderlist already, at that row's tick 0)
  if ((cp->pattptr != 0x7fffffff) && (cp->pattnum < MAX_PATT) && (cp->pattptr / 4 < (unsigned)pattlen[cp->pattnum]))
  {
    *patt = cp->pattnum;
    *row = cp->pattptr / 4;
    *trans = cp->trans;
    *skip = 1;
    return 2;
  }
  if (playnextpattern(c, patt, trans))
  {
    *row = 0;
    *skip = 1;
    return 2;
  }
  // Unknown: the row playing
  *patt = cp->playpatt;
  *row = cp->playrow;
  *trans = cp->playtrans;
  return 1;
}

void playrecordskip(int c, int patt, int row)
{
  if ((c < 0) || (c >= MAX_CHN) || (patt < 0) || (patt >= MAX_PATT) || (row < 0) || (row > 0xff)) return;
  SDL_AtomicSet(&recskip[c], RECSKIP_VALID | (patt << 8) | row);
}
