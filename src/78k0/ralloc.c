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
static set *spill_slots;

static void
setRematerializable (operand *result, iCode *remat_ic)
{
  symbol *sym = OP_SYMBOL (result);

  sym->remat = 1;
  sym->rematiCode = remat_ic;
  sym->usl.spillLoc = NULL;
}

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
      setRematerializable (result, ic);
      return;
    }

  if ((ic->op == '=' || ic->op == CAST) && IS_SYMOP (IC_RIGHT (ic)) &&
      OP_SYMBOL (IC_RIGHT (ic))->remat && !isOperandGlobal (result) &&
      !OP_SYMBOL (result)->addrtaken)
    {
      if (ic->op == '=')
        setRematerializable (result, OP_SYMBOL (IC_RIGHT (ic))->rematiCode);
      else if (IS_PTR (operandType (IC_LEFT (ic))) && IS_PTR (operandType (IC_RIGHT (ic))))
        setRematerializable (result, ic);
      return;
    }

  if ((ic->op == '+' || ic->op == '-') && IS_OP_LITERAL (IC_RIGHT (ic)) &&
      IS_SYMOP (IC_LEFT (ic)) && OP_SYMBOL (IC_LEFT (ic))->remat)
    {
      setRematerializable (result, ic);
      return;
    }

  if (ic->op == '+' && IS_OP_LITERAL (IC_LEFT (ic)) &&
      IS_SYMOP (IC_RIGHT (ic)) && OP_SYMBOL (IC_RIGHT (ic))->remat)
    setRematerializable (result, ic);
}

static bool
spillSlotAvailable (const symbol *slot, const symbol *sym, const int size)
{
  symbol *occupant;

  if (getSize (slot->type) < size)
    return false;

  for (occupant = setFirstItem (slot->usl.itmpStack); occupant; occupant = setNextItem (slot->usl.itmpStack))
    if (bitVectBitValue (sym->clashes, occupant->key))
      return false;

  return true;
}

static symbol *
findSpillSlot (const symbol *sym, const int size)
{
  symbol *slot;

  for (slot = setFirstItem (spill_slots); slot; slot = setNextItem (spill_slots))
    if (spillSlotAvailable (slot, sym, size))
      return slot;

  return NULL;
}

static symbol *
createSpillSlot (symbol *sym, const int size)
{
  symbol *slot = findSpillSlot (sym, size);

  if (!slot)
    {
      struct dbuf_s dbuf;

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
      slot->isref = 1;
      slot->stackSpil = 1;
      addSetHead (&spill_slots, slot);
    }

  addSetHead (&slot->usl.itmpStack, sym);
  return slot;
}

static bool
callResultNeedsHiddenDestination (const iCode *ic, const symbol *sym, const int size)
{
  return (ic->op == CALL || ic->op == PCALL) && (IS_STRUCT (sym->type) || size > 4);
}

static bool
blockIsReachable (const eBBlock *ebb)
{
  return !ebb->noPath || ebb->entryLabel == entryLabel || ebb->entryLabel == returnLabel;
}

void
k78k0_assignRegisters (ebbIndex *ebbi)
{
  eBBlock **ebbs = ebbi->bbOrder;
  int count = ebbi->count;
  symbol *func_sym = NULL;

  for (int i = 0; i < count; i++)
    {
      iCode *ic;

      if (!blockIsReachable (ebbs[i]))
        continue;

      for (ic = ebbs[i]->sch; ic; ic = ic->next)
        markRematerializable (ic);
    }

  for (int i = 0; i < count; i++)
    {
      iCode *ic;

      if (!blockIsReachable (ebbs[i]))
        continue;

      for (ic = ebbs[i]->sch; ic; ic = ic->next)
        {
          operand *result = IC_RESULT (ic);
          bool hidden_call_result;
          symbol *sym;
          int size;

          if (ic->op == FUNCTION && IC_LEFT (ic) && IS_SYMOP (IC_LEFT (ic)))
            {
              deleteSet (&spill_slots);
              func_sym = OP_SYMBOL (IC_LEFT (ic));
            }

          if (ic->op == ENDFUNCTION)
            {
              deleteSet (&spill_slots);
              func_sym = NULL;
              continue;
            }

          if (!result || !IS_ITEMP (result) || POINTER_SET (ic))
            continue;

          if (!func_sym)
            continue;

          sym = OP_SYMBOL (result);
          size = getSize (sym->type);
          hidden_call_result = callResultNeedsHiddenDestination (ic, sym, size);
          if (size < 1 || (!hidden_call_result && size > K78K0_MAX_SCALAR_BYTES) ||
              sym->remat || sym->usl.spillLoc || (!hidden_call_result && sym->liveTo <= ic->seq))
            continue;

          sym->usl.spillLoc = createSpillSlot (sym, size);
          sym->isspilt = sym->spillA = 1;
          sym->stackSpil = 1;

          if (func_sym && currFunc->stack > func_sym->stack)
            func_sym->stack = currFunc->stack;
        }
    }

  if (options.dump_i_code)
    dumpEbbsToFileExt (DUMP_RASSGN, ebbi);

  gen78K0Code (ebbi);
  deleteSet (&spill_slots);
  spill_slot_id = 0;
}
