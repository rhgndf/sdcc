/*-------------------------------------------------------------------------
  k78k0.h - 78K0 assembler definitions

  Copyright (C) 2026

  This program is free software; you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by the
  Free Software Foundation; either version 3, or (at your option) any
  later version.
-------------------------------------------------------------------------*/

#ifndef K78K0_ASM_H
#define K78K0_ASM_H 1

enum
{
  K78K0_X,
  K78K0_A,
  K78K0_C,
  K78K0_B,
  K78K0_E,
  K78K0_D,
  K78K0_L,
  K78K0_H,
  K78K0_AX,
  K78K0_BC,
  K78K0_DE,
  K78K0_HL,
  K78K0_SP,
  K78K0_PSW
};

enum
{
  S_K78K0_0OP = 80,
  S_K78K0_0OP2,
  S_K78K0_ADD,
  S_K78K0_ADDC,
  S_K78K0_ADDW,
  S_K78K0_AND,
  S_K78K0_CMPW,
  S_K78K0_CMP,
  S_K78K0_CONDBR,
  S_K78K0_DIVUW,
  S_K78K0_DEC,
  S_K78K0_DECW,
  S_K78K0_OR,
  S_K78K0_INC,
  S_K78K0_INCW,
  S_K78K0_SUB,
  S_K78K0_SUBC,
  S_K78K0_SUBW,
  S_K78K0_MOV,
  S_K78K0_MOVW,
  S_K78K0_MULU,
  S_K78K0_POP,
  S_K78K0_PUSH,
  S_K78K0_BR,
  S_K78K0_CALL,
  S_K78K0_BITCY,
  S_K78K0_BIT1,
  S_K78K0_BITBR,
  S_K78K0_BITMOV1,
  S_K78K0_ROT,
  S_K78K0_ROT4,
  S_K78K0_DBNZ,
  S_K78K0_SEL,
  S_K78K0_CALLF,
  S_K78K0_CALLT,
  S_K78K0_XCH,
  S_K78K0_XCHW,
  S_K78K0_XOR,
  S_K78K0_2BYTE
};

extern VOID machine (struct mne *mp);
extern VOID minit (void);

#endif
