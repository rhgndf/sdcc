/*-------------------------------------------------------------------------
  ralloc.h - 78K0 register definitions

  Copyright (C) 2026

  This program is free software; you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by the
  Free Software Foundation; either version 2, or (at your option) any
  later version.
-------------------------------------------------------------------------*/

#ifndef K78K0RALLOC_H
#define K78K0RALLOC_H 1

#include "common.h"

#define K78K0_MAX_SCALAR_BYTES 8

enum
{
  K78K0_RB0_X_IDX = 0,
  K78K0_RB0_A_IDX,
  K78K0_RB0_C_IDX,
  K78K0_RB0_B_IDX,
  K78K0_RB0_E_IDX,
  K78K0_RB0_D_IDX,
  K78K0_RB0_L_IDX,
  K78K0_RB0_H_IDX,

  K78K0_RB1_X_IDX,
  K78K0_RB1_A_IDX,
  K78K0_RB1_C_IDX,
  K78K0_RB1_B_IDX,
  K78K0_RB1_E_IDX,
  K78K0_RB1_D_IDX,
  K78K0_RB1_L_IDX,
  K78K0_RB1_H_IDX,

  K78K0_RB2_X_IDX,
  K78K0_RB2_A_IDX,
  K78K0_RB2_C_IDX,
  K78K0_RB2_B_IDX,
  K78K0_RB2_E_IDX,
  K78K0_RB2_D_IDX,
  K78K0_RB2_L_IDX,
  K78K0_RB2_H_IDX,

  K78K0_RB3_X_IDX,
  K78K0_RB3_A_IDX,
  K78K0_RB3_C_IDX,
  K78K0_RB3_B_IDX,
  K78K0_RB3_E_IDX,
  K78K0_RB3_D_IDX,
  K78K0_RB3_L_IDX,
  K78K0_RB3_H_IDX,

  K78K0_PSW_IDX,
  K78K0_SP_IDX
};

enum
{
  REG_GPR = 2,
  REG_CND = 4,
};

typedef struct reg_info
{
  short type;
  short rIdx;
  char *name;
} reg_info;

extern reg_info k78k0_regs[];

void k78k0_assignRegisters (ebbIndex *);

#endif
