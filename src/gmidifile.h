#ifndef GMIDIFILE_H
#define GMIDIFILE_H

// Standard MIDI file (.mid) -> GoatTracker song (.sng) conversion.
// Used by the tracker (File > Import MIDI) and the midi2sng tool.
//
// The notes are put on a grid of rows (rowsperbeat rows per quarter note)
// and go to the 3 SID channels in one of two ways:
// - shared out (split 0): when more than 3 notes play at once, the highest
//   and lowest new notes take over a channel from an older held note, other
//   notes are left out. With voicemask only the SID channels in it get
//   notes, each new note the one that has been free the longest (so that
//   the notes go to them in turn and ring on), whatever its MIDI channel;
//   with one channel in it, one note at a time.
// - split (split 1): MIDI channel 1 to SID channel 1, 2 to 2, 3 to 3, the
//   other MIDI channels are left out. One note at a time on each: a new note
//   cuts the one before; of notes starting together the highest plays.
// Every MIDI channel gets an instrument of its own. The MIDI tempo becomes
// the song tempo (with tempo changes as F commands on the first SID channel
// that has notes).
//
// When all the notes are on one MIDI channel but in several tracks of the
// file (as many sequencers write them), the tracks are read as MIDI
// channels 1, 2, 3...

#include "gsngwrite.h"

typedef struct
{
  int rowsperbeat;        // Pattern rows per quarter note (default 4)
  int patternlength;      // Rows per pattern, 16-128 (default 64)
  int transpose;          // Halfsteps added to every note (default 0)
  int drums;              // 1 = include MIDI channel 10 (drums) (default 0)
  unsigned channelmask;   // Bit n = use MIDI channel n+1, 0 = all (default 0)
  int ntsc;               // 1 = tempo for 60Hz (NTSC) playback (default 0)
  int split;              // 1 = MIDI channel n to SID channel n (default 0)
  unsigned voicemask;     // Shared out: bit n = SID channel n+1 gets notes,
                          // 0 = all 3 the usual way (default 0)
  int notempo;            // 1 = no tempo commands: the tracks are put into a
                          // song whose tempo stays (default 0)
} MIDI2SNG_OPTIONS;

// What a MIDI file has in it (for the import dialog)
typedef struct
{
  int notes[16];          // Number of notes on each MIDI channel
  int bytracks;           // 1 = the file's tracks are read as the channels
  char name[MAX_STR];     // The sequence name, "" if none
} MIDISCAN;

void midi2sng_defaults(MIDI2SNG_OPTIONS *opt);

// Converts midiname to sngname. Returns 1 on success, 0 on failure. report
// (if not NULL) gets a short text about the result or the error
int midi2sng(const char *midiname, const char *sngname, const MIDI2SNG_OPTIONS *opt, char *report, int reportsize);

// The same, the song in memory: one subtune, its 3 orderlists the same
// length. NULL on failure. Free it with free()
SNGDATA *midi2sngdata(const char *midiname, const MIDI2SNG_OPTIONS *opt, char *report, int reportsize);

// Reads the file and counts its notes. Returns 0 (with the error in report)
// if it can't be read
int midi2sng_scan(const char *midiname, MIDISCAN *scan, char *report, int reportsize);

#endif
