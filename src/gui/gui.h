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

// Interface choices for gui_chooseinterface() and the uimode setting
#define UI_ASK 0
#define UI_MODERN 1
#define UI_RETRO 2

// Shows the startup window that asks for the modern or the retro interface
// (only when SHIFT / OPTION is held at startup, or with -u0). current is the
// choice selected at first (UI_MODERN or UI_RETRO). Returns UI_MODERN or
// UI_RETRO, or UI_ASK if the window was closed
int gui_chooseinterface(int current);
// Switches the window between the modern and the retro interface (minimum
// window size, background)
void gui_setretro(int on);
// The status message shown at the moment, or NULL if there is none (for the
// retro interface, which shows it on the text screen)
const char *gui_currentmessage(void);

// Runs one frame: handles window events, draws the interface and shows it.
// With textscreen nonzero the classic text screen (scrbuffer) is drawn on
// top as a modal dialog. With the retro interface the text screen fills the
// window instead, whatever textscreen is. Key presses for the editors are
// queued with input_pushkey(), menu commands with cmd_post().
void gui_frame(int textscreen);

// Mouse state over the classic text screen (buttons as MOUSEB_xxx bits)
int gui_textscreenmousebuttons(void);
int gui_textscreenmousewheel(void);

void gui_changetextsize(int dir);
void gui_togglefullscreen(void);

// Handles the view commands (GTCMD_HELP, GTCMD_ZOOMIN, GTCMD_DESIGNER...).
// Returns 0 if the command is not a view command
int gui_command(int cmd);

// Nonzero while the SID designer is shown. MIDI notes go to it then
// (noteon / noteoff as midinoteon / midinoteoff)
int gui_designeractive(void);
// Nonzero while the SID designer is open (also under a classic dialog)
int gui_designershown(void);
// Opens the retro menu bar's VIEW menu (SID designer, background, meter
// colors, About) at the mouse pointer
void gui_openviewmenu(void);
void gui_designermidi(int noteon, int noteoff);

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
