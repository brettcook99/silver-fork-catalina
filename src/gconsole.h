#ifndef GCONSOLE_H
#define GCONSOLE_H

// GoatTracker "console": keyboard/mouse input state that the editors read,
// plus the classic 100x37 text screen. With the modern interface the text
// screen is only used by the few remaining classic full-screen dialogs (file
// selector on non-macOS systems, packer/relocator options); the GUI layer
// draws it as an overlay on top of the new interface while such a dialog
// runs. With the retro interface the text screen is the whole interface, as
// in the original GoatTracker.

#define MAX_COLUMNS 100
#define MAX_ROWS 37
#define HOLDDELAY 24
#define DOUBLECLICKDELAY 15

// Commands from the menu bar and CMD key shortcuts that have no key of
// their own (menucommand)
#define MENUCMD_NONE 0
#define MENUCMD_UNDO 1
#define MENUCMD_RECORD 2
#define MENUCMD_FOLLOW 3
#define MENUCMD_PLAYSTOP 4
#define MENUCMD_SELECTALL 5
#define MENUCMD_IMPORTMIDI 6

// Modifier bits of a queued key event
#define KMOD_GT_SHIFT 1
#define KMOD_GT_CTRL 2
#define KMOD_GT_ALT 4
#define KMOD_GT_META 8

#ifdef __cplusplus
extern "C" {
#endif

int initscreen(void);
void closescreen(void);
void clearscreen(void);
void fliptoscreen(void);
void printtext(int x, int y, int color, const char *text);
void printtextc(int y, int color, const char *text);
void printtextcp(int cp, int y, int color, const char *text);
void printblank(int x, int y, int length);
void printblankc(int x, int y, int color, int length);
void drawbox(int x, int y, int color, int sx, int sy);
void printbg(int x, int y, int color, int length);
// Runs one frame of the user interface (with the text screen shown, as the
// classic dialogs expect) and takes the next key / mouse input
void getkey(void);
void changetextsize(int dir);
void togglefullscreen(void);

// Key event queue, filled by the GUI layer from SDL events. rawkey is an
// SDL keycode (KEY_xxx), key the typed character (0 if none) and mods a
// combination of KMOD_GT_xxx
void input_pushkey(int rawkey, int key, int mods);
// Adds a typed character to the key event just pushed, or queues it on its
// own if it does not belong to one
void input_pushtext(int key, int mods);
// Takes the next queued key event into key/rawkey/shiftpressed etc. and
// applies the macOS/numpad key conversions. Returns 0 if the queue was empty
// (key and rawkey are then 0)
int input_nextkey(void);
// Discards all queued key events
void input_clearkeys(void);
// Takes the next MIDI note event into midinoteon/midinoteoff. Returns 0 if
// there was none
int input_nextmidi(void);

// The text screen: one unsigned per character, char | color << 16
extern unsigned *scrbuffer;
// Set while a classic dialog is drawing on the text screen
extern int textscreenactive;
// Nonzero with the retro interface (the classic full-window text screen)
extern int retromode;
// Set by the retro main loop while it waits for input on the main screen
// (not in a classic dialog such as the file selector or the online help)
extern int retroatmainscreen;
extern int retromainshown;
// The mouse position on the text screen, as character cells (set by the GUI
// while the text screen is shown)
extern int textscreenmousex, textscreenmousey;

extern int key, rawkey, shiftpressed, ctrlpressed, altpressed, metapressed, shiftonlypressed, cursorflashdelay;
extern int midinoteon, midinoteoff;
// When the MIDI note event arrived (milliseconds, SDL_GetTicks())
extern unsigned midinotetime;
extern int menucommand;
extern int mouseb, prevmouseb;
extern int mousewheel;
extern int mouseheld;
extern int mousex, mousey;
extern unsigned bigwindow;
extern unsigned bigwindowcmdline;
extern unsigned windowedxsize;
extern unsigned windowedysize;
extern unsigned textsizepercent;
extern unsigned retrogradient;
#define DEFAULT_METERBOTTOM 0xffdcf6ffu
#define DEFAULT_METERMIDDLE 0xff4896ecu
#define DEFAULT_METERTOP 0xff162ca0u
extern unsigned retrometercolors[3];
#define METERPRESETS 4
extern unsigned retrometerpresets[METERPRESETS][3];

#ifdef __cplusplus
}
#endif

#endif
