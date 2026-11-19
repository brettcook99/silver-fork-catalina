#ifndef GSWM_H
#define GSWM_H

// SID-Wizard modules (.swm, format SWM1, see gswm.c).
// swm_import(): the module becomes the song (the song before is cleared).
// swm_export(): the song is saved as a module.
// Both return nonzero if it worked; report gets what was converted and what
// couldn't be (or why it failed)
int swm_import(const char *path, char *report, int reportsize);
int swm_export(const char *path, char *report, int reportsize);

#endif
