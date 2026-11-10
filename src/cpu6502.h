#ifndef CPU6502_H
#define CPU6502_H

// 6502 CPU emulation, ported from SIDDump by Lasse Oorni et al.
// (BSD license, see cpu6502.c). Flat 64K RAM, no I/O or ROMs: the caller
// looks at / pokes the memory between steps (e.g. SID registers at $D400).

typedef struct
{
  unsigned short pc;
  unsigned char a;
  unsigned char x;
  unsigned char y;
  unsigned char flags;
  unsigned char sp;
  unsigned int cycles;              // Cycles since cpu6502_init()
  unsigned short errorpc;           // Where cpu6502_step() failed
  unsigned char errorop;            // and the opcode
  unsigned char mem[0x10000];
} CPU6502;

// Starts executing at newpc with the given registers, an empty stack and
// flags cleared. Memory is left as it is
void cpu6502_init(CPU6502 *cpu, unsigned short newpc, unsigned char newa, unsigned char newx, unsigned char newy);

// Executes one instruction. Returns 1 to continue, 0 when the routine has
// returned (RTS/RTI with an empty stack, or BRK), -1 on a CPU halt (JAM) or
// an unknown opcode (errorpc/errorop tell which)
int cpu6502_step(CPU6502 *cpu);

#endif
