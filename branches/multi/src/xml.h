/*
*				xml.h
*
* Include file for xml.c.
*
*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
*
*	This file part of:	SExtractor
*
*	Copyright:		(C) 2006-2010 Emmanuel Bertin -- IAP/CNRS/UPMC
*
*	License:		GNU General Public License
*
*	SExtractor is free software: you can redistribute it and/or modify
*	it under the terms of the GNU General Public License as published by
*	the Free Software Foundation, either version 3 of the License, or
*	(at your option) any later version.
*	SExtractor is distributed in the hope that it will be useful,
*	but WITHOUT ANY WARRANTY; without even the implied warranty of
*	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*	GNU General Public License for more details.
*	You should have received a copy of the GNU General Public License
*	along with SExtractor. If not, see <http://www.gnu.org/licenses/>.
*
*	Last modified:		11/10/2010
*
*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%*/

#ifdef HAVE_CONFIG_H
#include	"config.h"
#endif

#ifndef _FITSCAT_H_
#include "fits/fitscat.h"
#endif

/*----------------------------- Internal constants --------------------------*/
#ifndef XSL_URL
#define	XSL_URL	"."
#endif

/*--------------------------------- typedefs --------------------------------*/
typedef struct
  {
  int		currext;
  int		headflag[2];				/* external headers? */
  int		ndetect;
  int		ntotal;
  char 		ext_date[16],ext_time[16];		/* date and time */
  double        ext_elapsed;				/* processing time */
  char		ident[2][MAXCHAR];			/* identifiants */
  float		backmean[2];				/* mean background */
  float		backsig[2];				/* mean back stddev */
  float		sigfac[2];				/* mean weight scaling*/
  float		thresh[2];				/* thresholds (ADU) */
  double	pixscale[2];				/* pixel scale (deg2) */
  double	epoch[2];				/* epoch of coords */
  double	gain[2];				/* gain (e-/ADU) */
  double	satur_level[2];				/* saturation level */
  }	xmlstruct;

/*------------------------------- functions ---------------------------------*/

extern int		end_xml(void),
			init_xml(int next),
			update_xml(sexcatstruct *sexcat, fieldstruct *dfield, 
				fieldstruct *field, fieldstruct *dwfield,
				fieldstruct *wfield),
			write_xml(char *filename),
			write_xml_header(FILE *file),
			write_xml_meta(FILE *file, char *error);

extern void		write_xmlerror(char *filename, char *error);