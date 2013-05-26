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
 * @file doodle/testtree.h
 * @brief Testcase for suffix tree
 * @author Christian Grothoff
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <stdarg.h>
#include <unistd.h>
#include <time.h>
#include "doodle.h"
#include "tree.c"

#define DBNAME "/tmp/doodle-tree-test"

#define DEPTH 6
#define DIST 4
#define COUNT 100000


#ifndef MINGW
 #define DIR_SEPARATOR '/'
 #define DIR_SEPARATOR_STR "/"
#else
 #define DIR_SEPARATOR '\\'
 #define DIR_SEPARATOR_STR "\\"
#endif

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


/**
 * Complete filename (a la shell) from abbrevition.
 * @param fil the name of the file, may contain ~/ or
 *        be relative to the current directory
 * @returns the full file name,
 *          NULL is returned on error
 */
static char * expandFileName(const char * fil) {
  char buffer[512];
  char * fn;
#ifndef MINGW
  char * fm;
  const char *fil_ptr;
#else
  long lRet;
#endif

  if (fil == NULL)
    return NULL;

#ifndef MINGW
  if (fil[0] == DIR_SEPARATOR) {
    /* absolute path, just copy */
    return STRDUP(fil);
  }
  if (fil[0] == '~') {
    fm = getenv("HOME");
    if (fm == NULL) {
      /* keep it symbolic to show error to user! */
      fm = "$HOME";
    }

    /* do not copy '~' */
    fil_ptr = fil + 1;

	/* skip over dir seperator to be consistent */
    if (fil_ptr[0] == DIR_SEPARATOR)
      fil_ptr++;
  } else {
    fil_ptr = fil;
    if (getcwd(buffer, 512) != NULL)
      fm = buffer;
    else
      fm = "$PWD";
  }
  fn = MALLOC(strlen(fm) + 1 + strlen(fil_ptr) + 1);

  sprintf(fn, "%s/%s", fm, fil_ptr);
#else
  fn = MALLOC(MAX_PATH + 1);

  if ((lRet = conv_to_win_path(fil, buffer)) != ERROR_SUCCESS)
  {
  	SetErrnoFromWinError(lRet);

    return NULL;
  }
  /* is the path relative? */
  if ((strncmp(buffer + 1, ":\\", 2) != 0) &&
      (strncmp(buffer, "\\\\", 2) != 0))
  {
    char szCurDir[MAX_PATH + 1];
    lRet = GetCurrentDirectory(MAX_PATH + 1, szCurDir);
    if (lRet + strlen(fn) + 1 > (_MAX_PATH + 1))
    {
      SetErrnoFromWinError(ERROR_BUFFER_OVERFLOW);

      return NULL;
    }
    sprintf(fn, "%s\\%s", szCurDir, buffer);
  }
  else
  {
    strcpy(fn, buffer);
  }
#endif
  return fn;
}

int main(int argc,
	 char * argv[]) {
  struct DOODLE_SuffixTree * tree;
  char astring[DEPTH+1];
  char bstring[DEPTH+DIST+1];
  int i;
  int j;
  time_t start;
  time_t end;
  int pow;
  char * exp;

  exp = expandFileName(argv[0]);
  unlink(DBNAME);
  tree = DOODLE_tree_create(&my_log,
			    NULL,
			    DBNAME);
  time(&start);
  memset(astring, 0, DEPTH+1);
  pow = 1;
  for (i=0;i<DEPTH;i++) {
    pow = pow * ('H' - 'A');
    astring[i] = 'A';
  }
  for (i=0;i<pow-1;i++) {
    j = 0;
    do {
      astring[j++]++;
    } while (astring[j-1] == 'H');
    j--;
    while (j > 0)
      astring[--j] = 'A';

    if (0 != DOODLE_tree_expand(tree,
				astring,
				exp))
      ABORT();
  }
  time(&end);
  printf("%d expansions took %us.\n",
	 pow,
	 (unsigned int) (end - start));

  for (i=0;i<DEPTH+DIST;i++)
    bstring[i] = 'A';
  bstring[DEPTH+DIST] = '\0';
  time(&start);
  for (i=0;i<COUNT;i++)
    if (0 != DOODLE_tree_search(tree,
				bstring,
				NULL,
				NULL))
      ABORT();
  time(&end);
  printf("%d (cached) searches took %us.\n",
	 COUNT,
	 (unsigned int) (end - start));

  for (j=0;j<DIST-1;j++) {
    time(&start);
    for (i=0;i<COUNT;i++)
      if (0 != DOODLE_tree_search_approx(tree,
					 j,
					 0,
					 bstring,
					 NULL,
					 NULL))
	ABORT();
    time(&end);
    printf("%d (cached) %d-searches took %us.\n",
	   COUNT,
	   j,
	   (unsigned int) (end - start));
  }
  DOODLE_tree_destroy(tree);
  unlink(DBNAME);
  free(exp);
  return 0;
}
