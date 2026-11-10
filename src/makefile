#
# Linux / macOS makefile (SDL2)
#
# SDLCONFIG can point to another sdl2-config, e.g. the one build_catalina.sh
# writes for the bundled SDL2 framework

SDLCONFIG?=sdl2-config
CFLAGS+=`$(SDLCONFIG) --cflags`
PREFIX=../linux/
SUFFIX=
LIBS+=`$(SDLCONFIG) --libs`

# MIDI input uses CoreMIDI on macOS; the menu bar and file panels Cocoa
ifeq ($(shell uname -s),Darwin)
LIBS+=-framework CoreMIDI -framework CoreFoundation -framework Cocoa
MACOBJ=gmacmenu.o gmacfile.o
endif

include makefile.common

ifeq ($(shell uname -s),Darwin)
gmacmenu.o: gmacmenu.m gmacmenu.h gcommand.h
	$(CC) $(CFLAGS) -c -o $@ $<

gmacfile.o: gmacfile.m gmacfile.h
	$(CC) $(CFLAGS) -c -o $@ $<
endif
