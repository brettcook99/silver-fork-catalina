#ifndef GMACFILE_H
#define GMACFILE_H

// Native macOS open & save panels (NSOpenPanel / NSSavePanel)

#ifdef __APPLE__
// exts: extensions without dots separated by ';' (NULL = any file).
// startdir may be NULL. Return 1 with the full path, 0 if cancelled.
int macfile_open(const char *title, const char *exts, const char *startdir, char *path, int pathsize);
int macfile_save(const char *title, const char *ext, const char *suggestedname, const char *startdir, char *path, int pathsize);
#endif

#endif
