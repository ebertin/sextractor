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
*	Last modify:	03/08/2010
*
*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
*/

#ifdef HAVE_CONFIG_H
#include        "config.h"
#endif

#ifndef HAVE_MATHIMF_H
#define _GNU_SOURCE
#endif

#include	<math.h>
#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>

#include	"define.h"
#include	"globals.h"
#include	"prefs.h"
#include	"fits/fitscat.h"
#include	"levmar/lm.h"
#include	"fft.h"
#include	"fitswcs.h"
#include	"check.h"
#include	"pattern.h"
#include	"psf.h"
#include	"profit.h"

#define INTERPW		6	/* Interpolation function range (x) */
#define INTERPH		6	/* Interpolation function range (y) */

#define INTERPF(x)	(x==0.0?1.0:sinf(PI*x)*sinf(PI*x/3.0)/(PI*PI/3.0*x*x))
				/* Lanczos approximation */

static double	prof_gammainc(double x, double a),
		prof_gamma(double x);
static float	prof_interpolate(profstruct *prof, float *posin);
static float	interpolate_pix(float *posin, float *pix, int *naxisn,
		interpenum interptype);

static void	make_kernel(float pos, float *kernel, interpenum interptype);

/*------------------------------- variables ---------------------------------*/

const char	profname[][32]={"background offset", "Sersic spheroid",
		"De Vaucouleurs spheroid", "exponential disk", "spiral arms",
		"bar", "inner ring", "outer ring", "tabulated model",
		""};

const int	interp_kernwidth[5]={1,2,4,6,8};

const int	flux_flag[PARAM_NPARAM] = {0,0,0,
					1,0,0,0,0,
					1,0,0,0,
					1,0,0,0,0,0,0,0,
					1,0,0,
					1,0,0,
					1,0,0
					};

int theniter, the_gal;
/* "Local" global variables; it seems dirty but it simplifies a lot */
/* interfacing to the LM routines */
static picstruct	*the_field, *the_wfield;
profitstruct		*theprofit;

/****** profit_init ***********************************************************
PROTO	profitstruct profit_init(psfstruct *psf)
PURPOSE	Allocate and initialize a new profile-fitting structure.
INPUT	Pointer to PSF structure.
OUTPUT	A pointer to an allocated profit structure.
NOTES	-.
AUTHOR	E. Bertin (IAP)
VERSION	02/07/2010
 ***/
profitstruct	*profit_init(psfstruct *psf)
  {
   profitstruct		*profit;
   int			p, nprof,
			backflag, spheroidflag, diskflag, barflag, armsflag;

  QCALLOC(profit, profitstruct, 1);
  profit->psf = psf;
  profit->psfdft = NULL;

  profit->nparam = 0;
  QMALLOC(profit->prof, profstruct *, PROF_NPROF);
  backflag = spheroidflag = diskflag = barflag = armsflag = 0;
  nprof = 0;
  for (p=0; p<PROF_NPROF; p++)
    if (!backflag && FLAG(obj2.prof_offset_flux))
      {
      profit->prof[p] = prof_init(profit, PROF_BACK);
      backflag = 1;
      nprof++;
      }
    else if (!spheroidflag && FLAG(obj2.prof_spheroid_flux))
      {
      profit->prof[p] = prof_init(profit,
	FLAG(obj2.prof_spheroid_sersicn)? PROF_SERSIC : PROF_DEVAUCOULEURS);
      spheroidflag = 1;
      nprof++;
      }
    else if (!diskflag && FLAG(obj2.prof_disk_flux))
      {
      profit->prof[p] = prof_init(profit, PROF_EXPONENTIAL);
      diskflag = 1;
      nprof++;
      }
    else if (diskflag && !barflag && FLAG(obj2.prof_bar_flux))
      {
      profit->prof[p] = prof_init(profit, PROF_BAR);
      barflag = 1;
      nprof++;
      }
    else if (barflag && !armsflag && FLAG(obj2.prof_arms_flux))
      {
      profit->prof[p] = prof_init(profit, PROF_ARMS);
      armsflag = 1;
      nprof++;
      }

/* Allocate memory for the complete model */
  QMALLOC16(profit->modpix, float, PROFIT_MAXMODSIZE*PROFIT_MAXMODSIZE);
  QMALLOC16(profit->modpix2, float, PROFIT_MAXMODSIZE*PROFIT_MAXMODSIZE);
  QMALLOC16(profit->cmodpix, float, PROFIT_MAXMODSIZE*PROFIT_MAXMODSIZE);
  QMALLOC16(profit->psfpix, float, PROFIT_MAXMODSIZE*PROFIT_MAXMODSIZE);
  QMALLOC16(profit->objpix, PIXTYPE, PROFIT_MAXOBJSIZE*PROFIT_MAXOBJSIZE);
  QMALLOC16(profit->objweight, PIXTYPE, PROFIT_MAXOBJSIZE*PROFIT_MAXOBJSIZE);
  QMALLOC16(profit->lmodpix, PIXTYPE, PROFIT_MAXOBJSIZE*PROFIT_MAXOBJSIZE);
  QMALLOC16(profit->lmodpix2, PIXTYPE, PROFIT_MAXOBJSIZE*PROFIT_MAXOBJSIZE);
  QMALLOC16(profit->resi, float, PROFIT_MAXOBJSIZE*PROFIT_MAXOBJSIZE);
  QMALLOC16(profit->covar, float, profit->nparam*profit->nparam);
  profit->nprof = nprof;
  profit->oversamp = PROFIT_OVERSAMP;
  profit->fluxfac = 1.0;	/* Default */

  return profit;
  }  


/****** profit_end ************************************************************
PROTO	void prof_end(profstruct *prof)
PURPOSE	End (deallocate) a profile-fitting structure.
INPUT	Prof structure.
OUTPUT	-.
NOTES	-.
AUTHOR	E. Bertin (IAP)
VERSION	06/04/2010
 ***/
void	profit_end(profitstruct *profit)
  {
   int	p;

  for (p=0; p<profit->nprof; p++)
    prof_end(profit->prof[p]);
  free(profit->modpix);
  free(profit->modpix2);
  free(profit->cmodpix);
  free(profit->psfpix);
  free(profit->lmodpix);
  free(profit->lmodpix2);
  free(profit->objpix);
  free(profit->objweight);
  free(profit->resi);
  free(profit->prof);
  free(profit->covar);
  free(profit->psfdft);
  free(profit);

  return;
  }


/****** profit_fit ************************************************************
PROTO	void profit_fit(profitstruct *profit, picstruct *field,
		picstruct *wfield, objstruct *obj, obj2struct *obj2)
PURPOSE	Fit profile(s) convolved with the PSF to a detected object.
INPUT	Array of profile structures,
	Number of profiles,
	Pointer to the profile-fitting structure,
	Pointer to the field,
	Pointer to the field weight,
	Pointer to the obj.
OUTPUT	Pointer to an allocated fit structure (containing details about the
	fit).
NOTES	It is a modified version of the lm_minimize() of lmfit.
AUTHOR	E. Bertin (IAP)
VERSION	08/03/2010
 ***/
void	profit_fit(profitstruct *profit,
		picstruct *field, picstruct *wfield,
		objstruct *obj, obj2struct *obj2)
  {
    profitstruct	pprofit;
    profitstruct	hdprofit;
    patternstruct	*pattern;
    psfstruct		*psf;
    checkstruct		*check;
    double		emx2,emy2,emxy, a , cp,sp, cn, bn, n;
    float		param0[PARAM_NPARAM], param1[PARAM_NPARAM],
			param[PARAM_NPARAM],
			**list,
			*cov,
			psf_fwhm, dchi2, err, aspect, chi2;
    int			*index,
			i,j,p, nparam, nparam2, ncomp, nprof;

  nparam = profit->nparam;
  nparam2 = nparam*nparam;
  nprof = profit->nprof;
  if (profit->psfdft)
    {
    QFREE(profit->psfdft);
    }

  psf = profit->psf;
  profit->pixstep = psf->pixstep;
  obj2->prof_flag = 0;

/* Create pixmaps at image resolution */
  profit->ix = (int)(obj->mx + 0.49999);/* internal convention: 1st pix = 0 */
  profit->iy = (int)(obj->my + 0.49999);/* internal convention: 1st pix = 0 */
  psf_fwhm = psf->masksize[0]*psf->pixstep;
  profit->objnaxisn[0] = (((int)((obj->xmax-obj->xmin+1) + psf_fwhm + 0.499)
		*1.2)/2)*2 + 1;
  profit->objnaxisn[1] = (((int)((obj->ymax-obj->ymin+1) + psf_fwhm + 0.499)
		*1.2)/2)*2 + 1;
  if (profit->objnaxisn[1]<profit->objnaxisn[0])
    profit->objnaxisn[1] = profit->objnaxisn[0];
  else
    profit->objnaxisn[0] = profit->objnaxisn[1];
  if (profit->objnaxisn[0]>PROFIT_MAXOBJSIZE)
    {
    profit->subsamp = ceil((float)profit->objnaxisn[0]/PROFIT_MAXOBJSIZE);
    profit->objnaxisn[1] = (profit->objnaxisn[0] /= (int)profit->subsamp);
    obj2->prof_flag |= PROFLAG_OBJSUB;
    }
  else
    profit->subsamp = 1.0;
  profit->nobjpix = profit->objnaxisn[0]*profit->objnaxisn[1];

/* Use (dirty) global variables to interface with lmfit */
  the_field = field;
  the_wfield = wfield;
  theprofit = profit;
  profit->obj = obj;
  profit->obj2 = obj2;

  profit->nresi = profit_copyobjpix(profit, field, wfield);
/* Check if the number of constraints exceeds the number of free parameters */
  if (profit->nresi < nparam)
    {
    if (FLAG(obj2.prof_vector))
      for (p=0; p<nparam; p++)
        obj2->prof_vector[p] = 0.0;
    if (FLAG(obj2.prof_errvector))
      for (p=0; p<nparam; p++)
        obj2->prof_errvector[p] = 0.0;
    if (FLAG(obj2.prof_errmatrix))
      for (p=0; p<nparam2; p++)
        obj2->prof_errmatrix[p] = 0.0;
    obj2->prof_niter = 0;
    obj2->prof_flag |= PROFLAG_NOTCONST;
    return;
    }

/* Create pixmap at PSF resolution */
  profit->modnaxisn[0] =
	((int)(profit->objnaxisn[0]*profit->subsamp/profit->pixstep
		+0.4999)/2+1)*2; 
  profit->modnaxisn[1] =
	((int)(profit->objnaxisn[1]*profit->subsamp/profit->pixstep
		+0.4999)/2+1)*2; 
  if (profit->modnaxisn[1] < profit->modnaxisn[0])
    profit->modnaxisn[1] = profit->modnaxisn[0];
  else
    profit->modnaxisn[0] = profit->modnaxisn[1];
  if (profit->modnaxisn[0]>PROFIT_MAXMODSIZE)
    {
    profit->pixstep = (double)profit->modnaxisn[0] / PROFIT_MAXMODSIZE;
    profit->modnaxisn[0] = profit->modnaxisn[1] = PROFIT_MAXMODSIZE;
    obj2->prof_flag |= PROFLAG_MODSUB;
    }
  profit->nmodpix = profit->modnaxisn[0]*profit->modnaxisn[1];

/* Compute the local PSF */
  profit_psf(profit);

/* Set initial guesses and boundaries */
  profit->sigma = obj->sigbkg;

  profit_resetparams(profit);

/* Actual minimisation */
  fft_reset();
the_gal++;
  for (p=0; p<profit->nparam; p++)
    profit->freeparam_flag[p] = 1;
  profit->nfreeparam = profit->nparam;

  profit->niter = profit_minimize(profit, PROFIT_MAXITER);
/*
  chi2 = profit->chi2;
  for (p=0; p<nparam; p++)
    param1[p] = profit->paraminit[p];
  profit_resetparams(profit);
  for (p=0; p<nparam; p++)
    profit->paraminit[p] = param1[p] + (profit->paraminit[p]<param1[p]?1.0:-1.0)
			* sqrt(profit->covar[p*(nparam+1)]);

  profit->niter = profit_minimize(profit, PROFIT_MAXITER);
  if (chi2<profit->chi2)
    for (p=0; p<nparam; p++)
      profit->paraminit[p] = param1[p];

list = profit->paramlist;
index = profit->paramindex;
for (i=0; i<PARAM_NPARAM; i++)
if (list[i] && i!= PARAM_SPHEROID_ASPECT && i!=PARAM_SPHEROID_POSANG)
profit->freeparam_flag[index[i]] = 0;
profit->nfreeparam = 2;
profit->niter = profit_minimize(profit, PROFIT_MAXITER);
*/
  for (p=0; p<nparam; p++)
    profit->paramerr[p]= sqrt(profit->covar[p*(nparam+1)]);

/* CHECK-Images */
  if ((check = prefs.check[CHECK_PROFILES]))
    {
    profit_residuals(profit,field,wfield, 0.0, profit->paraminit, NULL);
    addcheck(check, profit->lmodpix, profit->objnaxisn[0],profit->objnaxisn[1],
		profit->ix,profit->iy, 1.0);
    }
  if ((check = prefs.check[CHECK_SUBPROFILES]))
    {
    profit_residuals(profit,field,wfield, 0.0, profit->paraminit, NULL);
    addcheck(check, profit->lmodpix, profit->objnaxisn[0],profit->objnaxisn[1],
		profit->ix,profit->iy, -1.0);
    }
  if ((check = prefs.check[CHECK_SPHEROIDS]))
    {
/*-- Set to 0 flux components that do not belong to spheroids */
    for (p=0; p<profit->nparam; p++)
      param[p] = profit->paraminit[p];
    list = profit->paramlist;
    index = profit->paramindex;
    for (i=0; i<PARAM_NPARAM; i++)
      if (list[i] && flux_flag[i] && i!= PARAM_SPHEROID_FLUX)
        param[index[i]] = 0.0;
    profit_residuals(profit,field,wfield, 0.0, param, NULL);
    addcheck(check, profit->lmodpix, profit->objnaxisn[0],profit->objnaxisn[1],
		profit->ix,profit->iy, 1.0);
    }
  if ((check = prefs.check[CHECK_SUBSPHEROIDS]))
    {
/*-- Set to 0 flux components that do not belong to spheroids */
    for (p=0; p<profit->nparam; p++)
      param[p] = profit->paraminit[p];
    list = profit->paramlist;
    index = profit->paramindex;
    for (i=0; i<PARAM_NPARAM; i++)
      if (list[i] && flux_flag[i] && i!= PARAM_SPHEROID_FLUX)
        param[index[i]] = 0.0;
    profit_residuals(profit,field,wfield, 0.0, param, NULL);
    addcheck(check, profit->lmodpix, profit->objnaxisn[0],profit->objnaxisn[1],
		profit->ix,profit->iy, -1.0);
    }
  if ((check = prefs.check[CHECK_DISKS]))
    {
/*-- Set to 0 flux components that do not belong to disks */
    for (p=0; p<profit->nparam; p++)
      param[p] = profit->paraminit[p];
    list = profit->paramlist;
    index = profit->paramindex;
    for (i=0; i<PARAM_NPARAM; i++)
      if (list[i] && flux_flag[i] && i!= PARAM_DISK_FLUX)
        param[index[i]] = 0.0;
    profit_residuals(profit,field,wfield, 0.0, param, NULL);
    addcheck(check, profit->lmodpix, profit->objnaxisn[0],profit->objnaxisn[1],
		profit->ix,profit->iy, 1.0);
    }
  if ((check = prefs.check[CHECK_SUBDISKS]))
    {
/*-- Set to 0 flux components that do not belong to disks */
    for (p=0; p<profit->nparam; p++)
      param[p] = profit->paraminit[p];
    list = profit->paramlist;
    index = profit->paramindex;
    for (i=0; i<PARAM_NPARAM; i++)
      if (list[i] && flux_flag[i] && i!= PARAM_DISK_FLUX)
        param[index[i]] = 0.0;
    profit_residuals(profit,field,wfield, 0.0, param, NULL);
    addcheck(check, profit->lmodpix, profit->objnaxisn[0],profit->objnaxisn[1],
		profit->ix,profit->iy, -1.0);
    }

/* Compute compressed residuals */
  profit_residuals(profit,field,wfield, 10.0, profit->paraminit,profit->resi);

/* Fill measurement parameters */
  if (FLAG(obj2.prof_vector))
    {
    for (p=0; p<nparam; p++)
      obj2->prof_vector[p]= profit->paraminit[p];
    }
  if (FLAG(obj2.prof_errvector))
    {
    for (p=0; p<nparam; p++)
      obj2->prof_errvector[p]= profit->paramerr[p];
    }
  if (FLAG(obj2.prof_errmatrix))
    {
    for (p=0; p<nparam2; p++)
      obj2->prof_errmatrix[p]= profit->covar[p];
    }

  obj2->prof_niter = profit->niter;
  obj2->flux_prof = profit->flux;
  if (FLAG(obj2.fluxerr_prof))
    {
    err = 0.0;
    cov = profit->covar;
    index = profit->paramindex;
    list = profit->paramlist;
    for (i=0; i<PARAM_NPARAM; i++)
      if (flux_flag[i] && list[i])
        {
        cov = profit->covar + nparam*index[i];
        for (j=0; j<PARAM_NPARAM; j++)
          if (flux_flag[j] && list[j])
            err += cov[index[j]];
        }
    obj2->fluxerr_prof = err>0.0? sqrt(err): 0.0;
    }

  obj2->prof_chi2 = (profit->nresi > profit->nparam)?
		profit->chi2 / (profit->nresi - profit->nparam) : 0.0;

  if (FLAG(obj2.x_prof))
    {
    i = profit->paramindex[PARAM_X];
    j = profit->paramindex[PARAM_Y];
/*-- Model coordinates follow the FITS convention (first pixel at 1,1) */
    if (profit->paramlist[PARAM_X])
      {
      obj2->x_prof = (double)profit->ix + *profit->paramlist[PARAM_X] + 1.0;
      obj2->poserrmx2_prof = emx2 = profit->covar[i*(nparam+1)];
      }
    else
      emx2 = 0.0;
    if (profit->paramlist[PARAM_Y])
      {
      obj2->y_prof = (double)profit->iy + *profit->paramlist[PARAM_Y] + 1.0;
      obj2->poserrmy2_prof = emy2 = profit->covar[j*(nparam+1)];
      }
    else
      emy2 = 0.0;
    if (profit->paramlist[PARAM_X] && profit->paramlist[PARAM_Y])
      obj2->poserrmxy_prof = emxy = profit->covar[i+j*nparam];
    else
      emxy = 0.0;

/*-- Error ellipse parameters */
    if (FLAG(obj2.poserra_prof))
      {
       double	pmx2,pmy2,temp,theta;

      if (fabs(temp=emx2-emy2) > 0.0)
        theta = atan2(2.0 * emxy,temp) / 2.0;
      else
        theta = PI/4.0;

      temp = sqrt(0.25*temp*temp+ emxy*emxy);
      pmy2 = pmx2 = 0.5*(emx2+emy2);
      pmx2+=temp;
      pmy2-=temp;

      obj2->poserra_prof = (float)sqrt(pmx2);
      obj2->poserrb_prof = (float)sqrt(pmy2);
      obj2->poserrtheta_prof = (float)(theta/DEG);
      }

    if (FLAG(obj2.poserrcxx_prof))
      {
       double	temp;

      obj2->poserrcxx_prof = (float)(emy2/(temp=emx2*emy2-emxy*emxy));
      obj2->poserrcyy_prof = (float)(emx2/temp);
      obj2->poserrcxy_prof = (float)(-2*emxy/temp);
      }
    }

  if (FLAG(obj2.prof_mx2))
    profit_moments(profit, obj2);

/* Do measurements on the rasterised model (surface brightnesses) */
  if (FLAG(obj2.peak_prof))
    profit_surface(profit, obj2); 

/* Spheroid */
  if (FLAG(obj2.prof_spheroid_flux))
    {
    if ((aspect = *profit->paramlist[PARAM_SPHEROID_ASPECT]) > 1.0)
      {
      *profit->paramlist[PARAM_SPHEROID_REFF] *= aspect;
      profit->paramerr[profit->paramindex[PARAM_SPHEROID_REFF]] *= aspect;
      profit->paramerr[profit->paramindex[PARAM_SPHEROID_ASPECT]]
			/= (aspect*aspect);
      *profit->paramlist[PARAM_SPHEROID_ASPECT] = 1.0 / aspect;
      *profit->paramlist[PARAM_SPHEROID_POSANG] += 90.0;
      }
    obj2->prof_spheroid_flux = *profit->paramlist[PARAM_SPHEROID_FLUX];
    obj2->prof_spheroid_fluxerr =
		profit->paramerr[profit->paramindex[PARAM_SPHEROID_FLUX]];
    obj2->prof_spheroid_reff = *profit->paramlist[PARAM_SPHEROID_REFF];
    obj2->prof_spheroid_refferr = 
		profit->paramerr[profit->paramindex[PARAM_SPHEROID_REFF]];
    obj2->prof_spheroid_aspect = *profit->paramlist[PARAM_SPHEROID_ASPECT];
    obj2->prof_spheroid_aspecterr = 
		profit->paramerr[profit->paramindex[PARAM_SPHEROID_ASPECT]];
    obj2->prof_spheroid_theta =
			fmod_m90_p90(*profit->paramlist[PARAM_SPHEROID_POSANG]);
    obj2->prof_spheroid_thetaerr = 
		profit->paramerr[profit->paramindex[PARAM_SPHEROID_POSANG]];
    if (FLAG(obj2.prof_spheroid_sersicn))
      {
      obj2->prof_spheroid_sersicn = *profit->paramlist[PARAM_SPHEROID_SERSICN];
      obj2->prof_spheroid_sersicnerr = 
		profit->paramerr[profit->paramindex[PARAM_SPHEROID_SERSICN]];
      }
    else
      obj2->prof_spheroid_sersicn = 4.0;
    if (FLAG(obj2.prof_spheroid_peak))
      {
      n = obj2->prof_spheroid_sersicn;
      bn = 2.0*n - 1.0/3.0 + 4.0/(405.0*n) + 46.0/(25515.0*n*n)
		+ 131.0/(1148175*n*n*n);	/* Ciotti & Bertin 1999 */
      cn = n * prof_gamma(2.0*n) * pow(bn, -2.0*n);
      obj2->prof_spheroid_peak = obj2->prof_spheroid_reff>0.0?
	obj2->prof_spheroid_flux * profit->pixstep*profit->pixstep
		/ (2.0 * PI * cn
		* obj2->prof_spheroid_reff*obj2->prof_spheroid_reff
		* obj2->prof_spheroid_aspect)
	: 0.0;
      if (FLAG(obj2.prof_spheroid_fluxeff))
        obj2->prof_spheroid_fluxeff = obj2->prof_spheroid_peak * exp(-bn);
      if (FLAG(obj2.prof_spheroid_fluxmean))
        obj2->prof_spheroid_fluxmean = obj2->prof_spheroid_peak * cn;
      }
    }

/* Disk */
  if (FLAG(obj2.prof_disk_flux))
    {
    if ((aspect = *profit->paramlist[PARAM_DISK_ASPECT]) > 1.0)
      {
      *profit->paramlist[PARAM_DISK_SCALE] *= aspect;
      profit->paramerr[profit->paramindex[PARAM_DISK_SCALE]] *= aspect;
      profit->paramerr[profit->paramindex[PARAM_DISK_ASPECT]]
			/= (aspect*aspect);
      *profit->paramlist[PARAM_DISK_ASPECT] = 1.0 / aspect;
      *profit->paramlist[PARAM_DISK_POSANG] += 90.0;
      }
    obj2->prof_disk_flux = *profit->paramlist[PARAM_DISK_FLUX];
    obj2->prof_disk_fluxerr =
		profit->paramerr[profit->paramindex[PARAM_DISK_FLUX]];
    obj2->prof_disk_scale = *profit->paramlist[PARAM_DISK_SCALE];
    obj2->prof_disk_scaleerr =
		profit->paramerr[profit->paramindex[PARAM_DISK_SCALE]];
    obj2->prof_disk_aspect = *profit->paramlist[PARAM_DISK_ASPECT];
    obj2->prof_disk_aspecterr =
		profit->paramerr[profit->paramindex[PARAM_DISK_ASPECT]];
    obj2->prof_disk_theta = fmod_m90_p90(*profit->paramlist[PARAM_DISK_POSANG]);
    obj2->prof_disk_thetaerr =
		profit->paramerr[profit->paramindex[PARAM_DISK_POSANG]];
    if (FLAG(obj2.prof_disk_inclination))
      {
      obj2->prof_disk_inclination = acos(obj2->prof_disk_aspect) / DEG;
      if (FLAG(obj2.prof_disk_inclinationerr))
        {
        a = sqrt(1.0-obj2->prof_disk_aspect*obj2->prof_disk_aspect);
        obj2->prof_disk_inclinationerr = obj2->prof_disk_aspecterr
					/(a>0.1? a : 0.1)/DEG;
        }
      }

    if (FLAG(obj2.prof_disk_peak))
      {
      obj2->prof_disk_peak = obj2->prof_disk_scale>0.0?
	obj2->prof_disk_flux * profit->pixstep*profit->pixstep
	/ (2.0 * PI * obj2->prof_disk_scale*obj2->prof_disk_scale
		* obj2->prof_disk_aspect)
	: 0.0;
      if (FLAG(obj2.prof_disk_fluxeff))
        obj2->prof_disk_fluxeff = obj2->prof_disk_peak * 0.186682; /* e^-(b_n)*/
      if (FLAG(obj2.prof_disk_fluxmean))
        obj2->prof_disk_fluxmean = obj2->prof_disk_peak * 0.355007;/* b_n^(-2)*/
      }

/* Disk pattern */
    if (prefs.pattern_flag)
      {
      profit_residuals(profit,field,wfield, PROFIT_DYNPARAM,
			profit->paraminit,profit->resi);
      pattern = pattern_init(profit, prefs.pattern_type,
		prefs.prof_disk_patternncomp);
      pattern_fit(pattern, profit);
      if (FLAG(obj2.prof_disk_patternspiral))
        obj2->prof_disk_patternspiral = pattern_spiral(pattern);
      if (FLAG(obj2.prof_disk_patternvector))
        {
        ncomp = pattern->size[2];
        for (p=0; p<ncomp; p++)
          obj2->prof_disk_patternvector[p] = (float)pattern->coeff[p];
        }
      if (FLAG(obj2.prof_disk_patternmodvector))
        {
        ncomp = pattern->ncomp*pattern->nfreq;
        for (p=0; p<ncomp; p++)
          obj2->prof_disk_patternmodvector[p] = (float)pattern->mcoeff[p];
        }
      if (FLAG(obj2.prof_disk_patternargvector))
        {
        ncomp = pattern->ncomp*pattern->nfreq;
        for (p=0; p<ncomp; p++)
          obj2->prof_disk_patternargvector[p] = (float)pattern->acoeff[p];
        }
      pattern_end(pattern);
      }

/* Bar */
    if (FLAG(obj2.prof_bar_flux))
      {
      obj2->prof_bar_flux = *profit->paramlist[PARAM_BAR_FLUX];
      obj2->prof_bar_fluxerr =
		profit->paramerr[profit->paramindex[PARAM_BAR_FLUX]];
      obj2->prof_bar_length = *profit->paramlist[PARAM_ARMS_START]
				**profit->paramlist[PARAM_DISK_SCALE];
      obj2->prof_bar_lengtherr = *profit->paramlist[PARAM_ARMS_START]
		  * profit->paramerr[profit->paramindex[PARAM_DISK_SCALE]]
		+ *profit->paramlist[PARAM_DISK_SCALE]
		  * profit->paramerr[profit->paramindex[PARAM_ARMS_START]];
      obj2->prof_bar_aspect = *profit->paramlist[PARAM_BAR_ASPECT];
      obj2->prof_bar_aspecterr =
		profit->paramerr[profit->paramindex[PARAM_BAR_ASPECT]];
      obj2->prof_bar_posang = 
			fmod_m90_p90(*profit->paramlist[PARAM_ARMS_POSANG]);
      obj2->prof_bar_posangerr =
		profit->paramerr[profit->paramindex[PARAM_ARMS_POSANG]];
      if (FLAG(obj2.prof_bar_theta))
        {
        cp = cos(obj2->prof_bar_posang*DEG);
        sp = sin(obj2->prof_bar_posang*DEG);
        a = obj2->prof_disk_aspect;
        obj2->prof_bar_theta = fmod_m90_p90(atan2(a*sp,cp)/DEG
				+ obj2->prof_disk_theta);
        obj2->prof_bar_thetaerr = obj2->prof_bar_posangerr*a/(cp*cp+a*a*sp*sp);
        }

/* Arms */
      if (FLAG(obj2.prof_arms_flux))
        {
        obj2->prof_arms_flux = *profit->paramlist[PARAM_ARMS_FLUX];
        obj2->prof_arms_fluxerr =
		profit->paramerr[profit->paramindex[PARAM_ARMS_FLUX]];
        obj2->prof_arms_pitch =
		fmod_m90_p90(*profit->paramlist[PARAM_ARMS_PITCH]);
        obj2->prof_arms_pitcherr =
		profit->paramerr[profit->paramindex[PARAM_ARMS_PITCH]];
        obj2->prof_arms_start = *profit->paramlist[PARAM_ARMS_START]
				**profit->paramlist[PARAM_DISK_SCALE];
        obj2->prof_arms_starterr = *profit->paramlist[PARAM_ARMS_START]
		  * profit->paramerr[profit->paramindex[PARAM_DISK_SCALE]]
		+ *profit->paramlist[PARAM_DISK_SCALE]
		  * profit->paramerr[profit->paramindex[PARAM_ARMS_START]];
        obj2->prof_arms_quadfrac = *profit->paramlist[PARAM_ARMS_QUADFRAC];
        obj2->prof_arms_quadfracerr =
		profit->paramerr[profit->paramindex[PARAM_ARMS_QUADFRAC]];
        obj2->prof_arms_posang =
			fmod_m90_p90(*profit->paramlist[PARAM_ARMS_POSANG]);
        obj2->prof_arms_posangerr =
		profit->paramerr[profit->paramindex[PARAM_ARMS_POSANG]];
        }
      }
    }

/* Star/galaxy classification */
  if (FLAG(obj2.prof_class_star) || FLAG(obj2.prof_concentration))
    {
    pprofit = *profit;
    memset(pprofit.paramindex, 0, PARAM_NPARAM*sizeof(int));
    memset(pprofit.paramlist, 0, PARAM_NPARAM*sizeof(float *));
    pprofit.nparam = 0;
    QMALLOC(pprofit.prof, profstruct *, 1);
    pprofit.prof[0] = prof_init(&pprofit, PROF_DIRAC);
    QMALLOC16(pprofit.covar, float, pprofit.nparam*pprofit.nparam);
    pprofit.nprof = 1;
    profit_resetparams(&pprofit);
    if (profit->paramlist[PARAM_X] && profit->paramlist[PARAM_Y])
      {
      pprofit.paraminit[pprofit.paramindex[PARAM_X]] = *profit->paramlist[PARAM_X];
      pprofit.paraminit[pprofit.paramindex[PARAM_Y]] = *profit->paramlist[PARAM_Y];
      }
    pprofit.paraminit[pprofit.paramindex[PARAM_DISK_FLUX]] = profit->flux;
    for (p=0; p<pprofit.nparam; p++)
      pprofit.freeparam_flag[p] = 1;
    pprofit.nfreeparam = pprofit.nparam;
    pprofit.niter = profit_minimize(&pprofit, PROFIT_MAXITER);
    profit_residuals(&pprofit,field,wfield, 10.0, pprofit.paraminit,
			pprofit.resi);
    if (FLAG(obj2.prof_class_star))
      {
      dchi2 = 0.5*(pprofit.chi2 - profit->chi2);
      obj2->prof_class_star = dchi2 < 50.0?
	(dchi2 > -50.0? 2.0/(1.0+expf(dchi2)) : 2.0) : 0.0;
      }
    if (FLAG(obj2.prof_concentration))
      {
      if (profit->flux > 0.0 && pprofit.flux > 0.0)
        obj2->prof_concentration = -2.5*log10(pprofit.flux / profit->flux);
      else  if (profit->flux > 0.0)
        obj2->prof_concentration = 99.0;
      else  if (pprofit.flux > 0.0)
        obj2->prof_concentration = -99.0;
      if (FLAG(obj2.prof_concentrationerr))
        obj2->prof_concentrationerr = (obj2->flux_prof > 0.0?
		1.086*(obj2->fluxerr_prof / obj2->flux_prof) : 99.0);
      }
    prof_end(pprofit.prof[0]);
    free(pprofit.prof);
    free(pprofit.covar);
    }

/* clean up. */
  fft_reset();

  return;
  }


/****i* prof_gammainc *********************************************************
PROTO	double prof_gammainc(double x, double a)
PURPOSE	Returns the incomplete Gamma function (from Num. Recipes in C, p.216).
INPUT	A double,
	upper integration limit.
OUTPUT	Incomplete Gamma function.
NOTES	-.
AUTHOR	E. Bertin (IAP)
VERSION	18/09/009
*/
static double	prof_gammainc (double x, double a)

  {
   double	b,c,d,h, xn,xp, del,sum;
   int		i;

  if (a < 0.0 || x <= 0.0)
    return 0.0;

  if (a < (x+1.0))
    {
/*-- Use the series representation */
    xp = x;
    del = sum = 1.0/x;
    for (i=100;i--;)	/* Iterate to convergence */
      {
      sum += (del *= a/(++xp));
      if (fabs(del) < fabs(sum)*3e-7)
        return sum*exp(-a+x*log(a)) / prof_gamma(x);
      }
    }
  else
    {
/*-- Use the continued fraction representation and take its complement */
    b = a + 1.0 - x;
    c = 1e30;
    h = d = 1.0/b;
    for (i=1; i<=100; i++)	/* Iterate to convergence */
      {
      xn = -i*(i-x);
      b += 2.0;
      if (fabs(d=xn*d+b) < 1e-30)
        d = 1e-30;
      if (fabs(c=b+xn/c) < 1e-30)
        c = 1e-30;
      del= c * (d = 1.0/d);
      h *= del;
      if (fabs(del-1.0) < 3e-7)
        return 1.0 - exp(-a+x*log(a))*h / prof_gamma(x);
      }
    }
  error(EXIT_FAILURE, "*Error*: out of bounds in ",
		"prof_gammainc()");
  return 0.0;
  }


/****i* prof_gamma ************************************************************
PROTO	double prof_gamma(double xx)
PURPOSE	Returns the Gamma function (from Num. Recipes in C, p.213).
INPUT	A double.
OUTPUT	Gamma function.
NOTES	-.
AUTHOR	E. Bertin (IAP)
VERSION	11/09/009
*/
static double	prof_gamma(double xx)

  {
   double		x,tmp,ser;
   static double	cof[6]={76.18009173,-86.50532033,24.01409822,
			-1.231739516,0.120858003e-2,-0.536382e-5};
   int			j;

  tmp=(x=xx-1.0)+5.5;
  tmp -= (x+0.5)*log(tmp);
  ser=1.0;
  for (j=0;j<6;j++)
    ser += cof[j]/(x+=1.0);

  return 2.50662827465*ser*exp(-tmp);
  }


/****** profit_minradius ******************************************************
PROTO	float profit_minradius(profitstruct *profit, float refffac)
PURPOSE	Returns the minimum disk radius that guarantees that each and
	every model component fits within some margin in that disk.
INPUT	Profit structure pointer,
	margin in units of (r/r_eff)^(1/n)).
OUTPUT	Radius (in pixels).
NOTES	-.
AUTHOR	E. Bertin (IAP)
VERSION	21/09/009
*/
float	profit_minradius(profitstruct *profit, float refffac)

  {
   double	r,reff,rmax;
   int		p;

  rmax = reff = 0.0;
  for (p=0; p<profit->nprof; p++)
    {
    switch (profit->prof[p]->code)
      {
      case PROF_SERSIC:
        reff = *profit->paramlist[PARAM_SPHEROID_REFF];
      break;
      case PROF_DEVAUCOULEURS:
        reff = *profit->paramlist[PARAM_SPHEROID_REFF];
       break;
      case PROF_EXPONENTIAL:
        reff = *profit->paramlist[PARAM_DISK_SCALE]*1.67835;
      break;
      default:
        error(EXIT_FAILURE, "*Internal Error*: Unknown profile parameter in ",
		"profit_minradius()");
      break;
      }
    r = reff*(double)refffac;
    if (r>rmax)
      rmax = r;
    }

  return (float)rmax;
  }


/****** profit_psf ************************************************************
PROTO	void	profit_psf(profitstruct *profit)
PURPOSE	Build the local PSF at a given resolution.
INPUT	Profile-fitting structure.
OUTPUT	-.
NOTES	-.
AUTHOR	E. Bertin (IAP)
VERSION	07/07/2010
 ***/
void	profit_psf(profitstruct *profit)
  {
   double	flux;
   float	posin[2], posout[2], dnaxisn[2],
		*pixout,
		xcout,ycout, xcin,ycin, invpixstep, norm;
   int		d,i;

  psf = profit->psf;
  psf_build(psf);

  xcout = (float)(profit->modnaxisn[0]/2) + 1.0;	/* FITS convention */
  ycout = (float)(profit->modnaxisn[1]/2) + 1.0;	/* FITS convention */
  xcin = (psf->masksize[0]/2) + 1.0;			/* FITS convention */
  ycin = (psf->masksize[1]/2) + 1.0;			/* FITS convention */
  invpixstep = profit->pixstep / psf->pixstep;

/* Initialize multi-dimensional counters */
  for (d=0; d<2; d++)
    {
    posout[d] = 1.0;					/* FITS convention */
    dnaxisn[d] = profit->modnaxisn[d]+0.5;
    }

/* Remap each pixel */
  pixout = profit->psfpix;
  flux = 0.0;
  for (i=profit->modnaxisn[0]*profit->modnaxisn[1]; i--;)
    {
    posin[0] = (posout[0] - xcout)*invpixstep + xcin;
    posin[1] = (posout[1] - ycout)*invpixstep + ycin;
    flux += ((*(pixout++) = interpolate_pix(posin, psf->maskloc,
		psf->masksize, INTERP_LANCZOS3)));
    for (d=0; d<2; d++)
      if ((posout[d]+=1.0) < dnaxisn[d])
        break;
      else
        posout[d] = 1.0;
    }

/* Normalize PSF flux (just in case...) */
  flux *= profit->pixstep*profit->pixstep;
  if (fabs(flux) <= 0.0)
    error(EXIT_FAILURE, "*Error*: PSF model is empty or negative: ", psf->name);

  norm = 1.0/flux;
  pixout = profit->psfpix;
  for (i=profit->modnaxisn[0]*profit->modnaxisn[1]; i--;)
    *(pixout++) *= norm;  

  return;
  }


/****** profit_minimize *******************************************************
PROTO	void profit_minimize(profitstruct *profit)
PURPOSE	Provide a function returning residuals to lmfit.
INPUT	Pointer to the profit structure involved in the fit,
	maximum number of iterations.
OUTPUT	Number of iterations used.
NOTES	-.
AUTHOR	E. Bertin (IAP)
VERSION	02/04/2010
 ***/
int	profit_minimize(profitstruct *profit, int niter)
  {
   double		lm_opts[5], info[LM_INFO_SZ];
   double		dcovar[PARAM_NPARAM*PARAM_NPARAM],
			dparam[PARAM_NPARAM];

  profit->iter = 0;
  memset(dcovar, 0, profit->nparam*profit->nparam*sizeof(double));

/* Perform fit */
  lm_opts[0] = 1.0e-3;
  lm_opts[1] = 1.0e-12;
  lm_opts[2] = 1.0e-12;
  lm_opts[3] = 1.0e-12;
  lm_opts[4] = 1.0e-3;

  profit_boundtounbound(profit, profit->paraminit, dparam, PARAM_ALLPARAMS);

  niter = dlevmar_dif(profit_evaluate, dparam, NULL, profit->nfreeparam,
		profit->nresi, niter, lm_opts, info, NULL, dcovar, profit);

  profit_unboundtobound(profit, dparam, profit->paraminit, PARAM_ALLPARAMS);

/* Convert covariance matrix to bounded space */
  profit_covarunboundtobound(profit, dcovar, profit->covar);

  return niter;
  }


/****** profit_printout *******************************************************
PROTO	void profit_printout(int n_par, float* par, int m_dat, float* fvec,
		void *data, int iflag, int iter, int nfev )
PURPOSE	Provide a function to print out results to lmfit.
INPUT	Number of fitted parameters,
	pointer to the vector of parameters,
	number of data points,
	pointer to the vector of residuals (output),
	pointer to the data structure (unused),
	0 (init) 1 (outer loop) 2(inner loop) -1(terminated),
	outer loop counter,
	number of calls to evaluate().
OUTPUT	-.
NOTES	Input arguments are there only for compatibility purposes (unused)
AUTHOR	E. Bertin (IAP)
VERSION	17/09/2008
 ***/
void	profit_printout(int n_par, float* par, int m_dat, float* fvec,
		void *data, int iflag, int iter, int nfev )
  {
   checkstruct	*check;
   profitstruct	*profit;
   char		filename[256];
   static int	itero;

  profit = (profitstruct *)data;

  if (0 && (iter!=itero || iter<0))
    {
    if (iter<0)
      itero++;
    else
      itero = iter;
    sprintf(filename, "check_%d_%04d.fits", the_gal, itero);
    check=initcheck(filename, CHECK_PROFILES, 0);
    reinitcheck(the_field, check);
    addcheck(check, profit->lmodpix, profit->objnaxisn[0],profit->objnaxisn[1],
		profit->ix,profit->iy, 1.0);

    reendcheck(the_field, check);
    endcheck(check);
    }

  return;
  }


/****** profit_evaluate ******************************************************
PROTO	void profit_evaluate(double *par, double *fvec, int m, int n,
			void *adata)
PURPOSE	Provide a function returning residuals to levmar.
INPUT	Pointer to the vector of parameters,
	pointer to the vector of residuals (output),
	number of model parameters,
	number of data points,
	pointer to a data structure (we use it for the profit structure here).
OUTPUT	-.
NOTES	-.
AUTHOR	E. Bertin (IAP)
VERSION	08/07/2010
 ***/
void	profit_evaluate(double *dpar, double *fvec, int m, int n, void *adata)
  {
   profitstruct		*profit;
   profstruct		**prof;
   double		*dpar0, *dresi;
   float		*modpixt, *profpixt, *resi,
			tparam, val;
   PIXTYPE		*lmodpixt,*lmodpix2t, *objpix,*weight,
			wval;
   int			c,f,i,p,q, fd,pd, jflag,sflag, nprof;

  profit = (profitstruct *)adata;

/* Detect "Jacobian-related" calls */
  jflag = pd = fd = 0;
  dpar0 = profit->dparam;
  if (profit->iter)
    {
    f = q = 0;
    for (p=0; p<profit->nparam; p++)
      {
      if (dpar[f] - dpar0[f] != 0.0)
        {
        pd = p;
        fd = f;
        q++;
        }
      if (profit->freeparam_flag[p])
        f++;
      }
    if (f>0 && q==1)
      jflag = 1;
    }

  if (jflag)
    {
    prof = profit->prof;
    nprof = profit->nprof;

/*-- "Jacobian call" */
    tparam = profit->param[pd];
    profit_unboundtobound(profit, &dpar[fd], &profit->param[pd], pd);
    sflag = 1;
    switch(profit->paramrevindex[pd])
      {
      case PARAM_X:
      case PARAM_Y:
        profit_resample(profit, profit->cmodpix, profit->lmodpix2, 1.0);
        lmodpixt = profit->lmodpix;
        lmodpix2t = profit->lmodpix2;
        for (i=profit->nobjpix;i--;)
          *(lmodpix2t++) -= *(lmodpixt++);
        break;
      case PARAM_SPHEROID_FLUX:
      case PARAM_DISK_FLUX:
      case PARAM_ARMS_FLUX:
      case PARAM_BAR_FLUX:
        if (nprof==1 && tparam != 0.0)
          {
          lmodpixt = profit->lmodpix;
          lmodpix2t = profit->lmodpix2;
          val = (profit->param[pd] - tparam) / tparam;
          for (i=profit->nobjpix;i--;)
            *(lmodpix2t++) = val**(lmodpixt++);
          }
        else
          {
          for (c=0; c<nprof; c++)
            if (prof[c]->flux == &profit->param[pd])
              break;
          memcpy(profit->modpix, prof[c]->pix, profit->nmodpix*sizeof(float));
          profit_convolve(profit, profit->modpix);
          profit_resample(profit, profit->modpix, profit->lmodpix2,
		profit->param[pd] - tparam);
          }
        break;
      case PARAM_SPHEROID_REFF:
      case PARAM_SPHEROID_ASPECT:
      case PARAM_SPHEROID_POSANG:
      case PARAM_SPHEROID_SERSICN:
        sflag = 0;			/* We are in the same switch */
        for (c=0; c<nprof; c++)
          if (prof[c]->code == PROF_SERSIC
		|| prof[c]->code == PROF_DEVAUCOULEURS)
            break; 
      case PARAM_DISK_SCALE:
      case PARAM_DISK_ASPECT:
      case PARAM_DISK_POSANG:
        if (sflag)
          for (c=0; c<nprof; c++)
            if (prof[c]->code == PROF_EXPONENTIAL)
              break; 
        sflag = 0;
      case PARAM_ARMS_QUADFRAC:
      case PARAM_ARMS_SCALE:
      case PARAM_ARMS_START:
      case PARAM_ARMS_POSANG:
      case PARAM_ARMS_PITCH:
      case PARAM_ARMS_PITCHVAR:
      case PARAM_ARMS_WIDTH:
        if (sflag)
          for (c=0; c<nprof; c++)
            if (prof[c]->code == PROF_ARMS)
              break; 
        sflag = 0;
      case PARAM_BAR_ASPECT:
      case PARAM_BAR_POSANG:
        if (sflag)
          for (c=0; c<nprof; c++)
            if (prof[c]->code == PROF_ARMS)
              break; 
        modpixt = profit->modpix;
        profpixt = prof[c]->pix;
        val = -*prof[c]->flux;
        for (i=profit->nmodpix;i--;)
          *(modpixt++) = val**(profpixt++);
        memcpy(profit->modpix2, prof[c]->pix, profit->nmodpix*sizeof(float));
        prof_add(profit, prof[c], 0);
        memcpy(prof[c]->pix, profit->modpix2, profit->nmodpix*sizeof(float));
        profit_convolve(profit, profit->modpix);
        profit_resample(profit, profit->modpix, profit->lmodpix2, 1.0);
        break;
      default:
        error(EXIT_FAILURE, "*Internal Error*: ",
			"unknown parameter index in profit_jacobian()");
        break;
      }
    objpix = profit->objpix;
    weight = profit->objweight;
    lmodpixt = profit->lmodpix;
    lmodpix2t = profit->lmodpix2;
    resi = profit->resi;
    dresi = fvec;
    if (PROFIT_DYNPARAM > 0.0)
      for (i=profit->nobjpix;i--; lmodpixt++, lmodpix2t++)
        {
        val = *(objpix++);
        if ((wval=*(weight++))>0.0)
          *(dresi++) = *(resi++) + *lmodpix2t
		* wval/(1.0+wval*fabs(*lmodpixt - val)/PROFIT_DYNPARAM);
        }
    else
      for (i=profit->nobjpix;i--; lmodpix2t++)
        if ((wval=*(weight++))>0.0)
          *(dresi++) = *(resi++) + *lmodpix2t * wval;
    }
  else
    {
/*-- "Regular call" */
    for (p=0; p<profit->nparam; p++)
      dpar0[p] = dpar[p];
    profit_unboundtobound(profit, dpar, profit->param, PARAM_ALLPARAMS);

    profit_residuals(profit, the_field, the_wfield, PROFIT_DYNPARAM,
	profit->param, profit->resi);

    for (p=0; p<profit->nresi; p++)
      fvec[p] = profit->resi[p];
    }

//  profit_printout(m, par, n, fvec, adata, 0, -1, 0 );
  profit->iter++;

  return;
  }


/****** profit_residuals ******************************************************
PROTO	float *prof_residuals(profitstruct *profit, picstruct *field,
		picstruct *wfield, float dynparam, float *param, float *resi)
PURPOSE	Compute the vector of residuals between the data and the galaxy
	profile model.
INPUT	Profile-fitting structure,
	pointer to the field,
	pointer to the field weight,
	dynamic compression parameter (0=no compression),
	pointer to the model parameters (output),
	pointer to the computed residuals (output).
OUTPUT	Vector of residuals.
NOTES	-.
AUTHOR	E. Bertin (IAP)
VERSION	08/07/2010
 ***/
float	*profit_residuals(profitstruct *profit, picstruct *field,
		picstruct *wfield, float dynparam, float *param, float *resi)
  {
   int		p, nmodpix;

  nmodpix = profit->modnaxisn[0]*profit->modnaxisn[1]*sizeof(float);
  memset(profit->modpix, 0, nmodpix);
  for (p=0; p<profit->nparam; p++)
    profit->param[p] = param[p];
/* Simple PSF shortcut */
  if (profit->nprof == 1 && profit->prof[0]->code == PROF_DIRAC)
    {
    profit_resample(profit, profit->psfpix, profit->lmodpix,
		*profit->prof[0]->flux);
    profit->flux = *profit->prof[0]->flux;
    }
  else
    {
    profit->flux = 0.0;
    for (p=0; p<profit->nprof; p++)
      profit->flux += prof_add(profit, profit->prof[p], 0);
    memcpy(profit->cmodpix, profit->modpix, profit->nmodpix*sizeof(float));
    profit_convolve(profit, profit->cmodpix);
    profit_resample(profit, profit->cmodpix, profit->lmodpix, 1.0);
    }

  if (resi)
    profit_compresi(profit, dynparam, resi);

  return resi;
  }


/****** profit_compresi ******************************************************
PROTO	float *prof_compresi(profitstruct *profit, float dynparam,
			float *resi)
PURPOSE	Compute the vector of residuals between the data and the galaxy
	profile model.
INPUT	Profile-fitting structure,
	dynamic-compression parameter (0=no compression),
	vector of residuals (output).
OUTPUT	Vector of residuals.
NOTES	-.
AUTHOR	E. Bertin (IAP)
VERSION	07/09/2009
 ***/
float	*profit_compresi(profitstruct *profit, float dynparam, float *resi)
  {
   double	error;
   float	*resit;
   PIXTYPE	*objpix, *objweight, *lmodpix,
		val,val2,wval, invsig;
   int		npix, i;
  
/* Compute vector of residuals */
  resit = resi;
  objpix = profit->objpix;
  objweight = profit->objweight;
  lmodpix = profit->lmodpix;
  error = 0.0;
  npix = profit->objnaxisn[0]*profit->objnaxisn[1];
  if (dynparam > 0.0)
    {
    invsig = (PIXTYPE)(1.0/dynparam);
    for (i=npix; i--; lmodpix++)
      {
      val = *(objpix++);
      if ((wval=*(objweight++))>0.0)
        {
        val2 = (*lmodpix - val)*wval*invsig;
        val2 = val2>0.0? logf(1.0+val2) : -logf(1.0-val2);
        *(resit++) = val2*dynparam;
        error += val2*val2;
        }
      }
    profit->chi2 = dynparam*dynparam*error;
    }
  else
    {
    for (i=npix; i--; lmodpix++)
      {
      val = *(objpix++);
      if ((wval=*(objweight++))>0.0)
        {
        val2 = (*lmodpix - val)*wval;
        *(resit++) = val2;
        error += val2*val2;
        }
      }
    profit->chi2 = error;
    }

  return resi;
  }


/****** profit_resample ******************************************************
PROTO	int	prof_resample(profitstruct *profit, float *inpix,
		PIXTYPE *outpix, float factor)
PURPOSE	Resample the current full resolution model to image resolution.
INPUT	Profile-fitting structure,
	pointer to input raster,
	pointer to output raster,
	multiplicating factor.
OUTPUT	RETURN_ERROR if the rasters don't overlap, RETURN_OK otherwise.
NOTES	-.
AUTHOR	E. Bertin (IAP)
VERSION	01/05/2010
 ***/
int	profit_resample(profitstruct *profit, float *inpix, PIXTYPE *outpix,
	float factor)
  {
   PIXTYPE	*pixout,*pixout0;
   float	*pixin,*pixin0, *mask,*maskt, *pixinout, *dpixin,*dpixin0,
		*dpixout,*dpixout0, *dx,*dy,
		xcin,xcout,ycin,ycout, xsin,ysin, xin,yin, x,y, dxm,dym, val,
		invpixstep;
   int		*start,*startt, *nmask,*nmaskt,
		i,j,k,n,t, 
		ixsout,iysout, ixout,iyout, dixout,diyout, nxout,nyout,
		iysina, nyin, hmw,hmh, ix,iy, ixin,iyin;

  invpixstep = profit->subsamp/profit->pixstep;

  xcin = (profit->modnaxisn[0]/2);
  xcout = (float)(profit->objnaxisn[0]/2);
  if ((dx=(profit->paramlist[PARAM_X])))
    xcout += *dx;

  xsin = xcin - xcout*invpixstep;			/* Input start x-coord*/
  if ((int)xsin >= profit->modnaxisn[0])
    return RETURN_ERROR;
  ixsout = 0;				/* Int. part of output start x-coord */
  if (xsin<0.0)
    {
    dixout = (int)(1.0-xsin/invpixstep);
/*-- Simply leave here if the images do not overlap in x */
    if (dixout >= profit->objnaxisn[0])
      return RETURN_ERROR;
    ixsout += dixout;
    xsin += dixout*invpixstep;
    }
  nxout = (int)((profit->modnaxisn[0]-xsin)/invpixstep);/* nb of interpolated
							input pixels along x */
  if (nxout>(ixout=profit->objnaxisn[0]-ixsout))
    nxout = ixout;
  if (!nxout)
    return RETURN_ERROR;

  ycin = (profit->modnaxisn[1]/2);
  ycout = (float)(profit->objnaxisn[1]/2);
  if ((dy=(profit->paramlist[PARAM_Y])))
    ycout += *dy;

  ysin = ycin - ycout*invpixstep;		/* Input start y-coord*/
  if ((int)ysin >= profit->modnaxisn[1])
    return RETURN_ERROR;
  iysout = 0;				/* Int. part of output start y-coord */
  if (ysin<0.0)
    {
    diyout = (int)(1.0-ysin/invpixstep);
/*-- Simply leave here if the images do not overlap in y */
    if (diyout >= profit->objnaxisn[1])
      return RETURN_ERROR;
    iysout += diyout;
    ysin += diyout*invpixstep;
    }
  nyout = (int)((profit->modnaxisn[1]-ysin)/invpixstep);/* nb of interpolated
							input pixels along y */
  if (nyout>(iyout=profit->objnaxisn[1]-iysout))
    nyout = iyout;
  if (!nyout)
    return RETURN_ERROR;

/* Set the yrange for the x-resampling with some margin for interpolation */
  iysina = (int)ysin;	/* Int. part of Input start y-coord with margin */
  hmh = INTERPH/2 - 1;	/* Interpolant start */
  if (iysina<0 || ((iysina -= hmh)< 0))
    iysina = 0;
  nyin = (int)(ysin+nyout*invpixstep)+INTERPH-hmh;/* Interpolated Input y size*/
  if (nyin>profit->modnaxisn[1])						/* with margin */
    nyin = profit->modnaxisn[1];
/* Express everything relative to the effective Input start (with margin) */
  nyin -= iysina;
  ysin -= (float)iysina;

/* Allocate interpolant stuff for the x direction */
  QMALLOC(mask, float, nxout*INTERPW);	/* Interpolation masks */
  QMALLOC(nmask, int, nxout);		/* Interpolation mask sizes */
  QMALLOC(start, int, nxout);		/* Int. part of Input conv starts */
/* Compute the local interpolant and data starting points in x */
  hmw = INTERPW/2 - 1;
  xin = xsin;
  maskt = mask;
  nmaskt = nmask;
  startt = start;
  for (j=nxout; j--; xin+=invpixstep)
    {
    ix = (ixin=(int)xin) - hmw;
    dxm = ixin - xin - hmw;	/* starting point in the interpolation func */
    if (ix < 0)
      {
      n = INTERPW+ix;
      dxm -= (float)ix;
      ix = 0;
      }
    else
      n = INTERPW;
    if (n>(t=profit->modnaxisn[0]-ix))
      n=t;
    *(startt++) = ix;
    *(nmaskt++) = n;
    for (x=dxm, i=n; i--; x+=1.0)
      *(maskt++) = INTERPF(x);
    }

  QCALLOC(pixinout, float, nxout*nyin);	/* Intermediary frame-buffer */

/* Make the interpolation in x (this includes transposition) */
  pixin0 = inpix + iysina*profit->modnaxisn[0];
  dpixout0 = pixinout;
  for (k=nyin; k--; pixin0+=profit->modnaxisn[0], dpixout0++)
    {
    maskt = mask;
    nmaskt = nmask;
    startt = start;
    dpixout = dpixout0;
    for (j=nxout; j--; dpixout+=nyin)
      {
      pixin = pixin0+*(startt++);
      val = 0.0; 
      for (i=*(nmaskt++); i--;)
        val += *(maskt++)**(pixin++);
      *dpixout = val;
      }
    }

/* Reallocate interpolant stuff for the y direction */
  QREALLOC(mask, float, nyout*INTERPH);	/* Interpolation masks */
  QREALLOC(nmask, int, nyout);			/* Interpolation mask sizes */
  QREALLOC(start, int, nyout);		/* Int. part of Input conv starts */

/* Compute the local interpolant and data starting points in y */
  hmh = INTERPH/2 - 1;
  yin = ysin;
  maskt = mask;
  nmaskt = nmask;
  startt = start;
  for (j=nyout; j--; yin+=invpixstep)
    {
    iy = (iyin=(int)yin) - hmh;
    dym = iyin - yin - hmh;	/* starting point in the interpolation func */
    if (iy < 0)
      {
      n = INTERPH+iy;
      dym -= (float)iy;
      iy = 0;
      }
    else
      n = INTERPH;
    if (n>(t=nyin-iy))
      n=t;
    *(startt++) = iy;
    *(nmaskt++) = n;
    for (y=dym, i=n; i--; y+=1.0)
      *(maskt++) = INTERPF(y);
    }

/* Initialize destination buffer to zero */
  memset(outpix, 0, (size_t)profit->nobjpix*sizeof(PIXTYPE));

/* Make the interpolation in y and transpose once again */
  dpixin0 = pixinout;
  pixout0 = outpix+ixsout+iysout*profit->objnaxisn[0];
  for (k=nxout; k--; dpixin0+=nyin, pixout0++)
    {
    maskt = mask;
    nmaskt = nmask;
    startt = start;
    pixout = pixout0;
    for (j=nyout; j--; pixout+=profit->objnaxisn[0])
      {
      dpixin = dpixin0+*(startt++);
      val = 0.0; 
      for (i=*(nmaskt++); i--;)
        val += *(maskt++)**(dpixin++);
       *pixout = (PIXTYPE)(factor*val);
      }
    }

/* Free memory */
  free(pixinout);
  free(mask);
  free(nmask);
  free(start);

  return RETURN_OK;
  }


/****** profit_convolve *******************************************************
PROTO	void profit_convolve(profitstruct *profit, float *modpix)
PURPOSE	Convolve a model image with the local PSF.
INPUT	Pointer to the profit structure,
	Pointer to the image raster.
OUTPUT	-.
NOTES	-.
AUTHOR	E. Bertin (IAP)
VERSION	15/09/2008
 ***/
void	profit_convolve(profitstruct *profit, float *modpix)
  {
  if (!profit->psfdft)
    profit_makedft(profit);

  fft_conv(modpix, profit->psfdft, profit->modnaxisn);

  return;
  }


/****** profit_makedft *******************************************************
PROTO	void profit_makedft(profitstruct *profit)
PURPOSE	Create the Fourier transform of the descrambled PSF component.
INPUT	Pointer to the profit structure.
OUTPUT	-.
NOTES	-.
AUTHOR	E. Bertin (IAP)
VERSION	22/04/2008
 ***/
void	profit_makedft(profitstruct *profit)
  {
   psfstruct	*psf;
   float      *mask,*maskt, *ppix;
   float       dx,dy, r,r2,rmin,rmin2,rmax,rmax2,rsig,invrsig2;
   int          width,height,npix,offset, psfwidth,psfheight,psfnpix,
                cpwidth, cpheight,hcpwidth,hcpheight, i,j,x,y;

  if (!(psf=profit->psf))
    return;

  psfwidth = profit->modnaxisn[0];
  psfheight = profit->modnaxisn[1];
  psfnpix = psfwidth*psfheight;
  width = profit->modnaxisn[0];
  height = profit->modnaxisn[1];
  npix = width*height;
  QCALLOC(mask, float, npix);
  cpwidth = (width>psfwidth)?psfwidth:width;
  hcpwidth = cpwidth>>1;
  cpwidth = hcpwidth<<1;
  offset = width - cpwidth;
  cpheight = (height>psfheight)?psfheight:height;
  hcpheight = cpheight>>1;
  cpheight = hcpheight<<1;

/* Frame and descramble the PSF data */
  ppix = profit->psfpix + (psfheight/2)*psfwidth + psfwidth/2;
  maskt = mask;
  for (j=hcpheight; j--; ppix+=psfwidth)
    {
    for (i=hcpwidth; i--;)
      *(maskt++) = *(ppix++);      
    ppix -= cpwidth;
    maskt += offset;
    for (i=hcpwidth; i--;)
      *(maskt++) = *(ppix++);      
    }

  ppix = profit->psfpix + ((psfheight/2)-hcpheight)*psfwidth + psfwidth/2;
  maskt += width*(height-cpheight);
  for (j=hcpheight; j--; ppix+=psfwidth)
    {
    for (i=hcpwidth; i--;)
      *(maskt++) = *(ppix++);      
    ppix -= cpwidth;
    maskt += offset;
    for (i=hcpwidth; i--;)
      *(maskt++) = *(ppix++);      
    }

/* Truncate to a disk that has diameter = (box width) */
  rmax = cpwidth - 1.0 - hcpwidth;
  if (rmax > (r=hcpwidth))
    rmax = r;
  if (rmax > (r=cpheight-1.0-hcpheight))
    rmax = r;
  if (rmax > (r=hcpheight))
    rmax = r;
  if (rmax<1.0)
    rmax = 1.0;
  rmax2 = rmax*rmax;
  rsig = psf->fwhm/profit->pixstep;
  invrsig2 = 1/(2*rsig*rsig);
  rmin = rmax - (3*rsig);     /* 3 sigma annulus (almost no aliasing) */
  rmin2 = rmin*rmin;

  maskt = mask;
  dy = 0.0;
  for (y=hcpheight; y--; dy+=1.0)
    {
    dx = 0.0;
    for (x=hcpwidth; x--; dx+=1.0, maskt++)
      if ((r2=dx*dx+dy*dy)>rmin2)
        *maskt *= (r2>rmax2)?0.0:expf((2*rmin*sqrtf(r2)-r2-rmin2)*invrsig2);
    dx = -hcpwidth;
    maskt += offset;
    for (x=hcpwidth; x--; dx+=1.0, maskt++)
      if ((r2=dx*dx+dy*dy)>rmin2)
        *maskt *= (r2>rmax2)?0.0:expf((2*rmin*sqrtf(r2)-r2-rmin2)*invrsig2);
    }
  dy = -hcpheight;
  maskt += width*(height-cpheight);
  for (y=hcpheight; y--; dy+=1.0)
    {
    dx = 0.0;
    for (x=hcpwidth; x--; dx+=1.0, maskt++)
      if ((r2=dx*dx+dy*dy)>rmin2)
        *maskt *= (r2>rmax2)?0.0:expf((2*rmin*sqrtf(r2)-r2-rmin2)*invrsig2);
    dx = -hcpwidth;
    maskt += offset;
    for (x=hcpwidth; x--; dx+=1.0, maskt++)
      if ((r2=dx*dx+dy*dy)>rmin2)
        *maskt *= (r2>rmax2)?0.0:expf((2*rmin*sqrtf(r2)-r2-rmin2)*invrsig2);
    }

/* Finally move to Fourier space */
  profit->psfdft = fft_rtf(mask, profit->modnaxisn);

  free(mask);

  return;
  }


/****** profit_copyobjpix *****************************************************
PROTO	int profit_copyobjpix(profitstruct *profit, picstruct *field,
			picstruct *wfield)
PURPOSE	Copy a piece of the input field image to a profit structure.
INPUT	Pointer to the profit structure,
	Pointer to the field structure,
	Pointer to the field weight structure.
OUTPUT	The number of valid pixels copied.
NOTES	Global preferences are used.
AUTHOR	E. Bertin (IAP)
VERSION	01/12/2009
 ***/
int	profit_copyobjpix(profitstruct *profit, picstruct *field,
			picstruct *wfield)
  {
   float	dx, dy2, dr2, rad2;
   PIXTYPE	*pixin,*spixin, *wpixin,*swpixin, *pixout,*wpixout,
		backnoise2, invgain, satlevel, wthresh, pix,spix, wpix,swpix;
   int		i,x,y, xmin,xmax,ymin,ymax, w,h,dw, npix, off, gainflag,
		badflag, sflag, sx,sy,sn,sw, ix,iy;

/* First put the image background to -BIG */
  pixout = profit->objpix;
  wpixout = profit->objweight;
  for (i=profit->objnaxisn[0]*profit->objnaxisn[1]; i--;)
    {
    *(pixout++) = -BIG;
    *(wpixout++) = 0.0;
    }

/* Don't go further if out of frame!! */
  ix = profit->ix;
  iy = profit->iy;
  if (ix<0 || ix>=field->width || iy<field->ymin || iy>=field->ymax)
    return 0;

  backnoise2 = field->backsig*field->backsig;
  sn = (int)profit->subsamp;
  sflag = (sn>1);
  w = profit->objnaxisn[0]*sn;
  h = profit->objnaxisn[1]*sn;
  if (sflag)
    backnoise2 *= (PIXTYPE)sn;
  invgain = (field->gain > 0.0) ? 1.0/field->gain : 0.0;
  satlevel = field->satur_level - profit->obj->bkg;
  rad2 = h/2.0;
  if (rad2 > w/2.0)
    rad2 = w/2.0;
  rad2 *= rad2;

/* Set the image boundaries */
  pixout = profit->objpix;
  wpixout = profit->objweight;
  ymin = iy-h/2;
  ymax = ymin + h;
  if (ymin<field->ymin)
    {
    off = (field->ymin-ymin-1)/sn + 1;
    pixout += off*profit->objnaxisn[0];
    wpixout += off*profit->objnaxisn[0];
    ymin += off*sn;
    }
  if (ymax>field->ymax)
    ymax -= ((ymax-field->ymax-1)/sn + 1)*sn;

  xmin = ix-w/2;
  xmax = xmin + w;
  dw = 0;
  if (xmax>field->width)
    {
    off = (xmax-field->width-1)/sn + 1;
    dw += off;
    xmax -= off*sn;
    }
  if (xmin<0)
    {
    off = (-xmin-1)/sn + 1;
    pixout += off;
    wpixout += off;
    dw += off;
    xmin += off*sn;
    }
/* Make sure the input frame size is a multiple of the subsampling step */
  if (sflag)
    {
/*
    if (((rem=ymax-ymin)%sn))
      {
      ymin += rem/2;
      ymax -= (rem-rem/2);
      }
    if (((rem=xmax-xmin)%sn))
      {
      xmin += rem/2;
      pixout += rem/2;
      wpixout += rem/2;
      dw += rem;
      xmax -= (rem-rem/2);
      }
*/
    sw = field->width;
    }

/* Copy the right pixels to the destination */
  npix = 0;
  if (wfield)
    {
    wthresh = wfield->weight_thresh;
    gainflag = prefs.weightgain_flag;
    if (sflag)
      {
/*---- Sub-sampling case */
      for (y=ymin; y<ymax; y+=sn, pixout+=dw,wpixout+=dw)
        {
        for (x=xmin; x<xmax; x+=sn)
          {
          pix = wpix = 0.0;
          badflag = 0;
          for (sy=0; sy<sn; sy++)
            {
            dy2 = (y+sy-iy);
            dy2 *= dy2;
            dx = (x-ix);
            spixin = &PIX(field, x, y+sy);
            swpixin = &PIX(wfield, x, y+sy);
            for (sx=sn; sx--;)
              {
              dr2 = dy2 + dx*dx;
              dx++;
              spix = *(spixin++);
              swpix = *(swpixin++);
              if (dr2<rad2 && spix>-BIG && spix<satlevel && swpix<wthresh)
                {
                pix += spix;
                wpix += swpix;
                }
              else
                badflag=1;
              }
            }
          *(pixout++) = pix;
          if (!badflag)	/* A single bad pixel ruins is all (saturation, etc.)*/
            {
            *(wpixout++) = 1.0 / sqrt(wpix+(pix>0.0?
		(gainflag? pix*wpix/backnoise2:pix)*invgain : 0.0));
            npix++;
            }
          else
            *(wpixout++) = 0.0;
          }
        }
      }
    else
      for (y=ymin; y<ymax; y++, pixout+=dw,wpixout+=dw)
        {
        dy2 = y-iy;
        dy2 *= dy2;
        pixin = &PIX(field, xmin, y);
        wpixin = &PIX(wfield, xmin, y);
        for (x=xmin; x<xmax; x++)
          {
          dx = x-ix;
          dr2 = dy2 + dx*dx;
          pix = *(pixin++);
          wpix = *(wpixin++);
          if (dr2<rad2 && pix>-BIG && pix<satlevel && wpix<wthresh)
            {
            *(pixout++) = pix;
            *(wpixout++) = 1.0 / sqrt(wpix+(pix>0.0?
		(gainflag? pix*wpix/backnoise2:pix)*invgain : 0.0));
            npix++;
            }
          else
            *(pixout++) = *(wpixout++) = 0.0;
          }
        }
    }
  else
    {
    if (sflag)
      {
/*---- Sub-sampling case */
      for (y=ymin; y<ymax; y+=sn, pixout+=dw, wpixout+=dw)
        {
        for (x=xmin; x<xmax; x+=sn)
          {
          pix = 0.0;
          badflag = 0;
          for (sy=0; sy<sn; sy++)
            {
            dy2 = y+sy-iy;
            dy2 *= dy2;
            dx = x-ix;
            spixin = &PIX(field, x, y+sy);
            for (sx=sn; sx--;)
              {
              dr2 = dy2 + dx*dx;
              dx++;
              spix = *(spixin++);
              if (dr2<rad2 && spix>-BIG && spix<satlevel)
                pix += spix;
              else
                badflag=1;
              }
            }
          *(pixout++) = pix;
          if (!badflag)	/* A single bad pixel ruins is all (saturation, etc.)*/
            {
            *(wpixout++) = 1.0 / sqrt(backnoise2 + (pix>0.0?pix*invgain:0.0));
            npix++;
            }
          else
            *(wpixout++) = 0.0;
          }
        }
      }
    else
      for (y=ymin; y<ymax; y++, pixout+=dw,wpixout+=dw)
        {
        dy2 = y-iy;
        dy2 *= dy2;
        pixin = &PIX(field, xmin, y);
        for (x=xmin; x<xmax; x++)
          {
          dx = x-ix;
          dr2 = dy2 + dx*dx;
          pix = *(pixin++);
          if (dr2<rad2 && pix>-BIG && pix<satlevel)
            {
            *(pixout++) = pix;
            *(wpixout++) = 1.0 / sqrt(backnoise2 + (pix>0.0?pix*invgain : 0.0));
            npix++;
            }
          else
            *(pixout++) = *(wpixout++) = 0.0;
          }
        }
    }
 
  return npix;
  }


/****** profit_spiralindex ****************************************************
PROTO	float profit_spiralindex(profitstruct *profit)
PURPOSE	Compute the spiral index of a galaxy image (positive for arms
	extending counter-clockwise and negative for arms extending CW, 0 for
	no spiral pattern).
INPUT	Profile-fitting structure.
OUTPUT	Vector of residuals.
NOTES	-.
AUTHOR	E. Bertin (IAP)
VERSION	18/09/2008
 ***/
float profit_spiralindex(profitstruct *profit)
  {
   objstruct	*obj;
   obj2struct	*obj2;
   float	*dx,*dy, *fdx,*fdy, *gdx,*gdy, *gdxt,*gdyt, *pix,
		fwhm, invtwosigma2, hw,hh, ohw,ohh, x,y,xstart, tx,ty,txstart,
		gx,gy, r2, spirindex, invsig, val, sep;
   PIXTYPE	*fpix;
   int		i,j, npix;

  npix = profit->objnaxisn[0]*profit->objnaxisn[1];

  obj = profit->obj;
  obj2 = profit->obj2;
/* Compute simple derivative vectors at a fraction of the object scale */
  fwhm = obj2->hl_radius * 2.0 / 4.0;
  if (fwhm < 2.0)
    fwhm = 2.0;
  sep = 2.0;

  invtwosigma2 = -(2.35*2.35/(2.0*fwhm*fwhm));
  hw = (float)(profit->objnaxisn[0]/2);
  ohw = profit->objnaxisn[0] - hw;
  hh = (float)(profit->objnaxisn[1]/2);
  ohh = profit->objnaxisn[1] - hh;
  txstart = -hw;
  ty = -hh;
  QMALLOC(dx, float, npix);
  pix = dx;
  for (j=profit->objnaxisn[1]; j--; ty+=1.0)
    {
    tx = txstart;
    y = ty < -0.5? ty + hh : ty - ohh;
    for (i=profit->objnaxisn[0]; i--; tx+=1.0)
      {
      x = tx < -0.5? tx + hw : tx - ohw;
      *(pix++) = exp(invtwosigma2*((x+sep)*(x+sep)+y*y))
		- exp(invtwosigma2*((x-sep)*(x-sep)+y*y));
      }
    }
  QMALLOC(dy, float, npix);
  pix = dy;
  ty = -hh;
  for (j=profit->objnaxisn[1]; j--; ty+=1.0)
    {
    tx = txstart;
    y = ty < -0.5? ty + hh : ty - ohh;
    for (i=profit->objnaxisn[0]; i--; tx+=1.0)
      {
      x = tx < -0.5? tx + hw : tx - ohw;
      *(pix++) = exp(invtwosigma2*(x*x+(y+sep)*(y+sep)))
		- exp(invtwosigma2*(x*x+(y-sep)*(y-sep)));
      }
    }

  QMALLOC(gdx, float, npix);
  gdxt = gdx;
  fpix = profit->objpix;
  invsig = npix/profit->sigma;
  for (i=npix; i--; fpix++)
    {
    val = *fpix > -1e29? *fpix*invsig : 0.0;
    *(gdxt++) = (val>0.0? log(1.0+val) : -log(1.0-val));
    }
  gdy = NULL;			/* to avoid gcc -Wall warnings */
  QMEMCPY(gdx, gdy, float, npix);
  fdx = fft_rtf(dx, profit->objnaxisn);
  fft_conv(gdx, fdx, profit->objnaxisn);
  fdy = fft_rtf(dy, profit->objnaxisn);
  fft_conv(gdy, fdy, profit->objnaxisn);

/* Compute estimator */
  invtwosigma2 = -1.18*1.18 / (2.0*obj2->hl_radius*obj2->hl_radius);
  xstart = -hw - obj->mx + (int)(obj->mx+0.49999);
  y = -hh -  obj->my + (int)(obj->my+0.49999);;
  spirindex = 0.0;
  gdxt = gdx;
  gdyt = gdy;
  for (j=profit->objnaxisn[1]; j--; y+=1.0)
    {
    x = xstart;
    for (i=profit->objnaxisn[0]; i--; x+=1.0)
      {
      gx = *(gdxt++);
      gy = *(gdyt++);
      if ((r2=x*x+y*y)>0.0)
        spirindex += (x*y*(gx*gx-gy*gy)+gx*gy*(y*y-x*x))/r2
			* exp(invtwosigma2*r2);
      }
    }

  free(dx);
  free(dy);
  free(fdx);
  free(fdy);
  free(gdx);
  free(gdy);

  return spirindex;
  }


/****** profit_moments ****************************************************
PROTO	void profit_moments(profitstruct *profit, obj2struct *obj2)
PURPOSE	Compute the 2nd order moments from the unconvolved object model.
INPUT	Profile-fitting structure,
	Pointer to obj2 structure.
OUTPUT	-.
NOTES	-.
AUTHOR	E. Bertin (IAP)
VERSION	03/08/2010
 ***/
void	 profit_moments(profitstruct *profit, obj2struct *obj2)
  {
   profstruct	*prof;
   double	*jac,*jact, *pjac,*pjact,
		*dmx2,*dmx2t,*dmy2,*dmy2t,*dmxy,*dmxyt, *de1,*de1t,*de2,*de2t,
		m0,invm0, mx2,my2,mxy, invden, temp,temp2,invstemp2,temp3,
		pmx2,theta, flux, var1,var2,cov, dval;
   float	 *covart;
   int		findex[PROF_NPROF],
		i,j,p, nparam;

/*  hw = (float)(profit->modnaxisn[0]/2);*/
/*  hh = (float)(profit->modnaxisn[1]/2);*/
/*  r2max = hw<hh? hw*hw : hh*hh;*/
/*  xstart = -hw;*/
/*  y = -hh;*/
/*  pix = profit->modpix;*/
/*  mx2 = my2 = mxy = mx = my = sum = 0.0;*/
/*  for (iy=profit->modnaxisn[1]; iy--; y+=1.0)*/
/*    {*/
/*    x = xstart;*/
/*    for (ix=profit->modnaxisn[0]; ix--; x+=1.0)*/
/*      if (y*y+x*x <= r2max)*/
/*        {*/
/*        val = *(pix++);*/
/*        sum += val;*/
/*        mx  += val*x;*/
/*        my  += val*y;*/
/*        mx2 += val*x*x;*/
/*        mxy += val*x*y;*/
/*        my2 += val*y*y;*/
/*        }*/
/*      else*/
/*        pix++;*/
/*    }*/

/*  if (sum <= 1.0/BIG)*/
/*    sum = 1.0;*/
/*  mx /= sum;*/
/*  my /= sum;*/
/*  obj2->prof_mx2 = mx2 = mx2/sum - mx*mx;*/
/*  obj2->prof_my2 = my2 = my2/sum - my*my;*/
/*  obj2->prof_mxy = mxy = mxy/sum - mx*my;*/

  nparam = profit->nparam;
  if (FLAG(obj2.proferr_e1) || FLAG(obj2.proferr_pol1))
    {
/*-- Set up Jacobian matrices */
    QCALLOC(jac, double, nparam*3);
    QMALLOC(pjac, double, nparam*3);
    dmx2 = jac;
    dmy2 = jac+nparam;
    dmxy = jac+2*nparam;
    }
  else
    jac = pjac = NULL;

  m0 = mx2 = my2 = mxy = 0.0;
  for (p=0; p<profit->nprof; p++)
    {
    prof = profit->prof[p];
    findex[p] = prof_moments(profit, prof, pjac);
    flux = *prof->flux;
    m0 += flux;
    mx2 += prof->mx2*flux;
    my2 += prof->my2*flux;
    mxy += prof->mxy*flux;
    if (jac)
      {
      jact = jac;
      pjact = pjac;
      for (j=nparam*3; j--;)
        *(jact++) += flux * *(pjact++);
      dmx2[findex[p]] += prof->mx2;
      dmy2[findex[p]] += prof->my2;
      dmxy[findex[p]] += prof->mxy;
      }
    }
  invm0 = 1.0 / m0;
  obj2->prof_mx2 = (mx2 *= invm0);
  obj2->prof_my2 = (my2 *= invm0);
  obj2->prof_mxy = (mxy *= invm0);
/* Complete the flux derivative of moments */
  if (jac)
    {
    for (p=0; p<profit->nprof; p++)
      {
      dmx2[findex[p]] -= mx2;
      dmy2[findex[p]] -= my2;
      dmxy[findex[p]] -= mxy;
      }
    jact = jac;
    for (j=nparam*3; j--;)
      *(jact++) *= invm0;
    }

/* Handle fully correlated profiles (which cause a singularity...) */
  if ((temp2=mx2*my2-mxy*mxy)<0.00694)
    {
    mx2 += 0.0833333;
    my2 += 0.0833333;
    temp2 = mx2*my2-mxy*mxy;
    }

  if (FLAG(obj2.prof_pol1))
    {
    if (mx2+my2 > 1.0/BIG)
      {
      obj2->prof_pol1 = (mx2 - my2) / (mx2+my2);
      obj2->prof_pol2 = 2.0*mxy / (mx2 + my2);
      if (jac)
        {
/*------ Compute Jacobian of polarisation and ellipticity */
        if (FLAG(obj2.proferr_pol1))
          {
          invden = 2.0/((mx2+my2)*(mx2+my2));
/*-------- Ellipticity */
          dmx2t = dmx2;
          dmy2t = dmy2;
          dmxyt = dmxy;
          de1t = de1 = pjac;			/* We re-use pjac */
          de2t = de2 = pjac + nparam;		/* We re-use pjac */
          for (j=nparam; j--;)
            {
            *(de1t++) = invden*(*dmx2t*my2-*dmy2t*mx2);
            *(de2t++) = invden*(*(dmxyt++)*(mx2+my2)
			- mxy*(*(dmx2t++)-*(dmy2t++)));
            }
/*-------- Use the Jacobians to compute errors */
          covart = profit->covar;
          var1 = 0.0;
          for (j=0; j<nparam; j++)
            {
            dval = 0.0;
            de1t = de1;
            for (i=nparam; i--;)
              dval += *(covart++)**(de1t++);
            var1 += dval*de1[j];
            }
          obj2->proferr_pol1 = (float)sqrt(var1<0.0? 0.0: var1);
          covart = profit->covar;
          cov = var2 = 0.0;
          for (j=0; j<nparam; j++)
            {
            dval = 0.0;
            de2t = de2;
            for (i=nparam; i--;)
              dval += *(covart++)**(de2t++);
            cov += dval*de1[j];
            var2 += dval*de2[j];
            }
          obj2->proferr_pol2 = (float)sqrt(var2<0.0? 0.0: var2);
          obj2->profcorr_pol12 = (dval=var1*var2) > 0.0? (float)(cov/sqrt(dval))
							: 0.0;
          }
        }
      }
    else
      obj2->prof_pol1 = obj2->prof_pol2
	= obj2->proferr_pol1 = obj2->proferr_pol2 = obj2->profcorr_pol12 = 0.0;
    }

  if (FLAG(obj2.prof_e1))
    {
    if (mx2+my2 > 1.0/BIG)
      {
      if (temp2>=0.0)
        invden = 1.0/(mx2+my2+2.0*sqrt(temp2));
      else
        invden = 1.0/(mx2+my2);
      obj2->prof_e1 = (float)(invden * (mx2 - my2));
      obj2->prof_e2 = (float)(2.0 * invden * mxy);
      if (jac)
        {
/*------ Compute Jacobian of polarisation and ellipticity */
        invstemp2 = (temp2>=0.0) ? 1.0/sqrt(temp2) : 0.0;
        if (FLAG(obj2.proferr_e1))
          {
/*-------- Ellipticity */
          dmx2t = dmx2;
          dmy2t = dmy2;
          dmxyt = dmxy;
          de1t = de1 = pjac;			/* We re-use pjac */
          de2t = de2 = pjac + nparam;		/* We re-use pjac */
          for (j=nparam; j--;)
            {
            temp3 = invden*(*dmx2t+*dmy2t
			+ invstemp2 * (*dmx2t*my2+mx2**dmy2t-2.0*mxy**dmxyt));
            *(de1t++) = invden*(*(dmx2t++) - *(dmy2t++) - (mx2-my2)*temp3);
            *(de2t++) = 2.0*invden*(*(dmxyt++) - mxy*temp3);
            }
/*-------- Use the Jacobians to compute errors */
          covart = profit->covar;
          var1 = 0.0;
          for (j=0; j<nparam; j++)
            {
            dval = 0.0;
            de1t = de1;
            for (i=nparam; i--;)
              dval += *(covart++)**(de1t++);
            var1 += dval*de1[j];
            }
          obj2->proferr_e1 = (float)sqrt(var1<0.0? 0.0: var1);
          covart = profit->covar;
          cov = var2 = 0.0;
          for (j=0; j<nparam; j++)
            {
            dval = 0.0;
            de2t = de2;
            for (i=nparam; i--;)
              dval += *(covart++)**(de2t++);
            cov += dval*de1[j];
            var2 += dval*de2[j];
            }
          obj2->proferr_e2 = (float)sqrt(var2<0.0? 0.0: var2);
          obj2->profcorr_e12 = (dval=var1*var2) > 0.0? (float)(cov/sqrt(dval))
							: 0.0;
          }
        }
      }
    else
      obj2->prof_e1 = obj2->prof_e2
	= obj2->proferr_e1 = obj2->proferr_e2 = obj2->profcorr_e12 = 0.0;
    }

  if (FLAG(obj2.prof_cxx))
    {
    obj2->prof_cxx = (float)(my2/temp2);
    obj2->prof_cyy = (float)(mx2/temp2);
    obj2->prof_cxy = (float)(-2*mxy/temp2);
    }

  if (FLAG(obj2.prof_a))
    {
    if ((fabs(temp=mx2-my2)) > 0.0)
      theta = atan2(2.0 * mxy,temp) / 2.0;
    else
      theta = PI/4.0;

    temp = sqrt(0.25*temp*temp+mxy*mxy);
    pmx2 = 0.5*(mx2+my2);
    obj2->prof_a = (float)sqrt(pmx2 + temp);
    obj2->prof_b = (float)sqrt(pmx2 - temp);
    obj2->prof_theta = theta*180.0/PI;
    }

/* Free memory used by Jacobians */
  free(jac);
  free(pjac);

  return;
  }


/****** profit_surface ****************************************************
PROTO	void profit_surface(profitstruct *profit, obj2struct *obj2)
PURPOSE	Compute surface brightnesses from the unconvolved object model.
INPUT	Pointer to the profile-fitting structure,
	Pointer to obj2 structure.
OUTPUT	-.
NOTES	-.
AUTHOR	E. Bertin (IAP)
VERSION	08/07/2010
 ***/
void	 profit_surface(profitstruct *profit, obj2struct *obj2)
  {
   profitstruct	hdprofit;
   double	dsum,dhsum,dsumoff, dhval, frac, seff;
   float	*spix, *spixt,
		val,vmax,
		scalefac, imsizefac, flux, lost, sum, lostfluxfrac;
   int		i,p, imax, npix, neff;

/* Allocate "high-definition" raster only to make measurements */
  hdprofit.oversamp = PROFIT_OVERSAMP;
  hdprofit.modnaxisn[0] = hdprofit.modnaxisn[1] = PROFIT_HIDEFRES;
  npix = hdprofit.nmodpix = hdprofit.modnaxisn[0]*hdprofit.modnaxisn[1];
/* Find best image size factor from fitting results */
  imsizefac = 2.0*profit_minradius(profit, PROFIT_REFFFAC)/profit->pixstep
	/ (float)profit->modnaxisn[0];
  if (imsizefac<0.01)
    imsizefac = 0.01;
  else if (imsizefac>100.0)
    imsizefac = 100.0;
  scalefac = (float)hdprofit.modnaxisn[0] / (float)profit->modnaxisn[0]
	/ imsizefac;
  hdprofit.pixstep = profit->pixstep / scalefac;
  hdprofit.fluxfac = scalefac*scalefac;
  QCALLOC(hdprofit.modpix, float,npix*sizeof(float));

  for (p=0; p<profit->nparam; p++)
    profit->param[p] = profit->paraminit[p];
  lost = sum = 0.0;

  for (p=0; p<profit->nprof; p++)
    {
    sum += (flux = prof_add(&hdprofit, profit->prof[p],0));
    lost += flux*profit->prof[p]->lostfluxfrac;
    }
  lostfluxfrac = sum > 0.0? lost / sum : 0.0;

/*
char filename[256];
sprintf(filename, "raster_%02d.fits", the_gal);
check=initcheck(filename, CHECK_OTHER, 0);
check->width = hdprofit.modnaxisn[0];
check->height = hdprofit.modnaxisn[1];
reinitcheck(the_field, check);
memcpy(check->pix,hdprofit.modpix,check->npix*sizeof(float));


int r,t;
double ratio,ratio0,ang,ang0, x,x0,y,y0;
list = profit->paramlist;
index = profit->paramindex;
for (p=0; p<nparam; p++)
param[p] = profit->paraminit[p];

ratio0 = profit->paraminit[index[PARAM_SPHEROID_ASPECT]];
ang0 = profit->paraminit[index[PARAM_SPHEROID_POSANG]];
x0 = profit->paraminit[index[PARAM_X]];
y0 = profit->paraminit[index[PARAM_Y]];
for (r=0;r<check->height;r++)
for (t=0; t<check->width;t++)
{
//x = (r-10.0)/100.0 + x0;
//y = (t-10.0)/100.0 + y0;
ratio = ratio0*exp((r-10.0)/400.0);
ang = ang0+(t-10.0)/3.0;

for (i=0; i<PARAM_NPARAM; i++)
{
//if (list[i] && i==PARAM_X)
//param[index[i]] = x;
//if (list[i] && i==PARAM_Y)
//param[index[i]] = y;
if (list[i] && i==PARAM_SPHEROID_ASPECT)
param[index[i]] = ratio;
if (list[i] && i==PARAM_SPHEROID_POSANG)
param[index[i]] = ang;
//if (list[i] && i==PARAM_SPHEROID_REFF)
//param[index[i]] = profit->paraminit[index[i]]*sqrt(ratio0/ratio);
}
profit_residuals(profit,field,wfield, PROFIT_DYNPARAM, param,profit->resi);
*((float *)check->pix + t + r*check->width) = profit->chi2;
}
reendcheck(the_field, check);
endcheck(check);
*/

  if (FLAG(obj2.fluxeff_prof))
    {
/*-- Sort model pixel values */
    spix = NULL;			/* to avoid gcc -Wall warnings */
    QMEMCPY(hdprofit.modpix, spix, float, npix);
    hmedian(spix, npix);
/*-- Build a cumulative distribution */
    dsum = 0.0;
    spixt = spix;
    for (i=npix; i--;)
      dsum += (double)*(spixt++);
/*-- Find matching surface brightness */
    if (lostfluxfrac > 1.0)
      lostfluxfrac = 0.0;
    dhsum = 0.5 * dsum / (1.0-lostfluxfrac);
    dsum = lostfluxfrac * dsum / (1.0-lostfluxfrac);
    neff = 0;
    spixt = spix;
    for (i=npix; i--;)
      if ((dsum += (double)*(spixt++)) >= dhsum)
        {
        neff = i;
        break;
        }
    dhval = (double)*(spixt-1);
    seff = neff;
    dsumoff = 0.0;
    if (spixt>=spix+2)
      if (dhval > 0.0 && (frac = (dsum - dhsum) / dhval) < 1.0)
        {
        seff += frac;
        dsumoff = frac*dhval;
        dhval = dsumoff + (1.0 - frac)*(double)*(spixt-2);
        }
    obj2->fluxeff_prof = dhval;
    if (FLAG(obj2.fluxmean_prof))
      {
      dsum = dsumoff;
      for (i=neff; i--;)
        dsum += (double)*(spixt++);
      obj2->fluxmean_prof = seff > 0.0? dsum / seff : 0.0;
      }
    free(spix);
    }

/* Compute model peak (overwrites oversampled model!!) */
  if (FLAG(obj2.peak_prof))
    {
/*-- Find position of maximum pixel in current hi-def raster */
    imax = 0;
    vmax = -BIG;
    spixt = hdprofit.modpix;
    for (i=npix; i--;)
      if ((val=*(spixt++))>vmax)
        {
        vmax = val;
        imax = i;
        }
    imax = npix-1 - imax;
/*-- Recompute hi-def model raster without oversampling */
/*-- and with the same flux correction factor */
    hdprofit.oversamp = 0;
    memset(hdprofit.modpix,0, npix*sizeof(float));
    for (p=0; p<profit->nprof; p++)
      prof_add(&hdprofit, profit->prof[p], 1);
    obj2->peak_prof = hdprofit.modpix[imax];
    }

/* Free hi-def model raster */
  free(hdprofit.modpix);

  return;
  }


/****** profit_addparam *******************************************************
PROTO	void profit_addparam(profitstruct *profit, paramenum paramindex,
		float **param)
PURPOSE	Add a profile parameter to the list of fitted items.
INPUT	Pointer to the profit structure,
	Parameter index,
	Pointer to the parameter pointer.
OUTPUT	-.
NOTES	-.
AUTHOR	E. Bertin (IAP)
VERSION	29/03/2010
 ***/
void	profit_addparam(profitstruct *profit, paramenum paramindex,
		float **param)
  {
/* Check whether the parameter has already be registered */
  if (profit->paramlist[paramindex])
/*-- Yes */
    *param = profit->paramlist[paramindex];
  else
/*-- No */
    {
    *param = profit->paramlist[paramindex] = &profit->param[profit->nparam];
    profit->paramindex[paramindex] = profit->nparam;
    profit->paramrevindex[profit->nparam++] = paramindex;
    }

  return;
  }


/****** profit_resetparam ****************************************************
PROTO	void profit_resetparam(profitstruct *profit, paramenum paramtype)
PURPOSE	Set the initial, lower and upper boundary values of a profile parameter.
INPUT	Pointer to the profit structure,
	Parameter index.
OUTPUT	-.
NOTES	-.
AUTHOR	E. Bertin (IAP)
VERSION	02/07/2010
 ***/
void	profit_resetparam(profitstruct *profit, paramenum paramtype)
  {
   objstruct	*obj;
   obj2struct	*obj2;
   float	param, parammin,parammax, range;

  obj = profit->obj;
  obj2 = profit->obj2;
  param = parammin = parammax = 0.0;	/* Avoid gcc -Wall warnings*/

  switch(paramtype)
    {
    case PARAM_BACK:
      param = 0.0;
      parammin = -6.0*obj->sigbkg;
      parammax =  6.0*obj->sigbkg;
      break;
    case PARAM_X:
      param = obj->mx - (int)(obj->mx+0.49999);
      range = obj2->hl_radius*4.0;
      if (range>profit->objnaxisn[0]*2.0)
        range = profit->objnaxisn[0]*2.0;
      parammin = -range;
      parammax =  range;
      break;
    case PARAM_Y:
      param = obj->my - (int)(obj->my+0.49999);
      range = obj2->hl_radius*4.0;
      if (range>profit->objnaxisn[1]*2)
        range = profit->objnaxisn[1]*2;
      parammin = -range;
      parammax =  range;
      break;
    case PARAM_SPHEROID_FLUX:
      param = obj2->flux_auto/2.0;
      parammin = -obj2->flux_auto/1000.0;
      parammax = 2*obj2->flux_auto;
      break;
    case PARAM_SPHEROID_REFF:
      param = obj2->hl_radius;
      parammin = 0.1;
      parammax = param * 4.0;
      break;
    case PARAM_SPHEROID_ASPECT:
      param = FLAG(obj2.prof_disk_flux)? 1.0 : obj->b/obj->a;
      parammin = FLAG(obj2.prof_disk_flux)? 0.5 : 0.01;
      parammax = FLAG(obj2.prof_disk_flux)? 2.0 : 100.0;
      break;
    case PARAM_SPHEROID_POSANG:
      param = obj->theta;
      parammin = 90.0;
      parammax =  90.0;
      break;
    case PARAM_SPHEROID_SERSICN:
      param = 4.0;
      parammin = 1.0;
      parammax = 10.0;
      break;
    case PARAM_DISK_FLUX:
      param = obj2->flux_auto/2.0;
      parammin = -obj2->flux_auto/1000.0;
      parammax = 2*obj2->flux_auto;
      break;
    case PARAM_DISK_SCALE:	/* From scalelength to Re */
      param = obj2->hl_radius/1.67835*sqrt(obj->a/obj->b);
      parammin = param/4.0;
      parammax = param * 4.0;
      break;
    case PARAM_DISK_ASPECT:
      param = obj->b/obj->a;
      parammin = 0.01;
      parammax = 100.0;
      break;
    case PARAM_DISK_POSANG:
      param = obj->theta;
      parammin = 90.0;
      parammax =  90.0;
      break;
    case PARAM_ARMS_FLUX:
      param = obj2->flux_auto/2.0;
      parammin = 0.0;
      parammax = obj2->flux_auto*2.0;
      break;
    case PARAM_ARMS_QUADFRAC:
      param = 0.5;
      parammin = 0.0;
      parammax = 1.0;
      break;
    case PARAM_ARMS_SCALE:
      param = 1.0;
      parammin = 0.5;
      parammax = 10.0;
      break;
    case PARAM_ARMS_START:
      param = 0.5;
      parammin = 0.0;
      parammax = 3.0;
      break;
    case PARAM_ARMS_PITCH:
      param = 20.0;
      parammin = 5.0;
      parammax = 50.0;
      break;
    case PARAM_ARMS_PITCHVAR:
      param = 0.0;
      parammin = -1.0;
      parammax = 1.0;
      break;
//      if ((profit->spirindex=profit_spiralindex(profit, obj, obj2)) > 0.0)
//        {
//        param = -param;
//        parammin = -parammax;
//        parammax = -parammin;
//        }
//      printf("spiral index: %g  \n", profit->spirindex);
//      break;
    case PARAM_ARMS_POSANG:
      param = 0.0;
      parammin = 0.0;
      parammax = 0.0;
      break;
    case PARAM_ARMS_WIDTH:
      param = 3.0;
      parammin = 1.5;
      parammax = 11.0;
      break;
    case PARAM_BAR_FLUX:
      param = obj2->flux_auto/10.0;
      parammin = 0.0;
      parammax = 2.0*obj2->flux_auto;
      break;
    case PARAM_BAR_ASPECT:
      param = 0.3;
      parammin = 0.2;
      parammax = 0.5;
      break;
    case PARAM_BAR_POSANG:
      param = 0.0;
      parammin = 0.0;
      parammax = 0.0;
      break;
    case PARAM_INRING_FLUX:
      param = obj2->flux_auto/10.0;
      parammin = 0.0;
      parammax = 2.0*obj2->flux_auto;
      break;
    case PARAM_INRING_WIDTH:
      param = 0.3;
      parammin = 0.0;
      parammax = 0.5;
      break;
    case PARAM_INRING_ASPECT:
      param = 0.8;
      parammin = 0.4;
      parammax = 1.0;
      break;
    case PARAM_OUTRING_FLUX:
      param = obj2->flux_auto/10.0;
      parammin = 0.0;
      parammax = 2.0*obj2->flux_auto;
      break;
    case PARAM_OUTRING_START:
      param = 4.0;
      parammin = 3.5;
      parammax = 6.0;
      break;
    case PARAM_OUTRING_WIDTH:
      param = 0.3;
      parammin = 0.0;
      parammax = 0.5;
      break;
    default:
      error(EXIT_FAILURE, "*Internal Error*: Unknown profile parameter in ",
		"profit_resetparam()");
      break;
   }

  if (parammin!=parammax && (param<=parammin || param>=parammax))
    param = (parammin+parammax)/2.0;
  profit_setparam(profit, paramtype, param, parammin, parammax);

  return;
  }


/****** profit_resetparams ****************************************************
PROTO	void profit_resetparams(profitstruct *profit)
PURPOSE	Set the initial, lower and upper boundary values of profile parameters.
INPUT	Pointer to the profit structure.
OUTPUT	-.
NOTES	-.
AUTHOR	E. Bertin (IAP)
VERSION	18/09/2008
 ***/
void	profit_resetparams(profitstruct *profit)
  {
   int		p;


  for (p=0; p<PARAM_NPARAM; p++)
    profit_resetparam(profit, (paramenum)p);

  return;
  }


/****** profit_setparam ****************************************************
PROTO	void profit_setparam(profitstruct *profit, paramenum paramtype,
		float param, float parammin, float parammax)
PURPOSE	Set the actual, lower and upper boundary values of a profile parameter.
INPUT	Pointer to the profit structure,
	Parameter index,
	Actual value,
	Lower boundary to the parameter,
	Upper boundary to the parameter.
OUTPUT	RETURN_OK if the parameter is registered, RETURN_ERROR otherwise.
AUTHOR	E. Bertin (IAP)
VERSION	15/03/2009
 ***/
int	profit_setparam(profitstruct *profit, paramenum paramtype,
		float param, float parammin, float parammax)
  {
   float	*paramptr;
   int		index;

/* Check whether the parameter has already be registered */
  if ((paramptr=profit->paramlist[(int)paramtype]))
    {
    index = profit->paramindex[(int)paramtype];
    profit->paraminit[index] = param;
    profit->parammin[index] = parammin;
    profit->parammax[index] = parammax;
    return RETURN_OK;
    }
  else
    return RETURN_ERROR;
  }

  
/****** profit_boundtounbound *************************************************
PROTO	void profit_boundtounbound(profitstruct *profit,
				float *param, double *dparam, int index)
PURPOSE	Convert parameters from bounded to unbounded space.
INPUT	Pointer to the profit structure,
	input array of single precision parameters,
	output (incomplete) array of double precision parameters,
	parameter selection index (<0 = all parameters)
OUTPUT	-.
NOTES	-.
AUTHOR	E. Bertin (IAP)
VERSION	29/03/2010
 ***/
void	profit_boundtounbound(profitstruct *profit,
				float *param, double *dparam, int index)
  {
   double	num,den;
   float	tparam;
   int		f,p, pstart,np;

  if (index<0)
    {
    pstart = 0;
    np = profit->nparam;
    }
  else
    {
    pstart = index;
    np = pstart+1;
    }

  f = 0;
  for (p=pstart ; p<np; p++)
    if (profit->freeparam_flag[p])
      {
      tparam = param[p-pstart];
      if (profit->parammin[p]!=profit->parammax[p])
        {
        num = tparam - profit->parammin[p];
        den = profit->parammax[p] - tparam;
        dparam[f] = num>1e-50? (den>1e-50? log(num/den): 50.0) : -50.0;
        }
      else if (profit->parammax[p] > 0.0 || profit->parammax[p] < 0.0)
        dparam[f] = param[p-pstart] / profit->parammax[p];

      f++;
      }

  return;
  }


/****** profit_unboundtobound *************************************************
PROTO	void profit_unboundtobound(profitstruct *profit,
				double *dparam, float *param, int index)
PURPOSE	Convert parameters from unbounded to bounded space.
INPUT	Pointer to the profit structure,
	input (incomplete) array of double precision parameters,
	output array of single precision parameters.
	parameter selection index (<0 = all parameters)
OUTPUT	-.
NOTES	-.
AUTHOR	E. Bertin (IAP)
VERSION	08/07/2010
 ***/
void	profit_unboundtobound(profitstruct *profit,
				double *dparam, float *param, int index)
  {
   int		f,p, pstart,np;

  if (index<0)
    {
    pstart = 0;
    np = profit->nparam;
    }
  else
    {
    pstart = index;
    np = pstart+1;
    }

  f = 0;
  for (p=pstart; p<np; p++)
    {
    if (profit->freeparam_flag[p])
      {
      param[p-pstart] = (profit->parammin[p]!=profit->parammax[p])?
		((profit->parammax[p] - profit->parammin[p])
		/ (1.0 + exp(-(dparam[f]>50.0? 50.0
				: (dparam[f]<-50.0? -50.0: dparam[f]))))
		+ profit->parammin[p])
		:  dparam[f]*profit->parammax[p];
      f++;
      }
    else
      param[p-pstart] = profit->paraminit[p];
    }

  return;
  }


/****** profit_covarunboundtobound ********************************************
PROTO	void profit_covarunboundtobound(profitstruct *profit,
				double *dcovar, float *covar)
PURPOSE	Convert covariance matrix from unbounded to bounded space.
INPUT	Pointer to the profit structure,
	input (incomplete) matrix of double precision covariances,
	output matrix of single precision covariances.
OUTPUT	-.
NOTES	-.
AUTHOR	E. Bertin (IAP)
VERSION	27/07/2010
 ***/
void	profit_covarunboundtobound(profitstruct *profit,
				double *dcovar, float *covar)
  {
   double	*dxdy;
   float	*x,*xmin,*xmax;
   int		*fflag,
		f,f1,f2, nfree, p,p1,p2, nparam;

  nparam = profit->nparam;
  fflag = profit->freeparam_flag;
  nfree = profit->nfreeparam;
  QMALLOC16(dxdy, double, nfree);
  x = profit->paraminit;
  xmin = profit->parammin;
  xmax = profit->parammax;
  f = 0;
  for (p=0; p<profit->nparam; p++)
    if (fflag[p])
      {
      if (xmin[p]!=xmax[p])
        dxdy[f++] = (x[p] - xmin[p]) * (xmax[p] - x[p]) / (xmax[p] - xmin[p]);
      else
        dxdy[f++] = xmax[p];
      }

  memset(profit->covar, 0, nparam*nparam*sizeof(float));
  f2 = 0;
  for (p2=0; p2<nparam; p2++)
    {
    if (fflag[p2])
      {
      f1 = 0;
      for (p1=0; p1<nparam; p1++)
        if (fflag[p1])
          {
          covar[p2*nparam+p1] = (float)(dcovar[f2*nfree+f1]*dxdy[f1]*dxdy[f2]);
          f1++;
          }
      f2++;
      }
    }

  free(dxdy);

  return;
  }


/****** prof_init *************************************************************
PROTO	profstruct prof_init(profitstruct *profit, proftypenum profcode)
PURPOSE	Allocate and initialize a new profile structure.
INPUT	Pointer to the profile-fitting structure,
	profile type.
OUTPUT	A pointer to an allocated prof structure.
NOTES	-.
AUTHOR	E. Bertin (IAP)
VERSION	07/04/2010
 ***/
profstruct	*prof_init(profitstruct *profit, proftypenum profcode)
  {
   profstruct	*prof;
   float	*pix,
		rmax2, re2, dy2,r2, scale, zero, k,n, hinvn;
   int		width,height, ixc,iyc, ix,iy, nsub,
		d,s;

  QCALLOC(prof, profstruct, 1);
  prof->code = profcode;
  switch(profcode)
    {
    case PROF_BACK:
      prof->naxis = 2;
      prof->pix = NULL;
      profit_addparam(profit, PARAM_BACK, &prof->flux);
      prof->typscale = 1.0;
      break;
    case PROF_SERSIC:
      prof->naxis = 2;
      prof->naxisn[0] = PROFIT_MAXMODSIZE;
      prof->naxisn[1] = PROFIT_MAXMODSIZE;
      prof->naxisn[2] = 1;
      prof->npix = prof->naxisn[0]*prof->naxisn[1]*prof->naxisn[2];
      QMALLOC(prof->pix, float, prof->npix);
      prof->typscale = 1.0;
      profit_addparam(profit, PARAM_X, &prof->x[0]);
      profit_addparam(profit, PARAM_Y, &prof->x[1]);
      profit_addparam(profit, PARAM_SPHEROID_FLUX, &prof->flux);
      profit_addparam(profit, PARAM_SPHEROID_REFF, &prof->scale);
      profit_addparam(profit, PARAM_SPHEROID_ASPECT, &prof->aspect);
      profit_addparam(profit, PARAM_SPHEROID_POSANG, &prof->posangle);
      profit_addparam(profit, PARAM_SPHEROID_SERSICN, &prof->extra[0]);
      break;
    case PROF_DEVAUCOULEURS:
      prof->naxis = 2;
      prof->naxisn[0] = PROFIT_MAXMODSIZE;
      prof->naxisn[1] = PROFIT_MAXMODSIZE;
      prof->naxisn[2] = 1;
      prof->npix = prof->naxisn[0]*prof->naxisn[1]*prof->naxisn[2];
      QMALLOC(prof->pix, float, profit->nmodpix);
      prof->typscale = 1.0;
      profit_addparam(profit, PARAM_X, &prof->x[0]);
      profit_addparam(profit, PARAM_Y, &prof->x[1]);
      profit_addparam(profit, PARAM_SPHEROID_FLUX, &prof->flux);
      profit_addparam(profit, PARAM_SPHEROID_REFF, &prof->scale);
      profit_addparam(profit, PARAM_SPHEROID_ASPECT, &prof->aspect);
      profit_addparam(profit, PARAM_SPHEROID_POSANG, &prof->posangle);
      break;
    case PROF_EXPONENTIAL:
      prof->naxis = 2;
      prof->naxisn[0] = PROFIT_MAXMODSIZE;
      prof->naxisn[1] = PROFIT_MAXMODSIZE;
      prof->naxisn[2] = 1;
      prof->npix = prof->naxisn[0]*prof->naxisn[1]*prof->naxisn[2];
      QMALLOC(prof->pix, float, profit->nmodpix);
      prof->typscale = 1.0;
      profit_addparam(profit, PARAM_X, &prof->x[0]);
      profit_addparam(profit, PARAM_Y, &prof->x[1]);
      profit_addparam(profit, PARAM_DISK_FLUX, &prof->flux);
      profit_addparam(profit, PARAM_DISK_SCALE, &prof->scale);
      profit_addparam(profit, PARAM_DISK_ASPECT, &prof->aspect);
      profit_addparam(profit, PARAM_DISK_POSANG, &prof->posangle);
      break;
    case PROF_ARMS:
      prof->naxis = 2;
      prof->naxisn[0] = PROFIT_MAXMODSIZE;
      prof->naxisn[1] = PROFIT_MAXMODSIZE;
      prof->naxisn[2] = 1;
      prof->npix = prof->naxisn[0]*prof->naxisn[1]*prof->naxisn[2];
      QMALLOC(prof->pix, float, profit->nmodpix);
      prof->typscale = 1.0;
      profit_addparam(profit, PARAM_X, &prof->x[0]);
      profit_addparam(profit, PARAM_Y, &prof->x[1]);
      profit_addparam(profit, PARAM_DISK_SCALE, &prof->scale);
      profit_addparam(profit, PARAM_DISK_ASPECT, &prof->aspect);
      profit_addparam(profit, PARAM_DISK_POSANG, &prof->posangle);
      profit_addparam(profit, PARAM_ARMS_FLUX, &prof->flux);
      profit_addparam(profit, PARAM_ARMS_QUADFRAC, &prof->featfrac);
//      profit_addparam(profit, PARAM_ARMS_SCALE, &prof->featscale);
      profit_addparam(profit, PARAM_ARMS_START, &prof->featstart);
      profit_addparam(profit, PARAM_ARMS_PITCH, &prof->featpitch);
//      profit_addparam(profit, PARAM_ARMS_PITCHVAR, &prof->featpitchvar);
      profit_addparam(profit, PARAM_ARMS_POSANG, &prof->featposang);
//      profit_addparam(profit, PARAM_ARMS_WIDTH, &prof->featwidth);
      break;
    case PROF_BAR:
      prof->naxis = 2;
      prof->naxisn[0] = PROFIT_MAXMODSIZE;
      prof->naxisn[1] = PROFIT_MAXMODSIZE;
      prof->naxisn[2] = 1;
      prof->npix = prof->naxisn[0]*prof->naxisn[1]*prof->naxisn[2];
      QMALLOC(prof->pix, float, profit->nmodpix);
      prof->typscale = 1.0;
      profit_addparam(profit, PARAM_X, &prof->x[0]);
      profit_addparam(profit, PARAM_Y, &prof->x[1]);
      profit_addparam(profit, PARAM_DISK_SCALE, &prof->scale);
      profit_addparam(profit, PARAM_DISK_ASPECT, &prof->aspect);
      profit_addparam(profit, PARAM_DISK_POSANG, &prof->posangle);
      profit_addparam(profit, PARAM_ARMS_START, &prof->featstart);
      profit_addparam(profit, PARAM_BAR_FLUX, &prof->flux);
      profit_addparam(profit, PARAM_BAR_ASPECT, &prof->feataspect);
      profit_addparam(profit, PARAM_ARMS_POSANG, &prof->featposang);
      break;
    case PROF_INRING:
      prof->naxis = 2;
      prof->naxisn[0] = PROFIT_MAXMODSIZE;
      prof->naxisn[1] = PROFIT_MAXMODSIZE;
      prof->naxisn[2] = 1;
      prof->npix = prof->naxisn[0]*prof->naxisn[1]*prof->naxisn[2];
      QMALLOC(prof->pix, float, profit->nmodpix);
      prof->typscale = 1.0;
      profit_addparam(profit, PARAM_X, &prof->x[0]);
      profit_addparam(profit, PARAM_Y, &prof->x[1]);
      profit_addparam(profit, PARAM_DISK_SCALE, &prof->scale);
      profit_addparam(profit, PARAM_DISK_ASPECT, &prof->aspect);
      profit_addparam(profit, PARAM_DISK_POSANG, &prof->posangle);
      profit_addparam(profit, PARAM_ARMS_START, &prof->featstart);
      profit_addparam(profit, PARAM_INRING_FLUX, &prof->flux);
      profit_addparam(profit, PARAM_INRING_WIDTH, &prof->featwidth);
      profit_addparam(profit, PARAM_INRING_ASPECT, &prof->feataspect);
      break;
    case PROF_OUTRING:
      prof->naxis = 2;
      prof->naxisn[0] = PROFIT_MAXMODSIZE;
      prof->naxisn[1] = PROFIT_MAXMODSIZE;
      prof->naxisn[2] = 1;
      prof->npix = prof->naxisn[0]*prof->naxisn[1]*prof->naxisn[2];
      QMALLOC(prof->pix, float, profit->nmodpix);
      prof->typscale = 1.0;
      profit_addparam(profit, PARAM_X, &prof->x[0]);
      profit_addparam(profit, PARAM_Y, &prof->x[1]);
      profit_addparam(profit, PARAM_DISK_SCALE, &prof->scale);
      profit_addparam(profit, PARAM_DISK_ASPECT, &prof->aspect);
      profit_addparam(profit, PARAM_DISK_POSANG, &prof->posangle);
      profit_addparam(profit, PARAM_OUTRING_START, &prof->featstart);
      profit_addparam(profit, PARAM_OUTRING_FLUX, &prof->flux);
      profit_addparam(profit, PARAM_OUTRING_WIDTH, &prof->featwidth);
      break;
    case PROF_DIRAC:
      prof->naxis = 2;
      prof->naxisn[0] = PROFIT_MAXMODSIZE;
      prof->naxisn[1] = PROFIT_MAXMODSIZE;
      prof->naxisn[2] = 1;
      prof->npix = prof->naxisn[0]*prof->naxisn[1]*prof->naxisn[2];
      prof->typscale = 1.0;
      profit_addparam(profit, PARAM_X, &prof->x[0]);
      profit_addparam(profit, PARAM_Y, &prof->x[1]);
      profit_addparam(profit, PARAM_DISK_FLUX, &prof->flux);
      break;
    case PROF_SERSIC_TABEX:	/* An example of tabulated profile */
      prof->naxis = 3;
      width =  prof->naxisn[0] = PROFIT_PROFRES;
      height = prof->naxisn[1] = PROFIT_PROFRES;
      nsub = prof->naxisn[2] = PROFIT_PROFSRES;
      QCALLOC(prof->pix, float, width*height*nsub);
      ixc = width/2;
      iyc = height/2;
      rmax2 = (ixc - 1.0)*(ixc - 1.0);
      re2 = width/64.0;
      prof->typscale = re2;
      re2 *= re2;
      zero = prof->extrazero[0] = 0.2;
      scale = prof->extrascale[0]= 8.0/PROFIT_PROFSRES;
      pix = prof->pix;
      for (s=0; s<nsub; s++)
        {
        n = s*scale + zero;
        hinvn = 0.5/n;
        k = -1.0/3.0 + 2.0*n + 4.0/(405.0*n) + 46.0/(25515.0*n*n)
		+ 131.0/(1148175*n*n*n);
        for (iy=0; iy<height; iy++)
          {
          dy2 = (iy-iyc)*(iy-iyc);
          for (ix=0; ix<width; ix++)
            {
            r2 = dy2 + (ix-ixc)*(ix-ixc);
            *(pix++) = (r2<rmax2)? exp(-k*pow(r2/re2,hinvn)) : 0.0;
            }
          }
        }
      profit_addparam(profit, PARAM_X, &prof->x[0]);
      profit_addparam(profit, PARAM_Y, &prof->x[1]);
      profit_addparam(profit, PARAM_SPHEROID_FLUX, &prof->flux);
      profit_addparam(profit, PARAM_SPHEROID_REFF, &prof->scale);
      profit_addparam(profit, PARAM_SPHEROID_ASPECT, &prof->aspect);
      profit_addparam(profit, PARAM_SPHEROID_POSANG, &prof->posangle);
      profit_addparam(profit, PARAM_SPHEROID_SERSICN, &prof->extra[0]);
      break;
    default:
      error(EXIT_FAILURE, "*Internal Error*: Unknown profile in ",
		"prof_init()");
      break;
    }

  QMALLOC(prof->pix, float, prof->npix);

  if (prof->naxis>2)
    {
    prof->kernelnlines = 1;
    for (d=0; d<prof->naxis; d++)
      {
      prof->interptype[d] = INTERP_BILINEAR;
      prof->kernelnlines *= 
	(prof->kernelwidth[d] = interp_kernwidth[prof->interptype[d]]);
      }
    prof->kernelnlines /= prof->kernelwidth[0];
    QMALLOC16(prof->kernelbuf, float, prof->kernelnlines);
    }

  return prof;
  }  


/****** prof_end **************************************************************
PROTO	void prof_end(profstruct *prof)
PURPOSE	End (deallocate) a profile structure.
INPUT	Prof structure.
OUTPUT	-.
NOTES	-.
AUTHOR	E. Bertin (IAP)
VERSION	09/04/2007
 ***/
void	prof_end(profstruct *prof)
  {
  if (prof->pix)
    {
    free(prof->pix);
    free(prof->kernelbuf);
    }
  free(prof);

  return;
  }


/****** prof_add **************************************************************
PROTO	float prof_add(profitstruct *profit, profstruct *prof,
		int extfluxfac_flag)
PURPOSE	Add a model profile to an image.
INPUT	Profile-fitting structure,
	profile structure,
	flag (0 if flux correction factor is to be computed internally) 
OUTPUT	Total (asymptotic) flux contribution.
NOTES	-.
AUTHOR	E. Bertin (IAP)
VERSION	08/07/2010
 ***/
float	prof_add(profitstruct *profit, profstruct *prof, int extfluxfac_flag)
  {
   double	xscale, yscale, saspect, ctheta,stheta, flux, scaling, bn, n;
   float	posin[PROFIT_MAXEXTRA], posout[2], dnaxisn[2],
		*pixin, *pixin2, *pixout,
		fluxfac, amp,cd11,cd12,cd21,cd22, dcd11,dcd21, dx1,dx2,
		x1,x10,x2, x1cin,x2cin, x1cout,x2cout, x1max,x2max,
		x1in,x2in, odx, ostep,
		k, hinvn, x1t,x2t, ca,sa, u,umin,
		armamp,arm2amp, armrdphidr, armrdphidrvar, posang,
		width, invwidth2,
		r,r2,rmin, r2minxin,r2minxout, rmax, r2max,
		r2max1, r2max2, r2min, invr2xdif,
		val, theta, thresh, ra,rb,rao, num;
   int		npix, noversamp, threshflag,
		d,e,i, ix1,ix2, idx1,idx2, nx2, npix2;

  npix = profit->nmodpix;

  if (prof->code==PROF_BACK)
    {
    amp = fabs(*prof->flux);
    pixout = profit->modpix;
    for (i=npix; i--;)
      *(pixout++) += amp;
    prof->lostfluxfrac = 0.0;
    return 0.0;
    }

  scaling = profit->pixstep / prof->typscale;

  if (prof->code!=PROF_DIRAC)
    {
/*-- Compute Profile CD matrix */
    ctheta = cos(*prof->posangle*DEG);
    stheta = sin(*prof->posangle*DEG);
    saspect = fabs(*prof->aspect);
    xscale = (*prof->scale==0.0)?
		0.0 : fabs(scaling / (*prof->scale*prof->typscale));
    yscale = (*prof->scale*saspect == 0.0)?
		0.0 : fabs(scaling / (*prof->scale*prof->typscale*saspect));
    cd11 = (float)(xscale*ctheta);
    cd12 = (float)(xscale*stheta);
    cd21 = (float)(-yscale*stheta);
    cd22 = (float)(yscale*ctheta);
    dx1 = 0.0;	/* Shifting operations have been moved to profit_resample() */
    dx2 = 0.0;	/* Shifting operations have been moved to profit_resample() */

    x1cout = (float)(profit->modnaxisn[0]/2);
    x2cout = (float)(profit->modnaxisn[1]/2);
    nx2 = profit->modnaxisn[1]/2 + 1;

/*-- Compute the largest r^2 that fits in the frame */
    num = cd11*cd22-cd12*cd21;
    x1max = x1cout - 1.0;
    x2max = x2cout - 1.0;
    r2max1 = x1max*x1max*fabs(num*num / (cd12*cd12+cd22*cd22));
    r2max2 = x2max*x2max*fabs(num*num / (cd11*cd11+cd21*cd21));
    r2max = r2max1 < r2max2? r2max1 : r2max2;
    }

  switch(prof->code)
    {
    case PROF_SERSIC:
      n = fabs(*prof->extra[0]);
      bn = 2.0*n - 1.0/3.0 + 4.0/(405.0*n) + 46.0/(25515.0*n*n)
		+ 131.0/(1148175*n*n*n);	/* Ciotti & Bertin 1999 */
      k = -bn;
      hinvn = 0.5/n;
/*---- The consequence of sampling on flux is compensated by PSF normalisation*/
      x10 = -x1cout - dx1;
      x2 = -x2cout - dx2;
      pixin = prof->pix;
      for (ix2=nx2; ix2--; x2+=1.0)
        {
        x1 = x10;
        for (ix1=profit->modnaxisn[0]; ix1--; x1+=1.0)
          {
          x1in = cd12*x2 + cd11*x1;
          x2in = cd22*x2 + cd21*x1;
          ra = x1in*x1in+x2in*x2in;
          if (ra>r2max)
            {
            *(pixin++) = 0.0;
            continue;
            }
          val = expf(k*expf(logf(ra)*hinvn));
          noversamp  = (int)(val*profit->oversamp+0.1);
          if (noversamp < 2)
            *(pixin++) = val;
          else
            {
            ostep = 1.0/noversamp;
            dcd11 = cd11*ostep;
            dcd21 = cd21*ostep;
            odx = 0.5*(ostep-1.0);
            x1t = x1+odx;
            val = 0.0;
            for (idx2=noversamp; idx2--; odx+=ostep)
              {
              x1in = cd12*(x2+odx) + cd11*x1t;
              x2in = cd22*(x2+odx) + cd21*x1t;
              for (idx1=noversamp; idx1--;)
                {
                rao = x1in*x1in+x2in*x2in;
                val += expf(k*PROFIT_POWF(rao,hinvn));
                x1in += dcd11;
                x2in += dcd21;
                }
              }
            *(pixin++) = val*ostep*ostep;
            }
          }
        }
/*---- Copy the symmetric part */
      if ((npix2=(profit->modnaxisn[1]-nx2)*profit->modnaxisn[0]) > 0)
        {
        pixin2 = pixin - profit->modnaxisn[0] - 1;
        if (!(profit->modnaxisn[0]&1))
          {
          *(pixin++) = 0.0;
          npix2--;
          }
        for (i=npix2; i--;)
          *(pixin++) = *(pixin2--);
        }
      prof->lostfluxfrac = 1.0 - prof_gammainc(2.0*n, bn*pow(r2max, hinvn));
      threshflag = 0;
      break;
    case PROF_DEVAUCOULEURS:
/*---- The consequence of sampling on flux is compensated by PSF normalisation*/
      x10 = -x1cout - dx1;
      x2 = -x2cout - dx2;
      pixin = prof->pix;
      for (ix2=nx2; ix2--; x2+=1.0)
        {
        x1 = x10;
        for (ix1=profit->modnaxisn[0]; ix1--; x1+=1.0)
          {
          x1in = cd12*x2 + cd11*x1;
          x2in = cd22*x2 + cd21*x1;
          ra = x1in*x1in+x2in*x2in;
          if (ra>r2max)
            {
            *(pixin++) = 0.0;
            continue;
            }
          val = expf(-7.66924944f*PROFIT_POWF(ra,0.125));
          noversamp  = (int)(sqrt(val)*profit->oversamp+0.1);
          if (noversamp < 2)
            *(pixin++) = val;
          else
            {
            ostep = 1.0/noversamp;
            dcd11 = cd11*ostep;
            dcd21 = cd21*ostep;
            odx = 0.5*(ostep-1.0);
            x1t = x1+odx;
            val = 0.0;
            for (idx2=noversamp; idx2--; odx+=ostep)
              {
              x1in = cd12*(x2+odx) + cd11*x1t;
              x2in = cd22*(x2+odx) + cd21*x1t;
              for (idx1=noversamp; idx1--;)
                {
                ra = x1in*x1in+x2in*x2in;
                val += expf(-7.66924944f*PROFIT_POWF(ra,0.125));
                x1in += dcd11;
                x2in += dcd21;
                }
              }
            *(pixin++) = val*ostep*ostep;
            }
          }
        }
/*---- Copy the symmetric part */
      if ((npix2=(profit->modnaxisn[1]-nx2)*profit->modnaxisn[0]) > 0)
        {
        pixin2 = pixin - profit->modnaxisn[0] - 1;
        if (!(profit->modnaxisn[0]&1))
          {
          *(pixin++) = 0.0;
          npix2--;
          }
        for (i=npix2; i--;)
          *(pixin++) = *(pixin2--);
        }
      prof->lostfluxfrac = 1.0-prof_gammainc(8.0, 7.66924944*pow(r2max, 0.125));
      threshflag = 0;
      break;
    case PROF_EXPONENTIAL:
      x10 = -x1cout - dx1;
      x2 = -x2cout - dx2;
      pixin = prof->pix;
      for (ix2=nx2; ix2--; x2+=1.0)
        {
        x1 = x10;
        for (ix1=profit->modnaxisn[0]; ix1--; x1+=1.0)
          {
          x1in = cd12*x2 + cd11*x1;
          x2in = cd22*x2 + cd21*x1;
          ra = x1in*x1in+x2in*x2in;
          if (ra>r2max)
            {
            *(pixin++) = 0.0;
            continue;
            }
          val = expf(-sqrtf(ra));
          noversamp  = (int)(val*sqrt(profit->oversamp)+0.1);
          if (noversamp < 2)
            *(pixin++) = val;
          else
            {
            ostep = 1.0/noversamp;
            dcd11 = cd11*ostep;
            dcd21 = cd21*ostep;
            odx = 0.5*(ostep-1.0);
            x1t = x1+odx;
            val = 0.0;
            for (idx2=noversamp; idx2--; odx+=ostep)
              {
              x1in = cd12*(x2+odx) + cd11*x1t;
              x2in = cd22*(x2+odx) + cd21*x1t;
              for (idx1=noversamp; idx1--;)
                {
                ra = x1in*x1in+x2in*x2in;
                val += expf(-sqrtf(ra));
                x1in += dcd11;
                x2in += dcd21;
                }
              }
            *(pixin++) = val*ostep*ostep;
            }
          }
        }
/*---- Copy the symmetric part */
      if ((npix2=(profit->modnaxisn[1]-nx2)*profit->modnaxisn[0]) > 0)
        {
        pixin2 = pixin - profit->modnaxisn[0] - 1;
        if (!(profit->modnaxisn[0]&1))
          {
          *(pixin++) = 0.0;
          npix2--;
          }
        for (i=npix2; i--;)
          *(pixin++) = *(pixin2--);
        }
      rmax = sqrt(r2max);
      prof->lostfluxfrac = (1.0 + rmax)*exp(-rmax);
      threshflag = 0;
      break;
    case PROF_ARMS:
      r2min = *prof->featstart**prof->featstart;
      r2minxin = r2min * (1.0 - PROFIT_BARXFADE) * (1.0 - PROFIT_BARXFADE);
      r2minxout = r2min * (1.0 + PROFIT_BARXFADE) * (1.0 + PROFIT_BARXFADE);
      if ((invr2xdif = (r2minxout - r2minxin)) > 0.0)
        invr2xdif = 1.0 / invr2xdif;
      else
        invr2xdif = 1.0;
      umin = 0.5*logf(r2minxin + 0.00001);
      arm2amp = *prof->featfrac;
      armamp = 1.0 - arm2amp;
      armrdphidr = 1.0/tan(*prof->featpitch*DEG);
      armrdphidrvar = 0.0 /**prof->featpitchvar*/;
      posang = *prof->featposang*DEG;
      width = fabs(*prof->featwidth);
width = 3.0;
      x1 = -x1cout - dx1;
      x2 = -x2cout - dx2;
      pixin = prof->pix;
      for (ix2=profit->modnaxisn[1]; ix2--; x2+=1.0)
        {
        x1t = cd12*x2 + cd11*x1;
        x2t = cd22*x2 + cd21*x1;
        for (ix1=profit->modnaxisn[0]; ix1--;)
          {
          r2 = x1t*x1t+x2t*x2t;
          if (r2>r2minxin)
            {
            u = 0.5*logf(r2 + 0.00001);
            theta = (armrdphidr+armrdphidrvar*(u-umin))*u+posang;
            ca = cosf(theta);
            sa = sinf(theta);
            x1in = (x1t*ca - x2t*sa);
            x2in = (x1t*sa + x2t*ca);
            amp = expf(-sqrtf(x1t*x1t+x2t*x2t));
            if (r2<r2minxout)
              amp *= (r2 - r2minxin)*invr2xdif;
            ra = x1in*x1in/r2;
            rb = x2in*x2in/r2;
            *(pixin++) = amp * (armamp*PROFIT_POWF(ra,width)
				+ arm2amp*PROFIT_POWF(rb,width));
            }
          else
            *(pixin++) = 0.0;
          x1t += cd11;
          x2t += cd21; 
         }
        }
      prof->lostfluxfrac = 0.0;
      threshflag = 1;
      break;
    case PROF_BAR:
      r2min = *prof->featstart**prof->featstart;
      r2minxin = r2min * (1.0 - PROFIT_BARXFADE) * (1.0 - PROFIT_BARXFADE);
      r2minxout = r2min * (1.0 + PROFIT_BARXFADE) * (1.0 + PROFIT_BARXFADE);
      if ((invr2xdif = (r2minxout - r2minxin)) > 0.0)
        invr2xdif = 1.0 / invr2xdif;
      else
        invr2xdif = 1.0;
      invwidth2 = fabs(1.0 / (*prof->featstart**prof->feataspect));
      posang = *prof->featposang*DEG;
      ca = cosf(posang);
      sa = sinf(posang);
      x1 = -x1cout - dx1;
      x2 = -x2cout - dx2;
      pixin = prof->pix;
      for (ix2=profit->modnaxisn[1]; ix2--; x2+=1.0)
        {
        x1t = cd12*x2 + cd11*x1;
        x2t = cd22*x2 + cd21*x1;
        for (ix1=profit->modnaxisn[0]; ix1--;)
          {
          r2 = x1t*x1t+x2t*x2t;
          if (r2<r2minxout)
            {
            x1in = x1t*ca - x2t*sa;
            x2in = invwidth2*(x1t*sa + x2t*ca);
            *(pixin++) = (r2>r2minxin) ?
				(r2minxout - r2)*invr2xdif*expf(-x2in*x2in)
				: expf(-x2in*x2in);
            }
          else
            *(pixin++) = 0.0;
          x1t += cd11;
          x2t += cd21;
          }
        }
      prof->lostfluxfrac = 0.0;
      threshflag = 1;
      break;
    case PROF_INRING:
      rmin = *prof->featstart;
      r2minxin = *prof->featstart-4.0**prof->featwidth;
      if (r2minxin < 0.0)
        r2minxin = 0.0;
      r2minxin *= r2minxin;
      r2minxout = *prof->featstart+4.0**prof->featwidth;
      r2minxout *= r2minxout;
      invwidth2 = 0.5 / (*prof->featwidth**prof->featwidth);
      cd22 /= *prof->feataspect;
      cd21 /= *prof->feataspect;
      x1 = -x1cout - dx1;
      x2 = -x2cout - dx2;
      pixin = prof->pix;
      for (ix2=profit->modnaxisn[1]; ix2--; x2+=1.0)
        {
        x1t = cd12*x2 + cd11*x1;
        x2t = cd22*x2 + cd21*x1;
        for (ix1=profit->modnaxisn[0]; ix1--;)
          {
          r2 = x1t*x1t+x2t*x2t;
          if (r2>r2minxin && r2<r2minxout)
            {
            r = sqrt(r2) - rmin;
            *(pixin++) = expf(-invwidth2*r*r);
            }
          else
            *(pixin++) = 0.0;
          x1t += cd11;
          x2t += cd21;
          }
        }
      prof->lostfluxfrac = 0.0;
      threshflag = 1;
      break;
    case PROF_OUTRING:
      rmin = *prof->featstart;
      r2minxin = *prof->featstart-4.0**prof->featwidth;
      if (r2minxin < 0.0)
        r2minxin = 0.0;
      r2minxin *= r2minxin;
      r2minxout = *prof->featstart+4.0**prof->featwidth;
      r2minxout *= r2minxout;
      invwidth2 = 0.5 / (*prof->featwidth**prof->featwidth);
      x1 = -x1cout - dx1;
      x2 = -x2cout - dx2;
      pixin = prof->pix;
      for (ix2=profit->modnaxisn[1]; ix2--; x2+=1.0)
        {
        x1t = cd12*x2 + cd11*x1;
        x2t = cd22*x2 + cd21*x1;
        for (ix1=profit->modnaxisn[0]; ix1--;)
          {
          r2 = x1t*x1t+x2t*x2t;
          if (r2>r2minxin && r2<r2minxout)
            {
            r = sqrt(r2) - rmin;
            *(pixin++) = expf(-invwidth2*r*r);
            }
          else
            *(pixin++) = 0.0;
          x1t += cd11;
          x2t += cd21;
          }
        }
      prof->lostfluxfrac = 0.0;
      threshflag = 1;
      break;
    case PROF_DIRAC:
      memset(prof->pix, 0, npix*sizeof(float));
      prof->pix[profit->modnaxisn[0]/2
		+ (profit->modnaxisn[1]/2)*profit->modnaxisn[0]] = 1.0;
      prof->lostfluxfrac = 0.0;
      threshflag = 0;
      break;
    default:
/*---- Tabulated profile: remap each pixel */
/*---- Initialize multi-dimensional counters */
     for (d=0; d<2; d++)
        {
        posout[d] = 1.0;	/* FITS convention */
        dnaxisn[d] = profit->modnaxisn[d] + 0.99999;
        }

      for (e=0; e<prof->naxis - 2; e++)
        {
        d = 2+e;
/*------ Compute position along axis */
        posin[d] = (*prof->extra[e]-prof->extrazero[e])/prof->extrascale[e]+1.0;
/*------ Keep position within boundaries and let interpolation do the rest */
        if (posin[d] < 0.99999)
          {
          if (prof->extracycleflag[e])
            posin[d] += (float)prof->naxisn[d];
          else
            posin[d] = 1.0;
          }
        else if (posin[d] > (float)prof->naxisn[d])
          {
          if (prof->extracycleflag[e])
          posin[d] = (prof->extracycleflag[e])?
		  fmod(posin[d], (float)prof->naxisn[d])
		: (float)prof->naxisn[d];
          }
        }
      x1cin = (float)(prof->naxisn[0]/2);
      x2cin = (float)(prof->naxisn[1]/2);
      pixin = prof->pix;
      for (i=npix; i--;)
        {
        x1 = posout[0] - x1cout - 1.0 - dx1;
        x2 = posout[1] - x2cout - 1.0 - dx2;
        posin[0] = cd11*x1 + cd12*x2 + x1cin + 1.0;
        posin[1] = cd21*x1 + cd22*x2 + x2cin + 1.0;
        *(pixin++) = prof_interpolate(prof, posin);
        for (d=0; d<2; d++)
          if ((posout[d]+=1.0) < dnaxisn[d])
            break;
          else
            posout[d] = 1.0;
        }
      prof->lostfluxfrac = 0.0;
      threshflag = 1;
    break;
    }

/* For complex profiles, threshold to the brightest pixel value at border */
  if (threshflag)
    {
/*-- Now find truncation threshold */
/*-- Find the shortest distance to a vignet border */
    rmax = x1cout;
    if (rmax > (r = x2cout))
      rmax = r;
    rmax += 0.01;
    if (rmax<1.0)
      rmax = 1.0;
    r2max = rmax*rmax;
    rmin = rmax - 1.0;
    r2min = rmin*rmin;

/*-- Find best threshold (the max around the circle with radius rmax */
    dx2 = -x2cout;
    pixin = prof->pix;
    thresh = -BIG;
    for (ix2=profit->modnaxisn[1]; ix2--; dx2 += 1.0)
      {
      dx1 = -x1cout;
      for (ix1=profit->modnaxisn[0]; ix1--; dx1 += 1.0)
        if ((val=*(pixin++))>thresh && (r2=dx1*dx1+dx2*dx2)>r2min && r2<r2max)
          thresh = val;
      }

/*-- Threshold and measure the flux */
    flux = 0.0;
    pixin = prof->pix;
    for (i=npix; i--; pixin++)
      if (*pixin >= thresh)
        flux += (double)*pixin;
      else
        *pixin = 0.0;
    }
  else
    {
    flux = 0.0;
    pixin = prof->pix;
    for (i=npix; i--;)
      flux += (double)*(pixin++);
    }

  if (extfluxfac_flag)
    fluxfac = prof->fluxfac;
  else
    {
    if (prof->lostfluxfrac < 1.0)
      flux /= (1.0 - prof->lostfluxfrac);

    prof->fluxfac = fluxfac = fabs(flux)>0.0? profit->fluxfac/flux : 0.0;
    }

  pixin = prof->pix;
  for (i=npix; i--;)
    *(pixin++) *= fluxfac;

/* Correct final flux */
  fluxfac = *prof->flux;
  pixin = prof->pix;
  pixout = profit->modpix;
  for (i=npix; i--;)
    *(pixout++) += fluxfac**(pixin++);

  return *prof->flux;
  }


/****** prof_moments **********************************************************
PROTO	int	prof_moments(profitstruct *profit, profstruct *prof)
PURPOSE	Computes (analytically or numerically) the 2nd moments of a profile.
INPUT	Profile-fitting structure,
	profile structure,
	optional pointer to 3xnparam Jacobian matrix.
OUTPUT	Index to the profile flux for further processing.
NOTES	.
AUTHOR	E. Bertin (IAP)
VERSION	21/07/2010
 ***/
int	prof_moments(profitstruct *profit, profstruct *prof, double *jac)
  {
   double	*dmx2,*dmy2,*dmxy,
		m20, a2, ct,st, mx2fac, my2fac,mxyfac, dc2,ds2,dcs,
		bn,bn2, n,n2, nfac,nfac2, hscale2, dmdn;
   int		nparam, index;

  if (jac)
/*-- Clear output Jacobian */
    {
    nparam = profit->nparam;
    memset(jac, 0, nparam*3*sizeof(double));
    dmx2 = jac;
    dmy2 = jac + nparam;
    dmxy = jac + 2*nparam;
    }
  if (prof->posangle)
    {
    a2 = *prof->aspect**prof->aspect;
    ct = cos(*prof->posangle*DEG);
    st = sin(*prof->posangle*DEG);
    mx2fac = ct*ct + st*st*a2;
    my2fac = st*st + ct*ct*a2;
    mxyfac = ct*st * (1.0 - a2);
    if (jac)
      {
      dc2 = -2.0*ct*st*DEG;
      ds2 =  2.0*ct*st*DEG;
      dcs = (ct*ct - st*st)*DEG;
      }
    switch(prof->code)
      {
      case PROF_SERSIC:
        n = fabs(*prof->extra[0]);
        bn = 2.0*n - 1.0/3.0 + 4.0/(405.0*n) + 46.0/(25515.0*n*n)
		+ 131.0/(1148175*n*n*n);	/* Ciotti & Bertin 1999 */
        nfac  = prof_gamma(4.0*n) / (prof_gamma(2.0*n)*pow(bn, 2.0*n));
        hscale2 = 0.5 * *prof->scale**prof->scale;
        m20 = hscale2 * nfac;
        if (jac)
          {
          dmx2[profit->paramindex[PARAM_SPHEROID_REFF]]
			= *prof->scale * nfac * mx2fac;
          dmy2[profit->paramindex[PARAM_SPHEROID_REFF]]
			= *prof->scale * nfac * my2fac;
          dmxy[profit->paramindex[PARAM_SPHEROID_REFF]]
			= *prof->scale * nfac * mxyfac;
          n2 = n+0.01;
          bn2 = 2.0*n2 - 1.0/3.0 + 4.0/(405.0*n2) + 46.0/(25515.0*n2*n2)
		+ 131.0/(1148175*n2*n2*n2);	/* Ciotti & Bertin 1999 */
          nfac2 = prof_gamma(4.0*n2) / (prof_gamma(2.0*n2)*pow(bn2, 2.0*n2));
          dmdn = 100.0 * hscale2 * (nfac2-nfac);
          dmx2[profit->paramindex[PARAM_SPHEROID_SERSICN]] = dmdn * mx2fac;
          dmy2[profit->paramindex[PARAM_SPHEROID_SERSICN]] = dmdn * my2fac;
          dmxy[profit->paramindex[PARAM_SPHEROID_SERSICN]] = dmdn * mxyfac;
          dmx2[profit->paramindex[PARAM_SPHEROID_ASPECT]]
			= 2.0 * m20 * st*st * *prof->aspect;
          dmy2[profit->paramindex[PARAM_SPHEROID_ASPECT]]
			= 2.0 * m20 * ct*ct * *prof->aspect;
          dmxy[profit->paramindex[PARAM_SPHEROID_ASPECT]]
			= -2.0 * m20 * ct*st * *prof->aspect;
          dmx2[profit->paramindex[PARAM_SPHEROID_POSANG]] = m20 * (dc2+ds2*a2);
          dmy2[profit->paramindex[PARAM_SPHEROID_POSANG]] = m20 * (ds2+dc2*a2);
          dmxy[profit->paramindex[PARAM_SPHEROID_POSANG]] = m20 * (1.0-a2)*dcs;
          }
        index = profit->paramindex[PARAM_SPHEROID_FLUX];
        break; 
      case PROF_DEVAUCOULEURS:
        m20 = 10.83995 * *prof->scale**prof->scale;
        if (jac)
          {
          dmx2[profit->paramindex[PARAM_SPHEROID_REFF]]
			= 21.680 * *prof->scale * mx2fac;
          dmy2[profit->paramindex[PARAM_SPHEROID_REFF]]
			= 21.680 * *prof->scale * my2fac;
          dmxy[profit->paramindex[PARAM_SPHEROID_REFF]]
			= 21.680 * *prof->scale * mxyfac;
          dmx2[profit->paramindex[PARAM_SPHEROID_ASPECT]]
			= 2.0 * m20 * st*st * *prof->aspect;
          dmy2[profit->paramindex[PARAM_SPHEROID_ASPECT]]
			= 2.0 * m20 * ct*ct * *prof->aspect;
          dmxy[profit->paramindex[PARAM_SPHEROID_ASPECT]]
			= -2.0 * m20 * ct*st * *prof->aspect;
          dmx2[profit->paramindex[PARAM_SPHEROID_POSANG]] = m20 * (dc2+ds2*a2);
          dmy2[profit->paramindex[PARAM_SPHEROID_POSANG]] = m20 * (ds2+dc2*a2);
          dmxy[profit->paramindex[PARAM_SPHEROID_POSANG]] = m20 * (1.0-a2)*dcs;
          }
        index = profit->paramindex[PARAM_SPHEROID_FLUX];
        break;
      case PROF_EXPONENTIAL:
        m20 = 3.0 * *prof->scale**prof->scale;
        if (jac)
          {
          dmx2[profit->paramindex[PARAM_DISK_SCALE]]
			= 6.0 * *prof->scale * mx2fac;
          dmy2[profit->paramindex[PARAM_DISK_SCALE]]
			= 6.0 * *prof->scale * my2fac;
          dmxy[profit->paramindex[PARAM_DISK_SCALE]]
			= 6.0 * *prof->scale * mxyfac;
          dmx2[profit->paramindex[PARAM_DISK_ASPECT]]
			= 2.0 * m20 * st*st * *prof->aspect;
          dmy2[profit->paramindex[PARAM_DISK_ASPECT]]
			= 2.0 * m20 * ct*ct * *prof->aspect;
          dmxy[profit->paramindex[PARAM_DISK_ASPECT]]
			= -2.0 * m20 * ct*st * *prof->aspect;
          dmx2[profit->paramindex[PARAM_DISK_POSANG]] = m20 * (dc2 + ds2*a2);
          dmy2[profit->paramindex[PARAM_DISK_POSANG]] = m20 * (ds2 + dc2*a2);
          dmxy[profit->paramindex[PARAM_DISK_POSANG]] = m20 * (1.0 - a2) * dcs;
          }
        index = profit->paramindex[PARAM_DISK_FLUX];
        break;
      case PROF_ARMS:
        m20 = 1.0;
        index = profit->paramindex[PARAM_ARMS_FLUX];
        break;
      case PROF_BAR:
        m20 = 1.0;
        index = profit->paramindex[PARAM_BAR_FLUX];
        break;
      case PROF_INRING:
        m20 = 1.0;
        index = profit->paramindex[PARAM_INRING_FLUX];
        break;
      case PROF_OUTRING:
        m20 = 1.0;
        index = profit->paramindex[PARAM_OUTRING_FLUX];
        break;
      default:
        m20 = 0.0;		/* to avoid gcc -Wall warnings */
        index = 0;		/* to avoid gcc -Wall warnings */
        error(EXIT_FAILURE, "*Internal Error*: Unknown oriented model in ",
		"prof_moments()");
      break;
      }

    prof->mx2 = m20*mx2fac;
    prof->my2 = m20*my2fac;
    prof->mxy = m20*mxyfac;
    
    }
  else
    prof->mx2 = prof->my2 = prof->mxy = 0.0;

  return index;
  }


/****** prof_interpolate ******************************************************
PROTO	float	prof_interpolate(profstruct *prof, float *posin)
PURPOSE	Interpolate a multidimensional model profile at a given position.
INPUT	Profile structure,
	input position vector.
OUTPUT	-.
NOTES	-.
AUTHOR	E. Bertin (IAP)
VERSION	10/12/2006
 ***/
static float	prof_interpolate(profstruct *prof, float *posin)
  {
   float		dpos[2+PROFIT_MAXEXTRA],
			kernel_vector[INTERP_MAXKERNELWIDTH],
			*kvector, *pixin,*pixout,
			val;
   long			step[2+PROFIT_MAXEXTRA],
			start, fac;
   int			linecount[2+PROFIT_MAXEXTRA],
			*naxisn,
			i,j,n, ival, nlines, kwidth,width, badpixflag, naxis;

  naxis = prof->naxis;
  naxisn = prof->naxisn;
  start = 0;
  fac = 1;
  for (n=0; n<naxis; n++)
    {
    val = *(posin++);
    width = *(naxisn++);
/*-- Get the integer part of the current coordinate or nearest neighbour */
    ival = (prof->interptype[n]==INTERP_NEARESTNEIGHBOUR)?
                                        (int)(val-0.50001):(int)val;
/*-- Store the fractional part of the current coordinate */
    dpos[n] = val - ival;
/*-- Check if interpolation start/end exceed image boundary... */
    kwidth = prof->kernelwidth[n];
    ival-=kwidth/2;
    if (ival<0 || ival+kwidth<=0 || ival+kwidth>width)
      return 0.0;

/*-- Update starting pointer */
    start += ival*fac;
/*-- Update step between interpolated regions */
    step[n] = fac*(width-kwidth);
    linecount[n] = 0.0;
    fac *= width;
    }

/* Update Interpolation kernel vectors */
  make_kernel(*dpos, kernel_vector, prof->interptype[0]);
  kwidth = prof->kernelwidth[0];
  nlines = prof->kernelnlines;
/* First step: interpolate along NAXIS1 from the data themselves */
  badpixflag = 0;
  pixin = prof->pix+start;
  pixout = prof->kernelbuf;
  for (j=nlines; j--;)
    {
    val = 0.0;
    kvector = kernel_vector;
    for (i=kwidth; i--;)
       val += *(kvector++)**(pixin++);
    *(pixout++) = val;
    for (n=1; n<naxis; n++)
      {
      pixin+=step[n-1];
      if (++linecount[n]<prof->kernelwidth[n])
        break;
      else
        linecount[n] = 0;       /* No need to initialize it to 0! */
      }
    }

/* Second step: interpolate along other axes from the interpolation buffer */
  for (n=1; n<naxis; n++)
    {
    make_kernel(dpos[n], kernel_vector, prof->interptype[n]);
    kwidth = prof->kernelwidth[n];
    pixout = pixin = prof->kernelbuf;
    for (j = (nlines/=kwidth); j--;)
      {
      val = 0.0;
      kvector = kernel_vector;
      for (i=kwidth; i--;)
        val += *(kvector++)**(pixin++);
      *(pixout++) = val;
     }
    }

  return prof->kernelbuf[0];
  }


/****** interpolate_pix ******************************************************
PROTO	void interpolate_pix(float *posin, float *pix, int naxisn,
		interpenum interptype)
PURPOSE	Interpolate a model profile at a given position.
INPUT	Profile structure,
	input position vector,
	input pixmap dimension vector,
	interpolation type.
OUTPUT	-.
NOTES	-.
AUTHOR	E. Bertin (IAP)
VERSION	07/12/2006
 ***/
static float	interpolate_pix(float *posin, float *pix, int *naxisn,
			interpenum interptype)
  {
   float	buffer[INTERP_MAXKERNELWIDTH],
		kernel[INTERP_MAXKERNELWIDTH], dpos[2],
		*kvector, *pixin, *pixout,
		val;
   int		fac, ival, kwidth, start, width, step,
		i,j, n;

  kwidth = interp_kernwidth[interptype];
  start = 0;
  fac = 1;
  for (n=0; n<2; n++)
    {
    val = *(posin++);
    width = naxisn[n];
/*-- Get the integer part of the current coordinate or nearest neighbour */
    ival = (interptype==INTERP_NEARESTNEIGHBOUR)? (int)(val-0.50001):(int)val;
/*-- Store the fractional part of the current coordinate */
    dpos[n] = val - ival;
/*-- Check if interpolation start/end exceed image boundary... */
    ival-=kwidth/2;
    if (ival<0 || ival+kwidth<=0 || ival+kwidth>width)
      return 0.0;
/*-- Update starting pointer */
    start += ival*fac;
/*-- Update step between interpolated regions */
    fac *= width;
    }

/* First step: interpolate along NAXIS1 from the data themselves */
  make_kernel(dpos[0], kernel, interptype);
  step = naxisn[0]-kwidth;
  pixin = pix+start;
  pixout = buffer;
  for (j=kwidth; j--;)
    {
    val = 0.0;
    kvector = kernel;
    for (i=kwidth; i--;)
      val += *(kvector++)**(pixin++);
    *(pixout++) = val;
    pixin += step;
    }

/* Second step: interpolate along NAXIS2 from the interpolation buffer */
  make_kernel(dpos[1], kernel, interptype);
  pixin = buffer;
  val = 0.0;
  kvector = kernel;
  for (i=kwidth; i--;)
    val += *(kvector++)**(pixin++);

  return val;
  }


/****** make_kernel **********************************************************
PROTO	void make_kernel(float pos, float *kernel, interpenum interptype)
PURPOSE	Conpute interpolation-kernel data
INPUT	Position,
	Pointer to the output kernel data,
	Interpolation method.
OUTPUT	-.
NOTES	-.
AUTHOR	E. Bertin (IAP)
VERSION	07/09/2009
 ***/
void	make_kernel(float pos, float *kernel, interpenum interptype)
  {
   float	x, val, sinx1,sinx2,sinx3,cosx1;

  if (interptype == INTERP_NEARESTNEIGHBOUR)
    *kernel = 1;
  else if (interptype == INTERP_BILINEAR)
    {
    *(kernel++) = 1.0-pos;
    *kernel = pos;
    }
  else if (interptype == INTERP_LANCZOS2)
    {
    if (pos<1e-5 && pos>-1e-5)
      {
      *(kernel++) = 0.0;
      *(kernel++) = 1.0;
      *(kernel++) = 0.0;
      *kernel = 0.0;
      }
    else
      {
      x = -PI/2.0*(pos+1.0);
#ifdef HAVE_SINCOSF
      sincosf(x, &sinx1, &cosx1);
#else
      sinx1 = sinf(x);
      cosx1 = cosf(x);
#endif
      val = (*(kernel++) = sinx1/(x*x));
      x += PI/2.0;
      val += (*(kernel++) = -cosx1/(x*x));
      x += PI/2.0;
      val += (*(kernel++) = -sinx1/(x*x));
      x += PI/2.0;
      val += (*kernel = cosx1/(x*x));
      val = 1.0/val;
      *(kernel--) *= val;
      *(kernel--) *= val;
      *(kernel--) *= val;
      *kernel *= val;
      }
    }
  else if (interptype == INTERP_LANCZOS3)
    {
    if (pos<1e-5 && pos>-1e-5)
      {
      *(kernel++) = 0.0;
      *(kernel++) = 0.0;
      *(kernel++) = 1.0;
      *(kernel++) = 0.0;
      *(kernel++) = 0.0;
      *kernel = 0.0;
      }
    else
      {
      x = -PI/3.0*(pos+2.0);
#ifdef HAVE_SINCOS
      sincosf(x, &sinx1, &cosx1);
#else
      sinx1 = sinf(x);
      cosx1 = cosf(x);
#endif
      val = (*(kernel++) = sinx1/(x*x));
      x += PI/3.0;
      val += (*(kernel++) = (sinx2=-0.5*sinx1-0.866025403785*cosx1)
				/ (x*x));
      x += PI/3.0;
      val += (*(kernel++) = (sinx3=-0.5*sinx1+0.866025403785*cosx1)
				/(x*x));
      x += PI/3.0;
      val += (*(kernel++) = sinx1/(x*x));
      x += PI/3.0;
      val += (*(kernel++) = sinx2/(x*x));
      x += PI/3.0;
      val += (*kernel = sinx3/(x*x));
      val = 1.0/val;
      *(kernel--) *= val;
      *(kernel--) *= val;
      *(kernel--) *= val;
      *(kernel--) *= val;
      *(kernel--) *= val;
      *kernel *= val;
      }
    }
  else if (interptype == INTERP_LANCZOS4)
    {
    if (pos<1e-5 && pos>-1e-5)
      {
      *(kernel++) = 0.0;
      *(kernel++) = 0.0;
      *(kernel++) = 0.0;
      *(kernel++) = 1.0;
      *(kernel++) = 0.0;
      *(kernel++) = 0.0;
      *(kernel++) = 0.0;
      *kernel = 0.0;
      }
    else
      {
      x = -PI/4.0*(pos+3.0);
#ifdef HAVE_SINCOS
      sincosf(x, &sinx1, &cosx1);
#else
      sinx1 = sinf(x);
      cosx1 = cosf(x);
#endif
      val = (*(kernel++) = sinx1/(x*x));
      x += PI/4.0;
      val +=(*(kernel++) = -(sinx2=0.707106781186*(sinx1+cosx1))
				/(x*x));
      x += PI/4.0;
      val += (*(kernel++) = cosx1/(x*x));
      x += PI/4.0;
      val += (*(kernel++) = -(sinx3=0.707106781186*(cosx1-sinx1))/(x*x));
      x += PI/4.0;
      val += (*(kernel++) = -sinx1/(x*x));
      x += PI/4.0;
      val += (*(kernel++) = sinx2/(x*x));
      x += PI/4.0;
      val += (*(kernel++) = -cosx1/(x*x));
      x += PI/4.0;
      val += (*kernel = sinx3/(x*x));
      val = 1.0/val;
      *(kernel--) *= val;
      *(kernel--) *= val;
      *(kernel--) *= val;
      *(kernel--) *= val;
      *(kernel--) *= val;
      *(kernel--) *= val;
      *(kernel--) *= val;
      *kernel *= val;
      }
    }
  else
    error(EXIT_FAILURE, "*Internal Error*: Unknown interpolation type in ",
		"make_kernel()");

  return;
  }
