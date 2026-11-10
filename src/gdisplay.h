#ifndef GDISPLAY_H
#define GDISPLAY_H

// Colors of the classic text screen (used by the classic dialogs)
#define CNORMAL 8
#define CMUTE 3
#define CEDIT 10
#define CPLAYING 12
#define CCOMMAND 7
#define CTITLE 15

#ifdef __cplusplus
extern "C" {
#endif

void printmainscreen(void);
void displayupdate(void);
void followplayback(void);
void resettime(void);
void incrementtime(void);

#ifndef GDISPLAY_C
extern char *notename[];
extern int timemin, timesec, timeframe;
extern int followsuspended;
#endif

#ifdef __cplusplus
}
#endif

#endif
