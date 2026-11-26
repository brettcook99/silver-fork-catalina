#ifndef GPLAY_H
#define GPLAY_H

#define PLAY_PLAYING 0x00
#define PLAY_BEGINNING 0x01
#define PLAY_POS 0x02
#define PLAY_PATTERN 0x03
#define PLAY_STOP 0x04
#define PLAY_STOPPED 0x80

typedef struct
{
	int espos;
	int esend;
	int epnum;
}CHN_EDITOR_INFO;

typedef struct
{
	CHN_EDITOR_INFO editorInfo[MAX_CHN];
}EDITOR_UNDO_INFO;


typedef struct
{
  unsigned char trans;
  unsigned char instr;
  unsigned char note;
  unsigned char lastnote;
  unsigned char newnote;
  unsigned pattptr;
  unsigned char pattnum;
  unsigned char songptr;
  unsigned char repeat;
  unsigned short freq;
  unsigned char gate;
  unsigned char wave;
  unsigned short pulse;
  unsigned char ptr[2];
  unsigned char pulsetime;
  unsigned char wavetime;
  unsigned char vibtime;
  unsigned char vibdelay;
  unsigned char command;
  unsigned char cmddata;
  unsigned char newcommand;
  unsigned char newcmddata;
  unsigned char tick;
  unsigned char tempo;
  unsigned char mute;
  unsigned char advance;
  unsigned char gatetimer;
  // A note played from the keyboard or MIDI while the song plays: it starts
  // when jamtime counts down to 0 (after the hardrestart frames), whatever
  // the row timing of the channel
  unsigned char jamnote;
  unsigned char jaminstr;
  unsigned char jamtime;
  // LOOP (playloop): looping, the patterns read since the loop's first one,
  // and the sequencer's state (orderlist position, transpose, repeat) before
  // that one was read. The state before the last pattern read, for a loop
  // started while it plays
  unsigned char looping;
  unsigned char loopcount;
  unsigned char loopsongptr, looptrans, looprepeat;
  unsigned char lastvalid;
  unsigned char lastsongptr, lasttrans, lastrepeat;
  // Where the channel plays, for notes recorded from a host's MIDI tracks
  // (plugin): the row read last (readvalid: it starts on the next tick 0),
  // the row whose tick 0 came last, the frames since that tick 0 and the
  // row's length in frames. A row whose note was recorded (and played)
  // just before the row was read: its note isn't played again (skip)
  unsigned char readpatt, readrow, readvalid, readtrans;
  unsigned char playpatt, playrow, playvalid, playtrans;
  unsigned char rowframe, rowlen;
  unsigned char skippatt, skiprow, skipvalid;
  // A note played at once (see playnow_run()): its state (in its
  // hardrestart, or playing with the channel on its own frame clock), note,
  // instrument, and a release that came before it started. How many frames
  // the channel's own clock has run ahead of the player's while the song
  // plays (behind if negative)
  unsigned char nowstate, nownote, nowinstr, nowrelease;
  int nowlead;
} CHN;

#ifndef GPLAY_C
extern EDITOR_UNDO_INFO editorUndoInfo;
extern CHN chn[MAX_CHN];
extern unsigned char masterfader;
extern unsigned char freqtbllo[];
extern unsigned char freqtblhi[];
extern int lastsonginit;
#endif
// SPACE / numpad 0 started the playback (see playstopreturn())
extern int playreturnpoint;
// Starts of playback so far
extern unsigned playstarts;

void initchannels(void);
void initsong(int num, int playmode);
void initsongpos(int num, int playmode, int pattpos);
void stopsong(void);
void rewindsong(void);
void playtestnote(int note, int ins, int chnnum);
void releasenote(int chnnum);
// A note that starts on the channel's next row (tick 0) while the song
// plays, after the hardrestart frames as a pattern's note
void playtestnoterow(int note, int ins, int chnnum);

// Notes played from the keyboard or MIDI (playtestnote(), releasenote())
// start and stop at once, at the sample they come at, when the sound code
// (an emulated SID) sets playnowon and between the player's frames does
//   n = playnow_run(n, samplesperframe, samplerate);
//   (render n samples)
//   playnow_advance(n);
// playnow_run() does what is due now and makes n no longer than the time
// to the next thing due. Otherwise (HardSID, CatWeasel) they start on the
// player's frames after the hardrestart frames
extern int playnowon;
int playnow_run(int maxsamples, double spf, double rate);
void playnow_advance(int samples);
void mutechannel(int chnnum);
int isplaying(void);
int playstarting(void);
// The filtertable row the player runs next (1-based, 0 = stopped)
int playfilterptr(void);
void playroutine(void);

// Runs subtune num from its start without sound (MIDI export), until every
// channel has come to the end of its orderlist, the song stops or maxframes
// frames have run. hook is called for each note started (note 0-95 from
// C-0, transposed, as the player plays it; tone portamento too) and each
// keyoff read (note -1), with the frame number. loopframe (3 values) gets
// the frame at which each channel reached its end (-1 = not reached).
// After each frame hook is called with c = -1 and note = 1 if a row of
// channel 1 started in that frame (its tick 0), else 0. The playback and
// the player's state are as before afterwards. Returns the frames run
unsigned playoffline(int num, unsigned maxframes, void (*hook)(int c, int note, unsigned frame, void *user), void *user, int *loopframe);
// While playoffline() runs: the frame at which channel c came to the end of
// its orderlist, -1 if it hasn't
int playofflineloop(int c);

// LOOP on the retro screen: while the song plays, each channel plays its
// next playloop patterns again and again, from the one playing when the
// loop was set (or the first one, set before playing). 0 = no loop.
// PLAYLOOP_HIGHLIGHT: the rows marked (highlighted) in the pattern editor
// play again and again: each channel that plays the pattern the editor shows
// for it goes back to the first marked row after the last one
extern int playloop;
#define PLAYLOOP_HIGHLIGHT -1
// The rows LOOP HIGHLIGHT loops (first, last), 0 if no rows are marked
int playhighlightrows(int *first, int *last);
// While the song plays, channel c goes to orderlist position n (in the
// player's next frame), at the same row: playjump(c, n)
void playjump(int c, int pos);

// For following a host's tempo and position (plugin builds):
// Player frames per row of channel 1 (the average of the two values with
// funktempo)
double playframesperrow(void);
// Frames from the start of playback to the first row's tick 0
int playstartframes(void);
// Stops and clears the player and the SID registers (the channels' mutes
// stay)
void playerreset(void);
// Starts the song (subtune num) from its beginning on the next frame, like
// initsong(), also while the user interface's play commands are blocked
void playerstartsong(int num);
// The pattern and transpose a channel plays after its current pattern, from
// the orderlist. Returns 0 if not known
int playnextpattern(int c, int *patt, int *trans);

// Where a note played now on channel c is recorded while the song plays:
// the row playing if the note comes in its first half, else the next row
// (framefraction: how far the current frame has gone, 0-1). skip = the next
// row hasn't been read yet: it mustn't play the note again (the note sounds
// already), see playrecordskip(). Returns 0 if not known (no row started
// yet), 1 = the row playing, 2 = the next row
int playrecordrow(int c, double framefraction, int *patt, int *row, int *trans, int *skip);
// Asks the player (from any thread) not to play the note of the given row
// of channel c, if that is the row it reads next
void playrecordskip(int c, int patt, int row);

#endif
