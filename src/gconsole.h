#ifndef GCONSOLE_H
#define GCONSOLE_H

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
void getkey(void);
void changetextsize(int dir);
void togglefullscreen(void);

#ifndef GCONSOLE_C
extern int key, rawkey, shiftpressed, ctrlpressed, altpressed, metapressed, shiftonlypressed,cursorflashdelay;
extern int midinoteon, midinoteoff;
extern int menucommand;
extern int mouseb, prevmouseb;
extern int mousewheel;
extern int mouseheld;
extern int mousex, mousey;
extern unsigned bigwindow;
extern unsigned bigwindowcmdline;
extern unsigned windowedxsize;
extern unsigned windowedysize;
#endif

#endif
