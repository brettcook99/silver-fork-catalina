// Test harness only (tests/sid2sng): NOT the tracker's goattrk2.h.
// Test-harness stand-in for goattrk2.h: just what greloc.c needs, so the
// unmodified packer can be built without SDL/bme.
#ifndef GOATTRK2_H
#define GOATTRK2_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "gcommon.h"
#include "greloc.h"

#define MAX_FILENAME 256
#define MAX_PATHNAME 256
#define MAX_COLUMNS 100
#define MAX_ROWS 37
#define CNORMAL 8
#define CEDIT 10
#define CTITLE 15

typedef struct
{
  unsigned int multiplier;
  unsigned int sidmodel;
  unsigned int adparam;
  unsigned int optimizepulse;
  unsigned int optimizerealtime;
  unsigned int ntsc;
} EDITOR_INFO;

extern EDITOR_INFO editorInfo;
extern INSTR instr[MAX_INSTR];
extern unsigned char ltable[MAX_TABLES][MAX_TABLELEN];
extern unsigned char rtable[MAX_TABLES][MAX_TABLELEN];
extern unsigned char songorder[MAX_SONGS][MAX_CHN][MAX_SONGLEN+2];
extern unsigned char pattern[MAX_PATT][MAX_PATTROWS*4+4];
extern char songname[MAX_STR];
extern char authorname[MAX_STR];
extern char copyrightname[MAX_STR];
extern int pattlen[MAX_PATT];
extern int songlen[MAX_SONGS][MAX_CHN];
extern unsigned char freqtbllo[];
extern unsigned char freqtblhi[];
extern unsigned playerversion;
extern int fileformat;
extern int zeropageadr;
extern int playeradr;
extern unsigned sidaddress;
extern char textbuffer[MAX_PATHNAME];
extern char *programname;
extern char loadedsongfilename[MAX_FILENAME];
extern int key, rawkey;

void stopsong(void);
void countpatternlengths(void);
void exectable(int num, int ptr);
int gettablepartlen(int num, int pos);
void fwrite8(FILE *f, unsigned data);
void fwritele16(FILE *f, unsigned data);
int io_open(char *name);
int io_lseek(int handle, int bytes, int whence);
int io_read8(int handle);
void io_close(int handle);

#endif
