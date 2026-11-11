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

static void mainloopframe(void);
static void retromainloop(void);
static int chooseinterface(void);
static void presskey(int newrawkey, int newkey, int shift);
static void startplay(int mode);

int menu = 0;
//int editorInfo.editmode = EDIT_PATTERN;
int recordmode = 1;
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
int fileformat = FORMAT_PRG;
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
unsigned writer = 0;
unsigned hardsid = 0;
unsigned catweasel = 0;
unsigned interpolate = 3;
unsigned residdelay = 0;
unsigned hardsidbufinteractive = 20;
unsigned hardsidbufplayback = 400;
unsigned uimode = UI_ASK;  // Interface: ask at startup, modern or retro
float basepitch = 0.0f;
float equaldivisionsperoctave = 12.0f;
int tuningcount = 0;
double tuning[96];
extern unsigned bigwindow;

char configbuf[MAX_PATHNAME];
char loadedsongfilename[MAX_FILENAME];
char songfilename[MAX_FILENAME];
char songfilter[MAX_FILENAME];
char songpath[MAX_PATHNAME];
char instrfilename[MAX_FILENAME];
char instrfilter[MAX_FILENAME];
char instrpath[MAX_PATHNAME];
char packedpath[MAX_PATHNAME];

extern char *notename[];
char *programname = PROGRAMNAME;
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
	"-Bxx Set sound buffer length in milliseconds DEFAULT=100",
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
	"-uxx Interface (0 = ask at startup, 1 = modern, 2 = retro) DEFAULT=ask",
	"-N   Use NTSC timing",
	"-P   Use PAL timing (DEFAULT)",
	"-W   Write sound output to a file SIDAUDIO.RAW",
	"-?   Show this info again",
	"-??  Standalone online help window",
};

int usagelen = (sizeof usage / sizeof usage[0]);

int main(int argc, char **argv)
{
	char filename[MAX_PATHNAME];
	char startfile[MAX_PATHNAME];
	FILE *configfile;
	int c;
	int showhelpatstart = 0;

	startfile[0] = 0;

	// DEFAULT SETTINGS
	editorInfo.adparam = 0x0f00;
	editorInfo.editmode = EDIT_PATTERN;
	editorInfo.esmarkchn = -1;
	editorInfo.epoctave = 2;
	editorInfo.epmarkchn = -1;
	editorInfo.etmarknum = -1;
	editorInfo.etlock = 1;
	editorInfo.ntsc = 0;
	editorInfo.multiplier = 1;
	editorInfo.sidmodel = 1;
	editorInfo.finevibrato = 1;
	editorInfo.optimizepulse = 1;
	editorInfo.optimizerealtime = 1;

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
		getparam(configfile, &uimode);
		fclose(configfile);
	}

	// Init pathnames
	initpaths();

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
				sscanf(&argv[c][2], "%u", &uimode);
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
	if (uimode > UI_RETRO) uimode = UI_ASK;

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

	// Reset channels/song
	initchannels();
	clearsong(1, 1, 1, 1, 1);

	undoInitAllAreas();	// Must be called after clearSong. Creates undo buffers, containing duplicates of each GT area.

	gui_marksongsaved();

	// Init sound
	if (!sound_init(b, mr, writer, hardsid, editorInfo.sidmodel, editorInfo.ntsc, editorInfo.multiplier, catweasel, interpolate, customclockrate))
	{
		gui_alert("Sound init failed", "GoatTracker runs without sound. Notice that the song timer won't start.");
	}

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
			";Interface (0 = ask at startup, 1 = modern, 2 = retro)\n%d\n\n",
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
			stepsize,
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
			uimode);
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

// Asks for the modern or the retro interface unless it has been chosen for
// good, and sets it up. Returns 0 if the window was closed instead
static int chooseinterface(void)
{
	int choice = uimode;

	if ((choice == UI_ASK) || (startupmodifierheld()))
	{
		int remember = (uimode != UI_ASK);
		choice = gui_chooseinterface(uimode != UI_ASK ? uimode : UI_MODERN, &remember);
		if (choice == UI_ASK) return 0;
		uimode = remember ? choice : UI_ASK;
	}
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
			followplayback();
			displayupdate();
			getkey();
			macmenu_setstate(recordmode, followmode);
			if ((rawkey) || (key) || (mouseb) || (mousewheel) || (midinoteon) || (midinoteoff) || (menucommand))
			{
				input = 1;
				break;
			}
			if ((win_quitted) || (exitprogram) || (cmd_pending())) break;
		}
		retroatmainscreen = 0;

		if ((input) && (!exitprogram))
		{
			converthex();
			docommand();
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

void toggle_interpolation_mode() {
	interpolate++;
	if (interpolate == 2)  // skip FP only mode
		interpolate++;
	interpolate &= 3;
	stopsong();
	do_sound_init();
}

void docommand(void)
{
	// Menu bar & CMD key commands
	if (menucommands()) return;

	// Numpad play/stop, pattern row jumps, edit/jam mode & channel select
	if (numpadcommands()) return;

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
// - Numpad 0 = play from the start of the pattern / stop (SHIFT: play the
//   other follow way, like SHIFT+F1-F3)
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
		playstoppattern(followmode ^ shiftonlypressed);
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

void midicommands(void)
{
	// Last note played from the MIDI keyboard, and the channel it plays on
	static int lastnote = 0;
	static int lastchn = 0;

	// The instrument designer plays its whole patch
	if (gui_designeractive())
	{
		if ((midinoteon) || (midinoteoff)) gui_designermidi(midinoteon, midinoteoff);
		return;
	}

	if (midinoteon)
	{
		// The pattern editor enters & plays the note like a note key does.
		// Elsewhere (except the songname editor) the note is just played
		if ((editorInfo.editmode != EDIT_PATTERN) && (editorInfo.editmode != EDIT_NAMES))
			playtestnote(midinoteon, editorInfo.einum, editorInfo.epchn);
		lastnote = midinoteon;
		lastchn = editorInfo.epchn;
	}

	// Releasing the key that played last releases the note, like on a
	// synth. Releasing a key that is no longer sounding does nothing
	if ((midinoteoff) && (midinoteoff == lastnote))
	{
		releasenote(lastchn);
		lastnote = 0;
	}
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

	case '<':
		if (((editorInfo.editmode == EDIT_INSTRUMENT) && (editorInfo.eipos != 9)) || (editorInfo.editmode == EDIT_TABLES))
			previnstr();
		break;

	case '>':
		if (((editorInfo.editmode == EDIT_INSTRUMENT) && (editorInfo.eipos != 9)) || (editorInfo.editmode == EDIT_TABLES))
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
	case KEY_ESC:
		if (!shiftpressed)
			gui_askquit();
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

	case KEY_Z:
		if (ctrlpressed || metapressed)
			undoPerform();
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
		initsong(editorInfo.esnum, PLAY_BEGINNING);
		followplay = followmode ^ shiftpressed;
		followsuspended = 0;
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
			stopsong();
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
		if (filedialog_open("Open a song, or import a SID, MIDI or chipsynth file", "sng;sid;mid;midi;ins;fermatax", newpath, MAX_PATHNAME))
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
			if (filedialog_save("Save the song", "sng", suggest, newpath, MAX_PATHNAME))
				gui_savefinished(gt_savesong(newpath));
			else
				gui_savefinished(0);
		}
		break;

	case GTCMD_MERGE:
		if (filedialog_open("Merge a song into this one", "sng", newpath, MAX_PATHNAME))
			gt_mergesong(newpath);
		break;

	case GTCMD_IMPORTMIDI:
		if (filedialog_open("Import a MIDI file", "mid;midi", newpath, MAX_PATHNAME))
			runcommand(GTCMD_OPENFILE, newpath);
		break;

	case GTCMD_IMPORTSID:
		if (filedialog_open("Import a SID file", "sid", newpath, MAX_PATHNAME))
			runcommand(GTCMD_OPENFILE, newpath);
		break;

	case GTCMD_IMPORTSIDNOW:
		if ((path) && (path[0])) gt_importsid_run(path, &sidimportoptions);
		break;

	case GTCMD_IMPORTPATCH:
		if (filedialog_open("Import a chipsynth C64 patch into the current instrument", "fermatax", newpath, MAX_PATHNAME))
			gt_importpatch(newpath);
		break;

	case GTCMD_LOADINSTR:
		if (filedialog_open("Load an instrument or a chipsynth C64 patch into the current slot", "ins;fermatax", newpath, MAX_PATHNAME))
		{
			if (gt_filetype(newpath) == GTFILE_PATCH)
				gt_importpatch(newpath);
			else
				gt_loadinstrument(newpath, editorInfo.einum ? editorInfo.einum : 1);
		}
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
			if (filedialog_save("Save the instrument", "ins", suggest, newpath, MAX_PATHNAME))
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
			if (!filedialog_save("Export the song with its player", ext, suggest, newpath, MAX_PATHNAME))
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
		undoPerform();
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
		stopsong();
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
		patternkey(KEY_DEL, 0, 0);
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

	case GTCMD_ASKINTERFACE:
		uimode = UI_ASK;
		gui_message("The choice of modern or retro interface will be shown at the next start");
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
