/*-------------------------------------------------------------------------
  ralloc.c - 78K0 register metadata

  Copyright (C) 2026

  This program is free software; you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by the
  Free Software Foundation; either version 2, or (at your option) any
  later version.
-------------------------------------------------------------------------*/

#include "ralloc.h"
#include "gen.h"
#include "dbuf_string.h"

static int spill_slot_id;

reg_info k78k0_regs[] =
{
  {REG_GPR, K78K0_RB0_X_IDX, "rb0x"},
  {REG_GPR, K78K0_RB0_A_IDX, "rb0a"},
  {REG_GPR, K78K0_RB0_C_IDX, "rb0c"},
  {REG_GPR, K78K0_RB0_B_IDX, "rb0b"},
  {REG_GPR, K78K0_RB0_E_IDX, "rb0e"},
  {REG_GPR, K78K0_RB0_D_IDX, "rb0d"},
  {REG_GPR, K78K0_RB0_L_IDX, "rb0l"},
  {REG_GPR, K78K0_RB0_H_IDX, "rb0h"},

  {REG_GPR, K78K0_RB1_X_IDX, "rb1x"},
  {REG_GPR, K78K0_RB1_A_IDX, "rb1a"},
  {REG_GPR, K78K0_RB1_C_IDX, "rb1c"},
  {REG_GPR, K78K0_RB1_B_IDX, "rb1b"},
  {REG_GPR, K78K0_RB1_E_IDX, "rb1e"},
  {REG_GPR, K78K0_RB1_D_IDX, "rb1d"},
  {REG_GPR, K78K0_RB1_L_IDX, "rb1l"},
  {REG_GPR, K78K0_RB1_H_IDX, "rb1h"},

  {REG_GPR, K78K0_RB2_X_IDX, "rb2x"},
  {REG_GPR, K78K0_RB2_A_IDX, "rb2a"},
  {REG_GPR, K78K0_RB2_C_IDX, "rb2c"},
  {REG_GPR, K78K0_RB2_B_IDX, "rb2b"},
  {REG_GPR, K78K0_RB2_E_IDX, "rb2e"},
  {REG_GPR, K78K0_RB2_D_IDX, "rb2d"},
  {REG_GPR, K78K0_RB2_L_IDX, "rb2l"},
  {REG_GPR, K78K0_RB2_H_IDX, "rb2h"},

  {REG_GPR, K78K0_RB3_X_IDX, "rb3x"},
  {REG_GPR, K78K0_RB3_A_IDX, "rb3a"},
  {REG_GPR, K78K0_RB3_C_IDX, "rb3c"},
  {REG_GPR, K78K0_RB3_B_IDX, "rb3b"},
  {REG_GPR, K78K0_RB3_E_IDX, "rb3e"},
  {REG_GPR, K78K0_RB3_D_IDX, "rb3d"},
  {REG_GPR, K78K0_RB3_L_IDX, "rb3l"},
  {REG_GPR, K78K0_RB3_H_IDX, "rb3h"},

  {REG_CND, K78K0_PSW_IDX,   "psw"},
  {0,       K78K0_SP_IDX,    "sp"},
};

static void
markRematerializable (iCode *ic)
{
  operand *result = IC_RESULT (ic);

  if (!result || !IS_ITEMP (result) || POINTER_SET (ic))
    return;

  if (bitVectnBitsOn (OP_DEFS (result)) != 1 || IS_PARM (result))
    return;

  if (ic->op == ADDRESS_OF && IS_TRUE_SYMOP (IC_LEFT (ic)))
    {
      OP_SYMBOL (result)->remat = 1;
      OP_SYMBOL (result)->rematiCode = ic;
      OP_SYMBOL (result)->usl.spillLoc = NULL;
      return;
    }

  if (ic->op == '=' && IS_SYMOP (IC_RIGHT (ic)) && OP_SYMBOL (IC_RIGHT (ic))->remat &&
      !isOperandGlobal (result) && !OP_SYMBOL (result)->addrtaken)
    {
      OP_SYMBOL (result)->remat = OP_SYMBOL (IC_RIGHT (ic))->remat;
      OP_SYMBOL (result)->rematiCode = OP_SYMBOL (IC_RIGHT (ic))->rematiCode;
      OP_SYMBOL (result)->usl.spillLoc = NULL;
      return;
    }

  if (ic->op == CAST && IS_SYMOP (IC_RIGHT (ic)) && OP_SYMBOL (IC_RIGHT (ic))->remat &&
      !isOperandGlobal (result) && !OP_SYMBOL (result)->addrtaken)
    {
      sym_link *to_type = operandType (IC_LEFT (ic));
      sym_link *from_type = operandType (IC_RIGHT (ic));

      if (IS_PTR (to_type) && IS_PTR (from_type))
        {
          OP_SYMBOL (result)->remat = 1;
          OP_SYMBOL (result)->rematiCode = ic;
          OP_SYMBOL (result)->usl.spillLoc = NULL;
        }
      return;
    }

  if ((ic->op == '+' || ic->op == '-') && IS_OP_LITERAL (IC_RIGHT (ic)) &&
      IS_SYMOP (IC_LEFT (ic)) && OP_SYMBOL (IC_LEFT (ic))->remat)
    {
      OP_SYMBOL (result)->remat = 1;
      OP_SYMBOL (result)->rematiCode = ic;
      OP_SYMBOL (result)->usl.spillLoc = NULL;
      return;
    }

  if (ic->op == '+' && IS_OP_LITERAL (IC_LEFT (ic)) &&
      IS_SYMOP (IC_RIGHT (ic)) && OP_SYMBOL (IC_RIGHT (ic))->remat)
    {
      OP_SYMBOL (result)->remat = 1;
      OP_SYMBOL (result)->rematiCode = ic;
      OP_SYMBOL (result)->usl.spillLoc = NULL;
    }
}

void
k78k0_assignRegisters (ebbIndex *ebbi)
{
  eBBlock **ebbs = ebbi->bbOrder;
  int count = ebbi->count;
  symbol *func_sym = NULL;

  bool in_function = false;

  for (int i = 0; i < count; i++)
    {
      iCode *ic;

      if (ebbs[i]->noPath && (ebbs[i]->entryLabel != entryLabel && ebbs[i]->entryLabel != returnLabel))
        continue;

      for (ic = ebbs[i]->sch; ic; ic = ic->next)
        {
          if (ic->op == FUNCTION)
            in_function = true;

          if (in_function)
            markRematerializable (ic);

          if (ic->op == ENDFUNCTION)
            in_function = false;
        }
    }

  for (int i = 0; i < count; i++)
    {
      iCode *ic;

      if (ebbs[i]->noPath && (ebbs[i]->entryLabel != entryLabel && ebbs[i]->entryLabel != returnLabel))
        continue;

      for (ic = ebbs[i]->sch; ic; ic = ic->next)
        {
          operand *result = IC_RESULT (ic);
          symbol *sym;
          symbol *slot;
          struct dbuf_s dbuf;
          int size;

          if (ic->op == FUNCTION && IC_LEFT (ic) && IS_SYMOP (IC_LEFT (ic)))
            func_sym = OP_SYMBOL (IC_LEFT (ic));

          if (ic->op == ENDFUNCTION)
            {
              func_sym = NULL;
              continue;
            }

          if (!result || !IS_ITEMP (result) || POINTER_SET (ic))
            continue;

          if (!func_sym)
            continue;

          sym = OP_SYMBOL (result);
          size = getSize (sym->type);
          if (size < 1 || size > K78K0_MAX_SCALAR_BYTES || sym->remat || sym->usl.spillLoc || sym->liveTo <= ic->seq)
            continue;

          dbuf_init (&dbuf, 128);
          dbuf_printf (&dbuf, "sloc%d", spill_slot_id++);
          slot = newiTemp (dbuf_c_str (&dbuf));
          dbuf_destroy (&dbuf);

          slot->type = copyLinkChain (sym->type);
          slot->etype = getSpec (slot->type);
          SPEC_SCLS (slot->etype) = S_AUTO;
          SPEC_EXTR (slot->etype) = 0;
          SPEC_STAT (slot->etype) = 0;
          SPEC_VOLATILE (slot->etype) = 0;
          slot->_isparm = 0;
          slot->ismyparm = 0;

          wassertl (currFunc, "78K0 iTemp spill outside of a function.");
          allocLocal (slot);
          currFunc->stack += size;
          if (func_sym && currFunc->stack > func_sym->stack)
            func_sym->stack = currFunc->stack;
          slot->isref = 1;
          slot->stackSpil = 1;

          sym->usl.spillLoc = slot;
          sym->isspilt = sym->spillA = 1;
          sym->stackSpil = 1;
        }
    }

  gen78K0Code (ebbi);
  spill_slot_id = 0;
}
