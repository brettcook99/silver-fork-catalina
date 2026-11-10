#ifndef GUI_H
#define GUI_H

// GoatTracker user interface (Dear ImGui on SDL2). C interface for the rest
// of the program.

#ifdef __cplusplus
extern "C" {
#endif

// Opens the window. Returns 0 on failure
int gui_init(void);
void gui_shutdown(void);

// Runs one frame: handles window events, draws the interface and shows it.
// With textscreen nonzero the classic text screen (scrbuffer) is drawn on
// top as a modal dialog. Key presses for the editors are queued with
// input_pushkey(), menu commands with cmd_post().
void gui_frame(int textscreen);

// Mouse state over the classic text screen (buttons as MOUSEB_xxx bits)
int gui_textscreenmousebuttons(void);
int gui_textscreenmousewheel(void);

void gui_changetextsize(int dir);
void gui_togglefullscreen(void);

// Handles the view commands (GTCMD_HELP, GTCMD_ZOOMIN...). Returns 0 if the
// command is not a view command
int gui_command(int cmd);

// Shows a message in the status bar for a few seconds
void gui_message(const char *fmt, ...);

// Modal dialogs. They are shown on the next frames; the result is acted on
// by the GUI itself (or by queueing a command)
void gui_askquit(void);
void gui_askclear(void);
void gui_edithradsr(void);
// Asks whether to save the changes before running the command (with its
// file, for GTCMD_OPENFILE). Does nothing and returns 0 if the song is not
// modified; otherwise returns 1 and runs the command later when the user
// has answered (Save / Don't Save) or forgets it (Cancel)
int gui_asksavechanges(int cmd, const char *path);
// Tells the "Save changes?" dialog how the save it asked for went: on
// success the command that was waiting runs
void gui_savefinished(int ok);
// Export (pack, relocate & save SID/PRG/BIN) dialog
void gui_openexport(void);
// SID import dialog: shows the file's info and the conversion options
void gui_opensidimport(const char *path);

// Information / error box
void gui_alert(const char *title, const char *text);

// Remembers a file in the recent files list
void gui_addrecentfile(const char *path);

// The song as last loaded / saved, to tell whether it has been modified
void gui_marksongsaved(void);
int gui_songmodified(void);

#ifdef __cplusplus
}
#endif

#endif
