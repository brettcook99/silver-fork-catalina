/*
 * MIDI file (.mid) -> GoatTracker song (.sng) converter.
 * The conversion itself is in gmidifile.c (shared with the tracker's
 * File > Import MIDI).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gmidifile.h"

static void usage(void)
{
  printf("Usage: midi2sng <midi> <sng> [options]\n"
         "Options:\n"
         "-rxx Pattern rows per quarter note (1-32) DEFAULT=4\n"
         "-lxx Pattern length in rows (16-128) DEFAULT=64\n"
         "-txx Transpose in halfsteps (e.g. -t-12 = octave down) DEFAULT=0\n"
         "-cxx MIDI channels to use, e.g. -c1,2,4 DEFAULT=all\n"
         "-d   Include drums (MIDI channel 10)\n"
         "-n   Tempo for NTSC (60Hz) playback\n"
         "-s   Split: MIDI channels 1-3 to SID channels 1-3, one note at a time\n"
         "-vxx Only these SID channels, the notes spread over them, e.g. -v2,3\n"
         "     (-v3: all notes to SID channel 3, one at a time)\n"
         "\n"
         "The notes go to the 3 SID channels. When more than 3 notes play at\n"
         "once, the highest and lowest new notes win and the rest are left out.\n"
         "Every MIDI channel gets an instrument of its own. MIDI note 60 (middle\n"
         "C) is C-4. When all notes are on one MIDI channel in several tracks,\n"
         "the tracks are read as channels 1, 2, 3...\n");
}

int main(int argc, char **argv)
{
  MIDI2SNG_OPTIONS opt;
  char report[256];
  const char *in = NULL, *out = NULL;
  int c;

  midi2sng_defaults(&opt);

  for (c = 1; c < argc; c++)
  {
    if ((argv[c][0] == '-') && (argv[c][1]))
    {
      switch (argv[c][1])
      {
        case 'r':
        opt.rowsperbeat = atoi(&argv[c][2]);
        break;

        case 'l':
        opt.patternlength = atoi(&argv[c][2]);
        break;

        case 't':
        opt.transpose = atoi(&argv[c][2]);
        break;

        case 'c':
        {
          char *s = &argv[c][2];
          while (*s)
          {
            int chn = (int)strtol(s, &s, 10);
            if ((chn >= 1) && (chn <= 16)) opt.channelmask |= 1u << (chn - 1);
            else
            {
              printf("Illegal MIDI channel list %s\n", &argv[c][2]);
              return EXIT_FAILURE;
            }
            while ((*s == ',') || (*s == ' ')) s++;
          }
        }
        break;

        case 'd':
        opt.drums = 1;
        break;

        case 'n':
        opt.ntsc = 1;
        break;

        case 's':
        opt.split = 1;
        break;

        case 'v':
        {
          char *s = &argv[c][2];
          while (*s)
          {
            int chn = (int)strtol(s, &s, 10);
            if ((chn >= 1) && (chn <= 3)) opt.voicemask |= 1u << (chn - 1);
            else
            {
              printf("Illegal SID channel list %s\n", &argv[c][2]);
              return EXIT_FAILURE;
            }
            while ((*s == ',') || (*s == ' ')) s++;
          }
        }
        break;

        default:
        usage();
        return EXIT_FAILURE;
      }
    }
    else if (!in) in = argv[c];
    else if (!out) out = argv[c];
  }

  if ((!in) || (!out))
  {
    usage();
    return EXIT_FAILURE;
  }

  // Channel 10 given in the list means drums are wanted
  if (opt.channelmask & (1u << 9)) opt.drums = 1;

  if (!midi2sng(in, out, &opt, report, sizeof report))
  {
    printf("Error: %s\n", report);
    return EXIT_FAILURE;
  }
  printf("Converted successfully: %s\n", report);
  return EXIT_SUCCESS;
}
