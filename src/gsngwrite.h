#ifndef GSNGWRITE_H
#define GSNGWRITE_H

// GoatTracker v2 song (.sng, GTS5) writer for the importers (MIDI, SID...).
// Plain C, depends only on gcommon.h, so command line tools can use it too.
//
// Fill a SNGDATA (it is big, allocate it with sngdata_alloc() or malloc)
// and write it with sngwrite(). The layout follows the file format (see
// readme.txt, "GoatTracker v2 song (.SNG) format").

#include "gcommon.h"

typedef struct
{
  char songname[MAX_STR];             // Padded with zeros, need not be 0-terminated
  char authorname[MAX_STR];
  char copyrightname[MAX_STR];

  // Subtunes & orderlists. songlen is the orderlist length without the
  // LOOPSONG endmark: songorder[s][c][0..songlen-1] are pattern numbers and
  // REPEAT/TRANSPOSE commands, songorder[s][c][songlen+1] is the restart
  // position (the endmark itself is always written as LOOPSONG).
  int numsongs;                       // 1-MAX_SONGS
  int songlen[MAX_SONGS][MAX_CHN];    // 1-MAX_SONGLEN
  unsigned char songorder[MAX_SONGS][MAX_CHN][MAX_SONGLEN+2];

  // Instruments 1-numinstr (instr[0] is not stored)
  int numinstr;                       // 0-MAX_INSTR-1
  INSTR instr[MAX_INSTR];

  // Wave, pulse, filter & speedtable. Row n (1-based, as the pointers in
  // instruments & commands) is ltable[t][n-1] / rtable[t][n-1]
  int tablelen[MAX_TABLES];           // 0-MAX_TABLELEN
  unsigned char ltable[MAX_TABLES][MAX_TABLELEN];
  unsigned char rtable[MAX_TABLES][MAX_TABLELEN];

  // Patterns 0-numpatterns-1. pattlen is the number of rows without the
  // ENDPATT row (that is always written after them)
  int numpatterns;                    // 0-MAX_PATT
  int pattlen[MAX_PATT];              // 1-MAX_PATTROWS
  unsigned char pattern[MAX_PATT][MAX_PATTROWS*4];
} SNGDATA;

// Allocates a cleared SNGDATA (NULL if out of memory). Free with free()
SNGDATA *sngdata_alloc(void);

// Clears to an empty song: no subtunes, instruments, table rows or patterns
void sngdata_clear(SNGDATA *d);

// Copies a string into a song/author/copyright field (zero-padded, cut to fit)
void sngdata_settext(char *field, const char *text);

// Writes the song. Returns 1 on success, 0 on failure (could not write, or
// a count out of range)
int sngwrite(const char *path, const SNGDATA *d);

#endif
