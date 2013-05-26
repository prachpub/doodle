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
 * @file helper1.h
 * @brief helper library for doodle
 * @author Christian Grothoff
 */

#ifndef DOODLE_HELPER1_H
#define DOODLE_HELPER1_H

#include "doodle.h"
#include "gettext.h"
#include <errno.h>

#define _(String) gettext (String)
#define gettext_noop(String) String

#define GROW(arr,size,tsize) xgrow_((void**)&(arr), sizeof(arr[0]), &(size), (tsize), __FILE__, __LINE__)

void * MALLOC(size_t size);

char * STRDUP(const char * str);

void xgrow_(void ** old,
	    size_t elementSize,
	    unsigned int * oldCount,
	    unsigned int newCount,
	    const char * filename,
	    const int linenumber);

#endif
