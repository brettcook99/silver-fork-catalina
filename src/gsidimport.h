#ifndef GSIDIMPORT_H
#define GSIDIMPORT_H

// C64 music file (.sid, PSID/RSID) -> GoatTracker song (.sng) conversion.
// Used by the tracker (import) and the sid2sng tool. Plain C, no UI.
//
// Two ways to convert:
//
// EXACT  For SIDs made with GoatTracker 2's packer/relocator. The player
//        and the music data are found in the C64 binary and turned back
//        into a song: all subtunes, orderlists, patterns, instruments and
//        tables. What the packer leaves out can't come back: instrument
//        names, unused patterns/instruments/table rows, and the original
//        numbering (instruments come in hardrestart/no-HR/legato order).
//
// APPROX For any SID. Each subtune (all of them, up to the 32 a song can
//        have, or the one chosen) is played on an emulated C64 for a while
//        and the SID registers are recorded every call of its player: a
//        PSID's play routine; an RSID runs from its own interrupts (CIA
//        timers, VIC-II raster interrupt, the KERNAL's interrupt entry &
//        exit emulated) and the one that plays the music is recorded.
//        Voice 3's oscillator & envelope run (ported from reSID), so
//        players reading OSC3/ENV3 get what a C64 gives; voice 3 switched
//        off ($D418 bit 7) is left out. Digitized sound (samples played
//        through the volume register) isn't notes and is left out.
//        Notes come from gate-on edges (and big pitch changes), instruments
//        from the ADSR, waveform/pitch sequence (wavetable) and pulse
//        sweep (pulsetable) of the notes (a sweep going on through the
//        notes after: in the first one's pulse program), vibrato as the
//        instruments' vibrato (fitted to GoatTracker's). A note that differs
//        from an instrument in one thing (AD, SR, its wave or pulse program:
//        an arpeggio of its own) shares it, a command on its row sets that.
//        The filter becomes filtertable programs (set, sweeps, loops) that
//        are started where the one running stops giving the recorded
//        filter. The notes are on the frequency table of the base pitch
//        that fits them (GoatTracker's own, or one for a tune tuned
//        otherwise: an NTSC tune in NTSC mode, say). A note whose gate
//        goes off before the next row (drums, plucks) keeps the release in
//        its instrument: the wave program turns the gate off itself and
//        plays on while the release is heard. Other notes keep their gate
//        length: the player reads a keyoff (or the next note) the
//        gatetimer of ticks before its row, so each instrument's gatetimer
//        is the one that turns the gate off on the right tick (to a tick
//        when the gate went off on a row's start). When the instruments
//        don't fit, the pulse is simplified first, then the gate timing,
//        then the program length, the ADSR last. The rhythm is put on
//        a grid of rows with a fixed tempo (where the tune's timing shifts
//        a frame or two against it now and then, those frames are left
//        out). The result is a starting point
//        for editing, not a faithful copy: slides and anything changing
//        between rows is simplified. Where a subtune starts
//        over (the registers repeat to the end of a recording of up to 10
//        minutes, pulse widths aside), it ends and its orderlists loop
//        back, so the whole tune fits in few patterns; a subtune that stops
//        (a jingle, a sound effect) ends on empty rows. Each channel is cut
//        into patterns its own way, the same ones (also transposed: the
//        orderlist transposes them) only once, patterns that always come
//        together joined; shorter ones where that makes fewer. A pattern too
//        big for the player (256 bytes packed) is cut in two. The subtunes
//        share the instruments, tables and patterns; when there would still
//        be too many patterns, the ends of the longest subtunes are left out.
//        A player GoatTracker can't run at the same rate (a CIA timer
//        that isn't a whole multiple of the frame rate) gets the speed
//        that fits best, PAL or NTSC, and each subtune the tempo (or
//        funktempo pair) whose rows last as long; the instruments'
//        programs, which run per call, get faster or slower.
//
// AUTO   EXACT when the GoatTracker player is found, else APPROX.

#define SID2SNG_AUTO 0
#define SID2SNG_EXACT 1
#define SID2SNG_APPROX 2

typedef struct
{
  int mode;               // SID2SNG_AUTO/EXACT/APPROX (default AUTO)
  int subtune;            // APPROX: only this subtune, 1-n, 0 = all of them
                          // (up to 32, default 0). EXACT imports all.
  int seconds;            // APPROX: how long to record each subtune (that
                          // doesn't loop), 1-1800 (default 180)
  int rowsperbeat;        // APPROX: 0 = find the rhythm grid automatically
                          // (default), n = split the most common note
                          // length into n rows
  int tempo;              // APPROX: frames per row 3-127, 0 = automatic
                          // (default). Overrides rowsperbeat
  int patternlength;      // APPROX: rows per pattern 16-128 (default 64)
  int loops;              // APPROX: 1 = find where each subtune starts
                          // over (recording up to 10 minutes); it ends there
                          // and loops back (default 1). 0 = no loops
} SID2SNG_OPTIONS;

typedef struct
{
  char title[33];         // Header strings, 0-terminated
  char author[33];
  char released[33];
  int rsid;               // 1 = RSID (needs a real C64 environment)
  int version;            // Header version 1-4
  int songs;              // Number of subtunes
  int startsong;          // Default subtune 1-songs
  int clock;              // 0 = unknown, 1 = PAL, 2 = NTSC, 3 = both
  int model;              // 0 = unknown, 1 = 6581, 2 = 8580, 3 = both
  unsigned loadaddress;   // C64 addresses
  unsigned initaddress;
  unsigned playaddress;   // 0 = interrupt driven (set up by init)
  unsigned datasize;      // Bytes of C64 data
  unsigned speed;         // Speed flags (bit n set: subtune n+1 uses CIA timer)
} SIDINFO;

// How the song is to be played to sound like the SID: the editor settings,
// which aren't stored in the song
typedef struct
{
  int multiplier;         // Speed multiplier 1-16, 0 = 25Hz, -1 = none fits
                          // (EXACT only: APPROX fits the tempos to one)
  int ntsc;               // 1 = NTSC, 0 = PAL
  int adparam;            // Hardrestart ADSR $0000-$FFFF (APPROX: the one the
                          // tune uses most, $0F00 if none), -1 = not known
  int optimizepulse;      // Player optimizations (editor/packer settings):
  int optimizerealtime;   // 1 on, 0 off, -1 = not known (APPROX: both off,
                          // so pulse sweeps and effects run every frame)
  double basepitch;       // Tuning: the base pitch (A-4 in Hz on the PAL
                          // clock, as the tracker's setting) whose frequency
                          // table (12 notes an octave) has the notes; 0 =
                          // GoatTracker's own table, -1 = not known (another
                          // tuning: a Scala file, other divisions). APPROX:
                          // the one the tune's notes fit, e.g. 423.87 for an
                          // NTSC tune played in NTSC mode
} SID2SNG_PLAYBACK;

void sid2sng_defaults(SID2SNG_OPTIONS *opt);

// Reads the header of a .sid file. Returns 1 on success, 0 if the file
// can't be read or isn't a PSID/RSID file
int sidinfo_read(const char *sidpath, SIDINFO *info);

// Converts sidpath to sngpath. Returns 1 on success, 0 on failure. report
// (if not NULL) gets a short text about the result or the error
int sid2sng(const char *sidpath, const char *sngpath, const SID2SNG_OPTIONS *opt, char *report, int reportsize);

// The same, and playback (if not NULL) gets how to play the song: set the
// editor so (a song played at another speed or PAL/NTSC plays too fast or
// slow; NTSC tunes are converted for GoatTracker's NTSC mode)
int sid2sng_play(const char *sidpath, const char *sngpath, const SID2SNG_OPTIONS *opt, SID2SNG_PLAYBACK *playback,
  char *report, int reportsize);

#endif
