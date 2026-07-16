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

static bool
valid_word_layout (reg_t low, reg_t high)
{
  return low == spilled_register ? high == spilled_register :
    low >= K78K0_RB0_X_IDX && low <= K78K0_RB0_L_IDX && !(low & 1) &&
    high == low + 1;
}

struct operand_layout
{
  int size = 0;
  reg_t registers[2] = {spilled_register, spilled_register};
  unsigned mask = 0;
};

template <class G_t, class I_t>
static operand_layout
assigned_operand_layout (const operand *op, const assignment &a,
                         unsigned short i, const G_t &G, const I_t &I)
{
  operand_layout layout;

  if (!op || !IS_SYMOP (op))
    return layout;

  const auto range = G[i].operands.equal_range (OP_SYMBOL_CONST (op)->key);
  for (auto entry = range.first; entry != range.second; ++entry)
    {
      const var_t v = entry->second;
      const reg_t reg = a.global[v];

      layout.size = I[v].size;
      layout.registers[I[v].byte] = reg;
      if (reg >= 0)
        layout.mask |= 1u << reg;
    }
  return layout;
}

static bool
operand_is_spilled (const operand *op, const operand_layout &layout)
{
  if (!op || !IS_SYMOP (op))
    return false;

  const symbol *sym = OP_SYMBOL_CONST (op);
  if (IS_TRUE_SYMOP (op))
    return sym->onStack;
  if (sym->remat || sym->regType == REG_CND)
    return false;

  /* Graph-absent iTemps use the generic dry stack operand and receive a real
     spill slot after allocation when their lifetime requires one. */
  return !layout.size || layout.registers[0] < 0 ||
    layout.size == 2 && layout.registers[1] < 0;
}

static bool
layout_sane (const operand_layout &layout, unsigned forbidden)
{
  if (!layout.size)
    return true;

  const bool sane = layout.size == 1 ?
    layout.registers[0] == spilled_register ||
      layout.registers[0] >= K78K0_RB0_X_IDX &&
      layout.registers[0] <= K78K0_RB0_H_IDX :
    layout.size == 2 && valid_word_layout (layout.registers[0],
                                           layout.registers[1]);

  return sane && !(layout.mask & forbidden);
}

static const operand *
byte_pointer_preserved_in_de (const iCode *ic, unsigned clobbers)
{
  if (clobbers != (K78K0_MASK_AX | K78K0_MASK_DE))
    return NULL;

  if (ic->op != GET_VALUE_AT_ADDRESS)
    {
      const operand *value = IC_RIGHT (ic);

      if (!value || getSize (operandType (value)) != 1)
        return NULL;
      return ic->op == SET_VALUE_AT_ADDRESS ? IC_LEFT (ic) :
        POINTER_SET (ic) ? IC_RESULT (ic) : NULL;
    }

  const operand *offset = IC_RIGHT (ic);

  return offset && IS_OP_LITERAL (offset) && operandLitValue (offset) == 0 ?
    IC_LEFT (ic) : NULL;
}

static bool
layout_is_pair (const operand_layout &layout, reg_t low)
{
  return layout.size == 2 && layout.registers[0] == low &&
    layout.registers[1] == low + 1;
}

template <class G_t, class I_t>
static unsigned
set_surviving_regs (const assignment &a, unsigned short i,
                    const G_t &G, const I_t &I)
{
  iCode *ic = G[i].ic;
  const operand *result = IC_RESULT (ic);
  /* An ordinary result is a definition; a pointer-store result is an input. */
  const symbol *defined_result = !POINTER_SET (ic) && result && IS_SYMOP (result) ?
    OP_SYMBOL_CONST (result) : NULL;
  unsigned survivors = 0;

  bitVectClear (ic->rMask);
  bitVectClear (ic->rSurv);

  for (var_t v : G[i].alive)
    {
      const reg_t reg = a.global[v];

      if (reg < 0)
        continue;

      ic->rMask = bitVectSetBit (ic->rMask, reg);
      if (G[i].dying.find (v) != G[i].dying.end () ||
          (defined_result && defined_result->key == I[v].v))
        continue;

      ic->rSurv = bitVectSetBit (ic->rSurv, reg);
      survivors |= 1u << reg;
    }
  return survivors;
}

template <class G_t, class I_t>
static bool
inst_sane (const assignment &a, unsigned short i, const G_t &G, const I_t &I,
           const unsigned survivors)
{
  const iCode *ic = G[i].ic;
  const operand_layout left_layout =
    assigned_operand_layout (IC_LEFT (ic), a, i, G, I);
  const operand_layout right_layout =
    assigned_operand_layout (IC_RIGHT (ic), a, i, G, I);
  const operand_layout result_layout =
    assigned_operand_layout (IC_RESULT (ic), a, i, G, I);
  const bool left_spilled = operand_is_spilled (IC_LEFT (ic), left_layout);
  const bool right_spilled = operand_is_spilled (IC_RIGHT (ic), right_layout);
  const bool result_spilled = operand_is_spilled (IC_RESULT (ic), result_layout);
  const bool stack_uses_hl = ic->op == ADDRESS_OF ?
    result_spilled : left_spilled || right_spilled || result_spilled;
  k78k0_instruction_traits constraints = k78k0InstructionTraits (ic);

  if (right_spilled)
    constraints.left |= constraints.left_if_right_spilled;
  if (left_spilled)
    constraints.right |= constraints.right_if_left_spilled;

  unsigned clobbers = constraints.clobbers;
  const operand *de_pointer =
    byte_pointer_preserved_in_de (ic, constraints.clobbers);
  if (de_pointer)
    {
      const operand_layout &pointer_layout =
        de_pointer == IC_RESULT (ic) ? result_layout : left_layout;

      if (layout_is_pair (pointer_layout, K78K0_RB0_E_IDX))
        clobbers &= ~K78K0_MASK_DE;
    }
  if (stack_uses_hl)
    clobbers |= K78K0_MASK_HL;
  const unsigned hl_clobbers = clobbers & K78K0_MASK_HL;

  /* HL is the backend's stack and pointer scratch pair. Dying inputs need an
     explicit restriction because the survivor check below does not cover them. */
  constraints.left |= hl_clobbers;
  constraints.right |= hl_clobbers;
  if (POINTER_SET (ic))
    constraints.result |= hl_clobbers;

  return layout_sane (left_layout, constraints.left) &&
    layout_sane (right_layout, constraints.right) &&
    layout_sane (result_layout, constraints.result) &&
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
  const operand_layout layout = assigned_operand_layout (op, a, i, G, I);

  if (layout.size)
    assign_symbol_registers (OP_SYMBOL (op), layout.size, layout.registers);
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
    assign_operand_for_cost (IC_RESULT (next), a, next_instruction (i, G),
                             G, I);

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
  const unsigned survivors = set_surviving_regs (a, i, G, I);

  if (!inst_sane (a, i, G, I, survivors))
    return std::numeric_limits<float>::infinity ();
  if (ic->generated || assignment_does_not_matter (ic))
    return 0.0f;

  assign_operands_for_cost (a, i, G, I);

  const float cost = k78k0DryInstructionCost (ic);
  ic->generated = false;
  return cost;
}

template <class G_t, class I_t>
static bool
assignment_hopeless (const assignment &a, unsigned short, const G_t &, const I_t &I, const var_t lastvar)
{
  const int size = I[lastvar].size;
  const int byte = I[lastvar].byte;
  const reg_t reg = a.global[lastvar];

  if (size == 1)
    return reg < spilled_register || reg > K78K0_RB0_H_IDX;
  if (size != 2 || reg < spilled_register || reg > K78K0_RB0_H_IDX ||
      (reg >= 0 && (reg & 1) != byte))
    return true;

  const var_t sibling = lastvar + (byte ? -1 : 1);
  if (!std::binary_search (a.local.begin (), a.local.end (), sibling))
    return false;
  return byte ? !valid_word_layout (a.global[sibling], reg) :
    !valid_word_layout (reg, a.global[sibling]);
}

template <class G_t, class I_t>
static float
rough_cost_estimate (const assignment &a, unsigned short, const G_t &, const I_t &I)
{
  float cost = 0.0f;

  for (var_t v : a.local)
    {
      if (a.global[v] >= 0)
        continue;

      const symbol *sym =
        static_cast<const symbol *> (hTabItemWithKey (liveRanges, I[v].v));
      if (!sym->remat)
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

/* Mark iCodes emitted by return or comparison fusion. */
static void
extra_ic_generated (iCode *ic)
{
  if (ic->op == CALL || ic->op == PCALL)
    {
      if (iCode *bridge = k78k0ReturnForwardBridge (
            ic, currFunc ? currFunc->type : NULL))
        {
          OP_SYMBOL (IC_RESULT (ic))->for_newralloc = false;
          bridge->generated = true;
          if (bridge->op == ADDRESS_OF)
            bridge->next->generated = true;
        }
      return;
    }

  if (ic->op != EQ_OP && ic->op != NE_OP && ic->op != '<' &&
      ic->op != '>' && ic->op != GETABIT)
    return;

  if (iCode *ifx = ifxForOp (IC_RESULT (ic), ic))
    {
      OP_SYMBOL (IC_RESULT (ic))->for_newralloc = false;
      OP_SYMBOL (IC_RESULT (ic))->regType = REG_CND;
      ifx->generated = true;
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

      if (assign_symbol_registers (sym, size, &winner.global[v]))
        k78k0SpillThis (sym);
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
