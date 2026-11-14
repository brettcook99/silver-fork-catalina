/*
 * GoatTracker plugin build: audio, following the host's transport, MIDI from
 * the host
 *
 * gtcore_process() runs on the host's audio thread. It does what the SDL
 * audio callback (snd_mixdata()) does in the standalone program: it calls
 * the player once per frame and renders the SID in between. The frames come
 * at 50 / 60 Hz (times the multiplier), or, while following the host
 * (DAW SYNC), at the rate that makes one pattern row last 1 / stepsize of
 * the host's beat: at the song's own tempo that is the same rate.
 *
 * Following the host's position: beat 0 (the song's start bar) is the first
 * row's tick 0. When the host starts, jumps or loops, the player is "chased"
 * there: it is reset and run without sound up to that position.
 */

#include "goattrk2.h"
#include "gui/gui.h"
#include "gtcore_api.h"
#include "gtplugin.h"

extern SDL_mutex *gtplugin_enginemutex;
extern int songinit;
extern int psnum;
extern int followsuspended;

int gtsync_enabled = 1;
int gtsync_startbar = 1;
int gtsync_route = 1;
int gtsync_recordoff = 1;

static double samplerate = 44100.0;
static int sidready = 0;
static unsigned cur_model = 1, cur_ntsc = 0, cur_multiplier = 1, cur_interpolate = 3, cur_clock = 0;
static int soundparamsset = 0;

// Free running frames: samples until the next one
static double framecount = 0.0;

// Host transport
static volatile double hostbpm = 0.0;
static volatile int hostplaying = 0;
static volatile int following = 0;
static SDL_threadID uithread = 0;
static double songbeat = 0.0;     // beat of the frame run last
static double nextbeat = 0.0;     // beat of the next frame
static int waitstart = 0;         // the song starts at nextbeat
static double expectedppq = 0.0;
static int recordpass = 0;        // the next recorded note starts a pass
static volatile int resync = 0;   // chase again (a new song)
static int wasplaying = 0;

// Notes sounding from the routed MIDI channels
static int routednote[MAX_CHN];

#define RENDERCHUNK 1024
static short renderbuf[RENDERCHUNK];
static short scratchbuf[RENDERCHUNK];

/*
 * Recorded notes: audio thread -> GoatTracker thread
 */

#define RECORDQUEUESIZE 256
static GTRECORDNOTE recordqueue[RECORDQUEUESIZE];
static volatile int recordread = 0;
static volatile int recordwrite = 0;

static void recordpush(const GTRECORDNOTE *rn)
{
  int next = (recordwrite + 1) % RECORDQUEUESIZE;

  if (next == recordread) return;
  recordqueue[recordwrite] = *rn;
  SDL_MemoryBarrierRelease();
  recordwrite = next;
}

int gtrecord_pop(GTRECORDNOTE *rn)
{
  if (recordread == recordwrite) return 0;
  SDL_MemoryBarrierAcquire();
  *rn = recordqueue[recordread];
  SDL_MemoryBarrierRelease();
  recordread = (recordread + 1) % RECORDQUEUESIZE;
  return 1;
}

/*
 * State for the user interface
 */

double gtsync_hostbpm(void)
{
  return hostbpm;
}

int gtsync_hostplaying(void)
{
  return hostplaying;
}

int gtsync_following(void)
{
  return following;
}

void gtplugin_setuithread(void)
{
  uithread = SDL_ThreadID();
}

void gtsync_requestresync(void)
{
  resync = 1;
}

int gtsync_uiblocked(void)
{
  return (following) && (uithread) && (SDL_ThreadID() == uithread);
}

/*
 * SID setup
 */

static void initsid(void)
{
  sid_init((int)(samplerate + 0.5), cur_model, cur_ntsc, cur_interpolate & 1, cur_clock, cur_interpolate >> 1);
  sid_resetchip();
  sidready = 1;
  framecount = 0.0;
}

int gtplugin_soundinit(unsigned model, unsigned ntsc, unsigned multiplier, unsigned interpolate, unsigned customclockrate)
{
  gtplugin_lockengine();
  cur_model = model;
  cur_ntsc = ntsc;
  cur_multiplier = multiplier;
  cur_interpolate = interpolate;
  cur_clock = customclockrate;
  soundparamsset = 1;
  initsid();
  gtplugin_unlockengine();
  return 1;
}

void gtplugin_sounduninit(void)
{
}

GTCORE_EXPORT void gtcore_prepare(double rate, int maxblock)
{
  (void)maxblock;
  if (rate < 8000.0) rate = 8000.0;
  if (rate > 384000.0) rate = 384000.0;
  if (!gtplugin_enginemutex) return;
  gtplugin_lockengine();
  samplerate = rate;
  if (soundparamsset) initsid();
  gtplugin_unlockengine();
}

/*
 * Frame timing
 */

// Frames per second without the host: 50 / 60 times the multiplier
static double ownframerate(void)
{
  double fps = editorInfo.ntsc ? 60.0 : 50.0;

  if (editorInfo.multiplier) fps *= editorInfo.multiplier;
  else fps /= 2.0;
  return fps;
}

// The beats one player frame lasts while following the host
static double framebeats(void)
{
  double fpr = playframesperrow();
  int rows = stepsize > 0 ? stepsize : 4;

  if (fpr < 1.0) fpr = 1.0;
  return 1.0 / (rows * fpr);
}

// Frames per second now
static double framerate(int sync)
{
  if (sync) return hostbpm / 60.0 / framebeats();
  return ownframerate();
}

// The beat (quarter notes from the host's start) where the song starts
static double startoffset(const gtcore_transport *tp)
{
  double beatsperbar = 4.0;

  if ((tp->timesignum > 0) && (tp->timesigden > 0)) beatsperbar = tp->timesignum * 4.0 / tp->timesigden;
  return (gtsync_startbar - 1) * beatsperbar;
}

// Test aid: with GTCORE_REGLOG=file every frame heard is logged (its beat
// position and the SID registers)
static FILE *reglog = NULL;
static int reglogchecked = 0;

static void logframe(void)
{
  int c;

  if (!reglogchecked)
  {
    const char *path = getenv("GTCORE_REGLOG");
    reglogchecked = 1;
    if ((path) && (path[0])) reglog = fopen(path, "wt");
  }
  if (!reglog) return;
  fprintf(reglog, "%s%.6f", following ? "F " : "  ", songbeat);
  for (c = 0; c < NUMSIDREGS; c++) fprintf(reglog, " %02x", sidreg[c]);
  fprintf(reglog, " | t%d s%d r%d", chn[0].tempo, stepsize, chn[0].playrow);
  fputc(10, reglog);
}

// One player frame
static void playframe(void)
{
  playroutine();
  sid_newframe();
}

// Starts following the host: the player goes to the song position of the
// host's position target (beats from the song's start)
static void chase(double target)
{
  double fb, beat;
  int startframes;
  int warmframes = 10;
  int c;

  playerreset();
  sid_resetchip();
  for (c = 0; c < MAX_CHN; c++) routednote[c] = 0;
  recordpass = 1;

  // The song's first frame comes this many frames before its first row's
  // tick 0. The tempo it starts with is known after that frame
  startframes = playstartframes();
  playerstartsong(editorInfo.esnum);
  playroutine();
  fb = framebeats();
  beat = -startframes * fb;
  if (target < beat - 1e-9)
  {
    // Not started yet: frames go on meanwhile (for notes played) so that one
    // comes exactly at the start
    double fbw;
    double k;

    playerreset();
    fbw = framebeats();
    k = floor((beat - target) / fbw);
    waitstart = 1;
    nextbeat = beat - k * fbw;
    songbeat = nextbeat - fbw;
    return;
  }

  // Run the song without sound up to the position
  waitstart = 0;
  followplay = followmode;
  followsuspended = 0;
  for (;;)
  {
    fb = framebeats();
    if (beat + fb > target + 1e-9) break;
    beat += fb;
    playroutine();
    // The last frames are heard by the SID (not by the listener), so that
    // the envelopes and the filter are where they would be
    if ((target - beat) / fb < warmframes)
    {
      int samples = (int)(samplerate / framerate(1));
      sid_newframe();
      while (samples > 0)
      {
        int n = samples > RENDERCHUNK ? RENDERCHUNK : samples;
        sid_render(scratchbuf, n, editorInfo.adparam);
        samples -= n;
      }
    }
  }
  sid_newframe();
  songbeat = beat;
  nextbeat = beat + framebeats();
}

/*
 * MIDI from the host
 */

// The row of channel c that a note played now goes to: the row playing, or
// the next one if more than half of this one has gone. Returns 0 if not
// known
static int recordrow(int c, double framefraction, int *patt, int *row, int *trans, int *skip)
{
  CHN *cp = &chn[c];
  double pos;

  *skip = 0;
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
    return 1;
  }
  if (cp->playrow + 1 < pattlen[cp->playpatt])
  {
    *patt = cp->playpatt;
    *row = cp->playrow + 1;
    *trans = cp->playtrans;
    *skip = 1;
    return 1;
  }
  if (playnextpattern(c, patt, trans))
  {
    *row = 0;
    *skip = 1;
    return 1;
  }
  // Unknown: the row playing
  *patt = cp->playpatt;
  *row = cp->playrow;
  *trans = cp->playtrans;
  return 1;
}

static void recordnote(int c, int note, double framefraction)
{
  GTRECORDNOTE rn;
  int patt, row, trans, skip;

  if (!recordrow(c, framefraction, &patt, &row, &trans, &skip)) return;
  memset(&rn, 0, sizeof rn);
  rn.voice = c;
  rn.patt = patt;
  rn.row = row;
  if (note == KEYOFF)
    rn.note = KEYOFF;
  else
  {
    int n = note - (signed char)trans;
    if (n < FIRSTNOTE) n = FIRSTNOTE;
    if (n > LASTNOTE) n = LASTNOTE;
    rn.note = n;
    rn.instr = editorInfo.einum;
  }
  rn.newpass = recordpass;
  recordpass = 0;
  // The note sounds already: the player doesn't play it again
  if ((skip) && (note != KEYOFF))
  {
    chn[c].skippatt = (unsigned char)patt;
    chn[c].skiprow = (unsigned char)row;
    chn[c].skipvalid = 1;
  }
  recordpush(&rn);
}

static void handlemidi(const gtcore_midi *m, double framefraction)
{
  int type = m->status & 0xf0;
  int ch = m->status & 0x0f;
  int ev;

  if (type == 0x90) ev = m->data2 ? MIDI_NOTEON : MIDI_NOTEOFF;
  else if (type == 0x80) ev = MIDI_NOTEOFF;
  else if (type == 0xb0) ev = MIDI_CONTROL;
  else return;

  // MIDI channels 1-3 play (and record) voices 1-3 here, in time with the
  // player. In edit mode without playback they are entered at the cursor
  // like notes from a MIDI keyboard; so they are in the SID designer
  if ((gtsync_route) && (ch < MAX_CHN) && (ev != MIDI_CONTROL) && (!gui_designeractive()))
  {
    int playing = (songinit != PLAY_STOPPED);
    int note = FIRSTNOTE + m->data1 - 12;

    if ((!recordmode) || (playing))
    {
      if ((note < FIRSTNOTE) || (note > LASTNOTE)) return;
      if (ev == MIDI_NOTEON)
      {
        playtestnote(note, editorInfo.einum, ch);
        routednote[ch] = note;
        if ((recordmode) && (playing)) recordnote(ch, note, framefraction);
      }
      else if (routednote[ch] == note)
      {
        releasenote(ch);
        routednote[ch] = 0;
        if ((recordmode) && (playing) && (gtsync_recordoff)) recordnote(ch, KEYOFF, framefraction);
      }
      return;
    }
  }
  midi_pushevent_ch(ev, ch, m->data1, m->data2);
}

/*
 * Rendering
 */

static void render(float *left, float *right, int samples)
{
  while (samples > 0)
  {
    int n = samples > RENDERCHUNK ? RENDERCHUNK : samples;
    int c;

    memset(renderbuf, 0, n * sizeof(short));
    sid_render(renderbuf, n, editorInfo.adparam);
    for (c = 0; c < n; c++)
    {
      float v = renderbuf[c] * (1.0f / 32768.0f);
      left[c] = v;
      right[c] = v;
    }
    left += n;
    right += n;
    samples -= n;
  }
}

GTCORE_EXPORT void gtcore_process(float *left, float *right, int samples, const gtcore_transport *tp, const gtcore_midi *midi, int nmidi)
{
  gtcore_transport none;
  int sync;
  int pos = 0;
  int m = 0;
  double samplesperbeat = 0.0;
  double offset;
  double ppqbase;
  int playing;

  if ((!gtplugin_enginemutex) || (SDL_TryLockMutex(gtplugin_enginemutex) != 0))
  {
    memset(left, 0, samples * sizeof(float));
    memset(right, 0, samples * sizeof(float));
    return;
  }
  if (!sidready)
  {
    SDL_UnlockMutex(gtplugin_enginemutex);
    memset(left, 0, samples * sizeof(float));
    memset(right, 0, samples * sizeof(float));
    return;
  }
  if (!tp)
  {
    memset(&none, 0, sizeof none);
    tp = &none;
  }

  hostbpm = ((tp->valid) && (tp->bpm > 1.0)) ? tp->bpm : 0.0;
  hostplaying = (tp->valid) && (tp->playing);
  sync = (gtsync_enabled) && (hostbpm > 0.0);
  if (sync) samplesperbeat = samplerate * 60.0 / hostbpm;
  offset = startoffset(tp);
  ppqbase = tp->ppq;

  // A recording pass starts with each start of playback
  playing = (songinit != PLAY_STOPPED);
  if ((playing) && (!wasplaying)) recordpass = 1;
  wasplaying = playing;

  // Following the host's transport
  if ((sync) && (hostplaying))
  {
    double tol = 2.0 * framebeats();
    if ((!following) || (resync) || (fabs(tp->ppq - expectedppq) > tol))
    {
      following = 1;
      resync = 0;
      chase(tp->ppq - offset);
    }
  }
  else if (following)
  {
    // The host stopped
    following = 0;
    waitstart = 0;
    songinit = PLAY_STOP;
    framecount = 0.0;
  }

  while (pos < samples)
  {
    int n = samples - pos;

    // MIDI events at this sample
    while ((m < nmidi) && (midi[m].offset <= pos))
    {
      double fraction;
      if (following)
      {
        double ppq = ppqbase + pos / samplesperbeat - offset;
        fraction = (ppq - songbeat) / (nextbeat - songbeat);
      }
      else
      {
        double spf = samplerate / framerate(sync);
        fraction = 1.0 - framecount / spf;
      }
      if (fraction < 0.0) fraction = 0.0;
      if (fraction > 1.0) fraction = 1.0;
      handlemidi(&midi[m], fraction);
      m++;
    }
    if (m < nmidi)
    {
      int next = midi[m].offset - pos;
      if (next < n) n = next;
    }

    if (following)
    {
      double ppq = ppqbase + pos / samplesperbeat;
      double tosamples;

      // The host's loop ends within the block: back to its start
      if ((tp->looping) && (tp->loopend > tp->loopstart) && (ppq >= tp->loopend - 1e-9))
      {
        ppq = tp->loopstart + (ppq - tp->loopend);
        ppqbase = ppq - pos / samplesperbeat;
        chase(ppq - offset);
      }
      tosamples = (nextbeat - (ppq - offset)) * samplesperbeat;
      if (tosamples < 0.5)
      {
        // The next frame is due
        if (waitstart)
        {
          if (nextbeat >= -playstartframes() * framebeats() - 1e-9)
          {
            waitstart = 0;
            playerstartsong(editorInfo.esnum);
            followplay = followmode;
            followsuspended = 0;
          }
        }
        playframe();
        songbeat = nextbeat;
        nextbeat += framebeats();
        logframe();
        continue;
      }
      if (tosamples < n) n = (int)ceil(tosamples - 0.5);
      if ((tp->looping) && (tp->loopend > tp->loopstart))
      {
        double toloop = (tp->loopend - ppq) * samplesperbeat;
        if ((toloop > 0.0) && (toloop < n)) n = (int)ceil(toloop);
      }
      if (n < 1) n = 1;
    }
    else
    {
      if (framecount < 0.5)
      {
        playframe();
        framecount += samplerate / framerate(sync);
        continue;
      }
      if (framecount < n) n = (int)ceil(framecount - 0.5);
      if (n < 1) n = 1;
      framecount -= n;
    }

    render(left + pos, right + pos, n);
    pos += n;
  }

  // The rest of the MIDI events (offsets past the block)
  while (m < nmidi)
  {
    handlemidi(&midi[m], 1.0);
    m++;
  }

  if (following) expectedppq = ppqbase + samples / samplesperbeat;
  SDL_UnlockMutex(gtplugin_enginemutex);
}
