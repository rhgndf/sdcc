/*-------------------------------------------------------------------------
  78k0mch.c - 78K0 assembler machine support

  Copyright (C) 2026

  This program is free software; you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by the
  Free Software Foundation; either version 3, or (at your option) any
  later version.
-------------------------------------------------------------------------*/

#include "sdas.h"
#include "asxxxx.h"
#include "78k0.h"

char *cpu = "78k0";
char *dsft = "asm";

static int
get_identifier (char *id)
{
  int c = getnb ();

  if ((ctype[c] & LETTER) == 0)
    return 0;

  getid (id, c);
  return 1;
}

static int
register_id (const char *id)
{
  static const char *const names[] = {
    "x", "a", "c", "b", "e", "d", "l", "h",
    "ax", "bc", "de", "hl", "sp", "psw"
  };

  for (int reg = 0; reg < (int)(sizeof (names) / sizeof (names[0])); reg++)
    if (!strcmp (id, names[reg]))
      return reg;

  if (id[0] == 'r' && id[1] >= '0' && id[1] <= '7' && !id[2])
    return id[1] - '0';
  if (id[0] == 'r' && id[1] == 'p' && id[2] >= '0' && id[2] <= '3' && !id[3])
    return K78K0_AX + (id[2] - '0');

  return -1;
}

static int
getreg (void)
{
  char id[NCPS];
  char *p = ip;
  int reg;

  if (get_identifier (id) && (reg = register_id (id)) >= 0)
    return reg;

  ip = p;
  return -1;
}

static int
is_byte_reg (int reg)
{
  return reg >= K78K0_X && reg <= K78K0_H;
}

static int
is_byte_reg_except_a (int reg)
{
  return is_byte_reg (reg) && reg != K78K0_A;
}

static int
getrb (void)
{
  char id[NCPS];
  char *p = ip;

  if (get_identifier (id) && id[0] == 'r' && id[1] == 'b' &&
      id[2] >= '0' && id[2] <= '3' && !id[3])
    return id[2] - '0';

  ip = p;
  return -1;
}

static int
regpair_code (int reg)
{
  return reg >= K78K0_AX && reg <= K78K0_HL ? reg - K78K0_AX : -1;
}

static void
emit_pair_opcode (int reg, int opcode, int minimum_pair)
{
  const int pair = regpair_code (reg);

  if (pair < minimum_pair)
    qerr ();
  else
    outab (opcode + (pair << 1));
}

static void
emit_stack_register (int reg, int psw_opcode, int pair_opcode)
{
  if (reg == K78K0_PSW)
    outab (psw_opcode);
  else
    emit_pair_opcode (reg, pair_opcode, 0);
}

enum
{
  K78K0_MEM_DE,
  K78K0_MEM_HL,
  K78K0_MEM_HL_INDEX,
  K78K0_MEM_HL_B,
  K78K0_MEM_HL_C
};

enum k78k0_operand_kind
{
  K78K0_OPERAND_INVALID,
  K78K0_OPERAND_REGISTER,
  K78K0_OPERAND_IMMEDIATE,
  K78K0_OPERAND_DIRECT,
  K78K0_OPERAND_MEMORY,
  K78K0_OPERAND_CY
};

struct k78k0_operand
{
  enum k78k0_operand_kind kind;
  int mode;
  int bit;
  struct expr value;
};

static int
operand_register (const struct k78k0_operand *operand)
{
  return operand->kind == K78K0_OPERAND_REGISTER ? operand->mode : -1;
}

static void
expect_reg (int reg)
{
  if (getreg () != reg)
    qerr ();
}

static void
addr16expr (struct expr *e)
{
  if (getnb () != '!')
    qerr ();
  expr (e, 0);
}

enum
{
  K78K0_DIR_SADDR,
  K78K0_DIR_SFR,
  K78K0_DIR_ADDR16
};

static int
canonical_addr16 (a_uint raw, a_uint *addr)
{
  /* ASxxxx sign-extends values whose bit 15 is set.  Accept that canonical
   * representation, but not arbitrary values with nonzero upper bits. */
  if (raw > 0xffff &&
      (raw & (a_uint)0xffff8000) != (a_uint)0xffff8000)
    return 0;

  *addr = raw & 0xffff;
  return 1;
}

static int
is_saddr_value (a_uint addr)
{
  return addr >= 0xfe20 && addr <= 0xff1f;
}

static int
is_sfr_value (a_uint addr)
{
  return (addr >= 0xff00 && addr <= 0xffcf) ||
    (addr >= 0xffe0 && addr <= 0xffff);
}

static void
address_error (const char *message)
{
  xerr ('q', (char *)message);
}

static int
direct_class (struct expr *e, int forced_addr16)
{
  a_uint addr = 0;

  if (forced_addr16)
    {
      if (is_abs (e) && !canonical_addr16 (e->e_addr, &addr))
        address_error ("78K0 addr16 operand is outside 0x0000..0xffff.");
      return K78K0_DIR_ADDR16;
    }
  if (!is_abs (e))
    {
      if (e->e_rlcf)
        address_error ("Byte selection is not valid for a 78K0 direct address.");
      return K78K0_DIR_SADDR;
    }

  if (!canonical_addr16 (e->e_addr, &addr))
    address_error ("78K0 direct address is outside the 16-bit address space.");
  if (is_saddr_value (addr))
    return K78K0_DIR_SADDR;
  if (is_sfr_value (addr))
    return K78K0_DIR_SFR;

  address_error ("78K0 direct address is neither an saddr nor an SFR.");
  return K78K0_DIR_SADDR;
}

static void
emit_opcode (int opcode)
{
  if (opcode > 0xff)
    outab ((opcode >> 8) & 0xff);
  outab (opcode & 0xff);
}

static void
emit_u8 (struct expr *value)
{
  if (is_abs (value) && value->e_addr > 0xff)
    address_error ("78K0 byte operand is outside 0x00..0xff.");

  /* Explicit <symbol and >symbol extraction already constrains the result
   * to one byte; checking the full symbol address as an unsigned byte would
   * reject valid extraction at link time. */
  outrb (value, value->e_rlcf & R_BYTX ? R_NORM : R_USGN);
}

static void
emit_direct_address (struct expr *addr, int kind, int even)
{
  a_uint canonical;

  if (even && is_abs (addr) && (addr->e_addr & 1))
    address_error ("78K0 word address must be even.");

  if (kind == K78K0_DIR_ADDR16)
    {
      if (is_abs (addr) && !canonical_addr16 (addr->e_addr, &canonical))
        address_error ("78K0 addr16 operand is outside 0x0000..0xffff.");
      outrw (addr, R_NORM);
    }
  else
    outrb (addr, R_NORM);
}

static void
emit_direct_opcode (struct expr *addr, int kind, int even,
                    int addr16_opcode, int saddr_opcode, int sfr_opcode)
{
  const int opcode = kind == K78K0_DIR_ADDR16 ? addr16_opcode :
    kind == K78K0_DIR_SADDR ? saddr_opcode : sfr_opcode;

  if (opcode < 0)
    {
      qerr ();
      return;
    }

  outab (opcode);
  emit_direct_address (addr, kind, even);
}

static void
emit_relative_byte (struct expr *target)
{
  if (target->e_base.e_ap == dot.s_area)
    {
      const int displacement = (int)(target->e_addr - dot.s_addr - 1);

      if (pass == 2 && (displacement < -128 || displacement > 127))
        xerr ('a', "Branching range exceeded.");
      outab (displacement);
    }
  else
    {
      if (!target->e_flag && !target->e_base.e_ap)
        {
          target->e_flag = 1;
          target->e_base.e_sp = &sym[1];
        }
      outrb (target, R_PCR);
    }

  if (target->e_mode != S_USER)
    rerr ();
}

static int
bit_number (void)
{
  struct expr e = { 0 };

  if (getnb () != '.')
    qerr ();
  expr (&e, 0);
  if (!is_abs (&e) || e.e_addr > 7)
    qerr ();
  return e.e_addr & 7;
}

static int
bit_number_from_suffix (char *name)
{
  char *suffix = strchr (name, '.');

  if (!suffix)
    return -1;

  *suffix++ = '\0';
  if (suffix[0] < '0' || suffix[0] > '7' || suffix[1])
    {
      qerr ();
      return 0;
    }
  return suffix[0] - '0';
}

static int
parse_memory (struct expr *value)
{
  int reg = getreg ();
  if (reg == K78K0_DE)
    {
      if (getnb () != ']')
        qerr ();
      return K78K0_MEM_DE;
    }
  if (reg != K78K0_HL)
    {
      qerr ();
      return -1;
    }

  const int delimiter = getnb ();
  if (delimiter == '+')
    {
      char *p = ip;
      int index = getreg ();

      if (index == K78K0_B || index == K78K0_C)
        {
          if (getnb () != ']')
            qerr ();
          return index == K78K0_B ? K78K0_MEM_HL_B : K78K0_MEM_HL_C;
        }

      ip = p;
      expr (value, 0);
      if (getnb () != ']')
        qerr ();
      return K78K0_MEM_HL_INDEX;
    }

  if (delimiter != ']')
    qerr ();
  return K78K0_MEM_HL;
}

static struct k78k0_operand
parse_operand (int bit_operand)
{
  struct k78k0_operand operand = { K78K0_OPERAND_INVALID, -1, -1 };
  char id[NCPS];
  char *p = ip;
  int c = getnb ();
  int reg;

  if (c == '[')
    {
      operand.kind = K78K0_OPERAND_MEMORY;
      operand.mode = parse_memory (&operand.value);
      if (operand.mode < 0)
        operand.kind = K78K0_OPERAND_INVALID;
      if (bit_operand)
        {
          if (operand.mode != K78K0_MEM_HL)
            qerr ();
          operand.bit = bit_number ();
        }
      return operand;
    }

  if (c == '#')
    {
      operand.kind = K78K0_OPERAND_IMMEDIATE;
      expr (&operand.value, 0);
      if (bit_operand)
        qerr ();
      return operand;
    }

  if (ctype[c] & LETTER)
    {
      getid (id, c);
      if (bit_operand)
        operand.bit = bit_number_from_suffix (id);

      if (bit_operand && !strcmp (id, "cy"))
        {
          operand.kind = K78K0_OPERAND_CY;
          if (operand.bit >= 0)
            qerr ();
          return operand;
        }

      reg = register_id (id);
      if (reg >= 0)
        {
          operand.kind = K78K0_OPERAND_REGISTER;
          operand.mode = reg;
          if (!bit_operand)
            return operand;

          if (reg == K78K0_PSW)
            {
              operand.kind = K78K0_OPERAND_DIRECT;
              operand.mode = K78K0_DIR_SADDR;
              operand.value.e_addr = 0xff1e;
            }
          else if (reg != K78K0_A)
            qerr ();

          if (operand.bit < 0)
            operand.bit = bit_number ();
          return operand;
        }
    }

  if (bit_operand && c == '!')
    address_error ("Forced addr16 syntax is not valid for a 78K0 bit operand.");

  const int forced_addr16 = c == '!';
  if (!forced_addr16)
    ip = p;
  operand.kind = K78K0_OPERAND_DIRECT;
  expr (&operand.value, 0);
  operand.mode = direct_class (&operand.value, forced_addr16);
  if (bit_operand)
    operand.bit = bit_number ();
  return operand;
}

static void
emit_memory_opcode (struct k78k0_operand *operand, const int opcodes[])
{
  const int kind = operand->mode;

  if (kind < K78K0_MEM_DE || kind > K78K0_MEM_HL_C || opcodes[kind] < 0)
    {
      qerr ();
      return;
    }

  emit_opcode (opcodes[kind]);
  if (kind == K78K0_MEM_HL_INDEX)
    emit_u8 (&operand->value);
}

static const int mov_a_to_memory[] = { 0x95, 0x97, 0xbe, 0xbb, 0xba };

struct accumulator_source_encoding
{
  int immediate, reg, psw;
  int direct[3];
  int memory[5];
};

static const struct accumulator_source_encoding mov_a_source = {
  0xa1, 0x60, 0xf01e, { 0xf0, 0xf4, 0x8e }, { 0x85, 0x87, 0xae, 0xab, 0xaa }
};

static const struct accumulator_source_encoding xch_a_source = {
  -1, 0x30, -1, { 0x83, 0x93, 0xce }, { 0x05, 0x07, 0xde, 0x318b, 0x318a }
};

static void
emit_accumulator_source (struct k78k0_operand *operand,
                         const struct accumulator_source_encoding *encoding)
{
  const int reg = operand_register (operand);
  int opcode = -1;

  if (operand->kind == K78K0_OPERAND_IMMEDIATE)
    opcode = encoding->immediate;
  else if (reg >= 0)
    {
      if (is_byte_reg_except_a (reg))
        opcode = encoding->reg + reg;
      else if (reg == K78K0_PSW)
        opcode = encoding->psw;
    }
  else if (operand->kind == K78K0_OPERAND_DIRECT)
    opcode = encoding->direct[operand->mode];
  else if (operand->kind == K78K0_OPERAND_MEMORY)
    {
      emit_memory_opcode (operand, encoding->memory);
      return;
    }

  if (opcode < 0)
    {
      qerr ();
      return;
    }

  emit_opcode (opcode);
  if (operand->kind == K78K0_OPERAND_IMMEDIATE)
    emit_u8 (&operand->value);
  else if (operand->kind == K78K0_OPERAND_DIRECT)
    emit_direct_address (&operand->value, operand->mode, 0);
}

static void
emit_direct_move (struct k78k0_operand *dst, struct k78k0_operand *src,
                  int word)
{
  if (src->kind == K78K0_OPERAND_IMMEDIATE)
    {
      if (dst->mode == K78K0_DIR_ADDR16)
        qerr ();
      outab (dst->mode == K78K0_DIR_SADDR ? (word ? 0xee : 0x11) :
             (word ? 0xfe : 0x13));
      emit_direct_address (&dst->value, dst->mode, word);
      if (word)
        outrw (&src->value, R_NORM);
      else
        emit_u8 (&src->value);
      return;
    }

  if (operand_register (src) != (word ? K78K0_AX : K78K0_A))
    qerr ();
  emit_direct_opcode (&dst->value, dst->mode, word,
                      word ? 0x03 : 0x9e, word ? 0x99 : 0xf2,
                      word ? 0xb9 : 0xf6);
}

enum k78k0_bit_class
{
  K78K0_BIT_CY,
  K78K0_BIT_SADDR,
  K78K0_BIT_SFR,
  K78K0_BIT_A,
  K78K0_BIT_HL,
  K78K0_BIT_INVALID
};

struct bit_encoding
{
  int operation_prefix[2];
  int operation_base;
  int operation_adjust[2];
  int branch_base;
};

/* Indexed by k78k0_bit_class.  The two-element fields select ordinary bit
 * operations or operations that use CY. */
static const struct bit_encoding bit_encodings[] = {
  [K78K0_BIT_CY]    = { { -1, -1 },    0, { 0, 0 },  -1 },
  [K78K0_BIT_SADDR] = { { -1, 0x71 },  0, { 0, 0 },   0 },
  [K78K0_BIT_SFR]   = { { 0x71, 0x71 }, 0, { 0, 8 },   4 },
  [K78K0_BIT_A]     = { { 0x61, 0x61 }, 0x80, { 0, 8 }, 0x0c },
  [K78K0_BIT_HL]    = { { 0x71, 0x71 }, 0x80, { -8, 0 }, 0x84 }
};

static enum k78k0_bit_class
bit_class (const struct k78k0_operand *operand)
{
  if (operand->kind == K78K0_OPERAND_CY)
    return K78K0_BIT_CY;
  if (operand->kind == K78K0_OPERAND_DIRECT)
    return operand->mode == K78K0_DIR_SADDR ? K78K0_BIT_SADDR :
      operand->mode == K78K0_DIR_SFR ? K78K0_BIT_SFR : K78K0_BIT_INVALID;
  if (operand_register (operand) == K78K0_A)
    return K78K0_BIT_A;
  if (operand->kind == K78K0_OPERAND_MEMORY && operand->mode == K78K0_MEM_HL)
    return K78K0_BIT_HL;
  return K78K0_BIT_INVALID;
}

static void
emit_bit_address (struct k78k0_operand *operand)
{
  if (operand->kind == K78K0_OPERAND_DIRECT)
    emit_direct_address (&operand->value, operand->mode, 0);
}

static void
emit_bit_operation (struct k78k0_operand *operand, int low, int uses_cy)
{
  const enum k78k0_bit_class class = bit_class (operand);
  const struct bit_encoding *encoding;

  if (class == K78K0_BIT_CY)
    {
      if (uses_cy)
        qerr ();
      else
        outab (low == 0x0a ? 0x20 : 0x21);
      return;
    }
  if (class == K78K0_BIT_INVALID)
    {
      qerr ();
      return;
    }

  encoding = &bit_encodings[class];
  if (encoding->operation_prefix[uses_cy] >= 0)
    outab (encoding->operation_prefix[uses_cy]);
  outab ((operand->bit << 4) | encoding->operation_base |
         (low + encoding->operation_adjust[uses_cy]));
  emit_bit_address (operand);
}

static void
emit_bit_branch (struct k78k0_operand *operand, struct expr *target,
                 int low)
{
  const enum k78k0_bit_class class = bit_class (operand);
  const struct bit_encoding *encoding;

  if (class == K78K0_BIT_CY || class == K78K0_BIT_INVALID)
    {
      qerr ();
      return;
    }

  encoding = &bit_encodings[class];
  if (low == 0x06 && class == K78K0_BIT_SADDR)
    {
      outab (0x8c | (operand->bit << 4));
      emit_bit_address (operand);
      emit_relative_byte (target);
      return;
    }

  outab (0x31);
  outab ((operand->bit << 4) | low | encoding->branch_base);
  emit_bit_address (operand);
  emit_relative_byte (target);
}

static void
emit_byte_alu (int addr16_opcode)
{
  struct k78k0_operand dst = parse_operand (0);
  struct k78k0_operand src;
  const int dst_reg = operand_register (&dst);

  comma (1);
  src = parse_operand (0);

  if (dst_reg == K78K0_A)
    {
      const struct accumulator_source_encoding encoding = {
        addr16_opcode + 0x05,
        0x6100 + addr16_opcode,
        -1,
        { addr16_opcode + 0x06, -1, addr16_opcode },
        { -1, addr16_opcode + 0x07, addr16_opcode + 0x01,
          0x3100 | (addr16_opcode + 0x03),
          0x3100 | (addr16_opcode + 0x02) }
      };
      emit_accumulator_source (&src, &encoding);
    }
  else if (is_byte_reg_except_a (dst_reg))
    {
      if (operand_register (&src) != K78K0_A)
        qerr ();
      outab (0x61);
      outab (addr16_opcode - 0x08 + dst_reg);
    }
  else if (dst.kind == K78K0_OPERAND_DIRECT)
    {
      if (dst.mode != K78K0_DIR_SADDR)
        qerr ();
      if (src.kind != K78K0_OPERAND_IMMEDIATE)
        qerr ();
      outab (addr16_opcode + 0x80);
      emit_direct_address (&dst.value, K78K0_DIR_SADDR, 0);
      emit_u8 (&src.value);
    }
  else
    qerr ();
}

static void
emit_ax_imm16 (int opcode)
{
  struct k78k0_operand operand;

  expect_reg (K78K0_AX);
  comma (1);
  operand = parse_operand (0);
  if (operand.kind != K78K0_OPERAND_IMMEDIATE)
    qerr ();
  outab (opcode);
  outrw (&operand.value, R_NORM);
}

VOID
machine (struct mne *mp)
{
  struct expr e = { 0 };
  char *p;
  int c;
  int src;

  switch (mp->m_type)
    {
    case S_78K0_0OP:
      emit_opcode (mp->m_valu);
      break;

    case S_78K0_BYTE_ALU:
      emit_byte_alu (mp->m_valu);
      break;

    case S_78K0_AX_IMM16:
      emit_ax_imm16 (mp->m_valu);
      break;

    case S_78K0_CONDBR:
      expr (&e, 0);
      outab (mp->m_valu);
      emit_relative_byte (&e);
      break;

    case S_78K0_BITCY:
      {
        const struct k78k0_operand operand = parse_operand (1);

        if (operand.kind != K78K0_OPERAND_CY)
          qerr ();
      }
      outab (mp->m_valu);
      break;

    case S_78K0_BIT1:
      {
        struct k78k0_operand bit = parse_operand (1);
        emit_bit_operation (&bit, mp->m_valu, 0);
      }
      break;

    case S_78K0_BITMOV1:
      {
        struct k78k0_operand dst_bit = parse_operand (1);
        struct k78k0_operand src_bit;

        comma (1);
        src_bit = parse_operand (1);
        if (dst_bit.kind == K78K0_OPERAND_CY)
          emit_bit_operation (&src_bit, mp->m_valu, 1);
        else if (mp->m_valu == 0x04)
          {
            if (src_bit.kind != K78K0_OPERAND_CY)
              qerr ();
            emit_bit_operation (&dst_bit, 0x01, 1);
          }
        else
          qerr ();
      }
      break;

    case S_78K0_BITBR:
      {
        struct k78k0_operand bit = parse_operand (1);

        comma (1);
        expr (&e, 0);
        emit_bit_branch (&bit, &e, mp->m_valu);
      }
      break;

    case S_78K0_DIVUW:
      expect_reg (K78K0_C);
      emit_opcode (0x3182);
      break;

    case S_78K0_DBNZ:
      {
        struct k78k0_operand operand = parse_operand (0);
        const int reg = operand_register (&operand);

        if (reg == K78K0_C || reg == K78K0_B)
          {
            comma (1);
            expr (&e, 0);
            outab (reg == K78K0_C ? 0x8a : 0x8b);
          }
        else
          {
            if (operand.kind != K78K0_OPERAND_DIRECT ||
                operand.mode != K78K0_DIR_SADDR)
              qerr ();
            comma (1);
            outab (0x04);
            emit_direct_address (&operand.value, K78K0_DIR_SADDR, 0);
            expr (&e, 0);
          }
        emit_relative_byte (&e);
      }
      break;

    case S_78K0_INCDEC:
      {
        struct k78k0_operand operand = parse_operand (0);
        const int reg = operand_register (&operand);

        if (is_byte_reg (reg))
          outab (((mp->m_valu >> 8) & 0xff) + reg);
        else
          {
            if (operand.kind != K78K0_OPERAND_DIRECT ||
                operand.mode != K78K0_DIR_SADDR)
              qerr ();
            outab (mp->m_valu & 0xff);
            emit_direct_address (&operand.value, K78K0_DIR_SADDR, 0);
          }
      }
      break;

    case S_78K0_INCWDECW:
      emit_pair_opcode (getreg (), mp->m_valu, 0);
      break;

    case S_78K0_MOV:
      {
        struct k78k0_operand dst_operand = parse_operand (0);
        struct k78k0_operand src_operand;
        int dst_reg, src_reg;

        comma (1);
        src_operand = parse_operand (0);
        dst_reg = operand_register (&dst_operand);
        src_reg = operand_register (&src_operand);
        if (dst_reg >= 0)
          {
            if (src_operand.kind == K78K0_OPERAND_IMMEDIATE &&
                (is_byte_reg (dst_reg) || dst_reg == K78K0_PSW))
              {
                emit_opcode (dst_reg == K78K0_PSW ?
                             0x111e : 0xa0 + dst_reg);
                emit_u8 (&src_operand.value);
              }
            else if (dst_reg == K78K0_A)
              emit_accumulator_source (&src_operand, &mov_a_source);
            else if (dst_reg == K78K0_PSW || is_byte_reg_except_a (dst_reg))
              {
                if (src_reg != K78K0_A)
                  qerr ();
                emit_opcode (dst_reg == K78K0_PSW ?
                             0xf21e : 0x70 + dst_reg);
              }
            else
              qerr ();
          }
        else if (dst_operand.kind == K78K0_OPERAND_MEMORY)
          {
            if (src_reg != K78K0_A)
              qerr ();
            emit_memory_opcode (&dst_operand, mov_a_to_memory);
          }
        else if (dst_operand.kind == K78K0_OPERAND_DIRECT)
          emit_direct_move (&dst_operand, &src_operand, 0);
        else
          qerr ();
      }
      break;

    case S_78K0_MOVW:
      {
        struct k78k0_operand dst_operand = parse_operand (0);
        struct k78k0_operand src_operand;
        int dst_reg, src_reg;

        comma (1);
        src_operand = parse_operand (0);
        dst_reg = operand_register (&dst_operand);
        src_reg = operand_register (&src_operand);
        if (dst_reg >= 0)
          {
            if (src_operand.kind == K78K0_OPERAND_IMMEDIATE)
              {
                if (dst_reg == K78K0_SP)
                  emit_opcode (0xee1c);
                else
                  emit_pair_opcode (dst_reg, 0x10, 0);
                outrw (&src_operand.value, R_NORM);
              }
            else if (dst_reg == K78K0_AX &&
                     src_operand.kind == K78K0_OPERAND_DIRECT)
              emit_direct_opcode (&src_operand.value, src_operand.mode, 1,
                                  0x02, 0x89, 0xa9);
            else if (dst_reg == K78K0_AX && src_reg == K78K0_SP)
              emit_opcode (0xa91c);
            else if (dst_reg == K78K0_AX)
              emit_pair_opcode (src_reg, 0xc0, 1);
            else if (dst_reg == K78K0_SP && src_reg == K78K0_AX)
              emit_opcode (0xb91c);
            else if (src_reg == K78K0_AX)
              emit_pair_opcode (dst_reg, 0xd0, 1);
            else
              qerr ();
          }
        else if (dst_operand.kind == K78K0_OPERAND_DIRECT)
          emit_direct_move (&dst_operand, &src_operand, 1);
        else
          qerr ();
      }
      break;

    case S_78K0_STACK:
      emit_stack_register (getreg (), (mp->m_valu >> 8) & 0xff, mp->m_valu & 0xff);
      break;

    case S_78K0_MULU:
      expect_reg (K78K0_X);
      emit_opcode (0x3188);
      break;

    case S_78K0_ROT:
      expect_reg (K78K0_A);
      comma (1);
      if (getnb () != '1')
        qerr ();
      outab (mp->m_valu);
      break;

    case S_78K0_ROT4:
      {
        struct k78k0_operand operand = parse_operand (0);

        if (operand.kind != K78K0_OPERAND_MEMORY ||
            operand.mode != K78K0_MEM_HL)
          qerr ();
      }
      emit_opcode (mp->m_valu);
      break;

    case S_78K0_BR:
      p = ip;
      if (getreg () == K78K0_AX)
        emit_opcode (0x3198);
      else
        {
          c = getnb ();
          ip = p;
          if (c == '!')
            {
              addr16expr (&e);
              outab (0x9b);
              emit_direct_address (&e, K78K0_DIR_ADDR16, 0);
            }
          else
            {
              expr (&e, 0);
              outab (0xfa);
              emit_relative_byte (&e);
            }
        }
      break;

    case S_78K0_CALL:
      addr16expr (&e);
      outab (0x9a);
      emit_direct_address (&e, K78K0_DIR_ADDR16, 0);
      break;

    case S_78K0_CALLF:
      addr16expr (&e);
      if (!is_abs (&e) || e.e_addr < 0x0800 || e.e_addr > 0x0fff)
        qerr ();
      outab (0x0c | ((e.e_addr >> 4) & 0x70));
      outab (e.e_addr & 0xff);
      break;

    case S_78K0_CALLT:
      if (getnb () != '[')
        qerr ();
      expr (&e, 0);
      if (getnb () != ']')
        qerr ();
      if (!is_abs (&e) || e.e_addr < 0x40 || e.e_addr > 0x7e || (e.e_addr & 1))
        qerr ();
      outab (0xc1 | (e.e_addr & 0x3e));
      break;

    case S_78K0_SEL:
      src = getrb ();
      if (src < 0)
        qerr ();
      outab (0x61);
      outab (0xd0 | ((src & 0x02) << 4) | ((src & 0x01) << 3));
      break;

    case S_78K0_XCH:
      expect_reg (K78K0_A);
      comma (1);
      {
        struct k78k0_operand operand = parse_operand (0);
        emit_accumulator_source (&operand, &xch_a_source);
      }
      break;

    case S_78K0_XCHW:
      expect_reg (K78K0_AX);
      comma (1);
      emit_pair_opcode (getreg (), 0xe0, 1);
      break;

    default:
      xerr ('o', "Internal 78K0 opcode error.");
      break;
    }
}

VOID
minit (void)
{
  set_sdas_target (TARGET_ID_78K0);
  vflag = 1;
}
