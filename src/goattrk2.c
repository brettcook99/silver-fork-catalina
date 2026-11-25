//
// GOATTRACKER v2.82
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//

#define GOATTRK2_C

#ifdef __WIN32__
#include <windows.h>
#endif

#include "goattrk2.h"
#include "bme.h"
#include "gui/gui.h"
#include "gmacfile.h"
#include "gswm.h"
#include "gmidiexport.h"

static void mainloopframe(void);
static void retromainloop(void);
static int chooseinterface(void);
static void presskey(int newrawkey, int newkey, int shift);
static void startplay(int mode);
static void midichordassign(void);
static void resosync(void);

int menu = 0;
//int editorInfo.editmode = EDIT_PATTERN;
// Edit mode (notes are entered) or jam mode (0: notes are only played). The
// program starts in jam mode
int recordmode = 0;

// MIDI input (MIDI INPUT 1-3 PATTERNS). With 2 or 3 the MIDI notes go to
// that many channels, from the cursor's channel on (after channel 3 comes
// channel 1), so that they can ring on together:
// - a note played on its own goes to the next of these channels in turn
//   (ping-pong), on the cursor's row, and the cursor moves down
// - keys struck together (a chord) go to the next channels too, all on the
//   row of the first one; keys beyond the last channel are left out
// The turn starts again at the cursor's channel when the cursor has been
// moved. 1 = the usual input: every note goes to the cursor's channel
int midichord = 1;
// STEPS (0-16): rows skipped after a note is entered, so that the cursor
// moves down 1 + editstep rows
int editstep = 0;
// RESO: the rows per beat times 1 (off), 2 (X2) or 3 (X3), see setresomode()
int resomode = 1;
// Where the current MIDI note-on goes (set by midichordassign()): the
// channel (-1 = left out), the row, whether it starts a new row (the
// cursor moves down after it) and the channels of the chord's earlier notes
// (bit per channel)
int midichordchn = 0;
int midichordrow = 0;
int midichordfirst = 1;
int midichordmask = 0;
// Nonzero when the MIDI note has been played already, as it came (see
// midinotehook()): the editor only enters it
int midinoteplayed = 0;
// Nonzero when the MIDI note is recorded on the row it sounded in, while
// following the playback (see patternrecording()): the pattern, row and
// the transpose it plays with there
int midirecord = 0;
int midirecpatt = 0;
int midirecrow = 0;
int midirectrans = 0;
static int midinotehook(int type, int channel, int midinote, unsigned time);
int followplay = 0;
int followmode = 1;
int hexnybble = -1;
int stepsize = 4;
int autoadvance = 0;
int defaultpatternlength = 64;
int cursorflash = 0;
int cursorcolortable[] = { 1,2,7,2 };
int exitprogram = 0;
//int eacolumn = 0;
int eamode = 0;

unsigned keypreset = KEY_TRACKER;
unsigned playerversion = 0;
int fileformat = FORMAT_SID;
int zeropageadr = 0xfc;
int playeradr = 0x1000;
//unsigned editorInfo.sidmodel = 0;
//unsigned multiplier = 1;
//unsigned adparam = 0x0f00;
//unsigned ntsc = 0;
unsigned patterndispmode = 0;
unsigned sidaddress = 0xd400;
//unsigned finevibrato = 1;
//unsigned optimizepulse = 1;
//unsigned optimizerealtime = 1;
unsigned customclockrate = 0;
//unsigned usefinevib = 0;
unsigned b = DEFAULTBUF;
unsigned mr = DEFAULTMIXRATE;
// The audio output chosen in the preferences: the buffer length in samples
// (0 = b milliseconds) and the device's name ("" = the system's default)
unsigned audiobuffer = 32;
char audiodevice[MAX_PATHNAME] = "";
// Lines of the config file that only the plugin uses (see the end of it)
static char pluginfolders[4][MAX_PATHNAME];
unsigned writer = 0;
unsigned hardsid = 0;
unsigned catweasel = 0;
unsigned interpolate = 0;
unsigned residdelay = 0;
unsigned hardsidbufinteractive = 20;
unsigned hardsidbufplayback = 400;
unsigned uimode = UI_RETRO;  // Interface in use: modern or retro
static int uiarg = -1;       // -u on the command line (-1 = not given)
float basepitch = 434.03f;
float equaldivisionsperoctave = 12.0f;
int tuningcount = 0;
double tuning[96];
static unsigned char defaultfreqtbllo[96], defaultfreqtblhi[96];
extern unsigned bigwindow;

char configbuf[MAX_PATHNAME];
char loadedsongfilename[MAX_FILENAME];
char songfilename[MAX_FILENAME];
char songfilter[MAX_FILENAME];
char songpath[MAX_PATHNAME];
char instrfilename[MAX_FILENAME];
char instrfilter[MAX_FILENAME];
char instrpath[MAX_PATHNAME];
// Folder shown by the SID designer's instrument browser ("" = not chosen)
char browserfolder[MAX_PATHNAME];
char packedpath[MAX_PATHNAME];
char importpath[MAX_PATHNAME];
// The folders read from the config file (see gt_folder())
static char configfolder[FOLDER_KINDS][MAX_PATHNAME];

extern char *notename[];
char *programname = PROGRAMNAME;
// For the macOS About panel (gmacmenu.m)
const char abouttext[] = ABOUTTEXT;
char specialnotenames[186];
char scalatuningfilepath[MAX_PATHNAME];
char tuningname[64];

char textbuffer[MAX_PATHNAME];

unsigned char hexkeytbl[] = { '0', '1', '2', '3', '4', '5', '6', '7',
  '8', '9', 'a', 'b', 'c', 'd', 'e', 'f' };

extern unsigned char datafile[];

char* usage[] = {
	"Usage: goattrk2 [songname] [options]",
	"Options:",
	"-Axx Set ADSR parameter for hardrestart in hex. DEFAULT=0F00",
	"-Bxx Set sound buffer length in milliseconds DEFAULT=15",
	"-Cxx Use CatWeasel MK3 PCI SID (0 = off, 1 = on)",
	"-Dxx Pattern row display (0 = decimal, 1 = hex, 2 = decimal w/dots, 3 = hex w/dots)",
	"-Exx Set emulated SID model (0 = 6581 1 = 8580) DEFAULT=8580",
	"-Fxx Set custom SID clock cycles per second (0 = use PAL/NTSC default)",
	"-Gxx Set pitch of A-4 in Hz (0 = use default frequencytable, close to 440Hz)",
	"-Hxx Use HardSID (0 = off, 1 = HardSID ID0 2 = HardSID ID1 etc.)",
	"-Ixx Set reSID interpolation (0 = off, 1 = on, 2 = distortion, 3 = distortion & on) DEFAULT=3",
	"-Jxx Set special note names (2 chars for every note in an octave/cycle, e.g. C-DbD-EbE-F-GbG-AbA-BbB-)",
	"-Kxx Note-entry mode (0 = Protracker, 1 = DMC, 2 = Janko) DEFAULT=Protracker",
	"-Lxx SID memory location in hex. DEFAULT=D400",
	"-Mxx Set sound mixing rate DEFAULT=44100",
	"-Oxx Set pulseoptimization/skipping (0 = off, 1 = on) DEFAULT=on",
	"-Qxx Set equal divisions per octave (12 = default, 8.2019143 = Bohlen-Pierce)",
	"-Rxx Set realtime-effect optimization/skipping (0 = off, 1 = on) DEFAULT=on",
	"-Sxx Set speed multiplier (0 for 25Hz, 1 for 1x, 2 for 2x etc.)",
	"-Txx Set HardSID interactive mode sound buffer length in milliseconds DEFAULT=20, max.buffering=0",
	"-Uxx Set HardSID playback mode sound buffer length in milliseconds DEFAULT=400, max.buffering=0",
	"-Vxx Set finevibrato conversion (0 = off, 1 = on) DEFAULT=on",
	"-Xxx Set window type (0 = window, 1 = fullscreen) DEFAULT=window, ALT+ENTER toggles",
	"-Yxx Path to a Scala tuning file .scl",
	"-Zxx Set random reSID write delay in cycles (0 = off) DEFAULT=off",
	"-wxx Set initial window size (1 to 4 = 1 to 4 times 800x518). The window can be resized freely",
	"-mxx MIDI keyboard input (0 = off, 1 = on) DEFAULT=on",
	"-fxx Follow playback in the pattern editor (0 = off, 1 = on) DEFAULT=on",
	"-uxx Interface (0 = ask at startup, 1 = modern, 2 = retro) DEFAULT=retro",
	"-N   Use NTSC timing",
	"-P   Use PAL timing (DEFAULT)",
	"-W   Write sound output to a file SIDAUDIO.RAW",
	"-?   Show this info again",
	"-??  Standalone online help window",
};

int usagelen = (sizeof usage / sizeof usage[0]);

// Nonzero if the path is an existing folder (a slash at the end is fine)
static int isfolder(const char *path)
{
	char dir[MAX_PATHNAME];
	struct stat st;
	int len;

	if ((!path) || (!path[0])) return 0;
	snprintf(dir, sizeof dir, "%s", path);
	len = (int)strlen(dir);
	while ((len > 1) && ((dir[len - 1] == '/') || (dir[len - 1] == '\\')) && (dir[len - 2] != ':')) dir[--len] = 0;
	if (stat(dir, &st)) return 0;
	return (st.st_mode & S_IFDIR) != 0;
}

int main(int argc, char **argv)
{
	char filename[MAX_PATHNAME];
	char startfile[MAX_PATHNAME];
	FILE *configfile;
	int c;
	int showhelpatstart = 0;

	startfile[0] = 0;

	// DEFAULT SETTINGS (those that differ from GoatTracker's are Scott's:
	// 6581, no pulse and realtime skipping, 48 kHz, no interpolation, SID
	// files from the packer, A-4 at 434.03 Hz, the gradient background and
	// his meter colors)
	editorInfo.adparam = 0x0f00;
	editorInfo.editmode = EDIT_PATTERN;
	editorInfo.esmarkchn = -1;
	editorInfo.epoctave = 2;
	editorInfo.epmarkchn = -1;
	editorInfo.etmarknum = -1;
	editorInfo.etlock = 1;
	editorInfo.ntsc = 0;
	editorInfo.multiplier = 1;
	editorInfo.sidmodel = 0;
	editorInfo.finevibrato = 1;
	editorInfo.optimizepulse = 0;
	editorInfo.optimizerealtime = 0;

	programname += sizeof "$VER:";
	// Open datafile
	io_openlinkeddatafile(datafile);

	// Load configuration
#ifdef __WIN32__
	GetModuleFileName(NULL, filename, MAX_PATHNAME);
	filename[strlen(filename) - 3] = 'c';
	filename[strlen(filename) - 2] = 'f';
	filename[strlen(filename) - 1] = 'g';
#elif __amigaos__
	strcpy(filename, "PROGDIR:goattrk2.cfg");
#else
	strcpy(filename, getenv("HOME"));
	strcat(filename, "/.goattrk/goattrk2.cfg");
#endif
	specialnotenames[0] = 0;
	scalatuningfilepath[0] = 0;
#if defined(__APPLE__) && !defined(GT_PLUGIN)
	// Scott's folders next to the program, used when they are there
	strcpy(configfolder[FOLDER_IMPORT], "/Applications/GoatTracker Ultimate/SID Music/");
	strcpy(browserfolder, "/Applications/GoatTracker Ultimate/Instruments/");
#endif
	configfile = fopen(filename, "rt");
	if (configfile)
	{
		getparam(configfile, &b);
		getparam(configfile, &mr);
		getparam(configfile, &hardsid);
		getparam(configfile, &editorInfo.sidmodel);
		getparam(configfile, &editorInfo.ntsc);
		getparam(configfile, (unsigned *)&fileformat);
		getparam(configfile, (unsigned *)&playeradr);
		getparam(configfile, (unsigned *)&zeropageadr);
		getparam(configfile, &playerversion);
		getparam(configfile, &keypreset);
		getparam(configfile, (unsigned *)&stepsize);
		getparam(configfile, &editorInfo.multiplier);
		getparam(configfile, &catweasel);
		getparam(configfile, &editorInfo.adparam);
		getparam(configfile, &interpolate);
		getparam(configfile, &patterndispmode);
		getparam(configfile, &sidaddress);
		getparam(configfile, &editorInfo.finevibrato);
		getparam(configfile, &editorInfo.optimizepulse);
		getparam(configfile, &editorInfo.optimizerealtime);
		getparam(configfile, &residdelay);
		getparam(configfile, &customclockrate);
		getparam(configfile, &hardsidbufinteractive);
		getparam(configfile, &hardsidbufplayback);
		getfloatparam(configfile, &filterparams.distortionrate);
		getfloatparam(configfile, &filterparams.distortionpoint);
		getfloatparam(configfile, &filterparams.distortioncfthreshold);
		getfloatparam(configfile, &filterparams.type3baseresistance);
		getfloatparam(configfile, &filterparams.type3offset);
		getfloatparam(configfile, &filterparams.type3steepness);
		getfloatparam(configfile, &filterparams.type3minimumfetresistance);
		getfloatparam(configfile, &filterparams.type4k);
		getfloatparam(configfile, &filterparams.type4b);
		getfloatparam(configfile, &filterparams.voicenonlinearity);
		getparam(configfile, (unsigned*)&win_fullscreen);
		getparam(configfile, &bigwindow);
		getfloatparam(configfile, &basepitch);
		getfloatparam(configfile, &equaldivisionsperoctave);
		getstringparam(configfile, specialnotenames);
		getstringparam(configfile, scalatuningfilepath);
		getparam(configfile, &windowedxsize);
		getparam(configfile, &windowedysize);
		getparam(configfile, &midiinput);
		getparam(configfile, (unsigned *)&followmode);
		getparam(configfile, &textsizepercent);
		// Older config files have the interface used last (1 or 2) here: a
		// folder line is never just that, so such a line is skipped
		getpathparam(configfile, configfolder[0]);
		if ((!strcmp(configfolder[0], "1")) || (!strcmp(configfolder[0], "2")))
			getpathparam(configfile, configfolder[0]);
		for (c = 1; c < FOLDER_KINDS; c++)
			getpathparam(configfile, configfolder[c]);
		getparam(configfile, &retrogradient);
		for (c = 0; c < 3; c++)
			getparam(configfile, &retrometercolors[c]);
		for (c = 0; c < METERPRESETS * 3; c++)
			getparam(configfile, &retrometerpresets[c / 3][c % 3]);
		getpathparam(configfile, browserfolder);
		getparam(configfile, &windowposset);
		{
			// The position may be negative (a screen left of or above the
			// main one)
			float x = (float)windowedxpos;
			float y = (float)windowedypos;
			getfloatparam(configfile, &x);
			getfloatparam(configfile, &y);
			windowedxpos = (int)x;
			windowedypos = (int)y;
		}
		// The plugin (it shares this file on macOS) saves the folders of its
		// MIDI / chipsynth import & export dialogs here: kept as they are
		for (c = 0; c < 4; c++)
			getpathparam(configfile, pluginfolders[c]);
		getparam(configfile, &audiobuffer);
		getpathparam(configfile, audiodevice);
		if (!strcmp(audiodevice, "0")) audiodevice[0] = 0;
		fclose(configfile);

		// A config file saved with an old default sound buffer gets the
		// new, shorter one (a length set by hand stays)
		if ((b == OLDDEFAULTBUF) || (b == PREVDEFAULTBUF)) b = DEFAULTBUF;
	}

	// Init pathnames. The file dialogs start in the folders used last time
	initpaths();
	strcpy(importpath, songpath);
	for (c = 0; c < FOLDER_KINDS; c++)
	{
		if ((strcmp(configfolder[c], "0")) && (isfolder(configfolder[c])))
			strcpy(gt_folder(c), configfolder[c]);
	}
	if (!isfolder(browserfolder)) browserfolder[0] = 0;

	// Scan command line
	for (c = 1; c < argc; c++)
	{
		// Old macOS versions pass a process serial number when started from
		// the Finder
		if (!strncmp(argv[c], "-psn_", 5))
			continue;

#ifdef __WIN32__
		if ((argv[c][0] == '-') || (argv[c][0] == '/'))
#else
		if (argv[c][0] == '-')
#endif
		{
			int y;
			switch (argv[c][1]) //switch (toupper(argv[c][1]))
			{
			case '-':
				if (strcmp(argv[c], "--help"))
					break;
			case '?':
				if (argv[c][2] == '?')
				{
					// Open with the help window shown
					showhelpatstart = 1;
					break;
				}
				for (y = 0; y < usagelen; ++y)
					printf("%s\n", usage[y]);
				return EXIT_SUCCESS;

			case 'Z':
				sscanf(&argv[c][2], "%u", &residdelay);
				break;

			case 'A':
				sscanf(&argv[c][2], "%x", &editorInfo.adparam);
				break;

			case 'S':
				sscanf(&argv[c][2], "%u", &editorInfo.multiplier);
				break;

			case 'B':
				sscanf(&argv[c][2], "%u", &b);
				break;

			case 'D':
				sscanf(&argv[c][2], "%u", &patterndispmode);
				break;

			case 'E':
				sscanf(&argv[c][2], "%u", &editorInfo.sidmodel);
				break;

			case 'I':
				sscanf(&argv[c][2], "%u", &interpolate);
				break;

			case 'K':
				sscanf(&argv[c][2], "%u", &keypreset);
				break;

			case 'L':
				sscanf(&argv[c][2], "%x", &sidaddress);
				break;

			case 'N':
				editorInfo.ntsc = 1;
				customclockrate = 0;
				break;

			case 'P':
				editorInfo.ntsc = 0;
				customclockrate = 0;
				break;

			case 'F':
				sscanf(&argv[c][2], "%u", &customclockrate);
				break;

			case 'M':
				sscanf(&argv[c][2], "%u", &mr);
				break;

			case 'O':
				sscanf(&argv[c][2], "%u", &editorInfo.optimizepulse);
				break;

			case 'R':
				sscanf(&argv[c][2], "%u", &editorInfo.optimizerealtime);
				break;

			case 'H':
				sscanf(&argv[c][2], "%u", &hardsid);
				break;

			case 'V':
				sscanf(&argv[c][2], "%u", &editorInfo.finevibrato);
				break;

			case 'T':
				sscanf(&argv[c][2], "%u", &hardsidbufinteractive);
				break;

			case 'U':
				sscanf(&argv[c][2], "%u", &hardsidbufplayback);
				break;

			case 'W':
				writer = 1;
				break;

			case 'X':
				sscanf(&argv[c][2], "%u", &win_fullscreen);
				break;

			case 'm':
				sscanf(&argv[c][2], "%u", &midiinput);
				break;

			case 'f':
				sscanf(&argv[c][2], "%d", &followmode);
				break;

			case 'u':
				sscanf(&argv[c][2], "%d", &uiarg);
				break;

			case 'C':
				sscanf(&argv[c][2], "%u", &catweasel);
				break;

			case 'G':
				sscanf(&argv[c][2], "%f", &basepitch);
				break;

			case 'Q':
				sscanf(&argv[c][2], "%f", &equaldivisionsperoctave);
				break;

			case 'J':
				sscanf(&argv[c][2], "%s", specialnotenames);
				break;

			case 'Y':
				sscanf(&argv[c][2], "%s", scalatuningfilepath);
				break;

			case 'w':
				sscanf(&argv[c][2], "%u", &bigwindow);
				bigwindowcmdline = 1;
				break;
			}
		}
		else
		{
			// A file to open (song, instrument, SID, MIDI...), made into a
			// full path name
			int isabsolute = (argv[c][0] == '/') || (argv[c][0] == '\\');
#ifdef __WIN32__
			if ((argv[c][0]) && (argv[c][1] == ':')) isabsolute = 1;
#endif
			if ((!isabsolute) && (getcwd(startfile, MAX_PATHNAME)))
			{
				if (strlen(startfile) + strlen(argv[c]) + 2 < MAX_PATHNAME)
				{
					strcat(startfile, "/");
					strcat(startfile, argv[c]);
				}
				else startfile[0] = 0;
			}
			else if (strlen(argv[c]) < MAX_PATHNAME)
				strcpy(startfile, argv[c]);
		}
	}

	// Validate parameters
	editorInfo.sidmodel &= 1;
	editorInfo.adparam &= 0xffff;
	zeropageadr &= 0xff;
	playeradr &= 0xff00;
	sidaddress &= 0xffff;
	if (!stepsize) stepsize = 4;
	if (editorInfo.multiplier > 16) editorInfo.multiplier = 16;
	if (keypreset > 2) keypreset = 0;
	if (midiinput > 1) midiinput = 1;
	if (followmode) followmode = 1;
	// The retro interface, always: the one used last (in the config file) is
	// not chosen again. -u (or SHIFT / OPTION held at startup) chooses
	if ((uiarg >= UI_ASK) && (uiarg <= UI_RETRO)) uimode = uiarg;
	else uimode = UI_RETRO;
	if (retrogradient) retrogradient = 1;

	// removing 'usefinevib' since it's not actually being used and is
	// not being saved. Code suggests FV not to be used for multi-speed. 
	// Unfinished implementation?  /RaveGuru
	// if ((editorInfo.finevibrato == 1) && (editorInfo.multiplier < 2)) editorInfo.usefinevib = 1;
	
	if (editorInfo.finevibrato > 1) editorInfo.finevibrato = 1;
	if (editorInfo.optimizepulse > 1) editorInfo.optimizepulse = 1;
	if (editorInfo.optimizerealtime > 1) editorInfo.optimizerealtime = 1;
	if (residdelay > 63) residdelay = 63;
	if (customclockrate < 100) customclockrate = 0;
	if (bigwindow < 1) bigwindow = 1;
	if (bigwindow > 4) bigwindow = 4;

	// The standard frequency table, for setbasepitch()
	memcpy(defaultfreqtbllo, freqtbllo, sizeof defaultfreqtbllo);
	memcpy(defaultfreqtblhi, freqtblhi, sizeof defaultfreqtblhi);

	// Read Scala tuning file
	if (scalatuningfilepath[0] != '0' && scalatuningfilepath[1] != '\0')
	{
		readscalatuningfile();
	}

	// Calculate frequencytable if necessary
	if (basepitch < 0.0f)
		basepitch = 0.0f;
	if (basepitch > 0.0f)
		calculatefreqtable();

	// Set special note names
	if (specialnotenames[1] != '\0')
	{
		setspecialnotenames();
	}

	// Set screenmode
	if (!initscreen())
		return EXIT_FAILURE;

	// Modern or retro interface. It is asked unless chosen for good before;
	// holding SHIFT (or OPTION on macOS) while starting asks again
	if (!chooseinterface())
		return EXIT_SUCCESS;

	// The credits, for a while at the start (in the message area, like the
	// other messages; a file opened at the start shows its message instead)
	gui_message("%s", ABOUTTEXT);

	// Reset channels/song
	initchannels();
	clearsong(1, 1, 1, 1, 1);

	undoInitAllAreas();	// Must be called after clearSong. Creates undo buffers, containing duplicates of each GT area.

	gui_marksongsaved();

	// Init sound
#ifndef GT_PLUGIN
	snd_devicename = audiodevice;
	snd_buffersamples = audiobuffer;
	// The device chosen in the preferences may be gone: then the default
	if ((!sound_init(b, mr, writer, hardsid, editorInfo.sidmodel, editorInfo.ntsc, editorInfo.multiplier, catweasel, interpolate, customclockrate)) &&
		(!reopenaudio()) && (!snd_sndinitted))
#else
	if (!sound_init(b, mr, writer, hardsid, editorInfo.sidmodel, editorInfo.ntsc, editorInfo.multiplier, catweasel, interpolate, customclockrate))
#endif
	{
		gui_alert("Sound init failed", "GoatTracker runs without sound. Notice that the song timer won't start.");
	}

	// MIDI notes are played as they come (see midinotehook())
	midi_setnotehook(midinotehook);
	// Open MIDI input devices
	if (midiinput)
		midi_init();

	// Open and play the file given on the command line, if any (also a file
	// double-clicked on Windows). Files opened from the macOS Finder arrive as
	// drop events instead, and are played too
	if (startfile[0])
		runcommand(GTCMD_OPENFILEPLAY, startfile);

	// Start editor mainloop
	if (retromode)
	{
		if (showhelpatstart)
			onlinehelp(0, 0);
		retromainloop();
	}
	else
	{
		if (showhelpatstart)
			gui_command(GTCMD_HELP);
		while (!exitprogram)
		{
			mainloopframe();
		}
	}

	// Shutdown sound output & MIDI input now
	sound_uninit();
	midi_uninit();

	// Save configuration
#ifndef __WIN32__
#ifdef __amigaos__
	strcpy(filename, "PROGDIR:goattrk2.cfg");
#else
	strcpy(filename, getenv("HOME"));
	strcat(filename, "/.goattrk");
	mkdir(filename, S_IRUSR | S_IWUSR | S_IXUSR);
	strcat(filename, "/goattrk2.cfg");
#endif
#endif
	configfile = fopen(filename, "wt");
	if (configfile)
	{
		fprintf(configfile, ";------------------------------------------------------------------------------\n"
			";GT2 config file. Rows starting with ; are comments. Hexadecimal parameters are\n"
			";to be preceded with $ and decimal parameters with nothing.                    \n"
			";------------------------------------------------------------------------------\n"
			"\n"
			";reSID buffer length (in milliseconds)\n%d\n\n"
			";reSID mixing rate (in Hz)\n%d\n\n"
			";Hardsid device number (0 = off)\n%d\n\n"
			";reSID model (0 = 6581, 1 = 8580)\n%d\n\n"
			";Timing mode (0 = PAL, 1 = NTSC)\n%d\n\n"
			";Packer/relocator fileformat (0 = SID, 1 = PRG, 2 = BIN)\n%d\n\n"
			";Packer/relocator player address\n$%04x\n\n"
			";Packer/relocator zeropage baseaddress\n$%02x\n\n"
			";Packer/relocator player type (0 = standard ... 3 = minimal)\n%d\n\n"
			";Key entry mode (0 = Protracker, 1 = DMC, 2 = Janko)\n%d\n\n"
			";Pattern highlight step size\n%d\n\n"
			";Speed multiplier (0 = 25Hz, 1 = 1X, 2 = 2X etc.)\n%d\n\n"
			";Use CatWeasel SID (0 = off, 1 = on)\n%d\n\n"
			";Hardrestart ADSR parameter\n$%04x\n\n"
			";reSID interpolation (0 = off, 1 = on, 2 = distortion, 3 = distortion & on)\n%d\n\n"
			";Pattern display mode (0 = decimal, 1 = hex, 2 = decimal w/dots, 3 = hex w/dots)\n%d\n\n"
			";SID baseaddress\n$%04x\n\n"
			";Finevibrato mode (0 = off, 1 = on)\n%d\n\n"
			";Pulseskipping (0 = off, 1 = on)\n%d\n\n"
			";Realtime effect skipping (0 = off, 1 = on)\n%d\n\n"
			";Random reSID write delay in cycles (0 = off)\n%d\n\n"
			";Custom SID clock cycles per second (0 = use PAL/NTSC default)\n%d\n\n"
			";HardSID interactive mode buffer size (in milliseconds, 0 = maximum/no flush)\n%d\n\n"
			";HardSID playback mode buffer size (in milliseconds, 0 = maximum/no flush)\n%d\n\n"
			";reSID-fp distortion rate\n%f\n\n"
			";reSID-fp distortion point\n%f\n\n"
			";reSID-fp distortion CF threshold\n%f\n\n"
			";reSID-fp type 3 base resistance\n%f\n\n"
			";reSID-fp type 3 base offset\n%f\n\n"
			";reSID-fp type 3 base steepness\n%f\n\n"
			";reSID-fp type 3 minimum FET resistance\n%f\n\n"
			";reSID-fp type 4 k\n%f\n\n"
			";reSID-fp type 4 b\n%f\n\n"
			";reSID-fp voice nonlinearity\n%f\n\n"
			";Window type (0 = window, 1 = fullscreen)\n%d\n\n"
			";Window scale factor if no window size is saved below (1 = automatic, 2 to 4 = 2 to 4 times 800x518)\n%d\n\n"
			";Base pitch of A-4 in Hz (0 = use default frequencytable)\n%f\n\n"
			";Equal divisions per octave (12 = default, 8.2019143 = Bohlen-Pierce)\n%f\n\n"
			";Special note names (2 chars for every note in an octave/cycle, 0 = none)\n%s\n\n"
			";Path to a Scala tuning file .scl (0 = none)\n%s\n\n"
			";Window width in windowed mode (0 = automatic)\n%d\n\n"
			";Window height in windowed mode (0 = automatic)\n%d\n\n"
			";MIDI keyboard input (0 = off, 1 = on)\n%d\n\n"
			";Follow playback in the pattern editor (0 = off, 1 = on)\n%d\n\n"
			";Text size in percent (100 = default)\n%d\n\n"
			";Folder of the song file dialogs (0 = none)\n%s\n\n"
			";Folder of the instrument file dialogs (0 = none)\n%s\n\n"
			";Folder of the import dialogs: SID, MIDI, chipsynth (0 = none)\n%s\n\n"
			";Folder of the export dialog (0 = none)\n%s\n\n"
			";Retro screen background (0 = black, 1 = gradient)\n%d\n\n"
			";Retro level meter colors at the bottom, middle and top ($AARRGGBB, AA = opacity)\n$%08X\n$%08X\n$%08X\n\n",
			b,
			mr,
			hardsid,
			editorInfo.sidmodel,
			editorInfo.ntsc,
			fileformat,
			playeradr,
			zeropageadr,
			playerversion,
			keypreset,
			stepsize / resomode,
			editorInfo.multiplier,
			catweasel,
			editorInfo.adparam,
			interpolate,
			patterndispmode,
			sidaddress,
			editorInfo.finevibrato,
			editorInfo.optimizepulse,
			editorInfo.optimizerealtime,
			residdelay,
			customclockrate,
			hardsidbufinteractive,
			hardsidbufplayback,
			filterparams.distortionrate,
			filterparams.distortionpoint,
			filterparams.distortioncfthreshold,
			filterparams.type3baseresistance,
			filterparams.type3offset,
			filterparams.type3steepness,
			filterparams.type3minimumfetresistance,
			filterparams.type4k,
			filterparams.type4b,
			filterparams.voicenonlinearity,
			win_fullscreen,
			bigwindow,
			basepitch,
			equaldivisionsperoctave,
			// Empty strings are written as 0: a blank line would be skipped
			// when reading, shifting the values after it
			specialnotenames[0] ? specialnotenames : "0",
			scalatuningfilepath[0] ? scalatuningfilepath : "0",
			windowedxsize,
			windowedysize,
			midiinput,
			followmode,
			textsizepercent,
			songpath[0] ? songpath : "0",
			instrpath[0] ? instrpath : "0",
			importpath[0] ? importpath : "0",
			packedpath[0] ? packedpath : "0",
			retrogradient,
			retrometercolors[0],
			retrometercolors[1],
			retrometercolors[2]);
		fprintf(configfile, ";Meter color presets of your own: bottom, middle, top of each ($AARRGGBB, 0 = empty)\n");
		for (c = 0; c < METERPRESETS * 3; c++)
			fprintf(configfile, "$%08X\n", retrometerpresets[c / 3][c % 3]);
		fprintf(configfile, "\n;Folder of the SID designer's instrument browser (0 = none)\n%s\n\n", browserfolder[0] ? browserfolder : "0");
		fprintf(configfile, ";Window position in windowed mode: saved (0 = no: centered), left, top\n%d\n%d\n%d\n\n", windowposset, windowedxpos, windowedypos);
		fprintf(configfile, ";Folders of the MIDI import, chipsynth import, MIDI export and chipsynth export dialogs (0 = none)\n%s\n%s\n%s\n%s\n\n",
			pluginfolders[0][0] ? pluginfolders[0] : "0", pluginfolders[1][0] ? pluginfolders[1] : "0",
			pluginfolders[2][0] ? pluginfolders[2] : "0", pluginfolders[3][0] ? pluginfolders[3] : "0");
		fprintf(configfile, ";Audio output buffer in samples (0 = the reSID buffer length above, in milliseconds)\n%d\n\n", audiobuffer);
		fprintf(configfile, ";Audio output device (0 = the system's default)\n%s\n\n", audiodevice[0] ? audiodevice : "0");
		fclose(configfile);
	}

	// Exit
	return EXIT_SUCCESS;
}

// One pass of the main loop: draw the user interface, then run the
// commands, key presses and MIDI notes it received
static void mainloopframe(void)
{
	int cmd;
	char path[MAX_PATHNAME];

	// A classic dialog that has finished leaves its text behind
	if (textscreenactive)
	{
		clearscreen();
		textscreenactive = 0;
	}
	cursorflashdelay += win_getspeed(50);
	resosync();
	displayupdate();
	followplayback();
	gui_frame(0);
	macmenu_setstate(recordmode, followmode);

	// Menus, toolbar, drag & drop
	while (cmd_poll(&cmd, path, MAX_PATHNAME))
	{
		runcommand(cmd, path);
		if (exitprogram) return;
	}

	// Keys for the editors
	while (input_nextkey())
	{
		converthex();
		docommand();
		if (exitprogram) return;
	}

	// MIDI keyboard notes
	while (input_nextmidi())
	{
		key = 0;
		rawkey = 0;
		hexnybble = -1;
		docommand();
	}

	// Window closed / CMD+Q
	if (win_quitted)
	{
		win_quitted = 0;
		runcommand(GTCMD_QUIT, NULL);
	}
}

// Nonzero if SHIFT (or OPTION on macOS) is held down while the program
// starts
static int startupmodifierheld(void)
{
#ifdef __APPLE__
	return macfile_modifiersheld();
#elif defined(__WIN32__)
	return (GetAsyncKeyState(VK_SHIFT) & 0x8000) ? 1 : 0;
#else
	SDL_PumpEvents();
	return (SDL_GetModState() & KMOD_SHIFT) ? 1 : 0;
#endif
}

// Sets up the interface: the retro one, unless -u chose another. With -u0,
// or SHIFT / OPTION held while the program starts, the user picks one for
// this time. Returns 0 if the window was closed instead
static int chooseinterface(void)
{
	int choice = uimode;

	if ((choice == UI_ASK) || (startupmodifierheld()))
	{
		choice = gui_chooseinterface(choice == UI_MODERN ? UI_MODERN : UI_RETRO);
		if (choice == UI_ASK) return 0;
	}
	uimode = choice;
	retromode = (choice == UI_RETRO);
	gui_setretro(retromode);
	return 1;
}

// The retro interface's main loop: the classic main screen, with its keys
// and mouse, plus the menu commands and drag & drop of the new version
static void retromainloop(void)
{
	int cmd;
	char path[MAX_PATHNAME];

	printmainscreen();
	while (!exitprogram)
	{
		int input = 0;

		// Wait for a key, mouse button or wheel, MIDI note or a command
		retroatmainscreen = 1;
		for (;;)
		{
			resosync();
			followplayback();
			displayupdate();
			getkey();
			macmenu_setstate(recordmode, followmode);
			if ((rawkey) || (key) || (mouseb) || ((prevmouseb) && (!mouseb)) || (mousewheel) || (midinoteon) || (midinoteoff) || (menucommand))
			{
				input = 1;
				break;
			}
			if ((win_quitted) || (exitprogram) || (cmd_pending())) break;
		}
		retroatmainscreen = 0;

		if ((input) && (!exitprogram))
		{
			int midi = (midinoteon) || (midinoteoff);

			converthex();
			docommand();

			// More MIDI notes waiting (the rest of a chord) are handled now,
			// not one per frame. The mouse has been handled already
			if (midi)
			{
				int mb = mouseb, pmb = prevmouseb, mw = mousewheel;

				mouseb = 0;
				prevmouseb = 0;
				mousewheel = 0;
				while ((!exitprogram) && (input_nextmidi()))
				{
					key = 0;
					rawkey = 0;
					hexnybble = -1;
					docommand();
				}
				mouseb = mb;
				prevmouseb = pmb;
				mousewheel = mw;
			}
		}

		// Menus, drag & drop, commands from the dialogs
		while ((!exitprogram) && (cmd_poll(&cmd, path, MAX_PATHNAME)))
			runcommand(cmd, path);

		// Window closed / CMD+Q
		if (win_quitted)
		{
			win_quitted = 0;
			runcommand(GTCMD_QUIT, NULL);
		}
	}
}

// The classic dialogs wait for keys with these. Each call of getkey() runs
// one frame of the user interface with the classic text screen shown
void waitkey(void)
{
	for (;;)
	{
		displayupdate();
		getkey();
		if ((rawkey) || (key)) break;
		if (win_quitted) break;
	}

	converthex();
}

void waitkeymouse(void)
{
	for (;;)
	{
		displayupdate();
		getkey();
		if ((rawkey) || (key)) break;
		if (win_quitted) break;
		if (mouseb) break;
		if ((midinoteon) || (midinoteoff)) break;
		if (menucommand) break;
	}

	converthex();
}

void waitkeymousenoupdate(void)
{
	for (;;)
	{
		getkey();
		if ((rawkey) || (key)) break;
		if (win_quitted) break;
		if (mouseb) break;
	}

	converthex();
}

void waitkeynoupdate(void)
{
	for (;;)
	{
		getkey();
		if ((rawkey) || (key)) break;
		if ((mouseb) && (!prevmouseb)) break;
		if (win_quitted) break;
	}
}

void converthex()
{
	int c;

	hexnybble = -1;
	for (c = 0; c < 16; c++)
	{
		if (tolower(key) == hexkeytbl[c])
		{
			if (c >= 10)
			{
				if (!shiftpressed) hexnybble = c;
			}
			else
			{
				hexnybble = c;
			}
		}
	}
}

void do_sound_init() {
	sound_init(b, mr, writer, hardsid, editorInfo.sidmodel, editorInfo.ntsc, editorInfo.multiplier, catweasel, interpolate, customclockrate);
}

int reopenaudio(void)
{
#ifndef GT_PLUGIN
	int ok;

	sound_closeoutput();
	snd_devicename = audiodevice;
	snd_buffersamples = audiobuffer;
	ok = sound_init(b, mr, writer, hardsid, editorInfo.sidmodel, editorInfo.ntsc, editorInfo.multiplier, catweasel, interpolate, customclockrate);
	if ((!ok) && (audiodevice[0]))
	{
		sound_closeoutput();
		snd_devicename = NULL;
		sound_init(b, mr, writer, hardsid, editorInfo.sidmodel, editorInfo.ntsc, editorInfo.multiplier, catweasel, interpolate, customclockrate);
	}
	return ok;
#else
	return 1;
#endif
}

void toggle_interpolation_mode() {
	interpolate++;
	if (interpolate == 2)  // skip FP only mode
		interpolate++;
	interpolate &= 3;
	stopsong();
	do_sound_init();
}

// SPACE starts playing from the cursor / stops, in every editor except where
// text is typed (the song name, author & copyright and the instrument name).
// In the orderlist the playback starts at the position under the cursor.
// Stopping puts the editor back where the playback started (as in a DAW).
// SHIFT+SPACE plays without following. Returns nonzero if it was SPACE
static int spacecommand(void)
{
	if (rawkey != KEY_SPACE) return 0;
	if ((altpressed) || (metapressed)) return 0;
	if (editorInfo.editmode == EDIT_NAMES) return 0;
	if ((editorInfo.editmode == EDIT_INSTRUMENT) && (editorInfo.eipos >= 9)) return 0;

	if (shiftpressed)
	{
		if (editorInfo.editmode == EDIT_ORDERLIST) playfromrow(0, 0);
		else playfromcursor(0);
	}
	else
		playstopreturn(followmode, editorInfo.editmode == EDIT_ORDERLIST);
	followsuspended = 0;
	key = 0;
	rawkey = 0;
	return 1;
}

void docommand(void)
{
	// Where a MIDI note goes (MIDI chord input)
	if (midinoteon) midichordassign();

	// Menu bar & CMD key commands
	if (menucommands()) return;

	// Numpad play/stop, pattern row jumps, edit/jam mode & channel select
	if (numpadcommands()) return;

	// SPACE plays / stops in every editor
	if (spacecommand()) return;

	// Mouse operation is handled by the GUI panels (gui/), in the retro
	// interface by the classic main screen
	if (retromode) retro_mousecommands();

	GTUNDO_OBJECT *ed = undoCreateEditorInfo();

	// Mode-specific commands
	switch (editorInfo.editmode)
	{
	case EDIT_ORDERLIST:

		for (int i = 0;i < MAX_CHN;i++)
		{
			undoAreaSetCheckForChange(UNDO_AREA_ORDERLIST, i + (editorInfo.esnum*MAX_CHN), UNDO_AREA_DIRTY_CHECK);
		}
		undoAreaSetCheckForChange(UNDO_AREA_ORDERLIST_LEN, 0, UNDO_AREA_DIRTY_CHECK);

		orderlistcommands();
		break;

	case EDIT_INSTRUMENT:
		undoAreaSetCheckForChange(UNDO_AREA_INSTRUMENTS, editorInfo.einum, UNDO_AREA_DIRTY_CHECK);
		instrumentcommands();
		break;

	case EDIT_TABLES:
		undoAreaSetCheckForChange(UNDO_AREA_INSTRUMENTS, editorInfo.einum, UNDO_AREA_DIRTY_CHECK);
		undoAreaSetCheckForChange(UNDO_AREA_TABLES + editorInfo.etnum, 0, UNDO_AREA_DIRTY_CHECK);	// left table
		undoAreaSetCheckForChange(UNDO_AREA_TABLES + editorInfo.etnum, 1, UNDO_AREA_DIRTY_CHECK);	// right table
		tablecommands();
		break;

	case EDIT_PATTERN:
		undoAreaSetCheckForChange(UNDO_AREA_PATTERN, editorUndoInfo.editorInfo[editorInfo.epchn].epnum, UNDO_AREA_DIRTY_CHECK);
		undoAreaSetCheckForChange(UNDO_AREA_PATTERN_LEN, 0, UNDO_AREA_DIRTY_CHECK);
		for (int i = 0;i < MAX_CHN;i++)
		{
			undoAreaSetCheckForChange(UNDO_AREA_ORDERLIST, i + (editorInfo.esnum*MAX_CHN), UNDO_AREA_DIRTY_CHECK);
		}
		undoAreaSetCheckForChange(UNDO_AREA_ORDERLIST_LEN, 0, UNDO_AREA_DIRTY_CHECK);
		// The notes of a MIDI chord go to the other channels' patterns too,
		// so does a note nudged to the next track (CTRL+LEFT/RIGHT); ALT+UP/
		// DOWN transposes the marked rows, maybe on another track
		// DEL / BACKSPACE clear the marked rows, maybe on another track
		// (ALT+DEL marks the patterns it changes itself)
		if (((rawkey == KEY_DEL) || (rawkey == KEY_BACKSPACE)) && (editorInfo.epmarkchn != -1))
			undoAreaSetCheckForChange(UNDO_AREA_PATTERN, editorUndoInfo.editorInfo[editorInfo.epmarkchn].epnum, UNDO_AREA_DIRTY_CHECK);
		if (((midinoteon) && (midichord > 1)) || ((ctrlpressed) && ((rawkey == KEY_LEFT) || (rawkey == KEY_RIGHT))) ||
			((altpressed) && ((rawkey == KEY_UP) || (rawkey == KEY_DOWN))))
		{
			for (int i = 0;i < MAX_CHN;i++)
				undoAreaSetCheckForChange(UNDO_AREA_PATTERN, editorUndoInfo.editorInfo[i].epnum, UNDO_AREA_DIRTY_CHECK);
		}
		// Notes recorded while following the playback go to the patterns the
		// channels play, or the next ones (see patternrecording())
		if ((patternrecording()) || ((midinoteon) && (midirecord)))
		{
			for (int i = 0;i < MAX_CHN;i++)
			{
				int p, t;
				undoAreaSetCheckForChange(UNDO_AREA_PATTERN, editorUndoInfo.editorInfo[i].epnum, UNDO_AREA_DIRTY_CHECK);
				undoAreaSetCheckForChange(UNDO_AREA_PATTERN, chn[i].playpatt, UNDO_AREA_DIRTY_CHECK);
				undoAreaSetCheckForChange(UNDO_AREA_PATTERN, chn[i].readpatt, UNDO_AREA_DIRTY_CHECK);
				if (playnextpattern(i, &p, &t)) undoAreaSetCheckForChange(UNDO_AREA_PATTERN, p, UNDO_AREA_DIRTY_CHECK);
			}
			if ((midinoteon) && (midirecord)) undoAreaSetCheckForChange(UNDO_AREA_PATTERN, midirecpatt, UNDO_AREA_DIRTY_CHECK);
		}

		patterncommands();
		break;

	case EDIT_NAMES:
		namecommands();
		break;
	}

	if (undoValidateUndoAreas(ed) == 0)
	{
		undoFreeUndoObject((GTUNDO_OBJECT*)ed);
	}
	// MIDI keyboard note on/off
	midicommands();

	// General commands
	generalcommands();
}

// Handles the command from the menu bar or a CMD key shortcut, if any.
// Returns nonzero if nothing else is to be done for this keypress
int menucommands(void)
{
	switch (menucommand)
	{
	case MENUCMD_UNDO:
		undoPerform();
		return 1;

	case MENUCMD_RECORD:
		recordmode ^= 1;
		return 1;

	case MENUCMD_FOLLOW:
		followmode ^= 1;
		if (isplaying()) followplay = followmode;
		return 1;

	case MENUCMD_PLAYSTOP:
		playstop();
		return 1;

	case MENUCMD_IMPORTMIDI:
		runcommand(GTCMD_IMPORTMIDI, NULL);
		return 1;

	case MENUCMD_SELECTALL:
		// Mark the whole pattern / orderlist (SHIFT+L)
		if ((editorInfo.editmode == EDIT_PATTERN) || (editorInfo.editmode == EDIT_ORDERLIST))
		{
			rawkey = KEY_L;
			key = 0;
			shiftpressed = 1;
		}
		break;
	}
	return 0;
}

// Numpad keys:
// - CTRL + numpad 1-3 = put the cursor on track (channel) 1-3 in the pattern
//   editor, from any editor
// - Numpad 0 = play from the cursor / stop, like SPACE: stopping puts the
//   editor back where the playback started (SHIFT: play the other follow
//   way, like SHIFT+F1-F3)
// - Numpad 1-8 = go to pattern row 0, 8, 16, 24, 32, 40, 48 or 56 in the
//   pattern editor
// - Numpad . = switch between edit mode and jam mode
// The plain numpad keys don't work in the songname editor, nor with
// CTRL/CMD (CTRL/CMD + numpad 0 is the default text size).
// Returns nonzero if nothing else is to be done for this keypress
int numpadcommands(void)
{
	// SDL2: KP_1-KP_9 are consecutive, but KP_0 comes after KP_9
	if ((ctrlpressed) && (!metapressed) && (!altpressed) && (rawkey >= KEY_KP1) && (rawkey <= KEY_KP3))
	{
		int len;

		editorInfo.editmode = EDIT_PATTERN;
		editorInfo.epchn = rawkey - KEY_KP1;
		len = pattlen[editorUndoInfo.editorInfo[editorInfo.epchn].epnum];
		if (editorInfo.eppos > len) editorInfo.eppos = len;
		key = 0;
		rawkey = 0;
		return 1;
	}

	if ((editorInfo.editmode == EDIT_NAMES) || (ctrlpressed) || (metapressed) || (altpressed))
		return 0;

	if (rawkey == KEY_KP0)
	{
		playstopreturn(followmode ^ shiftonlypressed, 0);
		followsuspended = 0;
		key = 0;
		rawkey = 0;
		return 1;
	}

	if ((rawkey >= KEY_KP1) && (rawkey <= KEY_KP8) && (editorInfo.editmode == EDIT_PATTERN))
	{
		patternjump((rawkey - KEY_KP1) * 8);
		key = 0;
		rawkey = 0;
		return 1;
	}

	if (rawkey == KEY_KPPERIOD)
	{
		recordmode ^= 1;
		gui_message(recordmode ? "Edit mode: notes are entered" : "Jam mode: notes are only played");
		key = 0;
		rawkey = 0;
		return 1;
	}
	return 0;
}

// The state below up to midinextslot belongs to the note hook
// (midinotehook()), which runs in the MIDI input's thread under the MIDI
// queue's lock: the editor changes it only under that lock.
// MIDI keys held down: the channel each plays on + 1 (-1 = left out of the
// chord, 0 = not held)
static int midiheldchn[LASTNOTE + 1];
static int midiheldcount = 0;
// The MIDI note each channel plays (0 = none)
static int midichnnote[MAX_CHN];
// The chord being played: notes so far, its channels (bits) and when its
// first key was struck
static int chordnotes = 0;
static int chordmask = 0;
static unsigned chordtime = 0;
// Keys struck within this many milliseconds of the first one, while it is
// held, make a chord. A later key is a note of its own, even if the earlier
// ones are still held (legato playing)
#define MIDICHORDTIME 60
// The next channel in turn (0 = the cursor's) for MIDI input on 2-3
// channels, and (the editor's) where the cursor was after the last note
static int midinextslot = 0;
static volatile int miditurnchn = -1;
static volatile int miditurnrow = -1;
// The editor's: the row of the chord's first note
static int chordrow = 0;
// Whether the chord's first note was recorded on the next row (see
// patternrecordpos())
static int chordrecnext = 0;

// What the note hook made of a MIDI note (midinoteassign, kept with the
// event): the channel + 1 (0 = left out of a chord), whether it starts a
// new row (the cursor moves down after it), the channels of the chord's
// earlier notes (bits), whether the hook has played it, and where it is
// recorded while following the playback (MIDIASSIGN_REC: pattern, row,
// transpose + 16)
#define MIDIASSIGN_CHN 0x03
#define MIDIASSIGN_FIRST 0x04
#define MIDIASSIGN_MASKSHIFT 3
#define MIDIASSIGN_PLAYED 0x80
#define MIDIASSIGN_VALID 0x100
#define MIDIASSIGN_REC 0x200
#define MIDIASSIGN_RECPATTSHIFT 10
#define MIDIASSIGN_RECROWSHIFT 18
#define MIDIASSIGN_RECTRANSSHIFT 25

// Sets the number of channels MIDI chords go to (1-3, see midichord). The
// notes still sounding from the MIDI keyboard are released, and the keys
// still held forgotten (so that a lost note-off can't block chords for good)
void setmidichord(int channels)
{
	int c;

	if (channels < 1) channels = 1;
	if (channels > MAX_CHN) channels = MAX_CHN;
	midichord = channels;

	midi_lock();
	for (c = 0; c < MAX_CHN; c++)
	{
		if ((midichnnote[c]) && (!gui_designeractive())) releasenote(c);
		midichnnote[c] = 0;
	}
	memset(midiheldchn, 0, sizeof midiheldchn);
	midiheldcount = 0;
	chordnotes = 0;
	midinextslot = 0;
	midi_unlock();

	if (midichord > 1)
		gui_message("MIDI input: notes go to %d channels in turn, chords to %d channels", midichord, midichord);
	else
		gui_message("MIDI input: notes go to the cursor's channel");
}

// Sets STEPS (0-16): rows skipped after a note is entered
void seteditstep(int steps)
{
	if (steps < 0) steps = 0;
	if (steps > MAX_EDITSTEP) steps = MAX_EDITSTEP;
	editstep = steps;
}

// RESO (resolution): two (X2) or three (X3) times as many rows per beat, so
// that notes played in can land more finely (tuplets). The music stays as it
// is: every pattern gets that many times the rows (its rows on every second
// or third one), every tempo is divided and a beat is that many times the
// rows (stepsize), which also keeps the plugin in time with the host. New
// patterns get 128 rows (X2) or 96 rows (X3: two bars of 12 rows a beat).
//
// The song itself tells its RESO, so that it stays when saved and loaded, and
// so that undo and redo work: instrument $3F is named RESO X2 or RESO X3 (or
// DOUBLE, as songs made with the DOUBLE button are), and its attack/decay is
// the tempo the song starts with (the player's and the packer's "tempo
// instrument")
#define RESOINSTR (MAX_INSTR - 1)
#define RESOMAX 3
// The player's shortest row (tempo 3; lower values are funktempo)
#define RESOLOWEST 3

static int resooldlength = 64;

static const char *const resoname[RESOMAX + 1] = { "", "", "RESO X2", "RESO X3" };
static const char doublename[] = "DOUBLE";

// New patterns' rows with RESO X2 / X3
static int resopatternlength(int k)
{
	return (k == 3) ? 96 : MAX_PATTROWS;
}

// Frames per row the song starts with when nothing else says so
static int resodefaulttempo(void)
{
	return editorInfo.multiplier ? 6 * editorInfo.multiplier : 6;
}

// The song's RESO (1 = off)
static int resomarked(void)
{
	INSTR *in = &instr[RESOINSTR];
	int k;

	if ((in->ad < 2) || (in->ptr[WTBL])) return 1;
	if (!strncmp(in->name, doublename, MAX_INSTRNAMELEN)) return 2;
	for (k = 2; k <= RESOMAX; k++)
	{
		if (!strncmp(in->name, resoname[k], MAX_INSTRNAMELEN)) return k;
	}
	return 1;
}

// Marks the song as one with RESO k, starting at the default tempo
static void resomark(int k)
{
	INSTR *in = &instr[RESOINSTR];

	clearinstr(RESOINSTR);
	in->ad = resodefaulttempo() / k;
	strcpy(in->name, resoname[k]);
}

// A tempo of the song with RESO from becomes this with RESO to (0 if it
// can't: not a whole number of frames, or shorter than the player's rows)
static int resotempo(int tempo, int from, int to)
{
	if ((tempo * from) % to) return 0;
	tempo = tempo * from / to;
	if (tempo < RESOLOWEST) return 0;
	return (tempo > 0x7f) ? 0x7f : tempo;
}

static int rowempty(const unsigned char *row)
{
	return (row[0] == REST) && (!row[1]) && (!row[2]) && (!row[3]);
}

static int patternempty(int p)
{
	int d;

	for (d = 0; d < pattlen[p]; d++)
	{
		if (!rowempty(&pattern[p][d*4])) return 0;
	}
	return 1;
}

static int songempty(void)
{
	int c;

	for (c = 0; c < MAX_PATT; c++)
	{
		if (!patternempty(c)) return 0;
	}
	return 1;
}

// Slides and vibrato last for one row: on the finer song the rows in between
// go on with the same command
static int resocontinues(int command)
{
	return (command >= CMD_PORTAUP) && (command <= CMD_VIBRATO);
}

// A pattern's rows with RESO to instead of from
static int resolength(int len, int from, int to)
{
	return (len * to + from - 1) / from;
}

// The tempo the song starts with
static int resostarttempo(void)
{
	INSTR *in = &instr[RESOINSTR];

	return ((in->ad >= 2) && (!in->ptr[WTBL])) ? in->ad : resodefaulttempo();
}

// Whether the song can go from RESO from to RESO to. If not, the message says
// why. Off always works (notes in between move to the nearest row)
static int resocanchange(int from, int to)
{
	INSTR *in = &instr[RESOINSTR];
	unsigned char used[MAX_INSTR];
	int start, fastest, empty, c, d;
	const char *what = resoname[to];

	if (to == 1) return 1;
	if ((from == 1) && ((in->name[0]) || (in->ptr[WTBL]) || (in->ptr[PTBL]) || (in->ptr[FTBL]) || (in->ptr[STBL])))
	{
		gui_message("%s needs instrument 3F, which is in use", what);
		return 0;
	}
	start = resostarttempo();
	fastest = resotempo(start, from, to);
	if (!fastest)
	{
		gui_message("%s: tempo %02X can't be divided (%s needs %02X, %02X, %02X...)", what, start * from, what + 5,
			RESOLOWEST * to, (RESOLOWEST + 1) * to, (RESOLOWEST + 2) * to);
		return 0;
	}

	findusedpatterns();
	empty = songempty();
	memset(used, 0, sizeof used);
	used[1] = 1;  // Every channel starts on instrument 1
	for (c = 0; c < MAX_PATT; c++)
	{
		if ((!empty) && (to > from) && (resolength(pattlen[c], from, to) > MAX_PATTROWS) && ((pattused[c]) || (!patternempty(c))))
		{
			gui_message("%s: pattern %02X is longer than %d rows", what, c, MAX_PATTROWS * from / to);
			return 0;
		}
		for (d = 0; d < pattlen[c]; d++)
		{
			unsigned char *row = &pattern[c][d*4];

			if (row[1] < MAX_INSTR) used[row[1]] = 1;
			if ((row[2] == CMD_FUNKTEMPO) || ((row[2] == CMD_SETTEMPO) && ((row[3] & 0x7f) < 2)))
			{
				gui_message("%s: the funktempo in pattern %02X can't be divided", what, c);
				return 0;
			}
			if (row[2] == CMD_SETTEMPO)
			{
				int tempo = resotempo(row[3] & 0x7f, from, to);

				if (!tempo)
				{
					gui_message("%s: tempo %02X in pattern %02X can't be divided", what, row[3] & 0x7f, c);
					return 0;
				}
				if (tempo < fastest) fastest = tempo;
			}
		}
	}
	// The player reads the next row this many frames before it starts (rows
	// get shorter only with more of them)
	for (c = 1; (to > from) && (c < RESOINSTR); c++)
	{
		if ((used[c]) && ((instr[c].gatetimer & 0x3f) >= fastest))
		{
			gui_message("%s: instrument %02X's gate timer must be below %02X", what, c, fastest);
			return 0;
		}
	}
	return 1;
}

// Pattern p with RESO to instead of from: each row goes to the row nearest
// its time (an exact one if there is one). Going to fewer rows, a note that
// meets another one on its new row is lost (counted in lost), one that moves
// is counted in moved. With an empty song (empty) the pattern just gets the
// new patterns' length
static void resopattern(int p, int from, int to, int empty, int *moved, int *lost)
{
	unsigned char old[MAX_PATTROWS*4+4];
	unsigned char filled[MAX_PATTROWS];
	int oldlen = pattlen[p];
	int newlen = empty ? ((to > 1) ? resopatternlength(to) : resooldlength) : resolength(oldlen, from, to);
	int pass, d;

	if (newlen > MAX_PATTROWS) newlen = MAX_PATTROWS;
	if (newlen < 1) newlen = 1;
	memcpy(old, pattern[p], sizeof old);
	memset(pattern[p], 0, sizeof old);
	memset(filled, 0, sizeof filled);
	for (d = 0; d < newlen; d++) pattern[p][d*4] = REST;

	// The rows that fall exactly on a new row first, then the others
	for (pass = 0; pass < 2; pass++)
	{
		for (d = 0; d < oldlen; d++)
		{
			unsigned char *src = &old[d*4];
			unsigned char *dest;
			int exact = !((d * to) % from);
			int t = (2 * d * to + from - 1) / (2 * from);

			if (exact != (pass == 0)) continue;
			if (rowempty(src)) continue;
			if (t >= newlen) t = newlen - 1;
			dest = &pattern[p][t*4];
			if ((src[0] != REST) || (src[1]))
			{
				if ((dest[0] == REST) && (!dest[1]))
				{
					dest[0] = src[0];
					dest[1] = src[1];
					if ((!exact) && (src[0] != REST)) (*moved)++;
				}
				else if ((src[0] <= LASTNOTE) && ((src[0] != dest[0]) || (src[1] != dest[1]))) (*lost)++;
			}
			// A command: on the new row if it has none. A slide or vibrato
			// repeated on the rows in between is no loss
			if ((src[2]) && (!dest[2]))
			{
				dest[2] = src[2];
				dest[3] = src[3];
			}
			filled[t] = 1;
		}
	}

	// Going to more rows: the rows in between go on with a slide or vibrato
	if (to > from)
	{
		for (d = 1; d < newlen; d++)
		{
			unsigned char *row = &pattern[p][d*4];
			unsigned char *before = &pattern[p][(d-1)*4];

			if ((!filled[d]) && (rowempty(row)) && (resocontinues(before[2])))
			{
				row[2] = before[2];
				row[3] = before[3];
			}
		}
	}

	// The tempo commands
	for (d = 0; d < newlen; d++)
	{
		if (pattern[p][d*4+2] == CMD_SETTEMPO)
		{
			int tempo = pattern[p][d*4+3] & 0x7f;

			if (tempo >= 2)
			{
				int t = (tempo * from) / to;

				if (t > 0x7f) t = 0x7f;
				if (t < RESOLOWEST) t = RESOLOWEST;
				pattern[p][d*4+3] = (pattern[p][d*4+3] & 0x80) | t;
			}
		}
	}
	for (d = newlen; d <= MAX_PATTROWS; d++) pattern[p][d*4] = ENDPATT;
	pattlen[p] = newlen;
}

// What goes with the song's RESO: a beat's rows and new patterns' rows
static void resoapply(int k)
{
	int base = stepsize / resomode;

	if (base < 1) base = 1;
	if ((resomode == 1) && (k > 1)) resooldlength = defaultpatternlength;
	if (k > 1) defaultpatternlength = resopatternlength(k);
	else if (resomode > 1) defaultpatternlength = resooldlength;
	stepsize = base * k;
	if (stepsize > MAX_PATTROWS) stepsize = MAX_PATTROWS;
	if (stepsize < 2) stepsize = 2;
	resomode = k;
}

// The plugin's audio thread doesn't play the song while it changes, and
// follows the host's position again after it
static void resolock(void)
{
#ifdef GT_PLUGIN
	gtplugin_lockengine();
#endif
}

static void resounlock(void)
{
#ifdef GT_PLUGIN
	gtplugin_unlockengine();
	gtsync_requestresync();
#endif
}

// Sets RESO (1 = off, 2 = X2, 3 = X3; one undo step). Stays as it is if the
// song can't be changed
void setresomode(int k)
{
	INSTR *in = &instr[RESOINSTR];
	GTUNDO_OBJECT *undo;
	int from = resomode;
	int moved = 0, lost = 0;
	int empty, start, c;

	if (k < 1) k = 1;
	if (k > RESOMAX) k = RESOMAX;
	if (k == from) return;
	if (!resocanchange(from, k)) return;

	// While following the host the plugin goes on playing
#ifdef GT_PLUGIN
	if (!gtsync_following()) stopsong();
#else
	stopsong();
#endif
	resolock();
	undo = undoCreateEditorInfo();
	for (c = 0; c < MAX_PATT; c++)
		undoAreaSetCheckForChange(UNDO_AREA_PATTERN, c, UNDO_AREA_DIRTY_CHECK);
	undoAreaSetCheckForChange(UNDO_AREA_PATTERN_LEN, 0, UNDO_AREA_DIRTY_CHECK);
	undoAreaSetCheckForChange(UNDO_AREA_INSTRUMENTS, RESOINSTR, UNDO_AREA_DIRTY_CHECK);

	empty = songempty();
	start = resostarttempo();
	for (c = 0; c < MAX_PATT; c++) resopattern(c, from, k, empty, &moved, &lost);
	start = start * from / k;
	if (start > 0xff) start = 0xff;
	if (k > 1)
	{
		resomark(k);
		in->ad = start;
	}
	else
	{
		clearinstr(RESOINSTR);
		// A tempo of its own stays; the default one needs no instrument
		if (start != resodefaulttempo()) in->ad = start;
	}
	resoapply(k);

	// The cursor stays on the same music
	editorInfo.eppos = (2 * editorInfo.eppos * k + from - 1) / (2 * from);
	if (editorInfo.eppos > pattlen[editorUndoInfo.editorInfo[editorInfo.epchn].epnum])
		editorInfo.eppos = pattlen[editorUndoInfo.editorInfo[editorInfo.epchn].epnum];
	editorInfo.epview = editorInfo.eppos - VISIBLEPATTROWS / 2;
	editorInfo.epmarkchn = -1;

	if (undoValidateUndoAreas(undo) == 0) undoFreeUndoObject(undo);
	resounlock();
	if ((lost) && (moved))
		gui_message("RESO %s: %d note%s moved to the nearest row, %d lost (CTRL+Z undoes)", (k > 1) ? resoname[k] + 5 : "OFF",
			moved, (moved == 1) ? "" : "s", lost);
	else if (lost)
		gui_message("RESO %s: %d note%s lost, meeting another on its row (CTRL+Z undoes)", (k > 1) ? resoname[k] + 5 : "OFF",
			lost, (lost == 1) ? "" : "s");
	else if (moved)
		gui_message("RESO %s: %d note%s moved to the nearest row", (k > 1) ? resoname[k] + 5 : "OFF", moved, (moved == 1) ? "" : "s");
	else if (k > 1)
		gui_message("RESO %s: %d times the rows per beat, new patterns %d rows", resoname[k] + 5, k, resopatternlength(k));
	else
		gui_message("RESO OFF");
}

// The song has changed by itself (loaded, undo, redo): RESO follows it
static void resosync(void)
{
	int k = resomarked();

	if (k == resomode) return;
	resolock();
	resoapply(k);
	resounlock();
}

// The plugin's project has been restored, with the rows per beat it was saved
// with: only RESO itself follows the song
void resorestored(void)
{
	int k = resomarked();

	if ((k > 1) && (resomode == 1)) resooldlength = defaultpatternlength;
	if (k > 1) defaultpatternlength = resopatternlength(k);
	if ((k == 1) && (resomode > 1)) defaultpatternlength = resooldlength;
	resomode = k;
}

// The note hook (see gmidi.h): a MIDI note as it comes, in the MIDI input's
// thread. Where a note-on goes is decided here
// (see midichord) and it is played at once; a note-off releases its note.
// The editor enters the note later, where this has sent it
// (midichordassign()). Runs under the MIDI queue's lock
static int midinotehook(int type, int channel, int midinote, unsigned time)
{
	int note = FIRSTNOTE + midinote - 12;
	int designer = gui_designeractive();
	int chn, first = 1, mask = 0, played = 0, rec = 0;

	if ((note < FIRSTNOTE) || (note > LASTNOTE)) return 0;

	if (type == MIDI_NOTEOFF)
	{
		// Releasing a key releases its note, like on a synth, unless another
		// key has taken the channel meanwhile (the instrument designer lets
		// go of its own notes)
		if (midiheldchn[note])
		{
			int c = midiheldchn[note] - 1;

			midiheldchn[note] = 0;
			if (midiheldcount > 0) midiheldcount--;
			if ((c >= 0) && (midichnnote[c] == note))
			{
				if (!designer) releasenote(c);
				midichnnote[c] = 0;
			}
		}
		return MIDIASSIGN_VALID;
	}

	if (midichord <= 1)
	{
		// Every note to the cursor, like a note key
		chn = editorInfo.epchn;
		chordnotes = 0;
	}
	else if ((midiheldcount) && (chordnotes) && (time - chordtime <= MIDICHORDTIME))
	{
		// Struck together with the chord's first key: the next channel, on the
		// chord's row
		first = 0;
		mask = chordmask;
		if (chordnotes < midichord)
		{
			chn = (editorInfo.epchn + midinextslot) % MAX_CHN;
			midinextslot = (midinextslot + 1) % midichord;
			chordmask |= 1 << chn;
			chordnotes++;
		}
		else chn = -1;
	}
	else
	{
		// A note of its own (or the first of a chord): the next channel in
		// turn, on the cursor's row. If the cursor has been moved, the turn
		// starts again at its channel
		if ((editorInfo.epchn != miditurnchn) || (editorInfo.eppos != miditurnrow)) midinextslot = 0;
		chn = (editorInfo.epchn + midinextslot) % MAX_CHN;
		midinextslot = (midinextslot + 1) % midichord;
		chordmask = 1 << chn;
		chordnotes = 1;
		chordtime = time;
	}
	if ((chn < 0) || (chn >= MAX_CHN)) chn = -1;

	// A key that sends note-on twice is counted once
	if (!midiheldchn[note]) midiheldcount++;
	midiheldchn[note] = (chn >= 0) ? chn + 1 : -1;
	if (chn >= 0) midichnnote[chn] = note;

	// It sounds now. The instrument designer plays its whole patch (when the
	// editor gets to the note), the song name editor nothing
	if ((chn >= 0) && (!designer) && (editorInfo.editmode != EDIT_NAMES))
	{
		playtestnote(note, editorInfo.einum, chn);
		played = 1;
	}

	// In edit mode, following the playback: recorded on the row it sounds
	// in, a chord's later notes on the row of its first (not at the cursor,
	// which is on the row the player reads next, where it would sound again)
	if (played)
	{
		int patt, row, trans;
		int r = patternrecordpos(chn, first ? -1 : chordrecnext, &patt, &row, &trans);

		if (first) chordrecnext = (r == 2);
		if ((r) && (row >= 0) && (row < MAX_PATTROWS) && ((signed char)trans >= -16) && ((signed char)trans <= 15))
			rec = MIDIASSIGN_REC | (patt << MIDIASSIGN_RECPATTSHIFT) | (row << MIDIASSIGN_RECROWSHIFT) |
				(((signed char)trans + 16) << MIDIASSIGN_RECTRANSSHIFT);
	}
	return MIDIASSIGN_VALID | (chn + 1) | (first ? MIDIASSIGN_FIRST : 0) | ((mask & 7) << MIDIASSIGN_MASKSHIFT) |
		(played ? MIDIASSIGN_PLAYED : 0) | rec;
}

// Where the MIDI note-on midinoteon goes, as the note hook decided
// (midinoteassign): its channel, and the cursor's row (a chord's later
// notes go on the row of its first)
static void midichordassign(void)
{
	int a = midinoteassign;

	midinoteplayed = 0;
	midirecord = 0;
	if ((midinoteon < 0) || (midinoteon > LASTNOTE)) return;
	if (!(a & MIDIASSIGN_VALID))
	{
		// Not seen by the hook: to the cursor
		midichordfirst = 1;
		midichordchn = editorInfo.epchn;
		midichordrow = editorInfo.eppos;
		midichordmask = 0;
		return;
	}
	midichordchn = (a & MIDIASSIGN_CHN) - 1;
	midichordfirst = (a & MIDIASSIGN_FIRST) != 0;
	midichordmask = (a >> MIDIASSIGN_MASKSHIFT) & 7;
	midinoteplayed = (a & MIDIASSIGN_PLAYED) != 0;
	if (a & MIDIASSIGN_REC)
	{
		midirecord = 1;
		midirecpatt = (a >> MIDIASSIGN_RECPATTSHIFT) & 0xff;
		midirecrow = (a >> MIDIASSIGN_RECROWSHIFT) & 0x7f;
		midirectrans = ((a >> MIDIASSIGN_RECTRANSSHIFT) & 0x1f) - 16;
	}
	if (midichordfirst) chordrow = editorInfo.eppos;
	midichordrow = chordrow;
}

void midicommands(void)
{
	int designer = gui_designeractive();

	// The instrument designer plays its whole patch
	if (designer)
	{
		if ((midinoteon) || (midinoteoff)) gui_designermidi(midinoteon, midinoteoff);
	}
	else if ((midinoteon) && (midichordchn >= 0) && (!midinoteplayed))
	{
		// The pattern editor enters & plays the note like a note key does.
		// Elsewhere (except the songname editor) the note is just played,
		// the notes of a chord on their channels. (Played already as it
		// came, see midinotehook())
		if ((editorInfo.editmode != EDIT_PATTERN) && (editorInfo.editmode != EDIT_NAMES))
			playtestnote(midinoteon, editorInfo.einum, midichordchn);
	}

	// Where the cursor is after the note: the next channel in turn is used
	// while the cursor stays there
	if (midinoteon)
	{
		miditurnchn = editorInfo.epchn;
		miditurnrow = editorInfo.eppos;
	}

	// Releasing a key has released its note already (midinotehook())
}

void generalcommands(void)
{
	int c;

	switch (key)
	{
	case '?':
	case '-':
		if ((editorInfo.editmode != EDIT_NAMES) && (editorInfo.editmode != EDIT_ORDERLIST))
		{
			if (!((editorInfo.editmode == EDIT_INSTRUMENT) && (editorInfo.eipos == 9))) previnstr();
		}
		break;

	case '+':
	case '_':
		if ((editorInfo.editmode != EDIT_NAMES) && (editorInfo.editmode != EDIT_ORDERLIST))
		{
			if (!((editorInfo.editmode == EDIT_INSTRUMENT) && (editorInfo.eipos >= 9))) nextinstr();

		}
		break;

	case '*':
		if (editorInfo.editmode != EDIT_NAMES)
		{
			if (!((editorInfo.editmode == EDIT_INSTRUMENT) && (editorInfo.eipos >= 9)))
			{
				if (editorInfo.epoctave < 7) editorInfo.epoctave++;
			}
		}
		break;

	case '/':
	case '\'':
		if (editorInfo.editmode != EDIT_NAMES)
		{
			if (!((editorInfo.editmode == EDIT_INSTRUMENT) && (editorInfo.eipos >= 9)))
			{
				if (editorInfo.epoctave > 0) editorInfo.epoctave--;
			}
		}
		break;

	// < > select the instrument in the instrument & table editors and the
	// pattern editor (where [ ] select the pattern)
	case '<':
		if (((editorInfo.editmode == EDIT_INSTRUMENT) && (editorInfo.eipos != 9)) || (editorInfo.editmode == EDIT_TABLES) ||
			(editorInfo.editmode == EDIT_PATTERN))
			previnstr();
		break;

	case '>':
		if (((editorInfo.editmode == EDIT_INSTRUMENT) && (editorInfo.eipos != 9)) || (editorInfo.editmode == EDIT_TABLES) ||
			(editorInfo.editmode == EDIT_PATTERN))
			nextinstr();
		break;

	case ';':
		for (c = 0; c < MAX_CHN; c++)
		{
			if (editorUndoInfo.editorInfo[c].espos) editorUndoInfo.editorInfo[c].espos--;
			if (editorUndoInfo.editorInfo[c].espos < editorInfo.esview)
			{
				editorInfo.esview = editorUndoInfo.editorInfo[c].espos;
				editorInfo.eseditpos = editorUndoInfo.editorInfo[c].espos;
			}
		}
		updateviewtopos();
		rewindsong();
		break;

	case ':':
		for (c = 0; c < MAX_CHN; c++)
		{
			if (editorUndoInfo.editorInfo[c].espos < songlen[editorInfo.esnum][c] - 1)
				editorUndoInfo.editorInfo[c].espos++;
			if (editorUndoInfo.editorInfo[c].espos - editorInfo.esview >= VISIBLEORDERLIST)
			{
				editorInfo.esview = editorUndoInfo.editorInfo[c].espos - VISIBLEORDERLIST + 1;
				editorInfo.eseditpos = editorUndoInfo.editorInfo[c].espos;
			}
		}
		updateviewtopos();
		rewindsong();
		break;

	}
	switch (rawkey)
	{
	// ESC: switch between edit mode and jam mode, as NUMPAD . and the middle
	// mouse button (quit: the window's close button, the QUIT menu, CMD+Q
	// on macOS). SHIFT+ESC: clear / optimize the song
	case KEY_ESC:
		if (!shiftpressed)
		{
			recordmode ^= 1;
			gui_message(recordmode ? "Edit mode: notes are entered" : "Jam mode: notes are only played");
		}
		else
			gui_askclear();
		break;

	case KEY_KPMULTIPLY:
		if ((editorInfo.editmode != EDIT_NAMES) && (!key))
		{
			if (!((editorInfo.editmode == EDIT_INSTRUMENT) && (editorInfo.eipos >= 9)))
			{
				if (editorInfo.epoctave < 7) editorInfo.epoctave++;
			}
		}
		break;

	case KEY_KPDIVIDE:
		if ((editorInfo.editmode != EDIT_NAMES) && (!key))
		{
			if (!((editorInfo.editmode == EDIT_INSTRUMENT) && (editorInfo.eipos >= 9)))
			{
				if (editorInfo.epoctave > 0) editorInfo.epoctave--;
			}
		}
		break;

	// CTRL+Z undo, CTRL+SHIFT+Z or CTRL+Y redo (CMD on macOS)
	case KEY_Z:
		if (ctrlpressed || metapressed)
		{
			if (shiftonlypressed) runcommand(GTCMD_REDO, NULL);
			else undoPerform();
		}
		break;

	case KEY_Y:
		if ((ctrlpressed || metapressed) && (!shiftonlypressed) && (!altpressed))
			runcommand(GTCMD_REDO, NULL);
		break;

	case KEY_F12:
		// The retro interface has the classic online help screen (SHIFT:
		// help for the current editor)
		if (retromode)
			onlinehelp(0, shiftpressed);
		else
			gui_command(GTCMD_HELP);
		break;

	case KEY_TAB:
		if (!shiftpressed) editorInfo.editmode++;
		else editorInfo.editmode--;
		if (editorInfo.editmode > EDIT_NAMES) editorInfo.editmode = EDIT_PATTERN;
		if (editorInfo.editmode < EDIT_PATTERN) editorInfo.editmode = EDIT_NAMES;
		break;

	case KEY_F1:
		// From the start of the current pattern (the patterns shown in the
		// pattern editor). RETURN goes to the song's start
		playpatternstart(followmode ^ shiftpressed);
		break;

	case KEY_F2:
		initsong(editorInfo.esnum, PLAY_POS);
		followplay = followmode ^ shiftpressed;
		followsuspended = 0;
		break;

	case KEY_F3:
		initsong(editorInfo.esnum, PLAY_PATTERN);
		followplay = followmode ^ shiftpressed;
		followsuspended = 0;
		break;

	case KEY_F4:
		if (shiftpressed)
			mutechannel(editorInfo.epchn);
		else
			stopplayback();
		break;

	case KEY_F5:
		if (!shiftpressed)
			editorInfo.editmode = EDIT_PATTERN;
		else prevmultiplier();
		break;

	case KEY_F6:
		if (!shiftpressed)
			editorInfo.editmode = EDIT_ORDERLIST;
		else nextmultiplier();
		break;

	case KEY_F7:
		if (!shiftpressed)
		{
			if (editorInfo.editmode == EDIT_INSTRUMENT)
				editorInfo.editmode = EDIT_TABLES;
			else
				editorInfo.editmode = EDIT_INSTRUMENT;
		}
		else gui_edithradsr();
		break;

	case KEY_F8:
		if (!shiftpressed)
			editorInfo.editmode = EDIT_NAMES;
		else
		{
			editorInfo.sidmodel ^= 1;
			do_sound_init();
		}
		break;

	case KEY_F9:
		if (!shiftpressed) {
		runcommand(GTCMD_EXPORT, NULL);
		}
		else
		{
			toggle_interpolation_mode();
		}
		break;

	case KEY_F10:
		load();
		break;

	case KEY_F11:
		save();
		break;
	}
}

// F10 = open (with SHIFT: merge), in the instrument/table editor load an
// instrument
void load(void)
{
	if ((editorInfo.editmode == EDIT_INSTRUMENT) || (editorInfo.editmode == EDIT_TABLES))
		runcommand(GTCMD_LOADINSTR, NULL);
	else
		runcommand(shiftpressed ? GTCMD_MERGE : GTCMD_OPEN, NULL);
	key = 0;
	rawkey = 0;
}

// F11 = save, in the instrument/table editor save the instrument
void save(void)
{
	if ((editorInfo.editmode == EDIT_INSTRUMENT) || (editorInfo.editmode == EDIT_TABLES))
		runcommand(GTCMD_SAVEINSTR, NULL);
	else
		runcommand(GTCMD_SAVE, NULL);
	key = 0;
	rawkey = 0;
}

// Works like pressing a key (with SHIFT if shift is nonzero) in the current
// editor
static void presskey(int newrawkey, int newkey, int shift)
{
	rawkey = newrawkey;
	key = newkey;
	if ((shift) && (key >= 'a') && (key <= 'z')) key = toupper(key);
	shiftpressed = shift;
	shiftonlypressed = shift;
	ctrlpressed = 0;
	altpressed = 0;
	metapressed = 0;
	menucommand = 0;
	midinoteon = 0;
	midinoteoff = 0;
	converthex();
	docommand();
	key = 0;
	rawkey = 0;
	shiftpressed = 0;
	shiftonlypressed = 0;
}

// Pattern editor commands from the menus work in the pattern editor
static void patternkey(int newrawkey, int newkey, int shift)
{
	editorInfo.editmode = EDIT_PATTERN;
	presskey(newrawkey, newkey, shift);
}

static void startplay(int mode)
{
	initsong(editorInfo.esnum, mode);
	followplay = followmode;
	followsuspended = 0;
}

// Clears everything for a new song
static void newsong(void)
{
	int c;

	stopsong();
	clearsong(1, 1, 1, 1, 1);
	if (resomode > 1) resomark(resomode);
	for (c = 0; c < MAX_CHN; c++)
	{
		editorUndoInfo.editorInfo[c].epnum = c;
		editorUndoInfo.editorInfo[c].espos = 0;
		editorUndoInfo.editorInfo[c].esend = 0;
	}
	editorInfo.eppos = 0;
	editorInfo.epview = -VISIBLEPATTROWS / 2;
	editorInfo.eseditpos = 0;
	editorInfo.esview = 0;
	editorInfo.esnum = 0;
	editorInfo.epmarkchn = -1;
	editorInfo.esmarkchn = -1;
	editorInfo.etmarknum = -1;
	undoInitAllAreas();
	currentsongpath[0] = 0;
	suggestedsongname[0] = 0;
	loadedsongfilename[0] = 0;
	songfilename[0] = 0;
	gui_marksongsaved();
	gui_message("New song");
}

// Clears rows first-last of channel c's pattern (rests, no instruments or
// commands) as one undo step. last < 0: the whole pattern
static void clearpatternrows(int c, int first, int last)
{
	int p = editorUndoInfo.editorInfo[c].epnum;
	int r;

	if ((last < 0) || (last >= pattlen[p])) last = pattlen[p] - 1;
	if (first < 0) first = 0;
	undoAreaSetCheckForChange(UNDO_AREA_PATTERN, p, UNDO_AREA_DIRTY_CHECK);
	for (r = first; r <= last; r++)
	{
		pattern[p][r * 4] = REST;
		pattern[p][r * 4 + 1] = 0;
		pattern[p][r * 4 + 2] = 0;
		pattern[p][r * 4 + 3] = 0;
	}
}

// The pattern menu's and the CLEAR menu's clearing: what = GTCMD_PATTCLEAR
// (the marked rows, nothing marked: the cursor's track), GTCMD_CLEARTRACK
// (the cursor's track) or GTCMD_CLEARPATTERNS (every channel's pattern)
static void clearpatterns(int what)
{
	GTUNDO_OBJECT *ed = undoCreateEditorInfo();
	int c;

	if ((what == GTCMD_PATTCLEAR) && (editorInfo.epmarkchn != -1))
	{
		int first = editorInfo.epmarkstart < editorInfo.epmarkend ? editorInfo.epmarkstart : editorInfo.epmarkend;
		int last = editorInfo.epmarkstart < editorInfo.epmarkend ? editorInfo.epmarkend : editorInfo.epmarkstart;
		clearpatternrows(editorInfo.epmarkchn, first, last);
		editorInfo.epmarkchn = -1;
		gui_message("Marked rows cleared");
	}
	else if (what == GTCMD_CLEARPATTERNS)
	{
		for (c = 0; c < MAX_CHN; c++) clearpatternrows(c, 0, -1);
		gui_message("Patterns %02X %02X %02X cleared", editorUndoInfo.editorInfo[0].epnum, editorUndoInfo.editorInfo[1].epnum, editorUndoInfo.editorInfo[2].epnum);
	}
	else
	{
		clearpatternrows(editorInfo.epchn, 0, -1);
		gui_message("Track %d (pattern %02X) cleared", editorInfo.epchn + 1, editorUndoInfo.editorInfo[editorInfo.epchn].epnum);
	}
	if (undoValidateUndoAreas(ed) == 0) undoFreeUndoObject(ed);
}

// Runs a command from the menus, the toolbar, drag & drop or a shortcut key.
// path is the file of GTCMD_OPENFILE
void runcommand(int cmd, const char *path)
{
	char newpath[MAX_PATHNAME];
	int filetype;

	switch (cmd)
	{
	case GTCMD_NEW:
		if (gui_asksavechanges(cmd, NULL)) break;
		newsong();
		break;

	case GTCMD_OPEN:
		if (filedialog_open(FOLDER_SONG, "Open a song, or import a SID, MIDI, SID-Wizard or chipsynth file", "sng;sid;mid;midi;swm;ins;fermatax", newpath, MAX_PATHNAME))
			runcommand(GTCMD_OPENFILE, newpath);
		break;

	case GTCMD_OPENFILE:
		if ((!path) || (!path[0])) break;
		filetype = gt_filetype(path);
		// Instruments and patches go into the current song
		if ((filetype != GTFILE_INSTRUMENT) && (filetype != GTFILE_PATCH))
		{
			if (gui_asksavechanges(cmd, path)) break;
		}
		gt_openfile(path);
		break;

	case GTCMD_OPENFILEPLAY:
		// A file opened from the Finder or the command line: a song (or a
		// SID / MIDI file, converted at once) is loaded and played
		if ((!path) || (!path[0])) break;
		filetype = gt_filetype(path);
		if ((filetype == GTFILE_INSTRUMENT) || (filetype == GTFILE_PATCH))
		{
			gt_openfile(path);
			break;
		}
		if (gui_asksavechanges(cmd, path)) break;
		if (gt_openfile_direct(path))
			startplay(PLAY_BEGINNING);
		break;

	case GTCMD_SAVE:
		if (currentsongpath[0])
		{
			gui_savefinished(gt_savesong(currentsongpath));
			break;
		}
		// Not saved before: ask for the name
	case GTCMD_SAVEAS:
		{
			const char *suggest = "Untitled.sng";
			if (currentsongpath[0]) suggest = currentsongpath;
			else if (suggestedsongname[0]) suggest = suggestedsongname;
			if (filedialog_save(FOLDER_SONG, "Save the song", "sng", suggest, newpath, MAX_PATHNAME))
				gui_savefinished(gt_savesong(newpath));
			else
				gui_savefinished(0);
		}
		break;

	case GTCMD_MERGE:
		if (filedialog_open(FOLDER_SONG, "Merge a song into this one", "sng", newpath, MAX_PATHNAME))
			gt_mergesong(newpath);
		break;

	case GTCMD_IMPORTMIDI:
		if (filedialog_open(FOLDER_IMPORT, "Import a MIDI file", "mid;midi", newpath, MAX_PATHNAME))
			runcommand(GTCMD_OPENFILE, newpath);
		break;

	case GTCMD_IMPORTSID:
		if (filedialog_open(FOLDER_IMPORT, "Import a SID file", "sid", newpath, MAX_PATHNAME))
			runcommand(GTCMD_OPENFILE, newpath);
		break;

	case GTCMD_IMPORTSIDNOW:
		if ((path) && (path[0])) gt_importsid_run(path, &sidimportoptions);
		break;

	case GTCMD_IMPORTMIDINOW:
		if ((path) && (path[0])) gt_importmidi_run(path);
		break;

	case GTCMD_IMPORTSWM:
		if (filedialog_open(FOLDER_IMPORT, "Import a SID-Wizard module", "swm", newpath, MAX_PATHNAME))
			runcommand(GTCMD_OPENFILE, newpath);
		break;

	case GTCMD_EXPORTSWM:
		{
			char suggest[MAX_PATHNAME];
			char report[256];
			const char *base = "Untitled";

			if (loadedsongfilename[0]) base = gt_basename(loadedsongfilename);
			else if (suggestedsongname[0]) base = suggestedsongname;
			gt_replaceext(base, ".swm", suggest, MAX_PATHNAME);
			if (!filedialog_save(FOLDER_EXPORT, "Export the song as a SID-Wizard module", "swm", suggest, newpath, MAX_PATHNAME))
				break;
			stopsong();
			if (swm_export(newpath, report, sizeof report))
				gui_message("Exported %s: %s", gt_basename(newpath), report);
			else
				gui_alert("SID-Wizard export failed", report);
		}
		break;

	case GTCMD_EXPORTMIDI:
		{
			char suggest[MAX_PATHNAME];
			char report[256];
			const char *base = "Untitled";

			if (loadedsongfilename[0]) base = gt_basename(loadedsongfilename);
			else if (suggestedsongname[0]) base = suggestedsongname;
			gt_replaceext(base, ".mid", suggest, MAX_PATHNAME);
			if (!filedialog_save(FOLDER_EXPORT, "Export the song as a MIDI file", "mid", suggest, newpath, MAX_PATHNAME))
				break;
			if (midiexport(newpath, report, sizeof report))
				gui_message("%s", report);
			else
				gui_alert("MIDI export failed", report);
		}
		break;

	case GTCMD_IMPORTPATCH:
		if (filedialog_open(FOLDER_IMPORT, "Import a chipsynth C64 patch into the current instrument", "fermatax", newpath, MAX_PATHNAME))
			gt_importpatch(newpath);
		break;

	case GTCMD_LOADINSTR:
		if (filedialog_open(FOLDER_INSTR, "Load an instrument or a chipsynth C64 patch into the current slot", "ins;fermatax", newpath, MAX_PATHNAME))
		{
			if (gt_filetype(newpath) == GTFILE_PATCH)
				gt_importpatch(newpath);
			else
				gt_loadinstrument(newpath, editorInfo.einum ? editorInfo.einum : 1);
		}
		break;

	case GTCMD_BROWSEFOLDER:
		if (filedialog_folder("Choose the folder of instruments to browse", browserfolder[0] ? browserfolder : gt_folder(FOLDER_INSTR), newpath, MAX_PATHNAME))
			strcpy(browserfolder, newpath);
		break;

	case GTCMD_SAVEINSTR:
		if (!editorInfo.einum)
		{
			gui_alert("Select an instrument first", "Instrument 00 is the empty instrument and can't be saved.");
			break;
		}
		{
			char suggest[MAX_PATHNAME];
			if (instr[editorInfo.einum].name[0])
				snprintf(suggest, MAX_PATHNAME, "%s.ins", instr[editorInfo.einum].name);
			else
				snprintf(suggest, MAX_PATHNAME, "Instrument %02X.ins", editorInfo.einum);
			if (filedialog_save(FOLDER_INSTR, "Save the instrument", "ins", suggest, newpath, MAX_PATHNAME))
				gt_saveinstrument(newpath, editorInfo.einum);
		}
		break;

	case GTCMD_EXPORT:
		stopsong();
		gui_openexport();
		break;

	case GTCMD_EXPORTNOW:
		{
			const char *ext = (fileformat == FORMAT_PRG) ? "prg" : (fileformat == FORMAT_BIN) ? "bin" : "sid";
			char dotext[8];
			char suggest[MAX_PATHNAME];
			const char *base = "Untitled";

			if (loadedsongfilename[0]) base = gt_basename(loadedsongfilename);
			else if (suggestedsongname[0]) base = suggestedsongname;
			snprintf(dotext, sizeof dotext, ".%s", ext);
			gt_replaceext(base, dotext, suggest, MAX_PATHNAME);
			if (!filedialog_save(FOLDER_EXPORT, "Export the song with its player", ext, suggest, newpath, MAX_PATHNAME))
				break;
			stopsong();
			strcpy(reloctarget, newpath);
			relocquiet = 1;
			relocator();
			relocquiet = 0;
			if (relocresult)
				gui_message("Exported %s. %s", gt_basename(newpath), relocreport);
			else if (relocreport[0])
				gui_alert("Export failed", relocreport);
		}
		break;

	case GTCMD_QUIT:
		if (gui_asksavechanges(cmd, NULL)) break;
		exitprogram = 1;
		break;

	case GTCMD_UNDO:
		if (undoCanUndo()) undoPerform();
		else gui_message("Nothing to undo");
		break;

	case GTCMD_REDO:
		if (!undoRedo()) gui_message("Nothing to redo");
		break;

	// The pattern menu (right click on a pattern): the marked rows, or with
	// nothing marked the cursor's track (the menu put the cursor there). The
	// cursor row stays, so the patterns don't move
	case GTCMD_PATTCUT:
	case GTCMD_PATTCOPY:
		{
			int pos = editorInfo.eppos;
			int markchn = editorInfo.epmarkchn, markstart = editorInfo.epmarkstart, markend = editorInfo.epmarkend;
			editorInfo.editmode = EDIT_PATTERN;
			if (cmd == GTCMD_PATTCUT) presskey(KEY_X, 'x', 1);
			else
			{
				// Copying keeps the rows marked (SHIFT+C lets them go)
				presskey(KEY_C, 'c', 1);
				editorInfo.epmarkchn = markchn;
				editorInfo.epmarkstart = markstart;
				editorInfo.epmarkend = markend;
			}
			editorInfo.eppos = pos;
			gui_message("%s %d rows", cmd == GTCMD_PATTCUT ? "Cut" : "Copied", patterncopyrows);
		}
		break;

	// Paste: at the first row marked; nothing marked, a whole track cut /
	// copied goes to the track's row 0, some rows to the row right-clicked
	case GTCMD_PATTPASTE:
		if (patterncopyrows)
		{
			int pos = editorInfo.eppos;
			editorInfo.editmode = EDIT_PATTERN;
			if (editorInfo.epmarkchn != -1)
			{
				editorInfo.epchn = editorInfo.epmarkchn;
				editorInfo.eppos = editorInfo.epmarkstart < editorInfo.epmarkend ? editorInfo.epmarkstart : editorInfo.epmarkend;
			}
			else if (patterncopywhole) editorInfo.eppos = 0;
			else editorInfo.eppos = retro_pattmenurow;
			presskey(KEY_V, 'v', 1);
			editorInfo.eppos = pos;
			if (editorInfo.eppos > pattlen[editorUndoInfo.editorInfo[editorInfo.epchn].epnum])
				editorInfo.eppos = pattlen[editorUndoInfo.editorInfo[editorInfo.epchn].epnum];
			gui_message("Pasted %d rows", patterncopyrows);
		}
		else gui_message("Nothing to paste: cut or copy rows first");
		break;

	case GTCMD_PATTCLEAR:
	case GTCMD_CLEARTRACK:
	case GTCMD_CLEARPATTERNS:
		clearpatterns(cmd);
		break;

	case GTCMD_CLEARALL:
		gui_askclearall();
		break;

	case GTCMD_CUT:
		presskey(KEY_X, 'x', 1);
		break;

	case GTCMD_COPY:
		presskey(KEY_C, 'c', 1);
		break;

	case GTCMD_PASTE:
		presskey(KEY_V, 'v', 1);
		break;

	case GTCMD_DELETE:
		presskey(KEY_DEL, 0, 0);
		break;

	case GTCMD_SELECTALL:
		if ((editorInfo.editmode == EDIT_PATTERN) || (editorInfo.editmode == EDIT_ORDERLIST))
			presskey(KEY_L, 'l', 1);
		break;

	case GTCMD_CLEARSONG:
		gui_askclear();
		break;

	case GTCMD_HRADSR:
		gui_edithradsr();
		break;

	case GTCMD_PLAYSONG:
		startplay(PLAY_BEGINNING);
		break;

	case GTCMD_PLAYPATTSTART:
		playpatternstart(followmode);
		break;

	case GTCMD_SONGSTART:
		gotosongstart();
		break;

	case GTCMD_PLAYPOS:
		startplay(PLAY_POS);
		break;

	case GTCMD_PLAYPATTERN:
		startplay(PLAY_PATTERN);
		break;

	case GTCMD_PLAYCURSOR:
		playfromcursor(followmode);
		followsuspended = 0;
		break;

	case GTCMD_STOP:
		stopplayback();
		break;

	case GTCMD_PLAYSTOP:
		playstop();
		followsuspended = 0;
		break;

	case GTCMD_RECORD:
		recordmode ^= 1;
		break;

	case GTCMD_FOLLOW:
		followmode ^= 1;
		if (isplaying()) followplay = followmode;
		followsuspended = 0;
		break;

	case GTCMD_OCTAVEUP:
		if (editorInfo.epoctave < 7) editorInfo.epoctave++;
		break;

	case GTCMD_OCTAVEDOWN:
		if (editorInfo.epoctave > 0) editorInfo.epoctave--;
		break;

	case GTCMD_NEXTSUBTUNE:
		nextsong();
		break;

	case GTCMD_PREVSUBTUNE:
		prevsong();
		break;

	case GTCMD_EDITPATTERN:
		editorInfo.editmode = EDIT_PATTERN;
		break;

	case GTCMD_EDITORDER:
		editorInfo.editmode = EDIT_ORDERLIST;
		break;

	case GTCMD_EDITINSTR:
		editorInfo.editmode = (editorInfo.editmode == EDIT_INSTRUMENT) ? EDIT_TABLES : EDIT_INSTRUMENT;
		break;

	case GTCMD_EDITTABLES:
		editorInfo.editmode = EDIT_TABLES;
		break;

	case GTCMD_EDITNAMES:
		editorInfo.editmode = EDIT_NAMES;
		break;

	case GTCMD_TRANSPOSEUP:
		patternkey(KEY_Q, 'q', 1);
		break;

	case GTCMD_TRANSPOSEDOWN:
		patternkey(KEY_A, 'a', 1);
		break;

	case GTCMD_TRANSPOSEOCTUP:
		patternkey(KEY_W, 'w', 1);
		break;

	case GTCMD_TRANSPOSEOCTDOWN:
		patternkey(KEY_S, 's', 1);
		break;

	case GTCMD_INSERTROW:
		patternkey(KEY_INS, 0, 0);
		break;

	case GTCMD_DELETEROW:
		// BACKSPACE deletes the row in the pattern editor (DEL clears)
		patternkey(KEY_BACKSPACE, 8, 0);
		break;

	case GTCMD_SHRINKPATTERN:
		patternkey(KEY_O, 'o', 1);
		break;

	case GTCMD_EXPANDPATTERN:
		patternkey(KEY_P, 'p', 1);
		break;

	case GTCMD_SPLITPATTERN:
		patternkey(KEY_K, 'k', 1);
		break;

	case GTCMD_JOINPATTERN:
		patternkey(KEY_J, 'j', 1);
		break;

	case GTCMD_INVERTSEL:
		patternkey(KEY_I, 'i', 1);
		break;

	case GTCMD_HELP:
		// The retro interface has the classic online help screen
		if (retromode)
			onlinehelp(0, 0);
		else
			gui_command(cmd);
		break;

	default:
		gui_command(cmd);
		break;
	}
}

// Clear dialog result: what to clear, or optimize everything
void clearsongparts(int optimize, int cs, int cp, int ci, int ct, int cn, int patternlength)
{
	stopsong();
	if (optimize)
	{
		optimizeeverything(1, 1);
		gui_message("Song optimized: unused patterns, instruments and table entries removed");
		return;
	}
	if (cp)
	{
		if (patternlength < 1) patternlength = 1;
		if (patternlength > MAX_PATTROWS) patternlength = MAX_PATTROWS;
		defaultpatternlength = patternlength;
	}
	if (cs | cp | ci | ct | cn)
	{
		clearsong(cs, cp, ci, ct, cn);
		// RESO stays when the instruments go
		if ((ci) && (resomode > 1)) resomark(resomode);
		undoInitAllAreas();
		gui_message("Song cleared");
	}
}

// Hard restart ADSR dialog result
void sethradsr(unsigned value)
{
	undoCreateEditorInfoBackup();
	editorInfo.adparam = value & 0xffff;
	undoAddEditorSettingsToList();
}

void getparam(FILE *handle, unsigned *value)
{
	char *configptr;

	for (;;)
	{
		if (feof(handle)) return;
		if (!fgets(configbuf, MAX_PATHNAME, handle)) return;
		if ((configbuf[0]) && (configbuf[0] != ';') && (configbuf[0] != ' ') && (configbuf[0] != 13) && (configbuf[0] != 10)) break;
	}

	configptr = configbuf;
	if (*configptr == '$')
	{
		*value = 0;
		configptr++;
		for (;;)
		{
			char c = tolower(*configptr++);
			int h = -1;

			if ((c >= 'a') && (c <= 'f')) h = c - 'a' + 10;
			if ((c >= '0') && (c <= '9')) h = c - '0';

			if (h >= 0)
			{
				*value *= 16;
				*value += h;
			}
			else break;
		}
	}
	else
	{
		*value = 0;
		for (;;)
		{
			char c = tolower(*configptr++);
			int d = -1;

			if ((c >= '0') && (c <= '9')) d = c - '0';

			if (d >= 0)
			{
				*value *= 10;
				*value += d;
			}
			else break;
		}
	}
}

void getfloatparam(FILE *handle, float *value)
{
	char *configptr;

	for (;;)
	{
		if (feof(handle)) return;
		if (!fgets(configbuf, MAX_PATHNAME, handle)) return;
		if ((configbuf[0]) && (configbuf[0] != ';') && (configbuf[0] != ' ') && (configbuf[0] != 13) && (configbuf[0] != 10)) break;
	}

	configptr = configbuf;
	*value = 0.0f;
	sscanf(configptr, "%f", value);
}

// A whole line (a folder may have spaces in its name), without the line end
void getpathparam(FILE *handle, char *value)
{
	int len;

	for (;;)
	{
		if (feof(handle)) return;
		if (!fgets(configbuf, MAX_PATHNAME, handle)) return;
		if ((configbuf[0]) && (configbuf[0] != ';') && (configbuf[0] != ' ') && (configbuf[0] != 13) && (configbuf[0] != 10)) break;
	}

	len = (int)strlen(configbuf);
	while ((len > 0) && ((configbuf[len - 1] == 13) || (configbuf[len - 1] == 10))) len--;
	configbuf[len] = 0;
	strcpy(value, configbuf);
}

void getstringparam(FILE *handle, char *value)
{
	char *configptr;

	for (;;)
	{
		if (feof(handle)) return;
		if (!fgets(configbuf, MAX_PATHNAME, handle)) return;
		if ((configbuf[0]) && (configbuf[0] != ';') && (configbuf[0] != ' ') && (configbuf[0] != 13) && (configbuf[0] != 10)) break;
	}

	configptr = configbuf;

	sscanf(configptr, "%s", value);
}

void prevmultiplier(void)
{
	if (editorInfo.multiplier > 0)
	{
		editorInfo.multiplier--;
		do_sound_init();
	}
}

void nextmultiplier(void)
{
	if (editorInfo.multiplier < 16)
	{
		editorInfo.multiplier++;
		do_sound_init();
	}
}

void calculatefreqtable()
{
	double basefreq = (double)basepitch * (16777216.0 / 985248.0) * pow(2.0, 0.25) / 32.0;
	double cyclebasefreq = basefreq;
	double freq = basefreq;
	int c;
	int i;

	if (tuningcount)
	{
		c = 0;
		while (c < 96)
		{
			for (i = 0; i < tuningcount; i++)
			{
				if (c < 96)
				{
					int intfreq = freq + 0.5;
					if (intfreq > 0xffff)
						intfreq = 0xffff;
					freqtbllo[c] = intfreq & 0xff;
					freqtblhi[c] = intfreq >> 8;
					freq = cyclebasefreq * tuning[i];
					c++;
				}
			}
			cyclebasefreq = freq;
		}
	}
	else
	{
		for (c = 0; c < 8 * 12; c++)
		{
			double note = c;
			double freq = basefreq * pow(2.0, note / (double)equaldivisionsperoctave);
			int intfreq = freq + 0.5;
			if (intfreq > 0xffff)
				intfreq = 0xffff;
			freqtbllo[c] = intfreq & 0xff;
			freqtblhi[c] = intfreq >> 8;
		}
	}
}

// The tuning a song needs (the SID import gives it, for a tune tuned
// otherwise than GoatTracker's table: an NTSC tune for its 60 Hz C64, say):
// the frequency table for a base pitch, A-4 in Hz on the PAL clock with 12
// notes an octave, or the standard table (0). Like the other editor settings
// it stays for the songs after, and is saved in the configuration
void setbasepitch(float pitch)
{
	basepitch = (pitch > 0.0f) ? pitch : 0.0f;
	equaldivisionsperoctave = 12.0f;
	tuningcount = 0;
	if (basepitch > 0.0f)
		calculatefreqtable();
	else
	{
		memcpy(freqtbllo, defaultfreqtbllo, sizeof defaultfreqtbllo);
		memcpy(freqtblhi, defaultfreqtblhi, sizeof defaultfreqtblhi);
	}
}

void setspecialnotenames()
{
	int i;
	int j;
	int oct;
	char *name;
	char octave[11];

	i = 0;
	oct = 0;
	while (i < 93)
	{
		for (j = 0; j < 186; j += 2)
		{
			if (specialnotenames[j] == '\0')
				break;
			if (i < 93)
			{
				name = malloc(4);
				strncpy(name, specialnotenames + j, 2);
				sprintf(octave, "%d", oct);
				strcpy(name + 2, octave);
				notename[i] = name;
				i++;
			}
		}
		oct++;
	}
}

void readscalatuningfile()
{
	FILE *scalatuningfile;
	char *configptr;
	char strbuf[64];
	char name[3];
	int i;
	double numerator;
	double denominator;
	double centvalue;

	scalatuningfile = fopen(scalatuningfilepath, "rt");
	if (scalatuningfile)
	{
		// Tuning name
		for (;;)
		{
			if (feof(scalatuningfile)) return;
			fgets(configbuf, MAX_PATHNAME, scalatuningfile);
			if ((configbuf[0]) && (configbuf[0] != '!') && (configbuf[0] != 13) && (configbuf[0] != 10)) break;
		}
		configptr = configbuf;
		sscanf(configptr, "%63[^\t\n]", tuningname);

		// Tuning count
		for (;;)
		{
			if (feof(scalatuningfile)) return;
			fgets(configbuf, MAX_PATHNAME, scalatuningfile);
			if ((configbuf[0]) && (configbuf[0] != '!') && (configbuf[0] != 13) && (configbuf[0] != 10)) break;
		}
		configptr = configbuf;
		sscanf(configptr, "%d", &tuningcount);

		// Tunings
		for (i = 0; i < tuningcount; i++)
		{
			for (;;)
			{
				if (feof(scalatuningfile)) return;
				fgets(configbuf, MAX_PATHNAME, scalatuningfile);
				if ((configbuf[0]) && (configbuf[0] != '!') && (configbuf[0] != 13) && (configbuf[0] != 10)) break;
			}
			configptr = configbuf;
			name[0] = '\0';
			sscanf(configptr, "%63s %2s", strbuf, name);
			if (!i)
			{
				strcpy(specialnotenames, name);
			}
			else
			{
				if (i == tuningcount - 1)
				{
					char *tmp = strdup(specialnotenames);
					strcpy(specialnotenames, name);
					strcat(specialnotenames, tmp);
					free(tmp);
				}
				else
				{
					strcat(specialnotenames, name);
				}
			}
			if (!strchr(strbuf, '.'))
			{
				sscanf(strbuf, "%lf", &numerator);
				if (strchr(strbuf, '/'))
				{
					sscanf(strchr(strbuf, '/') + 1, "%lf", &denominator);
					tuning[i] = numerator / denominator;
				}
			}
			else
			{
				sscanf(configptr, "%lf", &centvalue);
				tuning[i] = pow(2.0, centvalue / 1200.0);
			}
		}
		fclose(scalatuningfile);
	}
}
