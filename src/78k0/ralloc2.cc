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
#include "gen.h"
}

template <class I_t>
static void add_operand_conflicts_in_node (const cfg_node &, I_t &) {}

static constexpr reg_t spilled_register = -1;
static constexpr reg_t unknown_register = -2;

struct register_layout
{
  register_layout (unsigned size, reg_t initial) : size (size)
  {
    bytes[0] = bytes[1] = initial;
  }

  reg_t bytes[2];
  unsigned size;
};

static bool
layout_completable (const register_layout &layout)
{
  if (layout.size == 1)
    return layout.bytes[0] == unknown_register ||
      layout.bytes[0] == spilled_register ||
      (layout.bytes[0] >= K78K0_RB0_X_IDX &&
       layout.bytes[0] <= K78K0_RB0_H_IDX);
  if (layout.size != 2)
    return false;

  const auto matches = [&layout](reg_t low, reg_t high)
  {
    return (layout.bytes[0] == unknown_register || layout.bytes[0] == low) &&
      (layout.bytes[1] == unknown_register || layout.bytes[1] == high);
  };

  if (matches (spilled_register, spilled_register))
    return true;

  for (reg_t low = K78K0_RB0_X_IDX; low <= K78K0_RB0_L_IDX; low += 2)
    if (matches (low, low + 1))
      return true;

  return false;
}

static bool
legal_layout (const register_layout &layout)
{
  return layout.bytes[0] != unknown_register &&
    (layout.size == 1 || layout.bytes[1] != unknown_register) &&
    layout_completable (layout);
}

static bool
operand_is_symbol (const operand *op, const int key)
{
  return op && IS_SYMOP (op) && OP_SYMBOL_CONST (op)->key == key;
}

template <class G_t>
static bool
operand_is_spilled (const operand *op, const assignment &a, unsigned short i, const G_t &G)
{
  if (!op || !IS_SYMOP (op))
    return false;

  const symbol *sym = OP_SYMBOL_CONST (op);
  if (IS_TRUE_SYMOP (op))
    return sym->onStack;
  if (!IS_ITEMP (op) || sym->remat || sym->regType == REG_CND)
    return false;

  const auto range = G[i].operands.equal_range (sym->key);
  /* Graph-absent iTemps use the generic dry stack operand and receive a real
     spill slot after allocation when their lifetime requires one. */
  if (range.first == range.second)
    return true;
  for (auto operand = range.first; operand != range.second; ++operand)
    if (a.global[operand->second] < 0)
      return true;
  return false;
}

template <class G_t, class I_t>
static bool
operand_sane (const operand *op, unsigned forbidden, const assignment &a,
              unsigned short i, const G_t &G, const I_t &I)
{
  if (!op || !IS_SYMOP (op))
    return true;

  const auto range = G[i].operands.equal_range (OP_SYMBOL_CONST (op)->key);
  if (range.first == range.second)
    return true;

  register_layout layout (I[range.first->second].size, spilled_register);
  unsigned registers = 0;
  for (auto entry = range.first; entry != range.second; ++entry)
    {
      const reg_t reg = a.global[entry->second];

      layout.bytes[I[entry->second].byte] = reg;
      if (reg >= 0)
        registers |= 1u << reg;
    }

  return legal_layout (layout) && !(registers & forbidden);
}

template <class G_t>
static k78k0_instruction_traits
instruction_constraints (const iCode *ic, const assignment &a, unsigned short i,
                         const G_t &G)
{
  k78k0_instruction_traits constraints = k78k0InstructionTraits (ic);

  if (operand_is_spilled (IC_RIGHT (ic), a, i, G))
    constraints.left |= constraints.left_if_right_spilled;
  if (operand_is_spilled (IC_LEFT (ic), a, i, G))
    constraints.right |= constraints.right_if_left_spilled;

  return constraints;
}

template <class G_t, class I_t>
static bool
inst_sane (const assignment &a, unsigned short i, const G_t &G, const I_t &I)
{
  const iCode *ic = G[i].ic;
  const bool stack_uses_hl = ic->op == ADDRESS_OF ?
    operand_is_spilled (IC_RESULT (ic), a, i, G) :
    operand_is_spilled (IC_LEFT (ic), a, i, G) ||
    operand_is_spilled (IC_RIGHT (ic), a, i, G) ||
    operand_is_spilled (IC_RESULT (ic), a, i, G);
  k78k0_instruction_traits constraints = instruction_constraints (ic, a, i, G);
  const unsigned clobbers = constraints.clobbers |
    (stack_uses_hl ? K78K0_MASK_HL : 0);

  /* HL is the backend's stack and pointer scratch pair. Dying inputs need an
     explicit restriction because the survivor check below does not cover them. */
  if (clobbers & K78K0_MASK_HL)
    {
      constraints.left |= K78K0_MASK_HL;
      constraints.right |= K78K0_MASK_HL;
      if (POINTER_SET (ic))
        constraints.result |= K78K0_MASK_HL;
    }

  if (!operand_sane (IC_LEFT (ic), constraints.left, a, i, G, I) ||
      !operand_sane (IC_RIGHT (ic), constraints.right, a, i, G, I) ||
      !operand_sane (IC_RESULT (ic), constraints.result, a, i, G, I))
    return false;

  for (var_t v : G[i].alive)
    if (a.global[v] >= 0 && G[i].dying.find (v) == G[i].dying.end () &&
        (POINTER_SET (ic) || !operand_is_symbol (IC_RESULT (ic), I[v].v)) &&
        (clobbers & (1u << a.global[v])))
      return false;

  return true;
}

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
            (POINTER_SET (ic) || !operand_is_symbol (IC_RESULT (ic), I[v].v)))
          ic->rSurv = bitVectSetBit (ic->rSurv, a.global[v]);
      }
}

template <class G_t, class I_t>
static void
assign_operand_for_cost (operand *op, const assignment &a, unsigned short i,
                         const G_t &G, const I_t &I)
{
  if (!op || !IS_SYMOP (op))
    return;

  symbol *sym = OP_SYMBOL (op);
  const auto range = G[i].operands.equal_range (sym->key);
  if (range.first == range.second)
    return;

  const int size = I[range.first->second].size;
  bool has_register = false;
  bool has_spill = false;

  sym->nRegs = size;
  std::fill (sym->regs, sym->regs + size, static_cast<reg_info *> (NULL));
  for (auto entry = range.first; entry != range.second; ++entry)
    {
      const var_t v = entry->second;
      const reg_t reg = a.global[v];

      if (reg >= 0)
        {
          sym->regs[I[v].byte] = k78k0_regs + reg;
          has_register = true;
        }
      else
        has_spill = true;
    }

  sym->isspilt = has_spill && !has_register;
  sym->spillA = has_spill;
  sym->stackSpil = has_spill && !sym->remat;
}

template <class G_t, class I_t>
static void
assign_operands_for_cost (const assignment &a, unsigned short i, const G_t &G,
                          const I_t &I)
{
  const iCode *ic = G[i].ic;

  if (ic->op == IFX)
    assign_operand_for_cost (IC_COND (ic), a, i, G, I);
  else if (ic->op == JUMPTABLE)
    assign_operand_for_cost (IC_JTCOND (ic), a, i, G, I);
  else
    {
      assign_operand_for_cost (IC_LEFT (ic), a, i, G, I);
      assign_operand_for_cost (IC_RIGHT (ic), a, i, G, I);
      assign_operand_for_cost (IC_RESULT (ic), a, i, G, I);
    }

  const iCode *next = ic->next;
  if (next && next->op == '=' && !POINTER_SET (next) && IC_RESULT (ic) &&
      IS_ITEMP (IC_RESULT (ic)) && IC_RIGHT (next) && IS_ITEMP (IC_RIGHT (next)) &&
      OP_SYMBOL_CONST (IC_RESULT (ic)) == OP_SYMBOL_CONST (IC_RIGHT (next)))
    {
      const auto adjacent = adjacent_vertices (i, G);

      for (auto node = adjacent.first; node != adjacent.second; ++node)
        if (G[*node].ic == next)
          {
            assign_operand_for_cost (IC_RESULT (next), a, (unsigned short)*node, G, I);
            break;
          }
    }

  if (ic->op == SEND && ic->builtinSEND)
    {
      const auto adjacent = adjacent_vertices (i, G);

      if (adjacent.first != adjacent.second)
        assign_operands_for_cost (a, (unsigned short)*adjacent.first, G, I);
    }
}

static bool
assignment_does_not_matter (const iCode *ic)
{
  return ic->op == FUNCTION || ic->op == ENDFUNCTION || ic->op == LABEL ||
    ic->op == GOTO || ic->op == INLINEASM;
}

template <class G_t, class I_t>
static float
instruction_cost (const assignment &a, unsigned short i, const G_t &G, const I_t &I)
{
  iCode *ic = G[i].ic;

  if (!inst_sane (a, i, G, I))
    return std::numeric_limits<float>::infinity ();
  if (ic->generated || assignment_does_not_matter (ic))
    return 0.0f;

  assign_operands_for_cost (a, i, G, I);
  set_surviving_regs (a, i, G, I);

  const float cost = k78k0DryInstructionCost (ic);
  ic->generated = false;
  return cost;
}

template <class G_t, class I_t>
static bool
assignment_hopeless (const assignment &a, unsigned short, const G_t &, const I_t &I, const var_t lastvar)
{
  const unsigned size = I[lastvar].size;
  const var_t first = lastvar - I[lastvar].byte;
  register_layout layout (size, unknown_register);

  for (unsigned byte = 0; byte < size; byte++)
    {
      const var_t v = first + byte;

      if (std::binary_search (a.local.begin (), a.local.end (), v))
        layout.bytes[byte] = a.global[v];
    }

  return !layout_completable (layout);
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

/* Comparisons and GETABIT directly emit a following IFX branch. Keep the
   fused boolean out of the conflict graph and do not cost the IFX twice. */
static void
extra_ic_generated (iCode *ic)
{
  if (ic->op == CALL || ic->op == PCALL)
    {
      iCode *bridge = k78k0HiddenReturnForwardBridge (
        ic, currFunc ? currFunc->type : NULL);

      if (bridge)
        {
          bridge->generated = true;
          if (bridge->op == ADDRESS_OF)
            bridge->next->generated = true;
        }
    }

  if (ic->op == EQ_OP || ic->op == NE_OP || ic->op == '<' || ic->op == '>' ||
      ic->op == GETABIT)
    {
      iCode *ifx = ifxForOp (IC_RESULT (ic), ic);

      if (ifx)
        {
          OP_SYMBOL (IC_RESULT (ic))->for_newralloc = false;
          OP_SYMBOL (IC_RESULT (ic))->regType = REG_CND;
          ifx->generated = true;
        }
    }
}

template <class T_t, class G_t, class I_t>
static void
allocate (T_t &T, G_t &G, const I_t &I)
{
  const unsigned variable_count = boost::num_vertices (I);
  con2_t conflicts (variable_count);
  for (unsigned v = 0; v < variable_count; v++)
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
  assignment spill_fallback;
  const assignment &winner = [&]() -> const assignment &
  {
    if (T[root].assignments.empty ())
      {
        /* An unsupported dry lowering gives every candidate infinite cost.
           Keep compilation correct by falling back to virtual-stack storage. */
        spill_fallback.global.resize (variable_count, -1);
        return spill_fallback;
      }
    return *T[root].assignments.begin ();
  }();

  for (unsigned v = 0; v < variable_count;)
    {
      symbol *sym = static_cast<symbol *> (hTabItemWithKey (liveRanges, I[v].v));
      const int size = I[v].size;
      bool spilled = false;

      sym->nRegs = size;
      for (int byte = 0; byte < size; byte++)
        {
          const reg_t reg = winner.global[v + byte];

          sym->regs[I[v + byte].byte] = reg >= 0 ? k78k0_regs + reg : NULL;
          spilled |= reg < 0;
        }

      if (spilled)
        k78k0SpillThis (sym);
      else
        sym->isspilt = sym->spillA = sym->stackSpil = false;
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
