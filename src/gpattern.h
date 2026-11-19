#ifndef GPATTERN_H
#define GPATTERN_H

#ifndef GPATTERN_C


//extern int epnum[MAX_CHN];
//extern int editorInfo.eppos;
//extern int editorInfo.epview;
//extern int editorInfo.epcolumn;
//extern int editorInfo.epchn;
//extern int editorInfo.epoctave;
//extern int editorInfo.epmarkchn;
//extern int editorInfo.epmarkstart;
//extern int editorInfo.epmarkend;
#endif

typedef struct {
	// From gtstereo.c

	unsigned int eacolumn;
	unsigned int multiplier;
	unsigned int sidmodel;
	unsigned int adparam;

	unsigned int finevibrato;
	unsigned int optimizepulse;
	unsigned int optimizerealtime;
	unsigned int ntsc;
	// unsigned int usefinevib;

	int editmode;

	// From order.c
	int eseditpos;
	int esview;
	int escolumn;
	int eschn;
	int esnum;
	int esmarkchn;	// = -1;
	int esmarkstart;
	int esmarkend;
	int enpos;

	// From pattern.c
	int eppos;
	int epview;
	int epcolumn;
	int epchn;
	int epoctave;
	int epmarkchn;
	int epmarkstart;
	int epmarkend;

	// From ginstr.c
	int einum;
	int eipos;
	int eicolumn;

	// From gtable.c
	int etview[MAX_TABLES];
	int etnum;
	int etpos;
	int etcolumn;
	int etlock;	// = 1;
	int etmarknum;	// = -1;
	int etmarkstart;
	int etmarkend;

}EDITOR_INFO;

extern EDITOR_INFO editorInfo;
// The rows cut / copied last (SHIFT+X, C) and how many there are
extern unsigned char patterncopybuffer[];
extern int patterncopyrows;
extern int patterncopywhole;

void patterncommands(void);
int notekeytonote(int key);
void playfromcursor(int follow);
void playstop(void);
void playfromrow(int row, int follow);
// SPACE / numpad 0: play from the cursor (frompos: from row 0 of the
// orderlist cursor's position) or stop; the editor goes back to where the
// playback started when it is stopped
void playstopreturn(int follow, int frompos);
// Stops the playback (F4, SPACE...); see playstopreturn()
void stopplayback(void);
// F1: play from the start of the current pattern(s)
void playpatternstart(int follow);
// RETURN in the pattern editor: back to the song's start, stopped
void gotosongstart(void);
void patternjump(int row);
void patternscroll(int steps);
// Transposes the marked rows' notes (nothing marked: the cursor channel's
// pattern) by steps halfsteps. Returns the pattern number
int patterntranspose(int steps);
// Edit mode, following the playback in the pattern editor: a note played is
// recorded on the row it sounds in, not at the cursor (which is on the next
// row the player reads: a note entered there sounded twice, at once and
// again when its row came). Called from the MIDI thread too
int patternrecording(void);
// Where a note played now on channel c is recorded while patternrecording()
// (next: -1 = the row it comes nearest, 0 = the row playing, 1 = the next
// row, for a chord's later notes). The player won't play it again on that
// row. Returns 0 when it isn't recorded so, 1 = on the row playing, 2 = on
// the next row
int patternrecordpos(int c, int next, int *patt, int *row, int *trans);
void nextpattern(void);
void prevpattern(void);
void patternup(void);
void patterndown(void);
void shrinkpattern(void);
void expandpattern(void);
void splitpattern(void);
void joinpattern(void);

#endif
