#ifndef GPATCH_H
#define GPATCH_H

// Helpers for the instrument designer: GoatTracker instruments seen as the
// parts of a synth patch. They work on instr[], ltable[] and rtable[] only
// (no user interface), so that the designer and the importers can share them.
//
// A "part" is an instrument's piece of a table: the rows from its pointer up
// to and including the first FF (jump / end) row. Rows are 0-based here,
// pointers and jump targets 1-based as in the tables themselves.

#define PATCH_OSC 3

#ifdef __cplusplus
extern "C" {
#endif

// Table parts
// Number of rows from pos up to and including the first FF row (to the end of
// the table if there is none). 1 for the speedtable
int patch_partlen(int t, int pos);
// The first row of the instrument's part, -1 if it has none
int patch_partstart(int t, int ins);
// Adds rows at the end of a table. Returns the 1-based position of the first,
// 0 if there is no room
int patch_addrows(int t, const unsigned char *l, const unsigned char *r, int rows);
// Inserts a row before row pos. Instrument pointers, jumps and pattern /
// wavetable commands are adjusted. With keepstart, what points at pos points
// at the new row afterwards. Returns 0 if the table is full
int patch_insertrow(int t, int pos, unsigned char l, unsigned char r, int keepstart);
// Deletes row pos (pointers adjusted as above)
void patch_deleterow(int t, int pos);
// Nonzero if something other than instrument ins points into the
// instrument's part: another instrument, a jump from another part, a
// wavetable command or a pattern command
int patch_isshared(int t, int ins);
// Copies the instrument's part to the end of the table and points the
// instrument at the copy. Returns 0 if there is no room
int patch_unshare(int t, int ins);
// Replaces the instrument's part with the given rows. In the FF rows the
// right byte is relative: 1 = the part's first row, 0 = stop. A shared part
// is unshared first; an instrument without a part gets one at the end of the
// table. Returns 0 if there is no room (nothing is changed then)
int patch_setpart(int t, int ins, const unsigned char *l, const unsigned char *r, int rows);
// Rows free at the end of a table
int patch_freerows(int t);

// Instruments
int patch_emptyinstr(int i);
// The first empty instrument after instrument after (wrapping round; never
// 00), 0 if there is none
int patch_findempty(int after);
// Copies instrument src into dest with copies of its wave and pulse table
// parts (the speedtable row is shared, the filter is left out). Returns 0 if
// a table is full
int patch_copyinstr(int dest, int src);

// The instrument's main wave row: the first row of its wavetable part that
// sets the waveform (10-EF). -1 if none
int patch_waverow(int ins);
// The same, making a wavetable [41 00][FF 00] (pulse with gate) or adding a
// wave row if there is none. -1 if the table is full
int patch_needwaverow(int ins);
// The first "set pulse width" row (80-FE) of the instrument's pulse part
int patch_pulserow(int ins);
int patch_needpulserow(int ins, int pw);
// The first "set filter" row (80-FE) and the first "set cutoff" row (00) of
// the filter part
int patch_filterrow(int ins);
int patch_cutoffrow(int ins);
// Makes sure the instrument has a filter part with both rows (lowpass with
// the given voice routing bits). -1 if the table is full, otherwise the
// filter row
int patch_needfilter(int ins, int route);

// Vibrato (speedtable row, compare value l and depth r). Both 0 = none. The
// instrument's own row is changed in place, a shared row is not: a matching
// or new row is used instead. Returns 0 if the speedtable is full
int patch_setvibrato(int ins, int l, int r);

// What the player does with the pulse and filter parts, frame by frame (for
// the graphs): out gets the pulse width (0-FFF) or cutoff (0-FF), rowat the
// row being run (-1 when stopped)
void patch_simpulse(int ins, int frames, unsigned short *out, int *rowat);
void patch_simfilter(int ins, int frames, unsigned char *out, int *rowat);

// Names: oscillator n of the patch "BASE" is the instrument named "BASE:n"
// (a colon, as songs often have unrelated instruments named "BASS 1" and
// "BASS 2"). Splits a name into the base (at most MAX_INSTRNAMELEN chars) and
// returns the oscillator number (1-3), 0 if the name has none
int patch_splitname(const char *name, char *base);
// The instrument named base + ":n" (or just base when n is 0), 0 if none
int patch_findosc(const char *base, int n);
// Names the instrument base + ":n" (just base when n is 0), cutting the base
// to fit
void patch_setname(int ins, const char *base, int n);
// The first instrument of the n-th patch (0 = the first): the instruments
// from 01 on, those of one patch (BASE:1, BASE:2...) counted once. 0 if
// there are not so many
int patch_nth(int n);

#ifdef __cplusplus
}
#endif

#endif
