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
bitVect *k78k0_partial_allocations;

reg_info k78k0_regs[] =
{
  {REG_GPR, K78K0_RB0_X_IDX, "x"},
  {REG_GPR, K78K0_RB0_A_IDX, "a"},
  {REG_GPR, K78K0_RB0_C_IDX, "c"},
  {REG_GPR, K78K0_RB0_B_IDX, "b"},
  {REG_GPR, K78K0_RB0_E_IDX, "e"},
  {REG_GPR, K78K0_RB0_D_IDX, "d"},
  {REG_CND, K78K0_PSW_IDX,   "psw"},
  {0,       K78K0_SP_IDX,    "sp"},
};

static void
markRematerializable (iCode *ic)
{
  operand *result = IC_RESULT (ic);
  operand *left = IC_LEFT (ic);
  operand *right = IC_RIGHT (ic);
  iCode *remat_ic = NULL;

  if (!result || !IS_ITEMP (result) || POINTER_SET (ic) ||
      bitVectnBitsOn (OP_DEFS (result)) != 1 || IS_PARM (result))
    return;

  if (ic->op == ADDRESS_OF && IS_TRUE_SYMOP (left))
    remat_ic = ic;
  else if ((ic->op == '=' || ic->op == CAST) && IS_SYMOP (right) &&
           OP_SYMBOL (right)->remat && !isOperandGlobal (result) && !OP_SYMBOL (result)->addrtaken)
    {
      if (ic->op == '=')
        remat_ic = OP_SYMBOL (right)->rematiCode;
      else if (IS_PTR (operandType (left)) && IS_PTR (operandType (right)))
        remat_ic = ic;
    }
  else if ((ic->op == '+' || ic->op == '-') && IS_OP_LITERAL (right) &&
           IS_SYMOP (left) && OP_SYMBOL (left)->remat)
    remat_ic = ic;
  else if (ic->op == '+' && IS_OP_LITERAL (left) && IS_SYMOP (right) && OP_SYMBOL (right)->remat)
    remat_ic = ic;

  if (!remat_ic)
    return;

  symbol *sym = OP_SYMBOL (result);
  sym->remat = 1;
  sym->rematiCode = remat_ic;
  sym->usl.spillLoc = NULL;
}

static symbol *
createSpillSlot (symbol *sym, const int size)
{
  symbol *slot = NULL;

  for (symbol *candidate = setFirstItem (spill_slots); candidate;
       candidate = setNextItem (spill_slots))
    {
      symbol *occupant;
      if (getSize (candidate->type) < size)
        continue;
      for (occupant = setFirstItem (candidate->usl.itmpStack); occupant;
           occupant = setNextItem (candidate->usl.itmpStack))
        if (bitVectBitValue (sym->clashes, occupant->key))
          break;
      if (!occupant)
        {
          slot = candidate;
          break;
        }
    }

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
      SPEC_EXTR (slot->etype) = SPEC_STAT (slot->etype) = SPEC_VOLATILE (slot->etype) = 0;
      slot->_isparm = slot->ismyparm = 0;

      wassertl (currFunc, "78K0 iTemp spill outside of a function.");
      allocLocal (slot);
      currFunc->stack += size;
      slot->isref = slot->stackSpil = 1;
      addSetHead (&spill_slots, slot);
    }

  addSetHead (&slot->usl.itmpStack, sym);
  return slot;
}

void
k78k0SpillThis (symbol *sym, bool force_spill)
{
  const int size = getSize (sym->type);

  if (!sym->remat && !sym->usl.spillLoc)
    sym->usl.spillLoc = createSpillSlot (sym, size);

  sym->isspilt = sym->spillA = 1;
  sym->stackSpil = !sym->remat;

  if (force_spill)
    for (int i = 0; i < sym->nRegs; i++)
      sym->regs[i] = NULL;
}

static bool
operandUsesSymbol (const operand *op, const symbol *sym)
{
  return op && IS_SYMOP (op) && OP_SYMBOL_CONST (op) == sym;
}

static bool
isComparison (const int op)
{
  return op == EQ_OP || op == NE_OP || op == '<' || op == '>';
}

static bool
isRegisterSafeUse (const iCode *ic, const symbol *sym, const int size)
{
  const bool uses_left = operandUsesSymbol (IC_LEFT (ic), sym);
  const bool uses_right = operandUsesSymbol (IC_RIGHT (ic), sym);

  if (uses_left &&
      (ic->op == RETURN || ic->op == SEND || ic->op == IPUSH || ic->op == '!' ||
       ic->op == UNARYMINUS || ic->op == GETBYTE || ic->op == GETWORD || ic->op == GETABIT))
    return true;
  if (uses_right && ((ic->op == '=' && !POINTER_SET (ic)) || ic->op == CAST))
    return true;
  if (ic->op == IFX)
    return operandUsesSymbol (IC_COND (ic), sym);

  if (size == 1)
    return (uses_left || uses_right) &&
           (ic->op == '+' || ic->op == '-' || ic->op == '*' || ic->op == '/' || ic->op == '%' ||
            ic->op == BITWISEAND || ic->op == '|' || ic->op == '^' || ic->op == LEFT_OP ||
            ic->op == RIGHT_OP || ic->op == ROT || isComparison (ic->op));

  if (size != 2)
    return false;

  if (ic->op == GET_VALUE_AT_ADDRESS || ic->op == SET_VALUE_AT_ADDRESS || ic->op == PCALL)
    return uses_left;
  if (ic->op == '+' || ic->op == '-')
    return (uses_left && IS_OP_LITERAL (IC_RIGHT (ic))) ||
           (ic->op == '+' && IS_OP_LITERAL (IC_LEFT (ic)) && uses_right);
  if (ic->op == BITWISEAND || ic->op == '|' || ic->op == '^')
    return uses_left || uses_right;
  if (ic->op == '*')
    return (uses_left && IS_OP_LITERAL (IC_RIGHT (ic)) && operandLitValueUll (IC_RIGHT (ic)) <= 255) ||
           (uses_right && IS_OP_LITERAL (IC_LEFT (ic)) && operandLitValueUll (IC_LEFT (ic)) <= 255);
  if (isComparison (ic->op))
    return uses_left && IS_OP_LITERAL (IC_RIGHT (ic));
  return POINTER_SET (ic) && operandUsesSymbol (IC_RESULT (ic), sym);
}

static bool
hasRegisterSafeUses (const symbol *sym, const int size)
{
  for (int key = 0; key < sym->uses->size; key++)
    {
      if (!bitVectBitValue (sym->uses, key))
        continue;
      const iCode *ic = hTabItemWithKey (iCodehTab, key);

      if (!ic || !isRegisterSafeUse (ic, sym, size))
        return false;
    }

  return true;
}

static bool
hasRegisterSafeDefinitions (const symbol *sym, const int size)
{
  bool found = false;

  for (int key = 0; key < sym->defs->size; key++)
    {
      if (!bitVectBitValue (sym->defs, key))
        continue;
      const iCode *ic = hTabItemWithKey (iCodehTab, key);

      found = true;
      /* Wide arithmetic still uses BC internally; only canonical ABI/copy results are safe. */
      if (!ic || (size > 2 && ic->op != CALL && ic->op != PCALL &&
                  (ic->op != '=' || POINTER_SET (ic))))
        return false;
    }

  return found;
}

void
k78k0_assignRegisters (ebbIndex *ebbi)
{
  int key;

  deleteSet (&spill_slots);
  spill_slot_id = 0;
  freeBitVect (k78k0_partial_allocations);
  k78k0_partial_allocations = NULL;

  for (int i = 0; i < ebbi->count; i++)
    {
      eBBlock *ebb = ebbi->bbOrder[i];
      if (ebb->noPath && ebb->entryLabel != entryLabel && ebb->entryLabel != returnLabel)
        continue;

      for (iCode *ic = ebb->sch; ic; ic = ic->next)
        markRematerializable (ic);
    }

  for (symbol *sym = hTabFirstItem (liveRanges, &key); sym; sym = hTabNextItem (liveRanges, &key))
    {
      const int size = getSize (sym->type);

      sym->nRegs = 0;
      sym->for_newralloc = 0;
      if (!sym->isitmp || sym->regType == REG_CND || sym->remat || size < 1)
        continue;

      sym->nRegs = size;
      sym->regType = REG_GPR;
      sym->for_newralloc = size <= 4 && sym->liveTo > sym->liveFrom &&
                           hasRegisterSafeDefinitions (sym, size) && hasRegisterSafeUses (sym, size);
    }

  iCode *ic_head = k78k0_ralloc2_cc (ebbi);

  /* Hidden destinations need storage even when the call result itself is unused. */
  for (symbol *sym = hTabFirstItem (liveRanges, &key); sym; sym = hTabNextItem (liveRanges, &key))
    if (sym->isitmp && !sym->remat && !sym->isspilt && !sym->regs[0] &&
        sym->nRegs > 0 && (sym->liveTo > sym->liveFrom || sym->nRegs > 4 || IS_STRUCT (sym->type)))
      k78k0SpillThis (sym, true);

  if (options.dump_i_code)
    dumpEbbsToFileExt (DUMP_RASSGN, ebbi);

  gen78K0Code (ic_head);
  freeBitVect (k78k0_partial_allocations);
  k78k0_partial_allocations = NULL;
  deleteSet (&spill_slots);
  spill_slot_id = 0;
}
