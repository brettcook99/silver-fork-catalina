#ifndef GCOMMAND_H
#define GCOMMAND_H

// Commands from the menus (macOS menu bar or the in-window menu), toolbar
// buttons, drag & drop and keyboard shortcuts. They are queued while the
// user interface is drawn and run by the main loop afterwards, so that a
// command may open a modal dialog of its own.

enum
{
  GTCMD_NONE = 0,

  // File
  GTCMD_NEW,
  GTCMD_OPEN,             // Open song (native dialog)
  GTCMD_OPENFILE,         // Open the file given as path (drop, Finder, recent)
  GTCMD_SAVE,
  GTCMD_SAVEAS,
  GTCMD_MERGE,
  GTCMD_IMPORTMIDI,
  GTCMD_IMPORTSID,
  GTCMD_IMPORTPATCH,      // chipsynth C64 .fermatax
  GTCMD_LOADINSTR,
  GTCMD_SAVEINSTR,
  GTCMD_EXPORT,           // Pack, relocate & save (SID/PRG/BIN): the dialog
  GTCMD_EXPORTNOW,        // Choose the file and export with the dialog's options
  GTCMD_QUIT,

  // Edit
  GTCMD_UNDO,
  GTCMD_CUT,
  GTCMD_COPY,
  GTCMD_PASTE,
  GTCMD_DELETE,
  GTCMD_SELECTALL,
  GTCMD_CLEARSONG,        // Clear / optimize dialog
  GTCMD_HRADSR,           // Hard restart ADSR dialog

  // Transport
  GTCMD_PLAYSONG,
  GTCMD_PLAYPOS,
  GTCMD_PLAYPATTERN,
  GTCMD_PLAYCURSOR,
  GTCMD_STOP,
  GTCMD_PLAYSTOP,
  GTCMD_RECORD,
  GTCMD_FOLLOW,
  GTCMD_OCTAVEUP,
  GTCMD_OCTAVEDOWN,
  GTCMD_PREVSUBTUNE,
  GTCMD_NEXTSUBTUNE,

  // Editors
  GTCMD_EDITPATTERN,
  GTCMD_EDITORDER,
  GTCMD_EDITINSTR,
  GTCMD_EDITTABLES,
  GTCMD_EDITNAMES,

  // Pattern editor commands (work like their SHIFT+key)
  GTCMD_TRANSPOSEUP,
  GTCMD_TRANSPOSEDOWN,
  GTCMD_TRANSPOSEOCTUP,
  GTCMD_TRANSPOSEOCTDOWN,
  GTCMD_INSERTROW,
  GTCMD_DELETEROW,
  GTCMD_SHRINKPATTERN,
  GTCMD_EXPANDPATTERN,
  GTCMD_SPLITPATTERN,
  GTCMD_JOINPATTERN,
  GTCMD_INVERTSEL,

  // View (handled by the GUI)
  GTCMD_HELP,
  GTCMD_ZOOMIN,
  GTCMD_ZOOMOUT,
  GTCMD_ZOOMRESET,
  GTCMD_FULLSCREEN,
  GTCMD_RESETLAYOUT,
  GTCMD_TOGGLELIBRARY,
  GTCMD_TOGGLEINSPECTOR,
  GTCMD_TOGGLETRACKS,
  GTCMD_TOGGLETABLES,
  GTCMD_TOGGLESONG,

  GTCMD_COUNT
};

#ifdef __cplusplus
extern "C" {
#endif

void cmd_post(int cmd);
void cmd_postfile(int cmd, const char *path);
// Takes the next queued command. path gets the file name of GTCMD_OPENFILE
// (empty for other commands). Returns 0 if the queue is empty
int cmd_poll(int *cmd, char *path, int pathsize);

#ifdef __cplusplus
}
#endif

#endif
