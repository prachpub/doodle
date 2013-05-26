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
 * @file doodle/testtree.c
 * @brief Testcase for suffix tree
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
#include "tree.c"

#ifndef MINGW
 #define DIR_SEPARATOR '/'
 #define DIR_SEPARATOR_STR "/"
#else
 #define DIR_SEPARATOR '\\'
 #define DIR_SEPARATOR_STR "\\"
#endif

#define DBNAME "/tmp/doodle-tree-test"

#define ABORT() { printf("Assertion failed at %s:%d\n", __FILE__, __LINE__); return -1; }

static void checkFN(const DOODLE_FileInfo * fn,
		    void * arg) {
  if (0 != strcmp(fn->filename,
		  DBNAME)) {
    printf("Assertion failed at %s:%d\n", __FILE__, __LINE__);
    abort();
  }
}

static void decrementor(const DOODLE_FileInfo * fn,
			int * arg) {
  (*arg)--;
}

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
  unsigned int nc;
  char * exp;

  exp = expandFileName(argv[0]);
  unlink(DBNAME);
  tree = DOODLE_tree_create(&my_log,
			    NULL,
			    DBNAME);
  if (0 != DOODLE_tree_expand(tree,
			      "abcdefg",
			      exp))
    ABORT();
  nc = 1;
  if ( (1 != DOODLE_tree_search_approx(tree,
				       1,
				       0,
				       "abcefg",
				       (DOODLE_ResultCallback)&decrementor,
				       &nc)) ||
       (nc != 0) )
    ABORT();
  nc = 1;
  if ( (1 != DOODLE_tree_search_approx(tree,
				       1,
				       1,
				       "abCefg",
				       (DOODLE_ResultCallback)&decrementor,
				       &nc)) ||
       (nc != 0) )
    ABORT();
  if ( (0 != DOODLE_tree_search_approx(tree,
				       1,
				       0,
				       "abCefg",
				       (DOODLE_ResultCallback)&decrementor,
				       &nc)) ||
       (nc != 0) )
    ABORT();

  DOODLE_tree_destroy(tree);
  tree = DOODLE_tree_create(&my_log,
			    NULL,
			    DBNAME);
  if (0 != DOODLE_tree_expand(tree,
			      "xYz",
			      DBNAME))
    ABORT();
  nc = 1;
  if ( (1 != DOODLE_tree_search(tree,
				"abc",
				(DOODLE_ResultCallback)&decrementor,
				&nc)) ||
       (nc != 0) )
    ABORT();
  if (0 != DOODLE_tree_search(tree,
			      "bcd",
			       NULL,
			       NULL))
    ABORT();
  if (0 != DOODLE_tree_truncate(tree,
				exp))
    ABORT();
  if (0 != DOODLE_tree_search(tree,
			      "abc",
			      NULL,
			      NULL))
    ABORT();
  if (1 != DOODLE_tree_search_approx(tree,
				     0,
				     0,
				     "xYz",
				     NULL,
				     NULL))
    ABORT();
  if (0 != DOODLE_tree_search_approx(tree,
				     0,
				     0,
				     "xyz",
				     NULL,
				     NULL))
    ABORT();
  if (1 != DOODLE_tree_search_approx(tree,
				     0,
				     1,
				     "xyz",
				     NULL,
				     NULL))
    ABORT();

  if (1 != DOODLE_tree_search_approx(tree,
				     1,
				     0,
				     "xyz",
				     NULL,
				     NULL))
    ABORT();
  if (1 != DOODLE_tree_search_approx(tree,
				     1,
				     0,
				     "xz",
				     NULL,
				     NULL))
    ABORT();
  if (1 != DOODLE_tree_search_approx(tree,
				     1,
				     0,			
				     "xYxz",
				     (DOODLE_ResultCallback)&checkFN,
				     NULL))
    ABORT();
  if (0 != DOODLE_tree_search_approx(tree,
				     1,
				     0,
				     "xYxxz",
				     NULL,
				     NULL))
    ABORT();

  DOODLE_tree_destroy(tree);
  unlink(DBNAME);
  tree = DOODLE_tree_create(&my_log,
			    NULL,
			    DBNAME);
  DOODLE_tree_expand(tree,
		     "1998-foo",
		     DBNAME);
  DOODLE_tree_expand(tree,
		     "1998,2000",
		     DBNAME);
  DOODLE_tree_expand(tree,
		     "1999-bar",
		     DBNAME);
  if (1 != DOODLE_tree_search(tree,
			      "1998-foo",
			      NULL,
			      NULL))
    ABORT();
  DOODLE_tree_destroy(tree);

  unlink(DBNAME);
  tree = DOODLE_tree_create(&my_log,
			    NULL,
			    DBNAME);
  DOODLE_tree_expand(tree,
		     "1999-bar",
		     DBNAME);
  DOODLE_tree_expand(tree,
		     "1999-ba",
		     DBNAME);
  if (2 != DOODLE_tree_search(tree,
			      "1999-ba",
			      NULL,
			      NULL))
    ABORT();
  DOODLE_tree_destroy(tree);
  unlink(DBNAME);

  tree = DOODLE_tree_create(&my_log,
			    NULL,
			    DBNAME);
  DOODLE_tree_expand(tree,
		     "CAAAAA",
		     DBNAME);
  DOODLE_tree_expand(tree,
		     "CCAAAA",
		     DBNAME);
  DOODLE_tree_expand(tree,
		     "CCCAAA",
		     DBNAME);
  DOODLE_tree_expand(tree,
		     "CCCCAA",
		     DBNAME);
  DOODLE_tree_destroy(tree);

  unlink(DBNAME);
  free(exp);
  return 0;
}
