/*
     This file is part of doodle.
     (C) 2004 Christian Grothoff (and other contributing authors)

     doodle is free software; you can redistribute it and/or modify
     it under the terms of the GNU General Public License as published
     by the Free Software Foundation; either version 2, or (at your
     option) any later version.

     doodle is distributed in the hope that it will be useful, but
     WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
     General Public License for more details.

     You should have received a copy of the GNU General Public License
     along with doodle; see the file COPYING.  If not, write to the
     Free Software Foundation, Inc., 59 Temple Place - Suite 330,
     Boston, MA 02111-1307, USA.
*/

/**
 * @file doodle/doodle.c
 * @brief help formatter
 * @author Christian Grothoff
 *
 */

#include "config.h"
#include "helper2.h"
#include "gettext.h"
#include <ctype.h>

#define BORDER 29


/**
 * Produce nicely formatted output for --help.  Generic
 * method also used in libextractor and GNUnet.
 */
void formatHelp(const char * general,
		const char * description,
		const Help * opt) {
  int slen;
  int i;
  int j;
  int ml;
  int p;
  char * scp;
  const char * trans;
	
  printf(_("Usage: %s\n%s\n\n"),
	 gettext(general),
	 gettext(description));
  printf(_("Arguments mandatory for long options are also mandatory for short options.\n"));
  slen = 0;
  i = 0;
  while (opt[i].description != NULL) {
    if (opt[i].shortArg == 0)
      printf("      ");
    else
      printf("  -%c, ",
	     opt[i].shortArg);
    printf("--%s",
	   opt[i].longArg);
    slen = 8 + strlen(opt[i].longArg);
    if (opt[i].mandatoryArg != NULL) {
      printf("=%s",
	     opt[i].mandatoryArg);
      slen += 1+strlen(opt[i].mandatoryArg);
    }
    if (slen > BORDER) {
      printf("\n%*s", BORDER, "");
      slen = BORDER;
    }
    if (slen < BORDER) {
      printf("%*s", BORDER-slen, "");
      slen = BORDER;
    }
    trans = gettext(opt[i].description);
    ml = strlen(trans);
    p = 0;
  OUTER:
    while (ml - p > 78 - slen) {
      for (j=p+78-slen;j>p;j--) {
	if (isspace(trans[j])) {
	  scp = MALLOC(j-p+1);
	  memcpy(scp,
		 &trans[p],
		 j-p);
	  scp[j-p] = '\0';
	  printf("%s\n%*s",
		 scp,
		 BORDER+2,
		 "");
	  free(scp);
	  p = j+1;
	  slen = BORDER+2;
	  goto OUTER;
	}
      }
      /* could not find space to break line */
      scp = MALLOC(78 - slen + 1);
      memcpy(scp,
	     &trans[p],
	     78 - slen);
      scp[78 - slen] = '\0';
      printf("%s\n%*s",
	     scp,
	     BORDER+2,
	     "");	
      free(scp);
      slen = BORDER+2;
      p = p + 78 - slen;
    }
    /* print rest */
    if (p < ml)
      printf("%s\n",
	     &trans[p]);
    i++;
  }
}
