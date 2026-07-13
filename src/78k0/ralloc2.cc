//-------------------------------------------------------------------------
// ralloc2.cc - 78K0 tree-decomposition register allocator
//
// Copyright (C) 2026
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2, or (at your option) any
// later version.
//-------------------------------------------------------------------------

#include "SDCCralloc.hpp"

extern "C"
{
#include "ralloc.h"
}

enum
{
  MASK_AX = (1 << K78K0_RB0_X_IDX) | (1 << K78K0_RB0_A_IDX),
  MASK_C = 1 << K78K0_RB0_C_IDX,
  MASK_B = 1 << K78K0_RB0_B_IDX,
  MASK_BC = (1 << K78K0_RB0_C_IDX) | (1 << K78K0_RB0_B_IDX),
  MASK_DE = (1 << K78K0_RB0_E_IDX) | (1 << K78K0_RB0_D_IDX),
  MASK_ALL = (1 << 6) - 1,
};

template <class I_t>
static void add_operand_conflicts_in_node (const cfg_node &, I_t &) {}

static bool
legal_layout (const std::vector<reg_t> &layout)
{
  const unsigned size = layout.size ();
  const bool has_register = std::any_of (layout.begin (), layout.end (), [](reg_t reg) { return reg >= 0; });
  if (!has_register)
    return true;

  const bool has_spill = std::any_of (layout.begin (), layout.end (), [](reg_t reg) { return reg < 0; });
  if (size <= 2)
    return !has_spill && (size == 1 ? layout[0] >= K78K0_RB0_X_IDX && layout[0] <= K78K0_RB0_D_IDX :
                          size == 2 && layout[0] % 2 == 0 && layout[1] == layout[0] + 1);
  if (size > 4)
    return false;
  for (unsigned byte = 0; byte < size; byte++)
    if (layout[byte] >= 0 && layout[byte] != (reg_t)byte)
      return false;
  return !has_spill || (size == 4 && (layout[0] < 0) == (layout[1] < 0) &&
                         (layout[2] < 0) == (layout[3] < 0));
}

static int
instruction_clobbers (const iCode *ic)
{
  const operand *right = IC_RIGHT (ic);
  const int result_size = IC_RESULT (ic) ? getSize (operandType (IC_RESULT (ic))) : 0;
  const int left_size = IC_LEFT (ic) ? getSize (operandType (IC_LEFT (ic))) : 0;

  switch (ic->op)
    {
    case FUNCTION:
    case ENDFUNCTION:
    case GOTO:
    case RETURN:
      return 0;
    case LABEL:
      return 0;
    case ADDRESS_OF:
      return MASK_AX;
    case '=':
      return !POINTER_SET (ic) && result_size == 1 ? MASK_AX | MASK_C : MASK_ALL;
    case GET_VALUE_AT_ADDRESS:
      return MASK_AX | MASK_C | MASK_DE;
    case '+':
    case '-':
      if (result_size == 2 && right && IS_OP_LITERAL (right) && left_size == 2)
        return MASK_AX;
      if (result_size == 1)
        return MASK_AX | MASK_C;
      return MASK_ALL;
    case '*':
      return result_size == 1 ? MASK_AX | MASK_C : MASK_ALL;
    case LEFT_OP:
    case RIGHT_OP:
    case ROT:
      return result_size == 1 ? MASK_AX | MASK_BC : MASK_ALL;
    case '/':
    case '%':
    case BITWISEAND:
    case '|':
    case '^':
    case UNARYMINUS:
    case '!':
    case CAST:
    case GETBYTE:
    case GETWORD:
    case GETABIT:
      return result_size == 1 ? MASK_AX | MASK_C : MASK_ALL;
    case EQ_OP:
    case NE_OP:
    case '<':
    case '>':
      if (left_size == 2 && right && IS_OP_LITERAL (right))
        return MASK_AX;
      return left_size == 1 ? MASK_AX | MASK_C : MASK_ALL;
    case IFX:
      {
        const int size = IC_COND (ic) ? getSize (operandType (IC_COND (ic))) : 0;
        return !size || size > 2 ? MASK_ALL : size == 1 ? MASK_AX : MASK_AX | MASK_B;
      }
    default:
      return MASK_ALL;
    }
}

static bool
operand_is_symbol (const operand *op, const int key)
{
  return op && IS_SYMOP (op) && OP_SYMBOL_CONST (op)->key == key;
}

struct value_layout
{
  std::vector<reg_t> registers;
  bool survives = false;
};

template <class G_t>
static bool
operand_is_spilled (const operand *op, const assignment &a, unsigned short i, const G_t &G)
{
  if (!op || !IS_SYMOP (op))
    return false;

  const symbol *sym = OP_SYMBOL_CONST (op);
  if (IS_TRUE_SYMOP (op))
    return sym->onStack;
  if (!IS_ITEMP (op))
    return false;

  const auto range = G[i].operands.equal_range (sym->key);
  for (auto operand = range.first; operand != range.second; ++operand)
    if (a.global[operand->second] < 0)
      return true;
  return false;
}

template <class G_t, class I_t>
static bool
inst_sane (const assignment &a, unsigned short i, const G_t &G, const I_t &I)
{
  const iCode *ic = G[i].ic;
  const operand *left = IC_LEFT (ic);
  const operand *right = IC_RIGHT (ic);
  const int clobbers = instruction_clobbers (ic);
  const bool right_needs_ax_free = ic->op == '+' || ic->op == '-' || ic->op == '*' ||
    ic->op == BITWISEAND || ic->op == '|' || ic->op == '^' || ic->op == EQ_OP ||
    ic->op == NE_OP || ic->op == '<' || ic->op == '>';
  std::map<int, value_layout> values;

  for (var_t v : G[i].alive)
    {
      value_layout &value = values[I[v].v];

      if (value.registers.empty ())
        value.registers.resize (I[v].size, -1);
      value.registers[I[v].byte] = a.global[v];
      value.survives |= G[i].dying.find (v) == G[i].dying.end ();
    }

  for (const auto &entry : values)
    {
      const std::vector<reg_t> &layout = entry.second.registers;
      const bool byte_in_register = layout.size () == 1 && layout[0] >= 0;
      const bool byte_in_ax = byte_in_register && layout[0] <= K78K0_RB0_A_IDX;
      int registers = 0;

      for (reg_t reg : layout)
        if (reg >= 0)
          registers |= 1 << reg;

      if (!legal_layout (layout))
        return false;

      if ((ic->op == LEFT_OP || ic->op == RIGHT_OP) && operand_is_symbol (right, entry.first) &&
          byte_in_register && layout[0] != K78K0_RB0_C_IDX)
        return false;

      if (byte_in_ax && right_needs_ax_free &&
          (operand_is_symbol (right, entry.first) ||
           (operand_is_symbol (left, entry.first) && operand_is_spilled (right, a, i, G))))
        return false;

      const bool overwritten = !POINTER_SET (ic) && operand_is_symbol (IC_RESULT (ic), entry.first);
      if (entry.second.survives && !overwritten && (registers & clobbers))
        return false;
    }

  return true;
}

template <class G_t, class I_t>
static float
instruction_cost (const assignment &a, unsigned short i, const G_t &G, const I_t &I)
{
  if (!inst_sane (a, i, G, I))
    return std::numeric_limits<float>::infinity ();

  float cost = 0.0f;
  const float frequency = std::max (1.0f, G[i].ic->count);
  for (const auto &operand : G[i].operands)
    if (a.global[operand.second] < 0)
      cost += frequency * (I[operand.second].byte < 2 ? 2.0f : 1.0f);
  return cost;
}

template <class G_t, class I_t>
static bool
assignment_hopeless (const assignment &a, unsigned short, const G_t &, const I_t &I, const var_t lastvar)
{
  const int symbol_key = I[lastvar].v;
  std::vector<reg_t> partial (I[lastvar].size, -2);
  bool has_register = false, has_spill = false;

  for (var_t v : a.local)
    if (I[v].v == symbol_key)
      {
        partial[I[v].byte] = a.global[v];
        has_register |= a.global[v] >= 0;
        has_spill |= a.global[v] < 0;
      }

  if (has_register && has_spill && partial.size () != 4)
    return true;
  if (!has_register || partial.size () == 1)
    return false;

  if (partial.size () == 2)
    for (reg_t low = K78K0_RB0_X_IDX; low <= K78K0_RB0_E_IDX; low += 2)
      if ((partial[0] == -2 || partial[0] == low) &&
          (partial[1] == -2 || partial[1] == low + 1))
        return false;
  else if (partial.size () <= 4)
    {
      for (unsigned byte = 0; byte < partial.size (); byte++)
        if (partial[byte] >= 0 && partial[byte] != (reg_t)byte)
          return true;

      if (partial.size () == 4)
        for (unsigned byte = 0; byte < 4; byte += 2)
          if (partial[byte] != -2 && partial[byte + 1] != -2 &&
              (partial[byte] < 0) != (partial[byte + 1] < 0))
            return true;

      return false;
    }

  return true;
}

template <class G_t, class I_t>
static float
rough_cost_estimate (const assignment &a, unsigned short, const G_t &, const I_t &I)
{
  float cost = 0.0f;

  for (var_t v : a.local)
    {
      const symbol *sym = static_cast<symbol *> (hTabItemWithKey (liveRanges, I[v].v));
      if (a.global[v] < 0 && !sym->remat)
        cost += IS_REGISTER (sym->type) ? 1.0f : 0.05f;
    }
  return cost;
}

template <class T_t>
static void
get_best_local_assignment_biased (assignment &a,
                                  typename boost::graph_traits<T_t>::vertex_descriptor t,
                                  const T_t &T)
{
  a = *T[t].assignments.begin ();
  varset_t local;
  std::set_union (T[t].alive.begin (), T[t].alive.end (), a.local.begin (), a.local.end (),
                  std::back_inserter (local));
  a.local.swap (local);
}

static void extra_ic_generated (iCode *) {}

template <class G_t, class I_t>
static void
set_surviving_regs (const assignment &a, unsigned short i, const G_t &G, const I_t &I)
{
  iCode *ic = G[i].ic;

  bitVectClear (ic->rMask);
  bitVectClear (ic->rSurv);
  for (var_t v : G[i].alive)
    if (a.global[v] >= 0)
      {
        ic->rMask = bitVectSetBit (ic->rMask, a.global[v]);
        if (G[i].dying.find (v) == G[i].dying.end () &&
            !operand_is_symbol (IC_RESULT (ic), I[v].v))
          ic->rSurv = bitVectSetBit (ic->rSurv, a.global[v]);
      }
}

template <class T_t, class G_t, class I_t>
static void
allocate (T_t &T, G_t &G, const I_t &I)
{
  con2_t conflicts (boost::num_vertices (I));
  for (unsigned v = 0; v < boost::num_vertices (I); v++)
    {
      conflicts[v].v = I[v].v;
      conflicts[v].byte = I[v].byte;
      conflicts[v].size = I[v].size;
      conflicts[v].name = I[v].name;
    }
  typename boost::graph_traits<I_t>::edge_iterator edge, edge_end;
  for (boost::tie (edge, edge_end) = boost::edges (I); edge != edge_end; ++edge)
    boost::add_edge (boost::source (*edge, I), boost::target (*edge, I), conflicts);

  assignment context;
  bool optimal = true;
  const auto root = find_root (T);
  tree_dec_ralloc_nodes (T, root, G, conflicts, context, &optimal);
  const assignment &winner = *T[root].assignments.begin ();

  for (unsigned v = 0; v < boost::num_vertices (I); v++)
    {
      symbol *sym = static_cast<symbol *> (hTabItemWithKey (liveRanges, I[v].v));
      const reg_t reg = winner.global[v];
      sym->regs[I[v].byte] = reg >= 0 ? k78k0_regs + reg : NULL;
      sym->nRegs = I[v].size;
    }

  for (unsigned v = 0; v < boost::num_vertices (I);)
    {
      symbol *sym = static_cast<symbol *> (hTabItemWithKey (liveRanges, I[v].v));
      const int size = I[v].size;
      const auto first = winner.global.begin () + v;
      const auto last = first + size;

      if (std::any_of (first, last, [](reg_t reg) { return reg < 0; }))
        {
          k78k0SpillThis (sym, false);
          if (size == 4 && std::any_of (first, last, [](reg_t reg) { return reg >= 0; }))
            k78k0_partial_allocations = bitVectSetBit (k78k0_partial_allocations, sym->key);
        }
      v += size;
    }

  for (unsigned i = 0; i < boost::num_vertices (G); i++)
    set_surviving_regs (winner, i, G, I);
}

extern "C" iCode *
k78k0_ralloc2_cc (ebbIndex *ebbi)
{
  cfg_t cfg;
  con_t conflicts;
  iCode *ic = create_cfg (cfg, conflicts, ebbi);

  if (!boost::num_vertices (conflicts))
    return ic;

  if (optimize.genconstprop)
    recomputeValinfos (ic, ebbi, "_3");
  guessCounts (ic, ebbi);

  tree_dec_t decomposition;
  get_nice_tree_decomposition (decomposition, cfg);
  alive_tree_dec (decomposition, cfg);
  good_re_root (decomposition);
  nicify (decomposition);
  alive_tree_dec (decomposition, cfg);
  allocate (decomposition, cfg, conflicts);
  return ic;
}
