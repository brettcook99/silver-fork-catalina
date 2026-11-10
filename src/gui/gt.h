// Includes the GoatTracker C headers in the C++ GUI files.
//
// The system and SDL headers are included first, normally: some C library
// headers (e.g. math.h with libc++) define C++ overloads and must not end up
// inside extern "C". Their include guards then make the copies inside
// goattrk2.h no-ops.

#ifndef GUI_GT_H
#define GUI_GT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <SDL.h>

extern "C" {
#include "../goattrk2.h"
#include "../gcommand.h"
#include "../gfileio.h"
}

#include "gui.h"

#endif
