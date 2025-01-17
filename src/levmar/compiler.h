#pragma once
/*
*				compiler.h
*
*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
*
*	This file part of:	AstrOmatic software
*
*	Copyright:		(C) 2007-2012 Emmanuel Bertin -- IAP/CNRS/UPMC
*				(C) 2004-2011 Manolis Lourakis (orig. version)
*
*	Licenses:		GNU General Public License
*
*	AstrOmatic software is free software: you can redistribute it and/or
*	modify it under the terms of the GNU General Public License as
*	published by the Free Software Foundation, either version 3 of the
*	License, or (at your option) any later version.
*	AstrOmatic software is distributed in the hope that it will be useful,
*	but WITHOUT ANY WARRANTY; without even the implied warranty of
*	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*	GNU General Public License for more details.
*	You should have received a copy of the GNU General Public License
*	along with AstrOmatic software.
*	If not, see <http://www.gnu.org/licenses/>.
*
*	Last modified:		09/07/2012
*
*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%*/
/////////////////////////////////////////////////////////////////////////////////
// 
//  Levenberg - Marquardt non-linear minimization algorithm
//  Copyright (C) 2004  Manolis Lourakis (lourakis at ics forth gr)
//  Institute of Computer Science, Foundation for Research & Technology - Hellas
//  Heraklion, Crete, Greece.
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
/////////////////////////////////////////////////////////////////////////////////

/* note: intel's icc defines both __ICC & __INTEL_COMPILER.
 * Also, some compilers other than gcc define __GNUC__,
 * therefore gcc should be checked last
 */
#ifdef _MSC_VER
#define inline __inline // MSVC
#elif !defined(__ICC) && !defined(__INTEL_COMPILER) && !defined(__GNUC__)
#define inline // other than MSVC, ICC, GCC: define empty
#endif

#ifdef _MSC_VER
#define LM_FINITE _finite // MSVC
#elif defined(__ICC) || defined(__INTEL_COMPILER) || defined(__GNUC__)
#  ifdef __APPLE__
#  include <math.h>
#  define LM_FINITE isfinite // Apple
#  else
#  define LM_FINITE finite // ICC, GCC
#  endif
#else
#define LM_FINITE finite // other than MSVC, ICC, GCC, let's hope this will work
#endif

#ifdef _MSC_VER
#define LM_ISINF(x) (!_finite(x) && !_isnan(x)) // MSVC
#elif defined(__ICC) || defined(__INTEL_COMPILER) || defined(__GNUC__)
#define LM_ISINF(x) isinf(x) // ICC, GCC
#else
#define LM_ISINF(x) isinf(x) // other than MSVC, ICC, GCC, let's hope this will work
#endif
