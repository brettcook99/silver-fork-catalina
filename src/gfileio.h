#ifndef GFILEIO_H
#define GFILEIO_H

// Loading, saving and importing by full path name. Used by the File menu,
// drag & drop onto the window, files opened from the Finder and the command
// line.

#include "gsidimport.h"

#ifdef __cplusplus
extern "C" {
#endif

// Full path of the song file being edited, "" if it has never been saved
// (new or imported song)
extern char currentsongpath[];
// Name suggested when saving a song that has no file yet
extern char suggestedsongname[];
// Options of the SID import dialog
extern SID2SNG_OPTIONS sidimportoptions;

// Kind of file by its extension
#define GTFILE_UNKNOWN 0
#define GTFILE_SONG 1
#define GTFILE_INSTRUMENT 2
#define GTFILE_MIDI 3
#define GTFILE_SID 4
#define GTFILE_PATCH 5    // chipsynth C64 .fermatax
int gt_filetype(const char *path);

// All return 1 on success. On failure an explanation is shown to the user.
int gt_loadsong(const char *path);
int gt_mergesong(const char *path);
int gt_savesong(const char *path);
int gt_loadinstrument(const char *path, int slot);
int gt_saveinstrument(const char *path, int slot);
int gt_importmidi(const char *path);
// Shows the SID import dialog for the file
int gt_importsid(const char *path);
// Converts and loads a SID with the dialog's options
int gt_importsid_run(const char *path, const SID2SNG_OPTIONS *opt);
int gt_importpatch(const char *path);
// Opens any supported file by its type (a song replaces the current song)
int gt_openfile(const char *path);
// The same, but a SID file is converted at once with the import options last
// used, without the import dialog. Returns 1 if the file was opened
int gt_openfile_direct(const char *path);

// File dialogs: the native ones on macOS, the classic file selector
// elsewhere. exts is a list of extensions without dots, separated by ';'.
// Return 1 with the chosen full path, 0 if cancelled.
// The file dialogs start in the folder last used for their kind of file
// (remembered in the config file): songs, instruments, imports (SID, MIDI,
// chipsynth patches) and exports
#define FOLDER_SONG 0
#define FOLDER_INSTR 1
#define FOLDER_IMPORT 2
#define FOLDER_EXPORT 3
#define FOLDER_KINDS 4
int filedialog_open(int kind, const char *title, const char *exts, char *path, int pathsize);
int filedialog_save(int kind, const char *title, const char *ext, const char *suggestedname, char *path, int pathsize);
// Chooses a folder (on macOS a folder panel; elsewhere the classic file
// selector, where any instrument in the folder is picked). startdir may be
// empty. Returns 1 with the folder's path
int filedialog_folder(const char *title, const char *startdir, char *path, int pathsize);
char *gt_folder(int kind);

// Pieces of a path
const char *gt_basename(const char *path);
void gt_dirname(const char *path, char *dir, int dirsize);
// Writes name with its extension replaced by newext (with dot)
void gt_replaceext(const char *name, const char *newext, char *out, int outsize);

#ifdef __cplusplus
}
#endif

#endif
