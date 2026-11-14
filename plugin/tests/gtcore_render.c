/*
 * Test program for the GoatTracker core library without a plugin host.
 *
 *   gtcore_render <gtcore library> <resource dir> <song> <out.wav> [options]
 *
 * Loads a private copy of the library (as the plugin does), opens the song
 * (as if dropped on the editor), then renders it following a simulated host
 * transport and writes a 16-bit stereo WAV file.
 *
 * Options:
 *   -bpm=N       host tempo (0 = no host tempo: the song's own speed)
 *   -sync=0/1    DAW SYNC on or off (default on)
 *   -rate=N      sample rate (default 44100)
 *   -block=N     block size (default 512; 0 = random sizes 1..4096)
 *   -seconds=N   length (default 10)
 *   -start=N     host position at the start, in beats (default 0)
 *   -jump=B:P    at beat B jump the host to beat P (a loop / locate)
 *   -stop=B      stop the host transport at beat B
 *   -midi=B:C:N  a MIDI note (channel C 1-16, note N) at beat B, one beat long
 *   -instances=N load N copies and render each (isolation test)
 *   -frame=out.bmp  save the editor's picture after loading
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "gtcore_api.h"

#ifdef _WIN32
#include <windows.h>
typedef HMODULE LIBHANDLE;
static LIBHANDLE openlib(const char *path) { return LoadLibraryA(path); }
static void *libsym(LIBHANDLE h, const char *name) { return (void *)GetProcAddress(h, name); }
static void closelib(LIBHANDLE h) { FreeLibrary(h); }
static void sleepms(int ms) { Sleep(ms); }
static int copyfile(const char *from, const char *to) { return CopyFileA(from, to, FALSE) ? 0 : -1; }
#else
#include <dlfcn.h>
#include <unistd.h>
typedef void *LIBHANDLE;
static LIBHANDLE openlib(const char *path) { return dlopen(path, RTLD_NOW | RTLD_LOCAL); }
static void *libsym(LIBHANDLE h, const char *name) { return dlsym(h, name); }
static void closelib(LIBHANDLE h) { dlclose(h); }
static void sleepms(int ms) { usleep(ms * 1000); }
static int copyfile(const char *from, const char *to)
{
  FILE *in = fopen(from, "rb");
  FILE *out;
  char buf[65536];
  size_t n;
  if (!in) return -1;
  out = fopen(to, "wb");
  if (!out) { fclose(in); return -1; }
  while ((n = fread(buf, 1, sizeof buf, in)) > 0) fwrite(buf, 1, n, out);
  fclose(in);
  fclose(out);
  return 0;
}
#endif

typedef struct
{
  LIBHANDLE lib;
  int (*apiversion)(void);
  int (*create)(const char *, const char *, const gtcore_callbacks *, int, int, float);
  int (*destroy)(void);
  void (*prepare)(double, int);
  void (*process)(float *, float *, int, const gtcore_transport *, const gtcore_midi *, int);
  void (*input)(const gtcore_inputevent *);
  void (*editorvisible)(int);
  unsigned (*getframe)(unsigned, void *, int, int, int, int *, int *);
  int (*getstate)(void **, int *);
  void (*freedata)(void *);
  int (*setstate)(const void *, int);
} CORE;

static int loadcore(CORE *core, const char *path, int copy)
{
  char copypath[1024];
  const char *loadpath = path;

  memset(core, 0, sizeof *core);
  if (copy >= 0)
  {
#ifdef _WIN32
    char temp[MAX_PATH];
    GetTempPathA(MAX_PATH, temp);
    snprintf(copypath, sizeof copypath, "%sgtcore-test-%d-%d.dll", temp, (int)GetCurrentProcessId(), copy);
#else
    snprintf(copypath, sizeof copypath, "/tmp/gtcore-test-%d-%d.dylib", (int)getpid(), copy);
#endif
    if (copyfile(path, copypath))
    {
      fprintf(stderr, "Can't copy %s\n", path);
      return 0;
    }
    loadpath = copypath;
  }
  core->lib = openlib(loadpath);
  if (!core->lib)
  {
    fprintf(stderr, "Can't load %s\n", loadpath);
    return 0;
  }
#define SYM(field, name) *(void **)&core->field = libsym(core->lib, name); if (!core->field) { fprintf(stderr, "Missing %s\n", name); return 0; }
  SYM(apiversion, "gtcore_apiversion");
  SYM(create, "gtcore_create");
  SYM(destroy, "gtcore_destroy");
  SYM(prepare, "gtcore_prepare");
  SYM(process, "gtcore_process");
  SYM(input, "gtcore_input");
  SYM(editorvisible, "gtcore_editorvisible");
  SYM(getframe, "gtcore_getframe");
  SYM(getstate, "gtcore_getstate");
  SYM(freedata, "gtcore_free");
  SYM(setstate, "gtcore_setstate");
#undef SYM
  return 1;
}

static void writewav(const char *path, const short *data, long frames, int rate)
{
  FILE *f = fopen(path, "wb");
  unsigned v;
  if (!f) return;
#define W32(x) do { v = (x); fputc(v & 0xff, f); fputc((v >> 8) & 0xff, f); fputc((v >> 16) & 0xff, f); fputc((v >> 24) & 0xff, f); } while (0)
#define W16(x) do { v = (x); fputc(v & 0xff, f); fputc((v >> 8) & 0xff, f); } while (0)
  fwrite("RIFF", 4, 1, f);
  W32(36 + frames * 4);
  fwrite("WAVEfmt ", 8, 1, f);
  W32(16); W16(1); W16(2); W32(rate); W32(rate * 4); W16(4); W16(16);
  fwrite("data", 4, 1, f);
  W32(frames * 4);
  fwrite(data, 4, frames, f);
  fclose(f);
}

static void savebmp(const char *path, const unsigned *pixels, int w, int h)
{
  FILE *f = fopen(path, "wb");
  unsigned v;
  int y;
  int rowbytes = w * 3;
  int pad = (4 - rowbytes % 4) % 4;
  if (!f) return;
  fputc('B', f); fputc('M', f);
  W32(54 + (rowbytes + pad) * h); W32(0); W32(54); W32(40); W32(w); W32(h); W16(1); W16(24); W32(0);
  W32((rowbytes + pad) * h); W32(2835); W32(2835); W32(0); W32(0);
  for (y = h - 1; y >= 0; y--)
  {
    int x;
    for (x = 0; x < w; x++)
    {
      unsigned p = pixels[y * w + x];
      fputc(p & 0xff, f); fputc((p >> 8) & 0xff, f); fputc((p >> 16) & 0xff, f);
    }
    for (x = 0; x < pad; x++) fputc(0, f);
  }
  fclose(f);
}

int main(int argc, char **argv)
{
  const char *libpath, *resdir, *song, *outpath;
  double bpm = 125.0;
  int sync = 1;
  int rate = 44100;
  int block = 512;
  double seconds = 10.0;
  double startbeat = 0.0;
  double jumpat = -1.0, jumpto = 0.0;
  double stopat = -1.0;
  double midibeat = -1.0;
  int midich = 1, midinote = 60;
  int instances = 1;
  const char *framepath = NULL;
  const char *statepath = NULL;
  const char *song2 = NULL;
  int resizew = 0, resizeh = 0;
  float resizes = 1.0f;
  int i, inst;
  CORE cores[8];

  if (argc < 5)
  {
    fprintf(stderr, "Usage: gtcore_render <gtcore library> <resource dir> <song> <out.wav> [options]\n");
    return 1;
  }
  libpath = argv[1];
  resdir = argv[2];
  song = argv[3];
  outpath = argv[4];
  for (i = 5; i < argc; i++)
  {
    if (!strncmp(argv[i], "-bpm=", 5)) bpm = atof(argv[i] + 5);
    else if (!strncmp(argv[i], "-sync=", 6)) sync = atoi(argv[i] + 6);
    else if (!strncmp(argv[i], "-rate=", 6)) rate = atoi(argv[i] + 6);
    else if (!strncmp(argv[i], "-block=", 7)) block = atoi(argv[i] + 7);
    else if (!strncmp(argv[i], "-seconds=", 9)) seconds = atof(argv[i] + 9);
    else if (!strncmp(argv[i], "-start=", 7)) startbeat = atof(argv[i] + 7);
    else if (!strncmp(argv[i], "-jump=", 6)) sscanf(argv[i] + 6, "%lf:%lf", &jumpat, &jumpto);
    else if (!strncmp(argv[i], "-stop=", 6)) stopat = atof(argv[i] + 6);
    else if (!strncmp(argv[i], "-midi=", 6)) sscanf(argv[i] + 6, "%lf:%d:%d", &midibeat, &midich, &midinote);
    else if (!strncmp(argv[i], "-instances=", 11)) instances = atoi(argv[i] + 11);
    else if (!strncmp(argv[i], "-frame=", 7)) framepath = argv[i] + 7;
    else if (!strncmp(argv[i], "-state=", 7)) statepath = argv[i] + 7;
    else if (!strncmp(argv[i], "-song2=", 7)) song2 = argv[i] + 7;
    else if (!strncmp(argv[i], "-resize=", 8)) sscanf(argv[i] + 8, "%d:%d:%f", &resizew, &resizeh, &resizes);
  }
  if (instances < 1) instances = 1;
  if (instances > 8) instances = 8;
  (void)sync;

  for (inst = 0; inst < instances; inst++)
  {
    gtcore_inputevent in;
    char cfgdir[1024];

    if (!loadcore(&cores[inst], libpath, inst)) return 1;
#ifdef _WIN32
    GetTempPathA(sizeof cfgdir, cfgdir);
#else
    strcpy(cfgdir, "/tmp/");
#endif
    if (cores[inst].apiversion() != GTCORE_API_VERSION)
    {
      fprintf(stderr, "API version mismatch\n");
      return 1;
    }
    if (!cores[inst].create(resdir, cfgdir, NULL, 1200, 777, 1.0f))
    {
      fprintf(stderr, "gtcore_create failed\n");
      return 1;
    }
    cores[inst].prepare(rate, block > 0 ? block : 4096);
    cores[inst].editorvisible(framepath != NULL);
    memset(&in, 0, sizeof in);
    in.type = GTIN_DROPFILE;
    // -song2: the other instances get another song (isolation test)
    in.text = ((inst > 0) && (song2)) ? song2 : song;
    cores[inst].input(&in);
    if (resizew > 0)
    {
      memset(&in, 0, sizeof in);
      in.type = GTIN_RESIZE;
      in.w = resizew;
      in.h = resizeh;
      in.scale = resizes;
      cores[inst].input(&in);
    }
  }
  // Time for the songs to load (on the GoatTracker threads)
  sleepms(1500);

  if (framepath)
  {
    static unsigned pixels[4096 * 2400];
    int w = 0, h = 0;
    cores[0].getframe(0, NULL, 0, 0, 0, &w, &h);
    if ((w > 0) && (w <= 4096) && (h <= 2400) && (cores[0].getframe(0, pixels, w * 4, w, h, &w, &h)))
      savebmp(framepath, pixels, w, h);
    else fprintf(stderr, "No frame\n");
  }

  for (inst = 0; inst < instances; inst++)
  {
    long total = (long)(seconds * rate);
    long done = 0;
    short *out = (short *)malloc(total * 4);
    float left[4096], right[4096];
    double beat = startbeat;
    int playing = 1;
    int jumped = 0;
    char path[1024];
    unsigned seed = 12345;
    double peak = 0.0, sum = 0.0;

    while (done < total)
    {
      gtcore_transport tp;
      gtcore_midi midi[2];
      int nmidi = 0;
      int n = block;
      double blockbeats;

      if (n <= 0)
      {
        seed = seed * 1103515245 + 12345;
        n = 1 + (int)((seed >> 8) % 4096);
      }
      if (n > total - done) n = (int)(total - done);
      blockbeats = (bpm > 0.0) ? n * bpm / 60.0 / rate : 0.0;

      if ((stopat >= 0.0) && (beat >= stopat)) playing = 0;
      if ((!jumped) && (jumpat >= 0.0) && (beat >= jumpat))
      {
        beat = jumpto;
        jumped = 1;
      }
      memset(&tp, 0, sizeof tp);
      tp.valid = (bpm > 0.0);
      tp.playing = playing;
      tp.bpm = bpm;
      tp.ppq = beat;
      tp.timesignum = 4;
      tp.timesigden = 4;

      if ((midibeat >= 0.0) && (bpm > 0.0))
      {
        if ((midibeat >= beat) && (midibeat < beat + blockbeats))
        {
          midi[nmidi].offset = (int)((midibeat - beat) * 60.0 / bpm * rate);
          midi[nmidi].status = 0x90 | ((midich - 1) & 15);
          midi[nmidi].data1 = midinote;
          midi[nmidi].data2 = 100;
          nmidi++;
        }
        if ((midibeat + 1.0 >= beat) && (midibeat + 1.0 < beat + blockbeats))
        {
          midi[nmidi].offset = (int)((midibeat + 1.0 - beat) * 60.0 / bpm * rate);
          midi[nmidi].status = 0x80 | ((midich - 1) & 15);
          midi[nmidi].data1 = midinote;
          midi[nmidi].data2 = 0;
          nmidi++;
        }
      }

      cores[inst].process(left, right, n, &tp, midi, nmidi);
      for (i = 0; i < n; i++)
      {
        float l = left[i], r = right[i];
        if (fabs(l) > peak) peak = fabs(l);
        sum += l * l;
        out[(done + i) * 2] = (short)(l * 32767.0f);
        out[(done + i) * 2 + 1] = (short)(r * 32767.0f);
      }
      done += n;
      if (playing) beat += blockbeats;
    }
    if (instances > 1)
    {
      snprintf(path, sizeof path, "%s.%d.wav", outpath, inst);
      writewav(path, out, total, rate);
    }
    else writewav(outpath, out, total, rate);
    printf("instance %d: peak %.3f rms %.4f\n", inst, peak, sqrt(sum / total));
    free(out);
  }

  for (inst = 0; inst < instances; inst++)
  {
    void *state;
    int size;
    // Time for recorded notes to reach the patterns
    sleepms(300);
    if (cores[inst].getstate(&state, &size))
    {
      printf("instance %d: state %d bytes\n", inst, size);
      if ((statepath) && (!inst))
      {
        FILE *f = fopen(statepath, "wb");
        if (f)
        {
          fwrite(state, 1, size, f);
          fclose(f);
        }
      }
      cores[inst].freedata(state);
    }
    printf("instance %d: destroy -> %d\n", inst, cores[inst].destroy());
  }
  return 0;
}
