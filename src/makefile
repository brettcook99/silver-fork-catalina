#
# Linux makefile
#

CFLAGS+=`sdl-config --cflags`
PREFIX=../linux/
SUFFIX=
LIBS+=`sdl-config --libs`

# MIDI input uses CoreMIDI on macOS, the menu bar Cocoa
ifeq ($(shell uname -s),Darwin)
LIBS+=-framework CoreMIDI -framework CoreFoundation -framework Cocoa
MACOBJ=gmacmenu.o
endif

include makefile.common

ifeq ($(shell uname -s),Darwin)
gmacmenu.o: gmacmenu.m gmacmenu.h
	$(CC) $(CFLAGS) -c -o $@ $<
endif
