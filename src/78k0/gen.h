/*-------------------------------------------------------------------------
  gen.h - 78K0 code generation declarations

  Copyright (C) 2026

  This program is free software; you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by the
  Free Software Foundation; either version 2, or (at your option) any
  later version.
-------------------------------------------------------------------------*/

#ifndef K78K0GEN_H
#define K78K0GEN_H 1

#include "ralloc.h"

typedef enum
{
  K78K0_AOP_INVALID,
  K78K0_AOP_LITERAL,
  K78K0_AOP_DIRECT,
  K78K0_AOP_STACK,
  K78K0_AOP_REGSTK,
  K78K0_AOP_IMMEDIATE,
  K78K0_AOP_RETURN,
}
K78K0_AOP_TYPE;

/* Normalized physical location of an operand. Stack offsets remain symbolic
   because outgoing pushes can change their effective displacement. */
typedef struct asmop
{
  K78K0_AOP_TYPE type;
  unsigned char size;
  const operand *operand;
  const symbol *storage;
  const reg_info *regs[K78K0_MAX_SCALAR_BYTES];
}
asmop;

enum
{
  K78K0_MASK_AX = (1 << K78K0_RB0_X_IDX) | (1 << K78K0_RB0_A_IDX),
  K78K0_MASK_C = 1 << K78K0_RB0_C_IDX,
  K78K0_MASK_B = 1 << K78K0_RB0_B_IDX,
  K78K0_MASK_BC = K78K0_MASK_C | K78K0_MASK_B,
  K78K0_MASK_DE = (1 << K78K0_RB0_E_IDX) | (1 << K78K0_RB0_D_IDX),
  K78K0_MASK_HL = (1 << K78K0_RB0_L_IDX) | (1 << K78K0_RB0_H_IDX),
  K78K0_MASK_ALL = K78K0_MASK_AX | K78K0_MASK_BC | K78K0_MASK_DE | K78K0_MASK_HL,
};

enum
{
  K78K0_ROLE_LEFT = 1u << 0,
  K78K0_ROLE_RIGHT = 1u << 1,
  K78K0_ROLE_RESULT = 1u << 2,
};

typedef struct
{
  unsigned clobbers;
  unsigned left;
  unsigned right;
  unsigned result;
  unsigned left_if_right_spilled;
  unsigned right_if_left_spilled;
  unsigned safe_roles;
}
k78k0_instruction_traits;

void gen78K0Code (iCode *);
void k78k0_emitDebuggerSymbol (const char *);
int k78k0_instructionSize (const char *, const char *);
float k78k0DryInstructionCost (iCode *);
k78k0_instruction_traits k78k0InstructionTraits (const iCode *);
iCode *k78k0HiddenReturnForwardBridge (iCode *, sym_link *);

#endif
