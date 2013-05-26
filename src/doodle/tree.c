/*
     This file is part of doodle.
     (C) 2004, 2007 Christian Grothoff (and other contributing authors)

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
 * @file doodle/tree.c
 * @brief suffix tree implementation (libdoodle)
 * @author Christian Grothoff
 *
 * Note that this file was deliberately written such that all exported
 * symbols have the prefix DOODLE_ (or Java_org_gnunet_doodle_Doodle for
 * the JNI API).  Furthermore, there are no symbols that are
 * accidentially exported.  When editing, please make sure that this
 * is preserved.
 *
 * Also a small warning here.  This code started as a reasonably pretty
 * implementation but has since then been extended with many bells and
 * whistles to improve performance (not in terms of adding assembler but
 * in terms of making the format and the algorithm smarter).  This means
 * that the code is no longer really easy to read.
 */

#include "config.h"
#include "doodle.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include "helper1.h"
#include "gettext.h"

/* The following two options maybe of interest for people who
   compile doodle for embedded devices.  For everyone else,
   the defaults should be fine. */

/**
 * Option to disable the CI cache.  The cache slightly increases
 * memory requirements (by less than 10%) but significantly reduces
 * the time to serialize the tree.  So unless you need a version of
 * doodle for embedded devices with pre-build trees (where users would
 * only search on a fixed index and never re-build it), you probably
 * want the cache enabled.  The cache has no effect on the database
 * format.
 */
#define USE_CI_CACHE 1

/**
 * Default memory limit.
 *
 * Try not to use more than 8 MB for nodes.  Note that additional
 * memory will be used for various auxiliary datastructures (keywords
 * and filenames, mostly), but it would be rather difficult (and
 * expensive) to limit that memory.  Also, hopefully, nodes are likely
 * to be the bulk of the data.  A smaller limit will result in more
 * frequent serializations of the data from memory to disk while
 * indexing, which will result in both reduced performance and
 * increased disk space requirements (in effect, if we run above
 * the limit k-times, we are going to serialize a certain portion of
 * the tree k-times -- using fresh space each time.  Worse, when
 * writing the final database we'll have to read all of it back into
 * memory.  Hence it's best to be generous with the limit, and today
 * I believe any reasonable machine has 8 MB to spare for that.
 *
 * Note that even with an 8 MB limit here Doodle can still easily
 * take 64 MB total (if the number of files and keywords is large
 * enough).
 */
#ifndef MEMORY_LIMIT
#define MEMORY_LIMIT (8 * 1024 * 1024)
#endif

/**
 * Window-size for IO.  Doodle will try to read and write in chunks
 * of this size.  This can significantly reduce the IO overhead,
 * but it of course costs a bit of memory -- and we may read more
 * than we need if the number is too large.  The number should never
 * be smaller than the block-size of the underlying file system,
 * so 512 bytes is definitively a hard lower limit, 4092 bytes is
 * a sane common value and 65536 bytes is definitively the upper limit
 * for most systems.
 *
 * The value must be greater than 2 otherwise we will produce a division
 * by zero in the code (in the optimized-read mode, the code will
 * try to align reads to half the BUF_SIZE!).
 */
#ifndef BUF_SIZE
#define BUF_SIZE 4096
#endif

/* ***************** debug options, toggle to use simpler variants
   of the code or to enable more checking *********************** */

/**
 * Run in debug mode.  Enables some very expensive checks.  Also
 * enables aborts (that is, assertion failures may lead to calls
 * to abort instead of returning error codes).  Never use in a
 * production setting.
 */
#define DEBUG 0

/**
 * Perform inexpensive extra checks for correctness.  These
 * are not input/output checks but cheap checks for datastructure
 * invariants.  While they are cheap there are still plenty of them,
 * so for a production system we probably want to turn them off.
 */
#define ASSERTS 0

/**
 * Should the new, experimental read-optimization code be used?
 * This will try to align file-reads and also optimize the
 * location of the read-window (read-ahead).
 */
#define OPTIMIZE_READS 1

/**
 * Optimize expand by being better at recycling keyword slots.
 */
#define OPTIMIZE_SPACE 1

/**
 * Selectively disable read/write caching code.
 */
#define DEBUG_READ 0
#define DEBUG_WRITE 0

typedef char * pchar;


/* **************** IO ********************* */

/**
 * @brief wrapper around a file-handle to allow
 *  buffered IO operations that are tailored to doodle.
 */
typedef struct {
  DOODLE_Logger log;
  void * context;
  int fd;
  unsigned long long off;
  unsigned long long fsize;
  unsigned long long bstart;
  unsigned long long bsize;
  char * buffer;
  unsigned long long dirty;
} BIO;

static int read_buf(DOODLE_Logger log,
		    void * context,
		    int fd,
		    unsigned long long off,
		    char * buf,
		    unsigned long long cnt) {
  int ret;
  if (off != lseek(fd, off, SEEK_SET)) {
    log(context,
	DOODLE_LOG_CRITICAL,
	_("Call to '%s' failed: %s\n"),
	"lseek", strerror(errno));
  }
  ret = read(fd, buf, cnt);
  if (cnt != ret) {
    if (ret == -1) {
      log(context,
	  DOODLE_LOG_CRITICAL,
	  _("Call to '%s' failed: %s\n"),
	  "read", strerror(errno));
    } else {
      log(context,
	  DOODLE_LOG_CRITICAL,
	  _("Short read at offset %llu (attempted to read %llu bytes).\n"),
	  off, cnt);
    }
    return -1;
  }
  return 0;
}

static void write_buf(DOODLE_Logger log,
		      void * context,
		      int fd,
		      unsigned long long off,
		      const void * buf,
		      unsigned long long cnt) {
  int ret;
  if (off != lseek(fd, off, SEEK_SET)) {
    log(context,
	DOODLE_LOG_CRITICAL,
	_("'%s' failed: %s\n"),
	"lseek", strerror(errno));
  }
  ret = write(fd, buf, cnt);
  if (cnt != ret) {
    if (ret == -1) {
      log(context,
	  DOODLE_LOG_CRITICAL,
	  _("Call to '%s' failed: %s\n"),
	  "write",
	  strerror(errno));
    } else {
      log(context,
	  DOODLE_LOG_CRITICAL,
	  _("Short write at offset %llu (wanted to write %llu bytes).\n"),
	  off, cnt);
    }
  }
}

static BIO * IO_WRAP(DOODLE_Logger log,
		     void * context,
		     int fd) {
  BIO * bio;
  struct stat buf;

  if (0 != fstat(fd, &buf))
    log(context,
	DOODLE_LOG_CRITICAL,
	_("Call to '%s' failed: %s\n"),
	"fstat",
	strerror(errno));
  bio = MALLOC(sizeof(BIO));
  bio->log = log;
  bio->context = context;
  bio->fd = fd;
  bio->off = 0;
  bio->buffer = MALLOC(BUF_SIZE);
  bio->bsize = 0;
  bio->bstart = 0;
  bio->fsize = buf.st_size;
  bio->dirty = 0;
  return bio;
}

static void flush_buffer(BIO * bio) {
  if (bio->dirty) {
    write_buf(bio->log,
	      bio->context,
	      bio->fd,
	      bio->bstart,
	      bio->buffer,
	      bio->dirty);
    bio->dirty = 0;
  }
}

static int retarget_buffer(BIO * bio,
			   unsigned long long off,
			   unsigned long long len) {
  unsigned long long min;
  int ret;
#if OPTIMIZE_READS
  unsigned int opt_off;

  opt_off = (off / (BUF_SIZE/2)) * (BUF_SIZE/2); /* round down! */
  if (opt_off + BUF_SIZE >= off + len)
    off = opt_off; /* can optimize */
#endif
  flush_buffer(bio);
  min = (bio->fsize - off > BUF_SIZE) ? BUF_SIZE : bio->fsize - off;
  bio->bsize = min;
  bio->bstart = off;
  ret = read_buf(bio->log,
		 bio->context,
		 bio->fd,
		 bio->bstart,
		 bio->buffer,
		 min);
  return ret;
}


static int READALL(BIO * bio,
		   void * buf,
		   unsigned long long len) {
  int ret;
#if DEBUG_READ
  ret = read_buf(bio->log,
		 bio->context,
		 bio->fd,
		 bio->off,
		 buf,
		 len);
#else
  if (len > BUF_SIZE) {
    flush_buffer(bio);
    ret = read_buf(bio->log,
		   bio->context,
		   bio->fd,
		   bio->off,
		   buf,
		   len);
    bio->off += len;
    return ret;
  }
  ret = 0;
  if ( (bio->off < bio->bstart) ||
       (bio->off + len > bio->bstart + bio->bsize) )
    ret = retarget_buffer(bio,
			  bio->off,
			  len);
  if ( (bio->off < bio->bstart) ||
       (bio->off + len > bio->bstart + bio->bsize) ) {
    bio->log(bio->context,
	     DOODLE_LOG_CRITICAL,
	     _("Assertion failed at %s:%d.\n"),
	     __FILE__, __LINE__); /* index out of bounds */
    return -1;
  }
  memcpy(buf,
	 &bio->buffer[bio->off - bio->bstart],
	 len);
#endif
  bio->off += len;
  return ret;
}

static void WRITEALL(BIO * bio,
		     const void * buf,
		     unsigned long long len) {
#if DEBUG_WRITE
  write_buf(bio->log,
	    bio->context,
	    bio->fd,
	    bio->off,
	    buf,
	    len);
  bio->off += len;
#else
  if (len > BUF_SIZE) {
    flush_buffer(bio);
    write_buf(bio->log,
	      bio->context,
	      bio->fd,
	      bio->off,
	      buf,
	      len);
    bio->off += len;
    return;
  }
  if ( (bio->off < bio->bstart) ||
       (bio->off != bio->bstart + bio->dirty) ||
       (bio->off + len > bio->bstart + BUF_SIZE) ) {
    flush_buffer(bio);
    bio->bsize = len;
    bio->bstart = bio->off;
  }
  memcpy(&bio->buffer[bio->off - bio->bstart],
	 buf,
	 len);
  bio->dirty += len;
  bio->off += len;
#endif
  if (bio->off > bio->fsize)
    bio->fsize = bio->off;
}

static unsigned long long LSEEK(BIO * bio,
				unsigned long long off,
				int whence) {
  switch (whence) {
  case SEEK_SET:
    bio->off = off;
    return off;
  case SEEK_END:
    bio->off = bio->fsize;
    return bio->fsize;
  case  SEEK_CUR:
    bio->off += off;
    return bio->off;
  default:
    return (unsigned long long) -1;
  }
}

static void IO_FREE(BIO * bio) {
  flush_buffer(bio);		
  close(bio->fd);
  free(bio->buffer);
  free(bio);
}

static int READUINT(BIO * fd,
		    unsigned int * val) {
  signed char c;
  signed char d;
  unsigned char v[4];
  if (-1 == READALL(fd, &c, sizeof(signed char)))
    return -1;
  if ( (c > 4) || (c < 0) ) {
    fd->log(fd->context,
	    DOODLE_LOG_CRITICAL,
	    _("Assertion failed at %s:%d.\nDatabase format error!\n"),
	    __FILE__, __LINE__);
    return -1;
  }
  *val = 0;
  if (-1 == READALL(fd, &v[0], (int)c))
    return -1;
  for (d=c-1;d>=0;d--)
    (*val) += (v[(unsigned char)d] << (8*d));
  return 0;
}

static int READULONG(BIO * fd,
		     unsigned long long * val) {
  signed char c;
  signed char d;
  unsigned char v[8];
  if (-1 == READALL(fd, &c, sizeof(signed char)))
    return -1;
  if ( (c > 8) || (c < 0) ) {
    fd->log(fd->context,
	    DOODLE_LOG_CRITICAL,
	    _("Assertion failed at %s:%d.\nDatabase format error!\n"),
	    __FILE__, __LINE__);
    return -1;
  }
  *val = 0;
  if (-1 == READALL(fd, &v[0], (int)c))
    return -1;
  for (d=c-1;d>=0;d--)
    (*val) += (((unsigned long long)v[(unsigned char)d]) << (8*d));
  return 0;
}

static int READULONGFULL(BIO * fd,
                         unsigned long long * val) {
  unsigned int d, e;
  unsigned char v[8];
  *val = 0;
  if (-1 == READALL(fd, &v[0], 8))
    return -1;
  for (d=0,e=7;d<8;d++,e--)
    (*val) += (((unsigned long long)v[d]) << (8*e));
  return 0;
}

static int READUINTPAIR(BIO * fd,
			unsigned int * val1,
			unsigned int * val2) {
  unsigned char c;
  signed char d;
  unsigned char v[4];

  if (-1 == READALL(fd, &c, sizeof(unsigned char)))
    return -1;
  if ( ((c & 15) > 4) || ( (c>>4) > 4) ) {
    fd->log(fd->context,
	    DOODLE_LOG_CRITICAL,
	    _("Assertion failed at %s:%d.\nDatabase format error!\n"),
	    __FILE__, __LINE__);
    return -1;
  }
  *val1 = 0;
  *val2 = 0;
  if (-1 == READALL(fd, &v[0], (unsigned char) c & 15))
    return -1;
  for (d=(c&15)-1;d>=0;d--)
    (*val2) += (v[(unsigned char)d] << (8*d));
  if (-1 == READALL(fd, &v[0], (unsigned char) c >> 4))
    return -1;
  for (d=(c>>4)-1;d>=0;d--)
    (*val1) += (v[(unsigned char)d] << (8*d));
  return 0;
}

static int READULONGPAIR(BIO * fd,
			 unsigned long long * val1,
			 unsigned long long * val2) {
  unsigned char c;
  signed char d;
  unsigned char v[8];

  if (-1 == READALL(fd, &c, sizeof(unsigned char)))
    return -1;
  if ( ((c & 15) > 8) || ( (c>>4) > 8) ) {
    fd->log(fd->context,
	    DOODLE_LOG_CRITICAL,
	    _("Assertion failed at %s:%d.\nDatabase format error!\n"),
	    __FILE__, __LINE__);
    return -1;
  }
  *val1 = 0;
  *val2 = 0;
  if (-1 == READALL(fd, &v[0], (unsigned char) c & 15))
    return -1;
  for (d=(c&15)-1;d>=0;d--)
    (*val2) += (((unsigned long long)v[(unsigned char)d]) << (8*d));
  if (-1 == READALL(fd, &v[0], (unsigned char) c >> 4))
    return -1;
  for (d=(c>>4)-1;d>=0;d--)
    (*val1) += (((unsigned long long)v[(unsigned char)d]) << (8*d));
  return 0;
}

static void WRITEUINTPAIR(BIO * fd,
			  unsigned int val1,
			  unsigned int val2) {
  unsigned char c;
  signed char d;
  unsigned char v[4];
  unsigned int xval;

  xval = val1;
  c = 0;
  while (xval > 0) {
    xval = xval >> 8;
    c++;
  }
  c = c << 4;
  xval = val2;
  while (xval > 0) {
    xval = xval >> 8;
    c++;
  }
  WRITEALL(fd, &c, sizeof(unsigned char));
  for (d=(c&15)-1;d>=0;d--)
    v[(unsigned char)d] = (unsigned char) (val2 >> (8*d));
  WRITEALL(fd, &v[0], (unsigned char) c & 15);
  for (d=(c>>4)-1;d>=0;d--)
    v[(unsigned char)d] = (unsigned char) (val1 >> (8*d));
  WRITEALL(fd, &v[0], (unsigned char) c >> 4);
}

static void WRITEULONGPAIR(BIO * fd,
			   unsigned long long val1,
			   unsigned long long val2) {
  unsigned char c;
  signed char d;
  unsigned char v[8];
  unsigned long long xval;

  xval = val1;
  c = 0;
  while (xval > 0) {
    xval = xval >> 8;
    c++;
  }
  c = c << 4;
  xval = val2;
  while (xval > 0) {
    xval = xval >> 8;
    c++;
  }
  WRITEALL(fd, &c, sizeof(unsigned char));
  for (d=(c&15)-1;d>=0;d--)
    v[(unsigned char)d] = (unsigned char) (val2 >> (8*d));
  WRITEALL(fd, &v[0], (unsigned char) c & 15);
  for (d=(c>>4)-1;d>=0;d--)
    v[(unsigned char)d] = (unsigned char) (val1 >> (8*d));
  WRITEALL(fd, &v[0], (unsigned char) c >> 4);
}

static void WRITEUINT(BIO * fd,
		      unsigned int val) {
  signed char c;
  signed char d;
  unsigned char v[4];
  unsigned int xval;

  xval = val;
  c = 0;
  while (val > 0) {
    val = val >> 8;
    c++;
  }
  WRITEALL(fd, &c, sizeof(signed char));
  for (d=c-1;d>=0;d--)
    v[(unsigned char)d] = (unsigned char) (xval >> (8*d));
  WRITEALL(fd, &v[0], (int) c);
}

static void WRITEULONG(BIO * fd,
		       unsigned long long val) {
  signed char c;
  signed char d;
  unsigned char v[8];
  unsigned long long xval;

  xval = val;
  c = 0;
  while (val > 0) {
    val = val >> 8;
    c++;
  }
  WRITEALL(fd, &c, sizeof(signed char));
  for (d=c-1;d>=0;d--)
    v[(unsigned char)d] = (unsigned char) (xval >> (8*d));
  WRITEALL(fd, &v[0], (int) c);
}

static void WRITEULONGFULL(BIO * fd,
                           unsigned long long val) {
  unsigned int d, e;
  unsigned char v[8];

  for (d=0,e=7;d<8;d++,e--)
    v[d] = (unsigned char) (val >> (8*e));
  WRITEALL(fd, &v[0], 8);
}

static char * readZT(BIO * fd) {
  unsigned int len;
  char * buf;

  if (-1 == READUINT(fd, &len))
    return NULL;
  buf = MALLOC(len+1);
  if (-1 == READALL(fd, buf, len)) {
    free(buf);
    return NULL;
  }
  buf[len] = '\0';
  return buf;
}

static void writeZT(BIO * fd,
		    const char * buf) {
  unsigned int len;

  len = strlen(buf);
  WRITEUINT(fd, len);
  WRITEALL(fd, buf, strlen(buf));
}

static char * readFN(BIO * fd,
		     const pchar * pathTab,
		     const unsigned int ptc) {
  unsigned int pid;
  unsigned int fnl;
  int slen;
  char * buf;

  if (-1 == READUINT(fd, &pid))
    return NULL;
  if (-1 == READUINT(fd, &fnl))
    return NULL;
  if (pid >= ptc) {
    fd->log(fd->context,
	    DOODLE_LOG_CRITICAL,
	    _("Assertion failed at %s:%d.\nDatabase format error!\n"),
	    pid, ptc); /* invalid PID index (pid >= ptc) */
    return NULL;
  }
  slen = strlen(pathTab[pid]);
  buf = MALLOC(fnl + slen + 2);
  strcpy(buf,
	 pathTab[pid]);
  strcat(buf,
	 "/");
  if (-1 == READALL(fd,
		    &buf[slen+1],
		    fnl)) {
    free(buf);
    return NULL;
  }
  buf[slen+fnl+1] = '\0';
  return buf;
}

static void writeFN(BIO * fd,
		    const pchar* pathTab,
		    const unsigned int ptc,
		    const char * fn) {
  int i;
  int slen;
  int xslen;

  slen = strlen(fn);
  xslen = slen;
  while ( (fn[slen] != '/') && (slen > 0) )
    slen--;
  for (i=ptc-1;i>=0;i--) {
    if ( (0 == strncmp(fn,
		       pathTab[i],
		       slen)) &&
	 (slen == strlen(pathTab[i])) ) {
      WRITEUINT(fd, i);
      WRITEUINT(fd, xslen - slen - 1);
      WRITEALL(fd, &fn[slen+1], xslen - slen - 1);
      return;
    }
  }
  /* invalid pathTab! */
  fd->log(fd->context,
	  DOODLE_LOG_CRITICAL,
	  _("Assertion failed at %s:%d.\n"));
}

/* ******************* tree code ******************** */

/**
 * Note that the entries in this struct are sorted
 * (long longs, pointers, ints, sub-words).  This is
 * to help the C coqmpiler produce a compact struct
 * while making accesses aligned.
 *
 * @brief a node in the doodle suffix tree
 */
typedef struct DOODLE_Node {
  /* if link == NULL and link_off != 0, then
     the link exists but is still on-disk */
  unsigned long long link_off;
  /* ifchild == NULL and next_off != 0, then
     the child exists but is still on-disk */
  unsigned long long next_off;
  /* position of this STNode in the file */
  unsigned long long pos;
  /* other characters on the same level */
  struct DOODLE_Node * link;
  /* subtrees for a longer suffix */
  struct DOODLE_Node * child;
  /* "parent" node (node that has a reference to this)*/
  struct DOODLE_Node * parent;
  /* character at this position in the tree,
     pointer into tree->cis. */
  char * c;
  /* list of indices into tree->filenames */
  unsigned int * matches;
  /* how many files match here? */
  unsigned int matchCount;
#if USE_CI_CACHE
  /* cix values (cached for serialization speed!) */
  int cix;
#endif
  /* Use counter, to avoid swapping out nodes
     that are frequently used! (Not stored on
     disk, just in memory!) */
  unsigned int useCounter;
  /* length of the character sequence of 'c' */
  unsigned char clength;
  /* size of this STNode array (multi-link support) */
  unsigned char mls_size;
  /* has this node been modified? */
  unsigned char modified;
} STNode;

/**
 * @brief the suffix tree (containing the interned
 *  content like keywords and filenames and the root-node).
 */
typedef struct DOODLE_SuffixTree {
  /* logger */
  DOODLE_Logger log;
  /* context */
  void * context;
  /* name of the database file */
  char * database;
  /* file handle for database file */
  BIO * fd;
  /* size of filenames array */
  unsigned int fns;
  /* number of entries used in filenames array */
  unsigned int fnc;
  /* interned filenames from entire tree */
  DOODLE_FileInfo * filenames;
  /* root of the suffix tree, maybe null! */
  STNode * root;
  /* the keyword/character index string */
  pchar* cis;
  /* how long is cis? */
  unsigned int cisPos;
  /* how much space do we have in cis? */
  unsigned int cisLen;
  /* was this suffix tree modified? 1: yes, 0: no */
  int modified;
  /* force full dump (even of unmodified nodes)? 1: yes, 0: no */
  int force_dump;
  /* how much memory is used at the moment for the tree?
     (excluding filenames, cis, matches; just Nodes!) */
  size_t used_memory;
  /* memory limit */
  size_t memory_limit;
  /* minimum number of uses to allow swapping out */
  unsigned int swapLimit;
  /* number of mutations since last swap */
  unsigned int mutationCount;
  /* is this tree read-only? */
  int read_only;
} SuffixTree;

unsigned int DOODLE_getFileCount(const struct DOODLE_SuffixTree * tree) {
  return tree->fnc;
}

const DOODLE_FileInfo * DOODLE_getFileAt(const struct DOODLE_SuffixTree * tree,
					 unsigned int index) {
  return &tree->filenames[index];
}

static char CIS[] = {
  0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
  10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
  20, 21, 22, 23, 24, 25, 26, 27, 28, 29,
  30, 31, 32, 33, 34, 35, 36, 37, 38, 39,
  40, 41, 42, 43, 44, 45, 46, 47, 48, 49,
  50, 51, 52, 53, 54, 55, 56, 57, 58, 59,
  60, 61, 62, 63, 64, 65, 66, 67, 68, 69,
  70, 71, 72, 73, 74, 75, 76, 77, 78, 79,
  80, 81, 82, 83, 84, 85, 86, 87, 88, 89,
  90, 91, 92, 93, 94, 95, 96, 97, 98, 99,
  100, 101, 102, 103, 104, 105, 106, 107, 108, 109,
  110, 111, 112, 113, 114, 115, 116, 117, 118, 119,
  120, 121, 122, 123, 124, 125, 126, 127, 128, 129,
  130, 131, 132, 133, 134, 135, 136, 137, 138, 139,
  140, 141, 142, 143, 144, 145, 146, 147, 148, 149,
  150, 151, 152, 153, 154, 155, 156, 157, 158, 159,
  160, 161, 162, 163, 164, 165, 166, 167, 168, 169,
  170, 171, 172, 173, 174, 175, 176, 177, 178, 179,
  180, 181, 182, 183, 184, 185, 186, 187, 188, 189,
  190, 191, 192, 193, 194, 195, 196, 197, 198, 199,
  200, 201, 202, 203, 204, 205, 206, 207, 208, 209,
  210, 211, 212, 213, 214, 215, 216, 217, 218, 219,
  220, 221, 222, 223, 224, 225, 226, 227, 228, 229,
  230, 231, 232, 233, 234, 235, 236, 237, 238, 239,
  240, 241, 242, 243, 244, 245, 246, 247, 248, 249,
  250, 251, 252, 253, 254, 255,
};

#if DEBUG
static void checkInvariants(STNode * pos,
			    int * nodeCounter) {
  while (pos != NULL) {
    if (pos->mls_size > 1) {
      if (pos[1].mls_size != pos->mls_size - 1)
	abort();
      if (pos[1].c[0] != pos->c[0] + 1)
	abort();
    }
    if ( (pos->link != NULL) &&
	 (pos->c[0] >= pos->link->c[0]) )
      abort();
    (*nodeCounter)++;
    if ( (pos->link != NULL) &&
	 (pos->link->parent != pos) )
      abort();
    if ( (pos->child != NULL) &&
	 (pos->child->parent != pos) )
      abort();
    if (pos->child != NULL)
      checkInvariants(pos->child, nodeCounter);
    pos = pos->link;
  }
}

/**
 * Macro to be used to check tree invariants.
 */
#define CHECK(tree) { int i = 0; checkInvariants(tree->root, &i); if ((tree->root != NULL) && (tree->root->parent != NULL)) abort(); if (tree->used_memory != sizeof(STNode) * i) abort(); }
#else
#define CHECK(tree) {} while(0)
#endif

static void freeNode(SuffixTree * tree,
		     STNode * node) {
  STNode * last;
  STNode * tmp;
  int mls;

  while (node != NULL) {
    for (mls = 0; mls<node->mls_size;mls++) {
      if (node[mls].child != NULL) {
	tmp = node[mls].child;
	node[mls].child = NULL;
	freeNode(tree, tmp);
      }
      if (node[mls].matches != NULL)
	free(node[mls].matches);
    }
    last = node;
    node = node[last->mls_size-1].link;
    tree->used_memory -= sizeof(STNode) * last->mls_size;

    free(last);
  }
}

/**
 * Prototype, code see below.
 */
static unsigned long long writeNode(BIO * fd,
				    SuffixTree * tree,
				    STNode * node);

/**
 * Shrink the given subtree of tree starting at node pos.
 * The ktC index into keepThese describes the next node
 * that must not be freed since it is referenced from
 * the calling context.
 */
static void processShrink(SuffixTree * tree,
			  STNode ** keepThese,
			  int ktC,
			  int ktP,
			  STNode * pos,
			  unsigned int * kept) {
  int mark;
  STNode * next;

  while (pos != NULL) {
    (*kept)++;
    next = NULL;
    mark = 0;
    if (ktP >= 0) {
      if (pos->link == keepThese[ktP])
	mark |= 1;
      if (pos->child == keepThese[ktP])
	mark |= 2;
    }
#if DEBUG
    if ( (mark & 1) == 0) {
      int i;
      for (i=0;i<ktC;i++)
	if (keepThese[i] == pos->link)
	  abort();
    }
    if ( (mark & 2) == 0) {
      int i;
      for (i=0;i<ktC;i++)
	if (keepThese[i] == pos->child)
	  abort();
    }
#endif
    if ( ( (mark & 1) == 0) &&
	 (pos->link != NULL) &&
	 (pos->link->mls_size == 1) &&
	 (pos->mls_size == 1) ) {
      /* we are "allowed" to swap, do we want to
	 swap this particular node? */
      if ( (pos->link->useCounter <= tree->swapLimit) &&
	   ( (0 == tree->read_only) ||
	     (pos->link->modified == 0) ) ) {
	if ( (tree->force_dump != 0) ||
	     (pos->link->modified != 0) ) {
	  pos->link_off = writeNode(tree->fd,
				    tree,
				    pos->link);
	}
	freeNode(tree,
		 pos->link);
	pos->link = NULL;
	CHECK(tree);
      } else {
	/* no, not this one, but recurse on the link! */
	pos->link->useCounter = 0;
	processShrink(tree,
		      keepThese,
		      ktC,
		      ktP,
		      pos->link,
		      kept);
	/* no continue here: need to also look
	   at pos->child! */
      }
    } else {
      /* swap was not allowed... */
      ktP--;
      processShrink(tree,
		    keepThese,
		    ktC,
		    ktP,
		    pos->child,
		    kept);
      pos = pos->link;
      continue;
    }

    if ( ( (mark & 2) == 0) &&
	 (pos->child != NULL) ) {
      /* we are allowed to swap, do we want to? */
      if ( (pos->child->useCounter <= tree->swapLimit) &&
	   ( (0 == tree->read_only) ||
	     (pos->child->modified == 0) ) ) {
	if ( (tree->force_dump != 0) ||
	     (pos->child->modified != 0) ) {
	  pos->next_off = writeNode(tree->fd,
				    tree,
				    pos->child);
	}
	freeNode(tree,
		 pos->child);
	pos->child = NULL;
	CHECK(tree);
	pos = NULL;
      } else {
	pos->child->useCounter = 0;
	/* no, we don't want to swap this child,
	   but continue processing with the subtree */
	pos = pos->child;
      }
    } else {
      /* swap was not allowed... */
      ktP--;
      pos = pos->child;
    }
  }
  CHECK(tree);
}
			
/**
 * Reduce the memory consumption by dumping unused portions of
 * the suffix tree to disk.
 */
static void shrinkMemoryFootprint(SuffixTree * tree,
				  STNode * keep) {
  STNode ** keepThese;
  unsigned int ktC;
  STNode * pos;
  unsigned int kept;
  int force_dump;

  force_dump = tree->force_dump;
  tree->force_dump = 0; /* deactivate while shrinking! */
  CHECK(tree);
  /* tree->swapLimit = tree->mutationCount / 2;
     RATIONALE: if we were able to perform very few mutations,
     we want to be more aggressive with swapping.  If we are able
     to perform many mutations, we can affort swapping out less. */
  tree->swapLimit = (tree->mutationCount / 2) + 1;
  tree->mutationCount = 0;
  tree->log(tree->context,
	    DOODLE_LOG_VERY_VERBOSE,
	    _("Memory limit (%u bytes) hit, serializing some data.\n"),	
	    tree->used_memory);
  pos = keep;
  keepThese = NULL;
  ktC = 0;
  while (pos != NULL) {
    GROW(keepThese, ktC, ktC+1);
    keepThese[ktC-1] = pos;
    pos = pos->parent;
  }
#if DEBUG > 2
  if (keepThese[ktC-1] != tree->root) {
    fprintf(stderr,
	    "Chain does not end at root %p\n",
	    tree->root);
    pos = keep;
    while (pos != NULL) {
      fprintf(stderr,
	      "Have %p -> %p\n", pos, pos->parent);
      pos = pos->parent;
    }
    abort(); /* assertion violated! */
  }
#endif
  kept = 0;
  processShrink(tree, keepThese, ktC, ktC-2, tree->root, &kept);
  CHECK(tree);
#if ASSERTS
  if (kept * sizeof(STNode) != tree->used_memory) {
    tree->log(tree->context,
	      DOODLE_LOG_CRITICAL,
	      _("Assertion failed at %s:%d.\n"),
	      __FILE__,  __LINE__);
  }
#endif
  GROW(keepThese, ktC, 0);
  tree->log(tree->context,
	    DOODLE_LOG_VERY_VERBOSE,
	    _("Reduced memory consumption for suffix tree to %u bytes.\n"),
	    tree->used_memory);
  CHECK(tree);
  tree->force_dump = force_dump;
}

/**
 * Read the node at the given offset that belongs to the given parent.
 * Lazy in the sense that it does not read the children of the node.
 */
static STNode * lazyReadNode(SuffixTree * tree,
			     unsigned long long off) {
  STNode * ret;
  int i;
  unsigned long long off_link;
  unsigned long long off_child;
  unsigned char c_length;
  unsigned char mls_size;
  int mls;

  if (off == 0)
    return NULL;
  LSEEK(tree->fd, off, SEEK_SET);
  if (-1 == READALL(tree->fd,
		    &c_length,
		    sizeof(signed char)))
    return NULL;
  if (c_length == 0) {
    if (-1 == READALL(tree->fd,
		      &mls_size,
		      sizeof(signed char)))
      return NULL;
    if (mls_size == 0) { /* not legal! */
      tree->log(tree->context,
		DOODLE_LOG_CRITICAL,
		_("Assertion failed at %s:%d.\nDatabase format error!\n"),
		__FILE__,  __LINE__);
      return NULL;
    }
  } else {
    /* if (c != 0) mls_size is always 1 and not
       stored in the file! */
    mls_size = 1;
  }

  ret = MALLOC(sizeof(STNode) * mls_size);
  ret[0].pos = off;
  for (mls=0;mls<mls_size;mls++) {
    ret[mls].clength  = c_length;
    ret[mls].mls_size = (unsigned char) (mls_size - mls);
    if (mls > 0) {
      ret[mls-1].link_off = 0;
      ret[mls-1].link     = &ret[mls];
      ret[mls].parent     = &ret[mls-1];
    }

    if (ret[mls].clength == 0) {
      char c;

      if (mls == 0) {
	if (-1 == READALL(tree->fd, &c, sizeof(unsigned char)))
	  goto ERROR_ABORT;	
      } else {
	c = ret[mls-1].c[0] + 1;
      }
      ret[mls].c = &CIS[(unsigned char) c];
      ret[mls].clength = 1;
    } else {
      unsigned int cix;
      unsigned int ciy;
      if (-1 == READUINTPAIR(tree->fd, &cix, &ciy))
	goto ERROR_ABORT;
      if ( (cix >= tree->cisLen) ||
	   (ciy >= strlen(tree->cis[cix])) ) {
	tree->log(tree->context,
		  DOODLE_LOG_CRITICAL,
		  _("Assertion failed at %s:%d.\nDatabase format error!\n"),
		  __FILE__,  __LINE__);
	goto ERROR_ABORT;
      }
      ret[mls].c = &tree->cis[cix][ciy];
#if USE_CI_CACHE
      ret[mls].cix = cix;
#endif
    }

    if (mls == mls_size-1) {
      if (-1 == READULONGPAIR(tree->fd, &off_link, &off_child))
	goto ERROR_ABORT;
      /* off_link and off_child are serialized relative
	 to off and negative (since child and link are
	 always stored before off in the file).
	 ASSERT and compute the absolute offsets. */
      if ( (off_link > off) ||
	   (off_child > off) ) {
	tree->log(tree->context,
		  DOODLE_LOG_CRITICAL,
		  _("Assertion failed at %s:%d.\nDatabase format error!\n"),
		  __FILE__,  __LINE__);
	goto ERROR_ABORT;
      }
      if (off_link != 0)
	off_link = off - off_link;
      if (off_child != 0)
	off_child = off - off_child;
    } else {
      off_link = 0; /* to be set in the next iteration! */
      if (-1 == READULONG(tree->fd, &off_child))
	goto ERROR_ABORT;
      /* off_child is serialized relative
	 to off and negative (since child and link are
	 always stored before off in the file).
	 ASSERT and compute the absolute offsets. */
      if (off_child > off)  {
	tree->log(tree->context,
		  DOODLE_LOG_CRITICAL,
		  _("Assertion failed at %s:%d.\nDatabase format error!\n"),
		  __FILE__,  __LINE__);
	goto ERROR_ABORT;
      }
      off_child = off - off_child;
    }
    ret[mls].link_off = off_link;
    ret[mls].next_off = off_child;

    if ( (ret[mls].link_off > tree->fd->fsize) ||
	 (ret[mls].next_off > tree->fd->fsize) ) {
      tree->log(tree->context,
		DOODLE_LOG_CRITICAL,
		_("Assertion failed at %s:%d.\nDatabase format error!\n"),
		__FILE__,  __LINE__);
      goto ERROR_ABORT;
    }

    if (-1 == READUINT(tree->fd, &ret[mls].matchCount))
      goto ERROR_ABORT;
    if (ret[mls].matchCount == 0) {
      ret[mls].matches = NULL;
    } else {
      ret[mls].matches
	= MALLOC(ret[mls].matchCount*sizeof(unsigned int));
      for (i=ret[mls].matchCount/2-1;i>=0;i--) {
	unsigned int idx1;
	unsigned int idx2;
	if (-1 == READUINTPAIR(tree->fd,
			       &idx1,
			       &idx2))
	  goto ERROR_ABORT;
	if (idx1 >= tree->fnc) {
	  tree->log(tree->context,
		    DOODLE_LOG_CRITICAL,
		    _("Assertion failed at %s:%d.\nDatabase format error!\n"),
		    __FILE__,  __LINE__);
	  goto ERROR_ABORT;
	}
	if (idx2 >= tree->fnc) {
	  tree->log(tree->context,
		    DOODLE_LOG_CRITICAL,
		    _("Assertion failed at %s:%d.\nDatabase format error!\n"),
		    __FILE__, __LINE__);
	  goto ERROR_ABORT;
	}
	ret[mls].matches[i*2+1] = idx1;
	ret[mls].matches[i*2]   = idx2;
      }
      if (1 == (ret[mls].matchCount & 1) ) {
	unsigned int idx;
	if (-1 == READUINT(tree->fd, &idx))
	  goto ERROR_ABORT;
	if ( idx >= tree->fnc ) {
	  tree->log(tree->context,
		    DOODLE_LOG_CRITICAL,
		    _("Assertion failed at %s:%d.\nDatabase format error!\n"),
		    __FILE__, __LINE__);
	  goto ERROR_ABORT;
	}
	ret[mls].matches[ret[mls].matchCount-1] = idx;
      }
    }
  } /* end for mls */
#if DEBUG > 1
  printf("%llu: Read  %u-Node (%c, %u, %u, %u) L:%llu, C:%llu until %llu\n",
	 off,
	 ret->mls_size,
	 ret->c[0],
	 ret->clength,
	 ret->matchCount,
	 (ret->matchCount > 0) ? ret->matches[0] : -1,
	 ret[ret->mls_size-1].link_off,
	 ret->next_off,
	 LSEEK(tree->fd, 0, SEEK_CUR));
#endif
  tree->used_memory += sizeof(STNode) * mls_size;
  return ret;
 ERROR_ABORT:
  for (mls=0;mls<mls_size;mls++)
    if (ret[mls].matches != NULL)
      free(ret[mls].matches);
  free(ret);
  return NULL;
}

static int loadChild(SuffixTree * tree,
		     STNode * node) {
  CHECK(tree);
  if (node->next_off == 0) {
#if ASSERTS
    abort();
#endif
    return -1;
  }
  if (tree->used_memory > tree->memory_limit)
    shrinkMemoryFootprint(tree, node);
  node->child = lazyReadNode(tree,
			     node->next_off);
  if (node->child == NULL) {
#if ASSERTS
    abort();
#endif
    return -1;
  }
  node->child->parent = node;
  CHECK(tree);
  return 0;
}

static int loadLink(SuffixTree * tree,
		    STNode * node) {
  if (node->link_off == 0) {
#if ASSERTS
    abort();
#endif
    return -1;
  }
  if (tree->used_memory > tree->memory_limit)
    shrinkMemoryFootprint(tree, node);

  node->link = lazyReadNode(tree,
			    node->link_off);
  if (node->link == NULL) {
#if ASSERTS
    abort();
#endif
    return -1;
  }
  node->link->parent = node;
  CHECK(tree);
  return 0;
}

/**
 * @return offset at which node is written!
 */
static unsigned long long writeNode(BIO * fd,
				    SuffixTree * tree,
				    STNode * node) {
  unsigned long long ret;
  unsigned long long linkRel;
  unsigned long long nextRel;
  int i;
  int mls;

  if (node == NULL)
    return 0;
  if (tree->read_only)
    abort();

  node->modified = 0;
  for (mls=0;mls<node->mls_size;mls++) {
    if ( (node[mls].child == NULL) &&
	 (node[mls].next_off != 0) &&
	 (tree->force_dump != 0) )
      loadChild(tree, &node[mls]);
    if ( (node[mls].child != NULL) &&
	 ( (node[mls].child->modified != 0) ||
	   (tree->force_dump != 0) ) )
      node[mls].next_off
	= writeNode(fd,
		    tree,
		    node[mls].child);
  }
  if ( (node[node->mls_size-1].link == NULL) &&
       (node[node->mls_size-1].link_off != 0) &&
       (tree->force_dump != 0) ) {
    loadLink(tree, &node[node->mls_size-1]);
  }
  if ( (node[node->mls_size-1].link != NULL) &&
       ( (node[node->mls_size-1].link->modified != 0) ||
	 (tree->force_dump != 0) ) ) {
    node[node->mls_size-1].link_off
      = writeNode(fd,
		  tree,
		  node[node->mls_size-1].link);
  }
  ret = LSEEK(fd, 0, SEEK_END);
#if ASSERTS
  if (node->clength == 0) {
    /* clength must not be 0! */
    tree->log(tree->context,
	      DOODLE_LOG_CRITICAL,
	      _("Assertion failed at %s:%d.\n"),
	      __FILE__, __LINE__);
    return 0;
  }
#endif

  if ( (node[node->mls_size-1].link_off > fd->fsize) ||
       (node->next_off > fd->fsize) ) {
    tree->log(tree->context,
	      DOODLE_LOG_CRITICAL,
	      _("Assertion failed at %s:%d: %llu > %llu or %llu > %llu.\n"),
	      __FILE__,  __LINE__,
	      node[node->mls_size-1].link_off, fd->fsize,
	      node->next_off, fd->fsize);
    tree->log(tree->context,
	      DOODLE_LOG_CRITICAL,
	      _("Assertion failed at %s:%d.\n"),
	      __FILE__,  __LINE__);
    abort();
    return 0;
  }

  if (node->clength == 1) {
    unsigned char o = 0;
    WRITEALL(fd, &o, sizeof(unsigned char));
    WRITEALL(fd, &node->mls_size, sizeof(unsigned char));
    WRITEALL(fd, node->c, sizeof(unsigned char));
  } else {
    int cix;
    int ciy;

    WRITEALL(fd, &node->clength, sizeof(unsigned char));
#if USE_CI_CACHE
    cix = node->cix;
    ciy = node->c - tree->cis[node->cix];	
#else
    cix = -1;
    ciy = -1;
    for (i=0;i<tree->cisPos;i++) {
      if ( (node->c >= tree->cis[i]) &&
	   (node->c < &tree->cis[i][strlen(tree->cis[i])]) ) {
	cix = i;
	ciy = node->c - tree->cis[i];
	break;
      }
    }
    if ( (cix == -1) ||
	 (ciy < 0) ||
	 (ciy >= strlen(tree->cis[cix])) ) {
      /* cis table invalid! */
      tree->log(tree->context,
		DOODLE_LOG_CRITICAL,
		_("Assertion failed at %s:%d.\n"),
		__FILE__, __LINE__);
    }
#endif
    WRITEUINTPAIR(fd, cix, ciy);
  }
  for (mls=0;mls<node->mls_size;mls++) {
    if (mls == node->mls_size-1) {
#if ASSERTS
      /* link/next must be stored before this node,
	 assert that! */
      if ( (node[mls].link_off >= ret) ||
	   (node[mls].next_off >= ret) ) {
	tree->log(tree->context,
		  DOODLE_LOG_CRITICAL,
		  _("Assertion failed at %s:%d.\n"),
		  __FILE__, __LINE__);
      }
#endif
      if (node[mls].link_off != 0)
	linkRel = ret - node[mls].link_off;
      else
	linkRel = 0;
      if (node[mls].next_off != 0)
	nextRel = ret - node[mls].next_off;
      else
	nextRel = 0;
      WRITEULONGPAIR(fd, linkRel, nextRel);
    } else {
#if ASSERTS
       /* next must be stored before this node,
	  assert that! */
      if (node[mls].next_off >= ret) {
	tree->log(tree->context,
		  DOODLE_LOG_CRITICAL,
		  _("Assertion failed at %s:%d.\n"),
		  __FILE__, __LINE__);
      }
#endif
      nextRel = ret - node[mls].next_off;
      WRITEULONG(fd, nextRel);
    }
    WRITEUINT(fd, node[mls].matchCount);

    for (i=node[mls].matchCount/2-1;i>=0;i--) {
      unsigned int idx1;
      unsigned int idx2;

      idx1 = node[mls].matches[i*2+1];
      idx2 = node[mls].matches[i*2];
      WRITEUINTPAIR(fd,
		    idx1,
		    idx2);
    }
    if (1 == (node[mls].matchCount & 1) ) {
      unsigned int idx;

      idx = node[mls].matches[node[mls].matchCount-1];
      WRITEUINT(fd,
		idx);
    }
  } /* for mls */
#if DEBUG > 1
  printf("%llu: Wrote %u-node (%c, %u, %u, %u) L:%llu, C:%llu until %llu\n",
	 ret,
	 node->mls_size,
	 node->c[0],
	 node->clength,
	 node->matchCount,
	 (node->matchCount > 0) ? node->matches[0] : -1,
	 node[node->mls_size-1].link_off,
	 node->next_off,	
	 LSEEK(fd, 0, SEEK_CUR));
#endif

  if (ret > fd->fsize) {
    tree->log(tree->context,
	      DOODLE_LOG_CRITICAL,
	      _("Assertion failed at %s:%d: %llu > %llu.\n"),
	      __FILE__,  __LINE__,
	      ret,
	      fd->fsize);
  }

  return ret;
}


/**
 * Magic string is DOO for doodle followed by a '\0' to indicate a
 * binary file.  The next 4 digits describe the format version,
 * currently always "0002" to indicate compatibility with doodle
 * 0.2.0. ("0000" used SHA-1 instead of timestamps and is no longer
 * supported; "0001" was never used (see TRAGIC)).
 *
 * Doodle 0.5.0 is incompatible with 0.2.0 to 0.4.0, hence a new
 * version number.  The major differences are support for 64-bit
 * offsets (which would allow reading an 0.4.0 db per-se), but
 * also relative file offsets (which reduces the DB size and
 * breaks compatibility even for DB-sizes smaller than 2^31).
 *
 * Doodle 0.6.0 is again incompatible with 0.5.0, this
 * time introducing the 'mls' node groups (which has the potential
 * to significantly improve performance).
 */
static char * MAGIC = "DOO\0000007";

/**
 * Magic string to indicate an temporary doodle database that
 * could not be completely created (the indexing/building process
 * was aborted).
 */
static char * TRAGIC = "XOO\0000001";

/**
 * Create a suffix-tree (and store in file named database).
 */
SuffixTree * DOODLE_tree_create_internal(DOODLE_Logger log,
					 void * context,
					 const char * database,
					 int flags) {
  int ifd;
  BIO * fd;
  SuffixTree * ret;
  struct stat buf;
  int i;
  unsigned long long off;
  pchar* pathTab;
  unsigned int ptc;
  signed char magic[8];

  ret = MALLOC(sizeof(SuffixTree));
  ret->log = log;
  ret->context = context;
  ret->database = STRDUP(database);
  ret->modified = 0;
  ret->used_memory = 0;
  ret->memory_limit = MEMORY_LIMIT;
  ret->swapLimit = 65536; /* start very high */
  ret->mutationCount = 0;
  ret->force_dump = 0;
  ret->read_only = (flags == O_RDONLY);

  if (0 == stat(database, &buf)) {
#ifdef O_LARGEFILE
    ifd = open(database,
	       flags | O_LARGEFILE,
	       S_IRUSR | S_IWUSR | S_IRGRP);
#else
    ifd = open(database,
	       flags,
	       S_IRUSR | S_IWUSR | S_IRGRP);
#endif
    if (ifd == -1) {
      log(context,
	  DOODLE_LOG_CRITICAL,
	  _("Could not open '%s': %s\n"),
	  database,
	  strerror(errno));
      free(ret->database);
      free(ret);
      return NULL;
    }
    if (0 != flock(ifd, (flags == O_RDWR) ? LOCK_EX : LOCK_SH)) {
      log(context,
	  DOODLE_LOG_CRITICAL,
	  _("Could not lock database '%s': %s\n"),
	  database,
	  strerror(errno));
      free(ret->database);
      free(ret);
      return NULL;
    }
    fd = IO_WRAP(log,
		 context,
		 ifd);
    if (-1 == READALL(fd,
		      magic,
		      8)) {
      memcpy(magic,
	     "garbage!",
	     8);
    }
    if (0 != memcmp(magic,
		    MAGIC,
		    8)) {
      if (0 == memcmp(magic,
		      TRAGIC,
		      8)) {
	log(context,
	    DOODLE_LOG_CRITICAL,
	    _("Database file '%s' is from incomplete database build.  I will remove it and rebuild the database.\n"),
	    database);
	IO_FREE(fd);
	if (0 != unlink(database))
	  log(context,
	      DOODLE_LOG_CRITICAL,
	      _("Could not unlink '%s': %s\n"),
	      database,
	      strerror(errno));	
	fd = NULL;
	goto FRESH_START;
      } else {
	log(context,
	    DOODLE_LOG_CRITICAL,
	    _("Database file '%s' has wrong magic code.\n"),
	    database);
	IO_FREE(fd);
	free(ret->database);
	free(ret);
	return NULL;
      }
    }
    /* read PTab */
    if (-1 == READUINT(fd,
		       &ptc)) {
      free(ret->database);
      free(ret);
      IO_FREE(fd);
      return NULL;
    }
    if (ptc != 0) {
      pathTab = MALLOC(ptc * sizeof(pchar));
      for (i=ptc-1;i>=0;i--) {
	pathTab[i] = readZT(fd);
	if (pathTab[i] == NULL) {
	  while (i < ptc-1)
	    free(pathTab[++i]);
	  free(pathTab);
	  free(ret->database);
	  free(ret);
	  IO_FREE(fd);
	  return NULL;
	}
      }
    } else
      pathTab = NULL;

    ret->fns = 0;
    ret->filenames = NULL;
    /* read... */
    if (-1 == READUINT(fd,
		       &ret->fnc)) {
      for (i=ptc-1;i>=0;i--)
	free(pathTab[i]);
      free(ret->database);
      free(ret);
      IO_FREE(fd);
      return NULL;
    }
    if (ret->fnc != 0) {
      GROW(ret->filenames,
	   ret->fns,
	   ret->fnc);
      for (i=ret->fnc-1;i>=0;i--) {
	ret->filenames[i].filename = readFN(fd,
					    pathTab,
					    ptc);
	if (ret->filenames[i].filename == NULL) {
	  while (i < ret->fnc-1)
	    free(ret->filenames[++i].filename);
	  GROW(ret->filenames,
	       ret->fns,
	       0);
	  for (i=ptc-1;i>=0;i--)
	    free(pathTab[i]);
	  free(pathTab);
	  free(ret->database);
	  free(ret);
	  IO_FREE(fd);
	  log(context,
	      DOODLE_LOG_CRITICAL,
	      _("Error reading database '%s' at %s.%d.\n"),
	      database,
	      __FILE__, __LINE__);
	  return NULL;
	}
	if (-1 == READUINT(fd,
			   &ret->filenames[i].mod_time)) {
	  while (i < ret->fnc-1)
	    free(ret->filenames[++i].filename);
	  GROW(ret->filenames,
	       ret->fns,
	       0);
	  for (i=ptc-1;i>=0;i--)
	    free(pathTab[i]);
	  free(pathTab);
	  free(ret->database);
	  free(ret);
	  IO_FREE(fd);
	  return NULL;
	}
      }
    }
    if (ptc != 0) {
      for (i=ptc-1;i>=0;i--)
	free(pathTab[i]);
      free(pathTab);
    }
    if (-1 == READUINT(fd,
		       &ret->cisPos)) {
      GROW(ret->filenames,
	   ret->fns,
	   0);
      free(ret->database);
      free(ret);
      IO_FREE(fd);
    }
    ret->cisLen = ret->cisPos;
    if (ret->cisLen > 0)
      ret->cis = MALLOC(ret->cisLen * sizeof(signed char*));
    else
      ret->cis = NULL;
    for (i=ret->cisPos-1;i>=0;i--) {
      ret->cis[i] = readZT(fd);
      if (ret->cis[i] == NULL) {
	while (i < ret->cisPos-1)
	  free(ret->cis[++i]);
	free(ret->cis);
	GROW(ret->filenames,
	     ret->fns,
	     0);
	free(ret->database);
	free(ret);
	IO_FREE(fd);
  	return NULL;
      }
    }
    if (-1 == READULONGFULL(fd, &off)) {
      for (i=ret->cisPos-1;i>=0;i--)
	free(ret->cis[++i]);
      free(ret->cis);
      GROW(ret->filenames,
	   ret->fns,
	   0);
      free(ret->database);
      free(ret);
      IO_FREE(fd);
      return NULL;
    }
    ret->fd = fd;
    ret->root = lazyReadNode(ret,
			     off);
  } else {
  FRESH_START:
    if (flags == O_RDONLY) {
      log(context,
	  DOODLE_LOG_CRITICAL,
	  _("Database '%s' does not exist.\n"),
	  database);
      free(ret->database);
      free(ret);
      return NULL;
    }
#ifdef O_LARGEFILE
    ifd = open(database,
	       O_CREAT | flags | O_LARGEFILE,
	       S_IRUSR | S_IWUSR | S_IRGRP);
#else
    ifd = open(database,
	       O_CREAT | flags,
	       S_IRUSR | S_IWUSR | S_IRGRP);
#endif
    if (ifd == -1) {
      log(context,
	  DOODLE_LOG_CRITICAL,
	  _("Error creating database '%s' at %s:%d: %s\n"),
	  database, __FILE__, __LINE__, strerror(errno));
      free(ret->database);
      free(ret);
      return NULL;
    }
    if (0 != flock(ifd, (flags == O_RDWR) ? LOCK_EX : LOCK_SH)) {
      log(context,
	  DOODLE_LOG_CRITICAL,
	  _("Could not lock database '%s': %s\n"),
	  database,
	  strerror(errno));
      free(ret->database);
      free(ret);
      return NULL;
    }
    ret->root = NULL;
    ret->fnc = 0;
    ret->fns = 0;
    ret->filenames = NULL;
    ret->fd = IO_WRAP(log,
		      context,
		      ifd);
    ret->cis = NULL;
    ret->cisLen = 0;
    ret->cisPos = 0;
    /* write anti-marker: this is at best a
       temporary-DB, but never the final one; hence
       "tragic" -- if we start with this one,
       it's from an aborted / crashed previous run! */
    WRITEALL(ret->fd,
	     TRAGIC,
	     8);
    /* make certain that the dirty marker is on
       disk... */
    flush_buffer(ret->fd);
#ifdef HAVE_FDATASYNC
    fdatasync(ret->fd->fd);
#endif
    ret->modified = 1;
  }

  CHECK(ret);
  return ret;
}

/**
 * Create a suffix-tree (and store in file named database).
 */
SuffixTree * DOODLE_tree_create(DOODLE_Logger log,
				void * context,
				const char * database) {
  return DOODLE_tree_create_internal(log,
				     context,
				     database,
				     O_RDWR);
}

/**
 * Open an existing database READ-ONLY.
 * @return NULL on error (i.e. DB does not exist)
 */
SuffixTree * DOODLE_tree_open_RDONLY(DOODLE_Logger log,
				     void * context,
				     const char * database) {
  return DOODLE_tree_create_internal(log,
				     context,
				     database,
				     O_RDONLY);
}

/**
 * Change the memory limit (how much memory the
 * tree may use).  Note that the limit only refers
 * to the tree nodes, memory used for keywords and
 * filenames is excluded since the current code
 * does not support keeping those on disk.
 *
 * @param limit new memory limit in bytes
 */
void DOODLE_tree_set_memory_limit(SuffixTree * tree,
				  size_t limit) {
  tree->memory_limit = limit;
  if (tree->used_memory > tree->memory_limit)
    shrinkMemoryFootprint(tree, tree->root);
}

/**
 * Destroy (and sync) suffix tree.
 */
void DOODLE_tree_destroy(SuffixTree * tree) {
  BIO * fd;
  int i;
  int j;
  unsigned long long off;
  off_t wpos;
  pchar * pathTab;
  unsigned int ptc;
  STNode * tmp;

  CHECK(tree);
  if ( (0 == tree->read_only) &&
       ( (tree->modified != 0) ||
	 ( (tree->root != NULL) &&
	   (tree->root->modified != 0) ) ) ) {
    int fdt;
    char * tdatabase;

    tree->force_dump = 1; /* force re-dump everything! */
    tdatabase = MALLOC(strlen(tree->database) + 2);
    strcpy(tdatabase,
	   tree->database);
    strcat(tdatabase,
	   "~");
#ifdef O_LARGEFILE
    fdt = open(tdatabase,
	       O_CREAT | O_TRUNC | O_RDWR | O_LARGEFILE,
	       S_IRUSR | S_IWUSR | S_IRGRP);
#else
    fdt = open(tdatabase,
	       O_CREAT | O_TRUNC | O_RDWR,
	       S_IRUSR | S_IWUSR | S_IRGRP);
#endif
    if (fdt == -1) {
      tree->log(tree->context,
		DOODLE_LOG_CRITICAL,
		_("Could not open temporary file '%s': %s\n"),
		tdatabase,
		strerror(errno));
      free(tdatabase);
      goto CLEANUP;
    }
    fd = IO_WRAP(tree->log,
		 tree->context,
		 fdt);
    WRITEALL(fd,
	     MAGIC,
	     8);
    tree->log(tree->context,
	      DOODLE_LOG_VERY_VERBOSE,
	      _("Writing doodle database to temporary file '%s'.\n"),
	      tdatabase);
    /* build pathTab */
    ptc = 0;
    pathTab = NULL;
    for (i=tree->fnc-1;i>=0;i--) {
      char * fn;
      int slen;
      int xslen;

      fn = tree->filenames[i].filename;
      slen = strlen(fn);
      xslen = slen;
      while ( (fn[slen] != '/') && (slen > 0) )
	slen--;
      for (j=ptc-1;j>=0;j--)
	if ( (0 == strncmp(fn,
			   pathTab[j],
			   slen)) &&
	     (slen == strlen(pathTab[j])) )
	  break;
      if (j < 0) {
	GROW(pathTab,
	     ptc,
	     ptc+1);
	pathTab[ptc-1] = MALLOC(slen+1);
	memcpy(pathTab[ptc-1],
	       fn,
	       slen);
	pathTab[ptc-1][slen] = '\0';
      }
    }

    /* write pathTab */
    WRITEUINT(fd,
	      ptc);
    for (i=ptc-1;i>=0;i--)
      writeZT(fd,
	      pathTab[i]);
    /* write files... */
    WRITEUINT(fd,
	      tree->fnc);
    for (i=tree->fnc-1;i>=0;i--) {
      writeFN(fd,
	      pathTab,
	      ptc,
	      tree->filenames[i].filename);
      WRITEUINT(fd,
		tree->filenames[i].mod_time);
    }
    if (ptc != 0) {
      for (i=ptc-1;i>=0;i--)
	free(pathTab[i]);
      free(pathTab);
    }
    WRITEUINT(fd,
	      tree->cisPos);
    for (i=tree->cisPos-1;i>=0;i--)
      writeZT(fd,
	      tree->cis[i]);
    wpos = LSEEK(fd, 0, SEEK_CUR);
    off = 0;
    WRITEULONGFULL(fd, off);

    off = writeNode(fd,
		    tree,
		    tree->root);
    LSEEK(fd, wpos, SEEK_SET);
    WRITEULONGFULL(fd, off);
    IO_FREE(tree->fd);
    tree->fd = NULL;
    IO_FREE(fd);

    if (0 != unlink(tree->database))
      tree->log(tree->context,
		DOODLE_LOG_VERBOSE,
		_("Could not remove old database '%s': %s\n"),
		tree->database,
		strerror(errno));
    if (0 != rename(tdatabase,
		    tree->database))
      tree->log(tree->context,
		DOODLE_LOG_CRITICAL,
		_("Could not rename temporary file '%s' to '%s: %s\n"),
		tdatabase,
		tree->database,
		strerror(errno));
    free(tdatabase);
  } /* end if tree or root modified */


  CLEANUP:
  if (tree->fd != NULL) {
    IO_FREE(tree->fd);
    tree->fd = NULL;
  }
  for (i=tree->cisPos-1;i>=0;i--)
    free(tree->cis[i]);
  if (tree->cis != NULL)
    free(tree->cis);
  for (i=tree->fnc-1;i>=0;i--)
    free(tree->filenames[i].filename);
  GROW(tree->filenames,
       tree->fns,
       0);
  tmp = tree->root;
  tree->root = NULL;
  freeNode(tree, tmp);
  free(tree->database);
  free(tree);
}

static void markModified(STNode * pos) {
  while (pos != NULL) {
    if (pos->modified == 1)
      break; /* already marked */
    pos->modified = 1;
    pos = pos->parent;
  }
}

/**
 * Expand a node with clength>1 to a subtree of nodes of clength == 1.
 * This transformation is semantically equivalent and increases memory
 * consumption but has the advantage of simplifying operations on the
 * node.  Thus it can be used whenever memory does not really matter
 * that much (see approximate search) and a more complex algorithm is
 * the bigger problem.
 */
static void tree_normalize(SuffixTree * tree,
			   STNode * pos) {
  STNode * insert;
  STNode * grandchild;

#if ASSERTS
  if (pos->clength == 0) {
    tree->log(tree->context,
	      DOODLE_LOG_CRITICAL,
	      _("Assertion failed at %s:%d!\n"),
	      __FILE__, __LINE__);
    abort();
  }
#endif
  if (pos->clength == 1)
    return;
  grandchild = pos->child;
  insert = MALLOC(sizeof(STNode));
  insert->mls_size = 1;
  insert->useCounter = 0;
  tree->used_memory += sizeof(STNode);
  pos->child = insert;
  insert->parent = pos;
  insert->child = grandchild;
  if (grandchild != NULL)
    grandchild->parent = insert;
  insert->next_off = pos->next_off;
  pos->next_off = 0;

  if (pos->clength == 2) {
    insert->c = &CIS[(unsigned char)pos->c[1]];
    insert->clength = 1;
#if USE_CI_CACHE
    insert->cix = -1;
#endif
  } else {
    insert->c = &pos->c[1];
    insert->clength = pos->clength - 1;
#if USE_CI_CACHE
    insert->cix = pos->cix;
#endif
  }
  insert->matches = pos->matches;
  pos->matches = NULL;
  insert->matchCount = pos->matchCount;
  pos->matchCount = 0;
  pos->clength = 1;
  pos->c = &CIS[(unsigned char)pos->c[0]];
  pos->next_off = 0;
  CHECK(tree);
  markModified(insert);
}

/**
 * Split a node that corresponds to n characters
 * into two nodes of at and n-at characters.
 */
static void tree_split(SuffixTree * tree,
		       STNode * pos,
		       unsigned int at) {
  STNode * insert;
  STNode * grandchild;

#if ASSERTS
 if (pos->clength <= at) {
    tree->log(tree->context,
	      DOODLE_LOG_CRITICAL,
	      _("Assertion failed at %s:%d!\n"),
	      __FILE__, __LINE__);
    return;
  }
#endif
  grandchild = pos->child;
  insert = MALLOC(sizeof(STNode));
  insert->mls_size = 1;
  insert->useCounter = 0;
  tree->used_memory += sizeof(STNode);
  pos->child = insert;
  insert->parent = pos;
  insert->child = grandchild;
  if (grandchild != NULL)
    grandchild->parent = insert;
  insert->next_off = pos->next_off;
  pos->next_off = 0;

  if (pos->clength - at == 1) {
    insert->c = &CIS[(unsigned char)pos->c[at]];
    insert->clength = 1;
#if USE_CI_CACHE
    insert->cix = -1;
#endif
  } else {
    insert->c = &pos->c[at];
    insert->clength = pos->clength - at;
#if USE_CI_CACHE
    insert->cix = pos->cix;
#endif
  }
  insert->matches = pos->matches;
  pos->matches = NULL;
  insert->matchCount = pos->matchCount;
  pos->matchCount = 0;
  pos->clength = at;
  if (at == 1)
    pos->c = &CIS[(unsigned char)pos->c[0]];
  CHECK(tree);
  markModified(insert);
}

static STNode * tree_search_internal(SuffixTree * tree,
				     const char * substring) {
  STNode * pos;
  const char * ss;
  int i;

  CHECK(tree);
  ss = substring;
  pos = tree->root;
  while (ss[0] != '\0') {
    if ( (pos == NULL) || (pos->c == NULL) )
      return NULL;
    if (pos->c[0] > ss[0])
      return NULL; /* not found! */
    if (pos->c[0] == ss[0]) {
      ss++;
      for (i=1;i<pos->clength;i++) {
	if (ss[0] == '\0')
	  break;
	if (pos->c[i] != ss[0])
	  return NULL;
	ss++;
      }				
      if (ss[0] == '\0')
	break;
      if (pos->child == NULL) {
	if (pos->next_off != 0) {
	  if (-1 == loadChild(tree,
			      pos))
	    return NULL; /* error */
	} else {
	  return NULL;
	}
      }
      pos = pos->child;
    } else {
#if ASSERTS
      if (ss[0] <= pos->c[0]) {
	  tree->log(tree->context,
		    DOODLE_LOG_CRITICAL,
		    _("Assertion failed at %s:%d!\n"),
		    __FILE__, __LINE__);
	  return NULL;
      }
#endif
      if ( (pos->clength == 1) &&
	   (pos->mls_size > ss[0] - pos->c[0]) ) {
	/* mls allows us a direct jump to the
	   correct entry! */
	pos = &pos[ss[0] - pos->c[0]];
#if ASSERTS
	if (pos->c[0] != ss[0]) { /* check that mls works! */
	  tree->log(tree->context,
		    DOODLE_LOG_CRITICAL,
		    _("Assertion failed at %s:%d!\n"),
		    __FILE__, __LINE__);
	  return NULL;
	}
#endif
      } else {
	if (pos->link == NULL) {
	  if (pos->link_off != 0) {
	    if (-1 == loadLink(tree,
			       pos))
	      return NULL; /* error */
	  } else {
	    return NULL;
	  }
	}
	pos = pos->link;
      }
    }
  } /* while ss[0] != '\0' */
  return pos;
}

/**
 * Add keyword to suffix tree.
 *
 * @return 0 on success, 1 on error
 */
int DOODLE_tree_expand(struct DOODLE_SuffixTree * tree,
		       const char * searchString,
		       const char * fileName) {
  STNode * pos;
  STNode * spos;
  char * cisp;
  const char * cisp0;
  char * sharedName;
  int i;
  int cix;
  struct stat sbuf;
  unsigned int sharedNameIndex;

  if ( (searchString == NULL) ||
       (strlen(searchString) == 0) )
    return 1; /* not legal! */

  CHECK(tree);
  if (0 != stat(fileName,
		&sbuf)) {
    tree->log(tree->context,
	      DOODLE_LOG_CRITICAL,
	      _("Call to '%s' for file '%s' failed: %s\n"),
	      "stat",
	      fileName,
	      strerror(errno));
    return 1;
  }
  tree->mutationCount++;
  tree->log(tree->context,
	    DOODLE_LOG_INSANELY_VERBOSE,
	    _("Adding keyword '%s' for file '%s'.\n"),
	    searchString, fileName);

  if ( (tree->fnc > 0) &&
       (0 == strcmp(fileName,
		    tree->filenames[tree->fnc-1].filename))) {
    sharedName = tree->filenames[tree->fnc-1].filename;
    sharedNameIndex = tree->fnc-1;
  } else {
    sharedNameIndex = -1;
    for (i=tree->fnc-2;i>=0;i--) {
      if (0 == strcmp(fileName,
		      tree->filenames[i].filename)) {
	sharedName = tree->filenames[i].filename;
	sharedNameIndex = i;
	break;
      }
    }
    if (sharedNameIndex == -1) {
      tree->modified = 1;
      if (tree->fnc == tree->fns) {
	GROW(tree->filenames,
	     tree->fns,
	     tree->fns * 2 + 1);
      }
      sharedName = STRDUP(fileName);
      tree->filenames[tree->fnc].mod_time = (unsigned int) sbuf.st_mtime;
      tree->filenames[tree->fnc].filename = sharedName;
      sharedNameIndex = tree->fnc;
      tree->fnc++;
    }
  }
  cisp = "";
  if (tree->cisPos > 0) {
    cisp = tree->cis[tree->cisPos-1];
    if (strlen(cisp) > strlen(searchString))
      cisp = &cisp[strlen(cisp) - strlen(searchString)];
    else
      cisp = "";
  }
  if (0 != strcmp(cisp,
		  searchString)) {
    cix = -1;
#if (OPTIMIZE_SPACE && USE_CI_CACHE)
    spos = tree_search_internal(tree,
				searchString);
    pos = spos;
    while ( (pos != NULL) &&
	    ( (pos->cix == -1) ||
	      (pos->clength == 1) ) )
      pos = pos->child;
    if (pos != NULL) {
      cix = pos->cix; /* now != -1 AND clength > 1 */
      cisp = strstr(tree->cis[cix], searchString);
#if ASSERTS
      if (cisp == NULL) {
#if DEBUG > 2
	printf("Did not find '%s' in '%s' at cix %d\n",
	       searchString,
	       tree->cis[cix],
	       cix);
#endif
	tree->log(tree->context,
		  DOODLE_LOG_CRITICAL,
		  _("Assertion failed at %s:%d.\n"),
		  __FILE__, __LINE__);
#if DEBUG
	abort();
#endif	
	cix = -1;
      }
#endif
    }			
#endif
    if (cix == -1) {
      if (tree->cisLen == tree->cisPos) {
	GROW(tree->cis,
	     tree->cisLen,
	     2 + tree->cisLen*2);
      }
      tree->cis[tree->cisPos] = STRDUP(searchString);
      cisp = tree->cis[tree->cisPos];
      tree->cisPos++;
      cix = tree->cisPos-1;
    }
  } else { /* found with simple lookup */
    cix = tree->cisPos-1;
  }

  if (strlen(cisp) == 0) {
    /* search string empty!? */
    tree->log(tree->context,
	      DOODLE_LOG_CRITICAL,
	      _("Assertion failed at %s:%d.\n"),
	      __FILE__, __LINE__);
    return 1;
  }
  cisp0 = searchString;
  pos = tree->root;
  if (pos == NULL) {
    pos = MALLOC(sizeof(STNode));
    pos->mls_size = 1;
    pos->useCounter = 0;
    tree->used_memory += sizeof(STNode);
    pos->parent = NULL; /* root! */
    pos->c = cisp;
#if USE_CI_CACHE
    pos->cix = cix;
#endif
    pos->clength = strlen(cisp0);
    tree->root = pos;
    cisp0 = "";  /* done */
    markModified(pos);
  }
  MORE:
  while (cisp0[0] != '\0') {
    pos->useCounter++;

    if (cisp0[0] < pos->c[0]) {
      STNode * insert;
      /* head (non-mls) insert here! */
      insert = MALLOC(sizeof(STNode));	
      insert->mls_size = 1;
      insert->useCounter = 0;
      insert->link = pos;
      insert->link_off = pos->pos;
      if (pos->parent != NULL) {
	insert->parent = pos->parent;
	if (pos->parent->link == pos)
	  pos->parent->link = insert;
	else
	  pos->parent->child = insert;
      } else {
	tree->root = insert;
	insert->parent = NULL;
      }
      /* for now (for check), fixed later */
      insert->c = &CIS[(unsigned char)cisp[0]];
      insert->clength = 1;
      pos->parent = insert;
      tree->modified = 1;
      tree->used_memory += sizeof(STNode);
      pos = insert;
      CHECK(tree);
      markModified(insert);
      break;
    } else {
      if (pos->c[0] == cisp0[0]) {
	i = 1;
	while ( (i < pos->clength) &&
		(i < strlen(cisp0)) &&
		(pos->c[i] == cisp0[i]) )
	  i++;
	
	if ( (i > 1) &&
	     (i < pos->clength) ) {	
	  /* eat 1 < i < clength characters, create new
	     child by splitting current string at i */
	  tree_split(tree,
		     pos,
		     i);
	  cisp0 = &cisp0[i];
	  cisp = &cisp[i];
	} else if (i == pos->clength) {
	  /* ate i==clength characters, current child is then already
	     correct; */
	  cisp0 = &cisp0[i];
	  cisp = &cisp[i];
	} else {
	  /* only eat one character, create new artificial
	     child if clength > 1; */
	  tree_normalize(tree,
			 pos);
	  cisp++;
	  cisp0++;
	}
	if (cisp0[0] == '\0')
	  break; /* strlen(cisp0) == 0! */
	if (pos->child == NULL) {
	  if (pos->next_off != 0) {
	    if (-1 == loadChild(tree,
				pos))
	      return 1;	
	  } else {
	    tree->modified = 1;
	    pos->child = MALLOC(sizeof(STNode));
	    pos->child->mls_size = 1;
	    pos->child->useCounter = 0;
	    tree->used_memory += sizeof(STNode);
	    pos->child->parent = pos;
	    pos = pos->child;
	    /* for now (for check), fixed later */
	    pos->c = &CIS[(unsigned char)cisp[0]];
	    pos->clength = 1;
	    CHECK(tree);
	    markModified(pos);
	    break; /* strlen(cisp0) > 0! */
	  }
	}
	pos = pos->child;	
      } else {
	if (pos->link == NULL) {
	  if (pos->link_off != 0) {
	    if (-1 == loadLink(tree,
			       pos))
	      return 1;
	  } else {
	    /* append entry to linked list */
	    tree->modified = 1;
	    pos->link = MALLOC(sizeof(STNode));
	    pos->link->mls_size = 1;
	    pos->link->useCounter = 0;
	    tree->used_memory += sizeof(STNode);
	    pos->link->parent = pos;
	    pos = pos->link;
	    /* for now (for check), fixed later */
	    pos->c = &CIS[(unsigned char)cisp[0]];
	    pos->clength = 1;
 	    CHECK(tree);
	    markModified(pos);
	    break; /* strlen(cisp0) > 0! */
	  }
	} else {
#if ASSERTS
	  if (cisp0[0] <= pos->c[0]) {
	    tree->log(tree->context,
		      DOODLE_LOG_CRITICAL,
		      _("Assertion failed at %s:%d!\n"),
		      __FILE__, __LINE__);
	    return 1;
	  }
#endif
	  if ( (pos->clength == 1) &&
	       (pos->mls_size > cisp0[0] - pos->c[0]) ) {
	    /* mls allows us a direct jump to the
	       correct entry! */
	    pos = &pos[cisp0[0] - pos->c[0]];
#if ASSERTS
	    if (pos->c[0] != cisp0[0]) { /* check that mls works! */
	      tree->log(tree->context,
			DOODLE_LOG_CRITICAL,
			_("Assertion failed at %s:%d!\n"),
			__FILE__, __LINE__);
	      return 1;
	    }
#endif
	    continue;
	  }
	}
#if ASSERTS
	if (pos->link == NULL) {
	  tree->log(tree->context,
		    DOODLE_LOG_CRITICAL,
		    _("Assertion failed at %s:%d!\n"),
		    __FILE__, __LINE__);
	  return 1;
	}
#endif
	if (pos->link->c[0] > cisp0[0]) {
	  if (pos->mls_size == cisp0[0] - pos->c[0]) { /* expand mls range! */
	    STNode * mlsroot;
	    STNode * mlsnew;
	    int mls;
	
	    /* use mls! */
	    if (pos->clength != 1) {
	      /* need to split tree first to make mls possible */
	      tree_split(tree,
			 pos,
			 1);
	    }
	    /* find mls 'root' */
	    mlsroot = pos;
	    while ( (mlsroot->parent != NULL) &&
		    (mlsroot->parent->link == mlsroot) &&
		    (mlsroot->parent->mls_size > 1) )
	      mlsroot = mlsroot->parent;
	
	    if (pos->link->c[0] == cisp0[0] + 1) {
	      /* JOIN two mls segments (the new character fills the gap)! */
	      if (pos->link->clength != 1) {
		/* need to split tree first to make mls possible */
		tree_split(tree,
			   pos->link,
			   1);
	      }
#if ASSERTS
	      if ( (pos->mls_size != 1) ||
		   (pos->link->parent != pos) ) {
		tree->log(tree->context,
			  DOODLE_LOG_CRITICAL,
			  _("Assertion failed at %s:%d!\n"),
			  __FILE__, __LINE__);
		return 1;
	      }
#endif
	      mlsnew = MALLOC(sizeof(STNode) * (mlsroot->mls_size + pos->link->mls_size + 1));
	
	      memcpy(mlsnew,
		     mlsroot,
		     sizeof(STNode) * (mlsroot->mls_size));
	      mlsnew[mlsroot->mls_size].clength = 1;
	      memcpy(&mlsnew[mlsroot->mls_size+1],
		     pos->link,
		     sizeof(STNode) * (pos->link->mls_size));
	
	      /* adjust data in copy */
	      mlsnew[0].mls_size = mlsroot->mls_size + pos->link->mls_size + 1;
	      for (mls=1;mls<mlsnew->mls_size;mls++) {
		mlsnew[mls].mls_size = mlsnew->mls_size - mls;
		mlsnew[mls].parent = &mlsnew[mls-1];
		mlsnew[mls-1].link = &mlsnew[mls];
	      }
	      for (mls=0;mls<mlsnew->mls_size;mls++)
		if (mlsnew[mls].child != NULL)
		  mlsnew[mls].child->parent = &mlsnew[mls];	
	
	      /* update link to next entry */
	      mlsnew[mlsnew->mls_size-1].link = pos->link[pos->link->mls_size-1].link;
	      mlsnew[mlsnew->mls_size-1].link_off = pos->link[pos->link->mls_size-1].link_off;
	      if (pos->link[pos->link->mls_size-1].link != NULL)
		pos->link[pos->link->mls_size-1].link->parent = &mlsnew[mlsnew->mls_size-1];
	
	      /* update data in new entry */
	      mlsnew[mlsroot->mls_size].c = 1 + &CIS[(unsigned char) mlsnew[mlsroot->mls_size-1].c[0]];
	
	      /* update link from parent */
	      if (mlsroot->parent != NULL) {
		mlsnew[0].parent = mlsroot->parent;
		if (mlsroot->parent->link == mlsroot) {
		  mlsroot->parent->link = mlsnew;
		} else {
#if ASSERTS
		  if (mlsroot->parent->child != mlsroot) {
		    tree->log(tree->context,
			      DOODLE_LOG_CRITICAL,
			      _("Assertion failed at %s:%d!\n"),
			      __FILE__, __LINE__);
		    free(mlsnew);
		    return 1;
		  }
#endif
		  mlsroot->parent->child = mlsnew;
		}
	      } else {
#if ASSERTS
		if (tree->root != mlsroot) {
		  tree->log(tree->context,
			    DOODLE_LOG_CRITICAL,
			    _("Assertion failed at %s:%d!\n"),
			    __FILE__, __LINE__);
		  free(mlsnew);
		  return 1;
		}
#endif
		tree->root = mlsnew;
	      }
	
	      /* update pos to point to new location */
	      free(pos->link);
	      pos = &mlsnew[mlsroot->mls_size];
	      /* end joining 2 mls segments */
	    } else {
	      /* EXTEND existing mls segment by one entry! */
	      mlsnew = MALLOC(sizeof(STNode) * (mlsroot->mls_size + 1));	
	      memcpy(mlsnew,
		     mlsroot,
		     sizeof(STNode) * (mlsroot->mls_size));
	      mlsnew[mlsroot->mls_size].clength = 1;	

	      /* adjust data in copy */
	      mlsnew[0].mls_size = mlsroot->mls_size+1;	
	      for (mls=1;mls<mlsnew->mls_size;mls++) {
		mlsnew[mls].mls_size = mlsnew->mls_size - mls;
		mlsnew[mls].parent = &mlsnew[mls-1];
		mlsnew[mls-1].link = &mlsnew[mls];
	      }
	      for (mls=0;mls<mlsnew->mls_size;mls++)
		if (mlsnew[mls].child != NULL)
		  mlsnew[mls].child->parent = &mlsnew[mls];	
	
	      /* update link to next entry */
	      mlsnew[mlsnew->mls_size-1].link = pos->link;
	      mlsnew[mlsnew->mls_size-1].link_off = pos->link_off;
	      if (pos->link != NULL)
		pos->link->parent = &mlsnew[mlsnew->mls_size-1];
	
	      /* update data in new entry */
	      mlsnew[mlsroot->mls_size].c 
		= 1 + &CIS[(unsigned char) mlsnew[mlsroot->mls_size-1].c[0]];
#if ASSERTS	
	      if (mlsnew[mlsroot->mls_size].c[0] != cisp0[0]) {
		tree->log(tree->context,
			  DOODLE_LOG_CRITICAL,
			  _("Assertion failed at %s:%d!\n"),
			  __FILE__, __LINE__);
		return 1;
	      }
#endif
	      /* update link from parent */
	      if (mlsroot->parent != NULL) {
		mlsnew[0].parent = mlsroot->parent;
		if (mlsroot->parent->link == mlsroot) {
		  mlsroot->parent->link = mlsnew;
		} else {
#if ASSERTS
		  if (mlsroot->parent->child != mlsroot) {
		    tree->log(tree->context,
			      DOODLE_LOG_CRITICAL,
			      _("Assertion failed at %s:%d!\n"),
			      __FILE__, __LINE__);
		    free(mlsnew);
		    return 1;
		  }
#endif
		  mlsroot->parent->child = mlsnew;
		}
	      } else {
#if ASSERTS
		if (tree->root != mlsroot) {
		  tree->log(tree->context,
			    DOODLE_LOG_CRITICAL,
			    _("Assertion failed at %s:%d!\n"),
			    __FILE__, __LINE__);
		  free(mlsnew);
		  return 1;
		}
#endif
		tree->root = mlsnew;
	      }
	      /* update pos to point to new location */
	      pos = &mlsnew[mlsroot->mls_size];
	    }
	    free(mlsroot);	
	    tree->used_memory += sizeof(STNode);
	    tree->modified = 1;	  	
	    CHECK(tree);
	    for (mls=0;mls<mlsnew->mls_size;mls++)
	      markModified(&mlsnew[mls]);

#if ASSERTS	
	    if (*pos->c != cisp0[0]) {
	      tree->log(tree->context,
			DOODLE_LOG_CRITICAL,
			_("Assertion failed at %s:%d!\n"),
			__FILE__, __LINE__);
	      return 1;
	    }
#endif
	    continue;
	  } else {
	    STNode * insert;
	    /* normal (non-mls) insert here!  */
	    insert = MALLOC(sizeof(STNode));	
	    insert->mls_size = 1;
	    insert->useCounter = 0;
	    insert->link = pos->link;
	    insert->link_off = pos->link_off;
	    pos->link = insert;
	    pos->link_off = 0;	
	    insert->parent = pos;
	    insert->link->parent = insert;
	    tree->modified = 1;
	    tree->used_memory += sizeof(STNode);
	    pos = insert;
	    /* for now (for check), fixed later */
	    pos->c = &CIS[(unsigned char)cisp[0]];
	    pos->clength = 1;
	    CHECK(tree);	
	    markModified(pos);
	    break; /* strlen(cisp0) > 0! */
	  }
	} else {
	  pos = pos->link;	
	}
      }
    } /* end 'MORE: while-loop */
  } /* end 'else' */
  if (strlen(cisp0) > 0) {
    if (strlen(cisp0) == 1) {
      pos->c = &CIS[(unsigned char)cisp[0]];
      pos->clength = 1;
    } else {
      if (strlen(cisp0) > 255) {
	/* special case: we do not support more than 255
	   characters per node, so we need to create
	   more sub-nodes */
	pos->c = cisp;
#if USE_CI_CACHE
	pos->cix = cix;
#endif
	pos->clength = 255;
	cisp = &cisp[255];
	cisp0 = &cisp0[255];
	goto MORE;
      } else {
	pos->c = cisp;
#if USE_CI_CACHE
	pos->cix = cix;
#endif
	pos->clength = strlen(cisp0);
      }
    }
  }
  for (i=pos->matchCount-1;i>=0;i--)
    if (pos->matches[i] == sharedNameIndex)
      goto CLEANUP_SUCCESS;
  GROW(pos->matches,
       pos->matchCount,
       pos->matchCount+1);
  pos->matches[pos->matchCount-1] = sharedNameIndex;
  markModified(pos);

CLEANUP_SUCCESS:
  if (tree->used_memory > tree->memory_limit)
    shrinkMemoryFootprint(tree, tree->root);

  return 0; 	
}

static int truncate_internal(SuffixTree * tree,
			     STNode * node,
			     unsigned int fileNameIndex[],
			     int max) {
  STNode * next;
  STNode * parent;
  int i;
  int j;
  int k;

  if (node == NULL)
    return 0;
  parent = node->parent;
  while (node != NULL) {
    for (k=0;k<max;k++) {
      j = -1;
      for (i=node->matchCount-1;i>=0;i--) {
	if (node->matches[i] == fileNameIndex[k])
	  j = i;
      }
      if (j != -1) {
	node->matches[j] = node->matches[node->matchCount-1];
	GROW(node->matches,
	     node->matchCount,
	     node->matchCount-1);
	markModified(node);
      }
    }
    for (k=0;k<max;k++) {
      for (i=node->matchCount-1;i>=0;i--) {
	if (node->matches[i] == tree->fnc-k-1) {
	  node->matches[i] = fileNameIndex[k];
	  markModified(node);
	}
      }
    }
    if ( (node->child == NULL) &&
	 (node->next_off != 0) )
      if (-1 == loadChild(tree,
			  node))
	return -1;
    if (0 != truncate_internal(tree,
			       node->child,
			       fileNameIndex,
			       max))
      return -1;
    if ( (node->link == NULL) &&
	 (node->link_off != 0) )
      if (-1 == loadLink(tree,
			 node))
	return -1;
    CHECK(tree);
    next = node->link;
    if ( (node->matchCount == 0) &&
	 (node->child == NULL) &&
	 (node->mls_size == 1) && /* make sure this is not an MLS! */
	 ( (node->parent == NULL) ||
	   (node->parent->mls_size == 1) ||
	   (node->parent->link != node) ) ) {
      tree->used_memory -= sizeof(STNode);
      if (parent != NULL) {
	if (parent->link == node) {
	  parent->link = next;
	  parent->link_off = node->link_off;
	} else {
	  parent->child = next;
	  parent->next_off = node->next_off;
	}
      }
      if (next != NULL)
	next->parent = parent;
      if (parent == NULL)
	tree->root = next;
      markModified(parent);
      free(node);
      markModified(next);
    } else {
      parent = node;
    }
    CHECK(tree);
    node = next;
  }
  return 0;
}

/**
 * Remove all entries for the given filenames.
 * @param fileNames array of filenames, NULL terminated!
 */
int DOODLE_tree_truncate_multiple(SuffixTree * tree,
				  const char * fileNames[]) {
  unsigned int * delOff;
  int off;
  int rep;
  int err;
  int max;
  int i;
  int pos;

  CHECK(tree);
  max = 0;
  while (fileNames[max] != NULL) {
    tree->log(tree->context,
	      DOODLE_LOG_VERBOSE,
	      _("Removing the keywords for file '%s'.\n"),
	      fileNames[max++]);
  }
  if (max == 0)
    return 0;
  delOff = MALLOC(sizeof(int) * max);
  rep = tree->fnc;
  err = 0;
  pos = 0;
  for (off = rep-1;off>=0;off--) {
    for (i=0;i<max;i++) {
      if (0 == strcmp(tree->filenames[off].filename,
		      fileNames[i])) {
	tree->modified = 1;
	delOff[pos++] = off; /* delOff is sorted largest to smallest! */
      }
    }
  }
  max = pos;
  if (max == 0) {
    free(delOff);
    return 0;
  }
  err = truncate_internal(tree,
			  tree->root,
			  delOff,
			  max);
  for (i=0;i<max;i++) {
    free(tree->filenames[delOff[i]].filename);
    tree->filenames[delOff[i]] = tree->filenames[--rep];
  }
  free(delOff);
  /* DOODLE_tree_dump(stdout, tree); */
  CHECK(tree);
  tree->fnc = rep;
  if (tree->fnc <= tree->fns / 2) {
    GROW(tree->filenames,
	 tree->fns,
	 tree->fnc);
  }
  return err;
}

/**
 * Remove all entries for the given filename.
 */
int DOODLE_tree_truncate(SuffixTree * tree,
			 const char * fileName) {
  const char * filenames[2];

  filenames[0] = fileName;
  filenames[1] = NULL;
  return DOODLE_tree_truncate_multiple(tree, filenames);
}

/**
 * First check if all indexed files still exist and are
 * accessible.  If not, remove those entries.
 */
void DOODLE_tree_truncate_deleted(SuffixTree * tree,
				  DOODLE_Logger log,
				  void * logContext) {
  int i;
  unsigned int killCount;
  const char ** killNames;

  log(logContext,
      DOODLE_LOG_VERBOSE,
      _("Scanning filesystem in order to remove obsolete entries from existing database.\n"));
  killCount = 0;
  killNames = NULL;

  for (i=DOODLE_getFileCount(tree)-1;i>=0;i--) {
    struct stat sbuf;
    char * fn;
    int keep;

    keep = 1;
    fn = DOODLE_getFileAt(tree,i)->filename;
    if ( (0 != lstat(fn,
		     &sbuf)) &&
	 ( (errno == ENOENT) ||
	   (errno == ENOTDIR) ||
	   (errno == ELOOP) ||
	   (errno == EACCES) ) ) {
      log(logContext,
	  DOODLE_LOG_VERBOSE,
	  _("File '%s' could not be accessed: %s. Removing file from index.\n"),
	  fn,
	  strerror(errno));
      keep = 0;
    } else if (! S_ISREG(sbuf.st_mode)) {
      log(logContext,
	  DOODLE_LOG_VERY_VERBOSE,
	  _("File '%s' is not a regular file. Removing file from index.\n"),
	  fn);
      keep = 0;
    }
    if (keep == 0) {
      GROW(killNames,
	   killCount,
	   killCount+1);
      killNames[killCount-1] = fn;
    }
  }
  GROW(killNames,
       killCount,
       killCount+1); /* add NULL termination! */
  DOODLE_tree_truncate_multiple(tree,
				killNames);
  GROW(killNames,
       killCount,
       0);
}


/**
 * First check if all indexed files still exist and are
 * up-to-date.  If not, remove those entries.
 */
void DOODLE_tree_truncate_modified(SuffixTree * tree,
				   DOODLE_Logger log,
				   void * logContext) {
  int i;
  unsigned int killCount;
  const char ** killNames;

  log(logContext,
      DOODLE_LOG_VERBOSE,
      _("Scanning filesystem in order to remove obsolete entries from existing database.\n"));
  killCount = 0;
  killNames = NULL;

  for (i=DOODLE_getFileCount(tree)-1;i>=0;i--) {
    struct stat sbuf;
    char * fn;
    int keep;

    keep = 1;
    fn = DOODLE_getFileAt(tree,i)->filename;
    if ( (0 != lstat(fn,
		     &sbuf)) &&
	 ( (errno == ENOENT) ||
	   (errno == ENOTDIR) ||
	   (errno == ELOOP) ||
	   (errno == EACCES) ) ) {
      log(logContext,
	  DOODLE_LOG_VERBOSE,
	  _("File '%s' could not be accessed: %s. Removing file from index.\n"),
	  fn,
	  strerror(errno));
      keep = 0;
    } else if (! S_ISREG(sbuf.st_mode)) {
      log(logContext,
	  DOODLE_LOG_VERY_VERBOSE,
	  _("File '%s' is not a regular file. Removing file from index.\n"),
	  fn);
      keep = 0;
    } else if (DOODLE_getFileAt(tree,i)->mod_time !=
	       (unsigned int) sbuf.st_mtime) {
      keep = 0; /* modified! */
    }
    if (keep == 0) {
      GROW(killNames,
	   killCount,
	   killCount+1);
      killNames[killCount-1] = fn;
    }
  }
  GROW(killNames,
       killCount,
       killCount+1); /* add NULL termination! */
  DOODLE_tree_truncate_multiple(tree,
				killNames);
  GROW(killNames,
       killCount,
       0);
}



/**
 * @param do_links do we traverse the link list, too?
 *   (0 for the search-result root, 1 for the children)
 * @param callback function to call for each matching file
 * @param arg extra argument to callback
 * @return number of results
 */
static int tree_iterate_internal(int do_links,
				 SuffixTree * tree,
				 STNode * node,
				 DOODLE_ResultCallback callback,
				 void * arg) {
  int i;
  int ret;

  ret = 0;
  while (node != NULL) {
    for (i=node->matchCount-1;i>=0;i--) {
      if (callback != NULL)
	callback(&tree->filenames[node->matches[i]],
		 arg);
      ret++;
    }
    if ( (node->child == NULL) &&
	 (node->next_off != 0) ) {
      if (-1 == loadChild(tree,
			  node))
	return -1;
    }
    ret += tree_iterate_internal(1,
				 tree,
				 node->child,
				 callback,
				 arg);
    if (do_links == 0)
      return ret;
    if ( (node->link == NULL) &&
	 (node->link_off != 0) ) {
      if (-1 == loadLink(tree,
			 node))
	return -1;
    }
    node = node->link;
  }
  CHECK(tree);
  return ret;
}


/**
 * Search the suffix tree for matching strings.
 * @return 0 for not found, >0 number of results found
 */
int DOODLE_tree_search(SuffixTree * tree,
		       const char * substring,
		       DOODLE_ResultCallback callback,
		       void * arg) {
  STNode * pos;

  pos = tree_search_internal(tree,
			     substring);
  return tree_iterate_internal(0,
			       tree,
			       pos,
			       callback,
			       arg);
}

/**
 * Search the suffix tree for matching strings.
 *
 * @param pos current position in the tree
 * @param ignore_case for case-insensitive analysis
 * @param approx how many letters may we be off?
 * @param result pass address of pointer to NULL, will be set
 * @param resultSize pass address of integer of value 0, will be set
 * @return -1 on error, 0 for no results, >0 for number of results
 */
static int tree_search_approx_internal(STNode * pos,
				       const unsigned int approx,
				       const int ignore_case,
				       SuffixTree * tree,
				       const char * ss,
				       DOODLE_ResultCallback callback,
				       void * arg) {
  int ret;
  int iret;

  ret = 0;
  CHECK(tree);
  if (ss[0] == '\0') {
    /* search string empty!? */
    tree->log(tree->context,
	      DOODLE_LOG_CRITICAL,
	      _("Assertion failed at %s:%d!\n"),
	      __FILE__, __LINE__);
    return -1;
  }
  if (pos == NULL)
    return 0; /* huh? */
  if (pos->clength > 1)
    tree_normalize(tree,
		   pos); /* normalize! */

  while (pos != NULL) {
    if ( (pos->c[0] == ss[0]) ||
	 ( (ignore_case == 1) &&
	   (tolower(pos->c[0]) == tolower(ss[0])) ) ) {
      tree_normalize(tree, pos);
      if (ss[1] == '\0') {
	iret = tree_iterate_internal(0,
				     tree,
				     pos,
				     callback,
				     arg);	
	if (iret == -1)
	  return -1;
	ret += iret;
      } else {
	if ( (pos->child == NULL) &&
	     (pos->next_off != 0) )
	  if (-1 == loadChild(tree,
			      pos))
	    return -1;
	iret = tree_search_approx_internal(pos->child,
					   approx,
					   ignore_case,
					   tree,
					   ss+1,
					   callback,
					   arg);
	if (iret == -1)
	  return -1;
	ret += iret;
      }
    } /* end if exact match */ else if (approx > 0) {
      /* we have approx room */
      if (ss[1] == '\0') {
	ret += tree_iterate_internal(0,
				     tree,
				     pos,
				     callback,
				     arg);
 	return ret;
      }
      tree_normalize(tree, pos);

      if ( (pos->child == NULL) &&
	   (pos->next_off != 0) )
	if (-1 == loadChild(tree,
			    pos))
	  return -1;
      /* extra character in suffix-tree */
      iret = tree_search_approx_internal(pos->child,
					 approx-1,
					 ignore_case,
					 tree,
					 ss,
					 callback,
					 arg);
      if (iret == -1)
	return -1;
      ret += iret;
      /* character mismatch */
      iret = tree_search_approx_internal(pos->child,
					 approx-1,
					 ignore_case,
					 tree,
					 ss+1,
					 callback,
					 arg);
      if (iret == -1)
	return -1;
      ret += iret;
      /* extra character in ss */
      iret = tree_search_approx_internal(pos,
					 approx-1,
					 ignore_case,
					 tree,
					 ss+1,
					 callback,
					 arg);
      if (iret == -1)
	return -1;
      ret += iret;
    }
    if ( (pos->link == NULL) &&
	 (pos->link_off != 0) )
      if (-1 == loadLink(tree,
			 pos))
	return -1;
    pos = pos->link;
  }
  CHECK(tree);
  return ret;
}


/**
 * Search the suffix tree for matching strings.
 * @param pos tree->root at the beginning
 * @param ignore_case for case-insensitive analysis
 * @param approx how many letters may we be off?
 * @param result pass address of pointer to NULL, will be set
 * @param resultSize pass address of integer of value 0, will be set
 * @return -1 on error, 0 for no results, >0 for number of results
 */
int DOODLE_tree_search_approx(SuffixTree * tree,
			      const unsigned int approx,
			      const int ignore_case,
			      const char * ss,
			      DOODLE_ResultCallback callback,
			      void * arg) {
  return tree_search_approx_internal(tree->root,
				     approx,
				     ignore_case,
				     tree,
				     ss,
				     callback,
				     arg);
}


static int print_internal(SuffixTree * tree,
			  STNode * node,
			  FILE * stream,
			  int ident) {
  int i;

  CHECK(tree);
  while (node != NULL) {
    fprintf(stream,
	    "%*c%.*s:\n",
	    ident,
	    ' ',
	    (int)node->clength,
	    node->c);
    for (i=node->matchCount-1;i>=0;i--)
      fprintf(stream,
	      "%*c  %s\n",
	      ident,
	      ' ',
	      tree->filenames[node->matches[i]].filename);
    if ( (node->child == NULL) &&
	 (node->next_off != 0) )
      if (-1 == loadChild(tree,
			  node))
	return -1;
    print_internal(tree,
		   node->child,
		   stream,
		   ident+2);
    if ( (node->link == NULL) &&
	 (node->link_off != 0) )
      if (-1 == loadLink(tree,
			 node))
	return -1;
    node = node->link;
  }
  return 0;
}

/**
 * Print the suffix tree.
 */
int DOODLE_tree_dump(FILE * stream,
		     SuffixTree * tree) {
  CHECK(tree);
  if ( (tree == NULL) ||
       (stream == NULL) )
    return 1;
  return print_internal(tree,
			tree->root,
			stream,
			2);
}




/* ******************** JNI *********************** */


#ifdef HAVE_JNI_H

#include <jni.h>

/* gcj's jni.h does not define JNIEXPORT/JNICALL (at least
 * not in my version).  Sun defines it to 'empty' in Linux,
 * so that should work */
#ifndef JNIEXPORT
#define JNIEXPORT
#endif
#ifndef JNICALL
#define JNICALL
#endif

#include "org_gnunet_doodle_Doodle.h"
/*
 * Class:     org_gnunet_doodle_Doodle
 * Method:    getVersionInternal
 * Signature: ()I
 */
JNIEXPORT jint JNICALL Java_org_gnunet_doodle_Doodle_getVersionInternal
(JNIEnv * env, jclass clz) {
  return DOODLE_VERSION;
}

typedef struct {
  JNIEnv * env;
  jobject logger;
  SuffixTree * dst;
} JNICTX;

/**
 * Invokes the Logger.log method in Java.
 */
static void jni_logger(JNICTX * ctx,
		       unsigned int level,
		       const char * message,
		       ...) {
  size_t n;
  va_list ap;
  char * buf;
  jstring arg;
  jmethodID log;
  jclass logger;

  va_start(ap, message);
  n = vsnprintf(NULL, 0, message, ap);
  va_end(ap);
  buf = MALLOC(n);
  va_start(ap, message);
  n = vsnprintf(buf, n, message, ap);
  va_end(ap);

  logger = (*ctx->env)->GetObjectClass(ctx->env,
				       ctx->logger);
  log = (*ctx->env)->GetMethodID(ctx->env,
				 logger,
				 "log",
				 "(ILjava/lang/String;)V");
  arg = (*ctx->env)->NewStringUTF(ctx->env,
				  buf);
  free(buf);
  (*ctx->env)->CallVoidMethod(ctx->env,
			      ctx->logger,
			      log,
			      level,
			      arg);
}

/*
 * Class:     org_gnunet_doodle_Doodle
 * Method:    open
 * Signature: (Ljava/lang/String;Lorg/gnunet/doodle/Doodle$Logger;)J
 */
JNIEXPORT jlong JNICALL Java_org_gnunet_doodle_Doodle_open
(JNIEnv * env, jclass cl, jstring filename, jobject logger) {
  JNICTX * ret;
  const char * database;
  jboolean isCopy;

  database = (*env)->GetStringUTFChars(env,
				       filename,
				       &isCopy);
  ret = MALLOC(sizeof(JNICTX));
  ret->env = env;
  ret->logger = logger;
  ret->dst = DOODLE_tree_create((DOODLE_Logger) &jni_logger,
				ret,
				database);
  (*env)->ReleaseStringUTFChars(env,
				filename,
				database);
  if (ret->dst == NULL) {
    free(ret);
    return 0;
  }
  return (jlong) (long) ret;
}

/*
 * Class:     org_gnunet_doodle_Doodle
 * Method:    close
 * Signature: (J)V
 */
JNIEXPORT void JNICALL Java_org_gnunet_doodle_Doodle_close
(JNIEnv * env, jclass cl, jlong handle) {
  JNICTX * ret = (JNICTX*) (long) handle;

  DOODLE_tree_destroy(ret->dst);
  free(ret);
}

/*
 * Class:     org_gnunet_doodle_Doodle
 * Method:    truncate
 * Signature: (JLjava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_org_gnunet_doodle_Doodle_truncate
(JNIEnv * env, jclass cl, jlong handle, jstring filename) {
  JNICTX * ret = (JNICTX*) (long) handle;
  const char * fn;
  jboolean isCopy;

  fn = (*env)->GetStringUTFChars(env,
				 filename,
				 &isCopy);	
  DOODLE_tree_truncate(ret->dst,
		       fn);
  (*env)->ReleaseStringUTFChars(env,
				filename,
				fn);
}

/*
 * Class:     org_gnunet_doodle_Doodle
 * Method:    expand
 * Signature: (JLjava/lang/String;Ljava/lang/String;)Z
 */
JNIEXPORT jboolean JNICALL Java_org_gnunet_doodle_Doodle_expand
(JNIEnv * env, jclass cl, jlong handle, jstring key, jstring filename) {
  JNICTX * ret = (JNICTX*) (long) handle;
  int i;
  const char * fn;
  const char * ky;
  jboolean isCopy;

  fn = (*env)->GetStringUTFChars(env,
				 filename,
				 &isCopy);	
  ky = (*env)->GetStringUTFChars(env,
				 key,
				 &isCopy);	
  i = DOODLE_tree_expand(ret->dst,
			 ky,
			 fn);
  (*env)->ReleaseStringUTFChars(env,
				filename,
				fn);
  (*env)->ReleaseStringUTFChars(env,
				key,
				ky);
  return (jboolean) i;
}

/*
 * Class:     org_gnunet_doodle_Doodle
 * Method:    lastModified
 * Signature: (JLjava/lang/String;)J
 */
JNIEXPORT jlong JNICALL Java_org_gnunet_doodle_Doodle_lastModified
(JNIEnv * env, jclass cl, jlong handle, jstring filename) {
  JNICTX * ret = (JNICTX*) (long) handle;
  int i;
  const char * fn;
  const DOODLE_FileInfo * f;
  jboolean isCopy;

  fn = (*env)->GetStringUTFChars(env,
				 filename,
				 &isCopy);	
  for (i=DOODLE_getFileCount(ret->dst)-1;i>=0;i--) {
    f = DOODLE_getFileAt(ret->dst, i);
    if (0 == strcmp(fn, f->filename)) {
      (*env)->ReleaseStringUTFChars(env,
				    filename,
				    fn);
      return f->mod_time;
    }
  }
  (*env)->ReleaseStringUTFChars(env,
				filename,
				fn);
  return -1;
}

typedef struct {
  JNIEnv * env;
  jobject vect;
  jmethodID add;
} SearchClosure;

static void jni_search_callback(const DOODLE_FileInfo * fileinfo,
				SearchClosure * cls) {
  jobject fn;

  fn = (*cls->env)->NewStringUTF(cls->env,
				 fileinfo->filename);
  (*cls->env)->CallVoidMethod(cls->env,
			      cls->vect,
			      cls->add,
			      fn);
}

/*
 * Class:     org_gnunet_doodle_Doodle
 * Method:    search
 * Signature: (JLjava/lang/String;IZLjava/util/Vector;)I
 */
JNIEXPORT jint JNICALL Java_org_gnunet_doodle_Doodle_search
(JNIEnv * env, jclass cl, jlong handle, jstring key, jint fuzzy, jboolean ignCase, jobject vect) {
  JNICTX * ret = (JNICTX*) (long) handle;
  jclass vector;
  SearchClosure cls;
  const char * ky;
  int i;
  jboolean isCopy;

  ky = (*env)->GetStringUTFChars(env,
				 key,
				 &isCopy);	
  vector = (*env)->GetObjectClass(env,
				  vect);
  cls.env = env;
  cls.vect = vect;
  cls.add = (*env)->GetMethodID(env,
				vector,
				"addElement",
				"(Ljava/lang/Object;)V");
  i = DOODLE_tree_search_approx(ret->dst,
				fuzzy,
				ignCase,
				ky,
				(DOODLE_ResultCallback) &jni_search_callback,
				&cls);
  (*env)->ReleaseStringUTFChars(env,
				key,
				ky);
  return i;
}

/* end JNI API */
#endif

/* end of tree.c */
