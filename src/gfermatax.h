#ifndef GFERMATAX_H
#define GFERMATAX_H

// Imports a Plogue chipsynth C64 preset (.fermatax) as GoatTracker
// instruments. The preset is XML; only the settings that differ from
// chipsynth's defaults are stored, as numbered parameters. What is used
// (oscillator n = 0, 1, 2):
//
//   512+16n  oscillator on (0/1)          768+16n  pulse width (0-4095)
//   517+16n  coarse tune (halfsteps)      770+16n  waveform bits: 1 tri,
//   518+16n  fine tune (cents)                     2 saw, 4 pulse, 8 noise
//   865      filter cutoff (0-2047)       773+16n  sync    774+16n  ring
//   866      filter resonance (0-15)      775-778+16n  attack, decay,
//   867-869  low, band, high pass                  sustain, release (0-15)
//   1024     chip model                   779+16n  oscillator to filter
//   262      clock (985248 PAL, 1022727 NTSC)
//
// Each oscillator that is on becomes an instrument with a wavetable
// (waveform, coarse tune), pulsetable (pulse width) and filtertable (when
// routed to the filter). The first goes into the given slot, the others into
// the next empty slots. Step sequencers, modulators, effects, fine tune and
// voice settings can't be expressed as an instrument and are left out.

#ifdef __cplusplus
extern "C" {
#endif

// Returns the number of instruments made (0 on failure). report gets a
// short description of the result or the error
int fermatax_import(const char *path, int slot, char *report, int reportsize);

#ifdef __cplusplus
}
#endif

#endif
