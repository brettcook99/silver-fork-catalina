/*
 * GOATTRACKER file operations by full path name (menus, drag & drop,
 * Finder, command line) and the file dialogs
 */

#define GFILEIO_C

#include "goattrk2.h"
#include "gui/gui.h"
#include "gmacfile.h"
#include "gfermatax.h"

char currentsongpath[MAX_PATHNAME] = "";
char suggestedsongname[MAX_PATHNAME] = "";
SID2SNG_OPTIONS sidimportoptions;
static int sidimportoptionsset = 0;
// Set by gt_openfile_direct(): SID files are converted without the dialog
static int sidimportdirect = 0;

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

int gt_importmidi(const char *path)
{
  char temppath[MAX_PATHNAME];
  char report[256];
  MIDI2SNG_OPTIONS opt;

  stopsong();
  tempfilename("goattrk2-midi.sng", temppath, MAX_PATHNAME);
  midi2sng_defaults(&opt);
  opt.ntsc = editorInfo.ntsc;
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
  if (sidimportdirect) return importsid(path, &sidimportoptions);
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

  sidimportdirect = 1;
  ok = gt_openfile(path);
  sidimportdirect = 0;
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
