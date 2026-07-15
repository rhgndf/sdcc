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

#include <math.h>

#define K78K0_RETURN_ADDRESS_BYTES 2
#define K78K0_REGISTER_RETURN_BYTES 4

static struct
{
  struct
  {
    int pushed;
    int local_size;
    int saved_de_bytes;
  }
  stack;
  struct
  {
    bool offset_valid;
    int sp_offset;
  }
  hl;
}
G;

#define REG_BYTE(index) (k78k0_regs + (index))

static const asmop asmop_a =
  {K78K0_AOP_RETURN, 1, NULL, NULL, {REG_BYTE (K78K0_RB0_A_IDX)}};
static const asmop asmop_ax =
  {K78K0_AOP_RETURN, 2, NULL, NULL,
   {REG_BYTE (K78K0_RB0_X_IDX), REG_BYTE (K78K0_RB0_A_IDX)}};
static const asmop asmop_cax =
  {K78K0_AOP_RETURN, 3, NULL, NULL,
   {REG_BYTE (K78K0_RB0_X_IDX), REG_BYTE (K78K0_RB0_A_IDX),
    REG_BYTE (K78K0_RB0_C_IDX)}};
static const asmop asmop_bcax =
  {K78K0_AOP_RETURN, 4, NULL, NULL,
   {REG_BYTE (K78K0_RB0_X_IDX), REG_BYTE (K78K0_RB0_A_IDX),
    REG_BYTE (K78K0_RB0_C_IDX), REG_BYTE (K78K0_RB0_B_IDX)}};

#undef REG_BYTE

#define ASMOP_A (&asmop_a)
#define ASMOP_AX (&asmop_ax)

typedef enum
{
  K78K0_SHIFT_LEFT,
  K78K0_SHIFT_RIGHT,
  K78K0_SHIFT_SIGNED_RIGHT
}
shift_kind;

static void genCritical (void);
static void genEndCritical (void);
static void emitByteShift (shift_kind kind);
static void clearHLState (void);
static bool typeReturnsViaHiddenPointer (sym_link *type);
static int functionReturnSize (sym_link *type);
static bool redundantLiteralReload (const char *line);

static bool regalloc_dry_run;
static bool regalloc_dry_run_failed;
static unsigned regalloc_dry_run_cost_bytes;
static unsigned regalloc_dry_label;
static symbol regalloc_dry_spill;
static unsigned codegen_label_scope;

static void
markGenerated (iCode *ic)
{
  if (!regalloc_dry_run && ic)
    ic->generated = true;
}

static void
emit2 (const char *inst, const char *fmt, ...)
{
  va_list ap;
  const char *line;

  va_start (ap, fmt);
  line = format_opcode (inst, fmt, ap);
  va_end (ap);

  if (regalloc_dry_run)
    {
      const int size = !inst[0] ? 0 : !strcmp (inst, ".dw") ? 2 :
        k78k0_instructionSize (inst, line + strlen (inst));

      if (size >= 999)
        regalloc_dry_run_failed = true;
      else if (size > 0)
        regalloc_dry_run_cost_bytes += (unsigned)size;
    }
  else if (options.nopeep || !redundantLiteralReload (line))
    emit_raw (line);

  dbuf_free (line);
}

static void
clearHLState (void)
{
  G.hl.offset_valid = false;
}

static void
moveAXToHL (void)
{
  emit2 ("movw", "hl,ax");
  clearHLState ();
}

static void
clearRegisterState (void)
{
  clearHLState ();
}

static bool
storeAToOperandByte (const operand *op, int offset);

static bool
genOperandReturnValue (const operand *op);

static bool
loadAddressOperandToPair (const operand *op, const char *pair, long offset);

static bool
loadAddressOperandToAX (const operand *op);

static bool
rematerializedOperandAddress (const operand *op, const symbol **base, long *offset);

static void
emitSignMaskForA (void);

static bool
genWordBinaryOp (const iCode *ic, const char *low_mnemonic, const char *high_mnemonic,
                 bool commutative);

static bool
genMove_o (const asmop *destination, int destination_offset,
           const asmop *source, int source_offset, int size);

static bool
genMove (const asmop *destination, const asmop *source);

static bool
aopForOperand (asmop *aop, const operand *op);

static const asmop *
aopReturnForSize (int size);

static const symbol *
operandStorageSymbol (const operand *op)
{
  const symbol *sym = OP_SYMBOL_CONST (op);

  if (IS_ITEMP (op) && sym->usl.spillLoc && !sym->remat &&
      !IS_FUNC (sym->usl.spillLoc->type))
    return sym->usl.spillLoc;

  if (regalloc_dry_run && IS_ITEMP (op) && !sym->remat)
    {
      const int size = sym->nRegs > 0 ? sym->nRegs : getSize (operandType (op));
      bool needs_storage = sym->isspilt || size < 1;

      for (int byte = 0; byte < size && byte < K78K0_MAX_SCALAR_BYTES; byte++)
        needs_storage |= !sym->regs[byte];
      if (needs_storage)
        {
          memset (&regalloc_dry_spill, 0, sizeof (regalloc_dry_spill));
          regalloc_dry_spill.type = sym->type;
          regalloc_dry_spill.etype = sym->etype;
          regalloc_dry_spill.onStack = 1;
          regalloc_dry_spill.stackSpil = 1;
          return &regalloc_dry_spill;
        }
    }

  return sym;
}

static const reg_info *
symbolRegisterByte (const symbol *sym, const int offset)
{
  if (!sym || sym->isspilt || offset < 0 || offset >= sym->nRegs)
    return NULL;

  return sym->regs[offset];
}

static const reg_info *
operandRegisterByte (const operand *op, const int offset)
{
  return IS_SYMOP (op) ? symbolRegisterByte (OP_SYMBOL_CONST (op), offset) : NULL;
}

static const char *
byteRegisterName (const reg_info *reg)
{
  wassertl (reg && reg->rIdx >= K78K0_RB0_X_IDX && reg->rIdx <= K78K0_RB0_H_IDX,
            "invalid 78K0 allocated byte register");
  return reg->name;
}

static bool
registerWritesHL (const reg_info *reg)
{
  return reg && (reg->rIdx == K78K0_RB0_L_IDX || reg->rIdx == K78K0_RB0_H_IDX);
}

static const char *
symbolRegisterPair (const symbol *sym)
{
  const reg_info *low = symbolRegisterByte (sym, 0);
  const reg_info *high = symbolRegisterByte (sym, 1);

  if (!low || !high)
    return NULL;
  if (low->rIdx == K78K0_RB0_X_IDX && high->rIdx == K78K0_RB0_A_IDX)
    return "ax";
  if (low->rIdx == K78K0_RB0_C_IDX && high->rIdx == K78K0_RB0_B_IDX)
    return "bc";
  if (low->rIdx == K78K0_RB0_E_IDX && high->rIdx == K78K0_RB0_D_IDX)
    return "de";
  if (low->rIdx == K78K0_RB0_L_IDX && high->rIdx == K78K0_RB0_H_IDX)
    return "hl";
  return NULL;
}

static bool
operandHasAllocatedByte (const operand *op)
{
  if (!op || !IS_SYMOP (op))
    return false;

  const symbol *sym = OP_SYMBOL_CONST (op);
  for (int offset = 0; offset < sym->nRegs && offset < K78K0_MAX_SCALAR_BYTES; offset++)
    if (symbolRegisterByte (sym, offset))
      return true;

  return false;
}

static void
movePair (const char *destination, const char *source)
{
  if (!strcmp (destination, source))
    return;

  if (!strcmp (destination, "ax"))
    emit2 ("movw", "ax,%s", source);
  else if (!strcmp (source, "ax"))
    emit2 ("movw", "%s,ax", destination);
  else
    {
      emit2 ("movw", "ax,%s", source);
      emit2 ("movw", "%s,ax", destination);
    }
  if (!strcmp (destination, "hl"))
    clearHLState ();
}

static void
makeLocalLabel (char *buf, size_t buflen)
{
  if (regalloc_dry_run)
    {
      SNPRINTF (buf, buflen, "Ldry%u$", regalloc_dry_label++);
      return;
    }

  const symbol *label = newiTempLabel (NULL);

  SNPRINTF (buf, buflen, "L%05u_%05d$", codegen_label_scope,
            labelKey2num (label->key));
}

static void
makeICLabel (char *buf, size_t buflen, const symbol *label)
{
  SNPRINTF (buf, buflen, "L%05u_%05d$", codegen_label_scope,
            labelKey2num (label->key));
}

static void
emitLocalLabel (const char *label)
{
  clearRegisterState ();
  emit2 ("", "%s:", label);
  if (!regalloc_dry_run)
    genLine.lineCurr->isLabel = 1;
}

static void
emitLocalLabelPreservingHL (const char *label)
{
  emit2 ("", "%s:", label);
  if (!regalloc_dry_run)
    genLine.lineCurr->isLabel = 1;
}

static void
emitCondBranch (const char *inst, const char *label)
{
  char skip_label[32];
  const char *inverse = !strcmp (inst, "bz") ? "bnz" : !strcmp (inst, "bnz") ? "bz" :
                        !strcmp (inst, "bc") ? "bnc" : !strcmp (inst, "bnc") ? "bc" : NULL;

  wassertl (inverse, "unsupported 78K0 conditional branch");
  makeLocalLabel (skip_label, sizeof (skip_label));
  emit2 (inverse, "%s", skip_label);
  emit2 ("br", "!%s", label);
  emitLocalLabelPreservingHL (skip_label);
}

static void
emitABitBranch (const bool branch_if_set, const unsigned bit, const char *label)
{
  char skip_label[32];

  makeLocalLabel (skip_label, sizeof (skip_label));
  emit2 (branch_if_set ? "bf" : "bt", "a.%u,%s", bit, skip_label);
  emit2 ("br", "!%s", label);
  emitLocalLabelPreservingHL (skip_label);
}

static void
adjustAX (const int amount)
{
  if (amount >= -2 && amount <= 2)
    for (int count = amount < 0 ? -amount : amount; count; count--)
      emit2 (amount < 0 ? "decw" : "incw", "ax");
  else if (amount > 0)
    emit2 ("addw", "ax,#0x%04x", (unsigned)amount);
  else
    emit2 ("subw", "ax,#0x%04x", (unsigned)(-amount));
}

static int
stackByteOffset (const symbol *sym, const int offset)
{
  int base = sym->stack;

  if (!sym->stackSpil && (sym->_isparm || sym->ismyparm) && !IS_REGPARM (sym->etype))
    base += G.stack.local_size +
      (currFunc && typeReturnsViaHiddenPointer (currFunc->type) ? 2 : 0);
  else if (sym->stack > 0)
    base -= G.stack.local_size + K78K0_RETURN_ADDRESS_BYTES + getSize (sym->type) - 1;
  else if (sym->stack < 0)
    base += G.stack.local_size;

  return base + G.stack.pushed + offset;
}

static bool
loadSymbolAddressToAX (const symbol *sym, const long offset)
{
  clearRegisterState ();

  if (sym->onStack)
    {
      emit2 ("movw", "ax,sp");
      adjustAX (stackByteOffset (sym, (int)offset));
      return true;
    }

  if (!sym->rname[0])
    return false;

  if (offset)
    emit2 ("movw", "ax,#%s + %ld", sym->rname, offset);
  else
    emit2 ("movw", "ax,#%s", sym->rname);

  return true;
}

typedef enum
{
  K78K0_CLOBBER_AX,
  K78K0_PRESERVE_A,
  K78K0_PRESERVE_AX
}
stack_address_preservation;

static void
setStackAddress (const int stack_offset, const stack_address_preservation preserve,
                 const char *scratch)
{
  if (preserve == K78K0_PRESERVE_A)
    {
      wassertl (scratch, "78K0 stack address setup needs an A scratch register.");
      emit2 ("mov", "%s,a", scratch);
    }

  if (preserve == K78K0_PRESERVE_AX)
    moveAXToHL ();
  else
    clearRegisterState ();

  emit2 ("movw", "ax,sp");
  adjustAX (stack_offset);

  if (preserve == K78K0_PRESERVE_AX)
    emit2 ("xchw", "ax,hl");
  else
    {
      moveAXToHL ();
      if (preserve == K78K0_PRESERVE_A)
        emit2 ("mov", "a,%s", scratch);
    }

  G.hl.offset_valid = true;
  G.hl.sp_offset = stack_offset;
}

static void
ensureStackAddress (const int stack_offset, const stack_address_preservation preserve,
                    const char *scratch)
{
  if (G.hl.offset_valid && G.hl.sp_offset == stack_offset)
    return;
  setStackAddress (stack_offset, preserve, scratch);
}

static bool
stackByteIndex (const symbol *sym, const int offset,
                const stack_address_preservation preserve, const char *scratch,
                unsigned *index)
{
  const int stack_offset = stackByteOffset (sym, offset);
  int base;
  int delta;

  if (!sym->onStack || stack_offset < 0)
    return false;
  if (G.hl.offset_valid && stack_offset >= G.hl.sp_offset)
    {
      delta = stack_offset - G.hl.sp_offset;
      if (delta <= 255)
        {
          *index = (unsigned)delta;
          return true;
        }
    }

  base = stack_offset > 255 ? stack_offset & ~0xff : 0;
  setStackAddress (base, preserve, scratch);
  *index = (unsigned)(stack_offset - base);
  return true;
}

static int
k78k0_operandSize (const operand *op)
{
  return op && IS_ITEMP (op) && op->isaddr ? 2 : getSize (operandType (op));
}

static bool
isScalarSize (const int size)
{
  return size >= 1 && size <= K78K0_MAX_SCALAR_BYTES;
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

static sym_link *
functionType (sym_link *type)
{
  return type && IS_FUNCPTR (type) ? type->next : type;
}

static const asmop *
aopReturnForSize (const int size)
{
  switch (size)
    {
    case 1:
      return &asmop_a;
    case 2:
      return &asmop_ax;
    case 3:
      return &asmop_cax;
    case 4:
      return &asmop_bcax;
    default:
      return NULL;
    }
}

static const asmop *
aopRet (sym_link *type)
{
  sym_link *ftype = functionType (type);

  if (!ftype || !IS_FUNC (ftype) || !ftype->next || IS_VOID (ftype->next) ||
      IS_STRUCT (ftype->next))
    return NULL;
  return aopReturnForSize (getSize (ftype->next));
}

static const asmop *
aopArg (sym_link *type, const int index)
{
  sym_link *ftype = functionType (type);
  value *arg;

  if (!ftype || !IS_FUNC (ftype) || index < 1)
    return NULL;

  arg = FUNC_ARGS (ftype);
  for (int current = 1; arg && current < index; current++)
    arg = arg->next;

  if (!arg || !SPEC_REGPARM (arg->etype) || IS_STRUCT (arg->type))
    return NULL;
  return aopReturnForSize (getSize (arg->type));
}

static bool
typeReturnsViaHiddenPointer (sym_link *type)
{
  sym_link *ftype = functionType (type);

  return ftype && ftype->next && !IS_VOID (ftype->next) && !aopRet (ftype);
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
      moveAXToHL ();
      G.hl.offset_valid = true;
      G.hl.sp_offset = 0;
    }
}

static void
adjustStackPointer (const int amount, const bool leave_hl_sp)
{
  if (!amount)
    return;

  adjustHardwareStackPointer (amount, leave_hl_sp);

  G.stack.pushed -= amount;
  wassertl (G.stack.pushed >= 0, "78K0 outgoing stack accounting underflow.");
}

static void
pushPSW (void)
{
  emit2 ("push", "psw");
  G.stack.pushed++;
  clearHLState ();
}

static void
popPSW (void)
{
  emit2 ("pop", "psw");
  G.stack.pushed--;
  wassertl (G.stack.pushed >= 0, "78K0 PSW stack accounting underflow.");
  clearHLState ();
}

static void
setReturnResult (const operand *op, const int size)
{
  asmop destination;
  const asmop *source = aopReturnForSize (size);

  if (IS_ITEMP (op) && OP_SYMBOL_CONST (op)->liveTo <= OP_SYMBOL_CONST (op)->liveFrom)
    return;
  if (source && aopForOperand (&destination, op) && destination.size == size &&
      genMove (&destination, source))
    return;

  if (regalloc_dry_run)
    regalloc_dry_run_failed = true;
  else
    wassertl (IS_ITEMP (op) &&
              OP_SYMBOL_CONST (op)->liveTo <= OP_SYMBOL_CONST (op)->liveFrom,
              "live 78K0 result has no physical storage");
}

static void
setAResult (const operand *op)
{
  setReturnResult (op, 1);
}

static bool
aopInReg (const asmop *aop, const int offset, const int reg)
{
  return aop && offset >= 0 && offset < aop->size && aop->regs[offset] &&
         aop->regs[offset]->rIdx == reg;
}

static const char *
aopPairName (const asmop *aop, const int offset)
{
  if (aopInReg (aop, offset, K78K0_RB0_X_IDX) &&
      aopInReg (aop, offset + 1, K78K0_RB0_A_IDX))
    return "ax";
  if (aopInReg (aop, offset, K78K0_RB0_C_IDX) &&
      aopInReg (aop, offset + 1, K78K0_RB0_B_IDX))
    return "bc";
  if (aopInReg (aop, offset, K78K0_RB0_E_IDX) &&
      aopInReg (aop, offset + 1, K78K0_RB0_D_IDX))
    return "de";
  if (aopInReg (aop, offset, K78K0_RB0_L_IDX) &&
      aopInReg (aop, offset + 1, K78K0_RB0_H_IDX))
    return "hl";
  return NULL;
}

static bool
aopForOperand (asmop *aop, const operand *op)
{
  memset (aop, 0, sizeof (*aop));
  if (!op)
    return false;

  aop->operand = op;
  aop->size = k78k0_operandSize (op);
  if (aop->size > K78K0_MAX_SCALAR_BYTES)
    return false;

  if (IS_OP_LITERAL (op))
    {
      aop->type = K78K0_AOP_LITERAL;
      return true;
    }
  if (!op || !IS_SYMOP (op))
    return false;

  const symbol *sym = OP_SYMBOL_CONST (op);
  bool in_register = false;
  for (int offset = 0; offset < aop->size; offset++)
    if ((aop->regs[offset] = symbolRegisterByte (sym, offset)))
      in_register = true;

  if (in_register)
    {
      aop->type = K78K0_AOP_REGSTK;
      aop->storage = operandStorageSymbol (op);
      return true;
    }

  aop->storage = operandStorageSymbol (op);
  if ((IS_ITEMP (op) && sym->remat) || IS_FUNC (operandType (op)))
    aop->type = K78K0_AOP_IMMEDIATE;
  else if (aop->storage->onStack)
    aop->type = K78K0_AOP_STACK;
  else if (aop->storage->rname[0])
    aop->type = K78K0_AOP_DIRECT;
  else
    aop->type = K78K0_AOP_INVALID;
  return aop->type != K78K0_AOP_INVALID;
}

static void
saveScalarAcrossStackAdjustment (const int size)
{
  if (size == 1)
    emit2 ("mov", "c,a");
  else if (size == 2)
    emit2 ("movw", "bc,ax");
  else if (size >= 3 && size <= K78K0_REGISTER_RETURN_BYTES)
    emit2 ("movw", "de,ax");
}

static void
restoreScalarAcrossStackAdjustment (const int size)
{
  if (size == 1)
    emit2 ("mov", "a,c");
  else if (size == 2)
    emit2 ("movw", "ax,bc");
  else if (size >= 3 && size <= K78K0_REGISTER_RETURN_BYTES)
    emit2 ("movw", "ax,de");
}

static void
resetFunctionState (void)
{
  memset (&G, 0, sizeof G);
}

static k78k0_instruction_traits
fixedInstructionTraits (const unsigned clobbers, const unsigned left,
                        const unsigned right)
{
  const k78k0_instruction_traits traits =
    {clobbers, left, right, 0, 0, 0};

  return traits;
}

static void
allowByteAccumulatorOperands (k78k0_instruction_traits *traits,
                              const int left_size, const int right_size)
{
  if (left_size == 1)
    {
      traits->left = 0;
      traits->left_if_right_spilled |= K78K0_MASK_AX;
    }
  if (right_size == 1)
    traits->right = K78K0_MASK_AX;
}

k78k0_instruction_traits
k78k0InstructionTraits (const iCode *ic)
{
  switch (ic->op)
    {
    case FUNCTION:
    case ENDFUNCTION:
    case CRITICAL:
    case ENDCRITICAL:
    case GOTO:
    case LABEL:
      return fixedInstructionTraits (0, K78K0_MASK_ALL, K78K0_MASK_ALL);
    case RETURN:
      return fixedInstructionTraits (K78K0_MASK_HL, 0, K78K0_MASK_ALL);
    case ADDRESS_OF:
      return fixedInstructionTraits (K78K0_MASK_AX, K78K0_MASK_ALL,
                                     K78K0_MASK_ALL);
    case GETBYTE:
    case GETABIT:
      return fixedInstructionTraits (K78K0_MASK_AX, 0, K78K0_MASK_ALL);
    case GETWORD:
      return fixedInstructionTraits (K78K0_MASK_AX | (1u << K78K0_RB0_D_IDX),
                                     0, K78K0_MASK_ALL);
    case DUMMY_READ_VOLATILE:
      return fixedInstructionTraits (K78K0_MASK_AX, 0, 0);
    case IPUSH:
      return fixedInstructionTraits (K78K0_MASK_AX | K78K0_MASK_C | K78K0_MASK_HL,
                                     0, K78K0_MASK_ALL);
    case SEND:
      return fixedInstructionTraits (K78K0_MASK_AX | K78K0_MASK_BC | K78K0_MASK_HL,
                                     0, K78K0_MASK_ALL);
    case RECEIVE:
      return fixedInstructionTraits (K78K0_MASK_AX | K78K0_MASK_BC | K78K0_MASK_HL,
                                     K78K0_MASK_ALL, K78K0_MASK_ALL);
    default:
      break;
    }

  k78k0_instruction_traits traits =
    {K78K0_MASK_ALL, K78K0_MASK_ALL, K78K0_MASK_ALL,
     POINTER_SET (ic) ? K78K0_MASK_ALL : 0, 0, 0};
  const operand *left = IC_LEFT (ic);
  const operand *right = IC_RIGHT (ic);
  const operand *result = IC_RESULT (ic);
  const int left_size = left ? getSize (operandType (left)) : 0;
  const int right_size = right ? getSize (operandType (right)) : 0;
  const int result_size = result ? getSize (operandType (result)) : 0;

  switch (ic->op)
    {
    case '=':
      traits.clobbers = POINTER_SET (ic) ? K78K0_MASK_ALL :
        K78K0_MASK_AX | K78K0_MASK_C;
      if (!POINTER_SET (ic))
        traits.right = 0;
      else if (result_size == 2)
        traits.result = 0;
      break;
    case SET_VALUE_AT_ADDRESS:
      if (left_size == 2)
        traits.left = 0;
      break;
    case GET_VALUE_AT_ADDRESS:
      traits.clobbers = K78K0_MASK_AX | K78K0_MASK_C | K78K0_MASK_DE | K78K0_MASK_HL;
      if (left_size == 2)
        traits.left = 0;
      if (result && IS_BITFIELD (getSpec (operandType (result))))
        {
          sym_link *type = getSpec (operandType (result));

          traits.result = K78K0_MASK_HL;
          if (result_size > 1 && SPEC_BSTR (type))
            traits.result |= K78K0_MASK_BC;
        }
      break;
    case '+':
    case '-':
      if (result_size == 2 &&
          ((right && IS_OP_LITERAL (right) && left_size == 2) ||
           (ic->op == '+' && left && IS_OP_LITERAL (left) && right_size == 2)))
        traits.clobbers = K78K0_MASK_AX;
      else if (result_size == 1)
        traits.clobbers = K78K0_MASK_AX | K78K0_MASK_C;
      allowByteAccumulatorOperands (&traits, left_size, right_size);
      if (left_size == 2 && (result_size == 2 || IS_OP_LITERAL (right)))
        traits.left = 0;
      if (right_size == 2 &&
          (result_size == 2 || ic->op == '+' && IS_OP_LITERAL (left)))
        traits.right = 0;
      break;
    case '*':
      traits.clobbers = result_size == 1 ? K78K0_MASK_AX | K78K0_MASK_C : K78K0_MASK_ALL;
      if (left_size == 2 && IS_OP_LITERAL (right) &&
          operandLitValueUll (right) <= 255)
        traits.left = 0;
      if (right_size == 2 && IS_OP_LITERAL (left) &&
          operandLitValueUll (left) <= 255)
        traits.right = 0;
      allowByteAccumulatorOperands (&traits, left_size, right_size);
      break;
    case LEFT_OP:
    case RIGHT_OP:
      traits.clobbers = result_size == 1 ? K78K0_MASK_AX | K78K0_MASK_BC : K78K0_MASK_ALL;
      if (left_size == 1 || left_size == 2 && result_size == 2)
        traits.left = 0;
      if (right_size == 1)
        traits.right = 0;
      if (!IS_OP_LITERAL (right))
        {
          if (result_size == 2)
            traits.right = K78K0_MASK_DE;
          if (right_size == 1)
            traits.right |= K78K0_MASK_ALL & ~K78K0_MASK_C;
        }
      break;
    case ROT:
      {
        const bool byte_rotate = result_size == 1;
        const bool word_swap = result_size == 2 && right && IS_OP_LITERAL (right) &&
          operandLitValueUll (right) % 16u == 8u;

        traits.clobbers = byte_rotate || word_swap ? K78K0_MASK_AX : K78K0_MASK_ALL;
        if (left_size == 1 || word_swap)
          traits.left = 0;
      }
      if (right_size == 1)
        traits.right = 0;
      break;
    case UNARYMINUS:
      traits.clobbers = result_size <= 2 ? K78K0_MASK_AX : K78K0_MASK_ALL;
      traits.left = 0;
      break;
    case '/':
    case '%':
      traits.clobbers = result_size == 1 ? K78K0_MASK_AX | K78K0_MASK_C : K78K0_MASK_ALL;
      if (left_size == 1)
        traits.left = 0;
      if (right_size == 1)
        traits.right = 0;
      if (!IS_OP_LITERAL (right))
        traits.left = (1u << K78K0_RB0_A_IDX) | K78K0_MASK_C;
      break;
    case BITWISEAND:
    case '|':
    case '^':
      traits.clobbers = result_size == 1 ? K78K0_MASK_AX | K78K0_MASK_C : K78K0_MASK_ALL;
      if (left_size == 2)
        traits.left = 0;
      if (right_size == 2)
        traits.right = 0;
      allowByteAccumulatorOperands (&traits, left_size, right_size);
      break;
    case '!':
      traits.left = 0;
      /* Fall through. */
    case CAST:
      /* Boolean materialization can accumulate a multi-byte or spilled
         two-byte source in B before producing its one-byte result. */
      traits.clobbers = result_size == 1 ? K78K0_MASK_AX | K78K0_MASK_BC : K78K0_MASK_ALL;
      if (ic->op == CAST)
        traits.right = 0;
      break;
    case EQ_OP:
    case NE_OP:
    case '<':
    case '>':
      if (left_size == 2 && right && IS_OP_LITERAL (right))
        traits.clobbers = K78K0_MASK_AX;
      else
        traits.clobbers = left_size == 1 ? K78K0_MASK_AX | K78K0_MASK_C : K78K0_MASK_ALL;
      allowByteAccumulatorOperands (&traits, left_size, right_size);
      if (left_size == 2 && IS_OP_LITERAL (right))
        traits.left = 0;
      if (left_size == 1 && !IS_OP_LITERAL (right) &&
          (ic->op == '<' || ic->op == '>') &&
          !SPEC_USIGN (getSpec (operandType (left))))
        {
          /* The biased comparison keeps the operand loaded first in C. */
          if (ic->op == '<')
            traits.left |= K78K0_MASK_C;
          else
            traits.right |= K78K0_MASK_C;
        }
      if (left_size > 1 && !IS_OP_LITERAL (right))
        {
          traits.left_if_right_spilled |= K78K0_MASK_AX;
          traits.right_if_left_spilled |= K78K0_MASK_AX;
          if ((ic->op == '<' || ic->op == '>') &&
              !SPEC_USIGN (getSpec (operandType (left))))
            traits.left |= traits.right |= K78K0_MASK_BC;
        }
      break;
    case IFX:
      traits.clobbers = !left_size ? K78K0_MASK_ALL :
        left_size == 1 ? K78K0_MASK_AX : K78K0_MASK_AX | K78K0_MASK_B;
      traits.left = 0;
      break;
    case PCALL:
      /* The indirect target cannot share DE with a register argument: PCALL
         parks that argument in DE before loading the target into AX. */
      if (!aopArg (left ? operandType (left) : NULL, 1))
        traits.left = 0;
      break;
    case CALL:
      {
        sym_link *ftype = left ? functionType (operandType (left)) : NULL;
        const int return_bytes = ftype && ftype->next ? getSize (ftype->next) : 0;

        traits.clobbers = K78K0_MASK_AX | K78K0_MASK_BC | K78K0_MASK_HL |
          (ftype && FUNC_HASVARARGS (ftype) && return_bytes > 2 &&
           return_bytes <= K78K0_REGISTER_RETURN_BYTES ? K78K0_MASK_DE : 0);
        break;
      }
    case JUMPTABLE:
      traits.clobbers = K78K0_MASK_AX | K78K0_MASK_C | K78K0_MASK_HL |
        (left_size > 3 ? K78K0_MASK_B : 0);
      traits.left = 0;
      break;
    default:
      break;
    }

  return traits;
}

static bool
functionNeedsDESave (const iCode *function, sym_link *type,
                     const int first_regarg_size)
{
  const int return_size = functionReturnSize (type);

  if (typeReturnsViaHiddenPointer (type) || return_size > 2 ||
      (IFFUNC_ISCRITICAL (type) && first_regarg_size > 2))
    return true;

  for (const iCode *ic = function ? function->next : NULL;
       ic && ic->op != ENDFUNCTION; ic = ic->next)
    {
      if (bitVectBitValue (ic->rMask, K78K0_RB0_E_IDX) ||
          bitVectBitValue (ic->rMask, K78K0_RB0_D_IDX) ||
          (k78k0InstructionTraits (ic).clobbers & K78K0_MASK_DE))
        return true;
    }

  return false;
}

static int
functionStackCleanupBytes (sym_link *type)
{
  sym_link *ftype = functionType (type);
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
functionReturnSize (sym_link *type)
{
  sym_link *ftype = functionType (type);
  sym_link *return_type = ftype && IS_FUNC (ftype) ? ftype->next : NULL;

  return return_type && !IS_VOID (return_type) ? getSize (return_type) : 0;
}

static void
moveReturnAddressForCalleeCleanup (const int cleanup_bytes, const bool use_pops)
{
  clearRegisterState ();
  emit2 ("pop", "hl");
  clearHLState ();
  if (use_pops)
    for (int bytes = 0; bytes < cleanup_bytes; bytes += 2)
      emit2 ("pop", "bc");
  else
    {
      emit2 ("movw", "ax,sp");
      adjustAX (cleanup_bytes);
      emit2 ("movw", "sp,ax");
    }
  emit2 ("push", "hl");
}

static void
loadWordAtHLToAX (void)
{
  emit2 ("mov", "a,[hl+0x00]");
  emit2 ("mov", "x,a");
  emit2 ("mov", "a,[hl+0x01]");
}

static void
emitWideRegisterReturnEpilogue (const int frame_local_size, const int cleanup_bytes)
{
  if (!cleanup_bytes)
    {
      emit2 ("pop", "de");
      clearHLState ();

      if (frame_local_size)
        {
          emit2 ("movw", "hl,ax");
          adjustHardwareStackPointer (frame_local_size, false);
          emit2 ("movw", "ax,hl");
        }
      return;
    }

  const int frame_bytes = frame_local_size + 2;
  emit2 ("movw", "de,ax");

  emit2 ("movw", "ax,sp");
  adjustAX (frame_bytes);
  moveAXToHL ();
  loadWordAtHLToAX ();
  moveAXToHL ();

  emit2 ("movw", "ax,sp");
  adjustAX (frame_bytes + cleanup_bytes + K78K0_RETURN_ADDRESS_BYTES);
  emit2 ("movw", "sp,ax");
  emit2 ("push", "hl");

  emit2 ("movw", "ax,sp");
  adjustAX (-(frame_bytes + cleanup_bytes));
  moveAXToHL ();
  loadWordAtHLToAX ();
  emit2 ("xchw", "ax,de");
}

void
k78k0_emitDebuggerSymbol (const char *debugSym)
{
  genLine.lineElement.isDebug = 1;
  emit2 ("", "%s ==.", debugSym);
  genLine.lineElement.isDebug = 0;
}

static void
genFunction (const iCode *ic)
{
  const symbol *sym = OP_SYMBOL (IC_LEFT (ic));
  sym_link *type = sym->type;
  const int frame_local_size = sym->stack > 0 ? sym->stack : 0;
  const asmop *first_argument = aopArg (type, 1);
  const int first_regarg_size = first_argument ? first_argument->size : 0;

  resetFunctionState ();
  G.stack.saved_de_bytes = functionNeedsDESave (ic, type, first_regarg_size) ? 2 : 0;
  G.stack.local_size = frame_local_size + G.stack.saved_de_bytes;
  emit2 ("", "%s:", sym->rname);
  if (!regalloc_dry_run)
    genLine.lineCurr->isLabel = 1;

  if (IFFUNC_ISNAKED (type))
    {
      emit2 (";", "naked function: no prologue.");
      return;
    }

  if (IFFUNC_ISISR (type))
    {
      emit2 ("push", "ax");
      emit2 ("push", "bc");
      emit2 ("push", "hl");
    }

  if (first_regarg_size > 2)
    moveAXToHL ();
  else
    saveScalarAcrossStackAdjustment (first_regarg_size);

  if (frame_local_size)
    adjustHardwareStackPointer (-frame_local_size, false);

  if (G.stack.saved_de_bytes)
    emit2 ("push", "de");
  if (first_regarg_size && first_regarg_size <= 2)
    ensureStackAddress (0, K78K0_CLOBBER_AX, NULL);
  else
    clearHLState ();

  if (first_regarg_size > 2)
    emit2 ("movw", "ax,hl");
  else
    restoreScalarAcrossStackAdjustment (first_regarg_size);

  if (frame_local_size)
    {
      if (first_regarg_size > 2)
        ensureStackAddress (0, K78K0_PRESERVE_AX, NULL);
      else
        ensureStackAddress (0, K78K0_CLOBBER_AX, NULL);
    }

  if (IFFUNC_ISCRITICAL (type))
    {
      if (first_regarg_size > 2)
        emit2 ("movw", "de,ax");
      else
        saveScalarAcrossStackAdjustment (first_regarg_size);

      genCritical ();
      ensureStackAddress (0, K78K0_CLOBBER_AX, NULL);

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
  sym_link *type = sym->type;
  const int return_size = functionReturnSize (type);
  const int cleanup_size = functionStackCleanupBytes (type);
  const bool is_isr = IFFUNC_ISISR (type);
  const bool register_return = !typeReturnsViaHiddenPointer (type);

  if (IFFUNC_ISNAKED (type))
    {
      resetFunctionState ();
      emit2 (";", "naked function: no epilogue.");
      return;
    }

  if (IFFUNC_ISCRITICAL (type))
    genEndCritical ();

  const int frame_local_size = G.stack.local_size - G.stack.saved_de_bytes;
  wassertl (frame_local_size >= 0, "78K0 invalid local frame size.");

  if (!is_isr && register_return && return_size > 2 &&
      return_size <= K78K0_REGISTER_RETURN_BYTES)
    {
      wassertl (G.stack.saved_de_bytes == 2,
                "78K0 wide return requires a saved DE pair.");
      emitWideRegisterReturnEpilogue (frame_local_size, cleanup_size);
      wassertl (G.stack.pushed == 0, "78K0 unbalanced outgoing stack.");
      resetFunctionState ();
      emit2 ("ret", "");
      return;
    }

  if (frame_local_size && register_return)
    saveScalarAcrossStackAdjustment (return_size);

  if (G.stack.saved_de_bytes)
    {
      emit2 ("pop", "de");
      clearHLState ();
    }

  if (frame_local_size)
    {
      adjustHardwareStackPointer (frame_local_size, false);
      if (register_return)
        restoreScalarAcrossStackAdjustment (return_size);
    }

  if (cleanup_size)
    {
      const bool use_pops = !(cleanup_size % 2) && cleanup_size <= 14 &&
        (!register_return || return_size <= 2);
      const bool preserve_return = register_return && !use_pops;

      if (preserve_return)
        saveScalarAcrossStackAdjustment (return_size);

      moveReturnAddressForCalleeCleanup (cleanup_size, use_pops);
      if (preserve_return)
        restoreScalarAcrossStackAdjustment (return_size);
    }

  wassertl (G.stack.pushed == 0, "78K0 unbalanced outgoing stack.");
  resetFunctionState ();
  if (is_isr)
    {
      emit2 ("pop", "hl");
      clearHLState ();
      emit2 ("pop", "bc");
      emit2 ("pop", "ax");
      emit2 ("reti", "");
    }
  else
    emit2 ("ret", "");
}

static void
genLabel (const iCode *ic)
{
  char label[32];

  /* The function symbol itself denotes the entry point.  Emitting entryLabel
     as well is redundant, and label keys can be reused by the next function. */
  if (IC_LABEL (ic) == entryLabel)
    return;

  clearRegisterState ();
  makeICLabel (label, sizeof (label), IC_LABEL (ic));
  emit2 ("", "%s:", label);
  if (!regalloc_dry_run)
    genLine.lineCurr->isLabel = 1;
}

static void
genGoto (const iCode *ic)
{
  char label[32];

  makeICLabel (label, sizeof (label), IC_LABEL (ic));
  emit2 ("br", "!%s", label);
  clearRegisterState ();
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
  pushPSW ();
  emit2 ("di", "");
}

static void
genEndCritical (void)
{
  clearRegisterState ();
  popPSW ();
}

static void
genLiteralReturnValue (const operand *op)
{
  const unsigned long long value = operandLitValueBits (op);
  const int size = getSize (operandType (op));

  wassertl (size <= K78K0_REGISTER_RETURN_BYTES,
            "78K0 literal return is wider than the supported scalar return size.");
  if (size <= 1)
    emit2 ("mov", "a,#0x%02x", (unsigned)(value & 0xffu));
  else if (size <= 2)
    emit2 ("movw", "ax,#0x%04x", (unsigned)(value & 0xffffu));
  else
    {
      emit2 ("mov", "c,#0x%02x", (unsigned)((value >> 16) & 0xffu));
      if (size == 4)
        emit2 ("mov", "b,#0x%02x", (unsigned)((value >> 24) & 0xffu));
      emit2 ("movw", "ax,#0x%04x", (unsigned)(value & 0xffffu));
    }
}

static bool
absoluteSymbolAddress (const symbol *sym, const int offset, unsigned *address)
{
  unsigned base;

  if (!sym || offset < 0 || !sym->etype || !IS_SPEC (sym->etype) ||
      !SPEC_ABSA (sym->etype))
    return false;

  base = SPEC_ADDR (sym->etype);
  if (base > 0xffffu || (unsigned)offset > 0xffffu - base)
    return false;

  *address = base + (unsigned)offset;
  return true;
}

static bool
isOneByteDirectAddress (const unsigned address)
{
  return (address >= 0xfe20u && address <= 0xff1fu) ||
    (address >= 0xff00u && address <= 0xffcfu) ||
    (address >= 0xffe0u && address <= 0xffffu);
}

typedef enum
{
  K78K0_DIRECT_MOV,
  K78K0_DIRECT_ALU
}
direct_access_kind;

static void
formatByteAddress (char *address, const size_t size, const symbol *sym, const int offset,
                   const direct_access_kind access)
{
  unsigned absolute_address;
  if (absoluteSymbolAddress (sym, offset, &absolute_address))
    {
      const bool short_address = access == K78K0_DIRECT_MOV ?
        isOneByteDirectAddress (absolute_address) :
        absolute_address >= 0xfe20u && absolute_address <= 0xff1fu;

      SNPRINTF (address, size, "%s0x%04x",
                short_address ? "" : "!", absolute_address);
      return;
    }

  /* Arithmetic instructions have an saddr form, but no general SFR form. */
  const char *prefix = access == K78K0_DIRECT_MOV && sym->etype &&
    SPEC_SCLS (sym->etype) == S_SFR ? "" : "!";

  if (offset)
    SNPRINTF (address, size, "%s%s + %d", prefix, sym->rname, offset);
  else
    SNPRINTF (address, size, "%s%s", prefix, sym->rname);
}

static bool
loadSymbolByteToA (const symbol *sym, const int offset)
{
  if (sym->onStack || !sym->rname[0])
    return false;

  char address[SDCC_NAME_MAX + 32];
  formatByteAddress (address, sizeof (address), sym, offset, K78K0_DIRECT_MOV);
  emit2 ("mov", "a,%s", address);
  return true;
}

static bool
loadOperandLowWordToAX (const operand *op)
{
  asmop source;

  if (!aopForOperand (&source, op) || source.size < 2)
    return false;
  return genMove_o (ASMOP_AX, 0, &source, 0, 2);
}

static bool
aopLoadByteToA (const asmop *aop, const int offset)
{
  if (!aop || offset < 0)
    return false;

  if (offset < aop->size && aop->regs[offset])
    {
      const char *source = byteRegisterName (aop->regs[offset]);
      if (strcmp (source, "a"))
        emit2 ("mov", "a,%s", source);
      return true;
    }

  if (aop->type == K78K0_AOP_LITERAL)
    {
      const unsigned long long value = operandLitValueBits (aop->operand);
      emit2 ("mov", "a,#0x%02x", (unsigned)((value >> (offset * 8)) & 0xffu));
      return true;
    }

  if (offset >= aop->size)
    {
      if (!aop->operand || !SPEC_USIGN (getSpec (operandType (aop->operand))))
        return false;

      emit2 ("mov", "a,#0x00");
      return true;
    }

  if (aop->type == K78K0_AOP_IMMEDIATE)
    {
      if (aop->size != 2 || offset > 1 || !loadAddressOperandToAX (aop->operand))
        return false;
      if (offset == 0)
        emit2 ("mov", "a,x");
      return true;
    }

  if (aop->storage && aop->storage->onStack)
    {
      unsigned index;

      if (!stackByteIndex (aop->storage, offset, K78K0_PRESERVE_AX, NULL, &index))
        return false;
      emit2 ("mov", "a,[hl+0x%02x]", index);
      return true;
    }

  return aop->storage && loadSymbolByteToA (aop->storage, offset);
}

static bool
loadOperandByteToA (const operand *op, const int offset)
{
  asmop aop;
  return aopForOperand (&aop, op) && aopLoadByteToA (&aop, offset);
}

static bool
operandByteOnStack (const operand *op, const int offset)
{
  asmop aop;

  if (!aopForOperand (&aop, op) || offset < 0 || offset >= aop.size ||
      aop.regs[offset] || !aop.storage || !aop.storage->onStack)
    return false;
  return stackByteOffset (aop.storage, offset) >= 0;
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
operandAccessPreservesCarry (const operand *op, const int size)
{
  asmop aop;

  if (!aopForOperand (&aop, op))
    return false;
  if (aop.type == K78K0_AOP_LITERAL)
    return true;

  for (int offset = 0; offset < size; offset++)
    {
      if (offset < aop.size && aop.regs[offset])
        continue;
      if (!aop.storage)
        return false;
      if (aop.storage->onStack)
        {
          const int stack_offset = stackByteOffset (aop.storage, offset);
          if (stack_offset >= 0 && stack_offset <= 255)
            continue;
        }
      else if (aop.storage->rname[0])
        continue;

      return false;
    }

  return true;
}

static bool
isUnsignedByteSource (const operand *op)
{
  sym_link *type = operandType (op);

  return getSize (type) == 1 && SPEC_USIGN (getSpec (type));
}

static bool
isUnsignedByteDivisor (const iCode *ic)
{
  operand *const op = IC_RIGHT (ic);

  if (isUnsignedByteSource (op))
    return true;

  if (IS_OP_LITERAL (op))
    return operandLitValueUll (op) <= 0xffu;

  if (!IS_SYMOP (op) || bitVectnBitsOn (OP_DEFS (op)) != 1)
    return false;

  const iCode *def = hTabItemWithKey (iCodehTab, bitVectFirstBit (OP_DEFS (op)));
  return def && def->op == CAST && IC_RIGHT (def) && isUnsignedByteSource (IC_RIGHT (def));
}

static bool
aopALUByteToA (const char *mnemonic, const asmop *aop, const int offset)
{
  if (!aop || offset < 0)
    return false;
  if (aop->type == K78K0_AOP_LITERAL)
    {
      const unsigned long long value = operandLitValueBits (aop->operand);
      emit2 (mnemonic, "a,#0x%02x", (unsigned)((value >> (offset * 8)) & 0xffu));
      return true;
    }

  if (offset >= aop->size)
    {
      if (!aop->operand || !SPEC_USIGN (getSpec (operandType (aop->operand))))
        return false;

      emit2 (mnemonic, "a,#0x00");
      return true;
    }

  if (aop->regs[offset])
    {
      const char *source = byteRegisterName (aop->regs[offset]);

      if (!strcmp (source, "a"))
        return false;
      emit2 (mnemonic, "a,%s", source);
      return true;
    }

  if (aop->type == K78K0_AOP_IMMEDIATE)
    {
      if (aop->size != 2 || offset > 1)
        return false;
      emit2 ("mov", "c,a");
      if (!loadAddressOperandToAX (aop->operand))
        return false;
      if (!offset)
        emit2 ("mov", "a,x");
      emit2 ("mov", "b,a");
      emit2 ("mov", "a,c");
      emit2 (mnemonic, "a,b");
      return true;
    }

  if (aop->storage && aop->storage->onStack)
    {
      unsigned index;

      if (!stackByteIndex (aop->storage, offset, K78K0_PRESERVE_A, "c", &index))
        return false;
      emit2 (mnemonic, "a,[hl+0x%02x]", index);
      return true;
    }

  if (!aop->storage || !aop->storage->rname[0])
    return false;

  char address[SDCC_NAME_MAX + 32];
  formatByteAddress (address, sizeof (address), aop->storage, offset, K78K0_DIRECT_ALU);
  emit2 (mnemonic, "a,%s", address);
  return true;
}

static bool
aluOperandByteToA (const char *mnemonic, const operand *op, const int offset)
{
  asmop aop;

  return aopForOperand (&aop, op) && aopALUByteToA (mnemonic, &aop, offset);
}

static bool
genLiteralBitwiseByteToA (const operand *left, const char *mnemonic, const int offset,
                          const unsigned value)
{
  const bool is_and = !strcmp (mnemonic, "and");
  const bool is_or = !strcmp (mnemonic, "or");

  if ((is_and && value == 0) || (is_or && value == 0xffu))
    {
      emit2 ("mov", "a,#0x%02x", value);
      return true;
    }

  if (!loadOperandByteToA (left, offset))
    return false;

  if ((is_and && value == 0xffu) || (!is_and && value == 0))
    return true;

  emit2 (mnemonic, "a,#0x%02x", value);
  return true;
}

static bool
storeAToSymbolByte (const symbol *sym, const int offset)
{
  if (sym->onStack || !sym->rname[0])
    return false;

  char address[SDCC_NAME_MAX + 32];
  formatByteAddress (address, sizeof (address), sym, offset, K78K0_DIRECT_MOV);
  emit2 ("mov", "%s,a", address);
  return true;
}

static bool
aopStoreByteFromA (const asmop *aop, const int offset)
{
  if (!aop || offset < 0 || offset >= aop->size)
    return false;

  if (aop->regs[offset])
    {
      const char *destination = byteRegisterName (aop->regs[offset]);
      if (strcmp (destination, "a"))
        {
          emit2 ("mov", "%s,a", destination);
          if (registerWritesHL (aop->regs[offset]))
            clearHLState ();
        }
      return true;
    }

  if (aop->storage && aop->storage->onStack)
    {
      unsigned index;

      if (!stackByteIndex (aop->storage, offset, K78K0_PRESERVE_AX, NULL, &index))
        return false;
      emit2 ("mov", "[hl+0x%02x],a", index);
      return true;
    }

  return aop->storage && storeAToSymbolByte (aop->storage, offset);
}

static bool
aopSameByte (const asmop *left, const int left_offset, const asmop *right, const int right_offset)
{
  if (right_offset >= right->size)
    return false;

  const reg_info *left_reg = left->regs[left_offset];
  const reg_info *right_reg = right->regs[right_offset];

  if (left_reg || right_reg)
    return left_reg && left_reg == right_reg;
  if (regalloc_dry_run &&
      (left->storage == &regalloc_dry_spill || right->storage == &regalloc_dry_spill))
    return false;
  return left->storage && left->storage == right->storage && left_offset == right_offset &&
    !IS_VOLATILE (left->storage->type);
}

static bool
aopMoveByte (const asmop *destination, const int destination_offset,
             const asmop *source, const int source_offset)
{
  if (aopSameByte (destination, destination_offset, source, source_offset))
    return true;
  return aopLoadByteToA (source, source_offset) &&
         aopStoreByteFromA (destination, destination_offset);
}

static bool
aopAbsoluteDirectAddress (const asmop *aop, const int offset, const int width,
                          unsigned *address)
{
  if (!aop || !aop->storage || aop->storage->onStack || !aop->storage->rname[0] ||
      offset < 0 || width < 1 || offset + width > aop->size)
    return false;

  for (int byte = 0; byte < width; byte++)
    if (aop->regs[offset + byte])
      return false;

  return absoluteSymbolAddress (aop->storage, offset, address) &&
    *address <= 0x10000u - (unsigned)width;
}

static bool
aopAbsoluteDirectWord (const asmop *aop, const int offset, unsigned *address)
{
  return aopAbsoluteDirectAddress (aop, offset, 2, address) && !(*address & 1u);
}

static bool
aopMoveWord (const asmop *destination, const int destination_offset,
             const asmop *source, const int source_offset)
{
  unsigned destination_address = 0;
  unsigned source_address = 0;
  char address[SDCC_NAME_MAX + 32];
  const bool destination_word =
    aopAbsoluteDirectWord (destination, destination_offset, &destination_address);
  const char *destination_pair = aopPairName (destination, destination_offset);

  if (!destination_word && !destination_pair)
    return false;

  if (source->type == K78K0_AOP_LITERAL && source->operand &&
      source_offset + 2 <= source->size)
    {
      const unsigned long long value =
        operandLitValueBits (source->operand) >> (source_offset * 8);

      if (destination_pair)
        {
          emit2 ("movw", "%s,#0x%04x", destination_pair,
                 (unsigned)(value & 0xffffu));
          if (!strcmp (destination_pair, "hl"))
            clearHLState ();
        }
      else
        {
          formatByteAddress (address, sizeof (address), destination->storage,
                             destination_offset, K78K0_DIRECT_MOV);
          if (isOneByteDirectAddress (destination_address))
            emit2 ("movw", "%s,#0x%04x", address,
                   (unsigned)(value & 0xffffu));
          else
            {
              emit2 ("movw", "ax,#0x%04x", (unsigned)(value & 0xffffu));
              emit2 ("movw", "%s,ax", address);
            }
        }
      return true;
    }

  if (source->type == K78K0_AOP_IMMEDIATE)
    {
      if (source_offset || !source->operand)
        return false;
      if (destination_pair &&
          loadAddressOperandToPair (source->operand, destination_pair, 0))
        return true;
      if (!loadAddressOperandToAX (source->operand))
        return false;
      if (destination_pair)
        movePair (destination_pair, "ax");
      else
        {
          formatByteAddress (address, sizeof (address), destination->storage,
                             destination_offset, K78K0_DIRECT_MOV);
          emit2 ("movw", "%s,ax", address);
        }
      return true;
    }

  const bool source_word =
    aopAbsoluteDirectWord (source, source_offset, &source_address);
  const char *source_pair = aopPairName (source, source_offset);

  /* HL addresses stack operands.  Load a spilled word completely before
     overwriting HL itself.  The second byte load preserves AX if it has to
     move the stack window.  Dry allocation keeps the larger bytewise cost;
     it is conservative and avoids changing unrelated allocation choices. */
  if (!regalloc_dry_run && !source_word && !source_pair && destination_pair &&
      !strcmp (destination_pair, "hl") &&
      source_offset + 2 <= source->size &&
      source->storage && source->storage->onStack &&
      !source->regs[source_offset] && !source->regs[source_offset + 1])
    {
      if (!aopLoadByteToA (source, source_offset))
        return false;
      emit2 ("mov", "x,a");
      if (!aopLoadByteToA (source, source_offset + 1))
        return false;
      movePair (destination_pair, "ax");
      return true;
    }

  if (!source_word && !source_pair)
    return false;

  if (source_pair && destination_pair)
    {
      movePair (destination_pair, source_pair);
      return true;
    }

  if (source_word && destination_word &&
      destination->storage == source->storage &&
      destination_offset == source_offset &&
      !IS_VOLATILE (destination->storage->type))
    return true;

  if (source_pair)
    movePair ("ax", source_pair);
  else
    {
      formatByteAddress (address, sizeof (address), source->storage, source_offset,
                         K78K0_DIRECT_MOV);
      emit2 ("movw", "ax,%s", address);
    }

  if (destination_pair)
    movePair (destination_pair, "ax");
  else
    {
      formatByteAddress (address, sizeof (address), destination->storage,
                         destination_offset, K78K0_DIRECT_MOV);
      emit2 ("movw", "%s,ax", address);
    }

  return true;
}

static bool
genMove_o (const asmop *destination, const int destination_offset,
           const asmop *source, const int source_offset, const int size)
{
  bool pending[K78K0_MAX_SCALAR_BYTES] = {false};
  bool saved_a = false;
  int remaining = size;

  if (!destination || !source || size < 0 || size > K78K0_MAX_SCALAR_BYTES ||
      destination_offset < 0 || source_offset < 0 ||
      destination_offset + size > destination->size || source_offset >= source->size ||
      source_offset + size > K78K0_MAX_SCALAR_BYTES)
    return false;

  if (size == 1 && source->type == K78K0_AOP_LITERAL && source->operand)
    {
      unsigned address;

      if (aopAbsoluteDirectAddress (destination, destination_offset, 1, &address) &&
          isOneByteDirectAddress (address))
        {
          char formatted[SDCC_NAME_MAX + 32];
          const unsigned value = (unsigned)(operandLitValueBits (source->operand) >>
                                             (source_offset * 8));

          formatByteAddress (formatted, sizeof (formatted), destination->storage,
                             destination_offset, K78K0_DIRECT_MOV);
          emit2 ("mov", "%s,#0x%02x", formatted, value & 0xffu);
          return true;
        }
    }
  else if (size == 2 && aopMoveWord (destination, destination_offset,
                                     source, source_offset))
    return true;

  bool may_save_a = false;
  for (int i = 0; i < size; i++)
    may_save_a |= aopInReg (source, source_offset + i, K78K0_RB0_A_IDX) &&
                  aopInReg (destination, destination_offset + i, K78K0_RB0_A_IDX) && size > 1;

  /* Saving A with AX also saves X. Restoring AX is only valid when every
     destination byte in X already denotes the matching source byte. */
  if (may_save_a)
    for (int i = 0; i < size; i++)
      if (aopInReg (destination, destination_offset + i, K78K0_RB0_X_IDX) &&
          !aopInReg (source, source_offset + i, K78K0_RB0_X_IDX))
        return false;

  for (int i = 0; i < size; i++)
    pending[i] = true;

  while (remaining)
    {
      int selected = -1;

      /* A is the transfer accumulator, so consume a source byte in A first. */
      for (int i = 0; i < size; i++)
        if (pending[i] && aopInReg (source, source_offset + i, K78K0_RB0_A_IDX))
          {
            if (aopInReg (destination, destination_offset + i, K78K0_RB0_A_IDX) && remaining > 1)
              {
                emit2 ("push", "ax");
                G.stack.pushed += 2;
                clearHLState ();
                saved_a = true;
                pending[i] = false;
                remaining--;
                break;
              }
            selected = i;
            break;
          }

      if (!remaining)
        break;

      for (int i = 0; selected < 0 && i < size; i++)
        {
          const reg_info *destination_reg;
          bool overwrites_source = false;

          if (!pending[i])
            continue;
          destination_reg = destination->regs[destination_offset + i];
          if (destination_reg && destination_reg->rIdx == K78K0_RB0_A_IDX && remaining > 1)
            continue;

          for (int j = 0; destination_reg && j < size; j++)
            if (i != j && pending[j] && source->regs[source_offset + j] == destination_reg)
              {
                overwrites_source = true;
                break;
              }
          if (!overwrites_source)
            selected = i;
        }

      if (selected < 0 || !aopMoveByte (destination, destination_offset + selected,
                                        source, source_offset + selected))
        break;
      pending[selected] = false;
      remaining--;
    }

  if (saved_a)
    {
      emit2 ("pop", "ax");
      G.stack.pushed -= 2;
      clearHLState ();
    }
  return !remaining;
}

static bool
genMove (const asmop *destination, const asmop *source)
{
  return destination && source && genMove_o (destination, 0, source, 0, destination->size);
}

static bool
storeAToOperandByte (const operand *op, const int offset)
{
  asmop aop;
  return aopForOperand (&aop, op) && aopStoreByteFromA (&aop, offset);
}

static unsigned bitIntTopByteMask (const operand *);

iCode *
k78k0AdjacentAssignment (const iCode *producer)
{
  iCode *assignment = producer ? producer->next : NULL;
  operand *result = producer ? IC_RESULT (producer) : NULL;
  operand *right = assignment ? IC_RIGHT (assignment) : NULL;
  operand *target = assignment ? IC_RESULT (assignment) : NULL;

  if (!result || !IS_ITEMP (result) || !assignment || assignment->op != '=' ||
      POINTER_SET (assignment) || !right || !IS_ITEMP (right) ||
      !target || !IS_SYMOP (target) ||
      OP_SYMBOL_CONST (right) != OP_SYMBOL_CONST (result) ||
      OP_SYMBOL_CONST (result)->liveTo > assignment->seq ||
      getSize (operandType (target)) != k78k0_operandSize (result))
    return NULL;
  return assignment;
}

static operand *
wideAssignmentTarget (const iCode *ic, const operand *result)
{
  iCode *assignment;

  if (!IS_ITEMP (result))
    return NULL;

  if (operandHasAllocatedByte (result))
    return (operand *)result;

  if ((assignment = k78k0AdjacentAssignment (ic)))
    return IC_RESULT (assignment);

  const symbol *storage = operandStorageSymbol (result);
  return storage != OP_SYMBOL_CONST (result) && (storage->onStack || storage->rname[0]) ? (operand *)result : NULL;
}

static bool
producerCanFuseAssignment (const iCode *ic, const int size)
{
  switch (ic->op)
    {
    case CALL:
    case PCALL:
      return isScalarSize (size) &&
        !typeReturnsViaHiddenPointer (functionType (operandType (IC_LEFT (ic))));
    case CAST:
      if (IC_RIGHT (ic))
        {
          const int right_size = k78k0_operandSize (IC_RIGHT (ic));

          return size > 2 && isScalarSize (size) && isScalarSize (right_size) &&
            (size != right_size || bitIntTopByteMask (IC_RESULT (ic)) != 0xffu);
        }
      return false;
    case '+':
    case '-':
    case BITWISEAND:
    case '|':
    case '^':
    case UNARYMINUS:
    case LEFT_OP:
    case RIGHT_OP:
      return size > 2 && isScalarSize (size);
    case GET_VALUE_AT_ADDRESS:
      return size > 2 && isScalarSize (size) &&
        !IS_BITFIELD (getSpec (operandType (IC_RESULT (ic))));
    case ROT:
      return (size == 2 || size == 4) && IS_OP_LITERAL (IC_RIGHT (ic)) &&
        !(size == 2 && operandLitValueUll (IC_RIGHT (ic)) % 16u == 8u);
    default:
      return false;
    }
}

static bool
assignmentFusedByPrevious (const iCode *ic)
{
  iCode *producer = ic && ic->op == '=' && !POINTER_SET (ic) ? ic->prev : NULL;
  iCode *assignment = k78k0AdjacentAssignment (producer);
  operand *producer_result = producer ? IC_RESULT (producer) : NULL;
  const int size = producer_result ? k78k0_operandSize (producer_result) : 0;

  return assignment == ic && producer_result && !producer->generated &&
    producerCanFuseAssignment (producer, size) &&
    wideAssignmentTarget (producer, producer_result) == IC_RESULT (ic);
}

static bool
finishWideAssignment (const iCode *ic, const operand *result, const operand *target)
{
  if (target && target != result)
    markGenerated (k78k0AdjacentAssignment (ic));
  return true;
}

static bool
genAssign (const iCode *ic)
{
  operand *result = IC_RESULT (ic);
  operand *right = IC_RIGHT (ic);
  asmop destination, source;

  if (!IS_SYMOP (result) || !right)
    return false;

  const int size = k78k0_operandSize (result);
  if (IS_ITEMP (result) &&
      OP_SYMBOL_CONST (result)->liveTo <= OP_SYMBOL_CONST (result)->liveFrom)
    return true;
  return aopForOperand (&destination, result) && destination.size == size &&
    aopForOperand (&source, right) && genMove (&destination, &source);
}

static bool
testOperandForZero (const operand *op)
{
  const int size = k78k0_operandSize (op);
  const bool is_float = IS_FLOAT (getSpec (operandType (op)));

  if (!isScalarSize (size))
    return false;

  if (size == 2 && operandHasAllocatedByte (op))
    {
      if (!loadOperandLowWordToAX (op))
        return false;
      emit2 ("cmpw", "ax,#0x0000");
      return true;
    }

  if (operandNeedsStackHL (op, size))
    ensureStackAddress (0, K78K0_CLOBBER_AX, NULL);

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
setBooleanResult (const operand *result)
{
  const int size = k78k0_operandSize (result);

  if (size <= 1)
    setAResult (result);
  else
    {
      emit2 ("mov", "x,a");
      emit2 ("mov", "a,#0x00");
      setReturnResult (result, size);
    }
}

static void
setBooleanResultFromCarry (const operand *result)
{
  /* MOV preserves CY, so ADDC materializes it as an unsigned byte. */
  emit2 ("mov", "a,#0x00");
  emit2 ("addc", "a,#0x00");
  setBooleanResult (result);
}

static void
materializeZeroTest (const operand *result, const bool invert)
{
  char done_label[32];

  makeLocalLabel (done_label, sizeof (done_label));
  emit2 ("mov", "a,#0x00");
  emit2 (invert ? "bnz" : "bz", "%s", done_label);
  emit2 ("inc", "a");
  emitLocalLabel (done_label);
  setBooleanResult (result);
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
maskUnsignedBitIntTopByteInA (const operand *op)
{
  const unsigned mask = unsignedBitIntTopByteMask (op);

  if (mask != 0xffu)
    emit2 ("and", "a,#0x%02x", mask);
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
genNormalizedCast (const iCode *ic, const operand *result, const operand *right,
                   const int result_size, const unsigned top_byte_mask)
{
  operand *target;

  if (result_size <= 2)
    {
      const bool loaded = result_size == 1 ? loadOperandByteToA (right, 0) :
                                             loadOperandLowWordToAX (right);

      if (!loaded)
        return false;
      if (top_byte_mask != 0xffu)
        normalizeBitIntTopByteInA (result);
      setReturnResult (result, result_size);
      return true;
    }

  target = wideAssignmentTarget (ic, result);
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

  return finishWideAssignment (ic, result, target);
}

static bool
genWideningCast (const iCode *ic, const operand *result, const operand *right,
                 const int result_size, const int right_size, const unsigned top_byte_mask)
{
  operand *target;
  const operand *source;
  const char *source_pair;
  bool stack_target;
  bool save_byte;
  bool save_ax;
  int offset;

  if (result_size == 2)
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

  source = right;
  source_pair = right_size == 2 && IS_SYMOP (source) ?
    symbolRegisterPair (OP_SYMBOL_CONST (source)) : NULL;

  target = wideAssignmentTarget (ic, result);
  if (!target)
    return true;

  stack_target = operandNeedsStackHL (target, result_size);
  save_byte = right_size == 1 && stack_target && operandRegisterByte (source, 0);
  save_ax = source_pair && !strcmp (source_pair, "ax");

  if (save_byte)
    {
      if (!loadOperandByteToA (source, 0))
        return false;
      emit2 ("mov", "d,a");
      if (!storeAToOperandByte (target, 0))
        return false;
    }
  else if (save_ax)
    {
      emit2 ("movw", "de,ax");
      emit2 ("mov", "a,e");
      if (!storeAToOperandByte (target, 0))
        return false;
      emit2 ("mov", "a,d");
      if (!storeAToOperandByte (target, 1))
        return false;
    }
  else
    for (offset = 0; offset < right_size; offset++)
      if (!loadOperandByteToA (source, offset) || !storeAToOperandByte (target, offset))
        return false;

  if (SPEC_USIGN (getSpec (operandType (source))))
    emit2 ("mov", "a,#0x00");
  else if (save_byte || save_ax)
    {
      emit2 ("mov", "a,d");
      emitSignMaskForA ();
    }
  else
    {
      if (!loadOperandByteToA (source, right_size - 1))
        return false;
      emitSignMaskForA ();
    }

  for (offset = right_size; offset < result_size; offset++)
    {
      if (offset == result_size - 1 && top_byte_mask != 0xffu)
        normalizeBitIntTopByteInA (result);
      if (!storeAToOperandByte (target, offset))
        return false;
    }

  return finishWideAssignment (ic, result, target);
}

static bool
genCast (const iCode *ic)
{
  operand *result = IC_RESULT (ic);
  operand *right = IC_RIGHT (ic);
  bool scalar_sizes;
  unsigned top_byte_mask;
  int result_size;
  int right_size;

  if (!IS_ITEMP (result) || !right)
    return false;

  result_size = k78k0_operandSize (result);
  right_size = k78k0_operandSize (right);
  top_byte_mask = bitIntTopByteMask (result);
  scalar_sizes = isScalarSize (result_size) && isScalarSize (right_size);

  if (IS_BOOLEAN (operandType (result)) && !IS_BOOLEAN (operandType (right)))
    {
      if (!testOperandForZero (right))
        return false;
      materializeZeroTest (result, false);
      return true;
    }

  if (scalar_sizes &&
      (result_size < right_size || (result_size == right_size && top_byte_mask != 0xffu)))
    return genNormalizedCast (ic, result, right, result_size, top_byte_mask);

  if (scalar_sizes && right_size < result_size)
    return genWideningCast (ic, result, right, result_size, right_size, top_byte_mask);

  if (scalar_sizes && result_size == right_size)
    return genAssign (ic);
  return OP_SYMBOL_CONST (result)->liveTo <= OP_SYMBOL_CONST (result)->liveFrom;
}

static bool
genBitwiseByteToA (const operand *left, const operand *right,
                   const char *mnemonic, const int offset)
{
  if (IS_OP_LITERAL (right))
    return genLiteralBitwiseByteToA (
      left, mnemonic, offset,
      (unsigned)(operandLitValueUll (right) >> (offset * 8)) & 0xffu);

  if (!loadOperandByteToA (left, offset))
    return false;
  if (operandByteOnStack (right, offset))
    ensureStackAddress (0, K78K0_PRESERVE_AX, NULL);
  return aluOperandByteToA (mnemonic, right, offset);
}

static bool
genWideBinaryAccumulatorOp (const iCode *ic, const operand *result, const operand *left,
                            const operand *right, const int size, const char *low_mnemonic,
                            const char *high_mnemonic, const bool uses_carry)
{
  operand *target = wideAssignmentTarget (ic, result);
  const int right_size = k78k0_operandSize (right);
  const bool literal_right = IS_OP_LITERAL (right);
  bool direct_carry;

  if (!target)
    return true;
  if (k78k0_operandSize (left) != size || right_size > size)
    return false;
  if (right_size != size && !literal_right && !SPEC_USIGN (getSpec (operandType (right))))
    return false;

  direct_carry = uses_carry && operandAccessPreservesCarry (left, size) &&
    operandAccessPreservesCarry (right, right_size) && operandAccessPreservesCarry (target, size);

  if (operandNeedsStackHL (left, size) || operandNeedsStackHL (right, size) ||
      operandNeedsStackHL (target, size))
    ensureStackAddress (0, K78K0_CLOBBER_AX, NULL);

  for (int offset = 0; offset < size; offset++)
    {
      if (uses_carry)
        {
          if (!loadOperandByteToA (left, offset))
            return false;

          if (!literal_right)
            {
              emit2 ("mov", "c,a");
              if (!loadOperandByteToA (right, offset))
                return false;
              emit2 ("mov", "b,a");
              emit2 ("mov", "a,c");
            }

          if (offset && !direct_carry)
            {
              popPSW ();
            }

          if (literal_right)
            {
              if (!aluOperandByteToA (offset ? high_mnemonic : low_mnemonic, right, offset))
                return false;
            }
          else
            emit2 (offset ? high_mnemonic : low_mnemonic, "a,b");
        }
      else
        {
          const char *mnemonic = offset ? high_mnemonic : low_mnemonic;

          if (!genBitwiseByteToA (left, right, mnemonic, offset))
            return false;
        }

      if (offset == size - 1)
        maskUnsignedBitIntTopByteInA (result);

      if (uses_carry && !direct_carry && offset != size - 1)
        {
          pushPSW ();
        }

      if (!storeAToOperandByte (target, offset))
        return false;
    }

  return finishWideAssignment (ic, result, target);
}

static bool
genBinaryAccumulatorOp (const iCode *ic, const char *low_mnemonic, const char *high_mnemonic)
{
  operand *result = IC_RESULT (ic);
  operand *left = IC_LEFT (ic);
  operand *right = IC_RIGHT (ic);
  const bool carry_add = !strcmp (high_mnemonic, "addc");
  const bool carry_sub = !strcmp (high_mnemonic, "subc");
  const bool uses_carry = carry_add || carry_sub;
  int size;

  if (!IS_ITEMP (result) || !left || !right)
    return false;

  size = k78k0_operandSize (result);
  if (!isScalarSize (size))
    return false;
  if (size == 2)
    return !uses_carry && genWordBinaryOp (ic, low_mnemonic, high_mnemonic, true);

  if (size > 2)
    return genWideBinaryAccumulatorOp (ic, result, left, right, size, low_mnemonic,
                                       high_mnemonic, uses_carry);

  if (operandNeedsStackHL (right, size) && !operandNeedsStackHL (left, size))
    ensureStackAddress (0, K78K0_CLOBBER_AX, NULL);

  if (!uses_carry && IS_OP_LITERAL (right))
    {
      if (!genLiteralBitwiseByteToA (left, low_mnemonic, 0,
                                     (unsigned)operandLitValueUll (right) & 0xffu))
        return false;
    }
  else
    {
      if (!loadOperandByteToA (left, 0))
        return false;
      if (operandByteOnStack (right, 0))
        ensureStackAddress (0, K78K0_PRESERVE_AX, NULL);
    }

  if (size == 1 && IS_OP_LITERAL (right) && operandLitValueUll (right) == 1u && uses_carry)
    emit2 (carry_add ? "inc" : "dec", "a");
  else if ((uses_carry || !IS_OP_LITERAL (right)) && !aluOperandByteToA (low_mnemonic, right, 0))
    return false;

  maskUnsignedBitIntTopByteInA (result);
  setAResult (result);
  return true;
}

static bool
operandUsesPair (const operand *op, const char *pair)
{
  const char *allocated_pair = IS_SYMOP (op) ? symbolRegisterPair (OP_SYMBOL_CONST (op)) : NULL;

  return allocated_pair && !strcmp (allocated_pair, pair);
}

static bool
operandUsesRegisters (const operand *op, const unsigned mask)
{
  if (!IS_SYMOP (op))
    return false;

  const symbol *sym = OP_SYMBOL_CONST (op);
  for (int byte = 0; byte < sym->nRegs && byte < K78K0_MAX_SCALAR_BYTES; byte++)
    if (sym->regs[byte] && (mask & (1u << sym->regs[byte]->rIdx)))
      return true;
  return false;
}

static bool
loadOperandWordToAX (const operand *op)
{
  const int size = k78k0_operandSize (op);

  if (size >= 2)
    return loadOperandLowWordToAX (op);
  if (size != 1 || !loadOperandByteToA (op, 0))
    return false;

  emit2 ("mov", "x,a");
  if (SPEC_USIGN (getSpec (operandType (op))))
    emit2 ("mov", "a,#0x00");
  else
    emitSignMaskForA ();
  return true;
}

static bool
loadOperandWordToPair (const operand *op, const char *pair)
{
  asmop source;

  if (k78k0_operandSize (op) >= 2 && aopForOperand (&source, op))
    {
      const char *source_pair = aopPairName (&source, 0);

      if (source_pair && !strcmp (source_pair, pair))
        return true;
      if (source.type == K78K0_AOP_LITERAL)
        {
          emit2 ("movw", "%s,#0x%04x", pair,
                 (unsigned)(operandLitValueBits (op) & 0xffffu));
          return true;
        }
      if (source.type == K78K0_AOP_IMMEDIATE &&
          loadAddressOperandToPair (op, pair, 0))
        return true;
    }

  if (!loadOperandWordToAX (op))
    return false;
  emit2 ("movw", "%s,ax", pair);
  return true;
}

static bool
genWordBinaryOp (const iCode *ic, const char *low_mnemonic, const char *high_mnemonic,
                 const bool commutative)
{
  operand *result = IC_RESULT (ic);
  operand *left = IC_LEFT (ic);
  operand *right = IC_RIGHT (ic);
  const int left_size = k78k0_operandSize (left);
  const int right_size = k78k0_operandSize (right);

  if (!IS_ITEMP (result) || k78k0_operandSize (result) != 2)
    return false;
  if (!isScalarSize (left_size) || !isScalarSize (right_size))
    return false;

  if (commutative && (operandUsesRegisters (left, K78K0_MASK_BC) ||
                      operandUsesRegisters (right, K78K0_MASK_DE | K78K0_MASK_AX)))
    {
      operand *swap = left;
      left = right;
      right = swap;
    }

  if (operandUsesRegisters (left, K78K0_MASK_BC) ||
      operandUsesRegisters (right, K78K0_MASK_DE | K78K0_MASK_AX))
    {
      const bool right_in_ax = operandUsesRegisters (right, K78K0_MASK_AX);
      operand *first = right_in_ax ? right : left;
      operand *second = right_in_ax ? left : right;

      if (!loadOperandWordToAX (first))
        return false;
      emit2 ("push", "ax");
      G.stack.pushed += 2;
      clearHLState ();
      if (!loadOperandWordToPair (second, right_in_ax ? "de" : "bc"))
        {
          emit2 ("pop", "ax");
          G.stack.pushed -= 2;
          clearHLState ();
          return false;
        }
      emit2 ("pop", "ax");
      G.stack.pushed -= 2;
      clearHLState ();
      emit2 ("movw", right_in_ax ? "bc,ax" : "de,ax");
    }
  else
    {
      if (!loadOperandWordToPair (left, "de"))
        return false;
      if (!loadOperandWordToPair (right, "bc"))
        return false;
    }

  emit2 ("mov", "a,e");
  emit2 (low_mnemonic, "a,c");
  emit2 ("mov", "x,a");
  emit2 ("mov", "a,d");
  emit2 (high_mnemonic, "a,b");

  maskUnsignedBitIntTopByteInA (result);
  setReturnResult (result, 2);
  return true;
}

static bool
genWordLiteralOffset (const iCode *ic, const bool subtract)
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
  else if (!subtract && IS_OP_LITERAL (left) && k78k0_operandSize (right) == 2)
    {
      base = right;
      literal = left;
    }
  else
    return false;

  offset = (long long)operandLitValue (literal);
  if (subtract)
    offset = -offset;
  if (offset < -0xffffll || offset > 0xffffll)
    return false;

  if (!genOperandReturnValue (base))
    return false;

  adjustAX ((int)offset);

  maskUnsignedBitIntTopByteInA (result);

  setReturnResult (result, 2);
  return true;
}

static bool
genAddSub (const iCode *ic, const bool subtract)
{
  if (genWordLiteralOffset (ic, subtract))
    return true;

  if (IC_RESULT (ic) && k78k0_operandSize (IC_RESULT (ic)) == 2)
    return genWordBinaryOp (ic, subtract ? "sub" : "add",
                            subtract ? "subc" : "addc", !subtract);

  return genBinaryAccumulatorOp (ic, subtract ? "sub" : "add", subtract ? "subc" : "addc");
}

static bool
genUnaryMinus (const iCode *ic)
{
  operand *result = IC_RESULT (ic);
  operand *left = IC_LEFT (ic);
  operand *target;
  bool is_float;
  int size;

  if (!IS_ITEMP (result) || !left)
    return false;

  size = k78k0_operandSize (result);
  if (!isScalarSize (size) || k78k0_operandSize (left) != size)
    return false;

  is_float = IS_FLOAT (getSpec (operandType (result)));
  if (is_float || size > 2)
    {
      target = wideAssignmentTarget (ic, result);
      if (!target)
        return true;

      const bool direct_carry = is_float ||
        (operandAccessPreservesCarry (left, size) &&
         operandAccessPreservesCarry (target, size));

      if (operandNeedsStackHL (left, size) || operandNeedsStackHL (target, size))
        ensureStackAddress (0, K78K0_CLOBBER_AX, NULL);

      for (int offset = 0; offset < size; offset++)
        {
          if (!loadOperandByteToA (left, offset))
            return false;

          if (is_float)
            {
              if (offset == size - 1)
                emit2 ("xor", "a,#0x80");
            }
          else
            {
              emit2 ("xor", "a,#0xff");
              if (offset && !direct_carry)
                {
                  popPSW ();
                }
              emit2 (offset ? "addc" : "add", offset ? "a,#0x00" : "a,#0x01");
              if (offset != size - 1 && !direct_carry)
                {
                  pushPSW ();
                }
            }

          if (!storeAToOperandByte (target, offset))
            return false;
        }

      return finishWideAssignment (ic, result, target);
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
  int size;

  if (!IS_ITEMP (result) || !left || !right)
    return false;

  if (IS_OP_LITERAL (left) && !IS_OP_LITERAL (right))
    {
      operand *temporary = left;
      left = right;
      right = temporary;
    }

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

  if (operandNeedsStackHL (left, 1) || operandNeedsStackHL (right, 1))
    ensureStackAddress (0, K78K0_CLOBBER_AX, NULL);

  if (IS_OP_LITERAL (right))
    {
      if (!loadOperandByteToA (left, 0))
        return false;
      emit2 ("mov", "x,#0x%02x", (unsigned)operandLitValueUll (right) & 0xffu);
    }
  else
    {
      const reg_info *left_reg = operandRegisterByte (left, 0);
      const reg_info *right_reg = operandRegisterByte (right, 0);
      const bool left_in_c = left_reg && left_reg->rIdx == K78K0_RB0_C_IDX;
      const bool right_in_c = right_reg && right_reg->rIdx == K78K0_RB0_C_IDX;

      if (left_in_c || right_in_c)
        {
          if (!loadOperandByteToA (left_in_c ? right : left, 0))
            return false;
          emit2 ("mov", "x,a");
          emit2 ("mov", "a,c");
        }
      else
        {
          if (!loadOperandByteToA (left, 0))
            return false;
          emit2 ("mov", "c,a");
          if (!loadOperandByteToA (right, 0))
            return false;
          emit2 ("mov", "x,a");
          emit2 ("mov", "a,c");
        }
    }

  emit2 ("mulu", "x");

  if (size == 1)
    emit2 ("mov", "a,x");
  maskUnsignedBitIntTopByteInA (result);
  setReturnResult (result, size);

  return true;
}

static bool
compareOperandBytes (const operand *left, const operand *right, const int offset, const bool biased)
{
  if (biased)
    {
      if (!loadOperandByteToA (right, offset))
        return false;
      emit2 ("xor", "a,#0x80");
      emit2 ("mov", "c,a");
    }

  if (!loadOperandByteToA (left, offset))
    return false;

  if (biased)
    {
      emit2 ("xor", "a,#0x80");
      emit2 ("cmp", "a,c");
      return true;
    }
  if (operandByteOnStack (right, offset))
    ensureStackAddress (0, K78K0_PRESERVE_A, "c");

  return aluOperandByteToA ("cmp", right, offset);
}

static void
prepareComparisonLabels (iCode *ifx, char *true_label, char *false_label, char *done_label, size_t label_size)
{
  if (!ifx)
    {
      makeLocalLabel (true_label, label_size);
      makeLocalLabel (false_label, label_size);
      makeLocalLabel (done_label, label_size);
    }
  else if (IC_TRUE (ifx))
    {
      makeICLabel (true_label, label_size, IC_TRUE (ifx));
      makeLocalLabel (false_label, label_size);
    }
  else
    {
      wassertl (IC_FALSE (ifx), "78K0 comparison IFX has no target.");
      makeLocalLabel (true_label, label_size);
      makeICLabel (false_label, label_size, IC_FALSE (ifx));
    }
}

static void
finishComparison (const operand *result, iCode *ifx, const char *true_label, const char *false_label,
                  const char *done_label)
{
  if (!ifx)
    {
      emitLocalLabel (false_label);
      emit2 ("mov", "a,#0x00");
      emit2 ("br", "%s", done_label);
      emitLocalLabel (true_label);
      emit2 ("mov", "a,#0x01");
      emitLocalLabel (done_label);
      setBooleanResult (result);
      return;
    }

  emitLocalLabel (IC_TRUE (ifx) ? false_label : true_label);
  markGenerated (ifx);
}

static bool
genNot (const iCode *ic)
{
  operand *result = IC_RESULT (ic);
  operand *left = IC_LEFT (ic);

  if (!IS_ITEMP (result) || !left || k78k0_operandSize (result) < 1 || k78k0_operandSize (result) > 2)
    return false;

  if (k78k0_operandSize (left) == 1)
    {
      if (!loadOperandByteToA (left, 0))
        return false;
      /* For an unsigned byte, A < 1 is exactly A == 0. */
      emit2 ("cmp", "a,#0x01");
      setBooleanResultFromCarry (result);
      return true;
    }

  if (!testOperandForZero (left))
    return false;

  materializeZeroTest (result, true);
  return true;
}

static bool
genCmpEqNe (const iCode *ic, iCode *ifx)
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
  if (!isScalarSize (size) || k78k0_operandSize (right) != size)
    return false;

  prepareComparisonLabels (ifx, true_label, false_label, done_label, sizeof (true_label));

  if (size > 2 && IS_OP_LITERAL (right) && operandLitValueBits (right) == 0)
    {
      if (!testOperandForZero (left))
        return false;
    }
  else if (size <= 2 && IS_OP_LITERAL (right))
    {
      const unsigned literal = (unsigned)operandLitValueBits (right);

      if (size == 1)
        {
          if (!loadOperandByteToA (left, 0))
            return false;
          emit2 ("cmp", "a,#0x%02x", literal & 0xffu);
        }
      else
        {
          if (!loadOperandLowWordToAX (left))
            return false;
          emit2 ("cmpw", "ax,#0x%04x", literal & 0xffffu);
        }
    }
  else
    for (int offset = 0; offset < size; offset++)
      {
        if (!compareOperandBytes (left, right, offset, false))
          return false;
        if (offset + 1 < size)
          emitCondBranch ("bnz", is_ne ? true_label : false_label);
      }

  emitCondBranch ("bnz", is_ne ? true_label : false_label);
  emit2 ("br", "!%s", is_ne ? false_label : true_label);

  finishComparison (result, ifx, true_label, false_label, done_label);
  return true;
}

static bool
genCmpLtGt (const iCode *ic, iCode *ifx)
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
  if (!isScalarSize (size) || k78k0_operandSize (right) != size)
    return false;

  prepareComparisonLabels (ifx, true_label, false_label, done_label, sizeof (true_label));

  if (size <= 2 && IS_OP_LITERAL (right))
    {
      const unsigned mask = size == 1 ? 0xffu : 0xffffu;
      unsigned literal = (unsigned)operandLitValueBits (right) & mask;
      const bool is_sign_test = is_signed && ((!is_gt && literal == 0) || (is_gt && literal == mask));

      if (is_sign_test)
        {
          const bool true_when_set = !is_gt;

          if (!loadOperandByteToA (left, size - 1))
            return false;
          if (!ifx)
            {
              emit2 ("mov1", "cy,a.7");
              if (!true_when_set)
                emit2 ("not1", "cy");
              setBooleanResultFromCarry (result);
              return true;
            }

          const bool target_is_true = IC_TRUE (ifx);
          emitABitBranch (target_is_true == true_when_set, 7,
                          target_is_true ? true_label : false_label);
          emit2 ("br", "!%s", target_is_true ? false_label : true_label);
          finishComparison (result, ifx, true_label, false_label, done_label);
          return true;
        }

      if (size == 1 ? !loadOperandByteToA (left, 0) : !loadOperandLowWordToAX (left))
        return false;
      if (is_signed)
        {
          emit2 ("xor", "a,#0x80");
          literal ^= size == 1 ? 0x80u : 0x8000u;
        }
      if (size == 1)
        emit2 ("cmp", "a,#0x%02x", literal);
      else
        emit2 ("cmpw", "ax,#0x%04x", literal);
      if (!ifx && !is_gt)
        {
          setBooleanResultFromCarry (result);
          return true;
        }
      if (is_gt)
        {
          emitCondBranch ("bc", false_label);
          emitCondBranch ("bz", false_label);
          emit2 ("br", "!%s", true_label);
        }
      else
        {
          emitCondBranch ("bc", true_label);
          emit2 ("br", "!%s", false_label);
        }
      finishComparison (result, ifx, true_label, false_label, done_label);
      return true;
    }

  for (int offset = size - 1; offset >= 0; offset--)
    {
      if (is_signed && offset == size - 1)
        {
          if (!compareOperandBytes (is_gt ? right : left, is_gt ? left : right, offset, true))
            return false;
        }
      else if (!compareOperandBytes (is_gt ? right : left, is_gt ? left : right, offset, false))
        return false;

      if (size == 1 && !ifx)
        {
          setBooleanResultFromCarry (result);
          return true;
        }

      emitCondBranch ("bc", true_label);
      if (offset)
        emitCondBranch ("bnz", false_label);
    }

  emit2 ("br", "!%s", false_label);

  finishComparison (result, ifx, true_label, false_label, done_label);
  return true;
}

static bool
pushBigReturnAddress (const operand *result)
{
  if (!result || !IS_SYMOP (result))
    return false;

  if (!loadSymbolAddressToAX (operandStorageSymbol (result), 0))
    return false;

  emit2 ("push", "ax");
  G.stack.pushed += 2;
  clearRegisterState ();
  return true;
}

iCode *
k78k0HiddenReturnForwardBridge (iCode *call, sym_link *enclosing_ftype)
{
  iCode *bridge = call ? call->next : NULL;
  operand *call_result = call ? IC_RESULT (call) : NULL;
  sym_link *callee_ftype = call && IC_LEFT (call) ? functionType (operandType (IC_LEFT (call))) : NULL;
  sym_link *callee_return = callee_ftype && IS_FUNC (callee_ftype) ? callee_ftype->next : NULL;
  sym_link *enclosing_return = enclosing_ftype && IS_FUNC (enclosing_ftype) ? enclosing_ftype->next : NULL;

  if (!call || (call->op != CALL && call->op != PCALL) || !call_result ||
      !IS_SYMOP (call_result) || IS_VOLATILE (operandType (call_result)) ||
      bitVectnBitsOn (OP_USES (call_result)) != 1 || !callee_return || !enclosing_return ||
      getSize (callee_return) != getSize (enclosing_return) ||
      getSize (operandType (call_result)) != getSize (callee_return) ||
      compareType (callee_return, enclosing_return, false) != 1 ||
      compareType (operandType (call_result), callee_return, false) != 1)
    return NULL;

  if (!IS_STRUCT (callee_return) && getSize (callee_return) > K78K0_REGISTER_RETURN_BYTES &&
      bridge && bridge->op == RETURN && IC_LEFT (bridge) && IS_SYMOP (IC_LEFT (bridge)) &&
      OP_SYMBOL_CONST (IC_LEFT (bridge)) == OP_SYMBOL_CONST (call_result))
    return bridge;

  if (IS_STRUCT (callee_return) && bridge && bridge->op == ADDRESS_OF &&
      IC_LEFT (bridge) && IS_SYMOP (IC_LEFT (bridge)) &&
      OP_SYMBOL_CONST (IC_LEFT (bridge)) == OP_SYMBOL_CONST (call_result) &&
      IC_RIGHT (bridge) && IS_OP_LITERAL (IC_RIGHT (bridge)) &&
      operandLitValueUll (IC_RIGHT (bridge)) == 0 && IC_RESULT (bridge) &&
      IS_ITEMP (IC_RESULT (bridge)) && bitVectnBitsOn (OP_USES (IC_RESULT (bridge))) == 1 &&
      bridge->next && bridge->next->op == RETURN && IC_LEFT (bridge->next) &&
      IS_SYMOP (IC_LEFT (bridge->next)) &&
      OP_SYMBOL_CONST (IC_LEFT (bridge->next)) == OP_SYMBOL_CONST (IC_RESULT (bridge)))
    return bridge;

  return NULL;
}

typedef struct
{
  iCode *ic;
  operand *callee;
  operand *result;
  sym_link *ftype;
  iCode *forward_bridge;
  int first_regarg_size;
  bool hidden_return;
}
call_plan;

static bool
makeCallPlan (call_plan *plan, iCode *ic, const int op)
{
  if (!plan || !ic || ic->op != op || !IC_LEFT (ic))
    return false;
  if (op == PCALL && k78k0_operandSize (IC_LEFT (ic)) != 2)
    return false;

  plan->ic = ic;
  plan->callee = IC_LEFT (ic);
  plan->result = IC_RESULT (ic);
  plan->ftype = functionType (operandType (plan->callee));
  plan->hidden_return = typeReturnsViaHiddenPointer (plan->ftype);
  const asmop *first_argument = aopArg (plan->ftype, 1);
  plan->first_regarg_size = first_argument ? first_argument->size : 0;
  plan->forward_bridge = plan->hidden_return ?
    k78k0HiddenReturnForwardBridge (ic, currFunc ? currFunc->type : NULL) : NULL;
  return !plan->hidden_return || plan->result;
}

static bool
pushEnclosingHiddenReturnAddress (const bool preserve_ax)
{
  int pointer_offset = G.stack.local_size + K78K0_RETURN_ADDRESS_BYTES + G.stack.pushed;

  if (pointer_offset < 0)
    return false;

  if (preserve_ax)
    {
      emit2 ("push", "ax");
      G.stack.pushed += 2;
      pointer_offset += 2;
      clearHLState ();
    }

  setStackAddress (pointer_offset, K78K0_CLOBBER_AX, NULL);
  loadWordAtHLToAX ();
  if (preserve_ax)
    {
      moveAXToHL ();
      emit2 ("pop", "ax");
      G.stack.pushed -= 2;
      emit2 ("push", "hl");
    }
  else
    emit2 ("push", "ax");

  G.stack.pushed += 2;
  clearRegisterState ();
  return true;
}

static void
finishHiddenReturnForwarding (iCode *address)
{
  char label[32];
  iCode *ret = address->op == ADDRESS_OF ? address->next : address;

  if (address != ret)
    markGenerated (address);
  markGenerated (ret);
  makeICLabel (label, sizeof (label), returnLabel);
  emit2 ("br", "!%s", label);
  clearRegisterState ();
}

static bool
finishCall (const call_plan *plan)
{
  iCode *ic = plan->ic;
  operand *result = plan->result;
  sym_link *ftype = plan->ftype;
  const int cleanup_bytes = ic->parmBytes + (plan->hidden_return ? 2 : 0);
  const asmop *return_location = plan->hidden_return ? NULL : aopRet (ftype);
  const int result_size = !plan->hidden_return && result && IS_ITEMP (result) ?
    k78k0_operandSize (result) : 0;
  const int return_size = return_location ? return_location->size : 0;

  if (cleanup_bytes)
    {
      if (ftype && IS_FUNC (ftype) && !FUNC_HASVARARGS (ftype))
        {
          G.stack.pushed -= cleanup_bytes;
          wassertl (G.stack.pushed >= 0, "78K0 outgoing stack accounting underflow.");
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

      target = wideAssignmentTarget (ic, result);
      if (target)
        {
          asmop destination;
          const bool stored = return_location && aopForOperand (&destination, target) &&
            genMove_o (&destination, 0, return_location, 0, result_size);

          if (stored)
            return finishWideAssignment (ic, result, target);
        }

      if (result_size <= 2)
        {
          if (result_size == 1 && return_size > 1 &&
              !genMove_o (ASMOP_A, 0, return_location, 0, 1))
            return false;
          setReturnResult (result, result_size);
        }
      else if (!IS_ITEMP (result) || OP_SYMBOL_CONST (result)->liveTo > ic->seq)
        return false;
    }
  return true;
}

static bool
finishCallPlan (const call_plan *plan)
{
  if (!finishCall (plan))
    return false;
  if (plan->forward_bridge)
    finishHiddenReturnForwarding (plan->forward_bridge);
  return true;
}

static bool
genCall (iCode *ic)
{
  call_plan plan;

  if (!makeCallPlan (&plan, ic, CALL))
    return false;

  clearRegisterState ();

  if (plan.hidden_return)
    {
      if (plan.first_regarg_size && !plan.forward_bridge)
        moveAXToHL ();
      if (!(plan.forward_bridge ?
            pushEnclosingHiddenReturnAddress (plan.first_regarg_size != 0) :
            pushBigReturnAddress (plan.result)))
        return false;
      if (plan.first_regarg_size && !plan.forward_bridge)
        emit2 ("movw", "ax,hl");
    }

  if (IS_SYMOP (plan.callee))
    {
      const symbol *sym = OP_SYMBOL_CONST (plan.callee);

      if (!sym->rname[0])
        return false;

      emit2 ("call", "!%s", sym->rname);
    }
  else if (IS_OP_LITERAL (plan.callee))
    emit2 ("call", "!0x%04x",
           (unsigned)(operandLitValueUll (plan.callee) & 0xffffu));
  else
    return false;

  clearHLState ();
  return finishCallPlan (&plan);
}

static bool
genPcall (iCode *ic)
{
  call_plan plan;
  char return_label[32];

  if (!makeCallPlan (&plan, ic, PCALL))
    return false;

  if (plan.first_regarg_size)
    emit2 ("movw", "de,ax");
  else if (plan.hidden_return)
    {
      if (!genOperandReturnValue (plan.callee))
        return false;
      emit2 ("movw", "de,ax");
    }

  if (plan.hidden_return)
    {
      if (!(plan.forward_bridge ? pushEnclosingHiddenReturnAddress (false) :
            pushBigReturnAddress (plan.result)))
        return false;
    }

  if (plan.hidden_return && !plan.first_regarg_size)
    emit2 ("movw", "ax,de");
  else if (!genOperandReturnValue (plan.callee))
    return false;

  moveAXToHL ();
  makeLocalLabel (return_label, sizeof (return_label));
  clearRegisterState ();
  emit2 ("movw", "ax,#%s", return_label);
  emit2 ("push", "ax");
  emit2 ("push", "hl");
  if (plan.first_regarg_size)
    emit2 ("movw", "ax,de");
  emit2 ("ret", "");
  emitLocalLabel (return_label);

  return finishCallPlan (&plan);
}

static bool
genIfx (const iCode *ic)
{
  operand *cond = IC_COND (ic);
  symbol *target = IC_FALSE (ic) ? IC_FALSE (ic) : IC_TRUE (ic);
  char label[32];

  if (!cond || !target || !testOperandForZero (cond))
    return false;

  makeICLabel (label, sizeof (label), target);
  emitCondBranch (IC_FALSE (ic) ? "bz" : "bnz", label);

  return true;
}

static bool
genOperandToAop (const operand *op, const asmop *destination)
{
  asmop source;

  const int size = k78k0_operandSize (op);

  if (!destination || destination->size != size)
    return false;
  if (IS_OP_LITERAL (op) && destination == aopReturnForSize (size))
    {
      genLiteralReturnValue (op);
      return true;
    }

  if (!aopForOperand (&source, op))
    return false;
  if (source.type == K78K0_AOP_IMMEDIATE)
    {
      if (size != 2 || !loadAddressOperandToAX (source.operand))
        return false;
      return destination == ASMOP_AX || genMove (destination, ASMOP_AX);
    }
  return genMove (destination, &source);
}

static bool
genOperandReturnValue (const operand *op)
{
  return genOperandToAop (op, aopReturnForSize (k78k0_operandSize (op)));
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
    return IS_SYMOP (IC_RIGHT (ic)) && OP_SYMBOL_CONST (IC_RIGHT (ic))->remat &&
           rematerializedAddress (OP_SYMBOL_CONST (IC_RIGHT (ic))->rematiCode, base, offset);

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
rematerializedOperandAddress (const operand *op, const symbol **base, long *offset)
{
  if (!IS_SYMOP (op) || !IS_ITEMP (op))
    return false;

  const symbol *sym = OP_SYMBOL_CONST (op);
  return sym->remat && rematerializedAddress (sym->rematiCode, base, offset) && *base;
}

static bool
loadAddressOperandToPair (const operand *op, const char *pair, long offset)
{
  const symbol *base = NULL;

  if (IS_OP_LITERAL (op) && k78k0_operandSize (op) == 2)
    {
      const unsigned long long value = operandLitValueBits (op) +
        (unsigned long long)offset;

      emit2 ("movw", "%s,#0x%04x", pair, (unsigned)(value & 0xffffu));
    }
  else
    {
      if (!rematerializedOperandAddress (op, &base, &offset))
        {
          if (!IS_SYMOP (op) || !IS_FUNC (operandType (op)))
            return false;
          base = OP_SYMBOL_CONST (op);
        }
      if (base->onStack)
        return !strcmp (pair, "ax") && loadSymbolAddressToAX (base, offset);
      if (!base->rname[0])
        return false;

      if (!strcmp (pair, "ax"))
        clearRegisterState ();
      if (offset)
        emit2 ("movw", "%s,#%s + %ld", pair, base->rname, offset);
      else
        emit2 ("movw", "%s,#%s", pair, base->rname);
    }

  if (!strcmp (pair, "hl"))
    clearHLState ();
  return true;
}

static bool
loadAddressOperandToAX (const operand *op)
{
  return loadAddressOperandToPair (op, "ax", 0);
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
  return true;
}

static bool
isLiteralAssignment (const operand *op)
{
  if (!IS_ITEMP (op))
    return false;

  const symbol *sym = OP_SYMBOL_CONST (op);
  if (bitVectnBitsOn (sym->defs) != 1)
    return false;

  const iCode *def = hTabItemWithKey (iCodehTab, bitVectFirstBit (sym->defs));
  return def && (def->op == '=' || def->op == CAST) &&
    IC_RIGHT (def) && IS_OP_LITERAL (IC_RIGHT (def));
}

static bool
savePointerToDE (const operand *op, const long offset)
{
  asmop source;

  if (k78k0_operandSize (op) != 2)
    return false;
  /* Final emission can remove any actual DE self-copy.  During dry costing,
     reward only literal definitions; a broader reward distorts spill choices. */
  if (!offset && (!regalloc_dry_run || isLiteralAssignment (op)) &&
      aopForOperand (&source, op) &&
      aopInReg (&source, 0, K78K0_RB0_E_IDX) &&
      aopInReg (&source, 1, K78K0_RB0_D_IDX))
    return true;
  if (loadAddressOperandToPair (op, "de", offset))
    return true;
  if (!genOperandReturnValue (op))
    return false;

  adjustAX ((int)offset);
  emit2 ("movw", "de,ax");
  return true;
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

  if (!loadSymbolAddressToAX (sym, offset))
    return false;

  setReturnResult (result, 2);
  return true;
}

static void
setHLFromDE (void)
{
  emit2 ("movw", "ax,de");
  moveAXToHL ();
}

static bool
genPointerGetBitField (const operand *result, const operand *ptr, long pointer_offset, sym_link *type)
{
  const int bit_start = SPEC_BSTR (type);
  const int bit_length = SPEC_BLEN (type);
  const int result_size = k78k0_operandSize (result);
  const int storage_size = (bit_start + bit_length + 7) / 8;
  const bool sign_extend = !SPEC_USIGN (type) && !IS_BOOLEAN (type);
  const bool keep_pointer_in_hl = !operandNeedsStackHL (result, result_size);

  if (bit_start < 0 || bit_start > 7 || bit_length < 1 || bit_length > K78K0_MAX_SCALAR_BYTES * 8 ||
      !isScalarSize (result_size))
    return false;

  const bool indexed_offset = pointer_offset >= 0 &&
    (unsigned long)pointer_offset + storage_size <= 256u;
  if (!savePointerToDE (ptr, indexed_offset ? 0 : pointer_offset))
    return false;
  if (!indexed_offset)
    pointer_offset = 0;

  if (keep_pointer_in_hl)
    setHLFromDE ();

  for (int byte = 0; byte < result_size; byte++)
    {
      const int remaining_bits = bit_length - byte * 8;

      if (!keep_pointer_in_hl)
        setHLFromDE ();
      emit2 ("mov", "a,[hl+0x%02x]", (unsigned)pointer_offset + (unsigned)byte);

      if (bit_start)
        {
          for (int shift = 0; shift < bit_start; shift++)
            emitByteShift (K78K0_SHIFT_RIGHT);

          if (byte + 1 < storage_size)
            {
              emit2 ("mov", "c,a");
              if (!keep_pointer_in_hl)
                setHLFromDE ();
              emit2 ("mov", "a,[hl+0x%02x]", (unsigned)pointer_offset + (unsigned)byte + 1u);
              for (int shift = bit_start; shift < 8; shift++)
                emitByteShift (K78K0_SHIFT_LEFT);
              emit2 ("or", "a,c");
            }
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
    }

  clearHLState ();
  return true;
}

static bool
genPointerGet (const iCode *ic)
{
  operand *result = IC_RESULT (ic);
  operand *left = IC_LEFT (ic);
  operand *right = IC_RIGHT (ic);
  operand *target;
  sym_link *bit_field_type;
  sym_link *left_type;
  sym_link *result_type;
  long offset;
  int size;

  if (!IS_ITEMP (result) || !left || !IS_OP_LITERAL (right) || ic->op != GET_VALUE_AT_ADDRESS)
    return false;

  size = k78k0_operandSize (result);
  offset = (long)operandLitValue (right);
  if (!isScalarSize (size) || offset < -65535l || offset > 65535l)
    return false;

  left_type = operandType (left);
  result_type = operandType (result);
  /* Aggregate dereferences are represented by their effective address.  Do
     not use result->isaddr here: function-pointer array loads set it too. */
  if (left->isaddr && IS_PTR (left_type) && IS_AGGREGATE (left_type->next) &&
      IS_PTR (result_type) && IS_AGGREGATE (result_type->next))
    {
      if (!savePointerToDE (left, offset))
        return false;
      emit2 ("movw", "ax,de");
      setReturnResult (result, 2);
      return true;
    }

  bit_field_type = getSpec (operandType (result));
  if (IS_BITFIELD (bit_field_type))
    return genPointerGetBitField (result, left, offset, bit_field_type);

  if (!savePointerToDE (left, offset))
    return false;

  if (size == 1)
    {
      emit2 ("mov", "a,[de]");
      setAResult (result);
      return true;
    }

  if (size == 2)
    {
      emit2 ("mov", "a,[de]");
      emit2 ("mov", "x,a");
      emit2 ("incw", "de");
      emit2 ("mov", "a,[de]");
      setReturnResult (result, size);
      return true;
    }

  target = wideAssignmentTarget (ic, result);
  if (!target)
    return true;
  for (int byte = 0; byte < size; byte++)
    {
      emit2 ("mov", "a,[de]");
      if (!storeAToOperandByte (target, byte))
        return false;
      if (byte + 1 < size)
        emit2 ("incw", "de");
    }
  return finishWideAssignment (ic, result, target);
}

static bool
genPointerSetBitField (const operand *ptr, const operand *value, sym_link *type)
{
  const int bit_start = SPEC_BSTR (type);
  const int bit_length = SPEC_BLEN (type);
  const int value_size = k78k0_operandSize (value);
  const int storage_size = (bit_start + bit_length + 7) / 8;

  if (bit_start < 0 || bit_start > 7 || bit_length < 1 || bit_length > K78K0_MAX_SCALAR_BYTES * 8 ||
      !isScalarSize (value_size) || storage_size < 1 || storage_size > 256 ||
      !savePointerToDE (ptr, 0))
    return false;
  for (int byte = 0; byte < storage_size; byte++)
    {
      const int first_bit = byte ? 0 : bit_start;
      const int remaining_end_bit = bit_start + bit_length - byte * 8;
      const int end_bit = remaining_end_bit < 8 ? remaining_end_bit : 8;
      const unsigned field_mask = (((1u << (end_bit - first_bit)) - 1u) << first_bit) & 0xffu;

      if (byte < value_size)
        {
          if (!loadOperandByteToA (value, byte))
            return false;
        }
      else
        emit2 ("mov", "a,#0x00");

      for (int shift = 0; shift < bit_start; shift++)
        emitByteShift (K78K0_SHIFT_LEFT);

      if (bit_start && byte > 0 && byte - 1 < value_size)
        {
          emit2 ("mov", "c,a");
          if (!loadOperandByteToA (value, byte - 1))
            return false;
          for (int shift = bit_start; shift < 8; shift++)
            emitByteShift (K78K0_SHIFT_RIGHT);
          emit2 ("or", "a,c");
        }

      if (field_mask != 0xffu)
        {
          emit2 ("and", "a,#0x%02x", field_mask);
          emit2 ("mov", "b,a");
          emit2 ("mov", "a,[de]");
          emit2 ("and", "a,#0x%02x", (~field_mask) & 0xffu);
          emit2 ("or", "a,b");
        }
      emit2 ("mov", "[de],a");
      if (byte + 1 < storage_size)
        emit2 ("incw", "de");
    }

  clearRegisterState ();
  return true;
}

static bool
genPointerSet (const iCode *ic)
{
  operand *ptr = ic->op == SET_VALUE_AT_ADDRESS ? IC_LEFT (ic) :
                 POINTER_SET (ic) ? IC_RESULT (ic) : NULL;
  operand *value = IC_RIGHT (ic);
  sym_link *ptr_type;

  if (!ptr || !value)
    return false;

  ptr_type = operandType (ptr);
  if (ptr_type && ptr_type->next && IS_BITFIELD (getSpec (ptr_type->next)))
    return genPointerSetBitField (ptr, value, getSpec (ptr_type->next));

  const int size = k78k0_operandSize (value);
  if (!isScalarSize (size) || !savePointerToDE (ptr, 0))
    return false;

  for (int offset = 0; offset < size; offset++)
    {
      if (!loadOperandByteToA (value, offset))
        return false;
      emit2 ("mov", "[de],a");
      if (offset + 1 < size)
        emit2 ("incw", "de");
    }

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

  if (!IS_ITEMP (result) || !left || !right || !isUnsignedByteDivisor (ic))
    return false;

  result_size = getSize (operandType (result));
  if (result_size < 1 || result_size > 2)
    return false;

  if (getSize (operandType (left)) > 2 || !SPEC_USIGN (getSpec (operandType (left))))
    return false;

  if (operandNeedsStackHL (left, getSize (operandType (left))) || operandNeedsStackHL (right, 1))
    ensureStackAddress (0, K78K0_CLOBBER_AX, NULL);

  if (IS_OP_LITERAL (right))
    {
      if (!loadUnsignedOperandToAX (left))
        return false;
      emit2 ("mov", "c,#0x%02x", (unsigned)operandLitValueUll (right) & 0xffu);
    }
  else
    {
      if (!loadOperandByteToA (right, 0))
        return false;
      emit2 ("mov", "c,a");
      if (!loadUnsignedOperandToAX (left))
        return false;
    }

  emit2 ("divuw", "c");

  if (is_mod)
    {
      emit2 ("mov", "a,c");
      if (result_size == 2)
        {
          emit2 ("mov", "x,a");
          emit2 ("mov", "a,#0x00");
        }
    }
  else if (result_size == 1)
    emit2 ("mov", "a,x");

  setReturnResult (result, result_size);
  return true;
}

static void
emitByteShift (const shift_kind kind)
{
  if (kind == K78K0_SHIFT_SIGNED_RIGHT)
    emit2 ("mov1", "cy,a.7");
  else
    emit2 ("clr1", "cy");
  emit2 (kind == K78K0_SHIFT_LEFT ? "rolc" : "rorc", "a,1");
}

static void
emitSignMaskForA (void)
{
  emit2 ("rolc", "a,1");
  emit2 ("mov", "a,#0x00");
  emit2 ("subc", "a,#0x00");
}

static void
emitWordShift (const shift_kind kind)
{
  if (kind == K78K0_SHIFT_LEFT)
    emit2 ("xch", "a,x");
  emitByteShift (kind);
  emit2 ("xch", "a,x");
  emit2 (kind == K78K0_SHIFT_LEFT ? "rolc" : "rorc", "a,1");
  if (kind != K78K0_SHIFT_LEFT)
    emit2 ("xch", "a,x");
}

static void
emitScalarShiftOne (const int size, const shift_kind kind)
{
  if (size == 1)
    emitByteShift (kind);
  else
    emitWordShift (kind);
}

static void
emitVariableShiftLoop (const int size, const shift_kind kind)
{
  char loop_label[32];
  char test_label[32];

  makeLocalLabel (loop_label, sizeof (loop_label));
  makeLocalLabel (test_label, sizeof (test_label));

  emit2 ("inc", "c");
  emit2 ("br", "!%s", test_label);

  emitLocalLabel (loop_label);
  emitScalarShiftOne (size, kind);

  emitLocalLabel (test_label);
  emit2 ("dbnz", "c,%s", loop_label);
}

static bool
copyOperandToTarget (const operand *source, const operand *target, const int size)
{
  asmop source_aop, target_aop;

  return aopForOperand (&source_aop, source) &&
         aopForOperand (&target_aop, target) && target_aop.size == size &&
         genMove (&target_aop, &source_aop);
}

static bool
fillTargetBytesWithA (const operand *target, const int size)
{
  if (operandNeedsStackHL (target, size))
    ensureStackAddress (0, K78K0_PRESERVE_A, "c");

  for (int offset = 0; offset < size; offset++)
    {
      if (!storeAToOperandByte (target, offset))
        return false;
    }

  return true;
}

static bool
shiftTargetOne (const operand *target, const int size, const shift_kind kind)
{
  const bool direct_carry = operandAccessPreservesCarry (target, size);
  const bool right = kind != K78K0_SHIFT_LEFT;

  for (int i = 0; i < size; i++)
    {
      const int offset = right ? size - i - 1 : i;

      if (!loadOperandByteToA (target, offset))
        return false;

      if (!i)
        emit2 (kind == K78K0_SHIFT_SIGNED_RIGHT ? "mov1" : "clr1",
               kind == K78K0_SHIFT_SIGNED_RIGHT ? "cy,a.7" : "cy");
      else if (!direct_carry)
        popPSW ();
      emit2 (right ? "rorc" : "rolc", "a,1");
      if (i + 1 < size && !direct_carry)
        pushPSW ();
      if (!storeAToOperandByte (target, offset))
        return false;
    }

  return true;
}

static bool
shiftOperandBytes (const operand *source, const operand *target, const int size,
                   const int count, const shift_kind kind)
{
  const bool right = kind != K78K0_SHIFT_LEFT;
  const int copied = size - count;

  if (kind == K78K0_SHIFT_SIGNED_RIGHT)
    {
      if (!loadOperandByteToA (source, size - 1))
        return false;
      emitSignMaskForA ();
      emit2 ("mov", "b,a");
    }

  for (int i = 0; i < copied; i++)
    {
      const int target_offset = right ? i : size - i - 1;
      const int source_offset = right ? i + count : target_offset - count;

      if (!loadOperandByteToA (source, source_offset) ||
          !storeAToOperandByte (target, target_offset))
        return false;
    }

  if (kind == K78K0_SHIFT_SIGNED_RIGHT)
    emit2 ("mov", "a,b");
  else
    emit2 ("mov", "a,#0x00");
  for (int i = 0; i < count; i++)
    if (!storeAToOperandByte (target, right ? copied + i : i))
      return false;

  return true;
}

static bool
shiftTargetByBits (const operand *target, const int size, const shift_kind kind,
                   const unsigned count)
{
  char loop_label[32];

  wassertl (count > 0 && count < 8, "78K0 invalid residual shift count.");

  if (count > 1)
    {
      makeLocalLabel (loop_label, sizeof (loop_label));
      emit2 ("mov", "b,#0x%02x", count);
      emitLocalLabelPreservingHL (loop_label);
    }

  if (!shiftTargetOne (target, size, kind))
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
genWideLiteralShift (const iCode *ic, const shift_kind kind, unsigned long long count)
{
  operand *result = IC_RESULT (ic);
  operand *left = IC_LEFT (ic);
  operand *target;
  const unsigned top_byte_mask = unsignedBitIntTopByteMask (result);
  unsigned byte_count;
  int size = getSize (operandType (result));

  if (!isScalarSize (size) || getSize (operandType (left)) != size)
    return false;

  target = wideAssignmentTarget (ic, result);
  if (!target)
    return true;

  if (count >= (unsigned long long)size * 8ull)
    {
      if (kind != K78K0_SHIFT_SIGNED_RIGHT)
        {
          if (operandNeedsStackHL (target, size))
            ensureStackAddress (0, K78K0_CLOBBER_AX, NULL);
          emit2 ("mov", "a,#0x00");
          if (!fillTargetBytesWithA (target, size))
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

      return finishWideAssignment (ic, result, target);
    }

  byte_count = (unsigned)(count / 8u);
  count %= 8u;

  if (byte_count)
    {
      if (!shiftOperandBytes (left, target, size, byte_count, kind))
        return false;
    }
  else if (!copyOperandToTarget (left, target, size))
    return false;

  if (count && !shiftTargetByBits (target, size, kind, (unsigned)count))
    return false;

  return maskTargetTopByte (target, size, top_byte_mask) && finishWideAssignment (ic, result, target);
}

static bool
genWideVariableShift (const iCode *ic, const shift_kind kind)
{
  operand *result = IC_RESULT (ic);
  operand *left = IC_LEFT (ic);
  operand *right = IC_RIGHT (ic);
  operand *target;
  const unsigned top_byte_mask = unsignedBitIntTopByteMask (result);
  char byte_loop_label[32];
  char residual_label[32];
  char loop_label[32];
  char test_label[32];
  int size = getSize (operandType (result));

  if (!isScalarSize (size) || getSize (operandType (left)) != size || getSize (operandType (right)) < 1)
    return false;

  target = wideAssignmentTarget (ic, result);
  if (!target)
    return true;

  if (!loadOperandByteToA (right, 0))
    return false;
  emit2 ("mov", "d,a");

  if (!copyOperandToTarget (left, target, size))
    return false;

  makeLocalLabel (byte_loop_label, sizeof (byte_loop_label));
  makeLocalLabel (residual_label, sizeof (residual_label));
  makeLocalLabel (loop_label, sizeof (loop_label));
  makeLocalLabel (test_label, sizeof (test_label));

  emitLocalLabel (byte_loop_label);
  emit2 ("mov", "a,d");
  emit2 ("cmp", "a,#0x08");
  emitCondBranch ("bc", residual_label);
  emit2 ("sub", "a,#0x08");
  emit2 ("mov", "d,a");

  if (!shiftOperandBytes (target, target, size, 1, kind))
    return false;
  emit2 ("br", "!%s", byte_loop_label);

  emitLocalLabel (residual_label);
  emit2 ("mov", "a,d");
  emit2 ("mov", "b,a");
  emit2 ("inc", "b");
  emit2 ("br", "!%s", test_label);

  emitLocalLabel (loop_label);
  if (!shiftTargetOne (target, size, kind))
    return false;

  emitLocalLabel (test_label);
  emit2 ("dbnz", "b,%s", loop_label);

  return maskTargetTopByte (target, size, top_byte_mask) && finishWideAssignment (ic, result, target);
}

static bool
genScalarVariableShift (const operand *result, const operand *left, const operand *right,
                        const int size, const shift_kind kind)
{
  if (getSize (operandType (right)) < 1)
    return false;

  if (operandNeedsStackHL (left, size) || operandNeedsStackHL (right, 1))
    ensureStackAddress (0, K78K0_CLOBBER_AX, NULL);

  if (size == 1)
    {
      if (!loadOperandByteToA (left, 0))
        return false;
      emit2 ("mov", "b,a");
    }
  else if (!loadOperandWordToPair (left, "de"))
    return false;

  if (!loadOperandByteToA (right, 0))
    return false;
  emit2 ("mov", "c,a");

  if (size == 1)
    emit2 ("mov", "a,b");
  else
    emit2 ("movw", "ax,de");

  emitVariableShiftLoop (size, kind);
  maskUnsignedBitIntTopByteInA (result);
  setReturnResult (result, size);
  return true;
}

static bool
genScalarLiteralShift (const operand *result, const operand *left, const int size,
                       const shift_kind kind, unsigned long long count)
{
  if (count >= (unsigned)(size * 8))
    {
      if (kind == K78K0_SHIFT_SIGNED_RIGHT)
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
      unsigned rotations;
      const char *mnemonic;

      if (!loadOperandByteToA (left, 0))
        return false;

      if (kind != K78K0_SHIFT_SIGNED_RIGHT && count > 1)
        {
          emit2 ("and", "a,#0x%02x",
                 kind == K78K0_SHIFT_RIGHT ?
                   (unsigned)(0xffu << count) & 0xffu : 0xffu >> count);
          rotations = count <= 4 ? (unsigned)count : 8u - (unsigned)count;
          mnemonic = (kind == K78K0_SHIFT_RIGHT) == (count <= 4) ? "ror" : "rol";
          while (rotations--)
            emit2 (mnemonic, "a,1");
        }
      else
        while (count--)
          emitScalarShiftOne (size, kind);

      maskUnsignedBitIntTopByteInA (result);
      setReturnResult (result, size);
      return true;
    }

  if (!genOperandReturnValue (left))
    return false;

  if (count >= 8)
    {
      if (kind != K78K0_SHIFT_LEFT)
        {
          emit2 ("mov", "x,a");
          if (kind == K78K0_SHIFT_SIGNED_RIGHT)
            emitSignMaskForA ();
          else
            emit2 ("mov", "a,#0x00");
        }
      else
        {
          emit2 ("mov", "a,x");
          emit2 ("mov", "x,#0x00");
        }
      count -= 8;
    }

  while (count--)
    emitScalarShiftOne (size, kind);

  maskUnsignedBitIntTopByteInA (result);
  setReturnResult (result, size);
  return true;
}

static bool
genShift (const iCode *ic)
{
  operand *result = IC_RESULT (ic);
  operand *left = IC_LEFT (ic);
  operand *right = IC_RIGHT (ic);
  const bool is_right = ic->op == RIGHT_OP;
  unsigned long long count;
  int size;

  if (!IS_ITEMP (result) || !left || !right)
    return false;

  const shift_kind kind = !is_right ? K78K0_SHIFT_LEFT :
    !SPEC_USIGN (getSpec (operandType (left))) ? K78K0_SHIFT_SIGNED_RIGHT :
                                                K78K0_SHIFT_RIGHT;

  size = getSize (operandType (result));
  if (!isScalarSize (size) || getSize (operandType (left)) != size)
    return false;

  if (!IS_OP_LITERAL (right))
    return size > 2 ? genWideVariableShift (ic, kind) :
                      genScalarVariableShift (result, left, right, size, kind);

  count = operandLitValueUll (right);
  if (size > 2)
    return genWideLiteralShift (ic, kind, count);
  return genScalarLiteralShift (result, left, size, kind, count);
}

static void
emitByteRotate (const char *mnemonic, unsigned count)
{
  while (count--)
    emit2 (mnemonic, "a,1");
}

static bool
genWideRot (const iCode *ic, const unsigned original_count)
{
  static const unsigned char copy_order[4] = {0, 2, 3, 1};
  operand *result = IC_RESULT (ic);
  const operand *left = IC_LEFT (ic);
  operand *target;
  const int size = k78k0_operandSize (result);
  const unsigned bits = size * 8u;
  unsigned count = original_count % bits;
  bool rotate_right = count > bits / 2u;

  if ((size != 2 && size != 4) || k78k0_operandSize (left) != size)
    return false;

  target = wideAssignmentTarget (ic, result);
  if (!target)
    return true;
  if (!count)
    return copyOperandToTarget (left, target, size) && finishWideAssignment (ic, result, target);

  if (rotate_right)
    count = bits - count;
  const unsigned byte_count = count / 8u;
  const unsigned residual = count % 8u;
  const bool saved_ax = operandUsesPair (left, "ax");
  if (saved_ax)
    emit2 ("movw", "de,ax");

  adjustStackPointer (-size, true);

  for (int offset = 0; offset < size; offset++)
    {
      const int source_offset = rotate_right ? (offset + byte_count) % size :
        (offset + size - byte_count) % size;
      const reg_info *reg = saved_ax ? operandRegisterByte (left, source_offset) : NULL;

      if (reg && reg->rIdx <= K78K0_RB0_A_IDX)
        emit2 ("mov", "a,%s", reg->rIdx == K78K0_RB0_X_IDX ? "e" : "d");
      else if (!loadOperandByteToA (left, source_offset))
        return false;
      ensureStackAddress (0, K78K0_PRESERVE_A, "c");
      emit2 ("mov", "[hl+0x%02x],a", offset);
    }

  if (residual)
    {
      char loop_label[32];

      if (residual > 1)
        {
          makeLocalLabel (loop_label, sizeof (loop_label));
          emit2 ("mov", "b,#0x%02x", residual);
          emitLocalLabelPreservingHL (loop_label);
        }

      ensureStackAddress (0, K78K0_CLOBBER_AX, NULL);
      emit2 ("mov", "a,[hl+0x%02x]", rotate_right ? 0 : size - 1);
      emit2 ("mov1", "cy,a.%u", rotate_right ? 0 : 7);
      for (int i = 0; i < size; i++)
        {
          const int offset = rotate_right ? size - 1 - i : i;
          emit2 ("mov", "a,[hl+0x%02x]", offset);
          emit2 (rotate_right ? "rorc" : "rolc", "a,1");
          emit2 ("mov", "[hl+0x%02x],a", offset);
        }

      if (residual > 1)
        emit2 ("dbnz", "b,%s", loop_label);
    }

  for (int i = 0; i < size; i++)
    {
      const int offset = size == 4 ? copy_order[i] : i;

      ensureStackAddress (0, K78K0_CLOBBER_AX, NULL);
      emit2 ("mov", "a,[hl+0x%02x]", offset);
      if (!storeAToOperandByte (target, offset))
        return false;
    }

  const bool target_uses_ax = operandUsesPair (target, "ax");
  if (target_uses_ax)
    emit2 ("movw", "de,ax");
  adjustStackPointer (size, false);
  if (target_uses_ax)
    emit2 ("movw", "ax,de");

  return finishWideAssignment (ic, result, target);
}

static bool
genRot (const iCode *ic)
{
  operand *result = IC_RESULT (ic);
  operand *left = IC_LEFT (ic);
  operand *right = IC_RIGHT (ic);
  unsigned count;
  int result_size;

  if (!IS_ITEMP (result) || !left || !IS_OP_LITERAL (right) ||
      bitsForType (operandType (left)) != bitsForType (operandType (result)))
    return false;

  result_size = k78k0_operandSize (result);
  count = (unsigned)operandLitValueUll (right);
  if (result_size == 2 && count % 16u == 8u)
    {
      if (!genOperandReturnValue (left))
        return false;
      emit2 ("xch", "a,x");
      setReturnResult (result, 2);
      return true;
    }
  if (result_size != 1)
    return genWideRot (ic, count);

  count %= 8u;
  if (!loadOperandByteToA (left, 0))
    return false;

  if (count <= 4)
    emitByteRotate ("rol", count);
  else
    emitByteRotate ("ror", 8 - count);

  setAResult (result);
  return true;
}

static bool
getLiteralByteOffset (const iCode *ic, const int result_size, int *byte_offset)
{
  operand *right = IC_RIGHT (ic);
  unsigned long long bit_offset;

  if (!IS_ITEMP (IC_RESULT (ic)) || !IC_LEFT (ic) || !right || !IS_OP_LITERAL (right) ||
      k78k0_operandSize (IC_RESULT (ic)) != result_size)
    return false;

  bit_offset = operandLitValueUll (right);
  if (bit_offset % 8ull)
    return false;

  *byte_offset = (int)(bit_offset / 8ull);
  return true;
}

static bool
genGetByte (const iCode *ic)
{
  operand *result = IC_RESULT (ic);
  operand *left = IC_LEFT (ic);
  int byte_offset;

  if (!getLiteralByteOffset (ic, 1, &byte_offset) || !loadOperandByteToA (left, byte_offset))
    return false;

  setAResult (result);
  return true;
}

static bool
genGetWord (const iCode *ic)
{
  operand *result = IC_RESULT (ic);
  operand *left = IC_LEFT (ic);
  int byte_offset;

  if (!getLiteralByteOffset (ic, 2, &byte_offset) ||
      !loadOperandByteToA (left, byte_offset + 1))
    return false;
  emit2 ("mov", "d,a");

  if (!loadOperandByteToA (left, byte_offset))
    return false;
  emit2 ("mov", "x,a");
  emit2 ("mov", "a,d");

  setReturnResult (result, 2);
  return true;
}

static bool
genGetABit (const iCode *ic)
{
  operand *result = IC_RESULT (ic);
  operand *left = IC_LEFT (ic);
  operand *right = IC_RIGHT (ic);
  iCode *ifx;
  unsigned long long bit_offset;

  if (!IS_ITEMP (result) || !left || !IS_OP_LITERAL (right) || k78k0_operandSize (result) != 1)
    return false;

  bit_offset = operandLitValueUll (right);
  if (bit_offset >= (unsigned long long)k78k0_operandSize (left) * 8ull)
    return false;

  if (!loadOperandByteToA (left, (int)(bit_offset / 8ull)))
    return false;

  unsigned shift = (unsigned)(bit_offset % 8ull);
  ifx = ifxForOp (result, (iCode *)ic);
  if (ifx)
    {
      char target_label[32];
      const bool branch_if_set = IC_TRUE (ifx) != NULL;

      makeICLabel (target_label, sizeof (target_label),
                   branch_if_set ? IC_TRUE (ifx) : IC_FALSE (ifx));
      emitABitBranch (branch_if_set, shift, target_label);
      markGenerated (ifx);
      return true;
    }

  if (shift <= 4)
    emitByteRotate ("ror", shift);
  else
    emitByteRotate ("rol", 8 - shift);
  emit2 ("and", "a,#0x01");
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
  if (!isScalarSize (size))
    return false;

  for (int offset = 0; offset < size; offset++)
    if (!loadOperandByteToA (right, offset))
      return false;

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
  if (!isScalarSize (size))
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
  emitWordShift (K78K0_SHIFT_LEFT);
  emit2 ("addw", "ax,#%s", table_label);
  moveAXToHL ();
  loadWordAtHLToAX ();
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
  if (!isScalarSize (size))
    return false;

  if (size == 1)
    {
      if (!loadOperandByteToA (left, 0))
        return false;
      emit2 ("mov", "c,a");
      adjustStackPointer (-1, true);
      emit2 ("mov", "a,c");
      emit2 ("mov", "[hl+0x00],a");
      return true;
    }

  if (size == 2)
    {
      if (!genOperandReturnValue (left))
        return false;

      emit2 ("push", "ax");
      G.stack.pushed += 2;
      clearRegisterState ();
      return true;
    }

  adjustStackPointer (-size, true);
  for (int offset = 0; offset < size; offset++)
    {
      if (!loadOperandByteToA (left, offset))
        return false;
      ensureStackAddress (0, K78K0_PRESERVE_A, "c");
      emit2 ("mov", "[hl+0x%02x],a", (unsigned)offset);
    }

  return true;
}

/* Copy a contiguous block from DE to HL. Both pointers are advanced by size.
   B's zero count represents 256 iterations; larger copies use all of BC. */
static bool
emitBlockCopyDEToHL (const int size)
{
  char copy_label[32];

  if (size < 1 || size > 0xffff)
    return false;

  if (size <= (optimize.codeSize ? 2 : 4))
    {
      for (int offset = 0; offset < size; offset++)
        {
          emit2 ("mov", "a,[de]");
          emit2 ("mov", "[hl],a");
          if (offset + 1 < size)
            {
              emit2 ("incw", "de");
              emit2 ("incw", "hl");
              clearHLState ();
            }
        }
      clearRegisterState ();
      return true;
    }

  makeLocalLabel (copy_label, sizeof (copy_label));
  if (size <= 256)
    emit2 ("mov", "b,#0x%02x", (unsigned)(size & 0xff));
  else
    emit2 ("movw", "bc,#0x%04x", (unsigned)size);

  emitLocalLabel (copy_label);
  emit2 ("mov", "a,[de]");
  emit2 ("mov", "[hl],a");
  emit2 ("incw", "de");
  emit2 ("incw", "hl");
  clearHLState ();
  if (size <= 256)
    emit2 ("dbnz", "b,%s", copy_label);
  else
    {
      emit2 ("decw", "bc");
      emit2 ("mov", "a,c");
      emit2 ("or", "a,b");
      emit2 ("bnz", "%s", copy_label);
    }

  clearRegisterState ();
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
  if (size < 1 || size > 0xffff || pointer_offset > 0xffffu ||
      pointer_offset + (unsigned long long)size > 0x10000u)
    return false;

  if (!savePointerToDE (left, (long)pointer_offset))
    return false;

  adjustStackPointer (-size, true);
  return emitBlockCopyDEToHL (size);
}

static bool
genSend (const iCode *ic)
{
  const operand *left = IC_LEFT (ic);
  const iCode *call;

  if (!left || ic->argreg < 1)
    return false;

  for (call = ic->next; call && call->op != CALL && call->op != PCALL; call = call->next)
    ;
  if (!call || !IC_LEFT (call))
    return false;

  const asmop *argument = aopArg (operandType (IC_LEFT (call)), ic->argreg);
  return argument ? genOperandToAop (left, argument) : true;
}

static bool
genReceive (const iCode *ic)
{
  const operand *result = IC_RESULT (ic);
  const asmop *argument = ic->argreg == 1 && currFunc ?
    aopArg (currFunc->type, 1) : NULL;
  asmop destination;

  if (!argument)
    return true;
  if (!result || !aopForOperand (&destination, result) || destination.size != argument->size)
    return false;
  return genMove (&destination, argument);
}

static bool
copyReturnToHiddenPointer (const operand *left)
{
  sym_link *type = currFunc ? currFunc->type : NULL;
  const int size = functionReturnSize (type);
  const int pointer_offset = G.stack.local_size + K78K0_RETURN_ADDRESS_BYTES + G.stack.pushed;
  const symbol *storage = IS_SYMOP (left) ? operandStorageSymbol (left) : NULL;
  const symbol *base = NULL;
  long address_offset = 0;
  sym_link *return_type = type && IS_FUNC (type) ? type->next : NULL;
  const bool source_is_address = IS_ITEMP (left) &&
    (left->isaddr || rematerializedOperandAddress (left, &base, &address_offset) ||
     (return_type && IS_PTR (operandType (left)) &&
      compareType (operandType (left)->next, return_type, false) == 1));
  const bool source_is_direct = !source_is_address && storage && !storage->onStack &&
    storage->rname[0];

  if (size < 1 || size > 0xffff || pointer_offset < 0)
    return false;

  if (source_is_direct)
    emit2 ("movw", "de,#%s", storage->rname);
  else if (source_is_address)
    {
      if (!genOperandReturnValue (left))
        return false;
    }
  else if (storage && storage->onStack)
    {
      if (!loadSymbolAddressToAX (storage, 0))
        return false;
    }
  else
    {
      setStackAddress (pointer_offset, K78K0_CLOBBER_AX, NULL);
      loadWordAtHLToAX ();
      emit2 ("movw", "de,ax");

      for (int offset = 0; offset < size; offset++)
        {
          if (!loadOperandByteToA (left, offset))
            return false;
          emit2 ("mov", "[de],a");
          if (offset + 1 < size)
            emit2 ("incw", "de");
        }

      return true;
    }

  if (!source_is_direct)
    emit2 ("movw", "de,ax");
  setStackAddress (pointer_offset, K78K0_CLOBBER_AX, NULL);
  loadWordAtHLToAX ();
  moveAXToHL ();
  return emitBlockCopyDEToHL (size);
}

static bool
genReturn (const iCode *ic)
{
  char label[32];
  sym_link *type = currFunc ? currFunc->type : NULL;
  const int return_size = functionReturnSize (type);
  const bool hidden = typeReturnsViaHiddenPointer (type);

  if (IC_LEFT (ic) && (hidden || return_size > 0))
    {
      const bool lowered = hidden ? copyReturnToHiddenPointer (IC_LEFT (ic)) :
        genOperandToAop (IC_LEFT (ic), aopRet (type));

      if (!lowered)
        return false;
    }

  makeICLabel (label, sizeof (label), returnLabel);
  emit2 ("br", "!%s", label);
  return true;
}

static bool
resultRemat (const iCode *ic)
{
  if (SKIP_IC (ic) || ic->op == IFX || POINTER_SET (ic) ||
      !IC_RESULT (ic) || !IS_ITEMP (IC_RESULT (ic)))
    return false;

  const symbol *sym = OP_SYMBOL_CONST (IC_RESULT (ic));
  if (!sym->remat)
    return false;

  const int size = getSize (sym->type);
  for (int byte = 0; byte < size && byte < K78K0_MAX_SCALAR_BYTES; byte++)
    if (sym->regs[byte])
      return false;

  return true;
}

static bool
lowerIcode (iCode *ic)
{
  switch (ic->op)
    {
    case FUNCTION:
      genFunction (ic);
      return true;

    case ENDFUNCTION:
      genEndFunction (ic);
      return true;

    case RETURN:
      return genReturn (ic);

    case LABEL:
      genLabel (ic);
      return true;

    case GOTO:
      genGoto (ic);
      return true;

    case ADDRESS_OF:
      return genAddrOf (ic);

    case GET_VALUE_AT_ADDRESS:
      return genPointerGet (ic);

    case SET_VALUE_AT_ADDRESS:
      return genPointerSet (ic);

    case CAST:
      return genCast (ic);

    case '+':
      return genAddSub (ic, false);

    case '-':
      return genAddSub (ic, true);

    case '*':
      return genMult (ic);

    case '/':
    case '%':
      return genDivMod (ic);

    case BITWISEAND:
      return genBinaryAccumulatorOp (ic, "and", "and");

    case '|':
      return genBinaryAccumulatorOp (ic, "or", "or");

    case '^':
      return genBinaryAccumulatorOp (ic, "xor", "xor");

    case UNARYMINUS:
      return genUnaryMinus (ic);

    case '!':
      return genNot (ic);

    case LEFT_OP:
    case RIGHT_OP:
      return genShift (ic);

    case ROT:
      return genRot (ic);

    case GETBYTE:
      return genGetByte (ic);

    case GETWORD:
      return genGetWord (ic);

    case GETABIT:
      return genGetABit (ic);

    case EQ_OP:
    case NE_OP:
      return genCmpEqNe (ic, ifxForOp (IC_RESULT (ic), ic));

    case '<':
    case '>':
      return genCmpLtGt (ic, ifxForOp (IC_RESULT (ic), ic));

    case IPUSH:
      return genIpush (ic);

    case IPUSH_VALUE_AT_ADDRESS:
      return genPointerIpush (ic);

    case SEND:
      return genSend (ic);

    case RECEIVE:
      return genReceive (ic);

    case CALL:
      return genCall (ic);

    case PCALL:
      return genPcall (ic);

    case IFX:
      return genIfx (ic);

    case JUMPTABLE:
      return genJumpTable (ic);

    case '=':
      return POINTER_SET (ic) ? genPointerSet (ic) : genAssign (ic);

    case INLINEASM:
      if (regalloc_dry_run)
        clearRegisterState ();
      else
        genInlineAsm (ic);
      return true;

    case DUMMY_READ_VOLATILE:
      return genDummyReadVolatile (ic);

    case CRITICAL:
      genCritical ();
      return true;

    case ENDCRITICAL:
      genEndCritical ();
      return true;

    default:
      return false;
    }
}

static void
loweringFailed (void)
{
  if (regalloc_dry_run)
    regalloc_dry_run_failed = true;
  else
    wassertl (0, "78K0 iCode lowering failed.");
}

static void
gen78K0iCode (iCode *ic)
{
  genLine.lineElement.ic = ic;

  if (!resultRemat (ic) && !ic->generated && !lowerIcode (ic))
    loweringFailed ();
}

static void
initializeDryCodegenState (const iCode *ic)
{
  resetFunctionState ();

  if (currFunc && currFunc->type)
    G.stack.local_size = currFunc->stack > 0 ? currFunc->stack : 0;

  if (ic->op == CALL || ic->op == PCALL)
    G.stack.pushed = ic->parmBytes;
  else if (ic->op == ENDCRITICAL)
    G.stack.pushed = 1;
}

float
k78k0DryInstructionCost (iCode *ic)
{
  if (!ic)
    return (float)HUGE_VAL;
  if (assignmentFusedByPrevious (ic))
    return 0.0f;

  regalloc_dry_run = true;
  regalloc_dry_run_failed = false;
  regalloc_dry_run_cost_bytes = 0;
  regalloc_dry_label = 0;
  genLine.lineHead = NULL;
  genLine.lineCurr = NULL;
  initGenLineElement ();
  initializeDryCodegenState (ic);

  gen78K0iCode (ic);
  if (regalloc_dry_run_failed)
    return (float)HUGE_VAL;

  /* As in the other tree-decomposition backends, keep a static byte cost and
     weight the execution cost by block frequency.  Until 78K0 has a cycle
     table, encoded bytes are a small, stable proxy for execution cost. */
  const unsigned byte_cost_weight = 2u << (optimize.codeSize * 3 + !optimize.codeSpeed * 3);
  return (float)regalloc_dry_run_cost_bytes * (byte_cost_weight + ic->count);
}

static lineNode *
previousCodeLine (lineNode *line)
{
  while (line)
    {
      if (line->isInline || (line->ic && line->ic->op == INLINEASM))
        return NULL;
      if (!line->isComment && !line->isDebug && line->line[0] != ';')
        return line;
      line = line->prev;
    }

  return NULL;
}

static const char *
loadedImmediate (const char *line, const char *prefix)
{
  const size_t length = strlen (prefix);
  return !strncmp (line, prefix, length) && line[length] ? line + length : NULL;
}

static bool
codeReferencesVolatile (const iCode *ic)
{
  return !ic ||
    (IS_SYMOP (IC_LEFT (ic)) && IC_LEFT (ic)->isvolatile) ||
    (IS_SYMOP (IC_RIGHT (ic)) && IC_RIGHT (ic)->isvolatile) ||
    (IS_SYMOP (IC_RESULT (ic)) && IC_RESULT (ic)->isvolatile);
}

static bool
operandMayBePSW (operand *op)
{
  unsigned address;

  if (!IS_SYMOP (op))
    return false;

  const symbol *sym = OP_SYMBOL (op);
  return (sym->etype && SPEC_SCLS (sym->etype) == S_SFR) ||
    (absoluteSymbolAddress (sym, 0, &address) && address == 0xfffeu);
}

static bool
storePreservesA (const lineNode *line)
{
  if (!line || line->isInline || (line->ic && line->ic->op == INLINEASM) ||
      line->isComment || line->isDebug || line->isLabel ||
      strncmp (line->line, "mov\t", 4))
    return false;

  const char *destination = line->line + 4;
  const size_t length = strlen (destination);

  return length > 2 && !strcmp (destination + length - 2, ",a") &&
    strcmp (destination, "psw,a") && strcmp (destination, "0xfffe,a") &&
    strcmp (destination, "!0xfffe,a") && strcmp (destination, "[de],a") &&
    !codeReferencesVolatile (line->ic) &&
    line->ic->op != SET_VALUE_AT_ADDRESS &&
    !(line->ic->op == '=' && POINTER_SET (line->ic)) &&
    !operandMayBePSW (IC_LEFT (line->ic)) &&
    !operandMayBePSW (IC_RIGHT (line->ic)) &&
    !operandMayBePSW (IC_RESULT (line->ic));
}

static bool
redundantLiteralReload (const char *line)
{
  /* Inline assembly bypasses emit2(), and an earlier inline node stops the scan. */
  const char *prefix = "mov\ta,#";
  const char *immediate = loadedImmediate (line, prefix);
  bool word = false;

  if (!immediate)
    {
      prefix = "movw\tax,#";
      immediate = loadedImmediate (line, prefix);
      word = true;
    }

  if (!immediate || genLine.lineElement.isInline ||
      (genLine.lineElement.ic && genLine.lineElement.ic->op == INLINEASM) ||
      codeReferencesVolatile (genLine.lineElement.ic))
    return false;

  lineNode *previous = previousCodeLine (genLine.lineCurr);
  bool crossed = false;

  while (previous && (word ?
         (!strcmp (previous->line, "push\tax") &&
          !codeReferencesVolatile (previous->ic)) : storePreservesA (previous)))
    {
      crossed = true;
      previous = previousCodeLine (previous->prev);
    }

  const char *loaded = previous && !previous->isLabel &&
    !codeReferencesVolatile (previous->ic) ?
    loadedImmediate (previous->line, prefix) : NULL;
  return crossed && loaded && !strcmp (loaded, immediate);
}

void
gen78K0Code (iCode *ic_head)
{
  int clevel = 0;
  int cblock = 0;
  int cln = 0;

  regalloc_dry_run = false;
  codegen_label_scope++;

  if (options.debug && currFunc)
    debugFile->writeFrameAddress (NULL, NULL, 0);

  for (iCode *ic = ic_head; ic; ic = ic->next)
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

  if (options.debug)
    debugFile->writeFrameAddress (NULL, NULL, 0);

  if (!options.nopeep)
    peepHole (&genLine.lineHead);

  printLine (genLine.lineHead, codeOutBuf);
  destroy_line_list ();
}
