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

static bool
k78k0_splitOperands (const char *text, struct k78k0_instruction_view *view)
{
  const char *end = strchr (text, ';');
  const char *comma;

  if (!end)
    end = text + strlen (text);
  view->operand_count = 0;
  comma = memchr (text, ',', (size_t)(end - text));
  if (comma)
    {
      if (memchr (comma + 1, ',', (size_t)(end - comma - 1)))
        return false;
      view->operand[view->operand_count++] = k78k0_trimSpan (
        (struct k78k0_span){text, (size_t)(comma - text)});
      text = comma + 1;
    }
  struct k78k0_span operand = k78k0_trimSpan (
    (struct k78k0_span){text, (size_t)(end - text)});
  if (operand.length)
    view->operand[view->operand_count++] = operand;
  return (!comma || view->operand_count == 2) &&
    (!view->operand_count || view->operand[0].length);
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
    {"x", K78K0_OPERAND_BYTE_REGISTER}, {"a", K78K0_OPERAND_BYTE_REGISTER},
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

static bool
k78k0_isAccumulator (const struct k78k0_span operand)
{
  return k78k0_spanEqual (operand, "a");
}

static int
k78k0_memorySize (const enum k78k0_operand_class operand, const bool prefix)
{
  return operand == K78K0_OPERAND_DE || operand == K78K0_OPERAND_HL ? 1 :
    operand == K78K0_OPERAND_HL_DISP ? 2 : operand == K78K0_OPERAND_HL_BC ?
    prefix + 1 : 999;
}

static int
k78k0_directSize (const enum k78k0_operand_class operand)
{
  return operand == K78K0_OPERAND_ADDR16 ? 3 :
    operand == K78K0_OPERAND_SADDR || operand == K78K0_OPERAND_SFR ? 2 : 999;
}

static int
k78k0_fixedInstructionSize (const struct k78k0_span mnemonic,
                            const unsigned operand_count)
{
  static const struct
  {
    const char *mnemonic;
    unsigned char size;
    unsigned char operands;
  }
  instructions[] = {
    {"adjba", 2, 0}, {"adjbs", 2, 0}, {"addw", 3, 2}, {"bc", 2, 1},
    {"bnc", 2, 1}, {"bnz", 2, 1}, {"brk", 1, 0}, {"bz", 2, 1},
    {"call", 3, 1}, {"callf", 2, 1}, {"callt", 1, 1}, {"cmpw", 3, 2},
    {"decw", 1, 1}, {"di", 2, 0}, {"divuw", 2, 1}, {"ei", 2, 0},
    {"halt", 2, 0}, {"incw", 1, 1}, {"mulu", 2, 1}, {"nop", 1, 0},
    {"not1", 1, 1}, {"pop", 1, 1}, {"push", 1, 1}, {"ret", 1, 0},
    {"retb", 1, 0}, {"reti", 1, 0}, {"rol", 1, 2}, {"rol4", 2, 1},
    {"rolc", 1, 2}, {"ror", 1, 2}, {"ror4", 2, 1}, {"rorc", 1, 2},
    {"sel", 2, 1}, {"stop", 2, 0}, {"subw", 3, 2}, {"xchw", 1, 2}
  };

  for (size_t i = 0; i < sizeof instructions / sizeof *instructions; i++)
    if (k78k0_spanEqual (mnemonic, instructions[i].mnemonic))
      return operand_count == instructions[i].operands ? instructions[i].size : 999;
  return 0;
}

static int
k78k0_sizeInstruction (const struct k78k0_instruction_view *view)
{
  const struct k78k0_span left = view->operand_count ? view->operand[0] : (struct k78k0_span){NULL, 0};
  const struct k78k0_span right = view->operand_count > 1 ? view->operand[1] : (struct k78k0_span){NULL, 0};
  const int fixed_size = k78k0_fixedInstructionSize (view->mnemonic, view->operand_count);

  if (fixed_size)
    return fixed_size;

  const enum k78k0_operand_class left_class = k78k0_classifyOperand (left, view->compiler_generated);
  const enum k78k0_operand_class right_class = k78k0_classifyOperand (right, view->compiler_generated);

  if (k78k0_spanEqual (view->mnemonic, "br"))
    return view->operand_count != 1 ? 999 : k78k0_spanEqual (left, "ax") ? 2 :
      left_class == K78K0_OPERAND_ADDR16 ? 3 :
      (view->compiler_generated || (left.length && left.text[left.length - 1] == '$')) ? 2 : 999;
  if (k78k0_spanEqual (view->mnemonic, "dbnz"))
    return view->operand_count != 2 ? 999 :
      k78k0_spanEqual (left, "b") || k78k0_spanEqual (left, "c") ? 2 :
      left_class == K78K0_OPERAND_SADDR ? 3 : 999;
  if (k78k0_spanEqual (view->mnemonic, "inc") || k78k0_spanEqual (view->mnemonic, "dec"))
    return view->operand_count != 1 ? 999 :
      left_class == K78K0_OPERAND_BYTE_REGISTER ? 1 :
      left_class == K78K0_OPERAND_SADDR ? 2 : 999;

  if (k78k0_spanEqual (view->mnemonic, "set1") || k78k0_spanEqual (view->mnemonic, "clr1"))
    return view->operand_count != 1 ? 999 : left_class == K78K0_OPERAND_BIT_CY ? 1 :
      left_class == K78K0_OPERAND_BIT_SFR ? 3 :
      left_class >= K78K0_OPERAND_BIT_A ? 2 : 999;
  if (k78k0_spanEqual (view->mnemonic, "mov1") || k78k0_spanEqual (view->mnemonic, "and1") ||
      k78k0_spanEqual (view->mnemonic, "or1") || k78k0_spanEqual (view->mnemonic, "xor1"))
    {
      if (view->operand_count != 2)
        return 999;
      const enum k78k0_operand_class bit = left_class == K78K0_OPERAND_BIT_CY ? right_class : left_class;
      return bit == K78K0_OPERAND_BIT_A || bit == K78K0_OPERAND_BIT_HL ? 2 :
        bit >= K78K0_OPERAND_BIT_PSW ? 3 : 999;
    }
  if (k78k0_spanEqual (view->mnemonic, "bt") || k78k0_spanEqual (view->mnemonic, "bf") ||
      k78k0_spanEqual (view->mnemonic, "btclr"))
    {
      if (view->operand_count != 2)
        return 999;
      if (left_class == K78K0_OPERAND_BIT_A || left_class == K78K0_OPERAND_BIT_HL)
        return 3;
      return k78k0_spanEqual (view->mnemonic, "bt") &&
        (left_class == K78K0_OPERAND_BIT_SADDR || left_class == K78K0_OPERAND_BIT_PSW) ? 3 :
        left_class >= K78K0_OPERAND_BIT_PSW ? 4 : 999;
    }

  if (k78k0_spanEqual (view->mnemonic, "mov"))
    {
      if (k78k0_isAccumulator (left))
        {
          if (right_class == K78K0_OPERAND_IMMEDIATE) return 2;
          if (right_class >= K78K0_OPERAND_DE && right_class <= K78K0_OPERAND_HL_BC)
            return k78k0_memorySize (right_class, false);
          if (right_class == K78K0_OPERAND_BYTE_REGISTER) return 1;
          if (right_class == K78K0_OPERAND_PSW) return 2;
          return k78k0_directSize (right_class);
        }
      if (left_class == K78K0_OPERAND_PSW)
        return right_class == K78K0_OPERAND_IMMEDIATE ? 3 : k78k0_isAccumulator (right) ? 2 : 999;
      if (left_class == K78K0_OPERAND_BYTE_REGISTER)
        return right_class == K78K0_OPERAND_IMMEDIATE ? 2 : k78k0_isAccumulator (right) ? 1 : 999;
      if (left_class >= K78K0_OPERAND_DE && left_class <= K78K0_OPERAND_HL_BC)
        return k78k0_isAccumulator (right) ? k78k0_memorySize (left_class, false) : 999;
      if (left_class == K78K0_OPERAND_SADDR || left_class == K78K0_OPERAND_SFR ||
          left_class == K78K0_OPERAND_ADDR16)
        return right_class == K78K0_OPERAND_IMMEDIATE ? 3 :
          k78k0_isAccumulator (right) ? k78k0_directSize (left_class) : 999;
      return 999;
    }

  if (k78k0_spanEqual (view->mnemonic, "movw"))
    {
      if (left_class == K78K0_OPERAND_REGISTER_PAIR)
        return right_class == K78K0_OPERAND_IMMEDIATE ? 3 :
          right_class == K78K0_OPERAND_SP ? 2 :
          right_class == K78K0_OPERAND_REGISTER_PAIR ? 1 : k78k0_directSize (right_class);
      if (left_class == K78K0_OPERAND_SP)
        return right_class == K78K0_OPERAND_IMMEDIATE ? 4 :
          right_class == K78K0_OPERAND_REGISTER_PAIR ? 2 : 999;
      if (left_class == K78K0_OPERAND_SADDR || left_class == K78K0_OPERAND_SFR ||
          left_class == K78K0_OPERAND_ADDR16)
        return right_class == K78K0_OPERAND_IMMEDIATE ? 4 :
          right_class == K78K0_OPERAND_REGISTER_PAIR ? k78k0_directSize (left_class) : 999;
      return 999;
    }

  if (k78k0_spanEqual (view->mnemonic, "add") || k78k0_spanEqual (view->mnemonic, "addc") ||
      k78k0_spanEqual (view->mnemonic, "sub") || k78k0_spanEqual (view->mnemonic, "subc") ||
      k78k0_spanEqual (view->mnemonic, "and") || k78k0_spanEqual (view->mnemonic, "or") ||
      k78k0_spanEqual (view->mnemonic, "xor") || k78k0_spanEqual (view->mnemonic, "cmp"))
    {
      if (k78k0_isAccumulator (left))
        {
          if (right_class == K78K0_OPERAND_IMMEDIATE || right_class == K78K0_OPERAND_BYTE_REGISTER)
            return 2;
          if (right_class >= K78K0_OPERAND_DE && right_class <= K78K0_OPERAND_HL_BC)
            return k78k0_memorySize (right_class, true);
          return k78k0_directSize (right_class);
        }
      return left_class == K78K0_OPERAND_BYTE_REGISTER && k78k0_isAccumulator (right) ? 2 :
        left_class == K78K0_OPERAND_SADDR && right_class == K78K0_OPERAND_IMMEDIATE ? 3 : 999;
    }

  if (k78k0_spanEqual (view->mnemonic, "xch") && k78k0_isAccumulator (left))
    {
      if (right_class >= K78K0_OPERAND_DE && right_class <= K78K0_OPERAND_HL_BC)
        return k78k0_memorySize (right_class, true);
      return right_class == K78K0_OPERAND_BYTE_REGISTER ? 1 : k78k0_directSize (right_class);
    }
  return 999;
}

int
k78k0_instructionSize (const char *mnemonic, const char *operands)
{
  /* emit2() supplies canonical compiler output. */
  struct k78k0_instruction_view view = {
    {mnemonic, strlen (mnemonic)}, {{NULL, 0}, {NULL, 0}}, 0, true
  };

  return k78k0_splitOperands (operands, &view) ? k78k0_sizeInstruction (&view) : 999;
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
    return 999;
  const char *colon = memchr (view.mnemonic.text, ':', view.mnemonic.length);
  if (colon)
    {
      while (isspace ((unsigned char)*cursor))
        cursor++;
      return colon == view.mnemonic.text + view.mnemonic.length - 1 &&
        (!*cursor || *cursor == ';') ? 0 : 999;
    }
  return k78k0_splitOperands (cursor, &view) ? k78k0_sizeInstruction (&view) : 999;
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

  if (IS_BITINT (OP_SYM_TYPE (IC_RESULT (ic))) && SPEC_BITINTWIDTH (OP_SYM_TYPE (IC_RESULT (ic))) % 8)
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
