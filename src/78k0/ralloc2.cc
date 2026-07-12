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
  MASK_ALL = (1 << 6) - 1,
};

template <class I_t>
static void
add_operand_conflicts_in_node (const cfg_node &, I_t &)
{
}

static bool
legal_layout (const std::vector<reg_t> &layout)
{
  const bool all_spilled = std::find_if (layout.begin (), layout.end (),
                                         [](reg_t reg) { return reg >= 0; }) == layout.end ();
  if (all_spilled)
    return true;
  if (std::find (layout.begin (), layout.end (), -1) != layout.end ())
    return false;

  switch (layout.size ())
    {
    case 1:
      return layout[0] >= K78K0_RB0_X_IDX && layout[0] <= K78K0_RB0_D_IDX;
    case 2:
      return layout[0] % 2 == 0 && layout[1] == layout[0] + 1;
    case 3:
      return layout[0] == K78K0_RB0_X_IDX && layout[1] == K78K0_RB0_A_IDX &&
             layout[2] == K78K0_RB0_C_IDX;
    case 4:
      return layout[0] == K78K0_RB0_X_IDX && layout[1] == K78K0_RB0_A_IDX &&
             layout[2] == K78K0_RB0_C_IDX && layout[3] == K78K0_RB0_B_IDX;
    default:
      return false;
    }
}

static int
layout_registers (const std::vector<reg_t> &layout)
{
  int registers = 0;

  for (reg_t reg : layout)
    if (reg >= 0)
      registers |= 1 << reg;
  return registers;
}

static int
instruction_clobbers (const iCode *ic)
{
  switch (ic->op)
    {
    case FUNCTION:
    case ENDFUNCTION:
    case LABEL:
    case GOTO:
      return 0;
    case '=':
      if (!POINTER_SET (ic) && IC_RESULT (ic) && getSize (operandType (IC_RESULT (ic))) == 1)
        return MASK_AX | (IS_TRUE_SYMOP (IC_RESULT (ic)) && OP_SYMBOL_CONST (IC_RESULT (ic))->onStack ?
                          1 << K78K0_RB0_C_IDX : 0);
      return MASK_ALL;
    case IFX:
      return IC_COND (ic) && getSize (operandType (IC_COND (ic))) == 1 ? MASK_AX : MASK_ALL;
    default:
      return MASK_ALL;
    }
}

template <class G_t, class I_t>
static bool
inst_sane (const assignment &a, unsigned short i, const G_t &G, const I_t &I)
{
  std::map<int, std::vector<reg_t>> layouts;
  std::map<int, std::vector<var_t>> variables;

  for (var_t v : G[i].alive)
    {
      std::vector<reg_t> &layout = layouts[I[v].v];
      std::vector<var_t> &vars = variables[I[v].v];

      if (layout.empty ())
        {
          layout.resize (I[v].size, -1);
          vars.resize (I[v].size, -1);
        }
      layout[I[v].byte] = a.global[v];
      vars[I[v].byte] = v;
    }

  for (const auto &entry : layouts)
    {
      const std::vector<reg_t> &layout = entry.second;
      const std::vector<var_t> &vars = variables[entry.first];
      bool survives = false;

      if (!legal_layout (layout))
        return false;

      for (var_t v : vars)
        if (v >= 0 && G[i].dying.find (v) == G[i].dying.end ())
          {
            const operand *result = IC_RESULT (G[i].ic);
            if (!result || !IS_SYMOP (result) || OP_SYMBOL_CONST (result)->key != entry.first)
              survives = true;
          }

      if (survives && (layout_registers (layout) & instruction_clobbers (G[i].ic)))
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
  for (const auto &operand : G[i].operands)
    cost += a.global[operand.second] < 0 ? 2.0f : 0.0f;
  return cost;
}

template <class G_t, class I_t>
static bool
assignment_hopeless (const assignment &a, unsigned short, const G_t &, const I_t &I, const var_t lastvar)
{
  const int symbol_key = I[lastvar].v;
  std::vector<reg_t> partial (I[lastvar].size, -2);
  bool has_register = false;
  bool has_spill = false;

  for (var_t v : a.local)
    if (I[v].v == symbol_key)
      {
        partial[I[v].byte] = a.global[v];
        has_register |= a.global[v] >= 0;
        has_spill |= a.global[v] < 0;
      }

  if (has_register && has_spill)
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

static void
extra_ic_generated (iCode *)
{
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
            (!IC_RESULT (ic) || !IS_SYMOP (IC_RESULT (ic)) || OP_SYMBOL_CONST (IC_RESULT (ic))->key != I[v].v))
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
  tree_dec_ralloc_nodes (T, find_root (T), G, conflicts, context, &optimal);
  const assignment &winner = *T[find_root (T)].assignments.begin ();

  for (unsigned v = 0; v < boost::num_vertices (I); v++)
    {
      symbol *sym = static_cast<symbol *> (hTabItemWithKey (liveRanges, I[v].v));
      sym->regs[I[v].byte] = winner.global[v] >= 0 ? k78k0_regs + winner.global[v] : NULL;
      sym->nRegs = I[v].size;
    }

  for (unsigned v = 0; v < boost::num_vertices (I);)
    {
      symbol *sym = static_cast<symbol *> (hTabItemWithKey (liveRanges, I[v].v));
      bool spilled = false;
      for (int byte = 0; byte < I[v].size; byte++, v++)
        spilled |= winner.global[v] < 0;
      if (spilled)
        k78k0SpillThis (sym, false);
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
