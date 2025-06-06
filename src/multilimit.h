/*
	This file is part of Warzone 2100.
	Copyright (C) 1999-2004  Eidos Interactive
	Copyright (C) 2005-2025  Warzone 2100 Project

	Warzone 2100 is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	Warzone 2100 is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Warzone 2100; if not, write to the Free Software
	Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
*/
/*
 * multilimit.h
 */

#ifndef __INCLUDED_MULTILIMIT_H__
#define __INCLUDED_MULTILIMIT_H__

// See titleui.h for the actual class; dependency reduction measure

bool applyLimitSet();
void createLimitSet();
void resetLimits(void);

#include <functional>
typedef std::function<void(const std::string& line)> PrintStructureLimitsLineFunc;
void printStructureLimitsInfo(std::vector<MULTISTRUCTLIMITS>& structureLimits, const PrintStructureLimitsLineFunc& printLineFunc);

#endif //__cplusplus //__INCLUDED_MULTILIMIT_H__
