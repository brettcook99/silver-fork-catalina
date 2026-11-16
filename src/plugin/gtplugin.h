/*
 * GoatTracker plugin build (GT_PLUGIN): what the tracker's own code calls
 * of the plugin layer (gtplugin.c, gtplugin_audio.c)
 */

#ifndef GTPLUGIN_H
#define GTPLUGIN_H

#ifdef GT_PLUGIN

#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

// Folders given by the plugin shell
extern char gtplugin_resourcedir[];
extern char gtplugin_configdir[];

// The tracker's main(), run by the GoatTracker thread (goattrk2.c)
int gt_main(int argc, char **argv);

// A path in the config folder
void gtplugin_configpath(const char *name, char *out, int outsize);
// A temporary file name of this instance (other instances use other names)
void gtplugin_tempname(const char *name, char *out, int outsize);

// The editor's size: physical pixels and physical pixels per logical one
void gtplugin_viewsize(int *pw, int *ph, float *scale);
int gtplugin_editorvisible(void);

// Called at the top of every user interface frame (and by getkey()): when
// the plugin is closed, the program's loops are told to finish
void gtplugin_checkquit(void);
int gtplugin_quitting(void);

// Puts the queued editor input into SDL's event queue (pollevents())
void gtplugin_feedevents(SDL_Window *window);

// After SDL_RenderPresent(): the window's picture for the editor
void gtplugin_publishframe(SDL_Window *window);
void gtplugin_setcursor(int cursor);
void gtplugin_setclipboard(const char *text);

// CTRL+ / CTRL- in the retro interface: asks the host for another size
void gtplugin_requestresize(int w, int h);

// Once per user interface frame on the GoatTracker thread: project state
// requests, notes recorded from the host's MIDI, project dirty tracking
void gtplugin_service(void);

// A file dialog of the system (the host's side shows it): GTFILEDIALOG_OPEN,
// _SAVE or _FOLDER; start = the folder, or for a save dialog the suggested
// file in it; filter = extensions separated by ';'. Waits for the answer
// with the user interface running. Returns 1 with the chosen path, 0 if
// cancelled, -1 if the host shows no file dialogs (the classic file
// selector is used then)
#define GTFILEDIALOG_OPEN 0
#define GTFILEDIALOG_SAVE 1
#define GTFILEDIALOG_FOLDER 2
int gtplugin_filedialog(int mode, const char *title, const char *start, const char *filter, char *path, int pathsize);

// Song loads and SID re-inits exclude the audio thread with this
void gtplugin_lockengine(void);
void gtplugin_unlockengine(void);

/*
 * Audio (gtplugin_audio.c)
 */

// sound_init() in the plugin: (re)creates the SID at the host's rate
int gtplugin_soundinit(unsigned model, unsigned ntsc, unsigned multiplier, unsigned interpolate, unsigned customclockrate);
void gtplugin_sounduninit(void);

// DAW SYNC settings (saved with the project)
extern int gtsync_enabled;        // follow the host's tempo & position
extern int gtsync_startbar;       // the song starts at this bar (1 = first)
extern int gtsync_route;          // MIDI channels 1-3 play / record voices 1-3
extern int gtsync_recordoff;      // record note-offs as KEYOFF
// The host's recording switched the mode: 1 = edit, 2 = jam (for the message)
extern volatile int gtsync_modechange;
// MIDI program changes: the instrument of each MIDI channel's notes (0 = the
// editor's instrument; saved with the project), the channels that got one
// since the GoatTracker thread looked (bits 0-15, for its message; bit
// GTSYNC_PROGRAMBAD: a program past the last instrument came, its number + 1
// in gtsync_programbad)
#define GTSYNC_PROGRAMBAD 0x10000
extern volatile int gtsync_programinstr[16];
extern SDL_atomic_t gtsync_programchanged;
extern volatile int gtsync_programbad;
// The host's tempo (0 if not known) and whether its transport runs
double gtsync_hostbpm(void);
int gtsync_hostplaying(void);
// Nonzero while the playback follows the host's transport: the tracker's
// own play / stop commands are not used then
int gtsync_following(void);
// Nonzero on the GoatTracker thread while the playback follows the host's
// transport: the user interface's play commands are blocked
int gtsync_uiblocked(void);
// A new song: while following the host, the player goes to the host's
// position in it
void gtsync_requestresync(void);
// Called by the GoatTracker thread when it starts
void gtplugin_setuithread(void);
void gtplugin_started(void);
// A line in gtcore.log (config folder)
void gtplugin_log(const char *fmt, ...);
const char *gtplugin_getclipboard(void);

// Notes recorded on the audio thread, written into the patterns on the
// GoatTracker thread (gtplugin_service())
typedef struct
{
  int voice;
  int patt;
  int row;
  int note;       // the note to write (transpose taken off), or KEYOFF
  int instr;      // 0 = keep
  int newpass;    // the first note of a recording pass (its own undo step)
} GTRECORDNOTE;
int gtrecord_pop(GTRECORDNOTE *rn);

// The settings of the DAW SYNC dialog (gui_dialogs.cpp)
void gui_opendawsync(void);

#ifdef __cplusplus
}
#endif

#endif

#endif
