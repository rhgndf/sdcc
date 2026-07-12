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
blockIsReachable (const eBBlock *ebb)
{
  return !ebb->noPath || ebb->entryLabel == entryLabel || ebb->entryLabel == returnLabel;
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
isByteBinaryOperation (const int op)
{
  return op == '+' || op == '-' || op == '*' || op == '/' || op == '%' ||
         op == BITWISEAND || op == '|' || op == '^' || op == LEFT_OP || op == RIGHT_OP ||
         isComparison (op);
}

static bool
isRegisterSafeUse (const iCode *ic, const symbol *sym, const int size)
{
  const bool uses_left = operandUsesSymbol (IC_LEFT (ic), sym);
  const bool uses_right = operandUsesSymbol (IC_RIGHT (ic), sym);

  if ((ic->op == RETURN || ic->op == SEND) && uses_left)
    return true;
  if (ic->op == '=' && !POINTER_SET (ic) && uses_right)
    return true;
  if (ic->op == IFX)
    return operandUsesSymbol (IC_COND (ic), sym);
  if (ic->op == IPUSH)
    return uses_left;

  if (size == 1)
    {
      if (ic->op == CAST)
        return uses_right;
      if (ic->op == '!' || ic->op == UNARYMINUS || ic->op == GETBYTE ||
          ic->op == GETWORD || ic->op == GETABIT)
        return uses_left;
      return isByteBinaryOperation (ic->op) && (uses_left || uses_right);
    }

  if (size != 2)
    return false;

  if (ic->op == GET_VALUE_AT_ADDRESS || ic->op == SET_VALUE_AT_ADDRESS || ic->op == PCALL)
    return uses_left;
  if (ic->op == '+' || ic->op == '-')
    return (uses_left && IS_OP_LITERAL (IC_RIGHT (ic))) ||
           (ic->op == '+' && IS_OP_LITERAL (IC_LEFT (ic)) && uses_right);
  if (isComparison (ic->op))
    return uses_left && IS_OP_LITERAL (IC_RIGHT (ic));
  return POINTER_SET (ic) && operandUsesSymbol (IC_RESULT (ic), sym);
}

static bool
hasRegisterSafeUses (const symbol *sym, const int size)
{
  for (int key = 0; key < sym->uses->size; key++)
    if (bitVectBitValue (sym->uses, key))
      {
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
    if (bitVectBitValue (sym->defs, key))
      {
        const iCode *ic = hTabItemWithKey (iCodehTab, key);

        found = true;
        if (!ic)
          return false;
        /* Wide arithmetic still uses BC internally; only canonical ABI/copy results are safe. */
        if (size > 2 && ic->op != CALL && ic->op != PCALL &&
            (ic->op != '=' || POINTER_SET (ic)))
          return false;
      }

  return found;
}

static bool
needsSpillStorage (const symbol *sym)
{
  /* Hidden destinations are needed even when the call result itself is unused. */
  return sym->liveTo > sym->liveFrom || sym->nRegs > 4 || IS_STRUCT (sym->type);
}

void
k78k0_assignRegisters (ebbIndex *ebbi)
{
  eBBlock **ebbs = ebbi->bbOrder;
  int count = ebbi->count;
  iCode *ic_head;
  symbol *sym;
  int key;

  deleteSet (&spill_slots);
  spill_slot_id = 0;

  for (int i = 0; i < count; i++)
    {
      iCode *ic;

      if (!blockIsReachable (ebbs[i]))
        continue;

      for (ic = ebbs[i]->sch; ic; ic = ic->next)
        markRematerializable (ic);
    }

  for (sym = hTabFirstItem (liveRanges, &key); sym; sym = hTabNextItem (liveRanges, &key))
    {
      const int size = getSize (sym->type);

      sym->for_newralloc = 0;
      sym->nRegs = 0;
      if (!sym->isitmp || sym->regType == REG_CND || sym->remat || size < 1)
        continue;

      sym->nRegs = size;
      sym->regType = REG_GPR;
      if (size <= 4 && sym->liveTo > sym->liveFrom &&
          hasRegisterSafeDefinitions (sym, size) && hasRegisterSafeUses (sym, size))
        sym->for_newralloc = 1;
    }

  ic_head = k78k0_ralloc2_cc (ebbi);

  for (sym = hTabFirstItem (liveRanges, &key); sym; sym = hTabNextItem (liveRanges, &key))
    if (sym->isitmp && !sym->remat && !sym->isspilt && !sym->regs[0] &&
        sym->nRegs > 0 && needsSpillStorage (sym))
      k78k0SpillThis (sym, true);

  if (options.dump_i_code)
    dumpEbbsToFileExt (DUMP_RASSGN, ebbi);

  gen78K0Code (ic_head);
  deleteSet (&spill_slots);
  spill_slot_id = 0;
}
