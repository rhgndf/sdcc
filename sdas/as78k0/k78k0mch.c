/*-------------------------------------------------------------------------
  k78k0mch.c - 78K0 assembler machine support

  Copyright (C) 2026

  This program is free software; you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by the
  Free Software Foundation; either version 3, or (at your option) any
  later version.
-------------------------------------------------------------------------*/

#include "sdas.h"
#include "asxxxx.h"
#include "k78k0.h"

char *cpu = "78k0";
char *dsft = "asm";

static int
getreg (void)
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

  if (strcmp (id, "x") == 0)
    return K78K0_X;
  if (strcmp (id, "a") == 0)
    return K78K0_A;
  if (strcmp (id, "c") == 0)
    return K78K0_C;
  if (strcmp (id, "b") == 0)
    return K78K0_B;
  if (strcmp (id, "e") == 0)
    return K78K0_E;
  if (strcmp (id, "d") == 0)
    return K78K0_D;
  if (strcmp (id, "l") == 0)
    return K78K0_L;
  if (strcmp (id, "h") == 0)
    return K78K0_H;
  if (strcmp (id, "ax") == 0)
    return K78K0_AX;
  if (strcmp (id, "bc") == 0)
    return K78K0_BC;
  if (strcmp (id, "de") == 0)
    return K78K0_DE;
  if (strcmp (id, "hl") == 0)
    return K78K0_HL;
  if (strcmp (id, "sp") == 0)
    return K78K0_SP;
  if (strcmp (id, "psw") == 0)
    return K78K0_PSW;
  if (strlen (id) == 2 && id[0] == 'r' && id[1] >= '0' && id[1] <= '7')
    return id[1] - '0';
  if (strlen (id) == 3 && id[0] == 'r' && id[1] == 'p' && id[2] >= '0' && id[2] <= '3')
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

  if (strlen (id) == 3 && id[0] == 'r' && id[1] == 'b' && id[2] >= '0' && id[2] <= '3')
    return id[2] - '0';

  ip = p;
  return -1;
}

static int
regpair_code (int reg)
{
  switch (reg)
    {
    case K78K0_AX:
      return 0;
    case K78K0_BC:
      return 1;
    case K78K0_DE:
      return 2;
    case K78K0_HL:
      return 3;
    default:
      return -1;
    }
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

static void
immexpr (struct expr *e)
{
  if (getnb () != '#')
    qerr ();
  expr (e, 0);
}

static void
addr16expr (struct expr *e)
{
  if (getnb () != '!')
    qerr ();
  expr (e, 0);
}

static int
direct_expr (struct expr *e)
{
  char *p = ip;
  int c = getnb ();

  if (c == '!')
    {
      expr (e, 0);
      return 1;
    }

  ip = p;
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
direct_class (const struct expr *e, int forced_addr16)
{
  a_uint addr;

  if (forced_addr16)
    return K78K0_DIR_ADDR16;
  if (!is_abs ((struct expr *)e))
    return K78K0_DIR_SADDR;

  addr = e->e_addr & 0xffff;
  if (addr <= 0xff || (addr >= 0xfe20 && addr <= 0xff1f))
    return K78K0_DIR_SADDR;
  if (addr >= 0xff00)
    return K78K0_DIR_SFR;

  qerr ();
  return K78K0_DIR_SADDR;
}

static void
check_even_direct (const struct expr *e)
{
  if (is_abs ((struct expr *)e) && (e->e_addr & 1))
    qerr ();
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
  int pos = 0;

  while (name[pos] && name[pos] != '.')
    pos++;
  if (!name[pos])
    return 0;

  name[pos++] = '\0';
  if (name[pos] < '0' || name[pos] > '7' || name[pos + 1] != '\0')
    qerr ();
  *bit = name[pos] - '0';
  return 1;
}

static struct bit_operand
bit_operand (void)
{
  struct bit_operand b;
  char id[NCPS];
  char *p = ip;
  int c = getnb ();
  int forced;

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
    {
      expr (&b.addr, 0);
      b.type = K78K0_BIT_SFR;
      b.bit = bit_number ();
      return b;
    }

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

  forced = direct_expr (&b.addr);
  switch (direct_class (&b.addr, 0))
    {
    case K78K0_DIR_SADDR:
      b.type = forced ? K78K0_BIT_SFR : K78K0_BIT_SADDR;
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
  else
    outrb ((struct expr *)&b->addr, R_USGN);
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
      outrb (target, R_PCR);
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
      outab (0x80 | (b->bit << 4) | (low | 0x0c));
      break;
    case K78K0_BIT_HL:
      outab (0x80 | (b->bit << 4) | (low == 0x06 ? low : low + 4));
      break;
    default:
      qerr ();
      break;
    }
  outrb (target, R_PCR);
}

static void
emit_byte_alu (int addr16_opcode)
{
  struct expr e;
  int c;
  int forced_addr16;
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
          outrb (&e, R_USGN);
        }
      else if (c == '[')
        {
          unget ('[');
          switch (bracket_operand (&e))
            {
            case K78K0_MEM_HL:
              outab (addr16_opcode + 0x07);
              break;
            case K78K0_MEM_HL_INDEX:
              outab (addr16_opcode + 0x01);
              outrb (&e, R_USGN);
              break;
            case K78K0_MEM_HL_B:
              outab (0x31);
              outab (addr16_opcode + 0x03);
              break;
            case K78K0_MEM_HL_C:
              outab (0x31);
              outab (addr16_opcode + 0x02);
              break;
            default:
              qerr ();
              break;
            }
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
              forced_addr16 = direct_expr (&e);
              kind = direct_class (&e, forced_addr16);
              if (kind == K78K0_DIR_ADDR16)
                {
                  outab (addr16_opcode);
                  outrw (&e, R_NORM);
                }
              else if (kind == K78K0_DIR_SADDR)
                {
                  outab (addr16_opcode + 0x06);
                  outrb (&e, R_USGN);
                }
              else
                qerr ();
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
      forced_addr16 = direct_expr (&e);
      if (forced_addr16 || direct_class (&e, 0) != K78K0_DIR_SADDR)
        qerr ();
      comma (1);
      addr = e;
      immexpr (&imm);
      outab (addr16_opcode + 0x80);
      outrb (&addr, R_USGN);
      outrb (&imm, R_USGN);
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
    case S_K78K0_0OP:
      outab (mp->m_valu);
      break;

    case S_K78K0_0OP2:
      outab ((mp->m_valu >> 8) & 0xff);
      outab (mp->m_valu & 0xff);
      break;

    case S_K78K0_2BYTE:
      outab ((mp->m_valu >> 8) & 0xff);
      outab (mp->m_valu & 0xff);
      break;

    case S_K78K0_ADD:
      emit_byte_alu (0x08);
      break;

    case S_K78K0_ADDC:
      emit_byte_alu (0x28);
      break;

    case S_K78K0_ADDW:
      emit_ax_imm16 (0xca);
      break;

    case S_K78K0_CMPW:
      emit_ax_imm16 (0xea);
      break;

    case S_K78K0_AND:
      emit_byte_alu (0x58);
      break;

    case S_K78K0_CMP:
      emit_byte_alu (0x48);
      break;

    case S_K78K0_CONDBR:
      expr (&e, 0);
      outab (mp->m_valu);
      outrb (&e, R_PCR);
      break;

    case S_K78K0_BITCY:
      if (getnb () != 'c' || getnb () != 'y')
        qerr ();
      outab (mp->m_valu);
      break;

    case S_K78K0_BIT1:
      {
        struct bit_operand bit = bit_operand ();
        emit_setclr_bit (&bit, mp->m_valu);
      }
      break;

    case S_K78K0_BITMOV1:
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

    case S_K78K0_BITBR:
      {
        struct bit_operand bit = bit_operand ();

        comma (1);
        expr (&e, 0);
        emit_bit_branch (&bit, &e, mp->m_valu);
      }
      break;

    case S_K78K0_DIVUW:
      expect_reg (K78K0_C);
      outab (0x31);
      outab (0x82);
      break;

    case S_K78K0_DBNZ:
      p = ip;
      dst = getreg ();

      if (dst == K78K0_C || dst == K78K0_B)
        {
          comma (1);
          expr (&e, 0);
          outab (dst == K78K0_C ? 0x8a : 0x8b);
          outrb (&e, R_PCR);
        }
      else
        {
          int forced_addr16;

          ip = p;
          forced_addr16 = direct_expr (&e);
          if (forced_addr16 || direct_class (&e, 0) != K78K0_DIR_SADDR)
            qerr ();
          comma (1);
          outab (0x04);
          outrb (&e, R_USGN);
          expr (&e, 0);
          outrb (&e, R_PCR);
        }
      break;

    case S_K78K0_DEC:
    case S_K78K0_INC:
      p = ip;
      dst = getreg ();
      if (is_byte_reg (dst))
        outab ((mp->m_type == S_K78K0_INC ? 0x40 : 0x50) + dst);
      else
        {
          int forced_addr16;

          ip = p;
          forced_addr16 = direct_expr (&e);
          if (forced_addr16 || direct_class (&e, 0) != K78K0_DIR_SADDR)
            qerr ();
          outab (mp->m_type == S_K78K0_INC ? 0x81 : 0x91);
          outrb (&e, R_USGN);
        }
      break;

    case S_K78K0_DECW:
    case S_K78K0_INCW:
      dst = regpair_code (getreg ());
      if (dst < 0)
        {
          qerr ();
          break;
        }
      outab ((mp->m_type == S_K78K0_INCW ? 0x80 : 0x90) + (dst << 1));
      break;

    case S_K78K0_OR:
      emit_byte_alu (0x68);
      break;

    case S_K78K0_SUB:
      emit_byte_alu (0x18);
      break;

    case S_K78K0_SUBC:
      emit_byte_alu (0x38);
      break;

    case S_K78K0_SUBW:
      emit_ax_imm16 (0xda);
      break;

    case S_K78K0_MOV:
      dst = getreg ();

      if (dst == K78K0_A)
        {
          comma (1);
          c = getnb ();

          if (c == '#')
            {
              expr (&e, 0);
              outab (0xa1);
              outrb (&e, R_USGN);
            }
          else if (c == '[')
            {
              unget ('[');
              switch (bracket_operand (&e))
                {
                case K78K0_MEM_DE:
                  outab (0x85);
                  break;
                case K78K0_MEM_HL:
                  outab (0x87);
                  break;
                case K78K0_MEM_HL_INDEX:
                  outab (0xae);
                  outrb (&e, R_USGN);
                  break;
                case K78K0_MEM_HL_B:
                  outab (0xab);
                  break;
                case K78K0_MEM_HL_C:
                  outab (0xaa);
                  break;
                default:
                  qerr ();
                  break;
                }
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
                {
                  outab (0xf0);
                  outab (0x1e);
                }
              else if (src < 0)
                {
                  int forced_addr16;
                  int kind;

                  ip = p;
                  forced_addr16 = direct_expr (&e);
                  kind = direct_class (&e, forced_addr16);
                  if (kind == K78K0_DIR_ADDR16)
                    {
                      outab (0x8e);
                      outrw (&e, R_NORM);
                    }
                  else if (kind == K78K0_DIR_SADDR)
                    {
                      outab (0xf0);
                      outrb (&e, R_USGN);
                    }
                  else
                    {
                      outab (0xf4);
                      outrb (&e, R_USGN);
                    }
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
              outab (0x01);
              outab (0x1e);
              outrb (&e, R_USGN);
            }
          else
            {
              unget (c);
              expect_reg (K78K0_A);
              outab (0xf2);
              outab (0x1e);
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
              outrb (&e, R_USGN);
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
              switch (bracket_operand (&e))
                {
                case K78K0_MEM_DE:
                  outab (0x95);
                  break;
                case K78K0_MEM_HL:
                  outab (0x97);
                  break;
                case K78K0_MEM_HL_INDEX:
                  outab (0xbe);
                  outrb (&e, R_USGN);
                  break;
                case K78K0_MEM_HL_B:
                  outab (0xbb);
                  break;
                case K78K0_MEM_HL_C:
                  outab (0xba);
                  break;
                default:
                  qerr ();
                  break;
                }
              comma (1);
              expect_reg (K78K0_A);
              break;
            }

          unget (c);
          {
            struct expr addr;
            struct expr imm;
            int forced_addr16;
            int kind;

            clrexpr (&addr);
            clrexpr (&imm);
            forced_addr16 = direct_expr (&addr);
            kind = direct_class (&addr, forced_addr16);
            comma (1);
            c = getnb ();
            if (c == '#')
              {
                expr (&imm, 0);
                if (kind == K78K0_DIR_ADDR16)
                  qerr ();
                outab (kind == K78K0_DIR_SADDR ? 0x11 : 0x13);
                outrb (&addr, R_USGN);
                outrb (&imm, R_USGN);
              }
            else
              {
                unget (c);
                expect_reg (K78K0_A);
                if (kind == K78K0_DIR_ADDR16)
                  {
                    outab (0x9e);
                    outrw (&addr, R_NORM);
                  }
                else
                  {
                    outab (kind == K78K0_DIR_SADDR ? 0xf2 : 0xf6);
                    outrb (&addr, R_USGN);
                  }
              }
          }
        }
      else
        qerr ();
      break;

    case S_K78K0_MOVW:
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

                  if (regpair_code (dst) >= 0)
                    outab (0x10 + (regpair_code (dst) << 1));
                  else if (dst == K78K0_SP)
                    {
                      outab (0xee);
                      outab (0x1c);
                    }
                  else
                    {
                      qerr ();
                      break;
                    }
                  outrw (&e, R_NORM);
                }
              else if (dst == K78K0_AX)
                {
                  int forced_addr16;
                  int kind;

                  ip = sp;
                  comma (1);
                  forced_addr16 = direct_expr (&e);
                  kind = direct_class (&e, forced_addr16);
                  check_even_direct (&e);
                  if (kind == K78K0_DIR_ADDR16)
                    {
                      outab (0x02);
                      outrw (&e, R_NORM);
                    }
                  else
                    {
                      outab (kind == K78K0_DIR_SADDR ? 0x89 : 0xa9);
                      outrb (&e, R_USGN);
                    }
                }
              else
                qerr ();
            }
          else if (dst == K78K0_AX && src == K78K0_SP)
            {
              outab (0xa9);
              outab (0x1c);
            }
          else if (dst == K78K0_AX && src == K78K0_BC)
            outab (0xc2);
          else if (dst == K78K0_AX && src == K78K0_HL)
            outab (0xc6);
          else if (dst == K78K0_AX && src == K78K0_DE)
            outab (0xc4);
          else if (dst == K78K0_BC && src == K78K0_AX)
            outab (0xd2);
          else if (dst == K78K0_DE && src == K78K0_AX)
            outab (0xd4);
          else if (dst == K78K0_HL && src == K78K0_AX)
            outab (0xd6);
          else if (dst == K78K0_SP && src == K78K0_AX)
            {
              outab (0xb9);
              outab (0x1c);
            }
          else
            qerr ();
        }
      else
        {
          struct expr addr;
          struct expr imm;
          int forced_addr16;
          int kind;

          clrexpr (&addr);
          clrexpr (&imm);
          forced_addr16 = direct_expr (&addr);
          kind = direct_class (&addr, forced_addr16);
          check_even_direct (&addr);
          comma (1);
          c = getnb ();
          if (c == '#')
            {
              expr (&imm, 0);
              if (kind == K78K0_DIR_ADDR16)
                qerr ();
              outab (kind == K78K0_DIR_SADDR ? 0xee : 0xfe);
              outrb (&addr, R_USGN);
              outrw (&imm, R_NORM);
            }
          else
            {
              unget (c);
              expect_reg (K78K0_AX);
              if (kind == K78K0_DIR_ADDR16)
                {
                  outab (0x03);
                  outrw (&addr, R_NORM);
                }
              else
                {
                  outab (kind == K78K0_DIR_SADDR ? 0x99 : 0xb9);
                  outrb (&addr, R_USGN);
                }
            }
        }
      break;

    case S_K78K0_POP:
      dst = getreg ();
      if (dst == K78K0_PSW)
        outab (0x23);
      else if (regpair_code (dst) >= 0)
        outab (0xb0 + (regpair_code (dst) << 1));
      else
        qerr ();
      break;

    case S_K78K0_PUSH:
      src = getreg ();
      if (src == K78K0_PSW)
        outab (0x22);
      else if (regpair_code (src) >= 0)
        outab (0xb1 + (regpair_code (src) << 1));
      else
        qerr ();
      break;

    case S_K78K0_MULU:
      expect_reg (K78K0_X);
      outab (0x31);
      outab (0x88);
      break;

    case S_K78K0_ROT:
      expect_reg (K78K0_A);
      comma (1);
      if (getnb () != '1')
        qerr ();
      outab (mp->m_valu);
      break;

    case S_K78K0_ROT4:
      if (bracket_operand (&e) != K78K0_MEM_HL)
        qerr ();
      outab ((mp->m_valu >> 8) & 0xff);
      outab (mp->m_valu & 0xff);
      break;

    case S_K78K0_BR:
      p = ip;
      if (getreg () == K78K0_AX)
        {
          outab (0x31);
          outab (0x98);
        }
      else
        {
          c = getnb ();
          ip = p;
          if (c == '!')
            {
              addr16expr (&e);
              outab (0x9b);
              outrw (&e, R_NORM);
            }
          else
            {
              expr (&e, 0);
              outab (0xfa);
              outrb (&e, R_PCR);
            }
        }
      break;

    case S_K78K0_CALL:
      addr16expr (&e);
      outab (0x9a);
      outrw (&e, R_NORM);
      break;

    case S_K78K0_CALLF:
      addr16expr (&e);
      if (!is_abs (&e) || e.e_addr < 0x0800 || e.e_addr > 0x0fff)
        qerr ();
      outab (0x0c | ((e.e_addr >> 4) & 0x70));
      outab (e.e_addr & 0xff);
      break;

    case S_K78K0_CALLT:
      if (getnb () != '[')
        qerr ();
      expr (&e, 0);
      if (getnb () != ']')
        qerr ();
      if (!is_abs (&e) || e.e_addr < 0x40 || e.e_addr > 0x7e || (e.e_addr & 1))
        qerr ();
      outab (0xc1 | (e.e_addr & 0x3e));
      break;

    case S_K78K0_SEL:
      src = getrb ();
      if (src < 0)
        qerr ();
      outab (0x61);
      outab (0xd0 | ((src & 0x02) << 4) | ((src & 0x01) << 3));
      break;

    case S_K78K0_XCH:
      expect_reg (K78K0_A);
      comma (1);
      c = getnb ();
      if (c == '[')
        {
          unget ('[');
          switch (bracket_operand (&e))
            {
            case K78K0_MEM_DE:
              outab (0x05);
              break;
            case K78K0_MEM_HL:
              outab (0x07);
              break;
            case K78K0_MEM_HL_INDEX:
              outab (0xde);
              outrb (&e, R_USGN);
              break;
            case K78K0_MEM_HL_B:
              outab (0x31);
              outab (0x8b);
              break;
            case K78K0_MEM_HL_C:
              outab (0x31);
              outab (0x8a);
              break;
            default:
              qerr ();
              break;
            }
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
              int forced_addr16;
              int kind;

              ip = p;
              forced_addr16 = direct_expr (&e);
              kind = direct_class (&e, forced_addr16);
              if (kind == K78K0_DIR_ADDR16)
                {
                  outab (0xce);
                  outrw (&e, R_NORM);
                }
              else
                {
                  outab (kind == K78K0_DIR_SADDR ? 0x83 : 0x93);
                  outrb (&e, R_USGN);
                }
            }
        }
      break;

    case S_K78K0_XCHW:
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

    case S_K78K0_XOR:
      emit_byte_alu (0x78);
      break;

    default:
      xerr ('o', "Internal 78K0 opcode error.");
      break;
    }
}

VOID
minit (void)
{
}
