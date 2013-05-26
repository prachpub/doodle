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
 * @file doodle/logreplay.c
 * @brief Replay a list of logged keywords back to the tree for
    testing
 * @author Christian Grothoff
 */

#include "config.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <stdarg.h>
#include <unistd.h>
#include "doodle.h"
#include "gettext.h"
#include "helper2.h"

/* stress test swapping... */
// #define MEMORY_LIMIT 50000

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

#define DBNAME "/tmp/doodle-tree-test"
#define TNAME "/tmp/doodle-tree-test-file"

#define ABORT() { printf("Assertion failed at %s:%d\n", __FILE__, __LINE__); return -1; }

static void my_log(void * unused,
		   unsigned int level,
		   const char * msg,
		   ...) {
  va_list args;
  if (level == DOODLE_LOG_INSANELY_VERBOSE)
    return;
  va_start(args, msg);
  vfprintf(stdout, msg, args);
  va_end(args);
}

static void add(struct DOODLE_SuffixTree * tree,
		char * key) {
  size_t slen;

  slen = strlen(key);
  if (slen > MAX_LENGTH) {
    char section[MAX_LENGTH+1];
    char * xpos;
    int j;

    for (j=0;j<slen;j+=MAX_LENGTH/2) {
      strncpy(section,
	      &key[j],
	      MAX_LENGTH);
      section[MAX_LENGTH] = '\0';
      xpos = &section[0];
      while (xpos[0] != '\0') {
	if (0 != DOODLE_tree_expand(tree,
				    xpos,
				    TNAME)) {
	  printf("ERROR!\n");
	  exit(-1);
	  return;
	}
	xpos++;
      }
    }
  } else {
    while (key[0] != '\0') {
      if (0 != DOODLE_tree_expand(tree,
				  key,
				  TNAME)) {
	printf("ERROR!\n");
	exit(-1);
	return;
      }
      key++;
    }
  }
}

#define MAX_KEYWORD_LEN 65536

int main(int argc,
	 char * argv[]) {
  struct DOODLE_SuffixTree * tree;
  FILE * in;
  char key[MAX_KEYWORD_LEN+1];
  int i;

  if (argc != 2) {
    fprintf(stderr,
	    "Call with log file as argument!\n");
    return -1;
  }
  unlink(DBNAME);
  tree = DOODLE_tree_create(&my_log,
			    NULL,
			    DBNAME);
  fclose(fopen(TNAME, "a+"));
  in = fopen(argv[1], "r");
  if (in == NULL) {
    fprintf(stderr,
	    "Could not open file '%s' for reading.\n",
	    argv[1]);
    return -1;
  }
  i = 0;
  while (NULL != fgets(key, MAX_KEYWORD_LEN, in)) {
    key[strlen(key)-1] = '\0'; /* kill newline */
    i++;
    if ( (i % 1000) == 0)
      printf("Processed %8u keywords...\n", i);
    add(tree, key);
#if VERBOSE
    printf("ADDING: %s\n", key);
    DOODLE_tree_dump(stdout, tree);
#endif
  }
  fclose(in);

  /* full serialization / deserialization */
  DOODLE_tree_destroy(tree);
  tree = DOODLE_tree_create(&my_log,
			    NULL,
			    DBNAME);
  DOODLE_tree_truncate(tree,
		       TNAME);
  DOODLE_tree_destroy(tree);
  unlink(DBNAME);
  unlink(TNAME);
  printf("Done.\n");
  return 0;
}
