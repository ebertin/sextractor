 /*
 				profit.c

*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
*
*	Part of:	SExtractor
*
*	Authors:	E.BERTIN (IAP)
*
*	Contents:	Fit an arbitrary profile combination to a detection.
*
*	Last modify:	09/11/2006
*
*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
*/

#ifdef HAVE_CONFIG_H
#include        "config.h"
#endif

#include	<math.h>
#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>

#include	"define.h"
#include	"globals.h"
#include	"prefs.h"
#include	"fits/fitscat.h"
#include	"check.h"
#include	"image.h"
#include	"profit.h"
#include	"psf.h"

/*------------------------------- variables ---------------------------------*/

/****** prof_init *************************************************************
PROTO	profstruct prof_init(void)
PURPOSE	Allocate and initialize a new profile structure.
INPUT	-.
OUTPUT	A pointer to an allocated prof structure.
AUTHOR	E. Bertin (IAP)
VERSION	09/11/2006
 ***/
profstruct	*prof_init(void)
  {
   profstruct		*profit;

  QMALLOC(prof, profstruct, 1);

  return prof;
  }  


/****** prof_end **************************************************************
PROTO	void prof_end(profstruct *prof)
PURPOSE	End (deallocate) a profile structure.
INPUT	Prof structure.
OUTPUT	-.
AUTHOR	E. Bertin (IAP)
VERSION	09/11/2006
 ***/
void	prof_end(profstruct *profit)
  {
  free(prof);

  return;
  }


/****** prof_residuals ********************************************************
PROTO	double *prof_residuals(profstruct *prof, int nprof, psfstruct *psf,
			picstruct *field, picstruct *wfield, objstruct *obj)
PURPOSE	Compute the vector of residuals between the data and the galaxy
	profile model.
INPUT	Array of profile structures,
	Number of profiles,
	Pointer to a PSF structure,
	Pointer to the field,
	Pointer to the field weight,
	Pointer to the obj.
OUTPUT	Vector of residuals.
AUTHOR	E. Bertin (IAP)
VERSION	13/11/2006
 ***/
double	*prof_residuals(profstruct *prof, int nprof, psfstruct *psf,
		picstruct *field, picstruct *wfield, objstruct *obj)
  {

  prof_

  return;
  }


/****** prof_fit ************************************************************
PROTO	fitstruct *prof_fit(profstruct *prof, int nprof, psfstruct *psf,
		picstruct *field, picstruct *wfield, objstruct *obj)
PURPOSE	Fit profile(s) convolved with the PSF to a detected object.
INPUT	Array of profile structures,
	Number of profiles,
	Pointer to a PSF structure,
	Pointer to the field,
	Pointer to the field weight,
	Pointer to the obj.
OUTPUT	Pointer to an allocated fit structure (containing details about the
	fit).
AUTHOR	E. Bertin (IAP)
VERSION	10/11/2006
 ***/
fitstruct	*prof_fit(profstruct *prof, int nprof, psfstruct *psf,
		picstruct *field, picstruct *wfield, objstruct *obj)
  {

  return;
  }
