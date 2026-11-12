/*
 * C64 music file (.sid) -> GoatTracker song (.sng) converter.
 * The conversion itself is in gsidimport.c (shared with the tracker's
 * SID import).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gsidimport.h"

static void usage(void)
{
  printf("Usage: sid2sng <sid> <sng> [options]\n"
         "Options:\n"
         "-exact   Only the exact conversion (SIDs made with GoatTracker 2)\n"
         "-approx  Only the approximate conversion (any SID, by playing it)\n"
         "-sxx     Only this subtune with -approx (1-n) DEFAULT=all (up to 32)\n"
         "-txx     Seconds of each subtune with -approx, when it doesn't loop (1-1800)\n"
         "         DEFAULT=180\n"
         "-rxx     Rows per most common note length with -approx DEFAULT=automatic\n"
         "-fxx     Frames per row (tempo) with -approx (3-127) DEFAULT=automatic\n"
         "-lxx     Pattern length in rows with -approx (16-128) DEFAULT=64\n"
         "-noloop  With -approx, don't look for loops: keep -t seconds of each\n"
         "         subtune (by default each one is played up to 10 minutes to\n"
         "         find where it starts over; it then ends there and loops back)\n"
         "-i       Only show the SID file's information\n"
         "\n"
         "By default the exact conversion is tried first: it gives back the\n"
         "original song (all subtunes, patterns, instruments and tables) of a\n"
         "SID made with GoatTracker 2's packer. Other SIDs are played on an\n"
         "emulated C64, subtune by subtune, and the notes & sounds are turned\n"
         "into a song that sounds roughly alike, as a starting point for\n"
         "editing.\n");
}

static void showinfo(const char *name, const SIDINFO *i)
{
  static const char *clocks[] = {"unknown", "PAL", "NTSC", "PAL/NTSC"};
  static const char *models[] = {"unknown", "6581", "8580", "6581/8580"};
  printf("%s: %s v%d\n", name, i->rsid ? "RSID" : "PSID", i->version);
  printf("Title:      %s\n", i->title);
  printf("Author:     %s\n", i->author);
  printf("Released:   %s\n", i->released);
  printf("Songs:      %d (start song %d)\n", i->songs, i->startsong);
  printf("Clock:      %s\n", clocks[i->clock & 3]);
  printf("SID model:  %s\n", models[i->model & 3]);
  printf("Load:       $%04X-$%04X\n", i->loadaddress, i->loadaddress + i->datasize - 1);
  printf("Init:       $%04X\n", i->initaddress);
  printf("Play:       $%04X\n", i->playaddress);
}

int main(int argc, char **argv)
{
  SID2SNG_OPTIONS opt;
  SID2SNG_PLAYBACK pb;
  char report[512];
  const char *in = NULL, *out = NULL;
  int infoonly = 0;
  int c;

  sid2sng_defaults(&opt);

  for (c = 1; c < argc; c++)
  {
    const char *a = argv[c];
    if ((a[0] == '-') && (a[1]))
    {
      if (!strcmp(a, "-exact")) opt.mode = SID2SNG_EXACT;
      else if (!strcmp(a, "-approx")) opt.mode = SID2SNG_APPROX;
      else if (!strcmp(a, "-i")) infoonly = 1;
      else if (!strcmp(a, "-noloop")) opt.loops = 0;
      else
      {
        switch (a[1])
        {
          case 's':
          opt.subtune = atoi(&a[2]);
          break;

          case 't':
          opt.seconds = atoi(&a[2]);
          break;

          case 'r':
          opt.rowsperbeat = atoi(&a[2]);
          break;

          case 'f':
          opt.tempo = atoi(&a[2]);
          break;

          case 'l':
          opt.patternlength = atoi(&a[2]);
          break;

          default:
          usage();
          return EXIT_FAILURE;
        }
      }
    }
    else if (!in) in = a;
    else if (!out) out = a;
  }

  if ((in) && (infoonly))
  {
    SIDINFO info;
    if (!sidinfo_read(in, &info))
    {
      printf("Error: %s is not a SID file\n", in);
      return EXIT_FAILURE;
    }
    showinfo(in, &info);
    return EXIT_SUCCESS;
  }

  if ((!in) || (!out))
  {
    usage();
    return EXIT_FAILURE;
  }

  if (!sid2sng_play(in, out, &opt, &pb, report, sizeof report))
  {
    printf("Error: %s\n", report);
    return EXIT_FAILURE;
  }
  printf("Converted successfully: %s\n", report);
  // The editor settings the song needs (not stored in it)
  printf("Play with:  ");
  if (pb.multiplier > 0) printf("speed multiplier %d", pb.multiplier);
  else if (!pb.multiplier) printf("speed multiplier 0 (25Hz)");
  else printf("speed multiplier: none fits");
  printf(", %s", pb.ntsc ? "NTSC" : "PAL");
  if (pb.adparam >= 0) printf(", hardrestart ADSR %04X", pb.adparam);
  if (pb.optimizepulse >= 0) printf(", pulse optimization %s", pb.optimizepulse ? "on" : "off");
  if (pb.optimizerealtime >= 0) printf(", realtime optimization %s", pb.optimizerealtime ? "on" : "off");
  if (pb.basepitch > 0.0) printf(", base pitch %.2f", pb.basepitch);
  else if (pb.basepitch == 0.0) printf(", default frequency table");
  printf("\n");
  return EXIT_SUCCESS;
}
