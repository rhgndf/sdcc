/*-------------------------------------------------------------------------
  peep.c - 78K0 peephole data-flow helpers

  Copyright (C) 2026

  This program is free software; you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by the
  Free Software Foundation; either version 2, or (at your option) any
  later version.
-------------------------------------------------------------------------*/

#include "common.h"
#include "ralloc.h"
#include "gen.h"
#include "peep.h"

#define K78K0_MAX_PEEP_OPERANDS 3
#define K78K0_PEEP_REGISTER_RETURN_BYTES 4

typedef struct
{
  char mnemonic[16];
  char operand[K78K0_MAX_PEEP_OPERANDS][SDCC_NAME_MAX + 32];
  int operand_count;
}
k78k0_parsed_instruction;

static void
trimText (char *text)
{
  char *start = text;
  char *end;

  while (*start && isspace ((unsigned char)*start))
    start++;
  if (start != text)
    memmove (text, start, strlen (start) + 1);

  end = text + strlen (text);
  while (end > text && isspace ((unsigned char)end[-1]))
    end--;
  *end = '\0';
}

static bool
parseInstruction (const lineNode *line, k78k0_parsed_instruction *instruction)
{
  const char *cursor;
  size_t length;

  memset (instruction, 0, sizeof (*instruction));
  if (!line || !line->line || line->isDebug || line->isComment || line->isLabel)
    return false;

  cursor = line->line;
  while (*cursor && isspace ((unsigned char)*cursor))
    cursor++;
  if (!*cursor || *cursor == ';')
    return false;

  length = 0;
  while (cursor[length] && !isspace ((unsigned char)cursor[length]))
    length++;
  if (!length || length >= sizeof (instruction->mnemonic))
    return false;
  memcpy (instruction->mnemonic, cursor, length);
  instruction->mnemonic[length] = '\0';
  cursor += length;

  while (*cursor && instruction->operand_count < K78K0_MAX_PEEP_OPERANDS)
    {
      char *destination = instruction->operand[instruction->operand_count];
      const char *end;

      while (*cursor && isspace ((unsigned char)*cursor))
        cursor++;
      if (!*cursor || *cursor == ';')
        break;

      end = cursor;
      while (*end && *end != ',' && *end != ';')
        end++;
      length = end - cursor;
      if (length >= sizeof (instruction->operand[0]))
        return false;
      memcpy (destination, cursor, length);
      destination[length] = '\0';
      trimText (destination);
      instruction->operand_count++;

      cursor = end;
      if (*cursor == ',')
        cursor++;
      else
        break;
    }

  return true;
}

static bool
registerByteInPair (const char *reg, const char *pair)
{
  return (!strcmp (pair, "ax") && (!strcmp (reg, "a") || !strcmp (reg, "x"))) ||
    (!strcmp (pair, "bc") && (!strcmp (reg, "b") || !strcmp (reg, "c"))) ||
    (!strcmp (pair, "de") && (!strcmp (reg, "d") || !strcmp (reg, "e"))) ||
    (!strcmp (pair, "hl") && (!strcmp (reg, "h") || !strcmp (reg, "l")));
}

static bool
operandIsRegister (const char *operand, const char *reg)
{
  return !strcmp (operand, reg) || registerByteInPair (reg, operand);
}

static bool
operandReadsRegister (const char *operand, const char *reg)
{
  if (!operand || !*operand)
    return false;
  if (operandIsRegister (operand, reg))
    return true;
  if (!strncmp (operand, "a.", 2))
    return !strcmp (reg, "a");
  if (operand[0] != '[')
    return false;

  if (!strncmp (operand, "[de", 3) &&
      (!strcmp (reg, "d") || !strcmp (reg, "e")))
    return true;
  if (!strncmp (operand, "[hl", 3))
    {
      if (!strcmp (reg, "h") || !strcmp (reg, "l"))
        return true;
      if (strstr (operand, "+b") && !strcmp (reg, "b"))
        return true;
      if (strstr (operand, "+c") && !strcmp (reg, "c"))
        return true;
    }
  return false;
}

static bool
destinationAddressReadsRegister (const char *operand, const char *reg)
{
  return operand && operand[0] == '[' && operandReadsRegister (operand, reg);
}

static bool
operandSurelyWritesRegister (const char *operand, const char *reg)
{
  return operand && operandIsRegister (operand, reg);
}

static bool
isConditionalBranch (const char *mnemonic)
{
  return !strcmp (mnemonic, "bc") || !strcmp (mnemonic, "bnc") ||
    !strcmp (mnemonic, "bz") || !strcmp (mnemonic, "bnz") ||
    !strcmp (mnemonic, "bt") || !strcmp (mnemonic, "bf") ||
    !strcmp (mnemonic, "dbnz");
}

static bool
instructionMightReadRegister (const k78k0_parsed_instruction *instruction,
                              const char *reg)
{
  const char *mnemonic = instruction->mnemonic;

  if (!strcmp (mnemonic, "mov") || !strcmp (mnemonic, "movw"))
    return instruction->operand_count != 2 ||
      destinationAddressReadsRegister (instruction->operand[0], reg) ||
      operandReadsRegister (instruction->operand[1], reg);

  if (!strcmp (mnemonic, "mov1"))
    return instruction->operand_count != 2 ||
      operandReadsRegister (instruction->operand[0], reg) ||
      operandReadsRegister (instruction->operand[1], reg);

  if (!strcmp (mnemonic, "xch") || !strcmp (mnemonic, "xchw") ||
      !strcmp (mnemonic, "add") || !strcmp (mnemonic, "addc") ||
      !strcmp (mnemonic, "addw") || !strcmp (mnemonic, "sub") ||
      !strcmp (mnemonic, "subc") || !strcmp (mnemonic, "subw") ||
      !strcmp (mnemonic, "and") || !strcmp (mnemonic, "or") ||
      !strcmp (mnemonic, "xor") || !strcmp (mnemonic, "cmp") ||
      !strcmp (mnemonic, "cmpw"))
    return instruction->operand_count != 2 ||
      operandReadsRegister (instruction->operand[0], reg) ||
      operandReadsRegister (instruction->operand[1], reg);

  if (!strcmp (mnemonic, "inc") || !strcmp (mnemonic, "incw") ||
      !strcmp (mnemonic, "decw") || !strcmp (mnemonic, "rol") ||
      !strcmp (mnemonic, "rolc") || !strcmp (mnemonic, "ror") ||
      !strcmp (mnemonic, "push"))
    return instruction->operand_count != 1 ||
      operandReadsRegister (instruction->operand[0], reg);

  if (!strcmp (mnemonic, "pop") || !strcmp (mnemonic, "di") ||
      !strcmp (mnemonic, "ei") || !strcmp (mnemonic, "nop"))
    return false;

  if (!strcmp (mnemonic, "clr1") || !strcmp (mnemonic, "not1"))
    return instruction->operand_count != 1 ||
      operandReadsRegister (instruction->operand[0], reg);

  if (!strcmp (mnemonic, "divuw"))
    return !strcmp (reg, "a") || !strcmp (reg, "x") || !strcmp (reg, "c");
  if (!strcmp (mnemonic, "mulu"))
    return !strcmp (reg, "a") || !strcmp (reg, "x");

  if (!strcmp (mnemonic, "br"))
    return instruction->operand_count != 1 ||
      operandReadsRegister (instruction->operand[0], reg);
  if (!strcmp (mnemonic, "bt") || !strcmp (mnemonic, "bf") ||
      !strcmp (mnemonic, "dbnz"))
    return instruction->operand_count < 2 ||
      operandReadsRegister (instruction->operand[0], reg);
  if (!strcmp (mnemonic, "bc") || !strcmp (mnemonic, "bnc") ||
      !strcmp (mnemonic, "bz") || !strcmp (mnemonic, "bnz"))
    return instruction->operand_count != 1;

  if (!strcmp (mnemonic, "ret") || !strcmp (mnemonic, "reti") ||
      !strcmp (mnemonic, "call"))
    return false;

  /* Directives, inline assembler and instructions not emitted by this
     backend are deliberately treated as reading every register. */
  return true;
}

static bool
instructionSurelyWritesRegister (const k78k0_parsed_instruction *instruction,
                                  const char *reg)
{
  const char *mnemonic = instruction->mnemonic;

  if (!strcmp (mnemonic, "mov") || !strcmp (mnemonic, "movw") ||
      !strcmp (mnemonic, "pop"))
    return instruction->operand_count >= 1 &&
      operandSurelyWritesRegister (instruction->operand[0], reg);

  if (!strcmp (mnemonic, "xch") || !strcmp (mnemonic, "xchw"))
    return instruction->operand_count == 2 &&
      (operandSurelyWritesRegister (instruction->operand[0], reg) ||
       operandSurelyWritesRegister (instruction->operand[1], reg));

  if (!strcmp (mnemonic, "add") || !strcmp (mnemonic, "addc") ||
      !strcmp (mnemonic, "addw") || !strcmp (mnemonic, "sub") ||
      !strcmp (mnemonic, "subc") || !strcmp (mnemonic, "subw") ||
      !strcmp (mnemonic, "and") || !strcmp (mnemonic, "or") ||
      !strcmp (mnemonic, "xor") || !strcmp (mnemonic, "inc") ||
      !strcmp (mnemonic, "incw") || !strcmp (mnemonic, "decw") ||
      !strcmp (mnemonic, "rol") || !strcmp (mnemonic, "rolc") ||
      !strcmp (mnemonic, "ror"))
    return instruction->operand_count >= 1 &&
      operandSurelyWritesRegister (instruction->operand[0], reg);

  if (!strcmp (mnemonic, "divuw"))
    return !strcmp (reg, "a") || !strcmp (reg, "x") || !strcmp (reg, "c");
  if (!strcmp (mnemonic, "mulu"))
    return !strcmp (reg, "a") || !strcmp (reg, "x");

  return false;
}

static int
registerIndex (const char *reg)
{
  if (!strcmp (reg, "x"))
    return K78K0_RB0_X_IDX;
  if (!strcmp (reg, "a"))
    return K78K0_RB0_A_IDX;
  if (!strcmp (reg, "c"))
    return K78K0_RB0_C_IDX;
  if (!strcmp (reg, "b"))
    return K78K0_RB0_B_IDX;
  if (!strcmp (reg, "e"))
    return K78K0_RB0_E_IDX;
  if (!strcmp (reg, "d"))
    return K78K0_RB0_D_IDX;
  if (!strcmp (reg, "l"))
    return K78K0_RB0_L_IDX;
  if (!strcmp (reg, "h"))
    return K78K0_RB0_H_IDX;
  return -1;
}

static sym_link *
peepFunctionType (sym_link *type)
{
  return type && IS_FUNCPTR (type) ? type->next : type;
}

static sym_link *
callFunctionType (const lineNode *line)
{
  operand *callee;
  sym_link *type;

  if (!line || !line->ic || line->ic->op != CALL)
    return NULL;
  callee = IC_LEFT (line->ic);
  if (!callee || IS_OP_LITERAL (callee))
    return NULL;
  type = peepFunctionType (operandType (callee));
  return type && IS_FUNC (type) ? type : NULL;
}

static int
callRegisterArgumentSize (sym_link *type)
{
  for (value *argument = FUNC_ARGS (type); argument; argument = argument->next)
    if (SPEC_REGPARM (argument->etype))
      return getSize (argument->type);
  return 0;
}

static bool
registerCarriesArgument (const char *reg, const int size)
{
  if (!strcmp (reg, "a"))
    return size >= 1;
  if (!strcmp (reg, "x"))
    return size >= 2;
  if (!strcmp (reg, "c"))
    return size >= 3;
  if (!strcmp (reg, "b"))
    return size >= 4;
  return false;
}

static bool
callMightReadRegister (const lineNode *line, const char *reg)
{
  sym_link *type = callFunctionType (line);

  if (!type || IFFUNC_ISNAKED (type))
    return true;
  if (registerCarriesArgument (reg, callRegisterArgumentSize (type)))
    return true;

  /* Hidden result pointers and indirect-call argument parking use DE. */
  if ((!strcmp (reg, "d") || !strcmp (reg, "e")) && type->next &&
      !IS_VOID (type->next) &&
      getSize (type->next) > K78K0_PEEP_REGISTER_RETURN_BYTES)
    return true;
  return false;
}

static bool
callSurelyClobbersRegister (const lineNode *line, const char *reg)
{
  sym_link *type = callFunctionType (line);
  const int index = registerIndex (reg);

  if (!type || index < 0 || IFFUNC_ISNAKED (type) ||
      FUNC_CALLEESAVES (type) || options.all_callee_saves)
    return false;
  if (type->funcAttrs.preserved_regs[index])
    return false;

  return strcmp (reg, "d") && strcmp (reg, "e");
}

static bool
functionReturnReadsRegister (lineNode *head, const char *reg)
{
  const lineNode *line;
  sym_link *type;
  sym_link *return_type;
  int size;

  /* DE is the sole general-purpose callee-saved pair. */
  if (!strcmp (reg, "d") || !strcmp (reg, "e"))
    return true;

  for (line = head; line; line = line->next)
    if (line->ic && line->ic->op == FUNCTION)
      break;
  if (!line || !IC_LEFT (line->ic))
    return true;

  type = peepFunctionType (operandType (IC_LEFT (line->ic)));
  return_type = type && IS_FUNC (type) ? type->next : NULL;
  if (!return_type || IS_VOID (return_type))
    return false;

  size = getSize (return_type);
  if (size > K78K0_PEEP_REGISTER_RETURN_BYTES)
    return false;
  if (!strcmp (reg, "a"))
    return size >= 1;
  if (!strcmp (reg, "x"))
    return size >= 2;
  if (!strcmp (reg, "c"))
    return size >= 3;
  if (!strcmp (reg, "b"))
    return size >= 4;
  return false;
}

static lineNode *
findBranchTarget (lineNode *head, const k78k0_parsed_instruction *instruction)
{
  const char *target;

  if (!instruction->operand_count)
    return NULL;
  target = instruction->operand[instruction->operand_count - 1];
  if (*target == '!')
    target++;

  for (lineNode *line = head; line; line = line->next)
    if (line->isLabel && line->line)
      {
        const size_t length = strlen (target);
        const char *start = line->line;

        while (*start && isspace ((unsigned char)*start))
          start++;
        if (!strncmp (start, target, length) && start[length] == ':')
          return line;
      }
  return NULL;
}

static void
unvisitLines (lineNode *head)
{
  for (lineNode *line = head; line; line = line->next)
    line->visited = false;
}

static bool
scanForDeadRegister (lineNode *line, lineNode *head, const char *reg)
{
  for (; line; line = line->next)
    {
      k78k0_parsed_instruction instruction;

      if (!line->line || line->isDebug || line->isComment || line->isLabel)
        continue;
      if (line->isInline || line->visited ||
          !parseInstruction (line, &instruction))
        return line->visited;
      line->visited = true;

      if (!strcmp (instruction.mnemonic, "call"))
        {
          if (callMightReadRegister (line, reg))
            return false;
          if (callSurelyClobbersRegister (line, reg))
            return true;
          continue;
        }

      if (!strcmp (instruction.mnemonic, "ret"))
        {
          /* An indirect call is lowered to push return address, push target,
             ret.  That ret transfers control to the callee rather than
             returning from the current function, so all parked argument
             registers must remain live across it. */
          if (line->ic && line->ic->op == PCALL)
            return false;
          return !functionReturnReadsRegister (head, reg);
        }
      if (!strcmp (instruction.mnemonic, "reti"))
        return false;

      if (instructionMightReadRegister (&instruction, reg))
        return false;
      if (instructionSurelyWritesRegister (&instruction, reg))
        return true;

      if (!strcmp (instruction.mnemonic, "br"))
        {
          line = findBranchTarget (head, &instruction);
          if (!line)
            return false;
          continue;
        }

      if (isConditionalBranch (instruction.mnemonic))
        {
          lineNode *target = findBranchTarget (head, &instruction);

          if (!target || !scanForDeadRegister (target, head, reg))
            return false;
          continue;
        }
    }
  return false;
}

static bool
notUsedByte (const char *reg, lineNode *start, lineNode *head)
{
  if (registerIndex (reg) < 0)
    return false;
  unvisitLines (head);
  return scanForDeadRegister (start, head, reg);
}

bool
k78k0notUsed (const char *what, lineNode *end, lineNode *head)
{
  if (!strcmp (what, "ax"))
    return notUsedByte ("a", end->next, head) &&
      notUsedByte ("x", end->next, head);
  if (!strcmp (what, "bc"))
    return notUsedByte ("b", end->next, head) &&
      notUsedByte ("c", end->next, head);
  if (!strcmp (what, "de"))
    return notUsedByte ("d", end->next, head) &&
      notUsedByte ("e", end->next, head);
  if (!strcmp (what, "hl"))
    return notUsedByte ("h", end->next, head) &&
      notUsedByte ("l", end->next, head);
  return notUsedByte (what, end->next, head);
}

bool
k78k0notUsedFrom (const char *what, const char *label, lineNode *head)
{
  for (lineNode *line = head; line; line = line->next)
    if (line->isLabel && line->line && !strncmp (line->line, label, strlen (label)))
      return k78k0notUsed (what, line, head);
  return false;
}
