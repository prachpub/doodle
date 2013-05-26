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
 * @file files.c
 * @brief helper functions that deal with files
 * @author Christian Grothoff
 */

#include "config.h"
#include "helper2.h"
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <dirent.h>
#include "gettext.h"


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
		  void * cb_arg) {
  DIR * dinfo;
  struct dirent *finfo;
  struct stat istat;
  int count = 0;

  if (dirName == NULL) {
    /* dirName==NULL */
    if (logger != NULL)
      logger(context,
	     DOODLE_LOG_CRITICAL,
	     _("Assertion failed at %s:%d.\n"),
	     __FILE__, __LINE__);
    return -1;
  }
  if (0 != lstat(dirName, &istat)) {
    if (logger != NULL)
      logger(context,
	     DOODLE_LOG_VERBOSE,
	     _("Call to '%s' for file '%s' failed: %s\n"),
	     "lstat",
	     dirName,
	     strerror(errno));
    return 0;
  }
#ifdef S_ISLNK
  if (S_ISLNK(istat.st_mode))
    return 0;
#endif
#ifdef S_ISSOCK
  if (S_ISSOCK(istat.st_mode))
    return 0;
#endif
  if (S_ISCHR(istat.st_mode) ||
      S_ISBLK(istat.st_mode) ||
      S_ISFIFO(istat.st_mode) )
    return 0;
  if ( (S_ISREG(istat.st_mode)) ||
       (S_ISDIR(istat.st_mode)) )
    if (-1 == callback(dirName,
		       cb_arg))
      return -1;
  if (! S_ISDIR(istat.st_mode))
    return 0;
  if (logger != NULL)
    logger(context,
	   DOODLE_LOG_VERBOSE,
	   _("Scanning '%s'\n"), dirName);
  errno = 0;
  dinfo = opendir(dirName);
  if ((errno == EACCES) || (dinfo == NULL)) {
    if (errno == EACCES)
      if (logger != NULL)
	logger(context,
	       DOODLE_LOG_VERBOSE,
	       _("Access to directory '%s' was denied.\n"),
	       dirName);
    return 0;
  }
  while ((finfo = readdir(dinfo)) != NULL) {
    char * dn;
    int ret;

    if (finfo->d_name[0] == '.')
      continue;
    dn = MALLOC(strlen(dirName) +
		strlen(finfo->d_name) + 2);
    strcpy(dn, dirName);
    if (dirName[strlen(dirName)-1] != DIR_SEPARATOR)
      strcat(dn, DIR_SEPARATOR_STR);
    strcat(dn, finfo->d_name);
    if (0 == pruner(dn,
		    pr_arg)) {
      ret = scanDirectory(dn,
			  logger,
			  context,
			  pruner,
			  pr_arg,
			  callback,
			  cb_arg);
    } else {
      ret = 0;
    }
    free(dn);
    if (ret >= 0)
      count += ret;
    else
      return -1;
  }
  closedir(dinfo);
  return count;
}


/**
 * Complete filename (a la shell) from abbrevition.
 * @param fil the name of the file, may contain ~/ or
 *        be relative to the current directory
 * @returns the full file name,
 *          NULL is returned on error
 */
char * expandFileName(const char * fil) {
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
