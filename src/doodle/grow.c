/*
     This file is part of doodle.
     (C) 2004, 2010 Christian Grothoff (and other contributing authors)

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
 * @file grow.c
 * @brief definition of the MALLOC, GROW and STRDUP functions
 * @author Christian Grothoff
 */

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/types.h>
#include "config.h"
#include "gettext.h"
#include "helper1.h"

void * MALLOC(size_t size) {
  void * ret;

  if (size == 0) {
    fprintf(stderr,
	    _("FATAL: MALLOC called with size 0!\n"));
    abort();
  }
  if (size < 0) {
    fprintf(stderr,
	    _("FATAL: MALLOC called with size < 0!\n"));
    abort();
  }
  ret = malloc(size);
  if (ret == NULL) {
    fprintf(stderr,
	    _("FATAL: %s\n"),
	    strerror(errno));
    abort();
  }
  memset(ret, 0, size);
  return ret;
}

char * STRDUP(const char * str) {
  char * ret;
  if (str == NULL) {
    fprintf(stderr,
	    _("FATAL: STRDUP called with str NULL!\n"));
    abort();
  }
  ret = strdup(str);
  if (ret == NULL) {
    fprintf(stderr,
	    _("FATAL: %s\n"),
	    strerror(errno));
    abort();
  }
  return ret;
}

/**
 * Grow an array.  Grows old by (*oldCount-newCount)*elementSize bytes
 * and sets *oldCount to newCount.
 *
 * @param old address of the pointer to the array
 *        *old may be NULL
 * @param elementSize the size of the elements of the array
 * @param oldCount address of the number of elements in the *old array
 * @param newCount number of elements in the new array, may be 0
 * @param filename where in the code was the call to GROW
 * @param linenumber where in the code was the call to GROW
 */
void xgrow_(void ** old,
	    size_t elementSize,
	    unsigned int * oldCount,
	    unsigned int newCount,
	    const char * filename,
	    const int linenumber) {
  void * tmp;
  size_t size;

  if (INT_MAX / elementSize <= newCount) {
    fprintf(stderr,
	    _("FATAL: can not allocate %u * %d elements (number too large) at %s:%d.\n"),
	    (unsigned int) elementSize,
	    (int) newCount, filename, linenumber);
    abort();
  }
  size = newCount * elementSize;
  if (size == 0) {
    tmp = NULL;
  } else {
    tmp = MALLOC(size);
    memset(tmp, 0, size); /* client code should not rely on this, though... */
    if (*oldCount > newCount)
      *oldCount = newCount; /* shrink is also allowed! */
    memcpy(tmp,
	   *old,
	   elementSize * (*oldCount));
  }

  if (*old != NULL) {
    free(*old);
  }
  *old = tmp;
  *oldCount = newCount;
}

/* end of grow.c */

