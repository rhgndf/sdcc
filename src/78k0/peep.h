/*-------------------------------------------------------------------------
  peep.h - 78K0 peephole data-flow helpers

  Copyright (C) 2026

  This program is free software; you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by the
  Free Software Foundation; either version 2, or (at your option) any
  later version.
-------------------------------------------------------------------------*/

#ifndef SDCC_78K0_PEEP_H
#define SDCC_78K0_PEEP_H

bool k78k0notUsed (const char *, lineNode *, lineNode *);
bool k78k0notUsedFrom (const char *, const char *, lineNode *);

#endif
