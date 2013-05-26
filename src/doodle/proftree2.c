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
 * @file doodle/proftree2.c
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
 * Problem size.  Note that the doodle CLI limits search
 * strings by default to a size of 64/128 characters.
 */
#define SIZE 10000

#define DBNAME "/tmp/doodle-tree-test"


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


int main(int argc,
	 char * argv[]) {
  struct DOODLE_SuffixTree * tree;
  time_t start;
  time_t end;

  int i;
  char * test;

  test = malloc(SIZE+1);
  test[SIZE] = '\0';
  for (i=SIZE-1;i>=0;i--)
    test[i] = 'A' + (lrand48() % 26);

  unlink(DBNAME);
  time(&start);
  tree = DOODLE_tree_create(&my_log,
			    NULL,
			    DBNAME);
  for (i=SIZE-1;i>=0;i--) {
    DOODLE_tree_expand(tree,
		       &test[i],
		       DBNAME);
  }

  /* full serialization / deserialization */
  DOODLE_tree_destroy(tree);
  unlink(DBNAME);
  time(&end);
  free(test);
  printf("%d expansions took %us.\n",
	 SIZE,
	 (unsigned int) (end - start));
  return 0;
}
