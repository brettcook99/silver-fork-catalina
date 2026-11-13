/*
 * GOATTRACKER file operations by full path name (menus, drag & drop,
 * Finder, command line) and the file dialogs
 */

#define GFILEIO_C

#include "goattrk2.h"
#include "gui/gui.h"
#include "gmacfile.h"
#include "gfermatax.h"
#include "gpatch.h"

char currentsongpath[MAX_PATHNAME] = "";
char suggestedsongname[MAX_PATHNAME] = "";
SID2SNG_OPTIONS sidimportoptions;
static int sidimportoptionsset = 0;
MIDI2SNG_OPTIONS midiimportoptions;
int midiimportmode = MIDIIMPORT_ALL;
static int midiimportoptionsset = 0;
// Set by gt_openfile_direct(): SID and MIDI files are converted without the
// import dialogs
static int importdirect = 0;

static int importsid(const char *path, const SID2SNG_OPTIONS *opt);

/*
 * Path helpers
 */

const char *gt_basename(const char *path)
{
  const char *p = path;
  const char *name = path;

  while (*p)
  {
    if ((*p == '/') || (*p == '\\')) name = p + 1;
    p++;
  }
  return name;
}

void gt_dirname(const char *path, char *dir, int dirsize)
{
  const char *name = gt_basename(path);
  int len = (int)(name - path);

  if (dirsize <= 0) return;
  if (len >= dirsize) len = dirsize - 1;
  memcpy(dir, path, len);
  dir[len] = 0;
}

void gt_replaceext(const char *name, const char *newext, char *out, int outsize)
{
  const char *base = gt_basename(name);
  const char *dot = strrchr(base, '.');
  int len = dot ? (int)(dot - name) : (int)strlen(name);

  if (outsize <= 0) return;
  if (len > outsize - 1) len = outsize - 1;
  memcpy(out, name, len);
  out[len] = 0;
  if ((int)(strlen(out) + strlen(newext)) < outsize) strcat(out, newext);
}

static int hasext(const char *path, const char *ext)
{
  const char *dot = strrchr(gt_basename(path), '.');
  const char *a;
  const char *b;

  if (!dot) return 0;
  a = dot + 1;
  b = ext;
  while ((*a) && (*b))
  {
    if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
    a++;
    b++;
  }
  return (!*a) && (!*b);
}

int gt_filetype(const char *path)
{
  if (hasext(path, "sng")) return GTFILE_SONG;
  if (hasext(path, "ins")) return GTFILE_INSTRUMENT;
  if ((hasext(path, "mid")) || (hasext(path, "midi"))) return GTFILE_MIDI;
  if (hasext(path, "sid")) return GTFILE_SID;
  if (hasext(path, "fermatax")) return GTFILE_PATCH;
  return GTFILE_UNKNOWN;
}

// Reads the 4-byte identifier at the start of a file. Returns 0 if the file
// can't be read
static int readident(const char *path, char *ident)
{
  FILE *handle = fopen(path, "rb");
  int ok;

  memset(ident, 0, 4);
  if (!handle) return 0;
  ok = (fread(ident, 4, 1, handle) == 1);
  fclose(handle);
  return ok;
}

static void setsongpath(const char *path)
{
  char dir[MAX_PATHNAME];

  gt_dirname(path, dir, MAX_PATHNAME);
  if (dir[0]) strcpy(songpath, dir);
}

static void tempfilename(const char *name, char *out, int outsize)
{
  char *tempdir = getenv("TMPDIR");

  if ((!tempdir) || (!tempdir[0])) tempdir = getenv("TEMP");
  if ((!tempdir) || (!tempdir[0])) tempdir = getenv("TMP");
#ifdef __WIN32__
  if ((!tempdir) || (!tempdir[0])) tempdir = ".";
#else
  if ((!tempdir) || (!tempdir[0])) tempdir = "/tmp";
#endif
  snprintf(out, outsize, "%s%s%s", tempdir,
    (tempdir[strlen(tempdir) - 1] == '/') ? "" : "/", name);
}

/*
 * Songs
 */

int gt_loadsong(const char *path)
{
  char ident[4];

  if (!readident(path, ident))
  {
    gui_alert("Can't open the song", path);
    return 0;
  }
  if ((memcmp(ident, "GTS3", 4)) && (memcmp(ident, "GTS4", 4)) && (memcmp(ident, "GTS5", 4)) &&
      (memcmp(ident, "GTS2", 4)) && (memcmp(ident, "GTS!", 4)))
  {
    gui_alert("Not a GoatTracker song", path);
    return 0;
  }
  if (strlen(path) >= MAX_FILENAME)
  {
    gui_alert("The file name is too long", path);
    return 0;
  }

  stopsong();
  strcpy(songfilename, path);
  loadsong();
  undoInitAllAreas();
  strcpy(loadedsongfilename, path);
  strcpy(currentsongpath, path);
  suggestedsongname[0] = 0;
  setsongpath(path);
  gui_marksongsaved();
  gui_addrecentfile(path);
  gui_message("Opened %s", gt_basename(path));
  return 1;
}

int gt_mergesong(const char *path)
{
  char ident[4];

  if (!readident(path, ident))
  {
    gui_alert("Can't open the song", path);
    return 0;
  }
  if ((memcmp(ident, "GTS3", 4)) && (memcmp(ident, "GTS4", 4)) && (memcmp(ident, "GTS5", 4)))
  {
    gui_alert("Only GoatTracker 2 songs (GTS3-GTS5) can be merged", path);
    return 0;
  }
  if (strlen(path) >= MAX_FILENAME)
  {
    gui_alert("The file name is too long", path);
    return 0;
  }

  stopsong();
  {
    // mergesong() sets the loaded song name to the merged file; keep ours
    char oldname[MAX_FILENAME];
    strcpy(oldname, loadedsongfilename);
    strcpy(songfilename, path);
    mergesong();
    strcpy(loadedsongfilename, oldname);
    if (currentsongpath[0]) strcpy(songfilename, currentsongpath);
  }
  undoInitAllAreas();
  gui_message("Merged %s", gt_basename(path));
  return 1;
}

int gt_savesong(const char *path)
{
  if (strlen(path) >= MAX_FILENAME - 4)
  {
    gui_alert("The file name is too long", path);
    return 0;
  }
  strcpy(songfilename, path);
  if (!savesong())
  {
    gui_alert("Could not save the song", path);
    return 0;
  }
  strcpy(currentsongpath, songfilename);
  strcpy(loadedsongfilename, songfilename);
  suggestedsongname[0] = 0;
  setsongpath(songfilename);
  gui_marksongsaved();
  gui_addrecentfile(songfilename);
  gui_message("Saved %s", gt_basename(songfilename));
  return 1;
}

/*
 * Instruments
 */

int gt_loadinstrument(const char *path, int slot)
{
  char ident[4];

  if (!readident(path, ident))
  {
    gui_alert("Can't open the instrument", path);
    return 0;
  }
  if ((memcmp(ident, "GTI3", 4)) && (memcmp(ident, "GTI4", 4)) && (memcmp(ident, "GTI5", 4)) &&
      (memcmp(ident, "GTI2", 4)) && (memcmp(ident, "GTI!", 4)))
  {
    gui_alert("Not a GoatTracker instrument", path);
    return 0;
  }
  if (strlen(path) >= MAX_FILENAME)
  {
    gui_alert("The file name is too long", path);
    return 0;
  }
  if ((slot < 1) || (slot >= MAX_INSTR)) slot = 1;

  editorInfo.einum = slot;
  strcpy(instrfilename, path);
  loadinstrument();
  {
    char dir[MAX_PATHNAME];
    gt_dirname(path, dir, MAX_PATHNAME);
    if (dir[0]) strcpy(instrpath, dir);
  }
  gui_addrecentfile(path);
  gui_message("Loaded instrument %02X from %s", slot, gt_basename(path));
  return 1;
}

int gt_saveinstrument(const char *path, int slot)
{
  if ((slot < 1) || (slot >= MAX_INSTR))
  {
    gui_alert("Select an instrument first", "Instrument 00 is the empty instrument and can't be saved.");
    return 0;
  }
  if (strlen(path) >= MAX_FILENAME - 4)
  {
    gui_alert("The file name is too long", path);
    return 0;
  }
  editorInfo.einum = slot;
  strcpy(instrfilename, path);
  if (!saveinstrument())
  {
    gui_alert("Could not save the instrument", path);
    return 0;
  }
  gui_message("Saved instrument %02X to %s", slot, gt_basename(instrfilename));
  return 1;
}

/*
 * Imports: the converter writes a song file which is then loaded. The song
 * gets the name of the imported file (with .sng), but no file of its own
 * until it is saved
 */

static int loadconvertedsong(const char *temppath, const char *sourcepath)
{
  char ident[4];
  char name[MAX_PATHNAME];

  if ((!readident(temppath, ident)) || (memcmp(ident, "GTS", 3)))
    return 0;

  stopsong();
  strcpy(songfilename, temppath);
  loadsong();
  remove(temppath);
  undoInitAllAreas();

  gt_replaceext(gt_basename(sourcepath), ".sng", name, MAX_PATHNAME);
  strcpy(suggestedsongname, name);
  strcpy(loadedsongfilename, name);
  strcpy(songfilename, name);
  currentsongpath[0] = 0;
  // An imported song counts as modified: it has not been saved yet
  gui_addrecentfile(sourcepath);
  return 1;
}

/*
 * MIDI import. The dialog chooses which MIDI channels go to which tracks
 * (midiimportmode). They are put into the current subtune's tracks; the
 * tracks that get no notes, the instruments and the other subtunes stay, and
 * CTRL+Z undoes it. Opened from the Finder or the command line, the MIDI file
 * becomes a new song instead
 */

static void midiimportdefaults(void)
{
  if (midiimportoptionsset) return;
  midi2sng_defaults(&midiimportoptions);
  midiimportoptionsset = 1;
}

// The conversion options of an import mode. dest gets the tracks it fills
static void midiimportopt(int mode, MIDI2SNG_OPTIONS *opt, unsigned *dest)
{
  midiimportdefaults();
  *opt = midiimportoptions;
  opt->ntsc = editorInfo.ntsc;
  opt->split = 1;
  opt->channelmask = 0;
  *dest = (1u << MAX_CHN) - 1;
  if ((mode >= MIDIIMPORT_CHN1) && (mode <= MIDIIMPORT_CHN3))
  {
    opt->channelmask = 1u << (mode - MIDIIMPORT_CHN1);
    *dest = 1u << (mode - MIDIIMPORT_CHN1);
  }
  if (mode == MIDIIMPORT_MIXED) opt->split = 0;
}

// A new song from the MIDI file
static int importmidinew(const char *path)
{
  char temppath[MAX_PATHNAME];
  char report[256];
  MIDI2SNG_OPTIONS opt;
  unsigned dest;

  stopsong();
  tempfilename("goattrk2-midi.sng", temppath, MAX_PATHNAME);
  midiimportopt(midiimportmode, &opt, &dest);
  report[0] = 0;
  if (!midi2sng(path, temppath, &opt, report, sizeof report))
  {
    gui_alert("MIDI import failed", report);
    return 0;
  }
  if (!loadconvertedsong(temppath, path))
  {
    gui_alert("MIDI import failed", "Could not load the converted song");
    return 0;
  }
  gui_message("MIDI file imported: %s", report);
  return 1;
}

// Rests only?
static int emptypattern(int p)
{
  int r;
  for (r = 0; r < pattlen[p]; r++)
  {
    if ((pattern[p][r*4] != REST) || (pattern[p][r*4+1]) || (pattern[p][r*4+2]) || (pattern[p][r*4+3]))
      return 0;
  }
  return 1;
}

// Copies a table part of the converted song (from its 1-based row ptr up to
// and including the first FF row; one row of the speedtable) to the end of
// the song's table. Returns its 1-based position, 0 if the table is full
static int mergetablepart(const SNGDATA *d, int t, int ptr)
{
  unsigned char l[MAX_TABLELEN], r[MAX_TABLELEN];
  int start = ptr - 1, len = 0, pos, k;

  if ((start < 0) || (start >= d->tablelen[t])) return 0;
  while (start + len < d->tablelen[t])
  {
    l[len] = d->ltable[t][start + len];
    r[len] = d->rtable[t][start + len];
    len++;
    if ((t == STBL) || (l[len - 1] == 0xff)) break;
  }
  pos = patch_addrows(t, l, r, len);
  // Jumps within the part go along with it
  if ((pos) && (t != STBL))
  {
    for (k = 0; k < len; k++)
    {
      if ((l[k] == 0xff) && (r[k])) rtable[t][pos - 1 + k] = (unsigned char)(r[k] - ptr + pos);
    }
  }
  return pos;
}

typedef struct
{
  INSTR instr[MAX_INSTR];
  unsigned char ltable[MAX_TABLES][MAX_TABLELEN];
  unsigned char rtable[MAX_TABLES][MAX_TABLELEN];
  unsigned char pattern[MAX_PATT][MAX_PATTROWS*4+4];
  unsigned char songorder[MAX_SONGS][MAX_CHN][MAX_SONGLEN+2];
} SONGBACKUP;

// Puts the tracks in dest of the converted song (d) that have notes into the
// current subtune: tracks gets them. The patterns go where the song has room
// (patterns that were only in the tracks replaced, or empty ones in no
// orderlist); an instrument the song has by the same name (made by an
// earlier import, maybe edited since) is used again, the others go into
// empty instruments. Returns 0 with the reason in msg if there is no room;
// nothing is changed then
static int mergemiditracks(const SNGDATA *d, unsigned dest, unsigned *tracks, int *newinstr, char *msg, int msgsize)
{
  static const char *tablenames[MAX_TABLES] = { "wave", "pulse", "filter", "speed" };
  unsigned char ref[MAX_PATT], wasdest[MAX_PATT], taken[MAX_PATT];
  unsigned char instrbusy[MAX_INSTR], used[MAX_INSTR];
  int pattmap[MAX_PATT], instrmap[MAX_INSTR];
  int es = editorInfo.esnum;
  unsigned got = 0;
  SONGBACKUP *b;
  int s, c, e, p, r, t, i, j;
  int ok = 0;

  *tracks = 0;
  *newinstr = 0;

  // The tracks that get notes
  for (c = 0; c < MAX_CHN; c++)
  {
    if (!(dest & (1u << c))) continue;
    for (e = 0; (e < d->songlen[0][c]) && (!(got & (1u << c))); e++)
    {
      p = d->songorder[0][c][e];
      if (p >= d->numpatterns) continue;
      for (r = 0; r < d->pattlen[p]; r++)
      {
        unsigned char n = d->pattern[p][r*4];
        if ((n >= FIRSTNOTE) && (n <= LASTNOTE))
        {
          got |= 1u << c;
          break;
        }
      }
    }
  }
  if (!got)
  {
    snprintf(msg, msgsize, "No notes for the tracks chosen");
    return 0;
  }

  b = (SONGBACKUP *)malloc(sizeof(SONGBACKUP));
  if (!b)
  {
    snprintf(msg, msgsize, "Out of memory");
    return 0;
  }
  memcpy(b->instr, instr, sizeof b->instr);
  memcpy(b->ltable, ltable, sizeof b->ltable);
  memcpy(b->rtable, rtable, sizeof b->rtable);
  memcpy(b->pattern, pattern, sizeof b->pattern);
  memcpy(b->songorder, songorder, sizeof b->songorder);

  countpatternlengths();

  // Patterns in the orderlists that stay
  memset(ref, 0, sizeof ref);
  memset(wasdest, 0, sizeof wasdest);
  for (s = 0; s < MAX_SONGS; s++)
  {
    for (c = 0; c < MAX_CHN; c++)
    {
      for (e = 0; e < songlen[s][c]; e++)
      {
        p = songorder[s][c][e];
        if (p >= MAX_PATT) continue;          // Repeat / transpose
        if ((s == es) && (got & (1u << c))) wasdest[p] = 1;
        else ref[p] = 1;
      }
    }
  }

  // Instruments used in the patterns that stay
  memset(instrbusy, 0, sizeof instrbusy);
  for (p = 0; p < MAX_PATT; p++)
  {
    if ((!ref[p]) && ((wasdest[p]) || (emptypattern(p)))) continue;
    for (r = 0; r < pattlen[p]; r++)
    {
      if (pattern[p][r*4+1] < MAX_INSTR) instrbusy[pattern[p][r*4+1]] = 1;
    }
  }

  // The converted song's instruments the tracks use
  memset(used, 0, sizeof used);
  for (c = 0; c < MAX_CHN; c++)
  {
    if (!(got & (1u << c))) continue;
    for (e = 0; e < d->songlen[0][c]; e++)
    {
      p = d->songorder[0][c][e];
      if (p >= d->numpatterns) continue;
      for (r = 0; r < d->pattlen[p]; r++)
      {
        if (d->pattern[p][r*4+1] < MAX_INSTR) used[d->pattern[p][r*4+1]] = 1;
      }
    }
  }
  for (i = 0; i < MAX_INSTR; i++) instrmap[i] = 0;
  for (i = 1; i <= d->numinstr; i++)
  {
    if (!used[i]) continue;
    j = MAX_INSTR;
    if (d->instr[i].name[0])
    {
      for (j = 1; j < MAX_INSTR; j++)
      {
        if ((!patch_emptyinstr(j)) && (!strncmp(instr[j].name, d->instr[i].name, MAX_INSTRNAMELEN))) break;
      }
    }
    if (j < MAX_INSTR)
    {
      instrmap[i] = j;
      continue;
    }
    for (j = 1; j < MAX_INSTR; j++)
    {
      if ((patch_emptyinstr(j)) && (!instrbusy[j])) break;
    }
    if (j >= MAX_INSTR)
    {
      snprintf(msg, msgsize, "No empty instrument left");
      goto done;
    }
    instr[j] = d->instr[i];
    for (t = 0; t < MAX_TABLES; t++)
    {
      if (!d->instr[i].ptr[t]) continue;
      instr[j].ptr[t] = (unsigned char)mergetablepart(d, t, d->instr[i].ptr[t]);
      if (!instr[j].ptr[t])
      {
        snprintf(msg, msgsize, "The %stable is full", tablenames[t]);
        goto done;
      }
    }
    instrmap[i] = j;
    (*newinstr)++;
  }

  // The patterns and the orderlists
  for (p = 0; p < MAX_PATT; p++) pattmap[p] = -1;
  memset(taken, 0, sizeof taken);
  for (c = 0; c < MAX_CHN; c++)
  {
    unsigned char order[MAX_SONGLEN + 2];
    int len = d->songlen[0][c];

    if (!(got & (1u << c))) continue;
    for (e = 0; e < len; e++)
    {
      int src = d->songorder[0][c][e];
      if (src >= d->numpatterns)
      {
        order[e] = (unsigned char)src;
        continue;
      }
      if (pattmap[src] < 0)
      {
        int q;
        for (q = 0; q < MAX_PATT; q++)
        {
          if ((!ref[q]) && (!taken[q]) && ((wasdest[q]) || (emptypattern(q)))) break;
        }
        if (q >= MAX_PATT)
        {
          snprintf(msg, msgsize, "Not enough free patterns");
          goto done;
        }
        taken[q] = 1;
        memset(pattern[q], 0, sizeof pattern[q]);
        for (r = 0; r < d->pattlen[src]; r++)
        {
          i = d->pattern[src][r*4+1];
          pattern[q][r*4] = d->pattern[src][r*4];
          pattern[q][r*4+1] = (unsigned char)(((i > 0) && (i < MAX_INSTR)) ? instrmap[i] : i);
          pattern[q][r*4+2] = d->pattern[src][r*4+2];
          pattern[q][r*4+3] = d->pattern[src][r*4+3];
        }
        for (r = d->pattlen[src]; r <= MAX_PATTROWS; r++) pattern[q][r*4] = ENDPATT;
        pattmap[src] = q;
      }
      order[e] = (unsigned char)pattmap[src];
    }
    memset(songorder[es][c], 0, MAX_SONGLEN + 2);
    memcpy(songorder[es][c], order, len);
    songorder[es][c][len] = LOOPSONG;
    songorder[es][c][len + 1] = d->songorder[0][c][len + 1];
  }

  // The replaced tracks' patterns no other orderlist uses go with them
  for (p = 0; p < MAX_PATT; p++)
  {
    if ((wasdest[p]) && (!ref[p]) && (!taken[p])) clearpattern(p);
  }

  // The new tracks are shown from their start
  for (c = 0; c < MAX_CHN; c++)
  {
    if (!(got & (1u << c))) continue;
    if (songorder[es][c][0] < MAX_PATT) editorUndoInfo.editorInfo[c].epnum = songorder[es][c][0];
    editorUndoInfo.editorInfo[c].espos = 0;
    editorUndoInfo.editorInfo[c].esend = 0;
  }
  editorInfo.eppos = 0;
  editorInfo.epview = -VISIBLEPATTROWS/2;
  editorInfo.eseditpos = 0;
  editorInfo.esview = 0;

  // A song without a name gets the MIDI file's
  if (!songname[0]) memcpy(songname, d->songname, sizeof songname);
  if (!copyrightname[0]) memcpy(copyrightname, d->copyrightname, sizeof copyrightname);

  *tracks = got;
  ok = 1;

done:
  if (!ok)
  {
    memcpy(instr, b->instr, sizeof b->instr);
    memcpy(ltable, b->ltable, sizeof b->ltable);
    memcpy(rtable, b->rtable, sizeof b->rtable);
    memcpy(pattern, b->pattern, sizeof b->pattern);
    memcpy(songorder, b->songorder, sizeof b->songorder);
  }
  free(b);
  countpatternlengths();
  return ok;
}

// Converts the MIDI file with the import dialog's choice and puts it into
// the song
int gt_importmidi_run(const char *path)
{
  MIDI2SNG_OPTIONS opt;
  unsigned dest, tracks;
  SNGDATA *d;
  GTUNDO_OBJECT *undo;
  char report[256], msg[160], list[32];
  int ok, c, t, newinstr;

  midiimportopt(midiimportmode, &opt, &dest);
  report[0] = 0;
  d = midi2sngdata(path, &opt, report, sizeof report);
  if (!d)
  {
    gui_alert("MIDI import failed", report);
    return 0;
  }
  stopsong();

  // One undo step: everything the import may change
  undo = undoCreateEditorInfo();
  for (c = 1; c < MAX_INSTR; c++)
    undoAreaSetCheckForChange(UNDO_AREA_INSTRUMENTS, c, UNDO_AREA_DIRTY_CHECK);
  for (t = 0; t < MAX_TABLES; t++)
  {
    undoAreaSetCheckForChange(UNDO_AREA_TABLES + t, 0, UNDO_AREA_DIRTY_CHECK);
    undoAreaSetCheckForChange(UNDO_AREA_TABLES + t, 1, UNDO_AREA_DIRTY_CHECK);
  }
  for (c = 0; c < MAX_PATT; c++)
    undoAreaSetCheckForChange(UNDO_AREA_PATTERN, c, UNDO_AREA_DIRTY_CHECK);
  undoAreaSetCheckForChange(UNDO_AREA_PATTERN_LEN, 0, UNDO_AREA_DIRTY_CHECK);
  for (c = 0; c < MAX_CHN; c++)
    undoAreaSetCheckForChange(UNDO_AREA_ORDERLIST, c + editorInfo.esnum * MAX_CHN, UNDO_AREA_DIRTY_CHECK);
  undoAreaSetCheckForChange(UNDO_AREA_ORDERLIST_LEN, 0, UNDO_AREA_DIRTY_CHECK);
  ok = mergemiditracks(d, dest, &tracks, &newinstr, msg, sizeof msg);
  if (undoValidateUndoAreas(undo) == 0) undoFreeUndoObject(undo);
  free(d);
  if (!ok)
  {
    gui_alert("MIDI import failed", msg);
    return 0;
  }

  // A song that has no file yet is named after the MIDI file
  if ((!currentsongpath[0]) && (!loadedsongfilename[0]))
  {
    char name[MAX_PATHNAME];
    gt_replaceext(gt_basename(path), ".sng", name, MAX_PATHNAME);
    strcpy(suggestedsongname, name);
    if (strlen(name) < MAX_FILENAME)
    {
      strcpy(loadedsongfilename, name);
      strcpy(songfilename, name);
    }
  }
  gui_addrecentfile(path);

  list[0] = 0;
  for (c = 0; c < MAX_CHN; c++)
  {
    if (tracks & (1u << c)) sprintf(list + strlen(list), "%s%d", list[0] ? ", " : "", c + 1);
  }
  snprintf(msg, sizeof msg, "%s to track%s %s", gt_basename(path), (tracks & (tracks - 1)) ? "s" : "", list);
  for (c = 0; c < MAX_CHN; c++)
  {
    if ((dest & (1u << c)) && (!(tracks & (1u << c))))
      snprintf(msg + strlen(msg), sizeof msg - strlen(msg), " (track %d kept: no notes)", c + 1);
  }
  if (newinstr) gui_message("Imported %s: %s, %d new instrument%s", msg, report, newinstr, newinstr == 1 ? "" : "s");
  else gui_message("Imported %s: %s", msg, report);
  return 1;
}

// The import dialog (opened from the Finder or the command line: a new song
// at once, with the import mode last used)
int gt_importmidi(const char *path)
{
  MIDISCAN scan;
  char report[256];

  midiimportdefaults();
  if (importdirect) return importmidinew(path);
  report[0] = 0;
  if (!midi2sng_scan(path, &scan, report, sizeof report))
  {
    gui_alert("MIDI import failed", report);
    return 0;
  }
  gui_openmidiimport(path, &scan);
  return 1;
}

// Opens the SID import dialog (the conversion runs when it is confirmed)
int gt_importsid(const char *path)
{
  SIDINFO info;

  if (!sidinfo_read(path, &info))
  {
    gui_alert("Not a SID file", path);
    return 0;
  }
  if (!sidimportoptionsset)
  {
    sid2sng_defaults(&sidimportoptions);
    sidimportoptionsset = 1;
  }
  // Opened from the Finder or the command line: convert at once with the
  // options last used
  if (importdirect) return importsid(path, &sidimportoptions);
  gui_opensidimport(path);
  return 1;
}

int gt_importsid_run(const char *path, const SID2SNG_OPTIONS *opt)
{
  return importsid(path, opt);
}

// Converts and loads a SID file, and sets the editor to play it as the SID
// does: speed multiplier, PAL/NTSC, hardrestart ADSR, tuning (base pitch;
// these are editor settings, they stay for the songs after). How it went is
// shown in the status bar (a box only when it fails)
static int importsid(const char *path, const SID2SNG_OPTIONS *opt)
{
  char temppath[MAX_PATHNAME];
  char report[1024];
  char speed[64];
  SID2SNG_PLAYBACK pb;

  stopsong();
  tempfilename("goattrk2-sid.sng", temppath, MAX_PATHNAME);
  report[0] = 0;
  if (!sid2sng_play(path, temppath, opt, &pb, report, sizeof report))
  {
    gui_alert("SID import failed", report);
    return 0;
  }
  if (!loadconvertedsong(temppath, path))
  {
    gui_alert("SID import failed", "Could not load the converted song");
    return 0;
  }

  editorInfo.ntsc = pb.ntsc;
  if (pb.multiplier >= 0) editorInfo.multiplier = pb.multiplier;
  if (pb.adparam >= 0) editorInfo.adparam = pb.adparam;
  if (pb.optimizepulse >= 0) editorInfo.optimizepulse = pb.optimizepulse;
  if (pb.optimizerealtime >= 0) editorInfo.optimizerealtime = pb.optimizerealtime;
  if (pb.basepitch >= 0.0) setbasepitch((float)pb.basepitch);
  do_sound_init();
  if (editorInfo.multiplier) snprintf(speed, sizeof speed, "%dx", editorInfo.multiplier);
  else snprintf(speed, sizeof speed, "25Hz");
  gui_message("Imported %s, playing at %s %s%s: %s", gt_basename(path), speed, editorInfo.ntsc ? "NTSC" : "PAL",
    (pb.multiplier < 0) ? " (no speed fits)" : "", report);
  return 1;
}

int gt_importpatch(const char *path)
{
  char report[600];
  int slot = editorInfo.einum;
  int made;
  int t;
  GTUNDO_OBJECT *undo;

  if ((slot < 1) || (slot >= MAX_INSTR)) slot = 1;
  report[0] = 0;

  // Undoable: the instruments and the tables may change
  undo = undoCreateEditorInfo();
  for (t = 1; t < MAX_INSTR; t++)
    undoAreaSetCheckForChange(UNDO_AREA_INSTRUMENTS, t, UNDO_AREA_DIRTY_CHECK);
  for (t = 0; t < MAX_TABLES; t++)
  {
    undoAreaSetCheckForChange(UNDO_AREA_TABLES + t, 0, UNDO_AREA_DIRTY_CHECK);
    undoAreaSetCheckForChange(UNDO_AREA_TABLES + t, 1, UNDO_AREA_DIRTY_CHECK);
  }
  made = fermatax_import(path, slot, report, sizeof report);
  if (undoValidateUndoAreas(undo) == 0) undoFreeUndoObject(undo);

  if (!made)
  {
    gui_alert("chipsynth patch import failed", report);
    return 0;
  }
  editorInfo.einum = slot;
  gui_addrecentfile(path);
  gui_alert("chipsynth patch imported", report);
  gui_message("Imported %s", gt_basename(path));
  return 1;
}

int gt_openfile(const char *path)
{
  switch (gt_filetype(path))
  {
    case GTFILE_SONG:
    return gt_loadsong(path);

    case GTFILE_INSTRUMENT:
    return gt_loadinstrument(path, editorInfo.einum);

    case GTFILE_MIDI:
    return gt_importmidi(path);

    case GTFILE_SID:
    return gt_importsid(path);

    case GTFILE_PATCH:
    return gt_importpatch(path);
  }

  {
    // Unknown extension: look at the contents
    char ident[4];
    if (readident(path, ident))
    {
      if (!memcmp(ident, "GTS", 3)) return gt_loadsong(path);
      if (!memcmp(ident, "GTI", 3)) return gt_loadinstrument(path, editorInfo.einum);
      if ((!memcmp(ident, "PSID", 4)) || (!memcmp(ident, "RSID", 4))) return gt_importsid(path);
      if (!memcmp(ident, "MThd", 4)) return gt_importmidi(path);
    }
  }
  gui_alert("GoatTracker can't open this kind of file", path);
  return 0;
}

int gt_openfile_direct(const char *path)
{
  int ok;

  importdirect = 1;
  ok = gt_openfile(path);
  importdirect = 0;
  return ok;
}

/*
 * File dialogs
 */

// The folder last used for a kind of file (FOLDER_SONG...)
char *gt_folder(int kind)
{
  switch (kind)
  {
    case FOLDER_INSTR: return instrpath;
    case FOLDER_IMPORT: return importpath;
    case FOLDER_EXPORT: return packedpath;
  }
  return songpath;
}

#ifdef __APPLE__
// The folder of a file chosen in a dialog is used next time
static void rememberfolder(int kind, const char *path)
{
  char dir[MAX_PATHNAME];

  gt_dirname(path, dir, MAX_PATHNAME);
  if (dir[0]) strcpy(gt_folder(kind), dir);
}
#endif

int filedialog_open(int kind, const char *title, const char *exts, char *path, int pathsize)
{
#ifdef __APPLE__
  char *folder = gt_folder(kind);

  if (!macfile_open(title, exts, folder[0] ? folder : NULL, path, pathsize)) return 0;
  rememberfolder(kind, path);
  return 1;
#else
  // The classic file selector. It shows one extension at a time
  static char name[MAX_FILENAME] = "";
  char filter[MAX_FILENAME];
  char dir[MAX_PATHNAME];
  int c;

  strcpy(filter, "*.");
  for (c = 0; (exts) && (exts[c]) && (exts[c] != ';') && (c < 32); c++)
  {
    filter[2 + c] = exts[c];
    filter[3 + c] = 0;
  }
  if ((!exts) || (!exts[0])) strcpy(filter, "*");
  strcpy(dir, gt_folder(kind));
  if (!fileselector(name, dir, filter, (char *)title, 0)) return 0;
  strcpy(gt_folder(kind), dir);
  snprintf(path, pathsize, "%s/%s", dir, name);
  return 1;
#endif
}

int filedialog_folder(const char *title, const char *startdir, char *path, int pathsize)
{
#ifdef __APPLE__
  return macfile_folder(title, ((startdir) && (startdir[0])) ? startdir : NULL, path, pathsize);
#else
  // The classic file selector can only pick files: any instrument in the
  // folder is chosen
  static char name[MAX_FILENAME] = "";
  char dir[MAX_PATHNAME];
  char fulltitle[MAX_PATHNAME];
  char filter[MAX_FILENAME] = "*.ins";       // The selector changes it

  snprintf(dir, sizeof dir, "%s", ((startdir) && (startdir[0])) ? startdir : gt_folder(FOLDER_INSTR));
  snprintf(fulltitle, sizeof fulltitle, "%s (pick any instrument in it)", title);
  if (!fileselector(name, dir, filter, fulltitle, 0)) return 0;
  snprintf(path, pathsize, "%s", dir);
  return 1;
#endif
}

int filedialog_save(int kind, const char *title, const char *ext, const char *suggestedname, char *path, int pathsize)
{
#ifdef __APPLE__
  char *folder = gt_folder(kind);

  if (!macfile_save(title, ext, suggestedname, folder[0] ? folder : NULL, path, pathsize)) return 0;
  rememberfolder(kind, path);
  return 1;
#else
  static char name[MAX_FILENAME] = "";
  char filter[MAX_FILENAME];
  char dir[MAX_PATHNAME];

  snprintf(filter, MAX_FILENAME, "*.%s", ext);
  if ((suggestedname) && (suggestedname[0]))
  {
    strncpy(name, gt_basename(suggestedname), MAX_FILENAME - 1);
    name[MAX_FILENAME - 1] = 0;
  }
  strcpy(dir, gt_folder(kind));
  if (!fileselector(name, dir, filter, (char *)title, 3)) return 0;
  strcpy(gt_folder(kind), dir);
  snprintf(path, pathsize, "%s/%s", dir, name);
  // Add the extension if there is none
  if (!strrchr(gt_basename(path), '.'))
  {
    if ((int)(strlen(path) + strlen(ext) + 1) < pathsize)
    {
      strcat(path, ".");
      strcat(path, ext);
    }
  }
  return 1;
#endif
}
