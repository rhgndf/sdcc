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
getreg (void)
{
  static const char *const names[] = {
    "x", "a", "c", "b", "e", "d", "l", "h",
    "ax", "bc", "de", "hl", "sp", "psw"
  };
  char id[NCPS];
  char *p = ip;

  if (get_identifier (id))
    {
      for (int reg = 0; reg < (int)(sizeof (names) / sizeof (names[0])); reg++)
        if (!strcmp (id, names[reg]))
          return reg;

      if (id[0] == 'r' && id[1] >= '0' && id[1] <= '7' && !id[2])
        return id[1] - '0';
      if (id[0] == 'r' && id[1] == 'p' && id[2] >= '0' && id[2] <= '3' && !id[3])
        return K78K0_AX + (id[2] - '0');
    }

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
emit_stack_register (int reg, int psw_opcode, int pair_opcode)
{
  const int pair = regpair_code (reg);

  if (reg == K78K0_PSW)
    outab (psw_opcode);
  else if (pair >= 0)
    outab (pair_opcode + (pair << 1));
  else
    qerr ();
}

enum
{
  K78K0_MEM_DE,
  K78K0_MEM_HL,
  K78K0_MEM_HL_INDEX,
  K78K0_MEM_HL_B,
  K78K0_MEM_HL_C
};

enum
{
  K78K0_BIT_CY,
  K78K0_BIT_SADDR,
  K78K0_BIT_SFR,
  K78K0_BIT_A,
  K78K0_BIT_HL
};

struct bit_operand
{
  int type;
  int bit;
  struct expr addr;
};

static void
expect_reg (int reg)
{
  if (getreg () != reg)
    qerr ();
}

static int
parse_immediate (struct expr *e)
{
  int c = getnb ();

  if (c != '#')
    {
      unget (c);
      return 0;
    }

  expr (e, 0);
  return 1;
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

static int
parse_direct (struct expr *e)
{
  char *p = ip;
  int forced_addr16 = getnb () == '!';

  if (!forced_addr16)
    ip = p;
  expr (e, 0);
  return direct_class (e, forced_addr16);
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
  if (even && is_abs (addr) && (addr->e_addr & 1))
    address_error ("78K0 word address must be even.");

  if (kind == K78K0_DIR_ADDR16)
    outrw (addr, R_NORM);
  else
    outrb (addr, R_NORM);
}

static void
emit_addr16 (struct expr *addr)
{
  a_uint canonical;

  if (is_abs (addr) && !canonical_addr16 (addr->e_addr, &canonical))
    address_error ("78K0 addr16 operand is outside 0x0000..0xffff.");
  outrw (addr, R_NORM);
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
bracket_operand (struct expr *e)
{
  int c;

  if (getnb () != '[')
    {
      qerr ();
      return 0;
    }

  c = getreg ();
  if (c == K78K0_DE)
    {
      if (getnb () != ']')
        qerr ();
      return K78K0_MEM_DE;
    }
  if (c != K78K0_HL)
    {
      qerr ();
      return -1;
    }

  c = getnb ();
  if (c == '+')
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
      expr (e, 0);
      if (getnb () != ']')
        qerr ();
      return K78K0_MEM_HL_INDEX;
    }

  if (c != ']')
    qerr ();

  return K78K0_MEM_HL;
}

static void
emit_memory_opcode (int kind, struct expr *index, const int opcodes[])
{
  if (kind < K78K0_MEM_DE || kind > K78K0_MEM_HL_C || opcodes[kind] < 0)
    {
      qerr ();
      return;
    }

  emit_opcode (opcodes[kind]);
  if (kind == K78K0_MEM_HL_INDEX)
    emit_u8 (index);
}

static const int mov_a_to_memory[] = { 0x95, 0x97, 0xbe, 0xbb, 0xba };

enum
{
  K78K0_OPERAND_DIRECT = K78K0_PSW + 1,
  K78K0_OPERAND_MEMORY = K78K0_OPERAND_DIRECT + 3,
  K78K0_OPERAND_IMMEDIATE = K78K0_OPERAND_MEMORY + 5,
  K78K0_OPERAND_INVALID
};

struct data_operand
{
  int kind;
  struct expr value;
};

static struct data_operand
data_operand (void)
{
  struct data_operand operand;
  int c;

  clrexpr (&operand.value);
  operand.kind = getreg ();
  if (operand.kind >= 0)
    return operand;

  if (parse_immediate (&operand.value))
    {
      operand.kind = K78K0_OPERAND_IMMEDIATE;
      return operand;
    }

  c = getnb ();
  unget (c);
  if (c == '[')
    {
      operand.kind = bracket_operand (&operand.value);
      operand.kind = operand.kind < 0 ? K78K0_OPERAND_INVALID :
        K78K0_OPERAND_MEMORY + operand.kind;
    }
  else
    operand.kind = K78K0_OPERAND_DIRECT + parse_direct (&operand.value);

  return operand;
}

static int
direct_operand_kind (const struct data_operand *operand)
{
  return operand->kind >= K78K0_OPERAND_DIRECT &&
    operand->kind < K78K0_OPERAND_MEMORY ?
    operand->kind - K78K0_OPERAND_DIRECT : -1;
}

static int
memory_operand_kind (const struct data_operand *operand)
{
  return operand->kind >= K78K0_OPERAND_MEMORY &&
    operand->kind < K78K0_OPERAND_IMMEDIATE ?
    operand->kind - K78K0_OPERAND_MEMORY : -1;
}

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
emit_accumulator_source (struct data_operand *operand,
                         const struct accumulator_source_encoding *encoding)
{
  int opcode = -1;
  const int direct = direct_operand_kind (operand);
  const int memory = memory_operand_kind (operand);

  if (operand->kind == K78K0_OPERAND_IMMEDIATE)
    opcode = encoding->immediate;
  else if (is_byte_reg_except_a (operand->kind))
    opcode = encoding->reg + operand->kind;
  else if (operand->kind == K78K0_PSW)
    opcode = encoding->psw;
  else if (direct >= 0)
    opcode = encoding->direct[direct];
  else if (memory >= 0)
    {
      emit_memory_opcode (memory, &operand->value, encoding->memory);
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
  else if (direct >= 0)
    emit_direct_address (&operand->value, direct, 0);
}

static void
emit_direct_move (struct data_operand *dst, struct data_operand *src, int word)
{
  const int direct = direct_operand_kind (dst);

  if (src->kind == K78K0_OPERAND_IMMEDIATE)
    {
      if (direct == K78K0_DIR_ADDR16)
        qerr ();
      outab (direct == K78K0_DIR_SADDR ? (word ? 0xee : 0x11) :
             (word ? 0xfe : 0x13));
      emit_direct_address (&dst->value, direct, word);
      if (word)
        outrw (&src->value, R_NORM);
      else
        emit_u8 (&src->value);
      return;
    }

  if (src->kind != (word ? K78K0_AX : K78K0_A))
    qerr ();
  emit_direct_opcode (&dst->value, direct, word,
                      word ? 0x03 : 0x9e, word ? 0x99 : 0xf2,
                      word ? 0xb9 : 0xf6);
}

static int
bit_number (void)
{
  struct expr e;

  clrexpr (&e);
  if (getnb () != '.')
    qerr ();
  expr (&e, 0);
  if (!is_abs (&e) || e.e_addr > 7)
    qerr ();
  return e.e_addr & 7;
}

static int
bit_number_from_suffix (char *name, int *bit)
{
  char *suffix = strchr (name, '.');

  if (!suffix)
    return 0;

  *suffix++ = '\0';
  if (suffix[0] < '0' || suffix[0] > '7' || suffix[1])
    qerr ();
  *bit = suffix[0] - '0';
  return 1;
}

static struct bit_operand
bit_operand (void)
{
  struct bit_operand b;
  char id[NCPS];
  char *p = ip;
  int c = getnb ();

  b.bit = 0;
  clrexpr (&b.addr);

  if (c == '[')
    {
      unget ('[');
      if (bracket_operand (&b.addr) != K78K0_MEM_HL)
        qerr ();
      b.type = K78K0_BIT_HL;
      b.bit = bit_number ();
      return b;
    }

  if (c == '!')
    address_error ("Forced addr16 syntax is not valid for a 78K0 bit operand.");

  if (ctype[c] & LETTER)
    {
      int has_bit;

      getid (id, c);
      has_bit = bit_number_from_suffix (id, &b.bit);
      if (strcmp (id, "cy") == 0)
        {
          b.type = K78K0_BIT_CY;
          if (has_bit)
            qerr ();
          return b;
        }
      if (strcmp (id, "a") == 0)
        {
          b.type = K78K0_BIT_A;
          if (!has_bit)
            b.bit = bit_number ();
          return b;
        }
      if (strcmp (id, "psw") == 0)
        {
          b.type = K78K0_BIT_SADDR;
          b.addr.e_addr = 0xff1e;
          if (!has_bit)
            b.bit = bit_number ();
          return b;
        }

      ip = p;
    }
  else
    ip = p;

  expr (&b.addr, 0);
  b.type = direct_class (&b.addr, 0) == K78K0_DIR_SFR ?
    K78K0_BIT_SFR : K78K0_BIT_SADDR;
  b.bit = bit_number ();
  return b;
}

static void
emit_bit_addr (const struct bit_operand *b)
{
  if (b->type != K78K0_BIT_SADDR && b->type != K78K0_BIT_SFR)
    qerr ();
  else
    emit_direct_address ((struct expr *)&b->addr,
                         b->type - K78K0_BIT_SADDR, 0);
}

static void
emit_bit_operation (const struct bit_operand *b, int low, int uses_cy)
{
  switch (b->type)
    {
    case K78K0_BIT_CY:
      if (uses_cy)
        qerr ();
      else
        outab (low == 0x0a ? 0x20 : 0x21);
      break;
    case K78K0_BIT_SADDR:
    case K78K0_BIT_SFR:
      if (uses_cy || b->type == K78K0_BIT_SFR)
        outab (0x71);
      outab ((b->bit << 4) | low |
             (uses_cy && b->type == K78K0_BIT_SFR ? 0x08 : 0));
      emit_bit_addr (b);
      break;
    case K78K0_BIT_A:
      outab (0x61);
      outab (0x80 | (b->bit << 4) | low | (uses_cy ? 0x08 : 0));
      break;
    case K78K0_BIT_HL:
      outab (0x71);
      outab (0x80 | (b->bit << 4) | (low - (uses_cy ? 0 : 0x08)));
      break;
    default:
      qerr ();
      break;
    }
}

static void
emit_bit_branch (const struct bit_operand *b, struct expr *target, int low)
{
  if (b->type == K78K0_BIT_CY)
    {
      qerr ();
      return;
    }

  if (low == 0x06 && b->type == K78K0_BIT_SADDR)
    {
      outab (0x8c | (b->bit << 4));
      emit_bit_addr (b);
      emit_relative_byte (target);
      return;
    }

  outab (0x31);
  switch (b->type)
    {
    case K78K0_BIT_SADDR:
      outab ((b->bit << 4) | low);
      emit_bit_addr (b);
      break;
    case K78K0_BIT_SFR:
      outab ((b->bit << 4) | (low == 0x06 ? low : low + 4));
      emit_bit_addr (b);
      break;
    case K78K0_BIT_A:
      outab ((b->bit << 4) | (low | 0x0c));
      break;
    case K78K0_BIT_HL:
      outab (0x80 | (b->bit << 4) | (low == 0x06 ? low : low + 4));
      break;
    default:
      qerr ();
      break;
    }
  emit_relative_byte (target);
}

static void
emit_byte_alu (int addr16_opcode)
{
  struct data_operand dst = data_operand ();
  const int direct = direct_operand_kind (&dst);

  if (dst.kind == K78K0_A)
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
      struct data_operand src;

      comma (1);
      src = data_operand ();
      emit_accumulator_source (&src, &encoding);
    }
  else if (is_byte_reg_except_a (dst.kind))
    {
      comma (1);
      if (getnb () != 'a')
        qerr ();
      outab (0x61);
      outab (addr16_opcode - 0x08 + dst.kind);
    }
  else if (direct >= 0)
    {
      struct data_operand src;

      if (direct != K78K0_DIR_SADDR)
        qerr ();
      comma (1);
      src = data_operand ();
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
  struct expr e;

  clrexpr (&e);
  expect_reg (K78K0_AX);
  comma (1);
  if (!parse_immediate (&e))
    qerr ();
  outab (opcode);
  outrw (&e, R_NORM);
}

VOID
machine (struct mne *mp)
{
  struct expr e;
  char *p;
  int c;
  int dst;
  int src;

  set_sdas_target (TARGET_ID_78K0);
  clrexpr (&e);

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
      if (getnb () != 'c' || getnb () != 'y')
        qerr ();
      outab (mp->m_valu);
      break;

    case S_78K0_BIT1:
      {
        struct bit_operand bit = bit_operand ();
        emit_bit_operation (&bit, mp->m_valu, 0);
      }
      break;

    case S_78K0_BITMOV1:
      {
        struct bit_operand dst_bit = bit_operand ();

        comma (1);
        if (dst_bit.type == K78K0_BIT_CY)
          {
            struct bit_operand src_bit = bit_operand ();
            emit_bit_operation (&src_bit, mp->m_valu, 1);
          }
        else if (mp->m_valu == 0x04)
          {
            struct bit_operand src_bit = bit_operand ();
            if (src_bit.type != K78K0_BIT_CY)
              qerr ();
            emit_bit_operation (&dst_bit, 0x01, 1);
          }
        else
          qerr ();
      }
      break;

    case S_78K0_BITBR:
      {
        struct bit_operand bit = bit_operand ();

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
        struct data_operand operand = data_operand ();

        if (operand.kind == K78K0_C || operand.kind == K78K0_B)
          {
            comma (1);
            expr (&e, 0);
            outab (operand.kind == K78K0_C ? 0x8a : 0x8b);
          }
        else
          {
            if (direct_operand_kind (&operand) != K78K0_DIR_SADDR)
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
        struct data_operand operand = data_operand ();

        if (is_byte_reg (operand.kind))
          outab (((mp->m_valu >> 8) & 0xff) + operand.kind);
        else
          {
            if (direct_operand_kind (&operand) != K78K0_DIR_SADDR)
              qerr ();
            outab (mp->m_valu & 0xff);
            emit_direct_address (&operand.value, K78K0_DIR_SADDR, 0);
          }
      }
      break;

    case S_78K0_INCWDECW:
      dst = regpair_code (getreg ());
      if (dst < 0)
        {
          qerr ();
          break;
        }
      outab (mp->m_valu + (dst << 1));
      break;

    case S_78K0_MOV:
      {
        struct data_operand dst_operand = data_operand ();
        struct data_operand src_operand;
        const int dst_direct = direct_operand_kind (&dst_operand);
        const int dst_memory = memory_operand_kind (&dst_operand);

        comma (1);
        src_operand = data_operand ();
        if (dst_operand.kind <= K78K0_PSW)
          {
            if (dst_operand.kind == K78K0_A)
              emit_accumulator_source (&src_operand, &mov_a_source);
            else if (dst_operand.kind == K78K0_PSW ||
                     is_byte_reg_except_a (dst_operand.kind))
              {
                if (src_operand.kind == K78K0_OPERAND_IMMEDIATE)
                  {
                    emit_opcode (dst_operand.kind == K78K0_PSW ?
                                 0x111e : 0xa0 + dst_operand.kind);
                    emit_u8 (&src_operand.value);
                  }
                else
                  {
                    if (src_operand.kind != K78K0_A)
                      qerr ();
                    emit_opcode (dst_operand.kind == K78K0_PSW ?
                                 0xf21e : 0x70 + dst_operand.kind);
                  }
              }
            else
              qerr ();
          }
        else if (dst_memory >= 0)
          {
            if (src_operand.kind != K78K0_A)
              qerr ();
            emit_memory_opcode (dst_memory, &dst_operand.value,
                                mov_a_to_memory);
          }
        else if (dst_direct >= 0)
          emit_direct_move (&dst_operand, &src_operand, 0);
        else
          qerr ();
      }
      break;

    case S_78K0_MOVW:
      {
        struct data_operand dst_operand = data_operand ();
        struct data_operand src_operand;
        const int dst_direct = direct_operand_kind (&dst_operand);
        int pair;

        comma (1);
        src_operand = data_operand ();
        if (dst_operand.kind <= K78K0_PSW)
          {
            const int src_direct = direct_operand_kind (&src_operand);

            if (src_operand.kind == K78K0_OPERAND_IMMEDIATE)
              {
                pair = regpair_code (dst_operand.kind);
                if (pair >= 0)
                  outab (0x10 + (pair << 1));
                else if (dst_operand.kind == K78K0_SP)
                  emit_opcode (0xee1c);
                else
                  {
                    qerr ();
                    break;
                  }
                outrw (&src_operand.value, R_NORM);
              }
            else if (dst_operand.kind == K78K0_AX &&
                     src_direct >= 0)
              emit_direct_opcode (&src_operand.value, src_direct, 1,
                                  0x02, 0x89, 0xa9);
            else if (dst_operand.kind == K78K0_AX &&
                     src_operand.kind == K78K0_SP)
              emit_opcode (0xa91c);
            else if (dst_operand.kind == K78K0_AX &&
                     (pair = regpair_code (src_operand.kind)) >= 1)
              outab (0xc0 + (pair << 1));
            else if (src_operand.kind == K78K0_AX &&
                     (pair = regpair_code (dst_operand.kind)) >= 1)
              outab (0xd0 + (pair << 1));
            else if (dst_operand.kind == K78K0_SP &&
                     src_operand.kind == K78K0_AX)
              emit_opcode (0xb91c);
            else
              qerr ();
          }
        else if (dst_direct >= 0)
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
      if (bracket_operand (&e) != K78K0_MEM_HL)
        qerr ();
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
              emit_addr16 (&e);
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
      emit_addr16 (&e);
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
        struct data_operand operand = data_operand ();
        emit_accumulator_source (&operand, &xch_a_source);
      }
      break;

    case S_78K0_XCHW:
      expect_reg (K78K0_AX);
      comma (1);
      src = regpair_code (getreg ());
      if (src < 1)
        {
          qerr ();
          break;
        }
      outab (0xe0 + (src << 1));
      break;

    default:
      xerr ('o', "Internal 78K0 opcode error.");
      break;
    }
}

VOID
minit (void)
{
  vflag = 1;
}
