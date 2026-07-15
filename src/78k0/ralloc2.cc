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

static bool
valid_register_pair (reg_t low, reg_t high)
{
  return low == spilled_register ? high == spilled_register :
    low >= K78K0_RB0_X_IDX && low <= K78K0_RB0_L_IDX && !(low & 1) &&
    high == low + 1;
}

static bool
register_pair_completable (reg_t low, reg_t high)
{
  if (low == unknown_register)
    return high == unknown_register || high == spilled_register ||
      high >= K78K0_RB0_A_IDX && high <= K78K0_RB0_H_IDX && (high & 1);
  if (high == unknown_register)
    return low == spilled_register ||
      low >= K78K0_RB0_X_IDX && low <= K78K0_RB0_L_IDX && !(low & 1);
  return valid_register_pair (low, high);
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
  if (sym->remat || sym->regType == REG_CND)
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

  const int size = I[range.first->second].size;
  reg_t registers_by_byte[2] = {spilled_register, spilled_register};
  unsigned registers = 0;
  for (auto entry = range.first; entry != range.second; ++entry)
    {
      const reg_t reg = a.global[entry->second];

      registers_by_byte[I[entry->second].byte] = reg;
      if (reg >= 0)
        registers |= 1u << reg;
    }

  const bool layout_sane = size == 1 ?
    registers_by_byte[0] == spilled_register ||
      registers_by_byte[0] >= K78K0_RB0_X_IDX &&
      registers_by_byte[0] <= K78K0_RB0_H_IDX :
    size == 2 && valid_register_pair (registers_by_byte[0], registers_by_byte[1]);

  return layout_sane && !(registers & forbidden);
}

struct register_masks
{
  unsigned assigned;
  unsigned surviving;
};

template <class G_t, class I_t>
static register_masks
assignment_register_masks (const assignment &a, unsigned short i,
                           const G_t &G, const I_t &I)
{
  const iCode *ic = G[i].ic;
  const operand *result = IC_RESULT (ic);
  /* An ordinary result is a definition; a pointer-store result is an input. */
  const symbol *defined_result = !POINTER_SET (ic) && result && IS_SYMOP (result) ?
    OP_SYMBOL_CONST (result) : NULL;
  register_masks masks = {0, 0};

  for (var_t v : G[i].alive)
    if (a.global[v] >= 0)
      {
        const unsigned bit = 1u << a.global[v];

        masks.assigned |= bit;
        if (G[i].dying.find (v) == G[i].dying.end () &&
            (!defined_result || defined_result->key != I[v].v))
          masks.surviving |= bit;
      }
  return masks;
}

static void
set_register_masks (iCode *ic, const register_masks masks)
{
  bitVectClear (ic->rMask);
  bitVectClear (ic->rSurv);
  for (unsigned reg = K78K0_RB0_X_IDX; reg <= K78K0_RB0_H_IDX; reg++)
    {
      if (masks.assigned & (1u << reg))
        ic->rMask = bitVectSetBit (ic->rMask, reg);
      if (masks.surviving & (1u << reg))
        ic->rSurv = bitVectSetBit (ic->rSurv, reg);
    }
}

template <class G_t, class I_t>
static bool
inst_sane (const assignment &a, unsigned short i, const G_t &G, const I_t &I,
           const unsigned survivors)
{
  const iCode *ic = G[i].ic;
  const bool left_spilled = operand_is_spilled (IC_LEFT (ic), a, i, G);
  const bool right_spilled = operand_is_spilled (IC_RIGHT (ic), a, i, G);
  const bool result_spilled = operand_is_spilled (IC_RESULT (ic), a, i, G);
  const bool stack_uses_hl = ic->op == ADDRESS_OF ?
    result_spilled : left_spilled || right_spilled || result_spilled;
  k78k0_instruction_traits constraints = k78k0InstructionTraits (ic);

  if (right_spilled)
    constraints.left |= constraints.left_if_right_spilled;
  if (left_spilled)
    constraints.right |= constraints.right_if_left_spilled;

  const unsigned clobbers = constraints.clobbers |
    (stack_uses_hl ? K78K0_MASK_HL : 0);
  const unsigned hl_clobbers = clobbers & K78K0_MASK_HL;

  /* HL is the backend's stack and pointer scratch pair. Dying inputs need an
     explicit restriction because the survivor check below does not cover them. */
  constraints.left |= hl_clobbers;
  constraints.right |= hl_clobbers;
  if (POINTER_SET (ic))
    constraints.result |= hl_clobbers;

  return operand_sane (IC_LEFT (ic), constraints.left, a, i, G, I) &&
    operand_sane (IC_RIGHT (ic), constraints.right, a, i, G, I) &&
    operand_sane (IC_RESULT (ic), constraints.result, a, i, G, I) &&
    !(survivors & clobbers);
}

static bool
assign_symbol_registers (symbol *sym, const int size, const reg_t *registers)
{
  bool has_register = false;
  bool has_spill = false;

  sym->nRegs = size;
  for (int byte = 0; byte < size; byte++)
    {
      const reg_t reg = registers[byte];

      sym->regs[byte] = reg >= 0 ? k78k0_regs + reg : NULL;
      has_register |= reg >= 0;
      has_spill |= reg < 0;
    }

  sym->isspilt = has_spill && !has_register;
  sym->spillA = has_spill;
  sym->stackSpil = has_spill && !sym->remat;
  return has_spill;
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
  reg_t registers[2] = {spilled_register, spilled_register};
  for (auto entry = range.first; entry != range.second; ++entry)
    registers[I[entry->second].byte] = a.global[entry->second];
  assign_symbol_registers (sym, size, registers);
}

template <class G_t>
static unsigned short
next_instruction (unsigned short i, const G_t &G)
{
  const unsigned short next = i + 1;

  wassert (next < boost::num_vertices (G));
  wassert (G[next].ic == G[i].ic->next);
  return next;
}

template <class G_t, class I_t>
static void
assign_operands_for_cost (const assignment &a, unsigned short i, const G_t &G,
                          const I_t &I)
{
  const iCode *ic = G[i].ic;

  assign_operand_for_cost (IC_LEFT (ic), a, i, G, I);
  assign_operand_for_cost (IC_RIGHT (ic), a, i, G, I);
  assign_operand_for_cost (IC_RESULT (ic), a, i, G, I);

  if (iCode *next = k78k0AdjacentAssignment (ic))
    {
      const unsigned short next_i = next_instruction (i, G);
      assign_operand_for_cost (IC_RESULT (next), a, next_i, G, I);
    }

  if (ic->op == SEND && ic->builtinSEND)
    assign_operands_for_cost (a, next_instruction (i, G), G, I);
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
  const register_masks masks = assignment_register_masks (a, i, G, I);

  if (!inst_sane (a, i, G, I, masks.surviving))
    return std::numeric_limits<float>::infinity ();
  if (ic->generated || assignment_does_not_matter (ic))
    return 0.0f;

  assign_operands_for_cost (a, i, G, I);
  set_register_masks (ic, masks);

  const float cost = k78k0DryInstructionCost (ic);
  ic->generated = false;
  return cost;
}

template <class G_t, class I_t>
static bool
assignment_hopeless (const assignment &a, unsigned short, const G_t &, const I_t &I, const var_t lastvar)
{
  const int size = I[lastvar].size;
  const var_t first = lastvar - I[lastvar].byte;
  reg_t registers_by_byte[2] = {unknown_register, unknown_register};

  for (int byte = 0; byte < size; byte++)
    {
      const var_t v = first + byte;

      if (std::binary_search (a.local.begin (), a.local.end (), v))
        registers_by_byte[byte] = a.global[v];
    }

  if (size == 1)
    return registers_by_byte[0] < unknown_register ||
      registers_by_byte[0] > K78K0_RB0_H_IDX;
  return size != 2 ||
    !register_pair_completable (registers_by_byte[0], registers_by_byte[1]);
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
  /* An empty child makes the later join empty as well; the caller's default
     context is sufficient while the sibling is visited. */
  if (T[t].assignments.empty ())
    return;
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
      if (iCode *bridge = k78k0HiddenReturnForwardBridge (
            ic, currFunc ? currFunc->type : NULL))
        {
          bridge->generated = true;
          if (bridge->op == ADDRESS_OF)
            bridge->next->generated = true;
        }
    }
  else if (ic->op == EQ_OP || ic->op == NE_OP || ic->op == '<' ||
           ic->op == '>' || ic->op == GETABIT)
    {
      if (iCode *ifx = ifxForOp (IC_RESULT (ic), ic))
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
  const bool allocation_failed = T[root].assignments.empty ();

  /* An unsupported dry lowering gives every candidate infinite cost. Keep
     compilation correct by falling back to stack storage. */
  if (allocation_failed)
    spill_fallback.global.resize (variable_count, spilled_register);
  const assignment &winner = allocation_failed ?
    spill_fallback : *T[root].assignments.begin ();

  for (unsigned v = 0; v < variable_count;)
    {
      symbol *sym = static_cast<symbol *> (hTabItemWithKey (liveRanges, I[v].v));
      const int size = I[v].size;
      reg_t registers[2] = {spilled_register, spilled_register};

      for (int byte = 0; byte < size; byte++)
        registers[byte] = winner.global[v + byte];

      if (assign_symbol_registers (sym, size, registers))
        k78k0SpillThis (sym);
      v += size;
    }

  for (unsigned i = 0; i < boost::num_vertices (G); i++)
    set_register_masks (G[i].ic, assignment_register_masks (winner, i, G, I));
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
