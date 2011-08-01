/*
*				winpos.c
*
* Compute iteratively windowed parameters.
*
*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
*
*	This file part of:	SExtractor
*
*	Copyright:		(C) 2005-2011 Emmanuel Bertin -- IAP/CNRS/UPMC
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
*	Last modified:		26/07/2011
*
*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%*/

#ifdef HAVE_CONFIG_H
#include        "config.h"
#endif

#include	<math.h>
#include	<stdlib.h>

#include	"define.h"
#include	"globals.h"
#include	"prefs.h"
#include	"winpos.h"

/****** compute_winpos ********************************************************
PROTO	void compute_winpos(picstruct *field, picstruct *wfield,
			 obj2struct *obj2)
PURPOSE	Compute windowed source barycenter.
INPUT	Picture structure pointer,
	Weight-map structure pointer,
	obj2 structure.
OUTPUT  -.
NOTES   obj2->mx and obj2->my are taken as initial centroid guesses.
AUTHOR  E. Bertin (IAP)
VERSION 26/07/2011
 ***/
void	compute_winpos(picstruct *field, picstruct *wfield, objstruct *obj,
			obj2struct *obj2)

  {
   float		r2,invtwosig2, raper,raper2, rintlim,rintlim2,rextlim2,
			dx,dx1,dy,dy2, sig, invngamma, pdbkg,
                        offsetx,offsety,scalex,scaley,scale2, locarea;
   double               tv, norm, pix, var, backnoise2, invgain, locpix,
			dxpos,dypos, err,err2, emx2,emy2,emxy,
			esum, temp,temp2, mx2, my2,mxy,pmx2, theta, mx,my,
			mx2ph, my2ph;
   int                  i,x,y, x2,y2, xmin,xmax,ymin,ymax, sx,sy, w,h,
                        pflag,corrflag, gainflag, errflag, momentflag;
   long                 pos;
   PIXTYPE              *image, *imaget, *weight,*weightt,
                        wthresh = 0.0;

  if (wfield)
    wthresh = wfield->weight_thresh;
  w = obj2->imsize[0];
  h = obj2->imsize[1];
  pflag = (prefs.detect_type==PHOTO)? 1:0;
  corrflag = (prefs.mask_type==MASK_CORRECT);
  gainflag = wfield && prefs.weightgain_flag;
  errflag = FLAG(obj2.winposerr_mx2) | FLAG(obj2.fluxerr_win);
  momentflag = FLAG(obj2.win_mx2) | FLAG(obj2.winposerr_mx2);
  var = backnoise2 = field->backsig*field->backsig;
  invgain = field->gain>0.0? 1.0/field->gain : 0.0;
  sig = obj2->hl_radius*2.0/2.35; /* From half-FWHM to sigma */
  invtwosig2 = 1.0/(2.0*sig*sig);

/* Integration radius */
  raper = WINPOS_NSIG*sig;

/* For photographic data */
  if (pflag)
    {
    invngamma = 1.0/field->ngamma;
    pdbkg = expf(obj2->dbkg*invngamma);
    }
  else
    {
    invngamma = 0.0;
    pdbkg = 0.0;
    }
  raper2 = raper*raper;
/* Internal radius of the oversampled annulus (<r-sqrt(2)/2) */
  rintlim = raper - 0.75;
  rintlim2 = (rintlim>0.0)? rintlim*rintlim: 0.0;
/* External radius of the oversampled annulus (>r+sqrt(2)/2) */
  rextlim2 = (raper + 0.75)*(raper + 0.75);
  scaley = scalex = 1.0/WINPOS_OVERSAMP;
  scale2 = scalex*scaley;
  offsetx = 0.5*(scalex-1.0);
  offsety = 0.5*(scaley-1.0);
/* Use isophotal centroid as a first guess */
  mx = obj2->mx - obj2->immin[0];
  my = obj2->my - obj2->immin[1];

  for (i=0; i<WINPOS_NITERMAX; i++)
    {
    xmin = (int)(mx-raper+0.499999);
    xmax = (int)(mx+raper+1.499999);
    ymin = (int)(my-raper+0.499999);
    ymax = (int)(my+raper+1.499999);
    mx2ph = mx*2.0 + 0.49999;
    my2ph = my*2.0 + 0.49999;

    if (xmin < 0)
      {
      xmin = 0;
      obj2->flag |= OBJ_APERT_PB;
      }
    if (xmax > w)
      {
      xmax = w;
      obj2->flag |= OBJ_APERT_PB;
      }
    if (ymin < 0)
      {
      ymin = 0;
      obj2->flag |= OBJ_APERT_PB;
      }
    if (ymax > h)
      {
      ymax = h;
      obj2->flag |= OBJ_APERT_PB;
      }

    tv = esum = emxy = emx2 = emy2 = mx2 = my2 = mxy = 0.0;
    dxpos = dypos = 0.0;
    image = obj2->image;
    weight = weightt = NULL;		/* To avoid gcc -Wall warnings */
    if (wfield)
      weight = obj2->weight;
    for (y=ymin; y<ymax; y++)
      {
      imaget = image + (pos = y*w + xmin);
      if (wfield)
        weightt = weight + pos;
      for (x=xmin; x<xmax; x++, imaget++, weightt++)
        {
        dx = x - mx;
        dy = y - my;
        if ((r2=dx*dx+dy*dy)<rextlim2)
          {
          if (WINPOS_OVERSAMP>1 && r2>rintlim2)
            {
            dx += offsetx;
            dy += offsety;
            locarea = 0.0;
            for (sy=WINPOS_OVERSAMP; sy--; dy+=scaley)
              {
              dx1 = dx;
              dy2 = dy*dy;
              for (sx=WINPOS_OVERSAMP; sx--; dx1+=scalex)
                if (dx1*dx1+dy2<raper2)
                  locarea += scale2;
              }
            }
          else
            locarea = 1.0;
          locarea *= expf(-r2*invtwosig2);
/*-------- Here begin tests for pixel and/or weight overflows. Things are a */
/*-------- bit intricated to have it running as fast as possible in the most */
/*-------- common cases */
          if ((pix=*imaget)<=-BIG || (wfield && (var=*weightt)>=wthresh))
            {
            if (corrflag
		&& (x2=(int)(mx2ph-x))>=0 && x2<w
		&& (y2=(int)(my2ph-y))>=0 && y2<h
		&& (pix=*(image + (pos = y2*w + x2)))>-BIG)
              {
              if (wfield)
                {
                var = *(weight + pos);
                if (var>=wthresh)
                  pix = var = 0.0;
                }
              }
            else
              {
              pix = 0.0;
              if (wfield)
                var = 0.0;
              }
            }
          if (pflag)
            pix = expf(pix*invngamma);
          dx = x - mx;
          dy = y - my;
          locpix = locarea*pix;
          tv += locpix;
          dxpos += locpix*dx;
          dypos += locpix*dy;
          if (errflag)
            {
            err = var;
            if (pflag)
              err *= locpix*pix*invngamma*invngamma;
            else if (invgain>0.0 && pix>0.0)
              {
              if (gainflag)
                err += pix*invgain*var/backnoise2;
              else
                err += pix*invgain;
              }
            err2 = locarea*locarea*err;
            esum += err2;
            emx2 += err2*(dx*dx+0.0833);	/* Finite pixel size */
            emy2 += err2*(dy*dy+0.0833);	/* Finite pixel size */
            emxy += err2*dx*dy;
            }
          if (momentflag)
            {
            mx2 += locpix*dx*dx;
            my2 += locpix*dy*dy;
            mxy += locpix*dx*dy;
            }
          }
        }
      }

    if (tv>0.0)
      {
      mx += (dxpos /= tv)*WINPOS_FAC;
      my += (dypos /= tv)*WINPOS_FAC;
      }
    else
      break;

/*-- Stop here if position does not change */
    if (dxpos*dxpos+dypos*dypos < WINPOS_STEPMIN*WINPOS_STEPMIN)
      break;
    }
  mx2 = mx2/tv - dxpos*dxpos;
  my2 = my2/tv - dypos*dypos;
  mxy = mxy/tv - dxpos*dypos;
  obj2->winpos_x = mx + 1.0;	/* The dreaded 1.0 FITS offset */
  obj2->winpos_y = my + 1.0; 	/* The dreaded 1.0 FITS offset */
  obj2->winpos_niter = i+1;

/* WINdowed flux */
  if (FLAG(obj2.flux_win))
    {
    obj2->flux_win = tv;
    obj2->fluxerr_win = sqrt(esum);
    obj2->snr_win = esum>(1.0/BIG)? obj2->flux_win / obj2->fluxerr_win: BIG;
    }
  temp2=mx2*my2-mxy*mxy;
  obj2->win_flag = (tv <= 0.0)*4 + (mx2 < 0.0 || my2 < 0.0)*2 + (temp2<0.0);
  if (obj2->win_flag)
    {
/*--- Negative values: revert to isophotal estimates */
    if (FLAG(obj2.winposerr_mx2))
      {
      obj2->winposerr_mx2 = obj->poserr_mx2;
      obj2->winposerr_my2 = obj->poserr_my2;
      obj2->winposerr_mxy = obj->poserr_mxy;
      if (FLAG(obj2.winposerr_a))
        {
        obj2->winposerr_a = obj2->poserr_a;
        obj2->winposerr_b = obj2->poserr_b;
        obj2->winposerr_theta = obj2->poserr_theta;
        }
      if (FLAG(obj2.winposerr_cxx))
        {
        obj2->winposerr_cxx = obj2->poserr_cxx;
        obj2->winposerr_cyy = obj2->poserr_cyy;
        obj2->winposerr_cxy = obj2->poserr_cxy;
        }
      }
    if (momentflag)
      {
      obj2->win_mx2 = obj->mx2;
      obj2->win_my2 = obj->my2;
      obj2->win_mxy = obj->mxy;
      if (FLAG(obj2.win_cxx))
        {
        obj2->win_cxx = obj->cxx;
        obj2->win_cyy = obj->cyy;
        obj2->win_cxy = obj->cxy;
        }
      if (FLAG(obj2.win_a))
        {
        obj2->win_a = obj->a;
        obj2->win_b = obj->b;
        obj2->win_polar = obj2->polar;
        obj2->win_theta = obj->theta;
        }
      }
    }
  else
    {
    if (FLAG(obj2.winposerr_mx2))
      {
      norm = WINPOS_FAC*WINPOS_FAC/(tv*tv);
      emx2 *= norm;
      emy2 *= norm;
      emxy *= norm;
/*-- Handle fully correlated profiles (which cause a singularity...) */
      esum *= 0.08333*norm;
      if (obj2->singuflag && (emx2*emy2-emxy*emxy) < esum*esum)
        {
        emx2 += esum;
        emy2 += esum;
        }

      obj2->winposerr_mx2 = emx2;
      obj2->winposerr_my2 = emy2;
      obj2->winposerr_mxy = emxy;
/*---- Error ellipse parameters */
      if (FLAG(obj2.winposerr_a))
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

        obj2->winposerr_a = (float)sqrt(pmx2);
        obj2->winposerr_b = (float)sqrt(pmy2);
        obj2->winposerr_theta = theta*180.0/PI;
        }

      if (FLAG(obj2.winposerr_cxx))
        {
         double	temp;

        obj2->winposerr_cxx = (float)(emy2/(temp=emx2*emy2-emxy*emxy));
        obj2->winposerr_cyy = (float)(emx2/temp);
        obj2->winposerr_cxy = (float)(-2*emxy/temp);
        }
      }

    if (momentflag)
      {
/*-- Handle fully correlated profiles (which cause a singularity...) */
      if ((temp2=mx2*my2-mxy*mxy)<0.00694)
        {
        mx2 += 0.0833333;
        my2 += 0.0833333;
        temp2 = mx2*my2-mxy*mxy;
        }
      obj2->win_mx2 = mx2;
      obj2->win_my2 = my2;
      obj2->win_mxy = mxy;

      if (FLAG(obj2.win_cxx))
        {
        obj2->win_cxx = (float)(my2/temp2);
        obj2->win_cyy = (float)(mx2/temp2);
        obj2->win_cxy = (float)(-2*mxy/temp2);
        }

      if (FLAG(obj2.win_a))
        {
        if ((fabs(temp=mx2-my2)) > 0.0)
          theta = atan2(2.0 * mxy,temp) / 2.0;
        else
          theta = PI/4.0;

        temp = sqrt(0.25*temp*temp+mxy*mxy);
        pmx2 = 0.5*(mx2+my2);
        obj2->win_a = (float)sqrt(pmx2 + temp);
        obj2->win_b = (float)sqrt(pmx2 - temp);
        if (FLAG(obj2.win_polar))
          obj2->win_polar = temp / pmx2;
        obj2->win_theta = theta*180.0/PI;
        }
      }
    }

  return;
  }


