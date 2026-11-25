#ifndef GMIDIEXPORT_H
#define GMIDIEXPORT_H

// Song -> standard MIDI file (.mid). The current subtune is played without
// sound by the player itself, so the notes come out as they sound: with the
// orderlist's transposes and repeats, tempo changes and tone portamento,
// once through every channel's orderlist (up to its loop). SID channels 1-3
// become tracks on MIDI channels 1-3 (note 0 = C-0 is MIDI note 12, so C-4 is
// middle C, as in the MIDI import). A keyoff or the next note ends a note.
// One pattern row is 1/stepsize beat (the highlighting step): the tempo
// follows channel 1's row length, so the rows stay on the DAW's beat grid.

#ifdef __cplusplus
extern "C" {
#endif

// Returns 1 if the file was written. report gets a short text about the
// result or the error
int midiexport(const char *path, char *report, int reportsize);

#ifdef __cplusplus
}
#endif

#endif
