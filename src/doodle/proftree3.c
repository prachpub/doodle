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
 * @file doodle/proftree3.c
 * @brief Profiling suffix tree insertion
 * @author Christian Grothoff
 */

#include "config.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include "doodle.h"
#include "gettext.h"

/**
 * The memory limit has a dramatic impact on performance,
 * for example (on my machine):
 *   1 MB => 870s
 *   8 MB =>  15s  <-- default!
 *  64 MB =>   4s
 * 128 MB =>   3s
 */
#define MEMORY_LIMIT (8 * 1024 * 1024)
#include "tree.c"

/**
 * Size of the strings.
 */
#define SIZE 10
#define FILECOUNT 50
#define ITERATIONS 10000
#define REPEAT 50

#define DBNAME "/tmp/doodle-tree-test"

static void my_log(void * unused,
		   unsigned int level,
		   const char * msg,
		   ...) {
  va_list args;
  if ( (0) ||
       (level == 0) ) {
    va_start(args, msg);
    vfprintf(stdout, msg, args);
    va_end(args);
  }
}

static int runTest() {
  struct DOODLE_SuffixTree * tree;
  int i;
  int j;
  char * test;
  char * fn;

  tree = DOODLE_tree_create(&my_log,
			    NULL,
			    DBNAME);
  test = malloc(SIZE+1);
  fn = malloc(strlen(DBNAME) + 20);
  DOODLE_tree_set_memory_limit(tree,
			       1024 * (rand() % 1024));
  for (i=0;i<ITERATIONS;i++) {
    sprintf(fn,
	    "%s.%u",
	    DBNAME,
	    rand() % FILECOUNT);
    fclose(fopen(fn, "a+"));

    if ((rand() % 100) == 0) {
      DOODLE_tree_truncate(tree,
			   fn);
    } else {
      memset(test, 0, SIZE+1);
      for (j=rand() % SIZE;j>=0;j--)
	test[j] = 'A' + (rand() % 26);

      DOODLE_tree_expand(tree,
			 test,
			 fn);
    }
  }
  DOODLE_tree_destroy(tree);
  unlink(DBNAME);
  for (i=0;i<FILECOUNT;i++) {
    sprintf(fn,
	    "%s.%u",
	    DBNAME,
	    rand() % FILECOUNT);
    unlink(fn);
  }
  free(fn);
  free(test);
  return 0;
}


int main(int argc,
	 char * argv[]) {
  time_t start;
  time_t end;
  int i;

  unlink(DBNAME);
  time(&start);
  for (i=0;i<REPEAT;i++) {
    fprintf(stderr, ".");
    srand(i);
    if (0 != runTest()) {
      fprintf(stderr,
	      "\nTest %d failed.\n",
	     i);
      return 1;
    }
  }
  time(&end);
  fprintf(stderr, "\n");
  printf("%d operations took %us.\n",
	 ITERATIONS * REPEAT,
	 (unsigned int) (end - start));

  return 0;
}
