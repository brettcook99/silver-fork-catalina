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
// APPROX For any SID with a play routine. The tune is run on an emulated
//        6502 for a while and the SID registers are recorded every call.
//        Notes come from gate-on edges (and big pitch changes), instruments
//        from the ADSR, waveform/pitch sequence (wavetable) and pulse
//        sweep (pulsetable) of the notes, the filter from the filter
//        registers (filtertable programs & commands). The rhythm is put on
//        a grid of rows with a fixed tempo. The result is a starting point
//        for editing, not a faithful copy: vibrato, slides and anything
//        changing between rows is simplified.
//
// AUTO   EXACT when the GoatTracker player is found, else APPROX.

#define SID2SNG_AUTO 0
#define SID2SNG_EXACT 1
#define SID2SNG_APPROX 2

typedef struct
{
  int mode;               // SID2SNG_AUTO/EXACT/APPROX (default AUTO)
  int subtune;            // APPROX: subtune to record, 1-n, 0 = the SID's
                          // start song (default 0). EXACT imports all.
  int seconds;            // APPROX: how long to record, 1-1800 (default 180)
  int rowsperbeat;        // APPROX: 0 = find the rhythm grid automatically
                          // (default), n = split the most common note
                          // length into n rows
  int tempo;              // APPROX: frames per row 3-127, 0 = automatic
                          // (default). Overrides rowsperbeat
  int patternlength;      // APPROX: rows per pattern 16-128 (default 64)
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

void sid2sng_defaults(SID2SNG_OPTIONS *opt);

// Reads the header of a .sid file. Returns 1 on success, 0 if the file
// can't be read or isn't a PSID/RSID file
int sidinfo_read(const char *sidpath, SIDINFO *info);

// Converts sidpath to sngpath. Returns 1 on success, 0 on failure. report
// (if not NULL) gets a short text about the result or the error
int sid2sng(const char *sidpath, const char *sngpath, const SID2SNG_OPTIONS *opt, char *report, int reportsize);

#endif
