// Test harness: command line packer built from the unmodified greloc.c
// (GT2RELOC mode). Options as gt2reloc, without its option parsing quirks:
//   relocharness in.sng out.sid [-Axxxx] [-B1] [-C1] [-D1] [-E1] [-H1] [-I0]
//     [-J1] [-N] [-Sn] [-Wxx] [-Zxx] [-O0] [-R0] [-Lxxxx] [-Gbasepitch]
#include "goattrk2.h"
#include <sys/stat.h>
#include <math.h>

EDITOR_INFO editorInfo;
INSTR instr[MAX_INSTR];
unsigned char ltable[MAX_TABLES][MAX_TABLELEN];
unsigned char rtable[MAX_TABLES][MAX_TABLELEN];
unsigned char songorder[MAX_SONGS][MAX_CHN][MAX_SONGLEN+2];
unsigned char pattern[MAX_PATT][MAX_PATTROWS*4+4];
char songname[MAX_STR];
char authorname[MAX_STR];
char copyrightname[MAX_STR];
int pattlen[MAX_PATT];
int songlen[MAX_SONGS][MAX_CHN];
unsigned playerversion = 0;
int fileformat = FORMAT_SID;
int zeropageadr = 0xfc;
int playeradr = 0x1000;
unsigned sidaddress = 0xd400;
char textbuffer[MAX_PATHNAME];
char *programname = "relocharness";
char loadedsongfilename[MAX_FILENAME];
char packedsongname[MAX_PATHNAME];
int key, rawkey;
static char playerdir[MAX_PATHNAME];

unsigned char freqtbllo[] = {
  0x17,0x27,0x39,0x4b,0x5f,0x74,0x8a,0xa1,0xba,0xd4,0xf0,0x0e,
  0x2d,0x4e,0x71,0x96,0xbe,0xe8,0x14,0x43,0x74,0xa9,0xe1,0x1c,
  0x5a,0x9c,0xe2,0x2d,0x7c,0xcf,0x28,0x85,0xe8,0x52,0xc1,0x37,
  0xb4,0x39,0xc5,0x5a,0xf7,0x9e,0x4f,0x0a,0xd1,0xa3,0x82,0x6e,
  0x68,0x71,0x8a,0xb3,0xee,0x3c,0x9e,0x15,0xa2,0x46,0x04,0xdc,
  0xd0,0xe2,0x14,0x67,0xdd,0x79,0x3c,0x29,0x44,0x8d,0x08,0xb8,
  0xa1,0xc5,0x28,0xcd,0xba,0xf1,0x78,0x53,0x87,0x1a,0x10,0x71,
  0x42,0x89,0x4f,0x9b,0x74,0xe2,0xf0,0xa6,0x0e,0x33,0x20,0xff};

unsigned char freqtblhi[] = {
  0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x02,
  0x02,0x02,0x02,0x02,0x02,0x02,0x03,0x03,0x03,0x03,0x03,0x04,
  0x04,0x04,0x04,0x05,0x05,0x05,0x06,0x06,0x06,0x07,0x07,0x08,
  0x08,0x09,0x09,0x0a,0x0a,0x0b,0x0c,0x0d,0x0d,0x0e,0x0f,0x10,
  0x11,0x12,0x13,0x14,0x15,0x17,0x18,0x1a,0x1b,0x1d,0x1f,0x20,
  0x22,0x24,0x27,0x29,0x2b,0x2e,0x31,0x34,0x37,0x3a,0x3e,0x41,
  0x45,0x49,0x4e,0x52,0x57,0x5c,0x62,0x68,0x6e,0x75,0x7c,0x83,
  0x8b,0x93,0x9c,0xa5,0xaf,0xb9,0xc4,0xd0,0xdd,0xea,0xf8,0xff};

void stopsong(void) {}

// The frequency table for a base pitch (A-4 in Hz, PAL), as the tracker's
// calculatefreqtable() makes it (12 notes an octave)
static void setbasepitch(float basepitch)
{
  double basefreq = (double)basepitch * (16777216.0 / 985248.0) * pow(2.0, 0.25) / 32.0;
  int c;
  for (c = 0; c < 8 * 12; c++)
  {
    double freq = basefreq * pow(2.0, c / 12.0);
    int intfreq = freq + 0.5;
    if (intfreq > 0xffff) intfreq = 0xffff;
    freqtbllo[c] = intfreq & 0xff;
    freqtblhi[c] = intfreq >> 8;
  }
}

// From gsong.c
void countpatternlengths(void)
{
  int c, d, e;
  for (c = 0; c < MAX_PATT; c++)
  {
    for (d = 0; d <= MAX_PATTROWS; d++)
      if (pattern[c][d*4] == ENDPATT) break;
    pattlen[c] = d;
  }
  for (e = 0; e < MAX_SONGS; e++)
  {
    for (c = 0; c < MAX_CHN; c++)
    {
      for (d = 0; d < MAX_SONGLEN; d++)
        if (songorder[e][c][d] >= LOOPSONG) break;
      songlen[e][c] = d;
    }
  }
}

// From gtable.c
int gettablepartlen(int num, int pos)
{
  int c;
  if (pos < 0) return 0;
  if (num == STBL) return 1;
  for (c = pos; c < MAX_TABLELEN; c++)
  {
    if (ltable[num][c] == 0xff)
    {
      c++;
      break;
    }
  }
  return c-pos;
}

void exectable(int num, int ptr)
{
  if ((num != STBL) && (ptr) && (ptr <= MAX_TABLELEN))
  {
    if (ltable[num][ptr-1] == 0xff)
    {
      tableerror = TYPE_JUMP;
      return;
    }
  }
  for (;;)
  {
    if (!ptr) break;
    if ((num != STBL) && (ptr > MAX_TABLELEN))
    {
      tableerror = TYPE_OVERFLOW;
      break;
    }
    if (tableused[num][ptr]) break;
    tableused[num][ptr] = 1;
    if (num != STBL)
    {
      if (ltable[num][ptr-1] == 0xff) ptr = rtable[num][ptr-1];
      else ptr++;
    }
    else break;
  }
}

void fwrite8(FILE *f, unsigned data)
{
  fputc(data & 0xff, f);
}

void fwritele16(FILE *f, unsigned data)
{
  fputc(data & 0xff, f);
  fputc((data >> 8) & 0xff, f);
}

// File access for insertfile("player.s"): read from the player directory
static FILE *iofile;
int io_open(char *name)
{
  char path[MAX_PATHNAME * 2];
  sprintf(path, "%s/%s", playerdir, name);
  iofile = fopen(path, "rb");
  return iofile ? 1 : -1;
}
int io_lseek(int handle, int bytes, int whence)
{
  (void)handle;
  fseek(iofile, bytes, whence);
  return (int)ftell(iofile);
}
int io_read8(int handle)
{
  (void)handle;
  return fgetc(iofile);
}
void io_close(int handle)
{
  (void)handle;
  fclose(iofile);
}

static int fread8(FILE *f)
{
  return fgetc(f) & 0xff;
}

static int loadsng(const char *name)
{
  FILE *f = fopen(name, "rb");
  char ident[4];
  int c, d, amount;
  if (!f) return 0;
  memset(songorder, 0, sizeof songorder);
  memset(pattern, 0, sizeof pattern);
  memset(instr, 0, sizeof instr);
  memset(ltable, 0, sizeof ltable);
  memset(rtable, 0, sizeof rtable);
  for (c = 0; c < MAX_PATT; c++) pattern[c][0] = ENDPATT;
  for (c = 0; c < MAX_SONGS; c++)
    for (d = 0; d < MAX_CHN; d++) songorder[c][d][0] = LOOPSONG;

  fread(ident, 4, 1, f);
  if (memcmp(ident, "GTS5", 4)) { fclose(f); return 0; }
  fread(songname, MAX_STR, 1, f);
  fread(authorname, MAX_STR, 1, f);
  fread(copyrightname, MAX_STR, 1, f);
  amount = fread8(f);
  for (d = 0; d < amount; d++)
  {
    for (c = 0; c < MAX_CHN; c++)
    {
      int length = fread8(f);
      fread(songorder[d][c], length + 1, 1, f);
    }
  }
  amount = fread8(f);
  for (c = 1; c <= amount; c++)
  {
    instr[c].ad = fread8(f);
    instr[c].sr = fread8(f);
    instr[c].ptr[WTBL] = fread8(f);
    instr[c].ptr[PTBL] = fread8(f);
    instr[c].ptr[FTBL] = fread8(f);
    instr[c].ptr[STBL] = fread8(f);
    instr[c].vibdelay = fread8(f);
    instr[c].gatetimer = fread8(f);
    instr[c].firstwave = fread8(f);
    fread(instr[c].name, MAX_INSTRNAMELEN, 1, f);
  }
  for (c = 0; c < MAX_TABLES; c++)
  {
    int len = fread8(f);
    fread(ltable[c], len, 1, f);
    fread(rtable[c], len, 1, f);
  }
  amount = fread8(f);
  for (c = 0; c < amount; c++)
  {
    int length = fread8(f) * 4;
    fread(pattern[c], length, 1, f);
  }
  fclose(f);
  countpatternlengths();
  return 1;
}

int main(int argc, char **argv)
{
  int c;
  char *s;

  if (argc < 3)
  {
    printf("Usage: relocharness in.sng out.sid|prg [options]\n");
    return 1;
  }
  strcpy(playerdir, argv[0]);
  s = strrchr(playerdir, '\\');
  if (!s) s = strrchr(playerdir, '/');
  if (s) *s = 0; else strcpy(playerdir, ".");
  if (getenv("GT_PLAYERDIR")) strcpy(playerdir, getenv("GT_PLAYERDIR"));

  editorInfo.multiplier = 1;
  editorInfo.adparam = 0x0f00;
  editorInfo.optimizepulse = 1;
  editorInfo.optimizerealtime = 1;
  editorInfo.ntsc = 0;
  editorInfo.sidmodel = 0;

  if (!loadsng(argv[1]))
  {
    printf("Could not load %s\n", argv[1]);
    return 1;
  }
  strcpy(loadedsongfilename, argv[1]);
  strcpy(packedsongname, argv[2]);
  s = strrchr(packedsongname, '.');
  fileformat = FORMAT_PRG;
  if ((s) && (!strcmp(s, ".sid"))) fileformat = FORMAT_SID;
  if ((s) && (!strcmp(s, ".bin"))) fileformat = FORMAT_BIN;

  for (c = 3; c < argc; c++)
  {
    char *a = argv[c];
    int on = (a[2] == '1');
    if (a[0] != '-') continue;
    switch (toupper(a[1]))
    {
      case 'A': sscanf(a + 2, "%x", &editorInfo.adparam); break;
      case 'B': if (on) playerversion |= PLAYER_BUFFERED; else playerversion &= ~PLAYER_BUFFERED; break;
      case 'C': if (on) playerversion |= PLAYER_ZPGHOSTREGS; else playerversion &= ~PLAYER_ZPGHOSTREGS; break;
      case 'D': if (on) playerversion |= PLAYER_SOUNDEFFECTS; else playerversion &= ~PLAYER_SOUNDEFFECTS; break;
      case 'E': if (on) playerversion |= PLAYER_VOLUME; else playerversion &= ~PLAYER_VOLUME; break;
      case 'H': if (on) playerversion |= PLAYER_AUTHORINFO; else playerversion &= ~PLAYER_AUTHORINFO; break;
      case 'I': if (on) playerversion &= ~PLAYER_NOOPTIMIZATION; else playerversion |= PLAYER_NOOPTIMIZATION; break;
      case 'J': if (on) playerversion |= PLAYER_FULLBUFFERED; else playerversion &= ~PLAYER_FULLBUFFERED; break;
      case 'N': editorInfo.ntsc = 1; break;
      case 'S': sscanf(a + 2, "%u", &editorInfo.multiplier); break;
      case 'O': sscanf(a + 2, "%u", &editorInfo.optimizepulse); break;
      case 'R': sscanf(a + 2, "%u", &editorInfo.optimizerealtime); break;
      case 'W': sscanf(a + 2, "%x", &playeradr); playeradr <<= 8; break;
      case 'Z': sscanf(a + 2, "%x", &zeropageadr); break;
      case 'L': sscanf(a + 2, "%x", &sidaddress); break;
      case 'M': editorInfo.sidmodel = on; break;
      case 'G': { float g = 0.0f; sscanf(a + 2, "%f", &g); if (g > 0.0f) setbasepitch(g); } break;
    }
  }
  playeradr &= 0xff00;
  zeropageadr &= 0xff;
  relocator();
  return 0;
}
