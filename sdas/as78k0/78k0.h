/*-------------------------------------------------------------------------
  78k0.h - 78K0 assembler definitions

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
  S_78K0_0OP = 80,
  S_78K0_BYTE_ALU,
  S_78K0_AX_IMM16,
  S_78K0_CONDBR,
  S_78K0_DIVUW,
  S_78K0_INCDEC,
  S_78K0_INCWDECW,
  S_78K0_MOV,
  S_78K0_MOVW,
  S_78K0_MULU,
  S_78K0_STACK,
  S_78K0_BR,
  S_78K0_CALL,
  S_78K0_BITCY,
  S_78K0_BIT1,
  S_78K0_BITBR,
  S_78K0_BITMOV1,
  S_78K0_ROT,
  S_78K0_ROT4,
  S_78K0_DBNZ,
  S_78K0_SEL,
  S_78K0_CALLF,
  S_78K0_CALLT,
  S_78K0_XCH,
  S_78K0_XCHW
};

/* Extended direct-address relocation mode: one class plus optional checks. */
#define R_78K0_SADDR       0x0800
#define R_78K0_SFR         0x0A00
#define R_78K0_EVEN        0x0100
#define R_78K0_MODE_MASK   0x0F00

extern VOID machine (struct mne *mp);
extern VOID minit (void);

#endif
