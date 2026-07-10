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
static int stack_pushed = 0;
static int local_stack_size = 0;
static int current_return_size = 0;
static bool current_return_is_struct = false;
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

typedef struct
{
  const symbol *sym;
  int size;
  bool valid;
}
wideReturnState;

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
}

static wideReturnState
saveWideReturnState (void)
{
  wideReturnState state;

  state.sym = return_result_sym;
  state.size = return_result_size;
  state.valid = return_result_sym && return_result_size > 2;
  return state;
}

static void
restoreWideReturnState (const wideReturnState state)
{
  if (!state.valid)
    return;

  a_result_sym = NULL;
  return_result_sym = state.sym;
  return_result_size = state.size;
  return_result_ax_valid = false;
}

static void
restoreWideReturnStateIfUnrelated (const wideReturnState state, const operand *target)
{
  if (state.valid && (!IS_SYMOP (target) || OP_SYMBOL_CONST (target) != state.sym))
    restoreWideReturnState (state);
}

static void
clearRegisterStatePreservingWideReturn (void)
{
  const wideReturnState state = saveWideReturnState ();

  clearAResult ();
  restoreWideReturnState (state);
}

static bool
storeAccumulatorToStack (const symbol *sym, int size);

static bool
storeReturnMirrorToStack (const symbol *sym, int size);

static bool
symbolIsSfr (const symbol *sym)
{
  return sym && sym->etype && SPEC_SCLS (sym->etype) == S_SFR;
}

static bool
setWideReturnResultFromMirror (const operand *result, int size);

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
  clearAResult ();
  clearHLState ();
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
  clearRegisterStatePreservingWideReturn ();
  emit2 ("movw", "ax,sp");
  emit2 ("movw", "hl,ax");
  hl_is_sp = true;
}

static void
ensureHLToSP (void)
{
  if (!hl_is_sp)
    setHLToSP ();
  else
    clearRegisterStatePreservingWideReturn ();
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
  clearRegisterStatePreservingWideReturn ();
  clearHLState ();

  emit2 ("movw", "ax,sp");
  if (stack_offset > 0)
    emit2 ("addw", "ax,#0x%04x", (unsigned)stack_offset);
  else if (stack_offset < 0)
    emit2 ("subw", "ax,#0x%04x", (unsigned)(-stack_offset));
  emit2 ("movw", "hl,ax");
  hl_is_sp = stack_offset == 0;
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
typeReturnsStruct (sym_link *type)
{
  sym_link *ftype;

  if (!type)
    return false;

  ftype = IS_FUNCPTR (type) ? type->next : type;
  return ftype && ftype->next && IS_STRUCT (ftype->next);
}

static void
adjustStackPointer (const int amount, const bool leave_hl_sp)
{
  if (!amount)
    return;

  clearAResult ();
  clearHLState ();

  emit2 ("movw", "ax,sp");
  if (amount > 0)
    emit2 ("addw", "ax,#0x%04x", (unsigned)amount);
  else
    emit2 ("subw", "ax,#0x%04x", (unsigned)(-amount));
  emit2 ("movw", "sp,ax");

  stack_pushed -= amount;
  wassertl (stack_pushed >= 0, "78K0 outgoing stack accounting underflow.");

  if (leave_hl_sp)
    {
      emit2 ("movw", "hl,ax");
      hl_is_sp = true;
    }
}

static void
adjustFramePointer (const int amount, const bool leave_hl_sp)
{
  if (!amount)
    return;

  clearAResult ();
  clearHLState ();

  emit2 ("movw", "ax,sp");
  if (amount > 0)
    emit2 ("addw", "ax,#0x%04x", (unsigned)amount);
  else
    emit2 ("subw", "ax,#0x%04x", (unsigned)(-amount));
  emit2 ("movw", "sp,ax");

  if (leave_hl_sp)
    {
      emit2 ("movw", "hl,ax");
      hl_is_sp = true;
    }
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

static void mirrorWideReturnLowBytes (void);
static void mirrorWideCallReturnBytes (int size);
static void loadWideReturnRegistersFromMirror (int size);
static void saveScalarReturnForEpilogue (int size);
static void restoreScalarReturnForEpilogue (int size);

static void
setReturnResult (const operand *op, const int size)
{
  if (IS_ITEMP (op) && size >= 1 && size <= K78K0_MAX_SCALAR_BYTES)
    {
      const symbol *sym = OP_SYMBOL_CONST (op);
      const symbol *storage = operandStorageSymbol (op);

      if (size <= 2 && storage != sym)
        {
          if (storeAccumulatorToStack (storage, size))
            clearAResult ();
          return;
        }

      if (size > 2)
        mirrorWideReturnLowBytes ();
      if (size > 2 && storage != sym)
        {
          if (storeReturnMirrorToStack (storage, size))
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

static const char *
wideReturnByteName (const int offset)
{
  static const char *const names[] = {
    "___SDCC_k78k0_ret0",
    "___SDCC_k78k0_ret1",
    "___SDCC_k78k0_ret2",
    "___SDCC_k78k0_ret3",
    "___SDCC_k78k0_ret4",
    "___SDCC_k78k0_ret5",
    "___SDCC_k78k0_ret6",
    "___SDCC_k78k0_ret7",
  };

  return offset >= 0 && offset < (int)(sizeof (names) / sizeof (names[0])) ? names[offset] : NULL;
}

static void
mirrorWideReturnLowBytes (void)
{
  emit2 ("movw", "de,ax");
  emit2 ("mov", "a,x");
  emit2 ("mov", "!%s,a", wideReturnByteName (0));
  emit2 ("movw", "ax,de");
  emit2 ("mov", "!%s,a", wideReturnByteName (1));
  emit2 ("movw", "ax,de");
}

static void
mirrorWideCallReturnBytes (const int size)
{
  if (size <= 2 || size > K78K0_MAX_SCALAR_BYTES)
    return;

  emit2 ("movw", "de,ax");
  emit2 ("mov", "a,x");
  emit2 ("mov", "!%s,a", wideReturnByteName (0));
  emit2 ("movw", "ax,de");
  emit2 ("mov", "!%s,a", wideReturnByteName (1));
  if (size >= 3)
    {
      emit2 ("mov", "a,c");
      emit2 ("mov", "!%s,a", wideReturnByteName (2));
    }
  if (size >= 4)
    {
      emit2 ("mov", "a,b");
      emit2 ("mov", "!%s,a", wideReturnByteName (3));
    }
  emit2 ("movw", "ax,de");
}

static void
loadWideReturnRegistersFromMirror (const int size)
{
  if (size < 3 || size > K78K0_MAX_SCALAR_BYTES)
    return;

  emit2 ("mov", "!%s,a", wideReturnByteName (1));
  emit2 ("mov", "a,x");
  emit2 ("mov", "!%s,a", wideReturnByteName (0));
  emit2 ("mov", "a,!%s", wideReturnByteName (2));
  emit2 ("mov", "c,a");
  if (size >= 4)
    {
      emit2 ("mov", "a,!%s", wideReturnByteName (3));
      emit2 ("mov", "b,a");
    }
  emit2 ("mov", "a,!%s", wideReturnByteName (0));
  emit2 ("mov", "x,a");
  emit2 ("mov", "a,!%s", wideReturnByteName (1));
}

static void
saveScalarReturnForEpilogue (const int size)
{
  if (size < 1 || size > K78K0_MAX_SCALAR_BYTES)
    return;

  if (size == 1)
    {
      emit2 ("mov", "!%s,a", wideReturnByteName (0));
      return;
    }

  emit2 ("mov", "!%s,a", wideReturnByteName (1));
  emit2 ("mov", "a,x");
  emit2 ("mov", "!%s,a", wideReturnByteName (0));

  if (size >= 3)
    {
      emit2 ("mov", "a,c");
      emit2 ("mov", "!%s,a", wideReturnByteName (2));
    }
  if (size >= 4)
    {
      emit2 ("mov", "a,b");
      emit2 ("mov", "!%s,a", wideReturnByteName (3));
    }
}

static void
restoreScalarReturnForEpilogue (const int size)
{
  if (size < 1 || size > K78K0_MAX_SCALAR_BYTES)
    return;

  if (size >= 4)
    {
      emit2 ("mov", "a,!%s", wideReturnByteName (3));
      emit2 ("mov", "b,a");
    }
  if (size >= 3)
    {
      emit2 ("mov", "a,!%s", wideReturnByteName (2));
      emit2 ("mov", "c,a");
    }

  if (size == 1)
    emit2 ("mov", "a,!%s", wideReturnByteName (0));
  else
    {
      emit2 ("mov", "a,!%s", wideReturnByteName (0));
      emit2 ("mov", "x,a");
      emit2 ("mov", "a,!%s", wideReturnByteName (1));
    }
}

static bool
restoreReturnValueAXFromMirror (const int size)
{
  if (size <= 2 || size > K78K0_MAX_SCALAR_BYTES)
    return false;

  emit2 ("mov", "a,!%s", wideReturnByteName (0));
  emit2 ("mov", "x,a");
  emit2 ("mov", "a,!%s", wideReturnByteName (1));
  return_result_ax_valid = true;
  a_result_sym = NULL;
  return true;
}

static int
functionStackCleanupBytes (sym_link *ftype)
{
  int bytes = 0;

  if (!ftype || !IS_FUNC (ftype) || FUNC_HASVARARGS (ftype))
    return 0;

  if (typeReturnsStruct (ftype))
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
  setHLToSP ();
  emit2 ("mov", "a,[hl+0x01]");
  emit2 ("mov", "c,a");
  setHLToStackOffset (cleanup_bytes + 1);
  emit2 ("mov", "a,c");
  emit2 ("mov", "[hl+0x00],a");

  setHLToSP ();
  emit2 ("mov", "a,[hl+0x00]");
  emit2 ("mov", "c,a");
  setHLToStackOffset (cleanup_bytes);
  emit2 ("mov", "a,c");
  emit2 ("mov", "[hl+0x00],a");

  adjustFramePointer (cleanup_bytes, false);
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

  clearAResult ();
  clearHLState ();
  stack_pushed = 0;
  local_stack_size = (sym->stack > 0 ? sym->stack : 0) + K78K0_PRESERVED_DE_BYTES;
  current_return_size = sym->type && sym->type->next && !IS_VOID (sym->type->next) ?
    getSize (sym->type->next) : 0;
  current_return_is_struct = typeReturnsStruct (sym->type);
  current_param_offset = current_return_is_struct ? 2 : 0;
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

  {
    const int first_regarg_size = functionFirstRegArgSize (sym->type);

    saveScalarReturnForEpilogue (first_regarg_size);

    adjustFramePointer (-local_stack_size, true);

    emit2 ("movw", "ax,de");
    emit2 ("mov", "[hl+0x01],a");
    emit2 ("mov", "a,x");
    emit2 ("mov", "[hl+0x00],a");

    restoreScalarReturnForEpilogue (first_regarg_size);

    if (IFFUNC_ISCRITICAL (sym->type))
      {
        saveScalarReturnForEpilogue (first_regarg_size);
        genCritical ();
        setHLToSP ();
        restoreScalarReturnForEpilogue (first_regarg_size);
      }
  }
}

static void
genEndFunction (const iCode *ic)
{
  const symbol *sym = OP_SYMBOL (IC_LEFT (ic));

  if (IFFUNC_ISNAKED (sym->type))
    {
      local_stack_size = 0;
      current_return_size = 0;
      current_return_is_struct = false;
      current_param_offset = 0;
      current_stack_cleanup_size = 0;
      current_function_is_isr = false;
      clearAResult ();
      clearHLState ();
      emit2 (";", "naked function: no epilogue.");
      return;
    }

  if (IFFUNC_ISCRITICAL (sym->type))
    {
      if (!current_return_is_struct && current_return_size > 0 && current_return_size <= K78K0_MAX_SCALAR_BYTES)
        saveScalarReturnForEpilogue (current_return_size);

      genEndCritical ();

      if (!current_return_is_struct && current_return_size > 0 && current_return_size <= K78K0_MAX_SCALAR_BYTES)
        restoreScalarReturnForEpilogue (current_return_size);
    }

  if (local_stack_size)
    {
      if (!current_return_is_struct && current_return_size > 0 && current_return_size <= K78K0_MAX_SCALAR_BYTES)
        saveScalarReturnForEpilogue (current_return_size);

      setHLToSP ();
      emit2 ("mov", "a,[hl+0x00]");
      emit2 ("mov", "x,a");
      emit2 ("mov", "a,[hl+0x01]");
      emit2 ("movw", "de,ax");

      adjustFramePointer (local_stack_size, false);

      if (!current_return_is_struct && current_return_size > 0 && current_return_size <= K78K0_MAX_SCALAR_BYTES)
        restoreScalarReturnForEpilogue (current_return_size);
    }

  if (current_stack_cleanup_size)
    {
      if (!current_return_is_struct && current_return_size > 0 && current_return_size <= K78K0_MAX_SCALAR_BYTES)
        saveScalarReturnForEpilogue (current_return_size);

      moveReturnAddressForCalleeCleanup (current_stack_cleanup_size);

      if (!current_return_is_struct && current_return_size > 0 && current_return_size <= K78K0_MAX_SCALAR_BYTES)
        restoreScalarReturnForEpilogue (current_return_size);
    }

  wassertl (stack_pushed == 0, "78K0 unbalanced outgoing stack.");
  local_stack_size = 0;
  current_return_size = 0;
  current_return_is_struct = false;
  current_param_offset = 0;
  current_stack_cleanup_size = 0;
  clearAResult ();
  clearHLState ();
  if (current_function_is_isr)
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
  current_function_is_isr = false;
}

static void
genLabel (const iCode *ic)
{
  char label[32];

  clearRegisterStatePreservingWideReturn ();
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
  clearRegisterStatePreservingWideReturn ();
  clearHLState ();
}

static void
genInlineAsm (iCode *ic)
{
  genInline (ic);
  clearAResult ();
  clearHLState ();
}

static void
genCritical (void)
{
  clearAResult ();
  clearHLState ();
  emit2 ("push", "psw");
  stack_pushed += 1;
  emit2 ("di", "");
}

static void
genEndCritical (void)
{
  clearAResult ();
  clearHLState ();
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
  else if (size <= K78K0_MAX_SCALAR_BYTES)
    {
      clearAResult ();
      for (int offset = size - 1; offset >= 2; offset--)
        {
          emit2 ("mov", "a,#0x%02x", (unsigned)((value >> (offset * 8)) & 0xffu));
          emit2 ("mov", "!%s,a", wideReturnByteName (offset));
        }
      emit2 ("movw", "ax,#0x%04x", (unsigned)(value & 0xffffu));
    }
  else
    wassertl (0, "78K0 literal return is wider than the supported scalar return size.");
}

static bool
loadStackByteToA (const symbol *sym, const int offset)
{
  const int stack_offset = stackByteOffset (sym, offset);

  if (!sym->onStack || stack_offset < 0)
    return false;

  if (stack_offset <= 255)
    {
      ensureHLToSP ();
      emit2 ("mov", "a,[hl+0x%02x]", (unsigned)stack_offset);
    }
  else
    {
      setHLToStackOffset (stack_offset);
      emit2 ("mov", "a,[hl+0x00]");
    }

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
          if (!loadStackByteToA (sym, 1))
            return false;
          emit2 ("mov", "b,a");
          if (!loadStackByteToA (sym, 0))
            return false;
          emit2 ("mov", "x,a");
          emit2 ("mov", "a,b");
        }
    }
  else if (size <= K78K0_MAX_SCALAR_BYTES)
    {
      for (int byte_offset = size - 1; byte_offset >= 2; byte_offset--)
        {
          if (!loadStackByteToA (sym, byte_offset))
            return false;
          emit2 ("mov", "!%s,a", wideReturnByteName (byte_offset));
        }
      if (!loadStackByteToA (sym, 1))
        return false;
      emit2 ("mov", "b,a");
      if (!loadStackByteToA (sym, 0))
        return false;
      emit2 ("mov", "x,a");
      emit2 ("mov", "a,b");
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
  else if (size <= K78K0_MAX_SCALAR_BYTES)
    {
      for (int offset = size - 1; offset >= 2; offset--)
        {
          emit2 ("mov", "a,!%s + %d", sym->rname, offset);
          emit2 ("mov", "!%s,a", wideReturnByteName (offset));
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
      const char *name = wideReturnByteName (offset);

      if (!name)
        return false;

      if (size == 2 && return_result_ax_valid)
        {
          emit2 ("mov", "!%s,a", wideReturnByteName (1));
          emit2 ("mov", "a,x");
          emit2 ("mov", "!%s,a", wideReturnByteName (0));
          if (offset == 1)
            emit2 ("mov", "a,!%s", wideReturnByteName (1));
          a_result_sym = NULL;
          return_result_ax_valid = false;
          return true;
        }

      a_result_sym = NULL;
      return_result_ax_valid = false;
      emit2 ("mov", "a,!%s", name);
      return true;
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

static bool
operandStackBytesFitIndexedSP (const operand *op, const int size)
{
  op = resolveReqvOperand (op);
  if (!IS_SYMOP (op))
    return true;

  const symbol *sym = operandStorageSymbol (op);
  if (!sym->onStack)
    return true;

  for (int offset = 0; offset < size; offset++)
    {
      const int stack_offset = stackByteOffset (sym, offset);

      if (stack_offset < 0 || stack_offset > 255)
        return false;
    }

  return true;
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

  if (IS_ITEMP (op) && offset < getSize (type) && operandInReturnValue (op, getSize (type)))
    {
      const char *name = wideReturnByteName (offset);

      if (!name)
        return false;

      emit2 ("mov", "c,a");
      emit2 ("mov", "a,!%s", name);
      emit2 ("mov", "b,a");
      emit2 ("mov", "a,c");
      emit2 (mnemonic, "a,b");
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

          if (stack_offset < 0)
            return false;

          if (stack_offset <= 255)
            {
              if (!hl_is_sp)
                return false;
              emit2 (mnemonic, "a,[hl+0x%02x]", (unsigned)stack_offset);
            }
          else
            {
              emit2 ("mov", "c,a");
              setHLToStackOffset (stack_offset);
              emit2 ("mov", "a,c");
              emit2 (mnemonic, "a,[hl+0x00]");
            }
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

  if (!sym->onStack || stack_offset < 0)
    return false;

  if (stack_offset <= 255)
    {
      if (!hl_is_sp)
        {
          emit2 ("mov", "c,a");
          setHLToSP ();
          emit2 ("mov", "a,c");
        }
      emit2 ("mov", "[hl+0x%02x],a", (unsigned)stack_offset);
    }
  else
    {
      emit2 ("mov", "c,a");
      setHLToStackOffset (stack_offset);
      emit2 ("mov", "a,c");
      emit2 ("mov", "[hl+0x00],a");
    }

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
storeAccumulatorToStack (const symbol *sym, const int size)
{
  if (!sym->onStack || size < 1 || size > 2)
    return false;

  if (size == 1)
    {
      emit2 ("mov", "c,a");
      setHLToSP ();
      emit2 ("mov", "a,c");
      return storeAToStackByte (sym, 0);
    }

  emit2 ("movw", "de,ax");
  setHLToSP ();
  emit2 ("movw", "ax,de");
  if (!storeAToStackByte (sym, 1))
    return false;
  emit2 ("movw", "ax,de");
  emit2 ("mov", "a,x");
  if (!storeAToStackByte (sym, 0))
    return false;
  emit2 ("movw", "ax,de");
  return true;
}

static bool
storeReturnMirrorToStack (const symbol *sym, const int size)
{
  if (!sym->onStack || size < 1 || size > K78K0_MAX_SCALAR_BYTES)
    return false;

  if (size <= 2)
    return storeAccumulatorToStack (sym, size);

  emit2 ("movw", "de,ax");
  for (int offset = size - 1; offset >= 2; offset--)
    {
      emit2 ("mov", "a,!%s", wideReturnByteName (offset));
      if (!storeAToStackByte (sym, offset))
        return false;
    }

  emit2 ("movw", "ax,de");
  if (!storeAToStackByte (sym, 1))
    return false;
  emit2 ("movw", "ax,de");
  emit2 ("mov", "a,x");
  if (!storeAToStackByte (sym, 0))
    return false;
  emit2 ("movw", "ax,de");
  return true;
}

static bool
storeReturnRegistersToDirect (const symbol *sym, const int size)
{
  if (!sym->rname[0] || sym->onStack || size < 1 || size > K78K0_MAX_SCALAR_BYTES)
    return false;

  if (size == 1)
    {
      storeAToDirectByte (sym, 0);
      return true;
    }

  emit2 ("movw", "de,ax");
  for (int offset = size - 1; offset >= 2; offset--)
    {
      emit2 ("mov", "a,!%s", wideReturnByteName (offset));
      storeAToDirectByte (sym, offset);
    }
  emit2 ("movw", "ax,de");
  storeAToDirectByte (sym, 1);
  emit2 ("mov", "a,x");
  storeAToDirectByte (sym, 0);
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

  if (size <= K78K0_MAX_SCALAR_BYTES)
    {
      emit2 ("movw", "de,ax");
      for (int offset = size - 1; offset >= 2; offset--)
        {
          emit2 ("mov", "a,!%s", wideReturnByteName (offset));
          storeAToDirectByte (sym, offset);
        }
      emit2 ("movw", "ax,de");
      storeAToDirectByte (sym, 1);
      emit2 ("mov", "a,x");
      storeAToDirectByte (sym, 0);
      emit2 ("movw", "ax,de");
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
      if (!storeAToStackByte (sym, 1))
        return false;
      emit2 ("movw", "ax,de");
      emit2 ("mov", "a,x");
      if (!storeAToStackByte (sym, 0))
        return false;
      emit2 ("movw", "ax,de");
      clearAResult ();
      return true;
    }

  if (size <= K78K0_MAX_SCALAR_BYTES)
    {
      emit2 ("movw", "de,ax");
      for (int offset = size - 1; offset >= 2; offset--)
        {
          emit2 ("mov", "a,!%s", wideReturnByteName (offset));
          if (!storeAToStackByte (sym, offset))
            return false;
        }
      emit2 ("movw", "ax,de");
      if (!storeAToStackByte (sym, 1))
        return false;
      emit2 ("mov", "a,x");
      if (!storeAToStackByte (sym, 0))
        return false;
      emit2 ("movw", "ax,de");
      clearAResult ();
      return true;
    }

  return false;
}

static operand *
wideAssignmentTarget (const iCode *ic, const operand *result, const int size)
{
  iCode *next = ic->next;
  operand *target;
  operand *right;

  if (!next || next->op != '=' || POINTER_SET (next) || !IS_ITEMP (result))
    return NULL;

  target = IC_RESULT (next);
  right = IC_RIGHT (next);
  if (!target || !right || !IS_SYMOP (target) || !IS_ITEMP (right))
    return NULL;

  if (OP_SYMBOL_CONST (right) != OP_SYMBOL_CONST (result))
    return NULL;

  if (OP_SYMBOL_CONST (result)->liveTo > next->seq)
    return NULL;

  if (getSize (operandType (target)) != size)
    return NULL;

  return target;
}

static bool
genAssign (const iCode *ic)
{
  operand *result = IC_RESULT (ic);
  operand *right = IC_RIGHT (ic);
  const wideReturnState saved_wide_return = saveWideReturnState ();
  const symbol *rsym;
  int size;

  if (!IS_SYMOP (result) || !right)
    return false;

  if (IS_ITEMP (result))
    {
      symbol *sym = OP_SYMBOL (result);
      const symbol *storage = operandStorageSymbol (result);

      if (storage != sym)
        {
          rsym = storage;
          size = k78k0_operandSize (result);
          if (size > K78K0_MAX_SCALAR_BYTES)
            return false;

          if (rsym->onStack && storeReturnValueToStack (right, rsym, size))
            {
              restoreWideReturnStateIfUnrelated (saved_wide_return, result);
              return true;
            }

          if (!rsym->onStack && storeReturnValueToDirect (right, rsym, size))
            {
              restoreWideReturnStateIfUnrelated (saved_wide_return, result);
              return true;
            }

          for (int offset = 0; offset < size; offset++)
            {
              if (!loadOperandByteToA (right, offset))
                return false;
              if (rsym->onStack)
                {
                  if (!storeAToStackByte (rsym, offset))
                    return false;
                }
              else
                storeAToDirectByte (rsym, offset);
            }

          restoreWideReturnStateIfUnrelated (saved_wide_return, result);
          return true;
        }

      sym->reqv = right;
      return true;
    }

  rsym = OP_SYMBOL_CONST (result);
  if (!rsym->onStack && !rsym->rname[0])
    return false;

  size = k78k0_operandSize (result);
  if (size > K78K0_MAX_SCALAR_BYTES)
    return false;

  if (rsym->onStack && storeReturnValueToStack (right, rsym, size))
    {
      restoreWideReturnStateIfUnrelated (saved_wide_return, result);
      return true;
    }

  if (!rsym->onStack && storeReturnValueToDirect (right, rsym, size))
    {
      restoreWideReturnStateIfUnrelated (saved_wide_return, result);
      return true;
    }

  for (int offset = 0; offset < size; offset++)
    {
      if (!loadOperandByteToA (right, offset))
        return false;
      if (rsym->onStack)
        {
          if (!storeAToStackByte (rsym, offset))
            return false;
        }
      else
        storeAToDirectByte (rsym, offset);
    }

  restoreWideReturnStateIfUnrelated (saved_wide_return, result);
  return true;
}

static bool
genBooleanCast (const operand *result, const operand *right)
{
  sym_link *right_type = getSpec (operandType (right));
  const int right_size = k78k0_operandSize (right);
  const bool is_float = IS_FLOAT (right_type);
  char true_label[32];
  char done_label[32];

  if (right_size < 1 || right_size > K78K0_MAX_SCALAR_BYTES)
    return false;

  if (operandNeedsStackHL (right, right_size))
    setHLToSP ();

  if (!loadOperandByteToA (right, 0))
    return false;
  emit2 ("mov", "b,a");

  for (int offset = 1; offset < right_size; offset++)
    {
      if (!loadOperandByteToA (right, offset))
        return false;
      if (is_float && offset == right_size - 1)
        emit2 ("and", "a,#0x7f");
      emit2 ("or", "a,b");
      emit2 ("mov", "b,a");
    }

  emit2 ("cmp", "a,#0x00");
  makeLocalLabel (true_label, sizeof (true_label));
  makeLocalLabel (done_label, sizeof (done_label));
  emitCondBranch ("bnz", true_label);
  emit2 ("mov", "a,#0x00");
  emit2 ("br", "!%s", done_label);
  emitLocalLabel (true_label);
  emit2 ("mov", "a,#0x01");
  emitLocalLabel (done_label);
  setAResult (result);
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
      emitCondBranch ("bc", positive_label);
      emit2 ("or", "a,#0x%02x", (~mask) & 0xffu);
      emitLocalLabel (positive_label);
    }
}

static bool
genCast (const iCode *ic)
{
  operand *result = IC_RESULT (ic);
  operand *right = IC_RIGHT (ic);
  sym_link *result_type;
  sym_link *right_type;
  unsigned top_byte_mask;
  int result_size;
  int right_size;

  if (!IS_ITEMP (result) || !right)
    return false;

  result_size = k78k0_operandSize (result);
  right_size = k78k0_operandSize (right);
  result_type = getSpec (operandType (result));
  right_type = getSpec (operandType (right));
  top_byte_mask = bitIntTopByteMask (result);

  if (IS_BOOLEAN (result_type) && !IS_BOOLEAN (right_type))
    return genBooleanCast (result, right);

  if (result_size < right_size && result_size >= 1 &&
      right_size <= K78K0_MAX_SCALAR_BYTES)
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
          if (!loadOperandByteToA (right, 0))
            return false;
          emit2 ("mov", "x,a");
          if (!loadOperandByteToA (right, 1))
            return false;
          if (top_byte_mask != 0xffu)
            normalizeBitIntTopByteInA (result);
          setReturnResult (result, result_size);
          return true;
        }

      for (int offset = 0; offset < result_size; offset++)
        {
          if (!loadOperandByteToA (right, offset))
            return false;
          if (offset == result_size - 1 && top_byte_mask != 0xffu)
            normalizeBitIntTopByteInA (result);
          emit2 ("mov", "!%s,a", wideReturnByteName (offset));
        }

      return setWideReturnResultFromMirror (result, result_size);
    }

  if (result_size == 2 && right_size == 1)
    {
      if (!loadOperandByteToA (right, 0))
        return false;

      emit2 ("mov", "x,a");
      if (SPEC_USIGN (getSpec (operandType (right))))
        emit2 ("mov", "a,#0x00");
      else
        {
          char positive_label[32];
          char done_label[32];

          makeLocalLabel (positive_label, sizeof (positive_label));
          makeLocalLabel (done_label, sizeof (done_label));
          emit2 ("cmp", "a,#0x80");
          emitCondBranch ("bc", positive_label);
          emit2 ("mov", "a,#0xff");
          emit2 ("br", "!%s", done_label);
          emitLocalLabel (positive_label);
          emit2 ("mov", "a,#0x00");
          emitLocalLabel (done_label);
        }

      if (top_byte_mask != 0xffu)
        normalizeBitIntTopByteInA (result);

      setReturnResult (result, result_size);
      return true;
    }

  if (result_size > 2 && result_size <= K78K0_MAX_SCALAR_BYTES &&
      right_size >= 1 && right_size < result_size && right_size <= K78K0_MAX_SCALAR_BYTES)
    {
      int offset;

      for (offset = 0; offset < right_size; offset++)
        {
          if (!loadOperandByteToA (right, offset))
            return false;
          emit2 ("mov", "!%s,a", wideReturnByteName (offset));
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
          emit2 ("mov", "!%s,a", wideReturnByteName (offset));
        }

      return setWideReturnResultFromMirror (result, result_size);
    }

  if (top_byte_mask != 0xffu && result_size == right_size && result_size >= 1 && result_size <= K78K0_MAX_SCALAR_BYTES)
    {
      if (result_size == 1)
        {
          if (!loadOperandByteToA (right, 0))
            return false;
          normalizeBitIntTopByteInA (result);
          setAResult (result);
          return true;
        }

      if (result_size == 2)
        {
          if (!loadOperandByteToA (right, 0))
            return false;
          emit2 ("mov", "x,a");
          if (!loadOperandByteToA (right, 1))
            return false;
          normalizeBitIntTopByteInA (result);
          setReturnResult (result, result_size);
          return true;
        }

      for (int offset = 0; offset < result_size; offset++)
        {
          if (!loadOperandByteToA (right, offset))
            return false;
          if (offset == result_size - 1)
            normalizeBitIntTopByteInA (result);
          emit2 ("mov", "!%s,a", wideReturnByteName (offset));
        }

      return setWideReturnResultFromMirror (result, result_size);
    }

  if (IS_ITEMP (result))
    {
      symbol *sym = OP_SYMBOL (result);
      const symbol *storage = operandStorageSymbol (result);

      if (storage != sym)
        {
          if (result_size < 1 || result_size > K78K0_MAX_SCALAR_BYTES || right_size != result_size)
            return false;

          if (storage->onStack && storeReturnValueToStack (right, storage, result_size))
            return true;

          if (!storage->onStack && storeReturnValueToDirect (right, storage, result_size))
            return true;

          for (int offset = 0; offset < result_size; offset++)
            {
              if (!loadOperandByteToA (right, offset))
                return false;
              if (storage->onStack)
                {
                  if (!storeAToStackByte (storage, offset))
                    return false;
                }
              else
                storeAToDirectByte (storage, offset);
            }

          return true;
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
      if (k78k0_operandSize (left) != size || right_size > size)
        return false;

      if (right_size != size && !IS_OP_LITERAL (right) && !SPEC_USIGN (getSpec (operandType (right))))
        return false;

      if (operandNeedsStackHL (left, size) || operandNeedsStackHL (right, size) || (target && operandNeedsStackHL (target, size)))
        setHLToSP ();

      for (int offset = 0; offset < size; offset++)
        {
          if (!loadOperandByteToA (left, offset))
            return false;

          if (operandByteOnStack (right, offset))
            {
              emit2 ("movw", "de,ax");
              setHLToSP ();
              emit2 ("movw", "ax,de");
            }

          if (!aluOperandByteToA (offset ? high_mnemonic : low_mnemonic, right, offset))
            return false;

          if (offset == size - 1 && top_byte_mask != 0xffu)
            emit2 ("and", "a,#0x%02x", top_byte_mask);

          if (target)
            {
              if (!storeAToOperandByte (target, offset))
                return false;
            }
          else
            emit2 ("mov", "!%s,a", wideReturnByteName (offset));
        }

      if (target)
        {
          ic->next->generated = true;
          clearAResult ();
        }
      else
        {
          if (!setWideReturnResultFromMirror (result, size))
            return false;
        }
      return true;
    }

  if (operandNeedsStackHL (right, size) && !operandNeedsStackHL (left, size))
    setHLToSP ();

  if (!loadOperandByteToA (left, 0))
    return false;

  if (operandByteOnStack (right, 0))
    {
      emit2 ("movw", "de,ax");
      setHLToSP ();
      emit2 ("movw", "ax,de");
    }

  if (!aluOperandByteToA (low_mnemonic, right, 0))
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
        {
          emit2 ("movw", "de,ax");
          setHLToSP ();
          emit2 ("movw", "ax,de");
        }

      if (!aluOperandByteToA (carry_add ? "add" : carry_sub ? "sub" : high_mnemonic, right, 1))
        return false;

      if (carry_add || carry_sub)
        {
          emit2 ("mov", "b,a");
          setHLToSP ();
          emit2 ("mov", "a,b");
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
    {
      emit2 ("movw", "de,ax");
      setHLToSP ();
      emit2 ("movw", "ax,de");
    }

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
  emit2 ("mov", "b,a");
  setHLToSP ();
  emit2 ("mov", "a,b");
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

  if (offset > 0)
    emit2 ("addw", "ax,#0x%04x", (unsigned)offset);
  else if (offset < 0)
    emit2 ("subw", "ax,#0x%04x", (unsigned)(-offset));

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

  if (size > 2)
    {
      target = wideAssignmentTarget (ic, result, size);

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

          if (target)
            {
              if (!storeAToOperandByte (target, offset))
                return false;
            }
          else
            emit2 ("mov", "!%s,a", wideReturnByteName (offset));
        }

      if (target)
        {
          ic->next->generated = true;
          clearAResult ();
        }
      else
        {
          if (!setWideReturnResultFromMirror (result, size))
            return false;
        }
      return true;
    }

  if (!loadOperandByteToA (left, 0))
    return false;
  emit2 ("xor", "a,#0xff");
  emit2 ("add", "a,#0x01");

  if (size == 1)
    {
      setAResult (result);
      return true;
    }

  emit2 ("mov", "x,a");

  if (!loadOperandByteToA (left, 1))
    return false;
  emit2 ("xor", "a,#0xff");
  emit2 ("addc", "a,#0x00");

  setReturnResult (result, size);
  return true;
}

static bool
genMult (const iCode *ic)
{
  operand *result = IC_RESULT (ic);
  operand *left = IC_LEFT (ic);
  operand *right = IC_RIGHT (ic);
  unsigned top_byte_mask;
  int size;

  if (!IS_ITEMP (result) || !left || !right)
    return false;

  size = k78k0_operandSize (result);
  if (size < 1 || size > 2)
    return false;

  if (!isUnsignedByteSource (left) || !isUnsignedByteSource (right))
    return false;

  top_byte_mask = unsignedBitIntTopByteMask (result);

  if (operandNeedsStackHL (left, 1) || operandNeedsStackHL (right, 1))
    setHLToSP ();

  if (!loadOperandByteToA (right, 0))
    return false;
  emit2 ("mov", "x,a");

  if (!loadOperandByteToA (left, 0))
    return false;
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
    {
      emit2 ("mov", "c,a");
      setHLToSP ();
      emit2 ("mov", "a,c");
    }

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
  emit2 ("br", "!%s", done_label);
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
  char true_label[32];
  char false_label[32];
  char done_label[32];
  int size;

  if (!IS_ITEMP (result) || !left || k78k0_operandSize (result) < 1 || k78k0_operandSize (result) > 2)
    return false;

  size = k78k0_operandSize (left);
  if (size < 1 || size > K78K0_MAX_SCALAR_BYTES)
    return false;

  makeLocalLabel (true_label, sizeof (true_label));
  makeLocalLabel (false_label, sizeof (false_label));
  makeLocalLabel (done_label, sizeof (done_label));

  for (int offset = 0; offset < size; offset++)
    {
      if (!loadOperandByteToA (left, offset))
        return false;
      emit2 ("cmp", "a,#0x00");
      emitCondBranch ("bnz", false_label);
    }

  emit2 ("br", "!%s", true_label);
  emitLocalLabel (false_label);
  genBooleanResult (result, true_label, done_label);
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
      if (stack_offset > 0)
        emit2 ("addw", "ax,#0x%04x", (unsigned)stack_offset);
      else if (stack_offset < 0)
        emit2 ("subw", "ax,#0x%04x", (unsigned)(-stack_offset));
    }
  else if (sym->rname[0])
    emit2 ("movw", "ax,#%s", sym->rname);
  else
    return false;

  emit2 ("push", "ax");
  stack_pushed += 2;
  clearAResult ();
  clearHLState ();
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
          if (return_size > 0 && return_size <= K78K0_MAX_SCALAR_BYTES)
            saveScalarReturnForEpilogue (return_size);

          adjustStackPointer (cleanup_bytes, false);

          if (return_size > 0 && return_size <= K78K0_MAX_SCALAR_BYTES)
            restoreScalarReturnForEpilogue (return_size);
        }
    }

  if (result_size > 0 && result_size <= K78K0_MAX_SCALAR_BYTES)
    {
      operand *target;

      if (return_size > 2)
        mirrorWideCallReturnBytes (return_size);

      if (result_size == 1 && return_size > 1)
        emit2 ("mov", "a,x");

      target = wideAssignmentTarget (ic, result, result_size);
      if (target)
        {
          const symbol *storage = operandStorageSymbol (target);
          const bool stored = storage->onStack ?
            (result_size > 2 ? storeReturnMirrorToStack (storage, result_size) : storeAccumulatorToStack (storage, result_size)) :
            storeReturnRegistersToDirect (storage, result_size);

          if (stored)
            {
              ic->next->generated = true;
              clearAResult ();
              return true;
            }
        }

      setReturnResult (result, result_size);
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
  const bool bigreturn = typeReturnsStruct (operandType (left));
  sym_link *ftype = IS_FUNCPTR (operandType (left)) ? operandType (left)->next : operandType (left);
  int first_regarg_size = 0;
  int result_size = 0;
  int return_size = 0;

  if (ic->op != CALL || !left)
    return false;

  clearAResult ();
  clearHLState ();

  if (bigreturn)
    {
      first_regarg_size = functionFirstRegArgSize (ftype);
      saveScalarReturnForEpilogue (first_regarg_size);
    }

  if (bigreturn)
    {
      wassertl (result, "78K0 struct-return call has no destination.");
      if (!pushBigReturnAddress (result))
        return false;
    }

  if (bigreturn)
    restoreScalarReturnForEpilogue (first_regarg_size);

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
  const bool bigreturn = typeReturnsStruct (operandType (left));
  sym_link *ftype = IS_FUNCPTR (operandType (left)) ? operandType (left)->next : operandType (left);
  char return_label[32];
  int first_regarg_size = 0;
  int result_size = 0;
  int return_size = 0;

  if (ic->op != PCALL || !left || k78k0_operandSize (left) != 2)
    return false;

  first_regarg_size = functionFirstRegArgSize (ftype);
  if (bigreturn)
    saveScalarReturnForEpilogue (first_regarg_size);

  if (bigreturn)
    {
      wassertl (result, "78K0 struct-return indirect call has no destination.");
      if (!pushBigReturnAddress (result))
        return false;
    }

  if (!bigreturn)
    saveScalarReturnForEpilogue (first_regarg_size);

  if (!genOperandReturnValue (left))
    return false;

  emit2 ("movw", "de,ax");
  makeLocalLabel (return_label, sizeof (return_label));
  clearAResult ();
  clearHLState ();
  emit2 ("movw", "ax,#%s", return_label);
  emit2 ("push", "ax");
  emit2 ("movw", "ax,de");
  emit2 ("push", "ax");
  restoreScalarReturnForEpilogue (first_regarg_size);
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
  int size;

  if (!cond)
    return false;

  size = k78k0_operandSize (cond);
  if (size < 1 || size > K78K0_MAX_SCALAR_BYTES)
    return false;

  if (operandNeedsStackHL (cond, size))
    setHLToSP ();

  if (!loadOperandByteToA (cond, 0))
    return false;

  if (size > 1)
    {
      emit2 ("mov", "b,a");
      for (int offset = 1; offset < size; offset++)
        {
          if (!loadOperandByteToA (cond, offset))
            return false;
          emit2 ("or", "a,b");
          emit2 ("mov", "b,a");
        }
    }

  emit2 ("cmp", "a,#0x00");

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

  clearAResult ();
  clearHLState ();
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
        return return_result_ax_valid || restoreReturnValueAXFromMirror (size);

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

  clearAResult ();
  clearHLState ();

  if (base->onStack)
    {
      const int stack_offset = stackByteOffset (base, (int)offset);

      emit2 ("movw", "ax,sp");
      if (stack_offset > 0)
        emit2 ("addw", "ax,#0x%04x", (unsigned)stack_offset);
      else if (stack_offset < 0)
        emit2 ("subw", "ax,#0x%04x", (unsigned)(-stack_offset));
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

  clearAResult ();
  clearHLState ();
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
getPointerOffset (const iCode *ic, unsigned long long *offset)
{
  operand *right = IC_RIGHT (ic);

  if (!right || !IS_OP_LITERAL (right))
    return false;

  *offset = operandLitValueUll (right);
  return *offset <= 255u;
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

  clearAResult ();
  clearHLState ();

  if (sym->onStack)
    {
      const int stack_offset = stackByteOffset (sym, offset);

      emit2 ("movw", "ax,sp");
      if (stack_offset > 0)
        emit2 ("addw", "ax,#0x%04x", (unsigned)stack_offset);
      else if (stack_offset < 0)
        emit2 ("subw", "ax,#0x%04x", (unsigned)(-stack_offset));
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
}

static bool
genPointerGetBitField (const operand *result, const operand *ptr, const unsigned pointer_offset, sym_link *type)
{
  const int bit_start = SPEC_BSTR (type);
  const int bit_length = SPEC_BLEN (type);
  const int result_size = k78k0_operandSize (result);
  const int storage_size = (bit_start + bit_length + 7) / 8;
  const bool sign_extend = !SPEC_USIGN (type) && !IS_BOOLEAN (type);

  if (bit_start < 0 || bit_start > 7 || bit_length < 1 || bit_length > K78K0_MAX_SCALAR_BYTES * 8 ||
      result_size < 1 || result_size > K78K0_MAX_SCALAR_BYTES ||
      pointer_offset + (unsigned)storage_size > 256u || !savePointerToDE (ptr))
    return false;

  for (int byte = 0; byte < result_size; byte++)
    {
      const int remaining_bits = bit_length - byte * 8;

      setHLFromDE ();
      emit2 ("mov", "a,[hl+0x%02x]", pointer_offset + (unsigned)byte);

      if (bit_start)
        {
          for (int shift = 0; shift < bit_start; shift++)
            emitByteRightShift ();
          emit2 ("mov", "c,a");

          if (byte + 1 < storage_size)
            {
              setHLFromDE ();
              emit2 ("mov", "a,[hl+0x%02x]", pointer_offset + (unsigned)byte + 1u);
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
              emitCondBranch ("bc", positive_label);
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
  sym_link *bit_field_type;
  unsigned long long offset;
  int size;

  if (!IS_ITEMP (result) || !left || ic->op != GET_VALUE_AT_ADDRESS)
    return false;

  size = k78k0_operandSize (result);
  if (size < 1 || size > K78K0_MAX_SCALAR_BYTES || !getPointerOffset (ic, &offset) || offset + size > 256u)
    return false;

  bit_field_type = getSpec (operandType (result));
  if (IS_BITFIELD (bit_field_type))
    return genPointerGetBitField (result, left, (unsigned)offset, bit_field_type);

  if (!loadPointerToHL (left))
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
      for (int byte = size - 1; byte >= 2; byte--)
        {
          emit2 ("mov", "a,[hl+0x%02x]", (unsigned)(offset + byte));
          emit2 ("mov", "!%s,a", wideReturnByteName (byte));
        }

      emit2 ("mov", "a,[hl+0x%02x]", (unsigned)offset);
      emit2 ("mov", "x,a");
      emit2 ("mov", "a,[hl+0x%02x]", (unsigned)(offset + 1u));
      setReturnResult (result, size);
      return true;
    }

  return false;
}

static bool
genPointerSetBitField (const operand *ptr, const operand *value, sym_link *type, const wideReturnState saved_wide_return)
{
  const int bit_start = SPEC_BSTR (type);
  const int bit_length = SPEC_BLEN (type);
  const int value_size = k78k0_operandSize (value);
  const int storage_size = (bit_start + bit_length + 7) / 8;

  if (bit_start < 0 || bit_start > 7 || bit_length < 1 || bit_length > K78K0_MAX_SCALAR_BYTES * 8 ||
      value_size < 1 || value_size > K78K0_MAX_SCALAR_BYTES || storage_size < 1 || storage_size > 256 ||
      !savePointerToDE (ptr))
    return false;
  restoreWideReturnState (saved_wide_return);

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

  clearAResult ();
  clearHLState ();
  restoreWideReturnState (saved_wide_return);
  return true;
}

static bool
genPointerSet (const iCode *ic)
{
  operand *ptr;
  operand *value = IC_RIGHT (ic);
  const wideReturnState saved_wide_return = saveWideReturnState ();
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
    return genPointerSetBitField (ptr, value, bit_field_type, saved_wide_return);

  size = k78k0_operandSize (value);
  if (size < 1 || size > K78K0_MAX_SCALAR_BYTES)
    return false;

  if (!savePointerToDE (ptr))
    return false;
  restoreWideReturnState (saved_wide_return);

  for (int offset = 0; offset < size; offset++)
    {
      if (!loadOperandByteToA (value, offset))
        return false;
      emit2 ("mov", "b,a");

      emit2 ("movw", "ax,de");
      emit2 ("movw", "hl,ax");
      hl_is_sp = false;
      emit2 ("mov", "a,b");
      emit2 ("mov", "[hl+0x%02x],a", (unsigned)offset);
    }

  clearAResult ();
  restoreWideReturnState (saved_wide_return);
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
  char positive_label[32];
  char shift_label[32];

  makeLocalLabel (positive_label, sizeof (positive_label));
  makeLocalLabel (shift_label, sizeof (shift_label));

  emit2 ("cmp", "a,#0x80");
  emitCondBranch ("bc", positive_label);
  emit2 ("set1", "cy");
  emit2 ("br", "!%s", shift_label);
  emitLocalLabel (positive_label);
  emit2 ("clr1", "cy");
  emitLocalLabel (shift_label);
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
  char negative_label[32];
  char done_label[32];

  makeLocalLabel (negative_label, sizeof (negative_label));
  makeLocalLabel (done_label, sizeof (done_label));

  emit2 ("cmp", "a,#0x80");
  emitCondBranch ("bnc", negative_label);
  emit2 ("mov", "a,#0x00");
  emit2 ("br", "!%s", done_label);
  emitLocalLabel (negative_label);
  emit2 ("mov", "a,#0xff");
  emitLocalLabel (done_label);
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

  emitLocalLabel (loop_label);
  emit2 ("mov", "b,a");
  emit2 ("mov", "a,c");
  emit2 ("cmp", "a,#0x00");
  emitCondBranch ("bz", done_label);
  emit2 ("sub", "a,#0x01");
  emit2 ("mov", "c,a");
  emit2 ("mov", "a,b");

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

  emit2 ("br", "!%s", loop_label);
  emitLocalLabel (done_label);
  emit2 ("mov", "a,b");
}

static bool
copyOperandToTarget (const operand *source, const operand *target, const int size)
{
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

  for (int offset = 0; offset < size; offset++)
    {
      emit2 ("mov", "a,#0x00");
      if (!storeAToOperandByte (target, offset))
        return false;
    }

  return true;
}

static bool
fillTargetBytesWithA (const operand *target, const int size)
{
  emit2 ("mov", "c,a");

  if (operandNeedsStackHL (target, size))
    setHLToSP ();

  for (int offset = 0; offset < size; offset++)
    {
      emit2 ("mov", "a,c");
      if (!storeAToOperandByte (target, offset))
        return false;
    }

  return true;
}

static bool
copyOperandToWideReturn (const operand *source, const int size)
{
  if (operandNeedsStackHL (source, size))
    setHLToSP ();

  for (int offset = 0; offset < size; offset++)
    {
      if (!loadOperandByteToA (source, offset))
        return false;

      emit2 ("mov", "!%s,a", wideReturnByteName (offset));
    }

  return true;
}

static void
clearWideReturnBytes (const int size)
{
  for (int offset = 0; offset < size; offset++)
    {
      emit2 ("mov", "a,#0x00");
      emit2 ("mov", "!%s,a", wideReturnByteName (offset));
    }
}

static void
fillWideReturnBytesWithA (const int size)
{
  for (int offset = 0; offset < size; offset++)
    emit2 ("mov", "!%s,a", wideReturnByteName (offset));
}

static bool
setWideReturnResultFromMirror (const operand *result, const int size)
{
  const symbol *sym = OP_SYMBOL_CONST (result);
  const symbol *storage = operandStorageSymbol (result);

  if (!restoreReturnValueAXFromMirror (size))
    return false;

  if (storage != sym)
    {
      if (!storeReturnMirrorToStack (storage, size))
        return false;
      clearAResult ();
      return true;
    }

  return_result_sym = sym;
  return_result_size = size;
  return_result_ax_valid = true;
  a_result_sym = NULL;
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

static void
shiftWideReturnLeftOne (const int size)
{
  emit2 ("clr1", "cy");

  for (int offset = 0; offset < size; offset++)
    {
      emit2 ("mov", "a,!%s", wideReturnByteName (offset));
      emit2 ("rolc", "a,1");
      emit2 ("mov", "!%s,a", wideReturnByteName (offset));
    }
}

static bool
shiftTargetRightOne (const operand *target, const int size, const bool is_signed_right)
{
  int offset = size - 1;

  if (!loadOperandByteToA (target, offset))
    return false;

  if (is_signed_right)
    {
      emitSignCarryForA ();
      if (operandNeedsStackHL (target, size))
        {
          emit2 ("mov", "c,a");
          setHLToSP ();
          emit2 ("mov", "a,c");
        }
    }
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

static void
shiftWideReturnRightOne (const int size, const bool is_signed_right)
{
  int offset = size - 1;

  emit2 ("mov", "a,!%s", wideReturnByteName (offset));

  if (is_signed_right)
    emitSignCarryForA ();
  else
    emit2 ("clr1", "cy");

  emit2 ("rorc", "a,1");
  emit2 ("mov", "!%s,a", wideReturnByteName (offset));

  while (offset--)
    {
      emit2 ("mov", "a,!%s", wideReturnByteName (offset));
      emit2 ("rorc", "a,1");
      emit2 ("mov", "!%s,a", wideReturnByteName (offset));
    }
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

static void
maskWideReturnTopByte (const int size, const unsigned mask)
{
  if (mask == 0xffu)
    return;

  emit2 ("mov", "a,!%s", wideReturnByteName (size - 1));
  emit2 ("and", "a,#0x%02x", mask);
  emit2 ("mov", "!%s,a", wideReturnByteName (size - 1));
}

static bool
genWideLiteralShift (const iCode *ic, const bool is_right, const bool is_signed_right, unsigned long long count)
{
  operand *result = IC_RESULT (ic);
  operand *left = IC_LEFT (ic);
  operand *target;
  const unsigned top_byte_mask = unsignedBitIntTopByteMask (result);
  int size = getSize (operandType (result));

  if (size < 1 || size > K78K0_MAX_SCALAR_BYTES || getSize (operandType (left)) != size)
    return false;

  target = wideAssignmentTarget (ic, result, size);

  if (count >= (unsigned long long)size * 8ull)
    {
      if (target)
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

          ic->next->generated = true;
          clearAResult ();
          return true;
        }

      if (!is_signed_right)
        clearWideReturnBytes (size);
      else
        {
          if (!loadOperandByteToA (left, size - 1))
            return false;
          emitSignMaskForA ();
          fillWideReturnBytesWithA (size);
        }

      return setWideReturnResultFromMirror (result, size);
    }

  if (target)
    {
      if (!copyOperandToTarget (left, target, size))
        return false;

      while (count--)
        {
          if (is_right)
            {
              if (!shiftTargetRightOne (target, size, is_signed_right))
                return false;
            }
          else if (!shiftTargetLeftOne (target, size))
            return false;
        }

      if (!maskTargetTopByte (target, size, top_byte_mask))
        return false;

      ic->next->generated = true;
      clearAResult ();
      return true;
    }

  if (!copyOperandToWideReturn (left, size))
    return false;

  while (count--)
    {
      if (is_right)
        shiftWideReturnRightOne (size, is_signed_right);
      else
        shiftWideReturnLeftOne (size);
    }

  maskWideReturnTopByte (size, top_byte_mask);

  return setWideReturnResultFromMirror (result, size);
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
  if (target)
    {
      if (!copyOperandToTarget (left, target, size))
        return false;
    }
  else if (!copyOperandToWideReturn (left, size))
    return false;

  if (!loadOperandByteToA (right, 0))
    return false;
  emit2 ("mov", "b,a");

  makeLocalLabel (loop_label, sizeof (loop_label));
  makeLocalLabel (done_label, sizeof (done_label));

  emitLocalLabel (loop_label);
  emit2 ("mov", "a,b");
  emit2 ("cmp", "a,#0x00");
  emitCondBranch ("bz", done_label);
  emit2 ("sub", "a,#0x01");
  emit2 ("mov", "b,a");

  if (is_right)
    {
      if (target)
        {
          if (!shiftTargetRightOne (target, size, is_signed_right))
            return false;
        }
      else
        shiftWideReturnRightOne (size, is_signed_right);
    }
  else if (target)
    {
      if (!shiftTargetLeftOne (target, size))
        return false;
    }
  else
    shiftWideReturnLeftOne (size);

  emit2 ("br", "!%s", loop_label);
  emitLocalLabel (done_label);

  if (target)
    {
      if (!maskTargetTopByte (target, size, top_byte_mask))
        return false;
      ic->next->generated = true;
      clearAResult ();
      return true;
    }

  maskWideReturnTopByte (size, top_byte_mask);
  return setWideReturnResultFromMirror (result, size);
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
  char true_label[32];
  char done_label[32];

  if (!IS_ITEMP (result) || !left || !IS_OP_LITERAL (right) || k78k0_operandSize (result) != 1)
    return false;

  bit_offset = operandLitValueUll (right);
  if (bit_offset >= (unsigned long long)k78k0_operandSize (left) * 8ull)
    return false;

  if (!loadOperandByteToA (left, (int)(bit_offset / 8ull)))
    return false;

  emit2 ("and", "a,#0x%02x", 1u << (unsigned)(bit_offset % 8ull));
  emit2 ("cmp", "a,#0x00");

  makeLocalLabel (true_label, sizeof (true_label));
  makeLocalLabel (done_label, sizeof (done_label));

  emitCondBranch ("bnz", true_label);
  emit2 ("mov", "a,#0x00");
  emit2 ("br", "!%s", done_label);
  emitLocalLabel (true_label);
  emit2 ("mov", "a,#0x01");
  emitLocalLabel (done_label);
  setAResult (result);
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
  if (size < 1 || size > 2)
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

  clearAResult ();
  clearHLState ();
  return true;
}

static bool
genIpush (const iCode *ic)
{
  operand *left = IC_LEFT (ic);
  const wideReturnState saved_wide_return = saveWideReturnState ();
  int size;

  if (!left)
    return false;

  size = getSize (operandType (left));
  if (size < 1 || size > K78K0_MAX_SCALAR_BYTES)
    return false;

  if (size == 1)
    {
      adjustStackPointer (-1, true);
      restoreWideReturnState (saved_wide_return);
      if (!loadOperandByteToA (left, 0))
        return false;
      emit2 ("mov", "c,a");
      if (!hl_is_sp)
        setHLToSP ();
      emit2 ("mov", "a,c");
      emit2 ("mov", "[hl+0x00],a");
      clearAResult ();
      restoreWideReturnState (saved_wide_return);
      return true;
    }

  if (size == 2)
    {
      if (!genOperandReturnValue (left))
        return false;

      emit2 ("push", "ax");
      stack_pushed += 2;
      clearAResult ();
      clearHLState ();
      restoreWideReturnState (saved_wide_return);
      return true;
    }

  adjustStackPointer (-size, true);
  restoreWideReturnState (saved_wide_return);
  for (int offset = 0; offset < size; offset++)
    {
      if (!loadOperandByteToA (left, offset))
        return false;
      emit2 ("mov", "c,a");
      if (!hl_is_sp)
        setHLToSP ();
      emit2 ("mov", "a,c");
      emit2 ("mov", "[hl+0x%02x],a", (unsigned)offset);
      restoreWideReturnState (saved_wide_return);
    }

  clearAResult ();
  restoreWideReturnState (saved_wide_return);
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
        emit2 ("addw", "ax,#0x%04x", (unsigned)(pointer_offset + (unsigned long long)offset));
      emit2 ("movw", "hl,ax");
      hl_is_sp = false;
      emit2 ("mov", "a,[hl+0x00]");
      emit2 ("mov", "c,a");
      setHLToSP ();
      emit2 ("mov", "a,c");
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

  if (size <= 2 && operandInReturnValueWithAX (left, size))
    return true;

  if (size == 2 && IS_SYMOP (left) && IS_FUNC (OP_SYMBOL_CONST (left)->type))
    return loadFunctionAddressToAX (left);

  if (operandNeedsStackHL (left, size))
    {
      if (operandStackBytesFitIndexedSP (left, size))
        setHLToSP ();
      else
        {
          for (int offset = 0; offset < size; offset++)
            {
              if (!loadOperandByteToA (left, offset))
                return false;
              emit2 ("mov", "!%s,a", wideReturnByteName (offset));
            }

          if (size >= 4)
            {
              emit2 ("mov", "a,!%s", wideReturnByteName (3));
              emit2 ("mov", "b,a");
            }
          if (size >= 3)
            {
              emit2 ("mov", "a,!%s", wideReturnByteName (2));
              emit2 ("mov", "c,a");
            }
          if (size >= 2)
            {
              emit2 ("mov", "a,!%s", wideReturnByteName (0));
              emit2 ("mov", "x,a");
              emit2 ("mov", "a,!%s", wideReturnByteName (1));
            }
          else
            emit2 ("mov", "a,!%s", wideReturnByteName (0));

          clearAResult ();
          clearHLState ();
          return true;
        }
    }

  if (size == 1)
    return loadOperandByteToA (left, 0);

  if (!loadOperandByteToA (left, 0))
    return false;
  emit2 ("mov", "x,a");

  if (size == 2)
    return loadOperandByteToA (left, 1);

  if (!loadOperandByteToA (left, 2))
    return false;
  emit2 ("mov", "c,a");

  if (size >= 4)
    {
      if (!loadOperandByteToA (left, 3))
        return false;
      emit2 ("mov", "b,a");
    }

  return loadOperandByteToA (left, 1);
}

static bool
storeFirstArgRegisters (const operand *result)
{
  const int size = k78k0_operandSize (result);

  if (size < 1 || size > 4 || !IS_SYMOP (result))
    return false;

  if (operandNeedsStackHL (result, size))
    {
      if (operandStackBytesFitIndexedSP (result, size))
        ensureHLToSP ();
      else
        {
          if (size == 1)
            emit2 ("mov", "!%s,a", wideReturnByteName (0));
          else
            {
              emit2 ("mov", "!%s,a", wideReturnByteName (1));
              emit2 ("mov", "a,x");
              emit2 ("mov", "!%s,a", wideReturnByteName (0));
            }
          if (size >= 3)
            {
              emit2 ("mov", "a,c");
              emit2 ("mov", "!%s,a", wideReturnByteName (2));
            }
          if (size >= 4)
            {
              emit2 ("mov", "a,b");
              emit2 ("mov", "!%s,a", wideReturnByteName (3));
            }

          for (int offset = 0; offset < size; offset++)
            {
              emit2 ("mov", "a,!%s", wideReturnByteName (offset));
              if (!storeAToOperandByte (result, offset))
                return false;
            }

          clearAResult ();
          clearHLState ();
          return true;
        }
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

  clearAResult ();
  clearHLState ();
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
copyStructReturnToHiddenPointer (const operand *left)
{
  const int size = getSize (operandType (left));
  const int pointer_offset = local_stack_size + K78K0_RETURN_ADDRESS_BYTES + stack_pushed;

  if (!IS_STRUCT (operandType (left)) || pointer_offset < 0 || pointer_offset + 1 > 255)
    return false;

  for (int offset = 0; offset < size; offset++)
    {
      if (!loadOperandByteToA (left, offset))
        return false;

      emit2 ("mov", "b,a");
      setHLToSP ();
      emit2 ("mov", "a,[hl+0x%02x]", (unsigned)pointer_offset);
      emit2 ("mov", "x,a");
      emit2 ("mov", "a,[hl+0x%02x]", (unsigned)(pointer_offset + 1));
      if (offset)
        emit2 ("addw", "ax,#0x%04x", (unsigned)offset);
      emit2 ("movw", "hl,ax");
      emit2 ("mov", "a,b");
      emit2 ("mov", "[hl+0x00],a");
      clearHLState ();
    }

  clearAResult ();
  return true;
}

static void
genReturn (const iCode *ic)
{
  char label[32];

  if (IC_LEFT (ic) && (current_return_is_struct || current_return_size > 0))
    {
      if (IS_STRUCT (operandType (IC_LEFT (ic))))
        {
          if (!copyStructReturnToHiddenPointer (IC_LEFT (ic)))
            {
              wassertl (0, "78K0 struct return operand is not implemented yet.");
              return;
            }
        }
      else if (!genOperandReturnValue (IC_LEFT (ic)))
        {
          wassertl (0, "78K0 return operand is not implemented yet.");
          return;
        }
      else
        loadWideReturnRegistersFromMirror (k78k0_operandSize (IC_LEFT (ic)));
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
