//
// GOATTRACKER v2 song (.sng) writer, shared by the importers
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gcommon.h"
#include "gsngwrite.h"

SNGDATA *sngdata_alloc(void)
{
  SNGDATA *d = (SNGDATA *)malloc(sizeof(SNGDATA));
  if (d) sngdata_clear(d);
  return d;
}

void sngdata_clear(SNGDATA *d)
{
  memset(d, 0, sizeof(SNGDATA));
}

void sngdata_settext(char *field, const char *text)
{
  int c;
  memset(field, 0, MAX_STR);
  if (!text) return;
  for (c = 0; (c < MAX_STR) && (text[c]); c++) field[c] = text[c];
}

static void fwrite8(FILE *f, int value)
{
  fputc(value & 0xff, f);
}

static int validate(const SNGDATA *d)
{
  int s, c;

  if ((d->numsongs < 1) || (d->numsongs > MAX_SONGS)) return 0;
  for (s = 0; s < d->numsongs; s++)
  {
    for (c = 0; c < MAX_CHN; c++)
    {
      if ((d->songlen[s][c] < 1) || (d->songlen[s][c] > MAX_SONGLEN)) return 0;
    }
  }
  if ((d->numinstr < 0) || (d->numinstr >= MAX_INSTR)) return 0;
  for (c = 0; c < MAX_TABLES; c++)
  {
    if ((d->tablelen[c] < 0) || (d->tablelen[c] > MAX_TABLELEN)) return 0;
  }
  if ((d->numpatterns < 0) || (d->numpatterns > MAX_PATT)) return 0;
  for (c = 0; c < d->numpatterns; c++)
  {
    if ((d->pattlen[c] < 1) || (d->pattlen[c] > MAX_PATTROWS)) return 0;
  }
  return 1;
}

int sngwrite(const char *path, const SNGDATA *d)
{
  FILE *f;
  int s, c;
  static const unsigned char endrow[4] = {ENDPATT, 0, 0, 0};

  if (!validate(d)) return 0;

  f = fopen(path, "wb");
  if (!f) return 0;

  fwrite("GTS5", 4, 1, f);
  fwrite(d->songname, MAX_STR, 1, f);
  fwrite(d->authorname, MAX_STR, 1, f);
  fwrite(d->copyrightname, MAX_STR, 1, f);

  // Orderlists: length (with the endmark), the entries, endmark, restart
  fwrite8(f, d->numsongs);
  for (s = 0; s < d->numsongs; s++)
  {
    for (c = 0; c < MAX_CHN; c++)
    {
      int len = d->songlen[s][c];
      fwrite8(f, len + 1);
      fwrite(d->songorder[s][c], len, 1, f);
      fwrite8(f, LOOPSONG);
      fwrite8(f, d->songorder[s][c][len + 1]);
    }
  }

  // Instruments 1-n
  fwrite8(f, d->numinstr);
  for (c = 1; c <= d->numinstr; c++)
  {
    const INSTR *i = &d->instr[c];
    fwrite8(f, i->ad);
    fwrite8(f, i->sr);
    fwrite8(f, i->ptr[WTBL]);
    fwrite8(f, i->ptr[PTBL]);
    fwrite8(f, i->ptr[FTBL]);
    fwrite8(f, i->ptr[STBL]);
    fwrite8(f, i->vibdelay);
    fwrite8(f, i->gatetimer);
    fwrite8(f, i->firstwave);
    fwrite(i->name, MAX_INSTRNAMELEN, 1, f);
  }

  // Tables: length, left side, right side
  for (c = 0; c < MAX_TABLES; c++)
  {
    fwrite8(f, d->tablelen[c]);
    fwrite(d->ltable[c], d->tablelen[c], 1, f);
    fwrite(d->rtable[c], d->tablelen[c], 1, f);
  }

  // Patterns: length (with the end row), rows, end row
  fwrite8(f, d->numpatterns);
  for (c = 0; c < d->numpatterns; c++)
  {
    fwrite8(f, d->pattlen[c] + 1);
    fwrite(d->pattern[c], d->pattlen[c] * 4, 1, f);
    fwrite(endrow, 4, 1, f);
  }

  if (ferror(f))
  {
    fclose(f);
    return 0;
  }
  if (fclose(f)) return 0;
  return 1;
}
