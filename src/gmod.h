#ifndef GMOD_H
#define GMOD_H

// ProTracker modules (.mod: M.K. and the other 4-channel kinds, xCHN up to
// 8 channels, 15-sample Soundtracker modules), see gmod.c.
//
// mod_scan(): what the import dialog shows (the channels and their notes).
// mod_import(): the module becomes the song (the song before is cleared),
//   each of the 3 tracks from the module channel(s) chosen in options.
// mod_export(): the current subtune is saved as a 4-channel module.
// They return nonzero if it worked; report gets what was converted and what
// couldn't be (or why it failed)

#define MOD_MAXCHN 8

typedef struct
{
  char name[21];              // the module's title
  int channels;               // 4 (more for xCHN modules)
  int positions;              // song length
  int patterns;
  int samples;                // samples with sound
  int notes[MOD_MAXCHN];      // notes per channel in the song
} MODSCAN;

typedef struct
{
  // Per track 1-3, the module channels it gets: bit n = channel n+1. None =
  // the track stays empty; two = the channels merged (where both have a
  // note on a row, the lower channel's note plays)
  unsigned source[3];
} MODIMPORT_OPTIONS;

int mod_scan(const char *path, MODSCAN *scan, char *report, int reportsize);
// The busiest three channels to tracks 1-3, in channel order (scan NULL:
// channels 1-3)
void mod_defaultoptions(const MODSCAN *scan, MODIMPORT_OPTIONS *o);
int mod_import(const char *path, const MODIMPORT_OPTIONS *o, char *report, int reportsize);
int mod_export(const char *path, char *report, int reportsize);

#endif
