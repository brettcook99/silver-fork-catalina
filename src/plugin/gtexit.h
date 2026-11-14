/*
 * Plugin builds include this first in the assembler's sources (asm/*.c) and
 * the relocator (greloc.c): their exit() calls on errors go back to the
 * relocator (see gtplugin_exit() in greloc.c) instead of ending the host
 */

#ifndef GTEXIT_H
#define GTEXIT_H

#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif
#if defined(__GNUC__) || defined(__clang__)
__attribute__((noreturn))
#endif
void gtplugin_exit(int code);
#ifdef __cplusplus
}
#endif

#define exit(code) gtplugin_exit(code)

#endif
