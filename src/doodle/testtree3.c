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
 * @file doodle/testtree3.c
 * @brief Testcase for suffix tree, inserts a bunch of strings
 *  each for a different filename and then tests that all searches
 *  are successful
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

/* stress test swapping... */
#define MEMORY_LIMIT 1

#include "tree.c"

#define DBNAME "/tmp/doodle-tree-test"
#define TNAME "/tmp/doodle-tree-test-files"

#define ABORT() { printf("Assertion failed at %s:%d\n", __FILE__, __LINE__); return -1; }

static void my_log(void * unused,
		   unsigned int level,
		   const char * msg,
		   ...) {
  va_list args;
  if (level == 0) {
    va_start(args, msg);
    vfprintf(stdout, msg, args);
    va_end(args);
  }
}

static int pos;

typedef struct R {
  int pos;
  char * key;
  char * fn;
  struct R * next;
} Record;

static Record * records = NULL;

static void add(struct DOODLE_SuffixTree * tree,
		char * key) {
  Record * r;

  r = malloc(sizeof(Record));
  r->next = records;
  records = r;
  r->key = key;
  r->pos = pos++;
  r->fn = malloc(strlen(TNAME) + 20);
  sprintf(r->fn,
	  "%s.%d",
	  TNAME,
	  r->pos);
  fclose(fopen(r->fn, "a+"));
  DOODLE_tree_expand(tree,
		     key,
		     r->fn);
}

static int found;

static void testEquals(const DOODLE_FileInfo * fi,
		       void * arg) {
  if (0 == strcmp(fi->filename,
		  (char*) arg))
    found = 1;
}

int main(int argc,
	 char * argv[]) {
  struct DOODLE_SuffixTree * tree;
  Record * next;
  int i;
  static char * testStrings[] = {
    "foo",
    "f",
    NULL,
  };
  static char * testStrings2[] = {
    "zardine",
    NULL,
  };

  unlink(DBNAME);
  tree = DOODLE_tree_create(&my_log,
			    NULL,
			    DBNAME);
  i = 0;
  while (testStrings[i] != NULL)
    add(tree, testStrings[i++]);

  /* full serialization / deserialization */
  DOODLE_tree_destroy(tree);
  tree = DOODLE_tree_create(&my_log,
			    NULL,
			    DBNAME);

  /* truncate! */
  if (0 != DOODLE_tree_truncate(tree,
				records->fn))
    ABORT();

  /* check: no longer found! */
  found = 0;
  DOODLE_tree_search(tree,
		     records->key,
		     &testEquals,
		     records->fn);
  if (found != 0) {
    DOODLE_tree_dump(stderr,
		     tree);
    ABORT();
  }
  next = records->next;
  free(records);
  records = next;

  i = 0;
  while (testStrings2[i] != NULL)
    add(tree, testStrings2[i++]);

  /* full serialization / deserialization */
  DOODLE_tree_destroy(tree);
  tree = DOODLE_tree_create(&my_log,
			    NULL,
			    DBNAME);

  /* test! */
  while (records != NULL) {
    next = records->next;
    found = 0;
    DOODLE_tree_search(tree,
		       records->key,
		       &testEquals,
		       records->fn);
    if (found == 0) {
      DOODLE_tree_dump(stderr,
		       tree);
      ABORT();
    }
    unlink(records->fn);
    free(records->fn);
    free(records);
    printf(".");
    records = next;
  }

  DOODLE_tree_destroy(tree);
  unlink(DBNAME);
  printf(" Ok.\n");
  return 0;
}
