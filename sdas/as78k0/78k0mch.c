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
getreg (void)
{
  static const char *const names[] = {
    "x", "a", "c", "b", "e", "d", "l", "h",
    "ax", "bc", "de", "hl", "sp", "psw"
  };
  char id[NCPS];
  char *p = ip;
  int c = getnb ();

  if ((ctype[c] & LETTER) == 0)
    {
      ip = p;
      return -1;
    }

  getid (id, c);

  for (int reg = 0; reg < (int)(sizeof (names) / sizeof (names[0])); reg++)
    if (!strcmp (id, names[reg]))
      return reg;

  if (id[0] == 'r' && id[1] >= '0' && id[1] <= '7' && !id[2])
    return id[1] - '0';
  if (id[0] == 'r' && id[1] == 'p' && id[2] >= '0' && id[2] <= '3' && !id[3])
    return K78K0_AX + (id[2] - '0');

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
  int c = getnb ();

  if ((ctype[c] & LETTER) == 0)
    {
      ip = p;
      return -1;
    }

  getid (id, c);

  if (id[0] == 'r' && id[1] == 'b' && id[2] >= '0' && id[2] <= '3' && !id[3])
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
  K78K0_BIT_PSW,
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

static void address_error (const char *message);

static void
immexpr (struct expr *e)
{
  if (getnb () != '#')
    qerr ();
  expr (e, 0);
}

static void
reject_unary_negative (void)
{
  char *p = ip;
  int c;

  /* ASxxxx sign-extends 16-bit constants, so -1 and 0xffff are identical
   * after expr().  Preserve the target syntax distinction before parsing. */
  do
    c = getnb ();
  while (c == '+' || c == '(');
  ip = p;

  if (c == '-')
    address_error ("Negative values are not valid 78K0 addresses.");
}

static void
addr16expr (struct expr *e)
{
  if (getnb () != '!')
    qerr ();
  reject_unary_negative ();
  expr (e, 0);
}

static int
direct_expr (struct expr *e)
{
  char *p = ip;
  int c = getnb ();

  if (c == '!')
    {
      reject_unary_negative ();
      expr (e, 0);
      return 1;
    }

  ip = p;
  reject_unary_negative ();
  expr (e, 0);
  return 0;
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
  if (raw <= 0xffff)
    *addr = raw;
  else if ((raw & (a_uint)0xffff8000) == (a_uint)0xffff8000)
    *addr = raw & 0xffff;
  else
    return 0;
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
direct_class (const struct expr *e, int forced_addr16)
{
  a_uint addr = 0;

  if (forced_addr16)
    {
      if (is_abs ((struct expr *)e) && !canonical_addr16 (e->e_addr, &addr))
        address_error ("78K0 addr16 operand is outside 0x0000..0xffff.");
      return K78K0_DIR_ADDR16;
    }
  if (!is_abs ((struct expr *)e))
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
  return direct_class (e, direct_expr (e));
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
  if (is_abs (addr))
    {
      a_uint canonical;

      if (!canonical_addr16 (addr->e_addr, &canonical))
        address_error ("78K0 addr16 operand is outside 0x0000..0xffff.");
    }
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

static const int mov_a_from_memory[] = { 0x85, 0x87, 0xae, 0xab, 0xaa };
static const int mov_a_to_memory[] = { 0x95, 0x97, 0xbe, 0xbb, 0xba };
static const int xch_a_memory[] = { 0x05, 0x07, 0xde, 0x318b, 0x318a };

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

  b.type = K78K0_BIT_SADDR;
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
          b.type = K78K0_BIT_PSW;
          if (!has_bit)
            b.bit = bit_number ();
          return b;
        }

      ip = p;
    }
  else
    ip = p;

  reject_unary_negative ();
  expr (&b.addr, 0);
  switch (direct_class (&b.addr, 0))
    {
    case K78K0_DIR_SADDR:
      b.type = K78K0_BIT_SADDR;
      break;
    case K78K0_DIR_SFR:
      b.type = K78K0_BIT_SFR;
      break;
    default:
      qerr ();
      break;
    }
  b.bit = bit_number ();
  return b;
}

static void
emit_bit_addr (const struct bit_operand *b)
{
  if (b->type == K78K0_BIT_PSW)
    outab (0x1e);
  else if (b->type == K78K0_BIT_SADDR)
    emit_direct_address ((struct expr *)&b->addr, K78K0_DIR_SADDR, 0);
  else if (b->type == K78K0_BIT_SFR)
    emit_direct_address ((struct expr *)&b->addr, K78K0_DIR_SFR, 0);
  else
    qerr ();
}

static void
emit_bit_to_cy (const struct bit_operand *b, int low)
{
  switch (b->type)
    {
    case K78K0_BIT_SADDR:
    case K78K0_BIT_PSW:
      outab (0x71);
      outab ((b->bit << 4) | low);
      emit_bit_addr (b);
      break;
    case K78K0_BIT_SFR:
      outab (0x71);
      outab ((b->bit << 4) | 0x08 | low);
      emit_bit_addr (b);
      break;
    case K78K0_BIT_A:
      outab (0x61);
      outab (0x80 | (b->bit << 4) | 0x08 | low);
      break;
    case K78K0_BIT_HL:
      outab (0x71);
      outab (0x80 | (b->bit << 4) | low);
      break;
    default:
      qerr ();
      break;
    }
}

static void
emit_cy_to_bit (const struct bit_operand *b)
{
  emit_bit_to_cy (b, 0x01);
}

static void
emit_setclr_bit (const struct bit_operand *b, int low)
{
  switch (b->type)
    {
    case K78K0_BIT_CY:
      outab (low == 0x0a ? 0x20 : 0x21);
      break;
    case K78K0_BIT_SADDR:
    case K78K0_BIT_PSW:
      outab ((b->bit << 4) | low);
      emit_bit_addr (b);
      break;
    case K78K0_BIT_SFR:
      outab (0x71);
      outab ((b->bit << 4) | low);
      emit_bit_addr (b);
      break;
    case K78K0_BIT_A:
      outab (0x61);
      outab (0x80 | (b->bit << 4) | low);
      break;
    case K78K0_BIT_HL:
      outab (0x71);
      outab (0x80 | (b->bit << 4) | (low - 0x08));
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

  if (low == 0x06 && (b->type == K78K0_BIT_SADDR || b->type == K78K0_BIT_PSW))
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
    case K78K0_BIT_PSW:
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
  struct expr e;
  int c;
  int kind;
  int dst;

  clrexpr (&e);

  dst = getreg ();

  if (dst == K78K0_A)
    {
      comma (1);
      c = getnb ();
      if (c == '#')
        {
          expr (&e, 0);
          outab (addr16_opcode + 0x05);
          emit_u8 (&e);
        }
      else if (c == '[')
        {
          const int opcodes[] = {
            -1,
            addr16_opcode + 0x07,
            addr16_opcode + 0x01,
            0x3100 | (addr16_opcode + 0x03),
            0x3100 | (addr16_opcode + 0x02)
          };

          unget ('[');
          emit_memory_opcode (bracket_operand (&e), &e, opcodes);
        }
      else
        {
          char *p;
          int src;

          unget (c);
          p = ip;
          src = getreg ();
          if (is_byte_reg_except_a (src))
            {
              outab (0x61);
              outab (addr16_opcode + src);
            }
          else
            {
              ip = p;
              kind = parse_direct (&e);
              emit_direct_opcode (&e, kind, 0, addr16_opcode, addr16_opcode + 0x06, -1);
            }
        }
    }
  else if (is_byte_reg_except_a (dst))
    {
      comma (1);
      c = getnb ();
      if (c != 'a')
        qerr ();
      outab (0x61);
      outab (addr16_opcode - 0x08 + dst);
    }
  else if (dst < 0)
    {
      struct expr addr;
      struct expr imm;

      clrexpr (&addr);
      clrexpr (&imm);
      if (parse_direct (&e) != K78K0_DIR_SADDR)
        qerr ();
      comma (1);
      addr = e;
      immexpr (&imm);
      outab (addr16_opcode + 0x80);
      emit_direct_address (&addr, K78K0_DIR_SADDR, 0);
      emit_u8 (&imm);
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
  immexpr (&e);
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
        emit_setclr_bit (&bit, mp->m_valu);
      }
      break;

    case S_78K0_BITMOV1:
      {
        struct bit_operand dst_bit = bit_operand ();

        comma (1);
        if (dst_bit.type == K78K0_BIT_CY)
          {
            struct bit_operand src_bit = bit_operand ();
            emit_bit_to_cy (&src_bit, mp->m_valu);
          }
        else if (mp->m_valu == 0x04)
          {
            struct bit_operand src_bit = bit_operand ();
            if (src_bit.type != K78K0_BIT_CY)
              qerr ();
            emit_cy_to_bit (&dst_bit);
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
      p = ip;
      dst = getreg ();

      if (dst == K78K0_C || dst == K78K0_B)
        {
          comma (1);
          expr (&e, 0);
          outab (dst == K78K0_C ? 0x8a : 0x8b);
          emit_relative_byte (&e);
        }
      else
        {
          ip = p;
          if (parse_direct (&e) != K78K0_DIR_SADDR)
            qerr ();
          comma (1);
          outab (0x04);
          emit_direct_address (&e, K78K0_DIR_SADDR, 0);
          expr (&e, 0);
          emit_relative_byte (&e);
        }
      break;

    case S_78K0_INCDEC:
      p = ip;
      dst = getreg ();
      if (is_byte_reg (dst))
        outab (((mp->m_valu >> 8) & 0xff) + dst);
      else
        {
          ip = p;
          if (parse_direct (&e) != K78K0_DIR_SADDR)
            qerr ();
          outab (mp->m_valu & 0xff);
          emit_direct_address (&e, K78K0_DIR_SADDR, 0);
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
      dst = getreg ();

      if (dst == K78K0_A)
        {
          comma (1);
          c = getnb ();

          if (c == '#')
            {
              expr (&e, 0);
              outab (0xa1);
              emit_u8 (&e);
            }
          else if (c == '[')
            {
              unget ('[');
              emit_memory_opcode (bracket_operand (&e), &e, mov_a_from_memory);
            }
          else
            {
              char *p;

              unget (c);
              p = ip;
              src = getreg ();
              if (is_byte_reg_except_a (src))
                outab (0x60 + src);
              else if (src == K78K0_PSW)
                emit_opcode (0xf01e);
              else if (src < 0)
                {
                  int kind;

                  ip = p;
                  kind = parse_direct (&e);
                  emit_direct_opcode (&e, kind, 0, 0x8e, 0xf0, 0xf4);
                }
              else
                qerr ();
            }
        }
      else if (dst == K78K0_PSW)
        {
          comma (1);
          c = getnb ();
          if (c == '#')
            {
              expr (&e, 0);
              emit_opcode (0x111e);
              emit_u8 (&e);
            }
          else
            {
              unget (c);
              expect_reg (K78K0_A);
              emit_opcode (0xf21e);
            }
        }
      else if (is_byte_reg_except_a (dst))
        {
          comma (1);
          c = getnb ();
          if (c == '#')
            {
              expr (&e, 0);
              outab (0xa0 + dst);
              emit_u8 (&e);
            }
          else
            {
              unget (c);
              expect_reg (K78K0_A);
              outab (0x70 + dst);
            }
        }
      else if (dst < 0)
        {
          int c = getnb ();

          if (c == '[')
            {
              unget ('[');
              emit_memory_opcode (bracket_operand (&e), &e, mov_a_to_memory);
              comma (1);
              expect_reg (K78K0_A);
              break;
            }

          unget (c);
          {
            struct expr addr;
            struct expr imm;
            int kind;

            clrexpr (&addr);
            clrexpr (&imm);
            kind = parse_direct (&addr);
            comma (1);
            c = getnb ();
            if (c == '#')
              {
                expr (&imm, 0);
                if (kind == K78K0_DIR_ADDR16)
                  qerr ();
                outab (kind == K78K0_DIR_SADDR ? 0x11 : 0x13);
                emit_direct_address (&addr, kind, 0);
                emit_u8 (&imm);
              }
            else
              {
                unget (c);
                expect_reg (K78K0_A);
                emit_direct_opcode (&addr, kind, 0, 0x9e, 0xf2, 0xf6);
              }
          }
        }
      else
        qerr ();
      break;

    case S_78K0_MOVW:
      dst = getreg ();

      if (dst >= 0)
        {
          char *sp = ip;

          comma (1);
          src = getreg ();

          if (src < 0)
            {
              c = getnb ();
              if (c == '#')
                {
                  expr (&e, 0);

                  if ((c = regpair_code (dst)) >= 0)
                    outab (0x10 + (c << 1));
                  else if (dst == K78K0_SP)
                    emit_opcode (0xee1c);
                  else
                    {
                      qerr ();
                      break;
                    }
                  outrw (&e, R_NORM);
                }
              else if (dst == K78K0_AX)
                {
                  int kind;

                  ip = sp;
                  comma (1);
                  kind = parse_direct (&e);
                  emit_direct_opcode (&e, kind, 1, 0x02, 0x89, 0xa9);
                }
              else
                qerr ();
            }
          else if (dst == K78K0_AX && src == K78K0_SP)
            emit_opcode (0xa91c);
          else if (dst == K78K0_AX && (c = regpair_code (src)) >= 1)
            outab (0xc0 + (c << 1));
          else if (src == K78K0_AX && (c = regpair_code (dst)) >= 1)
            outab (0xd0 + (c << 1));
          else if (dst == K78K0_SP && src == K78K0_AX)
            emit_opcode (0xb91c);
          else
            qerr ();
        }
      else
        {
          struct expr addr;
          struct expr imm;
          int kind;

          clrexpr (&addr);
          clrexpr (&imm);
          kind = parse_direct (&addr);
          comma (1);
          c = getnb ();
          if (c == '#')
            {
              expr (&imm, 0);
              if (kind == K78K0_DIR_ADDR16)
                qerr ();
              outab (kind == K78K0_DIR_SADDR ? 0xee : 0xfe);
              emit_direct_address (&addr, kind, 1);
              outrw (&imm, R_NORM);
            }
          else
            {
              unget (c);
              expect_reg (K78K0_AX);
              emit_direct_opcode (&addr, kind, 1, 0x03, 0x99, 0xb9);
            }
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
      c = getnb ();
      if (c == '[')
        {
          unget ('[');
          emit_memory_opcode (bracket_operand (&e), &e, xch_a_memory);
        }
      else
        {
          unget (c);
          p = ip;
          src = getreg ();
          if (is_byte_reg_except_a (src))
            outab (0x30 + src);
          else
            {
              int kind;

              ip = p;
              kind = parse_direct (&e);
              emit_direct_opcode (&e, kind, 0, 0xce, 0x83, 0x93);
            }
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
