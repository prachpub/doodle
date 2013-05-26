/*
     This file is part of doodle.
     (C) 2004, 2005 Christian Grothoff (and other contributing authors)

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
 * @file helper2.h
 * @brief helper library for doodle
 * @author Christian Grothoff
 */

#ifndef DOODLE_HELPER2_H
#define DOODLE_HELPER2_H

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/types.h>
#include <extractor.h>
#include "helper1.h"
#include "doodle.h"
#include "gettext.h"

#ifndef MINGW
 #define DIR_SEPARATOR '/'
 #define DIR_SEPARATOR_STR "/"
#else
 #define DIR_SEPARATOR '\\'
 #define DIR_SEPARATOR_STR "\\"
#endif

typedef struct {
  char shortArg;
  char * longArg;
  char * mandatoryArg;
  char * description;
} Help;

void formatHelp(const char * general,
		const char * description,
		const Help * opt);


typedef int (*ScannerCallback)(const char * filename,
			       void * arg);

/**
 * Complete filename (a la shell) from abbrevition.
 * @param fil the name of the file, may contain ~/ or
 *        be relative to the current directory
 * @returns the full file name,
 *          NULL is returned on error
 */
char * expandFileName(const char * fil);

/**
 * Function called by scanDirectory to check if a given
 * directory (and all subdirectories) is "pruned" and
 * thus should not be considered in the scan.
 * @return 0 if not pruned, 1 if pruned
 */
typedef int (*PruneCheck)(const char * name,
			  void * cls);

/**
 * Scan a directory for files. The name of the directory
 * must be expanded first (!).
 * @param dirName the name of the directory
 * @param callback the method to call for each file,
 *        can be NULL, in that case, we only count
 * @param data argument to pass to callback
 * @return the number of files found, -1 on error
 */
int scanDirectory(char * dirName,
		  DOODLE_Logger logger,
		  void * context,
		  PruneCheck pruner,
		  void * pr_arg,
		  ScannerCallback callback,
		  void * cb_arg);


struct EXTRACT_Process;

struct EXTRACT_Process * forkExtractor(int do_default,
				       const char * libraries,
				       DOODLE_Logger logger,
				       void * log_ctx);

void joinExtractor(struct EXTRACT_Process * proc);


/**
 * Find keywords in the given file and append
 * the string describing these keywords to the
 * long string (for the suffix tree).
 */
int buildIndex(struct EXTRACT_Process * elist,
	       FILE * logFile,
	       const char * filename,
	       struct DOODLE_SuffixTree * tree,
	       int do_filenames);


/**
 * Maximum search-string length.  If a search-string is more than
 * MAX_LENGTH/2 characters long, it may not find a match even if one
 * exists.  This value can be enlarged arbitrarily, but the cost is
 * that building the database will become more expensive IF for
 * keywords that are that long (the cost for search-strings is
 * quadratic in the length of the search-string; with MAX_LENGTH it is
 * bounded to become linear after a certain size by breaking the
 * string into smaller sections).
 */
#define MAX_LENGTH 128




#endif
