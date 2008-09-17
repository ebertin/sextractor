 /*
 				pattern.c

*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
*
*	Part of:	SExtractor
*
*	Authors:	E.BERTIN (IAP)
*
*	Contents:	Generate and handle image patterns for image fitting.
*
*	Last modify:	17/09/2008
*
*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
*/

#ifdef HAVE_CONFIG_H
#include        "config.h"
#endif

#ifdef HAVE_MATHIMF_H
#include <mathimf.h>
#else
#define _GNU_SOURCE
#include <math.h>
#endif

#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	ATLAS_LAPACK_H

#include	"define.h"
#include	"globals.h"
#include	"prefs.h"
#include	"fits/fitscat.h"
#include	"check.h"
#include	"pattern.h"
#include	"profit.h"

/*------------------------------- variables ---------------------------------*/

/****** pattern_init ***********************************************************
PROTO	patternstruct pattern_init(profitstruct *profit, pattern_type ptype,
				int nvec)
PURPOSE	Allocate and initialize a new pattern structure.
INPUT	Pointer to a profit structure,
	Pattern type,
	Number of vectors.
OUTPUT	Pointer to the new pattern structure.
NOTES	-.
AUTHOR	E. Bertin (IAP)
VERSION	16/09/2008
 ***/
patternstruct	*pattern_init(profitstruct *profit, pattypenum ptype, int nvec)
  {
   patternstruct	*pattern;
   int	npix;

  QCALLOC(pattern, patternstruct, 1);
  pattern->type = ptype;
  pattern->size[0] = profit->modnaxisn[0];
  pattern->size[1] = profit->modnaxisn[1];
  pattern->size[2] = nvec;
  npix = pattern->size[0]*pattern->size[1] * pattern->size[2];
  pattern->aspect = *profit->paramlist[PARAM_DISK_ASPECT];
  pattern->posangle = fmod_m90_p90(*profit->paramlist[PARAM_DISK_POSANG]);
  pattern->scale = *profit->paramlist[PARAM_DISK_SCALE]/profit->pixstep;
  QMALLOC(pattern->modpix, double, npix);
  QMALLOC(pattern->lmodpix, PIXTYPE, npix);
  QMALLOC(pattern->coeff, double, pattern->size[2]);

  return pattern;
  }  


/****** pattern_end ***********************************************************
PROTO	void pattern_end(patternstruct *pattern)
PURPOSE	End (deallocate) a pattern structure.
INPUT	Pattern structure.
OUTPUT	-.
NOTES	-.
AUTHOR	E. Bertin (IAP)
VERSION	16/09/2008
 ***/
void	pattern_end(patternstruct *pattern)
  {
  free(pattern->modpix);
  free(pattern->lmodpix);
  free(pattern->coeff);
  free(pattern);

  return;
  }


/****** pattern_fit ******************************************************
PROTO	void pattern_resample(patternstruct *pattern)
PURPOSE	Resample a pattern structure.
INPUT	Pointer to pattern structure.
OUTPUT	-.
NOTES	-.
AUTHOR	E. Bertin (IAP)
VERSION	17/09/2008
 ***/
void	pattern_fit(patternstruct *pattern, profitstruct *profit)
  {
catstruct *cat;
char	name[MAXCHAR];
static int number;
   checkstruct	*check;
   double	*inpix, *doutpix1, *alpha,*beta,
		dval, dprod;
   PIXTYPE	*outpix,*outpix1,*outpix2;
   PIXTYPE	*weightpix;
   int		n,p,p2, nvec, ninpix, noutpix,nout;

  nvec = pattern->size[2];
  pattern_create(pattern);
  QMALLOC(alpha, double, nvec*nvec);
  beta = pattern->coeff;
  inpix = pattern->modpix;
  ninpix = pattern->size[0]*pattern->size[1];
  outpix = pattern->lmodpix;
  noutpix = profit->objnaxisn[0]*profit->objnaxisn[1];
  for (p=0; p<nvec; p++)
    {
    profit_convolve(profit, inpix);
    profit_resample(profit, inpix, outpix);
    outpix1 = pattern->lmodpix;
    for (p2=0; p2<=p; p2++)
      {
      weightpix = profit->objweight;
      outpix2 = outpix;
      dval = 0.0;
      for (n=noutpix; n--;)
        {
        dprod = *(outpix1++)**(outpix2++);
        if (*(weightpix++)>0.0)
          dval += dprod;
        }
      alpha[p*nvec+p2] = alpha[p2*nvec+p] = dval;
      }
    weightpix = profit->objweight;
    doutpix1 = profit->resi;
    outpix2 = outpix;
    dval = 0.0;
    for (n=noutpix; n--;)
      {
      dprod = *doutpix1**(outpix2++);
      if (*(weightpix++)>0.0)
        {
        dval += dprod;
        doutpix1++;
        }
      }
    alpha[p*(nvec+1)] += 1.0;
    beta[p] = dval;
    inpix += ninpix;
    outpix += noutpix;
    }

/* Solve the system */
  clapack_dpotrf(CblasRowMajor,CblasUpper,nvec,alpha,nvec);
  clapack_dpotrs(CblasRowMajor,CblasUpper,nvec,1,alpha,nvec,beta,nvec);

  free(alpha);

  if ((check = prefs.check[CHECK_PATTERNS]))
    {
    QCALLOC(outpix, PIXTYPE, noutpix);
    outpix2 = pattern->lmodpix;
    for (p=0; p<nvec; p++)
      {
      dval = pattern->coeff[p];
      outpix1 = outpix;
      for (n=noutpix; n--;)
        *(outpix1++) += dval**(outpix2++);
      }
    addcheck(check, outpix, profit->objnaxisn[0],profit->objnaxisn[1],
		profit->ix, profit->iy, 1.0);
    free(outpix);
    }


nout = (pattern->type==PATTERN_POLARFOURIER?
			(nvec/(PATTERN_FMAX*2+1))*(PATTERN_FMAX+1) : nvec/2);
nout=nvec;
QCALLOC(outpix, PIXTYPE, noutpix*nout);
outpix1 = outpix;
outpix2 = pattern->lmodpix;
for (p=0; p<nvec; p++)
{
dval = pattern->coeff[p];
for (n=noutpix; n--; )
*(outpix1++) += dval**(outpix2++);
/*
if (pattern->type==PATTERN_POLARFOURIER)
  {
  if (p>0 && p%2)
    outpix1 -= noutpix;
  }
else if (!p%2)
  outpix1 -= noutpix;
*/
}
//outpix1 = outpix;
//for (n=noutpix*out; n--; outpix1++)
//*outpix1 *= *outpix1;

cat=new_cat(1);
init_cat(cat);
cat->tab->naxis=3;
QMALLOC(cat->tab->naxisn, int, 3);
cat->tab->naxisn[0]=profit->objnaxisn[0];
cat->tab->naxisn[1]=profit->objnaxisn[1];
cat->tab->naxisn[2]=nout;
cat->tab->bitpix=BP_FLOAT;
cat->tab->bytepix=4;
cat->tab->bodybuf=outpix;
cat->tab->tabsize=cat->tab->naxisn[0]*cat->tab->naxisn[1]*cat->tab->naxisn[2]*sizeof(PIXTYPE);
sprintf(name, "tata_%02d.fits", number);
save_cat(cat, name);
cat->tab->bodybuf=NULL;
free_cat(&cat, 1);

free(outpix);

  return;
  }


/****** pattern_create ******************************************************
PROTO	void pattern_create(patternstruct *pattern)
PURPOSE create a pattern basis.
INPUT	Pointer to pattern structure.
OUTPUT	-.
NOTES	-.
AUTHOR	E. Bertin (IAP)
VERSION	17/09/2008
 ***/
void	pattern_create(patternstruct *pattern)
  {
   double		x1,x2, x1t,x2t, r2,r2min,r2max, lr, lr0, 
			mod,ang,ang0, cosang,sinang, angcoeff,
			ctheta,stheta, saspect,xscale,yscale,
			cd11,cd12,cd21,cd22, x1cout,x2cout, cmod,smod,
			cnorm,snorm,norm, dval;
   double		*scbuf[PATTERN_FMAX],*scpix[PATTERN_FMAX],
			*scpixt,*cpix,*spix, *pix, *r2buf,*r2pix,*modpix;
   int			f,i,p, ix1,ix2, nrad, nvec, npix;

/* Compute Profile CD matrix */
  ctheta = cos(pattern->posangle*DEG);
  stheta = sin(pattern->posangle*DEG);
  saspect = sqrt(fabs(pattern->aspect));
  xscale = (pattern->scale==0.0)? 0.0 : 1.0/fabs(pattern->scale);
  yscale = (pattern->scale*saspect == 0.0)?
			0.0 : 1.0/fabs(pattern->scale*saspect);
  cd11 = xscale*ctheta;
  cd12 = xscale*stheta;
  cd21 = -yscale*stheta;
  cd22 = yscale*ctheta;
 
  x1cout = (double)(pattern->size[0]/2);
  x2cout = (double)(pattern->size[1]/2);

  switch(pattern->type)
    {
    case PATTERN_QUADRUPOLE:
    case PATTERN_OCTOPOLE:
      nvec = pattern->size[2]/2;
      npix = pattern->size[0]*pattern->size[1];
      r2min = fabs(cd11*cd22-cd12*cd21)/10.0;
      r2max = BIG;
      cpix = pattern->modpix;
      spix = pattern->modpix+npix;
      angcoeff = (pattern->type==PATTERN_OCTOPOLE)? 4.0 : 2.0;
      for (p=0; p<nvec; p++, cpix+=npix, spix+=npix)
        {
        x1 = -x1cout;
        x2 = -x2cout;
        lr0 = log(3.0*(p+1)/nvec);
        cnorm = snorm = 0.0;
        for (ix2=pattern->size[1]; ix2--; x2+=1.0)
          {
          x1t = cd12*x2 + cd11*x1;
          x2t = cd22*x2 + cd21*x1;
          for (ix1=pattern->size[0]; ix1--;)
            {
            r2 = x1t*x1t+x2t*x2t;
            if (r2<r2max)
              {
              lr = 20.0*(0.5*log(r2 > r2min ? r2 : r2min)-lr0);
              mod = exp(-0.5*lr*lr);
              ang = angcoeff*atan2(x2t,x1t);
#ifdef HAVE_SINCOS
              sincos(ang, &sinang, &cosang);
#else
              sinang = sin(ang);
              cosang = cos(ang);
#endif
              *(cpix++) = cmod = mod*cosang;
              *(spix++) = smod = mod*sinang;
              cnorm += cmod*cmod;
              snorm += smod*smod;
              }
            else
              *(cpix++) = *(spix++) = 0.0;
            x1t += cd11;
            x2t += cd21;
            }
          }
        cpix -= npix;
        cnorm = cnorm > 0.0? 1.0/cnorm : 0.0;
        for (i=npix; i--;)
          *(cpix++) *= cnorm;
        spix -= npix;
        snorm = snorm > 0.0? 1.0/snorm : 0.0;
        for (i=npix; i--;)
          *(spix++) *= snorm;
        }
      break;
    case PATTERN_POLARFOURIER:
      nvec = pattern->size[2];
      nrad = nvec/(PATTERN_FMAX*2+1);
      npix = pattern->size[0]*pattern->size[1];
      r2min = fabs(cd11*cd22-cd12*cd21)/10.0;
      r2max = BIG;
/*---- Pre-compute radii and quadrupoles to speed up computations later */
      QMALLOC(r2buf, double, npix);
      r2pix = r2buf;
      for (f=0; f<PATTERN_FMAX; f++)
        {
        QMALLOC(scbuf[f], double, 2*npix);
        scpix[f] = scbuf[f];
        }
      x1 = -x1cout;
      x2 = -x2cout;
      for (ix2=pattern->size[1]; ix2--; x2+=1.0)
        {
        x1t = cd12*x2 + cd11*x1;
        x2t = cd22*x2 + cd21*x1;
        for (ix1=pattern->size[0]; ix1--;)
          {
          *(r2pix++) = x1t*x1t+x2t*x2t;
          ang = ang0 = atan2(x2t,x1t);
          for (f=0; f<PATTERN_FMAX; f++)
            {
#ifdef HAVE_SINCOS
            sincos(ang, scpix[f], scpix[f]+npix);
            scpix[f]++;
#else
            *(scpix[f]) = sin(ang);
            *(scpix[f]+++npix) = cos(ang);
#endif
            ang+=ang0;
            }
          x1t += cd11;
          x2t += cd21;
          }
        }
      pix = pattern->modpix;
      for (p=0; p<nrad; p++)
        {
        for (f=0; f<=PATTERN_FMAX; f++)
          {
          norm = 0.0;
          lr0 = log(3.0*(p+1)/nrad);
          r2pix = r2buf;
          if (!f)
            {
            for (i=npix; i--;)
              {
              r2 = *(r2pix++);
              if (r2<r2max)
                {
                lr = 20.0*(0.5*log(r2 > r2min ? r2 : r2min)-lr0);
                *(pix++) = dval = exp(-0.5*lr*lr);
                norm += dval*dval;
                }
              else
                *(pix++) = 0.0;
              }
            pix -= npix;
            norm = norm > 0.0? 1.0/norm : 0.0;
            for (i=npix; i--;)
              *(pix++) *= norm;
            modpix = pix;
            }
          else
            {
            modpix -= npix;
            scpixt = scbuf[f-1];
            for (i=npix; i--;)
              {
              *(pix++) = dval = *(modpix++)**(scpixt++);
              norm += dval*dval;
              }
            pix -= npix;
            norm = norm > 0.0? 1.0/norm : 0.0;
            for (i=npix; i--;)
              *(pix++) *= norm;
            modpix -= npix;
            norm = 0.0;
            for (i=npix; i--;)
              {
              *(pix++) = dval = *(modpix++)**(scpixt++);
              norm += dval*dval;
              }
            pix -= npix;
            norm = norm > 0.0? 1.0/norm : 0.0;
            for (i=npix; i--;)
              *(pix++) *= norm;
            }
          }
        }
      free(r2buf);
      for (f=0; f<PATTERN_FMAX; f++)
        free(scbuf[f]);
      break;
    default:
      error(EXIT_FAILURE, "*Internal Error*: Unknown Pattern type","");
    }

  return;
  }


