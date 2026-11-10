//
// BME (Blasphemous Multimedia Engine) windows & timing module
//
// SDL2 version: the window, the event loop and all input now live in the
// GUI layer (gui/gui_main.cpp). This module only keeps the shared state the
// rest of the program still reads, plus frame timing.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>
#include "bme_main.h"
#include "bme_cfg.h"
#include "bme_err.h"
#include "bme_win.h"

// Global variables

int win_fullscreen = 0;         // Fullscreen (saved in the config file)
int win_windowinitted = 0;
int win_quitted = 0;            // Set when the user closes the window / quits

// Static variables

static Uint32 win_lasttime = 0;
static int win_framecounter = 0;

int win_openwindow(char *appname, char *icon)
{
    (void)appname;
    (void)icon;

    if (!win_windowinitted)
    {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) < 0)
            return BME_ERROR;
        atexit(SDL_Quit);
        win_windowinitted = 1;
    }
    return BME_OK;
}

void win_closewindow(void)
{
}

void win_messagebox(char *string)
{
    (void)string;
}

// Returns how many frames of the given rate have passed since the last call
// (0 or more). Does not wait.
int win_getspeed(int framerate)
{
    Uint32 now = SDL_GetTicks();
    int frametime = 10000 / framerate;
    int frames;

    if (!win_lasttime) win_lasttime = now;
    win_framecounter += (int)(now - win_lasttime) * 10;
    win_lasttime = now;

    frames = win_framecounter / frametime;
    win_framecounter -= frames * frametime;
    return frames;
}
