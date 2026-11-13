/*
 * The command line packer (gt2reloc) shares the tracker's modules but has
 * no user interface: these stand in for the GUI functions they refer to.
 */

#include "gui/gui.h"
#include "gretro.h"

void retro_printstatus(void) { }
int gui_init(void) { return 0; }
void gui_shutdown(void) { }
void gui_frame(int textscreen) { (void)textscreen; }
int gui_textscreenmousebuttons(void) { return 0; }
int gui_textscreenmousewheel(void) { return 0; }
void gui_changetextsize(int dir) { (void)dir; }
void gui_togglefullscreen(void) { }
void gui_message(const char *fmt, ...) { (void)fmt; }
int gui_designeractive(void) { return 0; }
void gui_designermidicc(int cc, int value) { (void)cc; (void)value; }
void gui_midipatch(int cc) { (void)cc; }
