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
 * @file doodle/testio.h
 * @brief Testcase for IO
 * @author Christian Grothoff
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include "tree.c"

static void my_log(void * unused,
		   unsigned int level,
		   const char * msg,
		   ...) {
  va_list args;
  va_start(args, msg);
  vfprintf(stdout, msg, args);
  va_end(args);
  if (level == DOODLE_LOG_CRITICAL)
    abort();
}

int main(int argc,
	 char * argv[]) {
  BIO * bio;
  int fd;
  unsigned int v1;
  unsigned int v2;
  char * st;
  off_t pos;
  unsigned int i;

  unlink("/tmp/doodle_bio_test");
  fd = open("/tmp/doodle_bio_test",
	    O_CREAT | O_EXCL | O_RDWR,
	    S_IRWXU);
  if (fd == -1) {
    printf("Open failed: %s\n",
	   strerror(errno));
    return -1;
  }
  bio = IO_WRAP(&my_log,
		NULL,
		fd);

  writeZT(bio, "Hello World");
  for (i=0;i<1000;i++)
    WRITEUINT(bio, i*i);
  pos = LSEEK(bio, 0, SEEK_CUR);
  for (i=0;i<1000;i++)
    WRITEUINTPAIR(bio, i, i*i);
  writeZT(bio, "");
  LSEEK(bio, pos, SEEK_SET);
  for (i=0;i<1000;i++) {
    READUINTPAIR(bio, &v1, &v2);
    if ( (v1 != i) ||
	 (v2 != i*i) )
      return -1;
  }
  st = readZT(bio);
  if (0 != strcmp(st, ""))
    return -1;
  free(st);
  LSEEK(bio, 0, SEEK_SET);
  st = readZT(bio);
  if (0 != strcmp(st, "Hello World"))
    return -1;
  free(st);
  for (i=0;i<1000;i++) {
    READUINT(bio, &v1);
    if (v1 != i*i)
      return -1;
  }
  IO_FREE(bio);
#define EVAL 0
#if EVAL
  printf("Used %d bytes to store 1000 integer pairs.\n",
	 (int) (LSEEK(bio, 0, SEEK_END) - pos));
  /* typical eval: 5471 bytes, or 2.7 bytes / integer */
#endif
  unlink("/tmp/doodle_bio_test");
  return 0;
}

