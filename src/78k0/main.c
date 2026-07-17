/*-------------------------------------------------------------------------
  main.c - 78K0 specific definitions.

  Copyright (C) 2026

  This program is free software; you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by the
  Free Software Foundation; either version 2, or (at your option) any
  later version.
-------------------------------------------------------------------------*/

#include "common.h"

#include "ralloc.h"
#include "gen.h"
#include "dbuf_string.h"

extern const char *iComments2;
extern DEBUGFILE dwarf2DebugFile;
extern int dwarf2FinalizeFile(FILE *);

static OPTION k78k0_options[] = {
  {0, NULL}
};

static char *k78k0_keywords[] = {
  "at",
  "code",
  "critical",
  "data",
  "far",
  "idata",
  "interrupt",
  "naked",
  "near",
  "pdata",
  "reentrant",
  "sfr",
  "xdata",
  0
};

static char k78k0_defaultRules[] = {
#include "peeph.rul"
};

static struct
{
  int n;
  sym_link *ftype;
}
k78k0_regparam;

static const char *_linkCmd[] =
{
  "sdld78k0", "-nf", "\"$1\"", NULL
};

static const char *k78k0AsmCmd[] =
{
  "sdas78k0", "$l", "$3", "\"$1.asm\"", NULL
};

static const char *const _libs_k78k0[] = { "78k0", NULL, };

static void
k78k0_init (void)
{
  asm_addTree (&asm_asxxxx_mapping);
}

static bool
k78k0_parseOptions (int *pargc, char **argv, int *i)
{
  (void)pargc;
  (void)argv;
  (void)i;
  return false;
}

static void
k78k0_reset_regparm (struct sym_link *ftype)
{
  k78k0_regparam.n = 0;
  k78k0_regparam.ftype = ftype;
}

static int
k78k0_reg_parm (sym_link *l, bool reentrant)
{
  (void)reentrant;

  k78k0_regparam.n++;

  if (options.noRegParams || !k78k0_regparam.ftype)
    return 0;

  if (k78k0_regparam.n != 1)
    return 0;

  if (IS_STRUCT (l) || getSize (l) < 1 || getSize (l) > 4)
    return 0;

  return 1;
}

static void
k78k0_finaliseOptions (void)
{
  port->mem.default_local_map = data;
  port->mem.default_globl_map = data;
}

static void
k78k0_setDefaultOptions (void)
{
  options.nopeep = 0;
  options.stackAuto = 1;
  options.intlong_rent = 1;
  options.float_rent = 1;
  options.noRegParams = 0;

  options.code_loc = 0x0000;
  options.data_loc = 0xfb00;
  options.idata_loc = 0xfb00;
  options.xdata_loc = 0;
  options.xstack_loc = 0;
  options.stack_loc = 0xfee0;

  options.out_fmt = 'i';
}

static const char *
k78k0_getRegName (const struct reg_info *reg)
{
  if (reg)
    return reg->name;
  return "err";
}

static void
k78k0_genAssemblerStart (FILE *of)
{
  if (options.noOptsdccInAsm)
    return;

  fprintf (of, "\t.optsdcc -m%s\n", port->target);
}

static void
k78k0_genAssemblerEnd (FILE *of)
{
  if (options.out_fmt == 'E' && options.debug)
    dwarf2FinalizeFile (of);
}

static void
k78k0_genExtraAreas (FILE *of, bool mainExists)
{
  (void)mainExists;

  if (sfr)
    {
      fprintf (of, "%s", iComments2);
      fprintf (of, "; special function registers\n");
      fprintf (of, "%s", iComments2);
      dbuf_write_and_destroy (&sfr->oBuf, of);
    }

  if (xdata)
    {
      fprintf (of, "%s", iComments2);
      fprintf (of, "; external data\n");
      fprintf (of, "%s", iComments2);
      dbuf_write_and_destroy (&xdata->oBuf, of);
    }
}

static int
k78k0_genIVT (struct dbuf_s *oBuf, symbol **intTable, int intCount)
{
  const int interrupt_vector_count = 30;

  if (intCount > interrupt_vector_count)
    {
      werror (E_INT_BAD_INTNO, intCount - 1);
      intCount = interrupt_vector_count;
    }

  dbuf_tprintf (oBuf, "\t!dws\n", "s_GSINIT");
  dbuf_tprintf (oBuf, "\t!dws\n", "0x0000");

  for (int i = 0; i < interrupt_vector_count; i++)
    dbuf_tprintf (oBuf, "\t!dws\n", i < intCount && intTable[i] ? intTable[i]->rname : "0x0000");

  return true;
}

static void
k78k0_genInitStartup (FILE *of)
{
  fprintf (of, "\tmovw\tsp,#0x%04x\n", options.stack_loc & 0xffff);
  fputs ("\tmovw\thl,#s_DATA\n"
         "\tmovw\tbc,#l_DATA\n"
         "00003$:\n"
         "\tmov\ta,c\n"
         "\tor\ta,b\n"
         "\tbz\t00004$\n"
         "\tmov\ta,#0x00\n"
         "\tmov\t[hl],a\n"
         "\tincw\thl\n"
         "\tdecw\tbc\n"
         "\tbr\t00003$\n"
         "00004$:\n"
         "\tmovw\tde,#s_INITIALIZER\n"
         "\tmovw\thl,#s_INITIALIZED\n"
         "\tmovw\tbc,#l_INITIALIZER\n"
         "00001$:\n"
         "\tmov\ta,c\n"
         "\tor\ta,b\n"
         "\tbz\t00002$\n"
         "\tmov\ta,[de]\n"
         "\tmov\t[hl],a\n"
         "\tincw\tde\n"
         "\tincw\thl\n"
         "\tdecw\tbc\n"
         "\tbr\t00001$\n"
         "00002$:\n",
         of);
}

static int
k78k0_dwarfRegNum (const struct reg_info *reg)
{
  return reg->rIdx;
}

struct k78k0_span
{
  const char *text;
  size_t length;
};

struct k78k0_instruction_view
{
  struct k78k0_span mnemonic;
  struct k78k0_span operand[2];
  unsigned operand_count;
  bool compiler_generated;
};

enum k78k0_operand_class
{
  K78K0_OPERAND_NONE,
  K78K0_OPERAND_A,
  K78K0_OPERAND_BYTE_REGISTER,
  K78K0_OPERAND_REGISTER_PAIR,
  K78K0_OPERAND_SP,
  K78K0_OPERAND_PSW,
  K78K0_OPERAND_IMMEDIATE,
  K78K0_OPERAND_SADDR,
  K78K0_OPERAND_SFR,
  K78K0_OPERAND_ADDR16,
  K78K0_OPERAND_DE,
  K78K0_OPERAND_HL,
  K78K0_OPERAND_HL_DISP,
  K78K0_OPERAND_HL_BC,
  K78K0_OPERAND_BIT_CY,
  K78K0_OPERAND_BIT_A,
  K78K0_OPERAND_BIT_PSW,
  K78K0_OPERAND_BIT_HL,
  K78K0_OPERAND_BIT_SADDR,
  K78K0_OPERAND_BIT_SFR
};

enum k78k0_instruction_kind
{
  K78K0_INSTRUCTION_FIXED,
  K78K0_INSTRUCTION_BRANCH,
  K78K0_INSTRUCTION_DBNZ,
  K78K0_INSTRUCTION_INC_DEC,
  K78K0_INSTRUCTION_BIT_SET_CLEAR,
  K78K0_INSTRUCTION_BIT_CARRY,
  K78K0_INSTRUCTION_BIT_BRANCH,
  K78K0_INSTRUCTION_BIT_BRANCH_SHORT,
  K78K0_INSTRUCTION_MOVE,
  K78K0_INSTRUCTION_MOVE_WORD,
  K78K0_INSTRUCTION_BYTE_ALU,
  K78K0_INSTRUCTION_EXCHANGE
};

struct k78k0_instruction_spec
{
  const char *mnemonic;
  enum k78k0_instruction_kind kind;
  unsigned char operands;
  unsigned char size;
};

enum
{
  K78K0_INVALID_INSTRUCTION_SIZE = 999
};

/* Keep this table sorted case-insensitively by mnemonic for bsearch.  Keeping
   each mnemonic, arity, and sizing family together also makes this list easy
   to compare with the assembler table and the instruction-set manual. */
static const struct k78k0_instruction_spec k78k0_instruction_specs[] =
{
  {"add",   K78K0_INSTRUCTION_BYTE_ALU,         2, 0},
  {"addc",  K78K0_INSTRUCTION_BYTE_ALU,         2, 0},
  {"addw",  K78K0_INSTRUCTION_FIXED,            2, 3},
  {"adjba", K78K0_INSTRUCTION_FIXED,            0, 2},
  {"adjbs", K78K0_INSTRUCTION_FIXED,            0, 2},
  {"and",   K78K0_INSTRUCTION_BYTE_ALU,         2, 0},
  {"and1",  K78K0_INSTRUCTION_BIT_CARRY,        2, 0},
  {"bc",    K78K0_INSTRUCTION_FIXED,            1, 2},
  {"bf",    K78K0_INSTRUCTION_BIT_BRANCH,       2, 0},
  {"bnc",   K78K0_INSTRUCTION_FIXED,            1, 2},
  {"bnz",   K78K0_INSTRUCTION_FIXED,            1, 2},
  {"br",    K78K0_INSTRUCTION_BRANCH,           1, 0},
  {"brk",   K78K0_INSTRUCTION_FIXED,            0, 1},
  {"bt",    K78K0_INSTRUCTION_BIT_BRANCH_SHORT, 2, 0},
  {"btclr", K78K0_INSTRUCTION_BIT_BRANCH,       2, 0},
  {"bz",    K78K0_INSTRUCTION_FIXED,            1, 2},
  {"call",  K78K0_INSTRUCTION_FIXED,            1, 3},
  {"callf", K78K0_INSTRUCTION_FIXED,            1, 2},
  {"callt", K78K0_INSTRUCTION_FIXED,            1, 1},
  {"clr1",  K78K0_INSTRUCTION_BIT_SET_CLEAR,    1, 0},
  {"cmp",   K78K0_INSTRUCTION_BYTE_ALU,         2, 0},
  {"cmpw",  K78K0_INSTRUCTION_FIXED,            2, 3},
  {"dbnz",  K78K0_INSTRUCTION_DBNZ,             2, 0},
  {"dec",   K78K0_INSTRUCTION_INC_DEC,          1, 0},
  {"decw",  K78K0_INSTRUCTION_FIXED,            1, 1},
  {"di",    K78K0_INSTRUCTION_FIXED,            0, 2},
  {"divuw", K78K0_INSTRUCTION_FIXED,            1, 2},
  {"ei",    K78K0_INSTRUCTION_FIXED,            0, 2},
  {"halt",  K78K0_INSTRUCTION_FIXED,            0, 2},
  {"inc",   K78K0_INSTRUCTION_INC_DEC,          1, 0},
  {"incw",  K78K0_INSTRUCTION_FIXED,            1, 1},
  {"mov",   K78K0_INSTRUCTION_MOVE,             2, 0},
  {"mov1",  K78K0_INSTRUCTION_BIT_CARRY,        2, 0},
  {"movw",  K78K0_INSTRUCTION_MOVE_WORD,        2, 0},
  {"mulu",  K78K0_INSTRUCTION_FIXED,            1, 2},
  {"nop",   K78K0_INSTRUCTION_FIXED,            0, 1},
  {"not1",  K78K0_INSTRUCTION_FIXED,            1, 1},
  {"or",    K78K0_INSTRUCTION_BYTE_ALU,         2, 0},
  {"or1",   K78K0_INSTRUCTION_BIT_CARRY,        2, 0},
  {"pop",   K78K0_INSTRUCTION_FIXED,            1, 1},
  {"push",  K78K0_INSTRUCTION_FIXED,            1, 1},
  {"ret",   K78K0_INSTRUCTION_FIXED,            0, 1},
  {"retb",  K78K0_INSTRUCTION_FIXED,            0, 1},
  {"reti",  K78K0_INSTRUCTION_FIXED,            0, 1},
  {"rol",   K78K0_INSTRUCTION_FIXED,            2, 1},
  {"rol4",  K78K0_INSTRUCTION_FIXED,            1, 2},
  {"rolc",  K78K0_INSTRUCTION_FIXED,            2, 1},
  {"ror",   K78K0_INSTRUCTION_FIXED,            2, 1},
  {"ror4",  K78K0_INSTRUCTION_FIXED,            1, 2},
  {"rorc",  K78K0_INSTRUCTION_FIXED,            2, 1},
  {"sel",   K78K0_INSTRUCTION_FIXED,            1, 2},
  {"set1",  K78K0_INSTRUCTION_BIT_SET_CLEAR,    1, 0},
  {"stop",  K78K0_INSTRUCTION_FIXED,            0, 2},
  {"sub",   K78K0_INSTRUCTION_BYTE_ALU,         2, 0},
  {"subc",  K78K0_INSTRUCTION_BYTE_ALU,         2, 0},
  {"subw",  K78K0_INSTRUCTION_FIXED,            2, 3},
  {"xch",   K78K0_INSTRUCTION_EXCHANGE,         2, 0},
  {"xchw",  K78K0_INSTRUCTION_FIXED,            2, 1},
  {"xor",   K78K0_INSTRUCTION_BYTE_ALU,         2, 0},
  {"xor1",  K78K0_INSTRUCTION_BIT_CARRY,        2, 0}
};

static struct k78k0_span
k78k0_trimSpan (struct k78k0_span span)
{
  while (span.length && isspace ((unsigned char)*span.text))
    span.text++, span.length--;
  while (span.length && isspace ((unsigned char)span.text[span.length - 1]))
    span.length--;
  return span;
}

static bool
k78k0_spanEqual (const struct k78k0_span span, const char *text)
{
  const size_t length = strlen (text);

  return span.length == length && !STRNCASECMP (span.text, text, length);
}

static int
k78k0_compareInstruction (const void *key, const void *member)
{
  const struct k78k0_span *mnemonic = key;
  const struct k78k0_instruction_spec *spec = member;
  const size_t spec_length = strlen (spec->mnemonic);
  const size_t common_length = mnemonic->length < spec_length ?
    mnemonic->length : spec_length;
  const int result = STRNCASECMP (mnemonic->text, spec->mnemonic,
                                  common_length);

  if (result)
    return result;
  return mnemonic->length < spec_length ? -1 :
    mnemonic->length > spec_length;
}

static const struct k78k0_instruction_spec *
k78k0_findInstruction (const struct k78k0_span mnemonic)
{
  return bsearch (&mnemonic, k78k0_instruction_specs,
                  sizeof k78k0_instruction_specs /
                    sizeof *k78k0_instruction_specs,
                  sizeof *k78k0_instruction_specs,
                  k78k0_compareInstruction);
}

static bool
k78k0_splitOperands (const char *text, struct k78k0_instruction_view *view)
{
  const char *end = strchr (text, ';');
  struct k78k0_span left;
  struct k78k0_span right;

  if (!end)
    end = text + strlen (text);
  view->operand_count = 0;

  const char *comma = memchr (text, ',', (size_t)(end - text));
  if (!comma)
    {
      left = k78k0_trimSpan (
        (struct k78k0_span){text, (size_t)(end - text)});
      if (left.length)
        view->operand[view->operand_count++] = left;
      return true;
    }

  if (memchr (comma + 1, ',', (size_t)(end - comma - 1)))
    return false;
  left = k78k0_trimSpan (
    (struct k78k0_span){text, (size_t)(comma - text)});
  right = k78k0_trimSpan (
    (struct k78k0_span){comma + 1, (size_t)(end - comma - 1)});
  if (!left.length || !right.length)
    return false;

  view->operand[0] = left;
  view->operand[1] = right;
  view->operand_count = 2;
  return true;
}

static bool
k78k0_absoluteValue (struct k78k0_span span, unsigned long *value)
{
  unsigned base = 10;
  unsigned long result = 0;

  span = k78k0_trimSpan (span);
  if (span.length > 2 && span.text[0] == '0' &&
      tolower ((unsigned char)span.text[1]) == 'x')
    span.text += 2, span.length -= 2, base = 16;
  if (!span.length)
    return false;
  for (size_t i = 0; i < span.length; i++)
    {
      const unsigned char c = (unsigned char)tolower ((unsigned char)span.text[i]);
      const unsigned digit = isdigit (c) ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : base;

      if (digit >= base || result > (ULONG_MAX - digit) / base)
        return false;
      result = result * base + digit;
    }
  *value = result;
  return true;
}

static enum k78k0_operand_class
k78k0_classifyDirectValue (const unsigned long address, const bool bit)
{
  if (address >= 0xfe20ul && address <= 0xff1ful)
    return bit ? K78K0_OPERAND_BIT_SADDR : K78K0_OPERAND_SADDR;
  if ((address >= 0xff00ul && address <= 0xffcful) ||
      (address >= 0xffe0ul && address <= 0xfffful))
    return bit ? K78K0_OPERAND_BIT_SFR : K78K0_OPERAND_SFR;
  return K78K0_OPERAND_NONE;
}

static enum k78k0_operand_class
k78k0_classifyOperand (struct k78k0_span operand, const bool compiler_generated)
{
  static const struct
  {
    const char *text;
    enum k78k0_operand_class class;
  }
  exact[] = {
    {"x", K78K0_OPERAND_BYTE_REGISTER}, {"a", K78K0_OPERAND_A},
    {"c", K78K0_OPERAND_BYTE_REGISTER}, {"b", K78K0_OPERAND_BYTE_REGISTER},
    {"e", K78K0_OPERAND_BYTE_REGISTER}, {"d", K78K0_OPERAND_BYTE_REGISTER},
    {"l", K78K0_OPERAND_BYTE_REGISTER}, {"h", K78K0_OPERAND_BYTE_REGISTER},
    {"ax", K78K0_OPERAND_REGISTER_PAIR}, {"bc", K78K0_OPERAND_REGISTER_PAIR},
    {"de", K78K0_OPERAND_REGISTER_PAIR}, {"hl", K78K0_OPERAND_REGISTER_PAIR},
    {"sp", K78K0_OPERAND_SP}, {"psw", K78K0_OPERAND_PSW},
    {"[de]", K78K0_OPERAND_DE}, {"[hl]", K78K0_OPERAND_HL}
  };
  struct k78k0_span base;
  struct k78k0_span suffix;
  unsigned long address;

  operand = k78k0_trimSpan (operand);
  if (!operand.length)
    return K78K0_OPERAND_NONE;
  if (k78k0_spanEqual (operand, "cy"))
    return K78K0_OPERAND_BIT_CY;
  for (size_t i = operand.length; i-- > 0;)
    if (operand.text[i] == '.')
      {
        base.text = operand.text;
        base.length = i;
        suffix.text = operand.text + i + 1;
        suffix.length = operand.length - i - 1;
        if (!compiler_generated &&
            (!k78k0_absoluteValue (suffix, &address) || address > 7))
          return K78K0_OPERAND_NONE;
        if (k78k0_spanEqual (base, "a"))
          return K78K0_OPERAND_BIT_A;
        if (k78k0_spanEqual (base, "psw"))
          return K78K0_OPERAND_BIT_PSW;
        if (k78k0_spanEqual (base, "[hl]"))
          return K78K0_OPERAND_BIT_HL;
        if (k78k0_absoluteValue (base, &address))
          return k78k0_classifyDirectValue (address, true);
        return compiler_generated ? K78K0_OPERAND_BIT_SFR : K78K0_OPERAND_NONE;
      }
  for (size_t i = 0; i < sizeof exact / sizeof *exact; i++)
    if (k78k0_spanEqual (operand, exact[i].text))
      return exact[i].class;
  if (operand.text[0] == '#')
    return compiler_generated || operand.length > 1 ?
      K78K0_OPERAND_IMMEDIATE : K78K0_OPERAND_NONE;
  if (operand.text[0] == '!')
    return compiler_generated || operand.length > 1 ?
      K78K0_OPERAND_ADDR16 : K78K0_OPERAND_NONE;
  if (operand.length >= 4 && !STRNCASECMP (operand.text, "[hl+", 4))
    {
      struct k78k0_span displacement;

      if (k78k0_spanEqual (operand, "[hl+b]") || k78k0_spanEqual (operand, "[hl+c]"))
        return K78K0_OPERAND_HL_BC;
      if (compiler_generated)
        return K78K0_OPERAND_HL_DISP;
      if (operand.length < 6 || operand.text[operand.length - 1] != ']')
        return K78K0_OPERAND_NONE;
      displacement.text = operand.text + 4;
      displacement.length = operand.length - 5;
      return k78k0_absoluteValue (displacement, &address) && address <= 0xfful ?
        K78K0_OPERAND_HL_DISP : K78K0_OPERAND_NONE;
    }
  if (k78k0_absoluteValue (operand, &address))
    return k78k0_classifyDirectValue (address, false);
  return compiler_generated ? K78K0_OPERAND_SADDR : K78K0_OPERAND_NONE;
}

static int
k78k0_memorySize (const enum k78k0_operand_class operand, const bool prefix)
{
  switch (operand)
    {
    case K78K0_OPERAND_DE:
    case K78K0_OPERAND_HL:
      return 1;
    case K78K0_OPERAND_HL_DISP:
      return 2;
    case K78K0_OPERAND_HL_BC:
      return prefix + 1;
    default:
      return K78K0_INVALID_INSTRUCTION_SIZE;
    }
}

static int
k78k0_directSize (const enum k78k0_operand_class operand)
{
  switch (operand)
    {
    case K78K0_OPERAND_ADDR16:
      return 3;
    case K78K0_OPERAND_SADDR:
    case K78K0_OPERAND_SFR:
      return 2;
    default:
      return K78K0_INVALID_INSTRUCTION_SIZE;
    }
}

static bool
k78k0_isMemoryOperand (const enum k78k0_operand_class operand)
{
  return operand == K78K0_OPERAND_DE || operand == K78K0_OPERAND_HL ||
    operand == K78K0_OPERAND_HL_DISP || operand == K78K0_OPERAND_HL_BC;
}

static bool
k78k0_isDirectOperand (const enum k78k0_operand_class operand)
{
  return operand == K78K0_OPERAND_SADDR || operand == K78K0_OPERAND_SFR ||
    operand == K78K0_OPERAND_ADDR16;
}

static int
k78k0_sizeBitInstruction (const enum k78k0_instruction_kind kind,
                          const enum k78k0_operand_class left,
                          const enum k78k0_operand_class right)
{
  if (kind == K78K0_INSTRUCTION_BIT_SET_CLEAR)
    return left == K78K0_OPERAND_BIT_CY ? 1 :
      left == K78K0_OPERAND_BIT_SFR ? 3 :
      left >= K78K0_OPERAND_BIT_A ? 2 :
      K78K0_INVALID_INSTRUCTION_SIZE;

  if (kind == K78K0_INSTRUCTION_BIT_CARRY)
    {
      const enum k78k0_operand_class bit =
        left == K78K0_OPERAND_BIT_CY ? right : left;
      return bit == K78K0_OPERAND_BIT_A || bit == K78K0_OPERAND_BIT_HL ? 2 :
        bit >= K78K0_OPERAND_BIT_PSW ? 3 :
        K78K0_INVALID_INSTRUCTION_SIZE;
    }

  if (kind == K78K0_INSTRUCTION_BIT_BRANCH ||
      kind == K78K0_INSTRUCTION_BIT_BRANCH_SHORT)
    {
      if (left == K78K0_OPERAND_BIT_A || left == K78K0_OPERAND_BIT_HL)
        return 3;
      return kind == K78K0_INSTRUCTION_BIT_BRANCH_SHORT &&
        (left == K78K0_OPERAND_BIT_SADDR || left == K78K0_OPERAND_BIT_PSW) ? 3 :
        left >= K78K0_OPERAND_BIT_PSW ? 4 :
        K78K0_INVALID_INSTRUCTION_SIZE;
    }

  return K78K0_INVALID_INSTRUCTION_SIZE;
}

static int
k78k0_sizeMove (const enum k78k0_operand_class destination,
                 const enum k78k0_operand_class source)
{
  if (destination == K78K0_OPERAND_A)
    {
      if (source == K78K0_OPERAND_IMMEDIATE)
        return 2;
      if (k78k0_isMemoryOperand (source))
        return k78k0_memorySize (source, false);
      if (source == K78K0_OPERAND_BYTE_REGISTER)
        return 1;
      if (source == K78K0_OPERAND_PSW)
        return 2;
      return k78k0_directSize (source);
    }
  if (destination == K78K0_OPERAND_PSW)
    return source == K78K0_OPERAND_IMMEDIATE ? 3 :
      source == K78K0_OPERAND_A ? 2 :
      K78K0_INVALID_INSTRUCTION_SIZE;
  if (destination == K78K0_OPERAND_BYTE_REGISTER)
    return source == K78K0_OPERAND_IMMEDIATE ? 2 :
      source == K78K0_OPERAND_A ? 1 :
      K78K0_INVALID_INSTRUCTION_SIZE;
  if (k78k0_isMemoryOperand (destination))
    return source == K78K0_OPERAND_A ? k78k0_memorySize (destination, false) :
      K78K0_INVALID_INSTRUCTION_SIZE;
  if (k78k0_isDirectOperand (destination))
    return source == K78K0_OPERAND_IMMEDIATE ? 3 :
      source == K78K0_OPERAND_A ? k78k0_directSize (destination) :
      K78K0_INVALID_INSTRUCTION_SIZE;
  return K78K0_INVALID_INSTRUCTION_SIZE;
}

static int
k78k0_sizeMoveWord (const enum k78k0_operand_class destination,
                     const enum k78k0_operand_class source)
{
  if (destination == K78K0_OPERAND_REGISTER_PAIR)
    return source == K78K0_OPERAND_IMMEDIATE ? 3 :
      source == K78K0_OPERAND_SP ? 2 :
      source == K78K0_OPERAND_REGISTER_PAIR ? 1 : k78k0_directSize (source);
  if (destination == K78K0_OPERAND_SP)
    return source == K78K0_OPERAND_IMMEDIATE ? 4 :
      source == K78K0_OPERAND_REGISTER_PAIR ? 2 :
      K78K0_INVALID_INSTRUCTION_SIZE;
  if (k78k0_isDirectOperand (destination))
    return source == K78K0_OPERAND_IMMEDIATE ? 4 :
      source == K78K0_OPERAND_REGISTER_PAIR ? k78k0_directSize (destination) :
      K78K0_INVALID_INSTRUCTION_SIZE;
  return K78K0_INVALID_INSTRUCTION_SIZE;
}

static int
k78k0_sizeByteAlu (const enum k78k0_operand_class destination,
                    const enum k78k0_operand_class source)
{
  if (destination == K78K0_OPERAND_A)
    {
      if (source == K78K0_OPERAND_IMMEDIATE ||
          source == K78K0_OPERAND_BYTE_REGISTER)
        return 2;
      if (k78k0_isMemoryOperand (source))
        return k78k0_memorySize (source, true);
      return k78k0_directSize (source);
    }
  return destination == K78K0_OPERAND_BYTE_REGISTER &&
    source == K78K0_OPERAND_A ? 2 :
    destination == K78K0_OPERAND_SADDR &&
    source == K78K0_OPERAND_IMMEDIATE ? 3 :
    K78K0_INVALID_INSTRUCTION_SIZE;
}

static int
k78k0_sizeInstruction (const struct k78k0_instruction_view *view)
{
  const struct k78k0_span left = view->operand_count ? view->operand[0] :
    (struct k78k0_span){NULL, 0};
  const struct k78k0_span right = view->operand_count > 1 ? view->operand[1] :
    (struct k78k0_span){NULL, 0};
  const struct k78k0_instruction_spec *spec =
    k78k0_findInstruction (view->mnemonic);

  if (!spec || view->operand_count != spec->operands)
    return K78K0_INVALID_INSTRUCTION_SIZE;
  if (spec->kind == K78K0_INSTRUCTION_FIXED)
    return spec->size;

  const enum k78k0_operand_class left_class =
    k78k0_classifyOperand (left, view->compiler_generated);
  const enum k78k0_operand_class right_class =
    k78k0_classifyOperand (right, view->compiler_generated);

  switch (spec->kind)
    {
    case K78K0_INSTRUCTION_BRANCH:
      return
      k78k0_spanEqual (left, "ax") ? 2 :
      left_class == K78K0_OPERAND_ADDR16 ? 3 :
      (view->compiler_generated || (left.length && left.text[left.length - 1] == '$')) ? 2 :
      K78K0_INVALID_INSTRUCTION_SIZE;
    case K78K0_INSTRUCTION_DBNZ:
      return
      k78k0_spanEqual (left, "b") || k78k0_spanEqual (left, "c") ? 2 :
      left_class == K78K0_OPERAND_SADDR ? 3 :
      K78K0_INVALID_INSTRUCTION_SIZE;
    case K78K0_INSTRUCTION_INC_DEC:
      return
      left_class == K78K0_OPERAND_A || left_class == K78K0_OPERAND_BYTE_REGISTER ? 1 :
      left_class == K78K0_OPERAND_SADDR ? 2 :
      K78K0_INVALID_INSTRUCTION_SIZE;
    case K78K0_INSTRUCTION_BIT_SET_CLEAR:
    case K78K0_INSTRUCTION_BIT_CARRY:
    case K78K0_INSTRUCTION_BIT_BRANCH:
    case K78K0_INSTRUCTION_BIT_BRANCH_SHORT:
      return k78k0_sizeBitInstruction (spec->kind, left_class, right_class);
    case K78K0_INSTRUCTION_MOVE:
      return k78k0_sizeMove (left_class, right_class);
    case K78K0_INSTRUCTION_MOVE_WORD:
      return k78k0_sizeMoveWord (left_class, right_class);
    case K78K0_INSTRUCTION_BYTE_ALU:
      return k78k0_sizeByteAlu (left_class, right_class);
    case K78K0_INSTRUCTION_EXCHANGE:
      if (left_class != K78K0_OPERAND_A)
        return K78K0_INVALID_INSTRUCTION_SIZE;
      if (k78k0_isMemoryOperand (right_class))
        return k78k0_memorySize (right_class, true);
      return right_class == K78K0_OPERAND_BYTE_REGISTER ? 1 : k78k0_directSize (right_class);
    case K78K0_INSTRUCTION_FIXED:
      break;
    }
  return K78K0_INVALID_INSTRUCTION_SIZE;
}

int
k78k0_instructionSize (const char *mnemonic, const char *operands)
{
  /* emit2() supplies canonical compiler output. */
  struct k78k0_instruction_view view = {
    {mnemonic, strlen (mnemonic)}, {{NULL, 0}, {NULL, 0}}, 0, true
  };

  return k78k0_splitOperands (operands, &view) ? k78k0_sizeInstruction (&view) :
    K78K0_INVALID_INSTRUCTION_SIZE;
}

static int
k78k0_instructionSizeLine (lineNode *line)
{
  /* Arbitrary assembly gets no compiler-only symbolic fallbacks. */
  struct k78k0_instruction_view view = {
    {NULL, 0}, {{NULL, 0}, {NULL, 0}}, 0, false
  };
  const char *cursor = line->line;

  while (isspace ((unsigned char)*cursor))
    cursor++;
  if (!*cursor || *cursor == ';')
    return 0;
  view.mnemonic.text = cursor;
  while (*cursor && !isspace ((unsigned char)*cursor) && *cursor != ';')
    cursor++;
  view.mnemonic.length = (size_t)(cursor - view.mnemonic.text);
  if (!view.mnemonic.length || view.mnemonic.text[0] == '.')
    return K78K0_INVALID_INSTRUCTION_SIZE;
  const char *colon = memchr (view.mnemonic.text, ':', view.mnemonic.length);
  if (colon)
    {
      while (isspace ((unsigned char)*cursor))
        cursor++;
      return colon == view.mnemonic.text + view.mnemonic.length - 1 &&
        (!*cursor || *cursor == ';') ? 0 :
        K78K0_INVALID_INSTRUCTION_SIZE;
    }
  return k78k0_splitOperands (cursor, &view) ? k78k0_sizeInstruction (&view) :
    K78K0_INVALID_INSTRUCTION_SIZE;
}

static bool
k78k0_isPromotedUnsignedByte (const iCode *ic)
{
  operand *const op = IC_RIGHT (ic);

  if (!IS_SYMOP (op) || bitVectnBitsOn (OP_DEFS (op)) != 1)
    return false;

  const iCode *def = hTabItemWithKey (iCodehTab, bitVectFirstBit (OP_DEFS (op)));
  if (!def || def->op != CAST || !IC_RIGHT (def))
    return false;

  sym_link *type = getSpec (operandType (IC_RIGHT (def)));
  return getSize (type) == 1 && SPEC_USIGN (type);
}

static bool
k78k0_hasNativeMulFor (iCode *ic, sym_link *left, sym_link *right)
{
  const int result_size = IS_SYMOP (IC_RESULT (ic)) ? getSize (OP_SYM_TYPE (IC_RESULT (ic))) : 4;

  if (ic->op != '*')
    {
      if (ic->op != '/' && ic->op != '%')
        return false;

      if (getSize (left) > 2 || !SPEC_USIGN (getSpec (left)))
        return false;

      return IS_LITERAL (right) ? ulFromVal (valFromType (right)) <= 255 :
        getSize (right) == 1 && SPEC_USIGN (getSpec (right)) ||
        k78k0_isPromotedUnsignedByte (ic);
    }

  if (IS_BITINT (OP_SYM_TYPE (IC_RESULT (ic))) &&
      SPEC_BITINTWIDTH (OP_SYM_TYPE (IC_RESULT (ic))) % 8)
    return false;

  if (IS_ITEMP (IC_RESULT (ic)) && result_size == 2 &&
      ((IS_LITERAL (left) && ulFromVal (valFromType (left)) <= 255 && getSize (right) == 2) ||
       (IS_LITERAL (right) && ulFromVal (valFromType (right)) <= 255 && getSize (left) == 2)))
    return true;

  return IS_ITEMP (IC_RESULT (ic)) && result_size <= 2 &&
    getSize (left) == 1 && getSize (right) == 1;
}

static bool
k78k0_hasExtBitOp (int op, sym_link *left, int right)
{
  const int size = getSize (left);

  switch (op)
    {
    case GETABIT:
      return right >= 0 && right < bitsForType (left);
    case GETBYTE:
      return right >= 0 && !(right % 8) && right / 8 < size;
    case GETWORD:
      return right >= 0 && !(right % 8) && right / 8 + 2 <= size;
    case ROT:
      return bitsForType (left) == 8 || bitsForType (left) == 16 || bitsForType (left) == 32;
    default:
      return false;
    }
}

PORT k78k0_port =
{
  TARGET_ID_78K0,
  "78k0",
  "78K0",
  NULL,
  {
    glue,
    true,
    NO_MODEL,
    NO_MODEL,
    0,
  },
  {
    k78k0AsmCmd,
    NULL,
    "-plosgffwy",
    "-plosgffw",
    0,
    ".asm"
  },
  {
    _linkCmd,
    NULL,
    NULL,
    ".rel",
    1,
    NULL,
    _libs_k78k0,
  },
  {
    k78k0_defaultRules,
    k78k0_instructionSizeLine,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
  },
  {
    1,
    2,
    2,
    4,
    8,
    2,
    2,
    2,
    2,
    2,
    1,
    4,
    64,
  },
  { 0x00, 0x40, 0x60, 0x80 },
  {
    "XSEG",
    "STACK",
    "CODE",
    "DATA",
    "DATA",
    "DATA",
    "DATA",
    NULL,
    NULL,
    "RSEG (ABS)",
    "GSINIT",
    NULL,
    "GSFINAL",
    "HOME",
    NULL,
    NULL,
    "CONST",
    "CABS (ABS)",
    "DABS (ABS)",
    NULL,
    "INITIALIZED",
    "INITIALIZER",
    NULL,
    NULL,
    1,
    false,
    1
  },
  { k78k0_genExtraAreas, NULL },
  1,
  {
    -1,
    0,
    4,
    2,
    0,
    2,
    0,
  },
  {
    -1,
    true,
    false,
  },
  { k78k0_emitDebuggerSymbol,
    {
      k78k0_dwarfRegNum,
      0,
      0,
      4,
      K78K0_RB0_A_IDX,
      K78K0_SP_IDX,
      0,
      2,
    },
  },
  {
    32767,
    2,
    {4, 5, 5},
    {4, 5, 5},
    3,
    5,
  },
  "_",
  k78k0_init,
  k78k0_parseOptions,
  k78k0_options,
  NULL,
  k78k0_finaliseOptions,
  k78k0_setDefaultOptions,
  k78k0_assignRegisters,
  k78k0_getRegName,
  0,
  NULL,
  k78k0_keywords,
  k78k0_genAssemblerStart,
  k78k0_genAssemblerEnd,
  k78k0_genIVT,
  0,
  k78k0_genInitStartup,
  k78k0_reset_regparm,
  k78k0_reg_parm,
  NULL,
  NULL,
  k78k0_hasNativeMulFor,
  k78k0_hasExtBitOp,
  NULL,
  TRUE,
  true,
  0,
  0,
  1,
  1,
  false,
  false,
  false,
  0,
  "",
  GPOINTER,
  false,
  false,
  1,
  1,
  8,
  PORT_MAGIC
};
