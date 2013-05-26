/*
     This file is part of doodle.
     (C) 2004, 2006 Christian Grothoff (and other contributing authors)

     GNUnet is free software; you can redistribute it and/or modify
     it under the terms of the GNU General Public License as published
     by the Free Software Foundation; either version 2, or (at your
     option) any later version.

     GNUnet is distributed in the hope that it will be useful, but
     WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
     General Public License for more details.

     You should have received a copy of the GNU General Public License
     along with GNUnet; see the file COPYING.  If not, write to the
     Free Software Foundation, Inc., 59 Temple Place - Suite 330,
     Boston, MA 02111-1307, USA.
*/

/**
 * @file doodle/doodle.h
 * @brief functions to access the doodle database
 * @author Christian Grothoff
 */

#ifndef DOODLE_H
#define DOODLE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>

/**
 * 0.2.6-1 => 0x00020601
 * 4.5.2-0 => 0x04050200
 */
#define DOODLE_VERSION 0x00060500

/* constants for the DOODLE_Logger callback */
#define DOODLE_LOG_CRITICAL 0
#define DOODLE_LOG_VERBOSE 1
#define DOODLE_LOG_VERY_VERBOSE 2
#define DOODLE_LOG_INSANELY_VERBOSE 3

/**
 * Type of a logger method.
 * @param level log-level, 0 for critical, 1 for verbose, 2 for very verbose
 * @param message the message string
 */
typedef void (*DOODLE_Logger)(void * context,
			      unsigned int level,
			      const char * message,
			      ...);

struct DOODLE_SuffixTree;

/**
 * The Doodle database stores per-file information.  This
 * struct encapsulates the filename and the modification
 * timestamp for each such file.  The timestamp is limited
 * to a 32 bit unsigned int.  Larger time-values are truncated
 * appropriately.
 */
typedef struct {
  char * filename;
  unsigned int mod_time;
} DOODLE_FileInfo;

/**
 * Obtain the number of files in the given doodle DB.
 */
unsigned int DOODLE_getFileCount(const struct DOODLE_SuffixTree * tree);

/**
 * Obtain the index-th file in the doodle DB.
 */
const DOODLE_FileInfo * DOODLE_getFileAt(const struct DOODLE_SuffixTree * tree,
					 unsigned int index);

/**
 * Create a suffix-tree (and store in file
 * named database).  Also used to re-open an existing
 * database for reading and writing.
 * @return NULL on error
 */
struct DOODLE_SuffixTree * DOODLE_tree_create(DOODLE_Logger log,
					      void * context,
					      const char * database);

/**
 * Open an existing database READ-ONLY.
 * @return NULL on error (i.e. DB does not exist)
 */
struct DOODLE_SuffixTree * DOODLE_tree_open_RDONLY(DOODLE_Logger log,
						   void * context,
						   const char * database);

/**
 * Destroy (and sync) suffix tree.
 */
void DOODLE_tree_destroy(struct DOODLE_SuffixTree * tree);

/**
 * Add keyword to suffix tree.
 * @return 0 on success, 1 on error
 */
int DOODLE_tree_expand(struct DOODLE_SuffixTree * tree,
		       const char * searchString,
		       const char * fileName);

/**
 * Remove all entries for the given filename.
 */
int DOODLE_tree_truncate(struct DOODLE_SuffixTree * tree,
			 const char * fileName);

/**
 * Remove all entries for the given filenames.
 * @param fileNames array of filenames, NULL terminated!
 */
int DOODLE_tree_truncate_multiple(struct DOODLE_SuffixTree * tree,
				  const char * fileNames[]);


/**
 * Check if all indexed files still exist and are
 * accessible.  If not, remove those entries.
 */
void DOODLE_tree_truncate_deleted(struct DOODLE_SuffixTree * tree,
				  DOODLE_Logger log,
				  void * logContext);

/**
 * First check if all indexed files still exist and are
 * up-to-date.  If not, remove those entries.
 */
void DOODLE_tree_truncate_modified(struct DOODLE_SuffixTree * tree,
				   DOODLE_Logger log,
				   void * logContext);

/**
 * Print the suffix tree.
 * @return 0 on success, 1 on error
 */
int DOODLE_tree_dump(FILE * stream,
		     struct DOODLE_SuffixTree * tree);

typedef void (*DOODLE_ResultCallback)(const DOODLE_FileInfo * fileinfo,
				      void * arg);

/**
 * Search the suffix tree for matching strings.
 *
 * @param substring the string to search for
 * @param callback function to call for each matching file
 * @param arg extra argument to callback
 * @return -1 on error, 1 on success
 */
int DOODLE_tree_search(struct DOODLE_SuffixTree * tree,
		       const char * substring,
		       DOODLE_ResultCallback callback,
		       void * arg);

/**
 * Search the suffix tree for matching strings.
 * The resulting nodes returned in result
 * MAY NOT be used after calls to DOODLE_tree_expand.
 *
 * @param pos pass the root node of the doodle tree
 * @param ignore_case for case-insensitive analysis
 * @param approx how many letters may we be off?
 * @param callback function to call for each matching file
 * @param arg extra argument to callback
 * @return -1 on error, 0 for pos==NULL, 1 on success
 */
int DOODLE_tree_search_approx(struct DOODLE_SuffixTree * tree,
			      const unsigned int approx,
			      const int ignore_case,
			      const char * ss,
			      DOODLE_ResultCallback callback,
			      void * arg);

/**
 * Change the memory limit (how much memory the
 * tree may use).  Note that the limit only refers
 * to the tree nodes, memory used for keywords and
 * filenames is excluded since the current code
 * does not support keeping those on disk.
 *
 * @param limit new memory limit in bytes
 */
void DOODLE_tree_set_memory_limit(struct DOODLE_SuffixTree * tree,
				  size_t limit);


#ifdef __cplusplus
}
#endif


#endif
