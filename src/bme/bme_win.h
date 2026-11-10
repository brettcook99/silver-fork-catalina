// BME windows & timing module header file (SDL2 version: input and the
// window are handled by the GUI layer)

int win_openwindow(char *appname, char *icon);
void win_closewindow(void);
void win_messagebox(char *string);
int win_getspeed(int framerate);

extern int win_windowinitted;
extern int win_quitted;
extern int win_fullscreen;
