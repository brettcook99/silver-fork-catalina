#ifndef GMIDIFILE_H
#define GMIDIFILE_H

// Standard MIDI file (.mid) -> GoatTracker song (.sng) conversion.
// Used by the tracker (File > Import MIDI) and the midi2sng tool.
//
// The notes are put on a grid of rows (rowsperbeat rows per quarter note)
// and shared out to the 3 SID channels. When more than 3 notes play at
// once, the highest and lowest new notes take over a channel from an older
// held note, other notes are left out. Every MIDI channel gets an
// instrument of its own. The MIDI tempo becomes the song tempo (with tempo
// changes as F commands on channel 1).

typedef struct
{
  int rowsperbeat;        // Pattern rows per quarter note (default 4)
  int patternlength;      // Rows per pattern, 16-128 (default 64)
  int transpose;          // Halfsteps added to every note (default 0)
  int drums;              // 1 = include MIDI channel 10 (drums) (default 0)
  unsigned channelmask;   // Bit n = use MIDI channel n+1, 0 = all (default 0)
  int ntsc;               // 1 = tempo for 60Hz (NTSC) playback (default 0)
} MIDI2SNG_OPTIONS;

void midi2sng_defaults(MIDI2SNG_OPTIONS *opt);

// Converts midiname to sngname. Returns 1 on success, 0 on failure. report
// (if not NULL) gets a short text about the result or the error
int midi2sng(const char *midiname, const char *sngname, const MIDI2SNG_OPTIONS *opt, char *report, int reportsize);

#endif
