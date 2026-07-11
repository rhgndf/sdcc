/*-------------------------------------------------------------------------
  gen.c - 78K0 code generation

  Copyright (C) 2026

  This program is free software; you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by the
  Free Software Foundation; either version 2, or (at your option) any
  later version.
-------------------------------------------------------------------------*/

#include "gen.h"
#include "SDCCpeeph.h"

#define K78K0_RETURN_ADDRESS_BYTES 2
#define K78K0_PRESERVED_DE_BYTES 2

static const symbol *a_result_sym = NULL;
static const symbol *return_result_sym = NULL;
static int return_result_size = 0;
static bool return_result_ax_valid = false;
static bool hl_is_sp = false;
static bool hl_sp_offset_valid = false;
static int hl_sp_offset = 0;
static int stack_pushed = 0;
static int local_stack_size = 0;
static int current_return_size = 0;
static bool current_return_via_hidden_pointer = false;
static int current_param_offset = 0;
static int current_stack_cleanup_size = 0;
static bool current_function_is_isr = false;
static unsigned local_label_key = 0;
static unsigned ic_label_key = 0;

static void genCritical (void);
static void genEndCritical (void);
static void emitByteLeftShift (void);
static void emitByteRightShift (void);
typedef struct labelMap
{
  const symbol *symbol;
  unsigned label;
  struct labelMap *next;
}
labelMap;

static labelMap *ic_label_map = NULL;

static void
emit2 (const char *inst, const char *fmt, ...)
{
  va_list ap;

  va_start (ap, fmt);
  va_emitcode (inst, fmt, ap);
  va_end (ap);
}

static void
clearAResult (void)
{
  a_result_sym = NULL;
  return_result_sym = NULL;
  return_result_size = 0;
  return_result_ax_valid = false;
}

static void
clearHLState (void)
{
  hl_is_sp = false;
  hl_sp_offset_valid = false;
}

static void
clearRegisterState (void)
{
  clearAResult ();
  clearHLState ();
}

static bool
storeAccumulatorToStack (const symbol *sym, int size);

static bool
symbolIsSfr (const symbol *sym)
{
  return sym && sym->etype && SPEC_SCLS (sym->etype) == S_SFR;
}

static bool
genOperandReturnValue (const operand *op);

static bool
loadFunctionAddressToAX (const operand *op);

static bool
loadRematerializedAddressToAX (const operand *op);

static void
emitSignMaskForA (void);

static const symbol *
operandStorageSymbol (const operand *op)
{
  const symbol *sym = OP_SYMBOL_CONST (op);

  if (IS_ITEMP (op) && sym->usl.spillLoc && !sym->remat &&
      !IS_FUNC (sym->usl.spillLoc->type))
    return sym->usl.spillLoc;

  return sym;
}

static const operand *
operandReqv (const operand *op)
{
  const symbol *sym;

  if (!IS_SYMOP (op) || !IS_ITEMP (op))
    return NULL;

  sym = operandStorageSymbol (op);
  if (sym != OP_SYMBOL_CONST (op) || !sym->reqv || sym->reqv == op)
    return NULL;

  return sym->reqv;
}

static const operand *
resolveReqvOperand (const operand *op)
{
  const operand *seen[32];
  const operand *current = op;
  int seen_count = 0;

  while (current && seen_count < (int)(sizeof (seen) / sizeof (seen[0])))
    {
      const operand *next;

      seen[seen_count++] = current;
      next = operandReqv (current);
      if (!next)
        return current;

      for (int i = 0; i < seen_count; i++)
        if (seen[i] == next)
          return op;

      current = next;
    }

  return op;
}

static void
makeLocalLabel (char *buf, size_t buflen)
{
  SNPRINTF (buf, buflen, "L78K0%05u", 60000u + local_label_key++);
}

static void
makeICLabel (char *buf, size_t buflen, const symbol *label)
{
  for (labelMap *entry = ic_label_map; entry; entry = entry->next)
    if (entry->symbol == label)
      {
        SNPRINTF (buf, buflen, "L78K0%05u", entry->label);
        return;
      }

  labelMap *entry = Safe_alloc (sizeof (*entry));

  entry->symbol = label;
  entry->label = 20000u + ic_label_key++;
  entry->next = ic_label_map;
  ic_label_map = entry;

  SNPRINTF (buf, buflen, "L78K0%05u", entry->label);
}

static void
emitLocalLabel (const char *label)
{
  clearRegisterState ();
  emit2 ("", "%s:", label);
  genLine.lineCurr->isLabel = 1;
}

static const char *
inverseCondBranch (const char *inst)
{
  if (!strcmp (inst, "bz"))
    return "bnz";
  if (!strcmp (inst, "bnz"))
    return "bz";
  if (!strcmp (inst, "bc"))
    return "bnc";
  if (!strcmp (inst, "bnc"))
    return "bc";

  wassertl (0, "unsupported 78K0 conditional branch");
  return "br";
}

static void
emitCondBranch (const char *inst, const char *label)
{
  char skip_label[32];

  makeLocalLabel (skip_label, sizeof (skip_label));
  emit2 (inverseCondBranch (inst), "%s", skip_label);
  emit2 ("br", "!%s", label);
  emit2 ("", "%s:", skip_label);
  genLine.lineCurr->isLabel = 1;
}

static void
setHLToSP (void)
{
  if (hl_is_sp)
    {
      clearRegisterState ();
      return;
    }

  clearRegisterState ();
  emit2 ("movw", "ax,sp");
  emit2 ("movw", "hl,ax");
  hl_is_sp = true;
  hl_sp_offset_valid = true;
  hl_sp_offset = 0;
}

static void
ensureHLToSP (void)
{
  setHLToSP ();
}

static void
ensureHLToSPPreservingA (const char *scratch)
{
  if (hl_is_sp)
    {
      clearRegisterState ();
      return;
    }

  emit2 ("mov", "%s,a", scratch);
  setHLToSP ();
  emit2 ("mov", "a,%s", scratch);
}

static void
ensureHLToSPPreservingAX (void)
{
  emit2 ("movw", "de,ax");

  if (hl_is_sp)
    {
      clearRegisterState ();
      return;
    }

  setHLToSP ();
  emit2 ("movw", "ax,de");
}

static void
adjustAX (const int amount)
{
  if (amount == 1)
    emit2 ("incw", "ax");
  else if (amount == -1)
    emit2 ("decw", "ax");
  else if (amount > 0)
    emit2 ("addw", "ax,#0x%04x", (unsigned)amount);
  else if (amount < 0)
    emit2 ("subw", "ax,#0x%04x", (unsigned)(-amount));
}

static int
stackByteOffset (const symbol *sym, const int offset)
{
  int base = sym->stack;

  if (sym->stackSpil && sym->stack > 0)
    base -= local_stack_size + K78K0_RETURN_ADDRESS_BYTES + getSize (sym->type) - 1;
  else if (sym->stackSpil && sym->stack < 0)
    base += local_stack_size;
  else if ((sym->_isparm || sym->ismyparm) && !IS_REGPARM (sym->etype))
    base += local_stack_size + current_param_offset;
  else if (sym->stack > 0)
    base -= local_stack_size + K78K0_RETURN_ADDRESS_BYTES + getSize (sym->type) - 1;
  else if (sym->stack < 0)
    base += local_stack_size;

  return base + stack_pushed + offset;
}

static void
setHLToStackOffset (const int stack_offset)
{
  clearRegisterState ();
  clearHLState ();

  emit2 ("movw", "ax,sp");
  adjustAX (stack_offset);
  emit2 ("movw", "hl,ax");
  hl_is_sp = stack_offset == 0;
  hl_sp_offset_valid = true;
  hl_sp_offset = stack_offset;
}

static bool
useHLForStackOffset (const int stack_offset, unsigned *index)
{
  int delta;

  if (!hl_sp_offset_valid || stack_offset < hl_sp_offset)
    return false;

  delta = stack_offset - hl_sp_offset;
  if (delta > 255)
    return false;

  clearRegisterState ();
  *index = (unsigned)delta;
  return true;
}

static int
stackWindowBase (const int stack_offset)
{
  return stack_offset > 255 ? stack_offset & ~0xff : 0;
}

static int
k78k0_operandSize (const operand *op)
{
  return op && IS_ITEMP (op) && op->isaddr ? 2 : getSize (operandType (op));
}

static unsigned long long
operandLitValueBits (const operand *op)
{
  if (IS_FLOAT (operandType (op)))
    {
      union
      {
        float value;
        unsigned char bytes[sizeof (float)];
      }
      representation;
      unsigned long long bits = 0;

      representation.value = (float)floatFromVal (OP_VALUE_CONST (op));
      for (int offset = 0; offset < (int)sizeof (representation.bytes); offset++)
#ifdef WORDS_BIGENDIAN
        bits |= (unsigned long long)representation.bytes[sizeof (representation.bytes) - offset - 1] << (offset * 8);
#else
        bits |= (unsigned long long)representation.bytes[offset] << (offset * 8);
#endif
      return bits;
    }

  return operandLitValueUll (op);
}

static bool
typeReturnsViaHiddenPointer (sym_link *type)
{
  sym_link *ftype;

  if (!type)
    return false;

  ftype = IS_FUNCPTR (type) ? type->next : type;
  return ftype && ftype->next && (IS_STRUCT (ftype->next) || getSize (ftype->next) > 4);
}

static void
adjustHardwareStackPointer (const int amount, const bool leave_hl_sp)
{
  bool ax_is_sp = false;

  clearRegisterState ();

  if (amount == -1)
    emit2 ("push", "psw");
  else if (amount == -2)
    emit2 ("push", "ax");
  else if (amount == 2)
    emit2 ("pop", "ax");
  else
    {
      emit2 ("movw", "ax,sp");
      adjustAX (amount);
      emit2 ("movw", "sp,ax");
      ax_is_sp = true;
    }

  if (leave_hl_sp)
    {
      if (!ax_is_sp)
        emit2 ("movw", "ax,sp");
      emit2 ("movw", "hl,ax");
      hl_is_sp = true;
      hl_sp_offset_valid = true;
      hl_sp_offset = 0;
    }
}

static void
adjustStackPointer (const int amount, const bool leave_hl_sp)
{
  if (!amount)
    return;

  adjustHardwareStackPointer (amount, leave_hl_sp);

  stack_pushed -= amount;
  wassertl (stack_pushed >= 0, "78K0 outgoing stack accounting underflow.");
}

static void
adjustFramePointer (const int amount, const bool leave_hl_sp)
{
  if (!amount)
    return;

  adjustHardwareStackPointer (amount, leave_hl_sp);
}

static void
setAResult (const operand *op)
{
  if (IS_ITEMP (op))
    {
      const symbol *sym = OP_SYMBOL_CONST (op);
      const symbol *storage = operandStorageSymbol (op);

      if (storage != sym)
        {
          if (storeAccumulatorToStack (storage, 1))
            clearAResult ();
          return;
        }

      a_result_sym = sym;
      return_result_sym = a_result_sym;
      return_result_size = 1;
      return_result_ax_valid = true;
    }
  else
    clearAResult ();
}

static void
setReturnResult (const operand *op, const int size)
{
  if (IS_ITEMP (op) && size >= 1 && size <= 2)
    {
      const symbol *sym = OP_SYMBOL_CONST (op);
      const symbol *storage = operandStorageSymbol (op);

      if (storage != sym)
        {
          if (storeAccumulatorToStack (storage, size))
            clearAResult ();
          return;
        }

      return_result_sym = sym;
      return_result_size = size;
      return_result_ax_valid = true;
      a_result_sym = size == 1 ? return_result_sym : NULL;
    }
  else
    clearAResult ();
}

static bool
operandInA (const operand *op, const int offset)
{
  return offset == 0 && IS_ITEMP (op) && OP_SYMBOL_CONST (op) == a_result_sym;
}

static bool
operandInReturnValue (const operand *op, const int size)
{
  const operand *resolved;

  if (!IS_ITEMP (op) || size != return_result_size)
    return false;

  if (OP_SYMBOL_CONST (op) == return_result_sym)
    return true;

  resolved = resolveReqvOperand (op);
  return resolved != op && IS_ITEMP (resolved) && OP_SYMBOL_CONST (resolved) == return_result_sym;
}

static bool
operandInReturnValueWithAX (const operand *op, const int size)
{
  return operandInReturnValue (op, size) && return_result_ax_valid;
}

static void
saveScalarAcrossStackAdjustment (const int size)
{
  if (size == 1)
    emit2 ("mov", "c,a");
  else if (size == 2)
    emit2 ("movw", "bc,ax");
  else if (size <= 4)
    emit2 ("movw", "de,ax");
}

static void
restoreScalarAcrossStackAdjustment (const int size)
{
  if (size == 1)
    emit2 ("mov", "a,c");
  else if (size == 2)
    emit2 ("movw", "ax,bc");
  else if (size <= 4)
    emit2 ("movw", "ax,de");
}

static void
saveCurrentReturnAcrossStackAdjustment (void)
{
  if (!current_return_via_hidden_pointer)
    saveScalarAcrossStackAdjustment (current_return_size);
}

static void
restoreCurrentReturnAcrossStackAdjustment (void)
{
  if (!current_return_via_hidden_pointer)
    restoreScalarAcrossStackAdjustment (current_return_size);
}

static void
resetFunctionState (void)
{
  stack_pushed = 0;
  local_stack_size = 0;
  current_return_size = 0;
  current_return_via_hidden_pointer = false;
  current_param_offset = 0;
  current_stack_cleanup_size = 0;
  current_function_is_isr = false;
  clearRegisterState ();
}

static int
functionStackCleanupBytes (sym_link *ftype)
{
  int bytes = 0;

  if (!ftype || !IS_FUNC (ftype) || FUNC_HASVARARGS (ftype))
    return 0;

  if (typeReturnsViaHiddenPointer (ftype))
    bytes += 2;

  for (value *arg = FUNC_ARGS (ftype); arg; arg = arg->next)
    if (!SPEC_REGPARM (arg->etype))
      bytes += getSize (arg->type);

  return bytes;
}

static int
functionFirstRegArgSize (sym_link *ftype)
{
  value *arg;

  if (!ftype || !IS_FUNC (ftype))
    return 0;

  arg = FUNC_ARGS (ftype);
  if (!arg || !SPEC_REGPARM (arg->etype))
    return 0;

  return getSize (arg->type);
}

static void
moveReturnAddressForCalleeCleanup (const int cleanup_bytes)
{
  clearRegisterState ();
  emit2 ("pop", "hl");
  emit2 ("movw", "ax,sp");
  adjustAX (cleanup_bytes);
  emit2 ("movw", "sp,ax");
  emit2 ("push", "hl");
}

static void
emitWideRegisterReturnEpilogue (const int frame_bytes, const int cleanup_bytes)
{
  emit2 ("movw", "de,ax");

  emit2 ("movw", "ax,sp");
  adjustAX (frame_bytes);
  emit2 ("movw", "hl,ax");
  emit2 ("mov", "a,[hl+0x00]");
  emit2 ("mov", "x,a");
  emit2 ("mov", "a,[hl+0x01]");
  emit2 ("movw", "hl,ax");

  emit2 ("movw", "ax,sp");
  adjustAX (frame_bytes + cleanup_bytes + K78K0_RETURN_ADDRESS_BYTES);
  emit2 ("movw", "sp,ax");
  emit2 ("push", "hl");

  emit2 ("movw", "ax,sp");
  adjustAX (-(frame_bytes + cleanup_bytes));
  emit2 ("movw", "hl,ax");
  emit2 ("mov", "a,[hl+0x00]");
  emit2 ("mov", "x,a");
  emit2 ("mov", "a,[hl+0x01]");
  emit2 ("xchw", "ax,de");
}

void
k78k0_emitDebuggerSymbol (const char *debugSym)
{
  (void)debugSym;
}

static void
genFunction (const iCode *ic)
{
  const symbol *sym = OP_SYMBOL (IC_LEFT (ic));
  const int frame_local_size = sym->stack > 0 ? sym->stack : 0;
  const int first_regarg_size = functionFirstRegArgSize (sym->type);

  resetFunctionState ();
  local_stack_size = frame_local_size + K78K0_PRESERVED_DE_BYTES;
  current_return_size = sym->type && sym->type->next && !IS_VOID (sym->type->next) ?
    getSize (sym->type->next) : 0;
  current_return_via_hidden_pointer = typeReturnsViaHiddenPointer (sym->type);
  current_param_offset = current_return_via_hidden_pointer ? 2 : 0;
  current_stack_cleanup_size = functionStackCleanupBytes (sym->type);
  current_function_is_isr = IFFUNC_ISISR (sym->type);
  emit2 ("", "%s:", sym->rname);
  genLine.lineCurr->isLabel = 1;

  if (IFFUNC_ISNAKED (sym->type))
    {
      emit2 (";", "naked function: no prologue.");
      return;
    }

  if (current_function_is_isr)
    {
      emit2 ("push", "psw");
      emit2 ("push", "ax");
      emit2 ("push", "bc");
      emit2 ("push", "de");
      emit2 ("push", "hl");
    }

  if (first_regarg_size > 2)
    emit2 ("movw", "hl,ax");
  else
    saveScalarAcrossStackAdjustment (first_regarg_size);

  adjustFramePointer (-frame_local_size, false);

  emit2 ("push", "de");
  if (first_regarg_size && first_regarg_size <= 2)
    setHLToSP ();
  else
    clearHLState ();

  if (first_regarg_size > 2)
    emit2 ("movw", "ax,hl");
  else
    restoreScalarAcrossStackAdjustment (first_regarg_size);

  if (IFFUNC_ISCRITICAL (sym->type))
    {
      if (first_regarg_size > 2)
        emit2 ("movw", "de,ax");
      else
        saveScalarAcrossStackAdjustment (first_regarg_size);

      genCritical ();
      setHLToSP ();

      if (first_regarg_size > 2)
        emit2 ("movw", "ax,de");
      else
        restoreScalarAcrossStackAdjustment (first_regarg_size);
    }
}

static void
genEndFunction (const iCode *ic)
{
  const symbol *sym = OP_SYMBOL (IC_LEFT (ic));
  const bool is_isr = current_function_is_isr;
  int frame_local_size;

  if (IFFUNC_ISNAKED (sym->type))
    {
      resetFunctionState ();
      emit2 (";", "naked function: no epilogue.");
      return;
    }

  if (IFFUNC_ISCRITICAL (sym->type))
    genEndCritical ();

  frame_local_size = local_stack_size - K78K0_PRESERVED_DE_BYTES;
  wassertl (frame_local_size >= 0, "78K0 invalid local frame size.");

  if (!is_isr && !current_return_via_hidden_pointer && current_return_size > 2 && current_return_size <= 4)
    {
      emitWideRegisterReturnEpilogue (local_stack_size, current_stack_cleanup_size);
      wassertl (stack_pushed == 0, "78K0 unbalanced outgoing stack.");
      resetFunctionState ();
      emit2 ("ret", "");
      return;
    }

  if (frame_local_size)
    saveCurrentReturnAcrossStackAdjustment ();

  emit2 ("pop", "de");
  clearHLState ();

  if (frame_local_size)
    {
      adjustFramePointer (frame_local_size, false);
      restoreCurrentReturnAcrossStackAdjustment ();
    }

  if (current_stack_cleanup_size)
    {
      saveCurrentReturnAcrossStackAdjustment ();

      moveReturnAddressForCalleeCleanup (current_stack_cleanup_size);
      restoreCurrentReturnAcrossStackAdjustment ();
    }

  wassertl (stack_pushed == 0, "78K0 unbalanced outgoing stack.");
  resetFunctionState ();
  if (is_isr)
    {
      emit2 ("pop", "hl");
      emit2 ("pop", "de");
      emit2 ("pop", "bc");
      emit2 ("pop", "ax");
      emit2 ("pop", "psw");
      emit2 ("reti", "");
    }
  else
    {
      emit2 ("ret", "");
    }
}

static void
genLabel (const iCode *ic)
{
  char label[32];

  clearRegisterState ();
  clearHLState ();
  makeICLabel (label, sizeof (label), IC_LABEL (ic));
  emit2 ("", "%s:", label);
  genLine.lineCurr->isLabel = 1;
}

static void
genGoto (const iCode *ic)
{
  char label[32];

  makeICLabel (label, sizeof (label), IC_LABEL (ic));
  emit2 ("br", "!%s", label);
  clearRegisterState ();
  clearHLState ();
}

static void
genInlineAsm (iCode *ic)
{
  genInline (ic);
  clearRegisterState ();
}

static void
genCritical (void)
{
  clearRegisterState ();
  emit2 ("push", "psw");
  stack_pushed += 1;
  emit2 ("di", "");
}

static void
genEndCritical (void)
{
  clearRegisterState ();
  emit2 ("pop", "psw");
  stack_pushed -= 1;
  wassertl (stack_pushed >= 0, "78K0 critical stack accounting underflow.");
}

static void
genLiteralReturnValue (const operand *op)
{
  const unsigned long long value = operandLitValueBits (op);
  const int size = getSize (operandType (op));

  if (size <= 1)
    {
      clearAResult ();
      emit2 ("mov", "a,#0x%02x", (unsigned)(value & 0xffu));
    }
  else if (size <= 2)
    {
      clearAResult ();
      emit2 ("movw", "ax,#0x%04x", (unsigned)(value & 0xffffu));
    }
  else if (size <= 4)
    {
      clearAResult ();
      emit2 ("mov", "c,#0x%02x", (unsigned)((value >> 16) & 0xffu));
      if (size == 4)
        emit2 ("mov", "b,#0x%02x", (unsigned)((value >> 24) & 0xffu));
      emit2 ("movw", "ax,#0x%04x", (unsigned)(value & 0xffffu));
    }
  else
    wassertl (0, "78K0 literal return is wider than the supported scalar return size.");
}

static bool
loadStackByteToA (const symbol *sym, const int offset)
{
  const int stack_offset = stackByteOffset (sym, offset);
  unsigned index;

  if (!sym->onStack || stack_offset < 0)
    return false;

  if (!useHLForStackOffset (stack_offset, &index))
    {
      const int base = stackWindowBase (stack_offset);

      setHLToStackOffset (base);
      index = (unsigned)(stack_offset - base);
    }
  emit2 ("mov", "a,[hl+0x%02x]", index);

  return true;
}

static bool
loadStackToReturnValue (const symbol *sym, const int size)
{
  const int offset = stackByteOffset (sym, 0);

  if (!sym->onStack)
    return false;

  if (size <= 1)
    {
      if (!loadStackByteToA (sym, 0))
        return false;
    }
  else if (size <= 2)
    {
      if (offset >= 0 && offset + size <= 256)
        {
          ensureHLToSP ();
          emit2 ("mov", "a,[hl+0x%02x]", (unsigned)offset);
          emit2 ("mov", "x,a");
          emit2 ("mov", "a,[hl+0x%02x]", (unsigned)(offset + 1));
        }
      else
        {
          setHLToStackOffset (offset);
          emit2 ("mov", "a,[hl+0x00]");
          emit2 ("mov", "x,a");
          emit2 ("mov", "a,[hl+0x01]");
        }
    }
  else if (size <= 4)
    {
      setHLToStackOffset (offset);
      emit2 ("mov", "a,[hl+0x02]");
      emit2 ("mov", "c,a");
      if (size == 4)
        {
          emit2 ("mov", "a,[hl+0x03]");
          emit2 ("mov", "b,a");
        }
      emit2 ("mov", "a,[hl+0x00]");
      emit2 ("mov", "x,a");
      emit2 ("mov", "a,[hl+0x01]");
    }
  else
    return false;

  return true;
}

static bool
loadDirectToReturnValue (const symbol *sym, const int size)
{
  if (!sym->rname[0] || sym->onStack)
    return false;

  if (size <= 1)
    {
      clearAResult ();
      emit2 ("mov", "a,!%s", sym->rname);
    }
  else if (size <= 2)
    {
      clearAResult ();
      emit2 ("mov", "a,!%s", sym->rname);
      emit2 ("mov", "x,a");
      emit2 ("mov", "a,!%s + 1", sym->rname);
    }
  else if (size <= 4)
    {
      emit2 ("mov", "a,!%s + 2", sym->rname);
      emit2 ("mov", "c,a");
      if (size == 4)
        {
          emit2 ("mov", "a,!%s + 3", sym->rname);
          emit2 ("mov", "b,a");
        }
      emit2 ("mov", "a,!%s", sym->rname);
      emit2 ("mov", "x,a");
      emit2 ("mov", "a,!%s + 1", sym->rname);
    }
  else
    return false;

  return true;
}

static bool
loadOperandLowWordToAX (const operand *op)
{
  op = resolveReqvOperand (op);

  if (IS_OP_LITERAL (op))
    {
      clearAResult ();
      emit2 ("movw", "ax,#0x%04x", (unsigned)(operandLitValueBits (op) & 0xffffu));
      return true;
    }

  if (IS_SYMOP (op))
    {
      const symbol *sym = operandStorageSymbol (op);

      return sym->onStack ? loadStackToReturnValue (sym, 2) : loadDirectToReturnValue (sym, 2);
    }

  return false;
}

static bool
loadOperandByteToA (const operand *op, const int offset)
{
  sym_link *type;
  int size;

  op = resolveReqvOperand (op);
  type = operandType (op);
  size = k78k0_operandSize (op);

  if (operandInA (op, offset))
    return true;

  if (IS_ITEMP (op) && offset < size && operandInReturnValue (op, size))
    {
      if (size == 2 && return_result_ax_valid)
        {
          if (offset == 0)
            emit2 ("mov", "a,x");
          clearAResult ();
          return true;
        }
      return false;
    }

  if (IS_OP_LITERAL (op))
    {
      const unsigned long long value = operandLitValueBits (op);
      clearAResult ();
      emit2 ("mov", "a,#0x%02x", (unsigned)((value >> (offset * 8)) & 0xffu));
      return true;
    }

  if (offset >= size)
    {
      if (!SPEC_USIGN (getSpec (type)))
        return false;

      clearAResult ();
      emit2 ("mov", "a,#0x00");
      return true;
    }

  if (IS_SYMOP (op))
    {
      const symbol *sym = operandStorageSymbol (op);

      if (sym == OP_SYMBOL_CONST (op) && IS_ITEMP (op) && sym->remat && size == 2)
        {
          if (offset > 1 || !loadRematerializedAddressToAX (op))
            return false;

          if (offset == 0)
            emit2 ("mov", "a,x");
          return true;
        }

      if (sym == OP_SYMBOL_CONST (op) && IS_FUNC (operandType (op)) && size == 2)
        {
          if (offset > 1 || !loadFunctionAddressToAX (op))
            return false;

          if (offset == 0)
            emit2 ("mov", "a,x");
          return true;
        }

      if (sym->onStack)
        return loadStackByteToA (sym, offset);

      if (!sym->onStack && sym->rname[0])
        {
          clearAResult ();
          if (offset == 0)
            emit2 ("mov", symbolIsSfr (sym) ? "a,%s" : "a,!%s", sym->rname);
          else
            emit2 ("mov", symbolIsSfr (sym) ? "a,%s + %d" : "a,!%s + %d", sym->rname, offset);
          return true;
        }
    }

  return false;
}

static bool
operandByteOnStack (const operand *op, const int offset)
{
  const symbol *sym;
  int stack_offset;

  op = resolveReqvOperand (op);
  if (!IS_SYMOP (op))
    return false;

  sym = operandStorageSymbol (op);
  stack_offset = stackByteOffset (sym, offset);
  return sym->onStack && stack_offset >= 0;
}

static bool
operandNeedsStackHL (const operand *op, const int size)
{
  for (int offset = 0; offset < size; offset++)
    if (operandByteOnStack (op, offset))
      return true;

  return false;
}

static sym_link *
operandByteSourceType (const operand *op)
{
  op = resolveReqvOperand (op);
  return operandType (op);
}

static bool
isUnsignedByteSource (const operand *op)
{
  sym_link *type = operandByteSourceType (op);

  return getSize (type) == 1 && SPEC_USIGN (getSpec (type));
}

static bool
isUnsignedByteDivisor (const operand *op)
{
  if (isUnsignedByteSource (op))
    return true;

  return IS_OP_LITERAL (op) && operandLitValueUll (op) <= 0xffu;
}

static bool
aluOperandByteToA (const char *mnemonic, const operand *op, const int offset)
{
  sym_link *type;

  op = resolveReqvOperand (op);
  type = operandType (op);

  if (IS_OP_LITERAL (op))
    {
      const unsigned long long value = operandLitValueUll (op);
      emit2 (mnemonic, "a,#0x%02x", (unsigned)((value >> (offset * 8)) & 0xffu));
      return true;
    }

  if (offset >= getSize (type))
    {
      if (!SPEC_USIGN (getSpec (type)))
        return false;

      emit2 (mnemonic, "a,#0x00");
      return true;
    }

  if (IS_SYMOP (op))
    {
      const symbol *sym = operandStorageSymbol (op);

      if (sym == OP_SYMBOL_CONST (op) && IS_ITEMP (op) && sym->remat && getSize (type) == 2)
        {
          if (offset > 1)
            return false;

          emit2 ("mov", "c,a");
          if (!loadRematerializedAddressToAX (op))
            return false;
          if (offset == 0)
            {
              emit2 ("mov", "a,x");
              emit2 ("mov", "b,a");
            }
          else
            emit2 ("mov", "b,a");
          emit2 ("mov", "a,c");
          emit2 (mnemonic, "a,b");
          return true;
        }

      if (sym == OP_SYMBOL_CONST (op) && IS_FUNC (operandType (op)) && getSize (type) == 2)
        {
          if (offset > 1)
            return false;

          emit2 ("mov", "c,a");
          if (!loadFunctionAddressToAX (op))
            return false;
          if (offset == 0)
            emit2 ("mov", "a,x");
          emit2 ("mov", "b,a");
          emit2 ("mov", "a,c");
          emit2 (mnemonic, "a,b");
          return true;
        }

      if (sym->onStack)
        {
          const int stack_offset = stackByteOffset (sym, offset);
          unsigned index;

          if (stack_offset < 0)
            return false;

          if (!useHLForStackOffset (stack_offset, &index))
            {
              const int base = stackWindowBase (stack_offset);

              emit2 ("mov", "c,a");
              setHLToStackOffset (base);
              emit2 ("mov", "a,c");
              index = (unsigned)(stack_offset - base);
            }
          emit2 (mnemonic, "a,[hl+0x%02x]", index);
          return true;
        }

      if (!sym->onStack && sym->rname[0])
        {
          if (offset == 0)
            emit2 (mnemonic, symbolIsSfr (sym) ? "a,%s" : "a,!%s", sym->rname);
          else
            emit2 (mnemonic, symbolIsSfr (sym) ? "a,%s + %d" : "a,!%s + %d", sym->rname, offset);
          return true;
        }
    }

  return false;
}

static bool
cmpOperandByteWithA (const operand *op, const int offset)
{
  return aluOperandByteToA ("cmp", op, offset);
}

static void
storeAToDirectByte (const symbol *sym, const int offset)
{
  if (offset == 0)
    emit2 ("mov", symbolIsSfr (sym) ? "%s,a" : "!%s,a", sym->rname);
  else
    emit2 ("mov", symbolIsSfr (sym) ? "%s + %d,a" : "!%s + %d,a", sym->rname, offset);
}

static bool
storeAToStackByte (const symbol *sym, const int offset)
{
  const int stack_offset = stackByteOffset (sym, offset);
  unsigned index;

  if (!sym->onStack || stack_offset < 0)
    return false;

  if (!useHLForStackOffset (stack_offset, &index))
    {
      const int base = stackWindowBase (stack_offset);

      emit2 ("mov", "c,a");
      setHLToStackOffset (base);
      emit2 ("mov", "a,c");
      index = (unsigned)(stack_offset - base);
    }
  emit2 ("mov", "[hl+0x%02x],a", index);

  return true;
}

static bool
storeAToOperandByte (const operand *op, const int offset)
{
  const symbol *sym;

  if (!IS_SYMOP (op))
    return false;

  sym = operandStorageSymbol (op);
  if (sym->onStack)
    return storeAToStackByte (sym, offset);

  if (!sym->rname[0])
    return false;

  storeAToDirectByte (sym, offset);
  return true;
}

static bool
storeSavedAXToStack (const symbol *sym)
{
  emit2 ("mov", "a,d");
  if (!storeAToStackByte (sym, 1))
    return false;

  emit2 ("mov", "a,e");
  return storeAToStackByte (sym, 0);
}

static void
storeSavedAXToDirect (const symbol *sym)
{
  emit2 ("mov", "a,d");
  storeAToDirectByte (sym, 1);
  emit2 ("mov", "a,e");
  storeAToDirectByte (sym, 0);
}

static bool
storeAccumulatorToStack (const symbol *sym, const int size)
{
  if (!sym->onStack || size < 1 || size > 2)
    return false;

  if (size == 1)
    {
      ensureHLToSPPreservingA ("c");
      return storeAToStackByte (sym, 0);
    }

  ensureHLToSPPreservingAX ();
  if (!storeSavedAXToStack (sym))
    return false;
  emit2 ("movw", "ax,de");
  return true;
}

static bool
storeReturnRegistersToStack (const symbol *sym, const int size)
{
  int stack_offset;

  if (!sym->onStack || size < 1 || size > 4)
    return false;

  if (size <= 2)
    return storeAccumulatorToStack (sym, size);

  stack_offset = stackByteOffset (sym, 0);
  emit2 ("movw", "de,ax");
  setHLToStackOffset (stack_offset);
  emit2 ("mov", "a,e");
  emit2 ("mov", "[hl+0x00],a");
  emit2 ("mov", "a,d");
  emit2 ("mov", "[hl+0x01],a");
  emit2 ("mov", "a,c");
  emit2 ("mov", "[hl+0x02],a");
  if (size == 4)
    {
      emit2 ("mov", "a,b");
      emit2 ("mov", "[hl+0x03],a");
    }
  emit2 ("movw", "ax,de");
  return true;
}

static bool
storeReturnRegistersToDirect (const symbol *sym, const int size)
{
  if (!sym->rname[0] || sym->onStack || size < 1 || size > 4)
    return false;

  if (size == 1)
    {
      storeAToDirectByte (sym, 0);
      return true;
    }

  emit2 ("movw", "de,ax");
  if (size >= 3)
    {
      emit2 ("mov", "a,c");
      storeAToDirectByte (sym, 2);
      if (size == 4)
        {
          emit2 ("mov", "a,b");
          storeAToDirectByte (sym, 3);
        }
    }
  storeSavedAXToDirect (sym);
  emit2 ("movw", "ax,de");
  return true;
}

static bool
storeReturnValueToDirect (const operand *right, const symbol *sym, const int size)
{
  right = resolveReqvOperand (right);

  if (!operandInReturnValueWithAX (right, size))
    return false;

  if (size <= 1)
    {
      storeAToDirectByte (sym, 0);
      return true;
    }

  if (size == 2)
    {
      storeAToDirectByte (sym, 1);
      emit2 ("mov", "a,x");
      storeAToDirectByte (sym, 0);
      clearAResult ();
      return true;
    }

  return false;
}

static bool
storeReturnValueToStack (const operand *right, const symbol *sym, const int size)
{
  right = resolveReqvOperand (right);

  if (!operandInReturnValueWithAX (right, size))
    return false;

  if (size <= 1)
    return storeAToStackByte (sym, 0);

  if (size == 2)
    {
      emit2 ("movw", "de,ax");
      if (!storeSavedAXToStack (sym))
        return false;
      emit2 ("movw", "ax,de");
      clearAResult ();
      return true;
    }

  return false;
}

static bool
storeOperandToSymbol (const operand *right, const symbol *sym, const int size)
{
  if (!sym || size > K78K0_MAX_SCALAR_BYTES || (!sym->onStack && !sym->rname[0]))
    return false;

  if (sym->onStack)
    {
      if (storeReturnValueToStack (right, sym, size))
        return true;
    }
  else if (storeReturnValueToDirect (right, sym, size))
    return true;

  for (int offset = 0; offset < size; offset++)
    {
      if (!loadOperandByteToA (right, offset))
        return false;

      if (sym->onStack)
        {
          if (!storeAToStackByte (sym, offset))
            return false;
        }
      else
        storeAToDirectByte (sym, offset);
    }

  return true;
}

static operand *
wideAssignmentTarget (const iCode *ic, const operand *result, const int size)
{
  iCode *next = ic->next;
  const symbol *storage;
  operand *target;
  operand *right;

  if (!IS_ITEMP (result))
    return NULL;

  if (next && next->op == '=' && !POINTER_SET (next))
    {
      target = IC_RESULT (next);
      right = IC_RIGHT (next);
      if (target && right && IS_SYMOP (target) && IS_ITEMP (right) &&
          OP_SYMBOL_CONST (right) == OP_SYMBOL_CONST (result) &&
          OP_SYMBOL_CONST (result)->liveTo <= next->seq &&
          getSize (operandType (target)) == size)
        return target;
    }

  storage = operandStorageSymbol (result);
  return storage != OP_SYMBOL_CONST (result) && (storage->onStack || storage->rname[0]) ? (operand *)result : NULL;
}

static bool
finishWideAssignment (const iCode *ic, const operand *result, const operand *target, const int size)
{
  (void)size;
  if (target && target != result)
    ic->next->generated = true;
  clearAResult ();
  return true;
}

static bool
genAssign (const iCode *ic)
{
  operand *result = IC_RESULT (ic);
  operand *right = IC_RIGHT (ic);
  const symbol *rsym;
  int size;

  if (!IS_SYMOP (result) || !right)
    return false;

  if (IS_ITEMP (result))
    {
      symbol *sym = OP_SYMBOL (result);
      const symbol *storage = operandStorageSymbol (result);

      if (storage == sym)
        {
          sym->reqv = right;
          return true;
        }

      rsym = storage;
    }
  else
    rsym = OP_SYMBOL_CONST (result);

  size = k78k0_operandSize (result);
  if (!storeOperandToSymbol (right, rsym, size))
    return false;

  return true;
}

static bool
testOperandForZero (const operand *op)
{
  const int size = k78k0_operandSize (op);
  const bool is_float = IS_FLOAT (getSpec (operandType (op)));

  if (size < 1 || size > K78K0_MAX_SCALAR_BYTES)
    return false;

  if (size == 2 && operandInReturnValueWithAX (op, size))
    {
      emit2 ("cmpw", "ax,#0x0000");
      return true;
    }

  if (operandNeedsStackHL (op, size))
    setHLToSP ();

  if (!loadOperandByteToA (op, 0))
    return false;

  if (size == 1)
    {
      emit2 ("cmp", "a,#0x00");
      return true;
    }

  emit2 ("mov", "b,a");
  for (int offset = 1; offset < size; offset++)
    {
      if (!loadOperandByteToA (op, offset))
        return false;
      if (is_float && offset == size - 1)
        emit2 ("and", "a,#0x7f");
      emit2 ("or", "a,b");
      if (offset + 1 < size)
        emit2 ("mov", "b,a");
    }

  return true;
}

static void
materializeZeroTest (const operand *result, const bool invert)
{
  char done_label[32];
  const int size = k78k0_operandSize (result);

  makeLocalLabel (done_label, sizeof (done_label));
  emit2 ("mov", "a,#0x00");
  emit2 (invert ? "bnz" : "bz", "%s", done_label);
  emit2 ("inc", "a");
  emitLocalLabel (done_label);

  if (size <= 1)
    setAResult (result);
  else
    {
      emit2 ("mov", "x,a");
      emit2 ("mov", "a,#0x00");
      setReturnResult (result, size);
    }
}

static bool
genBooleanCast (const operand *result, const operand *right)
{
  if (!testOperandForZero (right))
    return false;

  materializeZeroTest (result, false);
  return true;
}

static unsigned
bitIntTopByteMask (const operand *op)
{
  sym_link *type = getSpec (operandType (op));
  const unsigned remainder = IS_BITINT (type) ? SPEC_BITINTWIDTH (type) % 8 : 0;

  return remainder ? 0xffu >> (8 - remainder) : 0xffu;
}

static unsigned
unsignedBitIntTopByteMask (const operand *op)
{
  sym_link *type = getSpec (operandType (op));

  return SPEC_USIGN (type) ? bitIntTopByteMask (op) : 0xffu;
}

static void
normalizeBitIntTopByteInA (const operand *op)
{
  sym_link *type = getSpec (operandType (op));
  const unsigned mask = bitIntTopByteMask (op);

  if (mask == 0xffu)
    return;

  emit2 ("and", "a,#0x%02x", mask);
  if (!SPEC_USIGN (type))
    {
      char positive_label[32];

      makeLocalLabel (positive_label, sizeof (positive_label));
      emit2 ("cmp", "a,#0x%02x", (mask + 1u) >> 1);
      emit2 ("bc", "%s", positive_label);
      emit2 ("or", "a,#0x%02x", (~mask) & 0xffu);
      emitLocalLabel (positive_label);
    }
}

static bool
genCast (const iCode *ic)
{
  operand *result = IC_RESULT (ic);
  operand *right = IC_RIGHT (ic);
  operand *target;
  bool normalize_or_truncate;
  unsigned top_byte_mask;
  int result_size;
  int right_size;

  if (!IS_ITEMP (result) || !right)
    return false;

  result_size = k78k0_operandSize (result);
  right_size = k78k0_operandSize (right);
  top_byte_mask = bitIntTopByteMask (result);

  if (IS_BOOLEAN (operandType (result)) && !IS_BOOLEAN (operandType (right)))
    return genBooleanCast (result, right);

  normalize_or_truncate =
    result_size >= 1 && result_size <= K78K0_MAX_SCALAR_BYTES &&
    ((result_size < right_size && right_size <= K78K0_MAX_SCALAR_BYTES) ||
     (result_size == right_size && top_byte_mask != 0xffu));

  if (normalize_or_truncate)
    {
      if (result_size == 1)
        {
          if (!loadOperandByteToA (right, 0))
            return false;
          if (top_byte_mask != 0xffu)
            normalizeBitIntTopByteInA (result);
          setAResult (result);
          return true;
        }

      if (result_size == 2)
        {
          if (!loadOperandLowWordToAX (right))
            return false;
          if (top_byte_mask != 0xffu)
            normalizeBitIntTopByteInA (result);
          setReturnResult (result, result_size);
          return true;
        }

      target = wideAssignmentTarget (ic, result, result_size);
      if (!target)
        return true;
      for (int offset = 0; offset < result_size; offset++)
        {
          if (!loadOperandByteToA (right, offset))
            return false;
          if (offset == result_size - 1 && top_byte_mask != 0xffu)
            normalizeBitIntTopByteInA (result);
          if (!storeAToOperandByte (target, offset))
            return false;
        }

      return finishWideAssignment (ic, result, target, result_size);
    }

  if (result_size == 2 && right_size == 1)
    {
      if (!loadOperandByteToA (right, 0))
        return false;

      emit2 ("mov", "x,a");
      if (SPEC_USIGN (getSpec (operandType (right))))
        emit2 ("mov", "a,#0x00");
      else
        emitSignMaskForA ();

      if (top_byte_mask != 0xffu)
        normalizeBitIntTopByteInA (result);

      setReturnResult (result, result_size);
      return true;
    }

  if (result_size > 2 && result_size <= K78K0_MAX_SCALAR_BYTES &&
      right_size >= 1 && right_size < result_size && right_size <= K78K0_MAX_SCALAR_BYTES)
    {
      int offset;

      target = wideAssignmentTarget (ic, result, result_size);
      if (!target)
        return true;
      for (offset = 0; offset < right_size; offset++)
        {
          if (!loadOperandByteToA (right, offset))
            return false;
          if (!storeAToOperandByte (target, offset))
            return false;
        }

      if (SPEC_USIGN (getSpec (operandType (right))))
        emit2 ("mov", "a,#0x00");
      else
        {
          if (!loadOperandByteToA (right, right_size - 1))
            return false;
          emitSignMaskForA ();
        }

      for (; offset < result_size; offset++)
        {
          if (offset == result_size - 1 && top_byte_mask != 0xffu)
            normalizeBitIntTopByteInA (result);
          if (!storeAToOperandByte (target, offset))
            return false;
        }

      return finishWideAssignment (ic, result, target, result_size);
    }

  if (IS_ITEMP (result))
    {
      symbol *sym = OP_SYMBOL (result);
      const symbol *storage = operandStorageSymbol (result);

      if (storage != sym)
        {
          if (result_size < 1 || result_size > K78K0_MAX_SCALAR_BYTES || right_size != result_size)
            return false;

          return storeOperandToSymbol (right, storage, result_size);
        }
    }

  OP_SYMBOL (result)->reqv = right;
  return true;
}

static bool
genBinaryAccumulatorOp (const iCode *ic, const char *low_mnemonic, const char *high_mnemonic)
{
  operand *result = IC_RESULT (ic);
  operand *left = IC_LEFT (ic);
  operand *right = IC_RIGHT (ic);
  operand *target;
  const bool carry_add = !strcmp (low_mnemonic, "add") && !strcmp (high_mnemonic, "addc");
  const bool carry_sub = !strcmp (low_mnemonic, "sub") && !strcmp (high_mnemonic, "subc");
  unsigned top_byte_mask;
  int size;

  if (!IS_ITEMP (result) || !left || !right)
    return false;

  size = k78k0_operandSize (result);
  if (size < 1 || size > K78K0_MAX_SCALAR_BYTES)
    return false;

  top_byte_mask = unsignedBitIntTopByteMask (result);

  if (size > 2)
    {
      int right_size = k78k0_operandSize (right);

      target = wideAssignmentTarget (ic, result, size);
      if (!target)
        return true;
      if (k78k0_operandSize (left) != size || right_size > size)
        return false;

      if (right_size != size && !IS_OP_LITERAL (right) && !SPEC_USIGN (getSpec (operandType (right))))
        return false;

      if (operandNeedsStackHL (left, size) || operandNeedsStackHL (right, size) || (target && operandNeedsStackHL (target, size)))
        setHLToSP ();

      for (int offset = 0; offset < size; offset++)
        {
          if (carry_add || carry_sub)
            {
              if (!loadOperandByteToA (left, offset))
                return false;
              emit2 ("mov", "c,a");
              if (!loadOperandByteToA (right, offset))
                return false;
              emit2 ("mov", "b,a");
              emit2 ("mov", "a,c");

              if (offset)
                {
                  emit2 ("pop", "psw");
                  stack_pushed--;
                  clearHLState ();
                }

              emit2 (offset ? high_mnemonic : low_mnemonic, "a,b");
            }
          else
            {
              if (!loadOperandByteToA (left, offset))
                return false;

              if (operandByteOnStack (right, offset))
                ensureHLToSPPreservingAX ();

              if (!aluOperandByteToA (offset ? high_mnemonic : low_mnemonic, right, offset))
                return false;
            }

          if (offset == size - 1 && top_byte_mask != 0xffu)
            emit2 ("and", "a,#0x%02x", top_byte_mask);

          if ((carry_add || carry_sub) && offset != size - 1)
            {
              emit2 ("push", "psw");
              stack_pushed++;
              clearHLState ();
            }

          if (!storeAToOperandByte (target, offset))
            return false;
        }

      return finishWideAssignment (ic, result, target, size);
    }

  if (operandNeedsStackHL (right, size) && !operandNeedsStackHL (left, size))
    setHLToSP ();

  if (!loadOperandByteToA (left, 0))
    return false;

  if (operandByteOnStack (right, 0))
    ensureHLToSPPreservingAX ();

  if (size == 1 && IS_OP_LITERAL (right) && operandLitValueUll (right) == 1u && (carry_add || carry_sub))
    emit2 (carry_add ? "inc" : "dec", "a");
  else if (!aluOperandByteToA (low_mnemonic, right, 0))
    return false;

  if (size == 1)
    {
      if (top_byte_mask != 0xffu)
        emit2 ("and", "a,#0x%02x", top_byte_mask);
      setAResult (result);
      return true;
    }

  if (size == 2)
    {
      emit2 ("mov", "b,a");
      if (carry_add)
        {
          emit2 ("mov", "a,#0x00");
          emit2 ("addc", "a,#0x00");
          emit2 ("mov", "c,a");
        }
      else if (carry_sub)
        {
          emit2 ("mov", "a,#0x00");
          emit2 ("subc", "a,#0x00");
          emit2 ("mov", "c,a");
        }

      adjustStackPointer (-2, true);
      emit2 ("mov", "a,b");
      emit2 ("mov", "[hl+0x00],a");
      if (carry_add || carry_sub)
        {
          emit2 ("mov", "a,c");
          emit2 ("mov", "[hl+0x01],a");
        }

      if (!loadOperandByteToA (left, 1))
        return false;

      if (operandByteOnStack (right, 1))
        ensureHLToSPPreservingAX ();

      if (!aluOperandByteToA (carry_add ? "add" : carry_sub ? "sub" : high_mnemonic, right, 1))
        return false;

      if (carry_add || carry_sub)
        {
          ensureHLToSPPreservingA ("b");
          emit2 ("add", "a,[hl+0x01]");
        }

      if (top_byte_mask != 0xffu)
        emit2 ("and", "a,#0x%02x", top_byte_mask);

      emit2 ("mov", "b,a");
      setHLToSP ();
      emit2 ("mov", "a,[hl+0x00]");
      emit2 ("mov", "x,a");
      emit2 ("mov", "a,b");
      emit2 ("movw", "de,ax");
      adjustStackPointer (2, false);
      emit2 ("movw", "ax,de");

      setReturnResult (result, size);
      return true;
    }

  emit2 ("mov", "x,a");

  if (!loadOperandByteToA (left, 1))
    return false;

  if (operandByteOnStack (right, 1))
    ensureHLToSPPreservingAX ();

  if (!aluOperandByteToA (high_mnemonic, right, 1))
    return false;

  if (top_byte_mask != 0xffu)
    emit2 ("and", "a,#0x%02x", top_byte_mask);

  setReturnResult (result, size);
  return true;
}

static bool
loadNarrowSignExtensionToC (const operand *op)
{
  sym_link *type = operandType (op);

  if (getSize (type) != 1)
    return false;

  if (SPEC_USIGN (getSpec (type)))
    {
      emit2 ("mov", "a,#0x00");
      emit2 ("mov", "c,a");
      return true;
    }

  if (!loadOperandByteToA (op, 0))
    return false;

  emitSignMaskForA ();
  emit2 ("mov", "c,a");
  return true;
}

static bool
genPlusWithNarrowOperand (const iCode *ic)
{
  operand *result = IC_RESULT (ic);
  operand *left = IC_LEFT (ic);
  operand *right = IC_RIGHT (ic);
  operand *wide;
  operand *narrow;

  if (!IS_ITEMP (result) || k78k0_operandSize (result) != 2)
    return false;

  if (k78k0_operandSize (left) == 2 && k78k0_operandSize (right) == 1)
    {
      wide = left;
      narrow = right;
    }
  else if (k78k0_operandSize (left) == 1 && k78k0_operandSize (right) == 2)
    {
      wide = right;
      narrow = left;
    }
  else
    return false;

  if (operandNeedsStackHL (wide, 2) || operandNeedsStackHL (narrow, 1))
    setHLToSP ();

  if (!loadOperandByteToA (narrow, 0))
    return false;
  emit2 ("mov", "b,a");

  if (!loadNarrowSignExtensionToC (narrow))
    return false;
  emit2 ("mov", "a,c");
  emit2 ("mov", "d,a");

  if (!loadOperandByteToA (wide, 0))
    return false;
  emit2 ("add", "a,b");
  emit2 ("mov", "b,a");
  emit2 ("mov", "a,#0x00");
  emit2 ("addc", "a,#0x00");
  emit2 ("mov", "c,a");

  adjustStackPointer (-2, true);
  emit2 ("mov", "a,b");
  emit2 ("mov", "[hl+0x00],a");
  emit2 ("mov", "a,c");
  emit2 ("mov", "[hl+0x01],a");

  if (!loadOperandByteToA (wide, 1))
    return false;
  emit2 ("add", "a,d");
  ensureHLToSPPreservingA ("b");
  emit2 ("add", "a,[hl+0x01]");
  emit2 ("mov", "b,a");
  setHLToSP ();
  emit2 ("mov", "a,[hl+0x00]");
  emit2 ("mov", "x,a");
  emit2 ("mov", "a,b");
  emit2 ("movw", "de,ax");
  adjustStackPointer (2, false);
  emit2 ("movw", "ax,de");

  const unsigned top_byte_mask = unsignedBitIntTopByteMask (result);
  if (top_byte_mask != 0xffu)
    emit2 ("and", "a,#0x%02x", top_byte_mask);

  setReturnResult (result, 2);
  return true;
}

static bool
genPlusWithLiteralOffset (const iCode *ic)
{
  operand *result = IC_RESULT (ic);
  operand *left = IC_LEFT (ic);
  operand *right = IC_RIGHT (ic);
  operand *base;
  operand *literal;
  long long offset;

  if (!IS_ITEMP (result) || k78k0_operandSize (result) != 2)
    return false;

  if (k78k0_operandSize (left) == 2 && IS_OP_LITERAL (right))
    {
      base = left;
      literal = right;
    }
  else if (IS_OP_LITERAL (left) && k78k0_operandSize (right) == 2)
    {
      base = right;
      literal = left;
    }
  else
    return false;

  offset = (long long)operandLitValue (literal);
  if (offset < -0xffffll || offset > 0xffffll)
    return false;

  if (!genOperandReturnValue (base))
    return false;

  adjustAX ((int)offset);

  const unsigned top_byte_mask = unsignedBitIntTopByteMask (result);
  if (top_byte_mask != 0xffu)
    emit2 ("and", "a,#0x%02x", top_byte_mask);

  setReturnResult (result, 2);
  return true;
}

static bool
genPlus (const iCode *ic)
{
  if (genPlusWithLiteralOffset (ic))
    return true;

  if (genPlusWithNarrowOperand (ic))
    return true;

  return genBinaryAccumulatorOp (ic, "add", "addc");
}

static bool
genMinus (const iCode *ic)
{
  return genBinaryAccumulatorOp (ic, "sub", "subc");
}

static bool
genBitwiseAnd (const iCode *ic)
{
  return genBinaryAccumulatorOp (ic, "and", "and");
}

static bool
genOr (const iCode *ic)
{
  return genBinaryAccumulatorOp (ic, "or", "or");
}

static bool
genXor (const iCode *ic)
{
  return genBinaryAccumulatorOp (ic, "xor", "xor");
}

static bool
genUnaryMinus (const iCode *ic)
{
  operand *result = IC_RESULT (ic);
  operand *left = IC_LEFT (ic);
  operand *target;
  int size;

  if (!IS_ITEMP (result) || !left)
    return false;

  size = k78k0_operandSize (result);
  if (size < 1 || size > K78K0_MAX_SCALAR_BYTES || k78k0_operandSize (left) != size)
    return false;

  if (IS_FLOAT (getSpec (operandType (result))))
    {
      target = wideAssignmentTarget (ic, result, size);
      if (!target)
        return true;

      if (operandNeedsStackHL (left, size) || (target && operandNeedsStackHL (target, size)))
        setHLToSP ();

      for (int offset = 0; offset < size; offset++)
        {
          if (!loadOperandByteToA (left, offset))
            return false;

          if (offset == size - 1)
            emit2 ("xor", "a,#0x80");

          if (!storeAToOperandByte (target, offset))
            return false;
        }

      return finishWideAssignment (ic, result, target, size);
    }

  if (size > 2)
    {
      target = wideAssignmentTarget (ic, result, size);
      if (!target)
        return true;

      if (operandNeedsStackHL (left, size) || (target && operandNeedsStackHL (target, size)))
        setHLToSP ();

      for (int offset = 0; offset < size; offset++)
        {
          if (!loadOperandByteToA (left, offset))
            return false;

          emit2 ("xor", "a,#0xff");
          if (offset == 0)
            emit2 ("add", "a,#0x01");
          else
            emit2 ("addc", "a,#0x00");

          if (!storeAToOperandByte (target, offset))
            return false;
        }

      return finishWideAssignment (ic, result, target, size);
    }

  if (size == 1)
    {
      if (!loadOperandByteToA (left, 0))
        return false;
      emit2 ("xor", "a,#0xff");
      emit2 ("add", "a,#0x01");
      setAResult (result);
      return true;
    }

  if (!genOperandReturnValue (left))
    return false;
  emit2 ("xch", "a,x");
  emit2 ("xor", "a,#0xff");
  emit2 ("add", "a,#0x01");
  emit2 ("xch", "a,x");
  emit2 ("xor", "a,#0xff");
  emit2 ("addc", "a,#0x00");

  setReturnResult (result, size);
  return true;
}

static bool
genWordMultByByteLiteral (const operand *source, const unsigned literal)
{
  if (!genOperandReturnValue (source))
    return false;

  if (!literal)
    {
      emit2 ("movw", "ax,#0x0000");
      return true;
    }

  if (literal == 1)
    return true;

  emit2 ("mov", "c,#0x%02x", literal);
  emit2 ("mov", "b,a");
  emit2 ("mov", "a,c");
  emit2 ("xch", "a,x");
  emit2 ("mulu", "x");
  emit2 ("movw", "de,ax");

  emit2 ("mov", "a,c");
  emit2 ("mov", "x,a");
  emit2 ("mov", "a,b");
  emit2 ("mulu", "x");
  emit2 ("mov", "a,x");
  emit2 ("add", "a,d");
  emit2 ("mov", "d,a");
  emit2 ("movw", "ax,de");
  return true;
}

static bool
genMult (const iCode *ic)
{
  operand *result = IC_RESULT (ic);
  operand *left = IC_LEFT (ic);
  operand *right = IC_RIGHT (ic);
  operand *word_source = NULL;
  unsigned long long literal = 0;
  unsigned top_byte_mask;
  int size;

  if (!IS_ITEMP (result) || !left || !right)
    return false;

  size = k78k0_operandSize (result);
  if (size < 1 || size > 2)
    return false;

  if (size == 2 && IS_OP_LITERAL (left) && operandLitValueUll (left) <= 255 &&
      getSize (operandType (right)) == 2)
    {
      word_source = right;
      literal = operandLitValueUll (left);
    }
  else if (size == 2 && IS_OP_LITERAL (right) && operandLitValueUll (right) <= 255 &&
           getSize (operandType (left)) == 2)
    {
      word_source = left;
      literal = operandLitValueUll (right);
    }

  if (word_source)
    {
      if (!genWordMultByByteLiteral (word_source, (unsigned)literal))
        return false;
      setReturnResult (result, size);
      return true;
    }

  if (!isUnsignedByteSource (left) || !isUnsignedByteSource (right))
    return false;

  top_byte_mask = unsignedBitIntTopByteMask (result);

  if (operandNeedsStackHL (left, 1) || operandNeedsStackHL (right, 1))
    setHLToSP ();

  if (!loadOperandByteToA (left, 0))
    return false;
  emit2 ("mov", "c,a");

  if (!loadOperandByteToA (right, 0))
    return false;
  emit2 ("mov", "x,a");
  emit2 ("mov", "a,c");
  emit2 ("mulu", "x");

  if (size == 1)
    {
      emit2 ("mov", "a,x");
      if (top_byte_mask != 0xffu)
        emit2 ("and", "a,#0x%02x", top_byte_mask);
      setAResult (result);
    }
  else
    {
      if (top_byte_mask != 0xffu)
        emit2 ("and", "a,#0x%02x", top_byte_mask);
      setReturnResult (result, size);
    }

  return true;
}

static bool
compareOperandBytes (const operand *left, const operand *right, const int offset)
{
  if (operandNeedsStackHL (left, offset + 1) || operandNeedsStackHL (right, offset + 1))
    setHLToSP ();

  if (!loadOperandByteToA (left, offset))
    return false;

  if (operandByteOnStack (right, offset))
    ensureHLToSPPreservingA ("c");

  return cmpOperandByteWithA (right, offset);
}

static bool
compareOperandBytesBiased (const operand *left, const operand *right, const int offset, const bool bias)
{
  if (operandNeedsStackHL (left, offset + 1) || operandNeedsStackHL (right, offset + 1))
    setHLToSP ();

  if (!loadOperandByteToA (right, offset))
    return false;

  if (bias)
    emit2 ("xor", "a,#0x80");
  emit2 ("mov", "c,a");

  if (!loadOperandByteToA (left, offset))
    return false;

  if (bias)
    emit2 ("xor", "a,#0x80");

  emit2 ("cmp", "a,c");
  return true;
}

static void
genBooleanResult (const operand *result, const char *true_label, const char *done_label)
{
  const int result_size = k78k0_operandSize (result);

  emit2 ("mov", "a,#0x00");
  emit2 ("br", "%s", done_label);
  emitLocalLabel (true_label);
  emit2 ("mov", "a,#0x01");
  emitLocalLabel (done_label);

  if (result_size <= 1)
    setAResult (result);
  else
    {
      emit2 ("mov", "x,a");
      emit2 ("mov", "a,#0x00");
      setReturnResult (result, result_size);
    }
}

static bool
genNot (const iCode *ic)
{
  operand *result = IC_RESULT (ic);
  operand *left = IC_LEFT (ic);

  if (!IS_ITEMP (result) || !left || k78k0_operandSize (result) < 1 || k78k0_operandSize (result) > 2)
    return false;

  if (!testOperandForZero (left))
    return false;

  materializeZeroTest (result, true);
  return true;
}

static bool
genCmpEqNe (const iCode *ic)
{
  operand *result = IC_RESULT (ic);
  operand *left = IC_LEFT (ic);
  operand *right = IC_RIGHT (ic);
  const bool is_ne = ic->op == NE_OP;
  char true_label[32];
  char false_label[32];
  char done_label[32];
  int size;

  if (!IS_ITEMP (result) || !left || !right)
    return false;

  size = k78k0_operandSize (left);
  if (size < 1 || size > K78K0_MAX_SCALAR_BYTES || k78k0_operandSize (right) != size)
    return false;

  makeLocalLabel (true_label, sizeof (true_label));
  makeLocalLabel (false_label, sizeof (false_label));
  makeLocalLabel (done_label, sizeof (done_label));

  for (int offset = 0; offset < size; offset++)
    {
      if (!compareOperandBytes (left, right, offset))
        return false;
      emitCondBranch ("bnz", is_ne ? true_label : false_label);
    }

  if (!is_ne)
    emit2 ("br", "!%s", true_label);

  if (is_ne)
    emit2 ("br", "!%s", false_label);

  emitLocalLabel (false_label);
  genBooleanResult (result, true_label, done_label);
  return true;
}

static bool
genCmpLtGt (const iCode *ic)
{
  operand *result = IC_RESULT (ic);
  operand *left = IC_LEFT (ic);
  operand *right = IC_RIGHT (ic);
  const bool is_gt = ic->op == '>';
  bool is_unsigned;
  bool is_signed;
  char true_label[32];
  char false_label[32];
  char done_label[32];
  int size;

  if (!IS_ITEMP (result) || !left || !right)
    return false;

  is_unsigned = SPEC_USIGN (getSpec (operandType (left))) && SPEC_USIGN (getSpec (operandType (right)));
  is_signed = !SPEC_USIGN (getSpec (operandType (left))) && !SPEC_USIGN (getSpec (operandType (right)));

  if (!is_unsigned && !is_signed)
    return false;

  size = k78k0_operandSize (left);
  if (size < 1 || size > K78K0_MAX_SCALAR_BYTES || k78k0_operandSize (right) != size)
    return false;

  makeLocalLabel (true_label, sizeof (true_label));
  makeLocalLabel (false_label, sizeof (false_label));
  makeLocalLabel (done_label, sizeof (done_label));

  for (int offset = size - 1; offset >= 0; offset--)
    {
      if (is_signed && offset == size - 1)
        {
          if (!compareOperandBytesBiased (is_gt ? right : left, is_gt ? left : right, offset, true))
            return false;
        }
      else if (!compareOperandBytes (is_gt ? right : left, is_gt ? left : right, offset))
        return false;

      emitCondBranch ("bc", true_label);
      if (offset)
        emitCondBranch ("bnz", false_label);
    }

  emit2 ("br", "!%s", false_label);

  emitLocalLabel (false_label);
  genBooleanResult (result, true_label, done_label);
  return true;
}

static bool
pushBigReturnAddress (const operand *result)
{
  const symbol *sym;
  int stack_offset;

  if (!result || !IS_SYMOP (result))
    return false;

  sym = operandStorageSymbol (result);
  if (sym->onStack)
    {
      stack_offset = stackByteOffset (sym, 0);
      emit2 ("movw", "ax,sp");
      adjustAX (stack_offset);
    }
  else if (sym->rname[0])
    emit2 ("movw", "ax,#%s", sym->rname);
  else
    return false;

  emit2 ("push", "ax");
  stack_pushed += 2;
  clearRegisterState ();
  return true;
}

static bool
finishCall (const iCode *ic, sym_link *ftype, operand *result, const int result_size, const int return_size,
            const int extra_stack_bytes)
{
  const int cleanup_bytes = ic->parmBytes + extra_stack_bytes;

  if (cleanup_bytes)
    {
      if (ftype && IS_FUNC (ftype) && !FUNC_HASVARARGS (ftype))
        {
          stack_pushed -= cleanup_bytes;
          wassertl (stack_pushed >= 0, "78K0 outgoing stack accounting underflow.");
        }
      else
        {
          saveScalarAcrossStackAdjustment (return_size);
          adjustStackPointer (cleanup_bytes, false);
          restoreScalarAcrossStackAdjustment (return_size);
        }
    }

  if (result_size > 0 && result_size <= K78K0_MAX_SCALAR_BYTES)
    {
      operand *target;

      if (result_size == 1 && return_size > 1)
        emit2 ("mov", "a,x");

      target = wideAssignmentTarget (ic, result, result_size);
      if (target)
        {
          const symbol *storage = operandStorageSymbol (target);
          const bool stored = storage->onStack ?
            (result_size > 2 ? storeReturnRegistersToStack (storage, result_size) : storeAccumulatorToStack (storage, result_size)) :
            storeReturnRegistersToDirect (storage, result_size);

          if (stored)
            {
              if (target != result)
                ic->next->generated = true;
              clearAResult ();
              return true;
            }
        }

      if (result_size <= 2)
        setReturnResult (result, result_size);
      else if (IS_ITEMP (result) && OP_SYMBOL_CONST (result)->liveTo <= ic->seq)
        clearAResult ();
      else
        return false;
    }
  else
    clearAResult ();

  return true;
}

static bool
genCall (const iCode *ic)
{
  operand *left = IC_LEFT (ic);
  operand *result = IC_RESULT (ic);
  const bool bigreturn = typeReturnsViaHiddenPointer (operandType (left));
  sym_link *ftype = IS_FUNCPTR (operandType (left)) ? operandType (left)->next : operandType (left);
  int first_regarg_size = 0;
  int result_size = 0;
  int return_size = 0;

  if (ic->op != CALL || !left)
    return false;

  clearRegisterState ();

  if (bigreturn)
    {
      first_regarg_size = functionFirstRegArgSize (ftype);
      if (first_regarg_size)
        emit2 ("movw", "de,ax");
      wassertl (result, "78K0 large-return call has no destination.");
      if (!pushBigReturnAddress (result))
        return false;
      if (first_regarg_size)
        emit2 ("movw", "ax,de");
    }

  if (IS_SYMOP (left))
    {
      const symbol *sym = OP_SYMBOL_CONST (left);

      if (!sym->rname[0])
        return false;

      emit2 ("call", "!%s", sym->rname);
    }
  else if (IS_OP_LITERAL (left))
    emit2 ("call", "!0x%04x", (unsigned)(operandLitValueUll (left) & 0xffffu));
  else
    return false;

  if (!bigreturn && result && IS_ITEMP (result))
    result_size = k78k0_operandSize (result);

  if (!bigreturn && ftype && IS_FUNC (ftype) && ftype->next && !IS_VOID (ftype->next))
    return_size = getSize (ftype->next);

  return finishCall (ic, ftype, result, result_size, return_size, bigreturn ? 2 : 0);
}

static bool
genPcall (const iCode *ic)
{
  operand *left = IC_LEFT (ic);
  operand *result = IC_RESULT (ic);
  const bool bigreturn = typeReturnsViaHiddenPointer (operandType (left));
  sym_link *ftype = IS_FUNCPTR (operandType (left)) ? operandType (left)->next : operandType (left);
  char return_label[32];
  int first_regarg_size = 0;
  int result_size = 0;
  int return_size = 0;

  if (ic->op != PCALL || !left || k78k0_operandSize (left) != 2)
    return false;

  first_regarg_size = functionFirstRegArgSize (ftype);
  if (first_regarg_size)
    emit2 ("movw", "de,ax");

  if (bigreturn)
    {
      wassertl (result, "78K0 large-return indirect call has no destination.");
      if (!pushBigReturnAddress (result))
        return false;
    }

  if (!genOperandReturnValue (left))
    return false;

  emit2 ("movw", "hl,ax");
  makeLocalLabel (return_label, sizeof (return_label));
  clearRegisterState ();
  emit2 ("movw", "ax,#%s", return_label);
  emit2 ("push", "ax");
  emit2 ("push", "hl");
  if (first_regarg_size)
    emit2 ("movw", "ax,de");
  emit2 ("ret", "");
  emitLocalLabel (return_label);

  if (!bigreturn && result && IS_ITEMP (result))
    result_size = k78k0_operandSize (result);

  if (!bigreturn && ftype && IS_FUNC (ftype) && ftype->next && !IS_VOID (ftype->next))
    return_size = getSize (ftype->next);

  return finishCall (ic, ftype, result, result_size, return_size, bigreturn ? 2 : 0);
}

static bool
genIfx (const iCode *ic)
{
  operand *cond = IC_COND (ic);
  symbol *target;
  char label[32];

  if (!cond)
    return false;

  if (!testOperandForZero (cond))
    return false;

  if (IC_FALSE (ic))
    {
      target = IC_FALSE (ic);
      makeICLabel (label, sizeof (label), target);
      emitCondBranch ("bz", label);
    }
  else if (IC_TRUE (ic))
    {
      target = IC_TRUE (ic);
      makeICLabel (label, sizeof (label), target);
      emitCondBranch ("bnz", label);
    }
  else
    return false;

  clearRegisterState ();
  return true;
}

static bool
genOperandReturnValue (const operand *op)
{
  int size;

  op = resolveReqvOperand (op);
  size = k78k0_operandSize (op);

  if (IS_OP_LITERAL (op))
    {
      genLiteralReturnValue (op);
      return true;
    }

  if (IS_SYMOP (op))
    {
      const symbol *sym = operandStorageSymbol (op);

      if (operandInReturnValue (op, size))
        return return_result_ax_valid;

      if (operandInA (op, 0) && size == 1)
        return true;

      if (loadRematerializedAddressToAX (op))
        return true;

      if (loadFunctionAddressToAX (op))
        return true;

      if (loadStackToReturnValue (sym, size))
        return true;

      if (loadDirectToReturnValue (sym, size))
        return true;
    }

  return false;
}

static bool
rematerializedAddress (const iCode *ic, const symbol **base, long *offset)
{
  if (!ic)
    return false;

  if (ic->op == ADDRESS_OF)
    {
      if (!IS_TRUE_SYMOP (IC_LEFT (ic)) || !IC_RIGHT (ic) || !IS_OP_LITERAL (IC_RIGHT (ic)))
        return false;

      *base = OP_SYMBOL_CONST (IC_LEFT (ic));
      *offset += (long)operandLitValue (IC_RIGHT (ic));
      return true;
    }

  if (ic->op == CAST || ic->op == '=')
    {
      if (!IS_SYMOP (IC_RIGHT (ic)) || !OP_SYMBOL_CONST (IC_RIGHT (ic))->remat)
        return false;

      return rematerializedAddress (OP_SYMBOL_CONST (IC_RIGHT (ic))->rematiCode, base, offset);
    }

  if (ic->op == '+' || ic->op == '-')
    {
      const iCode *next_ic = NULL;
      long literal;

      if (IS_SYMOP (IC_LEFT (ic)) && OP_SYMBOL_CONST (IC_LEFT (ic))->remat && IS_OP_LITERAL (IC_RIGHT (ic)))
        {
          next_ic = OP_SYMBOL_CONST (IC_LEFT (ic))->rematiCode;
          literal = (long)operandLitValue (IC_RIGHT (ic));
        }
      else if (ic->op == '+' && IS_OP_LITERAL (IC_LEFT (ic)) &&
               IS_SYMOP (IC_RIGHT (ic)) && OP_SYMBOL_CONST (IC_RIGHT (ic))->remat)
        {
          next_ic = OP_SYMBOL_CONST (IC_RIGHT (ic))->rematiCode;
          literal = (long)operandLitValue (IC_LEFT (ic));
        }
      else
        return false;

      if (ic->op == '-')
        literal = -literal;

      if (!rematerializedAddress (next_ic, base, offset))
        return false;

      *offset += literal;
      return true;
    }

  return false;
}

static bool
loadRematerializedAddressToAX (const operand *op)
{
  const symbol *base = NULL;
  const symbol *sym;
  long offset = 0;

  if (!IS_SYMOP (op) || !IS_ITEMP (op))
    return false;

  sym = OP_SYMBOL_CONST (op);
  if (!sym->remat || !rematerializedAddress (sym->rematiCode, &base, &offset) || !base)
    return false;

  clearRegisterState ();

  if (base->onStack)
    {
      const int stack_offset = stackByteOffset (base, (int)offset);

      emit2 ("movw", "ax,sp");
      adjustAX (stack_offset);
      return true;
    }

  if (!base->rname[0])
    return false;

  if (offset)
    emit2 ("movw", "ax,#%s + %ld", base->rname, offset);
  else
    emit2 ("movw", "ax,#%s", base->rname);

  return true;
}

static bool
loadFunctionAddressToAX (const operand *op)
{
  const symbol *sym;

  if (!IS_SYMOP (op) || !IS_FUNC (operandType (op)))
    return false;

  sym = OP_SYMBOL_CONST (op);
  if (!sym->rname[0])
    return false;

  clearRegisterState ();
  emit2 ("movw", "ax,#%s", sym->rname);
  return true;
}

static bool
loadUnsignedOperandToAX (const operand *op)
{
  const int size = k78k0_operandSize (op);

  if (size == 2)
    return genOperandReturnValue (op);

  if (!isUnsignedByteSource (op) && !(IS_OP_LITERAL (op) && operandLitValueUll (op) <= 0xffu))
    return false;

  if (!loadOperandByteToA (op, 0))
    return false;

  emit2 ("mov", "x,a");
  emit2 ("mov", "a,#0x00");
  clearAResult ();
  return true;
}

static bool
loadPointerToHL (const operand *op)
{
  const int size = k78k0_operandSize (op);

  if (size != 2)
    return false;

  if (!genOperandReturnValue (op))
    return false;

  emit2 ("movw", "hl,ax");
  hl_is_sp = false;
  hl_sp_offset_valid = false;
  return true;
}

static bool
savePointerToDE (const operand *op)
{
  const int size = getSize (operandType (op));

  if (size != 2)
    return false;

  if (!genOperandReturnValue (op))
    return false;

  emit2 ("movw", "de,ax");
  return true;
}

static bool
getPointerOffset (const iCode *ic, long *offset)
{
  operand *right = IC_RIGHT (ic);

  if (!right || !IS_OP_LITERAL (right))
    return false;

  *offset = (long)operandLitValue (right);
  return *offset >= -65535l && *offset <= 65535l;
}

static bool
genAddrOf (const iCode *ic)
{
  operand *result = IC_RESULT (ic);
  operand *left = IC_LEFT (ic);
  operand *right = IC_RIGHT (ic);
  const symbol *sym;
  long offset;

  if (!IS_ITEMP (result) || !IS_TRUE_SYMOP (left) || !right || !IS_OP_LITERAL (right))
    return false;

  if (k78k0_operandSize (result) != 2)
    return false;

  sym = OP_SYMBOL_CONST (left);
  offset = (long)operandLitValue (right);

  clearRegisterState ();

  if (sym->onStack)
    {
      const int stack_offset = stackByteOffset (sym, offset);

      emit2 ("movw", "ax,sp");
      adjustAX (stack_offset);
    }
  else if (sym->rname[0])
    {
      if (offset)
        emit2 ("movw", "ax,#%s + %ld", sym->rname, offset);
      else
        emit2 ("movw", "ax,#%s", sym->rname);
    }
  else
    return false;

  setReturnResult (result, 2);
  return true;
}

static sym_link *
pointerBitFieldType (const operand *ptr)
{
  sym_link *type = operandType (ptr);

  if (!type || !type->next || !IS_BITFIELD (getSpec (type->next)))
    return NULL;

  return getSpec (type->next);
}

static void
setHLFromDE (void)
{
  emit2 ("movw", "ax,de");
  emit2 ("movw", "hl,ax");
  hl_is_sp = false;
  hl_sp_offset_valid = false;
}

static bool
savePointerToDEWithOffset (const operand *ptr, long *offset, const unsigned size)
{
  if (*offset >= 0 && (unsigned long)*offset + size <= 256u)
    return savePointerToDE (ptr);

  if (!genOperandReturnValue (ptr))
    return false;

  adjustAX ((int)*offset);
  emit2 ("movw", "de,ax");
  *offset = 0;
  return true;
}

static bool
loadPointerToHLWithOffset (const operand *ptr, long *offset, const unsigned size)
{
  if (*offset >= 0 && (unsigned long)*offset + size <= 256u)
    return loadPointerToHL (ptr);

  if (!genOperandReturnValue (ptr))
    return false;

  adjustAX ((int)*offset);
  emit2 ("movw", "hl,ax");
  clearHLState ();
  *offset = 0;
  return true;
}

static bool
genPointerGetBitField (const operand *result, const operand *ptr, long pointer_offset, sym_link *type)
{
  const int bit_start = SPEC_BSTR (type);
  const int bit_length = SPEC_BLEN (type);
  const int result_size = k78k0_operandSize (result);
  const int storage_size = (bit_start + bit_length + 7) / 8;
  const bool sign_extend = !SPEC_USIGN (type) && !IS_BOOLEAN (type);

  if (bit_start < 0 || bit_start > 7 || bit_length < 1 || bit_length > K78K0_MAX_SCALAR_BYTES * 8 ||
      result_size < 1 || result_size > K78K0_MAX_SCALAR_BYTES ||
      !savePointerToDEWithOffset (ptr, &pointer_offset, (unsigned)storage_size))
    return false;

  for (int byte = 0; byte < result_size; byte++)
    {
      const int remaining_bits = bit_length - byte * 8;

      setHLFromDE ();
      emit2 ("mov", "a,[hl+0x%02x]", (unsigned)pointer_offset + (unsigned)byte);

      if (bit_start)
        {
          for (int shift = 0; shift < bit_start; shift++)
            emitByteRightShift ();
          emit2 ("mov", "c,a");

          if (byte + 1 < storage_size)
            {
              setHLFromDE ();
              emit2 ("mov", "a,[hl+0x%02x]", (unsigned)pointer_offset + (unsigned)byte + 1u);
              for (int shift = bit_start; shift < 8; shift++)
                emitByteLeftShift ();
              emit2 ("or", "a,c");
            }
          else
            emit2 ("mov", "a,c");
        }

      if (remaining_bits < 8)
        {
          const unsigned value_mask = (1u << remaining_bits) - 1u;

          emit2 ("and", "a,#0x%02x", value_mask);
          if (sign_extend)
            {
              char positive_label[32];

              makeLocalLabel (positive_label, sizeof (positive_label));
              emit2 ("cmp", "a,#0x%02x", 1u << (remaining_bits - 1));
              emit2 ("bc", "%s", positive_label);
              emit2 ("or", "a,#0x%02x", (~value_mask) & 0xffu);
              emitLocalLabel (positive_label);
            }
        }

      if (!storeAToOperandByte (result, byte))
        return false;
      clearAResult ();
    }

  clearHLState ();
  return true;
}

static bool
genPointerGet (const iCode *ic)
{
  operand *result = IC_RESULT (ic);
  operand *left = IC_LEFT (ic);
  operand *target;
  sym_link *bit_field_type;
  long offset;
  int size;

  if (!IS_ITEMP (result) || !left || ic->op != GET_VALUE_AT_ADDRESS)
    return false;

  size = k78k0_operandSize (result);
  if (size < 1 || size > K78K0_MAX_SCALAR_BYTES || !getPointerOffset (ic, &offset))
    return false;

  bit_field_type = getSpec (operandType (result));
  if (IS_BITFIELD (bit_field_type))
    return genPointerGetBitField (result, left, offset, bit_field_type);

  if (!loadPointerToHLWithOffset (left, &offset, (unsigned)size))
    return false;

  if (size == 1)
    {
      emit2 ("mov", "a,[hl+0x%02x]", (unsigned)offset);
      setAResult (result);
      return true;
    }

  if (size == 2)
    {
      emit2 ("mov", "a,[hl+0x%02x]", (unsigned)offset);
      emit2 ("mov", "x,a");
      emit2 ("mov", "a,[hl+0x%02x]", (unsigned)(offset + 1u));
      setReturnResult (result, size);
      return true;
    }

  if (size <= K78K0_MAX_SCALAR_BYTES)
    {
      target = wideAssignmentTarget (ic, result, size);
      if (!target)
        return true;
      emit2 ("movw", "ax,hl");
      emit2 ("movw", "de,ax");
      for (int byte = 0; byte < size; byte++)
        {
          setHLFromDE ();
          emit2 ("mov", "a,[hl+0x%02x]", (unsigned)(offset + byte));
          if (!storeAToOperandByte (target, byte))
            return false;
        }
      return finishWideAssignment (ic, result, target, size);
    }

  return false;
}

static bool
genPointerSetBitField (const operand *ptr, const operand *value, sym_link *type)
{
  const int bit_start = SPEC_BSTR (type);
  const int bit_length = SPEC_BLEN (type);
  const int value_size = k78k0_operandSize (value);
  const int storage_size = (bit_start + bit_length + 7) / 8;

  if (bit_start < 0 || bit_start > 7 || bit_length < 1 || bit_length > K78K0_MAX_SCALAR_BYTES * 8 ||
      value_size < 1 || value_size > K78K0_MAX_SCALAR_BYTES || storage_size < 1 || storage_size > 256 ||
      !savePointerToDE (ptr))
    return false;
  for (int byte = 0; byte < storage_size; byte++)
    {
      const int first_bit = byte ? 0 : bit_start;
      const int remaining_end_bit = bit_start + bit_length - byte * 8;
      const int end_bit = remaining_end_bit < 8 ? remaining_end_bit : 8;
      const unsigned field_mask = (((1u << (end_bit - first_bit)) - 1u) << first_bit) & 0xffu;

      clearAResult ();
      if (byte < value_size)
        {
          if (!loadOperandByteToA (value, byte))
            return false;
        }
      else
        emit2 ("mov", "a,#0x00");

      for (int shift = 0; shift < bit_start; shift++)
        emitByteLeftShift ();
      emit2 ("mov", "c,a");

      if (bit_start && byte > 0 && byte - 1 < value_size)
        {
          clearAResult ();
          if (!loadOperandByteToA (value, byte - 1))
            return false;
          for (int shift = bit_start; shift < 8; shift++)
            emitByteRightShift ();
          emit2 ("or", "a,c");
        }
      else
        emit2 ("mov", "a,c");

      if (field_mask != 0xffu)
        emit2 ("and", "a,#0x%02x", field_mask);
      emit2 ("mov", "b,a");

      setHLFromDE ();
      if (field_mask == 0xffu)
        emit2 ("mov", "a,b");
      else
        {
          emit2 ("mov", "a,[hl+0x%02x]", (unsigned)byte);
          emit2 ("and", "a,#0x%02x", (~field_mask) & 0xffu);
          emit2 ("or", "a,b");
        }
      emit2 ("mov", "[hl+0x%02x],a", (unsigned)byte);
    }

  clearRegisterState ();
  return true;
}

static bool
genPointerSet (const iCode *ic)
{
  operand *ptr;
  operand *value = IC_RIGHT (ic);
  sym_link *bit_field_type;
  int size;

  if (ic->op == SET_VALUE_AT_ADDRESS)
    ptr = IC_LEFT (ic);
  else if (POINTER_SET (ic))
    ptr = IC_RESULT (ic);
  else
    return false;

  if (!ptr || !value)
    return false;

  bit_field_type = pointerBitFieldType (ptr);
  if (bit_field_type)
    return genPointerSetBitField (ptr, value, bit_field_type);

  size = k78k0_operandSize (value);
  if (size < 1 || size > K78K0_MAX_SCALAR_BYTES)
    return false;

  if (!savePointerToDE (ptr))
    return false;

  for (int offset = 0; offset < size; offset++)
    {
      if (!loadOperandByteToA (value, offset))
        return false;
      emit2 ("mov", "b,a");

      emit2 ("movw", "ax,de");
      emit2 ("movw", "hl,ax");
      hl_is_sp = false;
      hl_sp_offset_valid = false;
      emit2 ("mov", "a,b");
      emit2 ("mov", "[hl+0x%02x],a", (unsigned)offset);
    }

  clearAResult ();
  return true;
}

static bool
genDivMod (const iCode *ic)
{
  operand *result = IC_RESULT (ic);
  operand *left = IC_LEFT (ic);
  operand *right = IC_RIGHT (ic);
  const bool is_mod = ic->op == '%';
  int result_size;

  if (!IS_ITEMP (result) || !left || !right || !isUnsignedByteDivisor (right))
    return false;

  result_size = getSize (operandType (result));
  if (result_size < 1 || result_size > 2)
    return false;

  if (getSize (operandType (left)) > 2 || !SPEC_USIGN (getSpec (operandType (left))))
    return false;

  if (operandNeedsStackHL (left, getSize (operandType (left))) || operandNeedsStackHL (right, 1))
    setHLToSP ();

  if (!loadOperandByteToA (right, 0))
    return false;
  emit2 ("mov", "c,a");

  if (!loadUnsignedOperandToAX (left))
    return false;

  emit2 ("divuw", "c");

  if (is_mod)
    {
      emit2 ("mov", "a,c");
      if (result_size == 1)
        setAResult (result);
      else
        {
          emit2 ("mov", "x,a");
          emit2 ("mov", "a,#0x00");
          setReturnResult (result, result_size);
        }
    }
  else if (result_size == 1)
    {
      emit2 ("mov", "a,x");
      setAResult (result);
    }
  else
    setReturnResult (result, result_size);

  return true;
}

static void
emitByteLeftShift (void)
{
  emit2 ("clr1", "cy");
  emit2 ("rolc", "a,1");
}

static void
emitByteRightShift (void)
{
  emit2 ("clr1", "cy");
  emit2 ("rorc", "a,1");
}

static void
emitSignCarryForA (void)
{
  emit2 ("mov1", "cy,a.7");
}

static void
emitByteArithmeticRightShift (void)
{
  emitSignCarryForA ();
  emit2 ("rorc", "a,1");
}

static void
emitSignMaskForA (void)
{
  emit2 ("rolc", "a,1");
  emit2 ("mov", "a,#0x00");
  emit2 ("subc", "a,#0x00");
}

static void
emitWordLeftShift (void)
{
  emit2 ("xch", "a,x");
  emitByteLeftShift ();
  emit2 ("xch", "a,x");
  emit2 ("rolc", "a,1");
}

static void
emitWordRightShift (void)
{
  emitByteRightShift ();
  emit2 ("xch", "a,x");
  emit2 ("rorc", "a,1");
  emit2 ("xch", "a,x");
}

static void
emitWordArithmeticRightShift (void)
{
  emitByteArithmeticRightShift ();
  emit2 ("xch", "a,x");
  emit2 ("rorc", "a,1");
  emit2 ("xch", "a,x");
}

static void
emitVariableShiftLoop (const int size, const bool is_right, const bool is_signed_right)
{
  char loop_label[32];
  char done_label[32];

  makeLocalLabel (loop_label, sizeof (loop_label));
  makeLocalLabel (done_label, sizeof (done_label));

  emit2 ("mov", "b,a");
  emit2 ("mov", "a,c");
  emit2 ("cmp", "a,#0x00");
  emit2 ("mov", "a,b");
  emit2 ("bz", "%s", done_label);

  emitLocalLabel (loop_label);

  if (size == 1)
    {
      if (is_signed_right)
        emitByteArithmeticRightShift ();
      else if (is_right)
        emitByteRightShift ();
      else
        emitByteLeftShift ();
    }
  else
    {
      if (is_signed_right)
        emitWordArithmeticRightShift ();
      else if (is_right)
        emitWordRightShift ();
      else
        emitWordLeftShift ();
    }

  emit2 ("dbnz", "c,%s", loop_label);
  emitLocalLabel (done_label);
}

static bool
copyOperandToTarget (const operand *source, const operand *target, const int size)
{
  source = resolveReqvOperand (source);
  target = resolveReqvOperand (target);

  if (IS_SYMOP (source) && IS_SYMOP (target) &&
      operandStorageSymbol (source) == operandStorageSymbol (target))
    return true;

  if (operandNeedsStackHL (source, size) || operandNeedsStackHL (target, size))
    setHLToSP ();

  for (int offset = 0; offset < size; offset++)
    {
      if (!loadOperandByteToA (source, offset))
        return false;

      if (!storeAToOperandByte (target, offset))
        return false;
    }

  return true;
}

static bool
clearTargetBytes (const operand *target, const int size)
{
  if (operandNeedsStackHL (target, size))
    setHLToSP ();

  emit2 ("mov", "a,#0x00");
  for (int offset = 0; offset < size; offset++)
    {
      if (!storeAToOperandByte (target, offset))
        return false;
    }

  return true;
}

static bool
fillTargetBytesWithA (const operand *target, const int size)
{
  if (operandNeedsStackHL (target, size))
    ensureHLToSPPreservingA ("c");

  for (int offset = 0; offset < size; offset++)
    {
      if (!storeAToOperandByte (target, offset))
        return false;
    }

  return true;
}

static bool
shiftTargetLeftOne (const operand *target, const int size)
{
  emit2 ("clr1", "cy");

  for (int offset = 0; offset < size; offset++)
    {
      if (!loadOperandByteToA (target, offset))
        return false;

      emit2 ("rolc", "a,1");
      if (!storeAToOperandByte (target, offset))
        return false;
    }

  return true;
}

static bool
shiftTargetLeftBytes (const operand *target, const int size, const int count)
{
  int offset;

  for (offset = size - 1; offset >= count; offset--)
    {
      if (!loadOperandByteToA (target, offset - count))
        return false;
      if (!storeAToOperandByte (target, offset))
        return false;
    }

  emit2 ("mov", "a,#0x00");
  for (offset = 0; offset < count; offset++)
    if (!storeAToOperandByte (target, offset))
      return false;

  return true;
}

static bool
shiftTargetRightOne (const operand *target, const int size, const bool is_signed_right)
{
  int offset = size - 1;

  if (!loadOperandByteToA (target, offset))
    return false;

  if (is_signed_right)
    emitSignCarryForA ();
  else
    emit2 ("clr1", "cy");

  emit2 ("rorc", "a,1");
  if (!storeAToOperandByte (target, offset))
    return false;

  while (offset--)
    {
      if (!loadOperandByteToA (target, offset))
        return false;

      emit2 ("rorc", "a,1");
      if (!storeAToOperandByte (target, offset))
        return false;
    }

  return true;
}

static bool
shiftTargetRightBytes (const operand *target, const int size, const int count, const bool is_signed_right)
{
  int offset;

  if (is_signed_right)
    {
      if (!loadOperandByteToA (target, size - 1))
        return false;
      emitSignMaskForA ();
      emit2 ("mov", "b,a");
    }

  for (offset = 0; offset < size - count; offset++)
    {
      if (!loadOperandByteToA (target, offset + count))
        return false;
      if (!storeAToOperandByte (target, offset))
        return false;
    }

  if (is_signed_right)
    emit2 ("mov", "a,b");
  else
    emit2 ("mov", "a,#0x00");
  for (; offset < size; offset++)
    if (!storeAToOperandByte (target, offset))
      return false;

  return true;
}

static bool
shiftTargetByBits (const operand *target, const int size, const bool is_right, const bool is_signed_right, const unsigned count)
{
  char loop_label[32];

  wassertl (count > 0 && count < 8, "78K0 invalid residual shift count.");

  if (count > 1)
    {
      makeLocalLabel (loop_label, sizeof (loop_label));
      emit2 ("mov", "b,#0x%02x", count);
      emitLocalLabel (loop_label);
    }

  if (is_right)
    {
      if (!shiftTargetRightOne (target, size, is_signed_right))
        return false;
    }
  else if (!shiftTargetLeftOne (target, size))
    return false;

  if (count > 1)
    emit2 ("dbnz", "b,%s", loop_label);
  return true;
}

static bool
maskTargetTopByte (const operand *target, const int size, const unsigned mask)
{
  if (mask == 0xffu)
    return true;

  if (!loadOperandByteToA (target, size - 1))
    return false;
  emit2 ("and", "a,#0x%02x", mask);
  return storeAToOperandByte (target, size - 1);
}

static bool
genWideLiteralShift (const iCode *ic, const bool is_right, const bool is_signed_right, unsigned long long count)
{
  operand *result = IC_RESULT (ic);
  operand *left = IC_LEFT (ic);
  operand *target;
  const unsigned top_byte_mask = unsignedBitIntTopByteMask (result);
  unsigned byte_count;
  int size = getSize (operandType (result));

  if (size < 1 || size > K78K0_MAX_SCALAR_BYTES || getSize (operandType (left)) != size)
    return false;

  target = wideAssignmentTarget (ic, result, size);
  if (!target)
    return true;

  if (count >= (unsigned long long)size * 8ull)
    {
      if (!is_signed_right)
        {
          if (!clearTargetBytes (target, size))
            return false;
        }
      else
        {
          if (!loadOperandByteToA (left, size - 1))
            return false;
          emitSignMaskForA ();
          if (!fillTargetBytesWithA (target, size))
            return false;
        }

      return finishWideAssignment (ic, result, target, size);
    }

  byte_count = (unsigned)(count / 8u);
  count %= 8u;

  if (!copyOperandToTarget (left, target, size))
    return false;

  if (byte_count &&
      !(is_right ? shiftTargetRightBytes (target, size, byte_count, is_signed_right) :
                    shiftTargetLeftBytes (target, size, byte_count)))
    return false;

  if (count && !shiftTargetByBits (target, size, is_right, is_signed_right, (unsigned)count))
    return false;

  if (!maskTargetTopByte (target, size, top_byte_mask))
    return false;

  return finishWideAssignment (ic, result, target, size);
}

static bool
genWideVariableShift (const iCode *ic, const bool is_right, const bool is_signed_right)
{
  operand *result = IC_RESULT (ic);
  operand *left = IC_LEFT (ic);
  operand *right = IC_RIGHT (ic);
  operand *target;
  const unsigned top_byte_mask = unsignedBitIntTopByteMask (result);
  char loop_label[32];
  char done_label[32];
  int size = getSize (operandType (result));

  if (size < 1 || size > K78K0_MAX_SCALAR_BYTES || getSize (operandType (left)) != size || getSize (operandType (right)) < 1)
    return false;

  target = wideAssignmentTarget (ic, result, size);
  if (!target)
    return true;
  if (!copyOperandToTarget (left, target, size))
    return false;

  if (!loadOperandByteToA (right, 0))
    return false;
  emit2 ("mov", "b,a");

  makeLocalLabel (loop_label, sizeof (loop_label));
  makeLocalLabel (done_label, sizeof (done_label));

  emit2 ("cmp", "a,#0x00");
  emitCondBranch ("bz", done_label);

  emitLocalLabel (loop_label);

  if (is_right)
    {
      if (!shiftTargetRightOne (target, size, is_signed_right))
        return false;
    }
  else if (!shiftTargetLeftOne (target, size))
    return false;

  emit2 ("dbnz", "b,%s", loop_label);
  emitLocalLabel (done_label);

  if (!maskTargetTopByte (target, size, top_byte_mask))
    return false;
  return finishWideAssignment (ic, result, target, size);
}

static bool
genShift (const iCode *ic)
{
  operand *result = IC_RESULT (ic);
  operand *left = IC_LEFT (ic);
  operand *right = IC_RIGHT (ic);
  const bool is_right = ic->op == RIGHT_OP;
  bool is_signed_right;
  unsigned long long count;
  unsigned top_byte_mask;
  int size;

  if (!IS_ITEMP (result) || !left || !right)
    return false;

  is_signed_right = is_right && !SPEC_USIGN (getSpec (operandType (left)));

  size = getSize (operandType (result));
  if (size < 1 || size > K78K0_MAX_SCALAR_BYTES || getSize (operandType (left)) != size)
    return false;

  top_byte_mask = unsignedBitIntTopByteMask (result);

  if (!IS_OP_LITERAL (right))
    {
      if (size > 2)
        return genWideVariableShift (ic, is_right, is_signed_right);

      if (getSize (operandType (right)) < 1)
        return false;

      if (operandNeedsStackHL (left, size) || operandNeedsStackHL (right, 1))
        setHLToSP ();

      if (!loadOperandByteToA (right, 0))
        return false;
      emit2 ("mov", "c,a");

      if (size == 1)
        {
          if (!loadOperandByteToA (left, 0))
            return false;
        }
      else if (!genOperandReturnValue (left))
        return false;

      emitVariableShiftLoop (size, is_right, is_signed_right);

      if (top_byte_mask != 0xffu)
        emit2 ("and", "a,#0x%02x", top_byte_mask);

      if (size == 1)
        setAResult (result);
      else
        setReturnResult (result, size);

      return true;
    }

  count = operandLitValueUll (right);
  if (size > 2)
    return genWideLiteralShift (ic, is_right, is_signed_right, count);

  if (count >= (unsigned)(size * 8))
    {
      clearAResult ();
      if (is_signed_right)
        {
          if (!loadOperandByteToA (left, size - 1))
            return false;
          emitSignMaskForA ();
          if (size == 2)
            emit2 ("mov", "x,a");
        }
      else if (size == 1)
        emit2 ("mov", "a,#0x00");
      else
        emit2 ("movw", "ax,#0x0000");
      setReturnResult (result, size);
      return true;
    }

  if (size == 1)
    {
      if (!loadOperandByteToA (left, 0))
        return false;

      while (count--)
        {
          if (is_signed_right)
            emitByteArithmeticRightShift ();
          else if (is_right)
            emitByteRightShift ();
          else
            emitByteLeftShift ();
        }

      if (top_byte_mask != 0xffu)
        emit2 ("and", "a,#0x%02x", top_byte_mask);

      setAResult (result);
      return true;
    }

  if (!genOperandReturnValue (left))
    return false;

  while (count--)
    {
      if (is_signed_right)
        emitWordArithmeticRightShift ();
      else if (is_right)
        emitWordRightShift ();
      else
        emitWordLeftShift ();
    }

  if (top_byte_mask != 0xffu)
    emit2 ("and", "a,#0x%02x", top_byte_mask);

  setReturnResult (result, size);
  return true;
}

static bool
genGetByte (const iCode *ic)
{
  operand *result = IC_RESULT (ic);
  operand *left = IC_LEFT (ic);
  operand *right = IC_RIGHT (ic);
  unsigned long long bit_offset;

  if (!IS_ITEMP (result) || !left || !IS_OP_LITERAL (right) || k78k0_operandSize (result) != 1)
    return false;

  bit_offset = operandLitValueUll (right);
  if (bit_offset % 8ull)
    return false;

  if (!loadOperandByteToA (left, (int)(bit_offset / 8ull)))
    return false;

  setAResult (result);
  return true;
}

static bool
genGetWord (const iCode *ic)
{
  operand *result = IC_RESULT (ic);
  operand *left = IC_LEFT (ic);
  operand *right = IC_RIGHT (ic);
  unsigned long long bit_offset;
  int byte_offset;

  if (!IS_ITEMP (result) || !left || !IS_OP_LITERAL (right) || k78k0_operandSize (result) != 2)
    return false;

  bit_offset = operandLitValueUll (right);
  if (bit_offset % 8ull)
    return false;

  byte_offset = (int)(bit_offset / 8ull);

  if (!loadOperandByteToA (left, byte_offset))
    return false;
  emit2 ("mov", "c,a");

  if (!loadOperandByteToA (left, byte_offset + 1))
    return false;
  emit2 ("mov", "x,a");
  emit2 ("mov", "a,c");

  setReturnResult (result, 2);
  return true;
}

static bool
genGetABit (const iCode *ic)
{
  operand *result = IC_RESULT (ic);
  operand *left = IC_LEFT (ic);
  operand *right = IC_RIGHT (ic);
  unsigned long long bit_offset;

  if (!IS_ITEMP (result) || !left || !IS_OP_LITERAL (right) || k78k0_operandSize (result) != 1)
    return false;

  bit_offset = operandLitValueUll (right);
  if (bit_offset >= (unsigned long long)k78k0_operandSize (left) * 8ull)
    return false;

  if (!loadOperandByteToA (left, (int)(bit_offset / 8ull)))
    return false;

  emit2 ("and", "a,#0x%02x", 1u << (unsigned)(bit_offset % 8ull));
  materializeZeroTest (result, false);
  return true;
}

static bool
genDummyReadVolatile (const iCode *ic)
{
  operand *right = IC_RIGHT (ic) ? IC_RIGHT (ic) : (IC_LEFT (ic) ? IC_LEFT (ic) : IC_RESULT (ic));
  int size;

  if (!right)
    return false;

  size = k78k0_operandSize (right);
  if (size < 1 || size > K78K0_MAX_SCALAR_BYTES)
    return false;

  for (int offset = 0; offset < size; offset++)
    if (!loadOperandByteToA (right, offset))
      return false;

  clearAResult ();
  return true;
}

static bool
genJumpTable (const iCode *ic)
{
  operand *cond = IC_JTCOND (ic);
  symbol *label;
  char table_label[32];
  char target_label[32];
  int size;

  if (!cond)
    return false;

  size = getSize (operandType (cond));
  if (size < 1 || size > K78K0_MAX_SCALAR_BYTES)
    return false;

  if (size == 1)
    {
      if (!loadOperandByteToA (cond, 0))
        return false;

      emit2 ("mov", "x,a");
      emit2 ("mov", "a,#0x00");
    }
  else if (!genOperandReturnValue (cond))
    return false;

  makeLocalLabel (table_label, sizeof (table_label));
  emitWordLeftShift ();
  emit2 ("addw", "ax,#%s", table_label);
  emit2 ("movw", "hl,ax");
  clearHLState ();
  emit2 ("mov", "a,[hl+0x00]");
  emit2 ("mov", "x,a");
  emit2 ("mov", "a,[hl+0x01]");
  emit2 ("br", "ax");

  emitLocalLabel (table_label);
  for (label = setFirstItem (IC_JTLABELS (ic)); label; label = setNextItem (IC_JTLABELS (ic)))
    {
      makeICLabel (target_label, sizeof (target_label), label);
      emit2 (".dw", "%s", target_label);
    }

  clearRegisterState ();
  return true;
}

static bool
genIpush (const iCode *ic)
{
  operand *left = IC_LEFT (ic);
  int size;

  if (!left)
    return false;

  size = getSize (operandType (left));
  if (size < 1 || size > K78K0_MAX_SCALAR_BYTES)
    return false;

  if (size == 1)
    {
      adjustStackPointer (-1, true);
      if (!loadOperandByteToA (left, 0))
        return false;
      ensureHLToSPPreservingA ("c");
      emit2 ("mov", "[hl+0x00],a");
      clearAResult ();
      return true;
    }

  if (size == 2)
    {
      if (!genOperandReturnValue (left))
        return false;

      emit2 ("push", "ax");
      stack_pushed += 2;
      clearRegisterState ();
      return true;
    }

  adjustStackPointer (-size, true);
  for (int offset = 0; offset < size; offset++)
    {
      if (!loadOperandByteToA (left, offset))
        return false;
      ensureHLToSPPreservingA ("c");
      emit2 ("mov", "[hl+0x%02x],a", (unsigned)offset);
    }

  clearAResult ();
  return true;
}

static bool
genPointerIpush (const iCode *ic)
{
  operand *left = IC_LEFT (ic);
  sym_link *type;
  unsigned long long pointer_offset;
  int size;

  if (!left || !IC_RIGHT (ic) || !IS_OP_LITERAL (IC_RIGHT (ic)))
    return false;

  type = operandType (left);
  if (!type || !type->next)
    return false;

  size = getSize (type->next);
  pointer_offset = operandLitValueUll (IC_RIGHT (ic));
  if (size < 1 || pointer_offset + (unsigned long long)size > 256u || size > 256)
    return false;

  if (!loadPointerToHL (left))
    return false;

  emit2 ("movw", "de,ax");
  adjustStackPointer (-size, true);

  for (int offset = 0; offset < size; offset++)
    {
      emit2 ("movw", "ax,de");
      if (pointer_offset + (unsigned long long)offset)
        adjustAX ((int)(pointer_offset + (unsigned long long)offset));
      emit2 ("movw", "hl,ax");
      hl_is_sp = false;
      hl_sp_offset_valid = false;
      emit2 ("mov", "a,[hl+0x00]");
      ensureHLToSPPreservingA ("c");
      emit2 ("mov", "[hl+0x%02x],a", (unsigned)offset);
    }

  clearAResult ();
  return true;
}

static bool
loadFirstArgRegisters (const operand *left)
{
  int size;

  left = resolveReqvOperand (left);
  size = k78k0_operandSize (left);

  if (size < 1 || size > 4)
    return false;

  if (size == 1)
    return loadOperandByteToA (left, 0);
  return genOperandReturnValue (left);
}

static bool
storeFirstArgRegisters (const operand *result)
{
  const int size = k78k0_operandSize (result);
  const symbol *storage;
  int stack_offset;

  if (size < 1 || size > 4 || !IS_SYMOP (result))
    return false;

  storage = operandStorageSymbol (result);
  if (storage->onStack)
    {
      stack_offset = stackByteOffset (storage, 0);
      if (size == 1)
        emit2 ("mov", "c,a");
      else
        emit2 ("movw", "de,ax");
      setHLToStackOffset (stack_offset);
      if (size == 1)
        {
          emit2 ("mov", "a,c");
          emit2 ("mov", "[hl+0x00],a");
        }
      else
        {
          emit2 ("mov", "a,e");
          emit2 ("mov", "[hl+0x00],a");
          emit2 ("mov", "a,d");
          emit2 ("mov", "[hl+0x01],a");
          if (size >= 3)
            {
              emit2 ("mov", "a,c");
              emit2 ("mov", "[hl+0x02],a");
            }
          if (size == 4)
            {
              emit2 ("mov", "a,b");
              emit2 ("mov", "[hl+0x03],a");
            }
        }
      clearRegisterState ();
      return true;
    }

  if (size == 1)
    return storeAToOperandByte (result, 0);

  if (!storeAToOperandByte (result, 1))
    return false;
  emit2 ("mov", "a,x");
  if (!storeAToOperandByte (result, 0))
    return false;

  if (size >= 3)
    {
      emit2 ("mov", "a,c");
      if (!storeAToOperandByte (result, 2))
        return false;
    }
  if (size >= 4)
    {
      emit2 ("mov", "a,b");
      if (!storeAToOperandByte (result, 3))
        return false;
    }

  clearRegisterState ();
  return true;
}

static bool
genSend (const iCode *ic)
{
  const operand *left;
  int size;

  if (ic->argreg != 1)
    return true;

  if (!IC_LEFT (ic))
    return false;

  left = resolveReqvOperand (IC_LEFT (ic));
  size = k78k0_operandSize (left);
  if (size > 4)
    return true;

  return loadFirstArgRegisters (left);
}

static bool
genReceive (const iCode *ic)
{
  const operand *result;
  int size;

  if (ic->argreg != 1)
    return true;

  if (!IC_RESULT (ic))
    return false;

  result = resolveReqvOperand (IC_RESULT (ic));
  size = k78k0_operandSize (result);
  if (size > 4)
    return true;

  return storeFirstArgRegisters (result);
}

static bool
copyReturnToHiddenPointer (const operand *left)
{
  const int size = getSize (operandType (left));
  const int pointer_offset = local_stack_size + K78K0_RETURN_ADDRESS_BYTES + stack_pushed;
  const operand *source = resolveReqvOperand (left);
  const symbol *storage = IS_SYMOP (source) ? operandStorageSymbol (source) : NULL;
  char copy_label[32];

  if (size < 1 || pointer_offset < 0)
    return false;

  if (size <= 256 && storage && !operandInReturnValue (source, size) &&
      (storage->onStack || storage->rname[0]))
    {
      if (storage->onStack)
        {
          emit2 ("movw", "ax,sp");
          adjustAX (stackByteOffset (storage, 0));
        }
      else
        emit2 ("movw", "ax,#%s", storage->rname);
      emit2 ("movw", "de,ax");

      setHLToStackOffset (pointer_offset);
      emit2 ("mov", "a,[hl+0x00]");
      emit2 ("mov", "x,a");
      emit2 ("mov", "a,[hl+0x01]");
      emit2 ("movw", "hl,ax");

      emit2 ("mov", "a,#0x%02x", (unsigned)(size & 0xff));
      emit2 ("mov", "b,a");
      makeLocalLabel (copy_label, sizeof (copy_label));
      emitLocalLabel (copy_label);
      emit2 ("mov", "a,[de]");
      emit2 ("mov", "[hl],a");
      emit2 ("incw", "de");
      emit2 ("incw", "hl");
      emit2 ("dbnz", "b,%s", copy_label);

      clearRegisterState ();
      return true;
    }

  setHLToStackOffset (pointer_offset);
  emit2 ("mov", "a,[hl+0x00]");
  emit2 ("mov", "x,a");
  emit2 ("mov", "a,[hl+0x01]");
  emit2 ("movw", "de,ax");

  for (int offset = 0; offset < size; offset++)
    {
      if (!loadOperandByteToA (left, offset))
        return false;

      emit2 ("mov", "b,a");
      if (offset <= 255)
        {
          setHLFromDE ();
          emit2 ("mov", "a,b");
          emit2 ("mov", "[hl+0x%02x],a", (unsigned)offset);
        }
      else
        {
          emit2 ("movw", "ax,de");
          adjustAX (offset);
          emit2 ("movw", "hl,ax");
          emit2 ("mov", "a,b");
          emit2 ("mov", "[hl],a");
        }
      clearHLState ();
    }

  clearAResult ();
  return true;
}

static void
genReturn (const iCode *ic)
{
  char label[32];

  if (IC_LEFT (ic) && (current_return_via_hidden_pointer || current_return_size > 0))
    {
      if (current_return_via_hidden_pointer)
        {
          if (!copyReturnToHiddenPointer (IC_LEFT (ic)))
            {
              wassertl (0, "78K0 large return operand is not implemented yet.");
              return;
            }
        }
      else if (!genOperandReturnValue (IC_LEFT (ic)))
        {
          wassertl (0, "78K0 return operand is not implemented yet.");
          return;
        }
    }

  if (!ic->next || ic->next->op != LABEL || IC_LABEL (ic->next) != returnLabel)
    {
      makeICLabel (label, sizeof (label), returnLabel);
      emit2 ("br", "!%s", label);
    }
}

static void
gen78K0iCode (iCode *ic)
{
  genLine.lineElement.ic = ic;

  if (ic->generated)
    return;

  switch (ic->op)
    {
    case FUNCTION:
      genFunction (ic);
      break;

    case ENDFUNCTION:
      genEndFunction (ic);
      break;

    case RETURN:
      genReturn (ic);
      break;

    case LABEL:
      genLabel (ic);
      break;

    case GOTO:
      genGoto (ic);
      break;

    case ADDRESS_OF:
      if (!genAddrOf (ic))
        wassertl (0, "78K0 address-of is not implemented yet.");
      break;

    case GET_VALUE_AT_ADDRESS:
      if (!genPointerGet (ic))
        wassertl (0, "78K0 pointer read is not implemented yet.");
      break;

    case SET_VALUE_AT_ADDRESS:
      if (!genPointerSet (ic))
        wassertl (0, "78K0 pointer write is not implemented yet.");
      break;

    case CAST:
      if (!genCast (ic))
        wassertl (0, "78K0 cast is not implemented yet.");
      break;

    case '+':
      if (!genPlus (ic))
        wassertl (0, "78K0 addition is not implemented yet.");
      break;

    case '-':
      if (!genMinus (ic))
        wassertl (0, "78K0 subtraction is not implemented yet.");
      break;

    case '*':
      if (!genMult (ic))
        wassertl (0, "78K0 multiplication is not implemented yet.");
      break;

    case '/':
    case '%':
      if (!genDivMod (ic))
        wassertl (0, "78K0 division/modulo is not implemented yet.");
      break;

    case BITWISEAND:
      if (!genBitwiseAnd (ic))
        wassertl (0, "78K0 bitwise and is not implemented yet.");
      break;

    case '|':
      if (!genOr (ic))
        wassertl (0, "78K0 bitwise or is not implemented yet.");
      break;

    case '^':
      if (!genXor (ic))
        wassertl (0, "78K0 bitwise xor is not implemented yet.");
      break;

    case UNARYMINUS:
      if (!genUnaryMinus (ic))
        wassertl (0, "78K0 unary minus is not implemented yet.");
      break;

    case '!':
      if (!genNot (ic))
        wassertl (0, "78K0 logical not is not implemented yet.");
      break;

    case LEFT_OP:
    case RIGHT_OP:
      if (!genShift (ic))
        wassertl (0, "78K0 shift is not implemented yet.");
      break;

    case GETBYTE:
      if (!genGetByte (ic))
        wassertl (0, "78K0 get-byte is not implemented yet.");
      break;

    case GETWORD:
      if (!genGetWord (ic))
        wassertl (0, "78K0 get-word is not implemented yet.");
      break;

    case GETABIT:
      if (!genGetABit (ic))
        wassertl (0, "78K0 get-bit is not implemented yet.");
      break;

    case EQ_OP:
    case NE_OP:
      if (!genCmpEqNe (ic))
        wassertl (0, "78K0 equality comparison is not implemented yet.");
      break;

    case '<':
    case '>':
      if (!genCmpLtGt (ic))
        wassertl (0, "78K0 ordering comparison is not implemented yet.");
      break;

    case IPUSH:
      if (!genIpush (ic))
        wassertl (0, "78K0 parameter push is not implemented yet.");
      break;

    case IPUSH_VALUE_AT_ADDRESS:
      if (!genPointerIpush (ic))
        wassertl (0, "78K0 indirect parameter push is not implemented yet.");
      break;

    case SEND:
      if (!genSend (ic))
        wassertl (0, "78K0 register parameter send is not implemented yet.");
      break;

    case RECEIVE:
      if (!genReceive (ic))
        wassertl (0, "78K0 register parameter receive is not implemented yet.");
      break;

    case CALL:
      if (!genCall (ic))
        wassertl (0, "78K0 call is not implemented yet.");
      break;

    case PCALL:
      if (!genPcall (ic))
        wassertl (0, "78K0 indirect call is not implemented yet.");
      break;

    case IFX:
      if (!genIfx (ic))
        wassertl (0, "78K0 conditional branch is not implemented yet.");
      break;

    case JUMPTABLE:
      if (!genJumpTable (ic))
        wassertl (0, "78K0 jump table is not implemented yet.");
      break;

    case '=':
      if (POINTER_SET (ic) ? !genPointerSet (ic) : !genAssign (ic))
        wassertl (0, "78K0 assignment is not implemented yet.");
      break;

    case INLINEASM:
      genInlineAsm (ic);
      break;

    case DUMMY_READ_VOLATILE:
      if (!genDummyReadVolatile (ic))
        wassertl (0, "78K0 volatile dummy read is not implemented yet.");
      break;

    case CRITICAL:
      genCritical ();
      break;

    case ENDCRITICAL:
      genEndCritical ();
      break;

    default:
      wassertl (0, "78K0 iCode operation is not implemented yet.");
    }
}

void
gen78K0Code (ebbIndex *ebbi)
{
  int clevel = 0;
  int cblock = 0;
  int cln = 0;

  if (options.debug && currFunc)
    debugFile->writeFrameAddress (NULL, NULL, 0);

  for (int i = 0; i < ebbi->count; i++)
    {
      eBBlock *ebb = ebbi->bbOrder[i];

      for (iCode *ic = ebb->sch; ic; ic = ic->next)
        {
          initGenLineElement ();
          genLine.lineElement.ic = ic;

          if (ic->level != clevel || ic->block != cblock)
            {
              if (options.debug)
                debugFile->writeScope (ic);
              clevel = ic->level;
              cblock = ic->block;
            }

          if (ic->lineno && cln != ic->lineno)
            {
              if (options.debug)
                debugFile->writeCLine (ic);

              if (!options.noCcodeInAsm)
                emit2 (";", "%s: %d: %s", ic->filename, ic->lineno, printCLine (ic->filename, ic->lineno));
              cln = ic->lineno;
            }

          if (options.iCodeInAsm)
            {
              const char *iLine = printILine (ic);
              emit2 ("; ic:", "%d: %s", ic->key, iLine);
              dbuf_free (iLine);
            }

          gen78K0iCode (ic);
        }
    }

  if (options.debug)
    debugFile->writeFrameAddress (NULL, NULL, 0);

  if (!options.nopeep)
    peepHole (&genLine.lineHead);

  printLine (genLine.lineHead, codeOutBuf);
  destroy_line_list ();
}
