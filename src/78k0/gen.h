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

void gen78K0Code (iCode *);
void k78k0_emitDebuggerSymbol (const char *);

#endif
