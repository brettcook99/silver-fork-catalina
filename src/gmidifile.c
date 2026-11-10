//
// GOATTRACKER v2 MIDI file import: standard MIDI file (.mid) -> song (.sng)
//
// See gmidifile.h for what the conversion does. This file doesn't depend on
// the rest of the tracker, so the midi2sng tool can use it too.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gcommon.h"
#include "gmidifile.h"

#define MIDI_MAXSIZE (16*1024*1024)
#define DEFAULT_USPQN 500000      // Microseconds per quarter note (120 BPM)
#define DRUMCHANNEL 9             // MIDI channel 10
#define NUMNOTES (LASTNOTE - FIRSTNOTE + 1)
#define MINTEMPO 3
#define MAXTEMPO 0x7f

typedef struct
{
  unsigned start;                 // Start & end in MIDI ticks, then in rows
  unsigned end;
  int chn;                        // MIDI channel 0-15
  int note;                       // MIDI note 0-127
} MNOTE;

typedef struct
{
  unsigned tick;
  unsigned uspqn;
} MTEMPO;

typedef struct
{
  int start;                      // Rows: start, end (exclusive)
  int end;
  int voice;                      // SID channel 0-2
  int note;                       // GoatTracker note number 0-(NUMNOTES-1)
  int chn;                        // MIDI channel
} PLACED;

typedef struct
{
  const char *name;
  unsigned char ad, sr, wave;
  unsigned short pulse;           // 0 = no pulse table
  unsigned char absnote;          // Wavetable absolute note, 0 = play the note
} PRESET;

static const PRESET presets[] =
{
  {"Pulse", 0x08, 0xa6, 0x41, 0x400, 0},
  {"Saw", 0x08, 0x96, 0x21, 0, 0},
  {"Triangle", 0x08, 0xc6, 0x11, 0, 0},
  {"Square", 0x08, 0xa6, 0x41, 0x800, 0},
};
#define NUMPRESETS (int)(sizeof presets / sizeof presets[0])
static const PRESET basspreset = {"Bass", 0x06, 0x96, 0x41, 0x600, 0};
static const PRESET drumpreset = {"Drums", 0x05, 0x00, 0x81, 0, 0xc8};

// Parsing state
static MNOTE *notes;
static int numnotes, maxnotes;
static MTEMPO *tempos;
static int numtempos, maxtempos;
static int firstprogram[16];
static char trackname[MAX_STR];
static char copyright[MAX_STR];

static void setreport(char *report, int reportsize, const char *text)
{
  if ((report) && (reportsize > 0))
  {
    strncpy(report, text, reportsize - 1);
    report[reportsize - 1] = 0;
  }
}

static void copytext(char *dest, const unsigned char *src, unsigned len)
{
  unsigned c, d = 0;
  if (dest[0]) return;            // Use the first one found
  for (c = 0; (c < len) && (d < MAX_STR - 1); c++)
  {
    if ((src[c] >= 32) && (src[c] < 127)) dest[d++] = src[c];
  }
  while ((d > 0) && (dest[d-1] == ' ')) d--;
  dest[d] = 0;
}

static int readvlq(const unsigned char **p, const unsigned char *end, unsigned *value)
{
  unsigned v = 0;
  int c;

  for (c = 0; c < 4; c++)
  {
    unsigned char b;
    if (*p >= end) return 0;
    b = *(*p)++;
    v = (v << 7) | (b & 0x7f);
    if (!(b & 0x80))
    {
      *value = v;
      return 1;
    }
  }
  return 0;
}

static int addnote(unsigned tick, int chn, int note)
{
  if (numnotes >= maxnotes)
  {
    int newmax = maxnotes ? maxnotes * 2 : 1024;
    MNOTE *newnotes = (MNOTE *)realloc(notes, newmax * sizeof(MNOTE));
    if (!newnotes) return -1;
    notes = newnotes;
    maxnotes = newmax;
  }
  notes[numnotes].start = tick;
  notes[numnotes].end = tick;
  notes[numnotes].chn = chn;
  notes[numnotes].note = note;
  return numnotes++;
}

static int addtempo(unsigned tick, unsigned uspqn)
{
  if (numtempos >= maxtempos)
  {
    int newmax = maxtempos ? maxtempos * 2 : 64;
    MTEMPO *newtempos = (MTEMPO *)realloc(tempos, newmax * sizeof(MTEMPO));
    if (!newtempos) return 0;
    tempos = newtempos;
    maxtempos = newmax;
  }
  tempos[numtempos].tick = tick;
  tempos[numtempos].uspqn = uspqn;
  numtempos++;
  return 1;
}

// Reads the events of one track. Returns 0 if out of memory
static int parsetrack(const unsigned char *p, const unsigned char *end, int tracknum)
{
  static int active[16][128];     // Index+1 of the sounding note, 0 = none
  unsigned tick = 0;
  unsigned char status = 0;
  int c, d;

  memset(active, 0, sizeof active);

  while (p < end)
  {
    unsigned delta;
    unsigned char b;

    if (!readvlq(&p, end, &delta)) break;
    tick += delta;
    if (p >= end) break;

    b = *p;
    if (b & 0x80)
    {
      p++;
      if (b < 0xf0) status = b;
    }
    else
    {
      // Running status
      if (!status) break;
      b = status;
    }

    if (b < 0xf0)
    {
      int type = b & 0xf0;
      int chn = b & 0x0f;
      int nbytes = ((type == 0xc0) || (type == 0xd0)) ? 1 : 2;
      int d1, d2;

      if (p + nbytes > end) break;
      d1 = p[0] & 0x7f;
      d2 = (nbytes > 1) ? (p[1] & 0x7f) : 0;
      p += nbytes;

      if ((type == 0x90) && (d2))
      {
        int n;
        // The same key again while it sounds: the old note ends here
        if (active[chn][d1]) notes[active[chn][d1] - 1].end = tick;
        n = addnote(tick, chn, d1);
        if (n < 0) return 0;
        active[chn][d1] = n + 1;
      }
      else if ((type == 0x80) || (type == 0x90))
      {
        if (active[chn][d1])
        {
          notes[active[chn][d1] - 1].end = tick;
          active[chn][d1] = 0;
        }
      }
      else if (type == 0xc0)
      {
        if (firstprogram[chn] < 0) firstprogram[chn] = d1;
      }
    }
    else if (b == 0xff)
    {
      unsigned char type;
      unsigned len;

      if (p >= end) break;
      type = *p++;
      if (!readvlq(&p, end, &len)) break;
      if (len > (unsigned)(end - p)) break;

      if ((type == 0x51) && (len >= 3))
      {
        if (!addtempo(tick, (p[0] << 16) | (p[1] << 8) | p[2])) return 0;
      }
      if ((type == 0x03) && (tracknum == 0)) copytext(trackname, p, len);
      if (type == 0x02) copytext(copyright, p, len);
      p += len;
      if (type == 0x2f) break;    // End of track
    }
    else if ((b == 0xf0) || (b == 0xf7))
    {
      unsigned len;
      if (!readvlq(&p, end, &len)) break;
      if (len > (unsigned)(end - p)) break;
      p += len;
      status = 0;
    }
    else break;                   // Not allowed in a MIDI file
  }

  // Notes still sounding end with the track
  for (c = 0; c < 16; c++)
  {
    for (d = 0; d < 128; d++)
    {
      if (active[c][d]) notes[active[c][d] - 1].end = tick;
    }
  }
  return 1;
}

static int cmptempo(const void *a, const void *b)
{
  const MTEMPO *ta = (const MTEMPO *)a;
  const MTEMPO *tb = (const MTEMPO *)b;
  if (ta->tick < tb->tick) return -1;
  if (ta->tick > tb->tick) return 1;
  return 0;
}

// Notes in the order they get a SID channel: by start row, and on the
// same row highest note first
static int cmpnote(const void *a, const void *b)
{
  const MNOTE *na = (const MNOTE *)a;
  const MNOTE *nb = (const MNOTE *)b;
  if (na->start != nb->start) return (na->start < nb->start) ? -1 : 1;
  if (na->note != nb->note) return (na->note > nb->note) ? -1 : 1;
  return na->chn - nb->chn;
}

static unsigned tickstorows(unsigned tick, unsigned ppq, int rowsperbeat)
{
  return (unsigned)(((unsigned long long)tick * rowsperbeat + ppq / 2) / ppq);
}

static void fwrite8(FILE *f, int value)
{
  fputc(value & 0xff, f);
}

int midi2sng(const char *midiname, const char *sngname, const MIDI2SNG_OPTIONS *opt, char *report, int reportsize)
{
  FILE *f;
  unsigned char *data = NULL;
  const unsigned char *p, *end;
  long size;
  int format, numtracks, division, track;
  unsigned ppq;
  int smpte = 0;
  int ok = 0;
  int c, d;
  char msg[256];

  // Conversion state
  PLACED *placed = NULL;
  int numplaced = 0, dropped = 0, clampedtempos = 0;
  int instrnum[16];
  int numinstr = 0;
  int instrchn[16];
  unsigned char *grid[MAX_CHN] = {NULL, NULL, NULL};
  unsigned char *patterns = NULL;
  int numpatterns = 0;
  unsigned char orderlist[MAX_CHN][MAX_SONGLEN];
  int songlen = 0;
  int patternlength;
  int totalrows = 0;
  int firsttempo = 6;
  int rowsperbeat;

  notes = NULL;
  numnotes = maxnotes = 0;
  tempos = NULL;
  numtempos = maxtempos = 0;
  for (c = 0; c < 16; c++) firstprogram[c] = -1;
  memset(trackname, 0, sizeof trackname);
  memset(copyright, 0, sizeof copyright);

  rowsperbeat = opt->rowsperbeat;
  if (rowsperbeat < 1) rowsperbeat = 1;
  if (rowsperbeat > 32) rowsperbeat = 32;
  patternlength = opt->patternlength;
  if (patternlength < 16) patternlength = 16;
  if (patternlength > MAX_PATTROWS) patternlength = MAX_PATTROWS;

  // Read the whole file
  f = fopen(midiname, "rb");
  if (!f)
  {
    setreport(report, reportsize, "Could not open the MIDI file");
    return 0;
  }
  fseek(f, 0, SEEK_END);
  size = ftell(f);
  fseek(f, 0, SEEK_SET);
  if ((size < 14) || (size > MIDI_MAXSIZE))
  {
    fclose(f);
    setreport(report, reportsize, "Not a MIDI file");
    return 0;
  }
  data = (unsigned char *)malloc(size);
  if ((!data) || (fread(data, 1, size, f) != (size_t)size))
  {
    fclose(f);
    free(data);
    setreport(report, reportsize, "Could not read the MIDI file");
    return 0;
  }
  fclose(f);

  p = data;
  end = data + size;

  // RIFF MIDI (.rmi): the standard MIDI file is in the "data" chunk
  if ((!memcmp(p, "RIFF", 4)) && (size > 20) && (!memcmp(p + 8, "RMID", 4)))
  {
    const unsigned char *q = p + 12;
    while (q + 8 <= end)
    {
      unsigned len = q[4] | (q[5] << 8) | (q[6] << 16) | ((unsigned)q[7] << 24);
      if (!memcmp(q, "data", 4))
      {
        p = q + 8;
        if (len < (unsigned)(end - p)) end = p + len;
        break;
      }
      if (len > (unsigned)(end - q - 8)) break;
      q += 8 + len + (len & 1);
    }
  }

  if ((end - p < 14) || (memcmp(p, "MThd", 4)))
  {
    setreport(report, reportsize, "Not a MIDI file");
    goto done;
  }
  {
    unsigned hlen = ((unsigned)p[4] << 24) | (p[5] << 16) | (p[6] << 8) | p[7];
    format = (p[8] << 8) | p[9];
    numtracks = (p[10] << 8) | p[11];
    division = (p[12] << 8) | p[13];
    if ((hlen < 6) || (hlen > (unsigned)(end - p - 8)))
    {
      setreport(report, reportsize, "Not a MIDI file");
      goto done;
    }
    p += 8 + hlen;
  }
  if (format > 2)
  {
    setreport(report, reportsize, "Unknown MIDI file format");
    goto done;
  }
  if (division & 0x8000)
  {
    // SMPTE time: ticks per second. Treated as 120 BPM
    int fps = -(signed char)(division >> 8);
    int tpf = division & 0xff;
    if ((fps <= 0) || (!tpf))
    {
      setreport(report, reportsize, "Unknown MIDI time division");
      goto done;
    }
    ppq = (fps * tpf) / 2;
    smpte = 1;
  }
  else ppq = division;
  if (!ppq)
  {
    setreport(report, reportsize, "Unknown MIDI time division");
    goto done;
  }

  // Read the tracks
  track = 0;
  while ((end - p >= 8) && (track < numtracks))
  {
    unsigned len = ((unsigned)p[4] << 24) | (p[5] << 16) | (p[6] << 8) | p[7];
    const unsigned char *trackend;
    if (len > (unsigned)(end - p - 8)) len = (unsigned)(end - p - 8);
    trackend = p + 8 + len;
    if (!memcmp(p, "MTrk", 4))
    {
      if (!parsetrack(p + 8, trackend, track))
      {
        setreport(report, reportsize, "Out of memory");
        goto done;
      }
      track++;
    }
    p = trackend;
  }

  // Leave out the channels not wanted, convert ticks to rows and notes to
  // GoatTracker notes
  d = 0;
  for (c = 0; c < numnotes; c++)
  {
    MNOTE n = notes[c];
    int gtnote;
    if ((n.chn == DRUMCHANNEL) && (!opt->drums)) continue;
    if ((opt->channelmask) && (!(opt->channelmask & (1 << n.chn)))) continue;

    // MIDI note 60 (middle C) = C-4. Notes outside the range go an octave
    // up/down until they fit
    gtnote = n.note - 12 + opt->transpose;
    while (gtnote < 0) gtnote += 12;
    while (gtnote >= NUMNOTES) gtnote -= 12;

    n.note = gtnote;
    n.start = tickstorows(notes[c].start, ppq, rowsperbeat);
    n.end = tickstorows(notes[c].end, ppq, rowsperbeat);
    if (n.end <= n.start) n.end = n.start + 1;
    notes[d++] = n;
  }
  numnotes = d;
  if (!numnotes)
  {
    setreport(report, reportsize, "No notes found in the MIDI file");
    goto done;
  }

  qsort(notes, numnotes, sizeof(MNOTE), cmpnote);
  placed = (PLACED *)malloc(numnotes * sizeof(PLACED));
  if (!placed)
  {
    setreport(report, reportsize, "Out of memory");
    goto done;
  }

  // Share the notes out to the 3 SID channels
  {
    int busyuntil[MAX_CHN];       // Row where the channel is free again
    int lastchn[MAX_CHN];         // MIDI channel it played last
    int laststart[MAX_CHN];
    int lastplaced[MAX_CHN];      // Index of the note it plays
    int groupstart = 0;

    for (c = 0; c < MAX_CHN; c++)
    {
      busyuntil[c] = 0;
      lastchn[c] = -1;
      laststart[c] = -1;
      lastplaced[c] = -1;
    }

    while (groupstart < numnotes)
    {
      // The notes starting on one row, highest first. The lowest note
      // (usually the bass) goes second
      int groupend = groupstart;
      int order[NUMNOTES * 16];
      int count = 0;

      while ((groupend < numnotes) && (notes[groupend].start == notes[groupstart].start)) groupend++;
      for (c = groupstart; (c < groupend) && (count < (int)(sizeof order / sizeof order[0])); c++)
        order[count++] = c;
      dropped += (groupend - groupstart) - count;
      if (count > 2)
      {
        int lowest = order[count - 1];
        memmove(&order[2], &order[1], (count - 2) * sizeof(int));
        order[1] = lowest;
      }

      for (d = 0; d < count; d++)
      {
        MNOTE *n = &notes[order[d]];
        int row = n->start;
        int voice = -1;
        int v;

        // A free channel that played the same MIDI channel, or else the
        // channel free for the longest time
        for (v = 0; v < MAX_CHN; v++)
        {
          if ((busyuntil[v] <= row) && (lastchn[v] == n->chn))
          {
            voice = v;
            break;
          }
        }
        if (voice < 0)
        {
          for (v = 0; v < MAX_CHN; v++)
          {
            if ((busyuntil[v] <= row) && ((voice < 0) || (busyuntil[v] < busyuntil[voice])))
              voice = v;
          }
        }

        // No free channel: the highest and lowest new notes cut off a note
        // that started earlier (on the same MIDI channel if possible)
        if ((voice < 0) && (d < 2))
        {
          for (v = 0; v < MAX_CHN; v++)
          {
            if ((laststart[v] < row) && (lastchn[v] == n->chn))
            {
              voice = v;
              break;
            }
          }
          if (voice < 0)
          {
            for (v = 0; v < MAX_CHN; v++)
            {
              if ((laststart[v] < row) && ((voice < 0) || (laststart[v] < laststart[voice])))
                voice = v;
            }
          }
          if (voice >= 0)
          {
            placed[lastplaced[voice]].end = row;
          }
        }

        if (voice < 0)
        {
          dropped++;
          continue;
        }

        placed[numplaced].start = row;
        placed[numplaced].end = n->end;
        placed[numplaced].voice = voice;
        placed[numplaced].note = n->note;
        placed[numplaced].chn = n->chn;
        busyuntil[voice] = n->end;
        lastchn[voice] = n->chn;
        laststart[voice] = row;
        lastplaced[voice] = numplaced;
        numplaced++;
      }
      groupstart = groupend;
    }
  }

  // An instrument for every MIDI channel, in the order they first play
  for (c = 0; c < 16; c++) instrnum[c] = 0;
  for (c = 0; c < numplaced; c++)
  {
    int chn = placed[c].chn;
    if (!instrnum[chn])
    {
      instrchn[numinstr] = chn;
      instrnum[chn] = ++numinstr;
    }
    if (placed[c].end > totalrows) totalrows = placed[c].end;
  }
  totalrows++;                    // Room for the last keyoff

  // Patterns: try the wanted length, then the longest if the song doesn't
  // fit the orderlist / pattern limits
  for (;;)
  {
    int rows = ((totalrows + patternlength - 1) / patternlength) * patternlength;
    int pattbytes = (patternlength + 1) * 4;
    int toomany = 0;
    unsigned prevtempo = 0;

    songlen = rows / patternlength;
    if (songlen > MAX_SONGLEN) toomany = 1;

    if (!toomany)
    {
      for (c = 0; c < MAX_CHN; c++)
      {
        free(grid[c]);
        grid[c] = (unsigned char *)malloc(rows * 4);
        if (!grid[c])
        {
          setreport(report, reportsize, "Out of memory");
          goto done;
        }
        for (d = 0; d < rows; d++)
        {
          grid[c][d*4] = REST;
          grid[c][d*4+1] = 0;
          grid[c][d*4+2] = 0;
          grid[c][d*4+3] = 0;
        }
      }

      // Notes, then keyoffs where no new note starts
      for (c = 0; c < numplaced; c++)
      {
        unsigned char *cell = &grid[placed[c].voice][placed[c].start*4];
        cell[0] = FIRSTNOTE + placed[c].note;
        cell[1] = instrnum[placed[c].chn];
      }
      for (c = 0; c < numplaced; c++)
      {
        unsigned char *cell = &grid[placed[c].voice][placed[c].end*4];
        if (cell[0] == REST) cell[0] = KEYOFF;
      }

      // Tempo (frames per row) at the start and on every tempo change
      qsort(tempos, numtempos, sizeof(MTEMPO), cmptempo);
      clampedtempos = 0;
      for (c = -1; c < numtempos; c++)
      {
        unsigned uspqn = DEFAULT_USPQN;
        unsigned row = 0;
        double frames;
        int tempo;

        if (c >= 0)
        {
          if (smpte) break;
          row = tickstorows(tempos[c].tick, ppq, rowsperbeat);
          uspqn = tempos[c].uspqn;
          if (!uspqn) continue;
        }
        if ((int)row >= rows) break;

        frames = (double)uspqn * (opt->ntsc ? 60.0 : 50.0) / (1000000.0 * rowsperbeat);
        tempo = (int)(frames + 0.5);
        if ((tempo < MINTEMPO) || (tempo > MAXTEMPO)) clampedtempos++;
        if (tempo < MINTEMPO) tempo = MINTEMPO;
        if (tempo > MAXTEMPO) tempo = MAXTEMPO;
        if (!row) firsttempo = tempo;
        if ((unsigned)tempo == prevtempo) continue;
        grid[0][row*4+2] = CMD_SETTEMPO;
        grid[0][row*4+3] = tempo;
        prevtempo = tempo;
      }

      // Cut the channels into patterns. Patterns that are the same are
      // stored once
      free(patterns);
      patterns = (unsigned char *)malloc(MAX_PATT * pattbytes);
      if (!patterns)
      {
        setreport(report, reportsize, "Out of memory");
        goto done;
      }
      numpatterns = 0;
      for (c = 0; (c < MAX_CHN) && (!toomany); c++)
      {
        for (d = 0; d < songlen; d++)
        {
          unsigned char temp[(MAX_PATTROWS + 1) * 4];
          int e;
          memcpy(temp, &grid[c][d*patternlength*4], patternlength*4);
          temp[patternlength*4] = ENDPATT;
          temp[patternlength*4+1] = 0;
          temp[patternlength*4+2] = 0;
          temp[patternlength*4+3] = 0;
          for (e = 0; e < numpatterns; e++)
          {
            if (!memcmp(&patterns[e*pattbytes], temp, pattbytes)) break;
          }
          if (e == numpatterns)
          {
            if (numpatterns >= MAX_PATT)
            {
              toomany = 1;
              break;
            }
            memcpy(&patterns[e*pattbytes], temp, pattbytes);
            numpatterns++;
          }
          orderlist[c][d] = e;
        }
      }
    }

    if (!toomany) break;
    if (patternlength < MAX_PATTROWS)
    {
      patternlength = MAX_PATTROWS;
      continue;
    }
    setreport(report, reportsize, "The song is too long for GoatTracker (try fewer rows per beat)");
    goto done;
  }

  // Write the song
  f = fopen(sngname, "wb");
  if (!f)
  {
    setreport(report, reportsize, "Could not write the song file");
    goto done;
  }
  {
    char name[MAX_STR];
    unsigned char ltable[MAX_TABLES][MAX_TABLELEN];
    unsigned char rtable[MAX_TABLES][MAX_TABLELEN];
    int tablelen[MAX_TABLES];
    int others = 0;

    memset(ltable, 0, sizeof ltable);
    memset(rtable, 0, sizeof rtable);
    memset(tablelen, 0, sizeof tablelen);

    fwrite("GTS5", 4, 1, f);

    // Song name: the MIDI sequence name, or else the file name
    memset(name, 0, sizeof name);
    if (trackname[0]) strcpy(name, trackname);
    else
    {
      const char *base = midiname;
      const char *s;
      for (s = midiname; *s; s++)
      {
        if ((*s == '/') || (*s == '\\') || (*s == ':')) base = s + 1;
      }
      for (c = 0; (base[c]) && (base[c] != '.') && (c < MAX_STR - 1); c++) name[c] = base[c];
    }
    fwrite(name, MAX_STR, 1, f);
    memset(name, 0, sizeof name);
    fwrite(name, MAX_STR, 1, f);            // Author
    memset(name, 0, sizeof name);
    strcpy(name, copyright);
    fwrite(name, MAX_STR, 1, f);

    // One subtune: the orderlists loop back to the start
    fwrite8(f, 1);
    for (c = 0; c < MAX_CHN; c++)
    {
      fwrite8(f, songlen + 1);
      fwrite(orderlist[c], songlen, 1, f);
      fwrite8(f, LOOPSONG);
      fwrite8(f, 0);
    }

    // Instruments
    fwrite8(f, numinstr);
    for (c = 0; c < numinstr; c++)
    {
      int chn = instrchn[c];
      const PRESET *pr;
      int wptr, pptr = 0;
      char iname[MAX_INSTRNAMELEN + 1];

      if (chn == DRUMCHANNEL) pr = &drumpreset;
      else if ((firstprogram[chn] >= 32) && (firstprogram[chn] <= 39)) pr = &basspreset;
      else pr = &presets[(others++) % NUMPRESETS];

      // Wavetable: the waveform (and a fixed note for drums), then stop
      wptr = tablelen[WTBL] + 1;
      ltable[WTBL][tablelen[WTBL]] = pr->wave;
      rtable[WTBL][tablelen[WTBL]] = pr->absnote ? pr->absnote : 0x00;
      tablelen[WTBL]++;
      ltable[WTBL][tablelen[WTBL]] = 0xff;
      rtable[WTBL][tablelen[WTBL]] = 0x00;
      tablelen[WTBL]++;

      // Pulsetable: set the pulse width, then stop
      if (pr->pulse)
      {
        pptr = tablelen[PTBL] + 1;
        ltable[PTBL][tablelen[PTBL]] = 0x80 | ((pr->pulse >> 8) & 0x0f);
        rtable[PTBL][tablelen[PTBL]] = pr->pulse & 0xff;
        tablelen[PTBL]++;
        ltable[PTBL][tablelen[PTBL]] = 0xff;
        rtable[PTBL][tablelen[PTBL]] = 0x00;
        tablelen[PTBL]++;
      }

      fwrite8(f, pr->ad);
      fwrite8(f, pr->sr);
      fwrite8(f, wptr);
      fwrite8(f, pptr);
      fwrite8(f, 0);              // Filtertable
      fwrite8(f, 0);              // Speedtable
      fwrite8(f, 0);              // Vibrato delay
      fwrite8(f, 2);              // Gateoff timer (hard restart)
      fwrite8(f, 0x09);           // First frame waveform
      memset(iname, 0, sizeof iname);
      snprintf(iname, sizeof iname, "Ch%d %s", chn + 1, pr->name);
      fwrite(iname, MAX_INSTRNAMELEN, 1, f);
    }

    // Tables
    for (c = 0; c < MAX_TABLES; c++)
    {
      fwrite8(f, tablelen[c]);
      fwrite(ltable[c], tablelen[c], 1, f);
      fwrite(rtable[c], tablelen[c], 1, f);
    }

    // Patterns
    fwrite8(f, numpatterns);
    for (c = 0; c < numpatterns; c++)
    {
      fwrite8(f, patternlength + 1);
      fwrite(&patterns[c*(patternlength+1)*4], (patternlength + 1) * 4, 1, f);
    }
  }
  if (fclose(f))
  {
    setreport(report, reportsize, "Could not write the song file");
    goto done;
  }

  sprintf(msg, "%d notes, %d patterns, tempo %d", numplaced, numpatterns, firsttempo);
  if (dropped) sprintf(msg + strlen(msg), ", %d left out", dropped);
  if (clampedtempos) sprintf(msg + strlen(msg), ", tempo out of range");
  setreport(report, reportsize, msg);
  ok = 1;

done:
  for (c = 0; c < MAX_CHN; c++) free(grid[c]);
  free(patterns);
  free(placed);
  free(notes);
  free(tempos);
  free(data);
  notes = NULL;
  tempos = NULL;
  return ok;
}

void midi2sng_defaults(MIDI2SNG_OPTIONS *opt)
{
  opt->rowsperbeat = 4;
  opt->patternlength = 64;
  opt->transpose = 0;
  opt->drums = 0;
  opt->channelmask = 0;
  opt->ntsc = 0;
}
