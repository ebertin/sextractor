/*
*				main.c
*
* Command line parsing.
*
*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
*
*	This file part of:	SExtractor
*
*	Copyright:		(C) 1993-2012 Emmanuel Bertin -- IAP/CNRS/UPMC
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
*	Last modified:		21/03/2012
*
*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%*/

#ifdef HAVE_CONFIG_H
#include        "config.h"
#endif

#include	<ctype.h>
#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>

#include	"define.h"
#include	"globals.h"
#include	"catout.h"
#include	"prefs.h"
#include "pattern.h"
#define		SYNTAX \
EXECUTABLE " <image> [<image2>][-c <configuration_file>][-<keyword> <value>]\n" \
"> to dump a default configuration file:          " EXECUTABLE " -d \n" \
"> to dump a default extended configuration file: " EXECUTABLE " -dd \n" \
"> to dump a full list of measurement parameters: " EXECUTABLE " -dp \n"

extern const char       notokstr[];

/****** main *****************************************************************
PROTO	void main(int argc, char *argv[])
PURPOSE	Main function (called from the shell).
INPUT	Number of arguments (including the executable name),
	array of strings.
OUTPUT	EXIT_SUCCESS in case of success, EXIT_FAILURE otherwise.
NOTES	Global preferences are used.
AUTHOR	E. Bertin (IAP)
VERSION	21/03/2012
 ***/
int	main(int argc, char *argv[])

  {
   double	tdiff, lines, dets;
   char		**argkey, **argval,
		*str, *listbuf;
   int		a, narg, nim,ntok, opt, opt2;

setlinebuf(stdout);
 if (argc<2)
    {
    fprintf(OUTPUT, "\n         %s  version %s (%s)\n", BANNER,MYVERSION,DATE);
    fprintf(OUTPUT, "\nWritten by %s\n", AUTHORS);
    fprintf(OUTPUT, "Copyright %s\n", COPYRIGHT);
    fprintf(OUTPUT, "\nvisit %s\n", WEBSITE);
    fprintf(OUTPUT, "\n%s\n", DISCLAIMER);
    error(EXIT_SUCCESS, "SYNTAX: ", SYNTAX);
    }
  QMALLOC(argkey, char *, argc);
  QMALLOC(argval, char *, argc);

/*default parameters */
  prefs.command_line = argv;
  prefs.ncommand_line = argc;
  prefs.pipe_flag = 0;
  prefs.nimage = 1;
  prefs.image_name[0] = "image";
  strcpy(prefs.prefs_name, "default.sex");
  narg = nim = 0;

  for (a=1; a<argc; a++)
    {
    if (*(argv[a]) == '-')
      {
      opt = (int)argv[a][1];
      if (strlen(argv[a])<4 || opt == '-')
        {
        opt2 = (int)tolower((int)argv[a][2]);
        if (opt == '-')
          {
          opt = opt2;
          opt2 = (int)tolower((int)argv[a][3]);
          }
        switch(opt)
          {
          case 'c':
            if (a<(argc-1))
              strcpy(prefs.prefs_name, argv[++a]);
            break;
          case 'd':
            if (opt2=='d')
              prefs_dump(1);
            else if (opt2=='p')
              catout_dumpparams();
            else
              prefs_dump(0);
            exit(EXIT_SUCCESS);
            break;
          case 'v':
            printf("%s version %s (%s)\n", BANNER,MYVERSION,DATE);
            exit(0);
            break;
          case 'h':
          default:
            error(EXIT_SUCCESS,"SYNTAX: ", SYNTAX);
          }
        }
      else
        {
/*------ Config parameters */
        argkey[narg] = &argv[a][1];
        argval[narg++] = argv[++a];
        }       
      }
    else
      {
/*---- The input image filename(s) */
      for(; (a<argc) && (*argv[a]!='-'); a++)
        {
        str = (*argv[a] == '@'? listbuf=list_to_str(argv[a]+1) : argv[a]);
        for (ntok=0; (str=strtok(ntok?NULL:str, notokstr)); nim++,ntok++)
          if (nim<MAXIMAGE)
            prefs.image_name[nim] = str;
          else
            error(EXIT_FAILURE, "*Error*: Too many input images: ", str);
        }
      prefs.nimage = nim;
      a--;
      }
    }

/* Read configuration keywords */
  prefs_read(prefs.prefs_name, argkey, argval, narg);

/* Apply low level preferences/settings (threads, architecture, endianity) */
  prefs_tune();

  free(argkey);
  free(argval);

/* Main processing */
  makeit();

/* Free memory associated with preferences/settings */
  prefs_end();

/* Display computing time / statistics */
  NFPRINTF(OUTPUT, "");
  tdiff = prefs.time_diff>0.0? prefs.time_diff : 0.001;
  lines = (double)thecat.nlinesum/tdiff;
  dets = (double)thecat.ntotalsum/tdiff;
  NPRINTF(OUTPUT,
	"> All done (in %.1f s: %.1f line%s/s , %.1f detection%s/s)\n",
	prefs.time_diff, lines, lines>1.0? "s":"", dets, dets>1.0? "s":"");

  return EXIT_SUCCESS;
  }
