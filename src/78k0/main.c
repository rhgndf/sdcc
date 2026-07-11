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
         "\tbr\t!00003$\n"
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
         "\tbr\t!00001$\n"
         "00002$:\n",
         of);
}

static int
k78k0_dwarfRegNum (const struct reg_info *reg)
{
  return reg->rIdx;
}

static bool
k78k0_hasNativeMulFor (iCode *ic, sym_link *left, sym_link *right)
{
  const int result_size = IS_SYMOP (IC_RESULT (ic)) ? getSize (OP_SYM_TYPE (IC_RESULT (ic))) : 4;

  if (ic->op != '*')
    {
      if (ic->op != '/' && ic->op != '%')
        return false;

      return getSize (left) == 1 && getSize (right) == 1 &&
        SPEC_USIGN (getSpec (left)) && SPEC_USIGN (getSpec (right));
    }

  if (IS_BITINT (OP_SYM_TYPE (IC_RESULT (ic))) && SPEC_BITINTWIDTH (OP_SYM_TYPE (IC_RESULT (ic))) % 8)
    return false;

  if (IS_ITEMP (IC_RESULT (ic)) && result_size == 2 &&
      ((IS_LITERAL (left) && ulFromVal (valFromType (left)) <= 255 && getSize (right) == 2) ||
       (IS_LITERAL (right) && ulFromVal (valFromType (right)) <= 255 && getSize (left) == 2)))
    return true;

  return getSize (left) == 1 && getSize (right) == 1 &&
    SPEC_USIGN (getSpec (left)) && SPEC_USIGN (getSpec (right));
}

static bool
k78k0_hasExtBitOp (int op, sym_link *left, int right)
{
  (void)op;
  (void)left;
  (void)right;
  return false;
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
    NULL,
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
  32,
  PORT_MAGIC
};
