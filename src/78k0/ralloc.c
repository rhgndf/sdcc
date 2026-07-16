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

static int spill_slot_id;
static set *spill_slots;

reg_info k78k0_regs[] =
{
  {REG_GPR, K78K0_RB0_X_IDX, "x"},
  {REG_GPR, K78K0_RB0_A_IDX, "a"},
  {REG_GPR, K78K0_RB0_C_IDX, "c"},
  {REG_GPR, K78K0_RB0_B_IDX, "b"},
  {REG_GPR, K78K0_RB0_E_IDX, "e"},
  {REG_GPR, K78K0_RB0_D_IDX, "d"},
  {REG_GPR, K78K0_RB0_L_IDX, "l"},
  {REG_GPR, K78K0_RB0_H_IDX, "h"},
  {REG_CND, K78K0_PSW_IDX,   "psw"},
  {0,       K78K0_SP_IDX,    "sp"},
};

static void
markRematerializable (iCode *ic)
{
  operand *result = IC_RESULT (ic);
  operand *left = IC_LEFT (ic);
  operand *right = IC_RIGHT (ic);
  symbol *sym = IS_ITEMP (result) ? OP_SYMBOL (result) : NULL;
  iCode *remat_ic = NULL;

  if (!sym || POINTER_SET (ic) || bitVectnBitsOn (sym->defs) != 1 || sym->_isparm)
    return;

  if (ic->op == ADDRESS_OF && IS_TRUE_SYMOP (left))
    remat_ic = ic;
  else if ((ic->op == '=' || ic->op == CAST) && IS_SYMOP (right) &&
           OP_SYMBOL (right)->remat && !isOperandGlobal (result) && !sym->addrtaken)
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

  sym->remat = 1;
  sym->rematiCode = remat_ic;
  sym->usl.spillLoc = NULL;
}

static symbol *
createSpillSlot (symbol *sym)
{
  const int size = getSize (sym->type);

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
          addSetHead (&candidate->usl.itmpStack, sym);
          return candidate;
        }
    }

  char name[32];
  SNPRINTF (name, sizeof (name), "sloc%d", spill_slot_id++);
  symbol *slot = newiTemp (name);

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

  addSetHead (&slot->usl.itmpStack, sym);
  return slot;
}

void
k78k0SpillThis (symbol *sym)
{
  if (!sym->remat && !sym->usl.spillLoc)
    sym->usl.spillLoc = createSpillSlot (sym);
  if (!sym->remat && !sym->usl.spillLoc->allocreq)
    sym->usl.spillLoc->allocreq++;

  sym->isspilt = sym->spillA = 1;
  sym->stackSpil = !sym->remat;

  memset (sym->regs, 0, sizeof sym->regs);
}

static void
disableForbiddenOperand (operand *op, const unsigned forbidden)
{
  if (forbidden == K78K0_MASK_ALL && IS_ITEMP (op))
    OP_SYMBOL (op)->for_newralloc = false;
}

static void
disableUnsupportedOperands (iCode *ic)
{
  const k78k0_instruction_traits traits = k78k0InstructionTraits (ic);

  disableForbiddenOperand (IC_LEFT (ic), traits.left);
  disableForbiddenOperand (IC_RIGHT (ic), traits.right);
  if (POINTER_SET (ic))
    disableForbiddenOperand (IC_RESULT (ic), traits.result);
}

static bool
isDirectlyForwardedResult (const symbol *sym)
{
  if (!sym || !currFunc || bitVectnBitsOn (sym->defs) != 1)
    return false;

  iCode *call = hTabItemWithKey (iCodehTab, bitVectFirstBit (sym->defs));
  return call && k78k0ReturnForwardBridge (call, currFunc->type);
}

void
k78k0_assignRegisters (ebbIndex *ebbi)
{
  int key;

  deleteSet (&spill_slots);
  spill_slot_id = 0;

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
      if (!sym->isitmp || sym->regType == REG_CND || sym->remat || size < 1 ||
          size > K78K0_MAX_SCALAR_BYTES || IS_STRUCT (sym->type))
        continue;

      sym->nRegs = size;
      sym->regType = REG_GPR;
      /* Wide lowerings use the fixed AX/BC result registers as scratch, so
         keep values wider than a word in stack storage. */
      sym->for_newralloc = size <= 2 && sym->liveTo > sym->liveFrom &&
                           !bitVectIsZero (sym->defs);
    }

  /* Keep operands that no lowering can consume in registers out of the
     conflict graph, and retain true local variables in the compacted frame. */
  for (int i = 0; i < ebbi->count; i++)
    for (iCode *ic = ebbi->bbOrder[i]->sch; ic; ic = ic->next)
      {
        if (IC_RESULT (ic) && ic->op != IFX && IS_TRUE_SYMOP (IC_RESULT (ic)))
          OP_SYMBOL (IC_RESULT (ic))->allocreq++;
        disableUnsupportedOperands (ic);
      }

  iCode *ic_head = k78k0_ralloc2_cc (ebbi);

  /* Graph-absent values need storage unless a call forwards them to RETURN. */
  for (symbol *sym = hTabFirstItem (liveRanges, &key); sym; sym = hTabNextItem (liveRanges, &key))
    {
      const int size = getSize (sym->type);

      if (sym->isitmp && sym->regType != REG_CND && size > 0 && !sym->remat &&
          !sym->regs[0] &&
          (sym->liveTo > sym->liveFrom || size > 4 || IS_STRUCT (sym->type)) &&
          !isDirectlyForwardedResult (sym))
        {
          if (!sym->usl.spillLoc)
            k78k0SpillThis (sym);
          else if (!sym->usl.spillLoc->allocreq)
            sym->usl.spillLoc->allocreq++;
        }
    }

  if (currFunc)
    redoStackOffsets ();

  if (options.dump_i_code)
    dumpEbbsToFileExt (DUMP_RASSGN, ebbi);

  gen78K0Code (ic_head);
  deleteSet (&spill_slots);
}
