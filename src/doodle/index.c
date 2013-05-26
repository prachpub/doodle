/*
     This file is part of doodle.
     (C) 2004, 2006, 2010 Christian Grothoff (and other contributing authors)

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
 * @file index.c
 * @brief index file using libextractor
 * @author Christian Grothoff
 *
 * This module effectively describes the bridge between libextractor
 * and libdoodle.  What is special is that LE is executed out-of-process
 * (to guard against bus-errors on corrupted file systems and possible
 * libextractor bugs)
 */

#include "config.h"
#include "helper2.h"
#include "gettext.h"
#include "doodle.h"
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>

#define DEBUG_IPC 0

typedef struct EXTRACT_Process {
  char * libs;
  void * log_ctx;
  int send_pipe;
  int read_pipe;
  pid_t pid;
  int do_default;
  DOODLE_Logger my_log;
} EXTRACT_Process;

struct EXTRACT_Process * forkExtractor(int do_default,
				       const char * libraries,
				       DOODLE_Logger logger,
				       void * log_ct) {
  EXTRACT_Process * ret;
  ret = malloc(sizeof(EXTRACT_Process));
  if (libraries != NULL)
    ret->libs = strdup(libraries);
  else
    ret->libs = NULL;
  ret->do_default = do_default;
  ret->pid = -1;
  ret->send_pipe = -1;
  ret->read_pipe = -1;
  ret->my_log = logger;
  ret->log_ctx = log_ct;
  return ret;
}

void joinExtractor(struct EXTRACT_Process * proc) {
  int status;

#if DEBUG_IPC
  fprintf(stderr, "Joining!\n");
#endif
  if (proc->send_pipe != -1)
    close(proc->send_pipe);
  if (proc->read_pipe != -1)
    close(proc->read_pipe);
  if (proc->pid != -1) {
    kill(proc->pid, SIGTERM);
    waitpid(proc->pid, &status, 0);
  }
  if (proc->libs != NULL)
    free(proc->libs);
  free(proc);
}

static int do_read(int fd,
		   void * d,
		   size_t len) {
  size_t pos;
  ssize_t ret;
  char *  data = d;

  pos = 0;
  ret = read(fd, &data[pos], len - pos);
  while ( (ret != -1) &&
	  (ret > 0) ) {
    pos += ret;
    if (pos == len)
      return 0;
    ret = read(fd, &data[pos], len - pos);
  }
  if (ret == 0)
    close(fd);
  return 1; /* error */
}

#if DEBUG_IPC
#define DO_WRITE(fd,data, len) if (len != write(fd,data,len)) { fprintf(stderr, "Write error!\n"); goto ERROR;}
#else
#define DO_WRITE(fd,data, len) if (len != write(fd,data,len)) goto ERROR;
#endif
#define DO_READ(fd,data,len) if (do_read(fd,data,len)) goto ERROR;
#define MAX_SLEN 16 * 1024 * 1024

struct AccuCtx
{
  unsigned int count;
  unsigned int size;
  enum EXTRACTOR_MetaType *types;
  char ** keywords;
};


static int 
accumulator(void *cls,
	    const char *plugin_name,
	    enum EXTRACTOR_MetaType type,
	    enum EXTRACTOR_MetaFormat format,
	    const char *data_mime_type,
	    const char *data,
	    size_t data_len)
{
  struct AccuCtx * ac = cls;
  unsigned int nsize;
  if ( (format != EXTRACTOR_METAFORMAT_UTF8) &&
       (format != EXTRACTOR_METAFORMAT_C_STRING) )
    return 0;
  if (ac->size == ac->count)
    {
      nsize = 4 + 2 * ac->size;
      ac->types = realloc (ac->types, nsize * sizeof(enum EXTRACTOR_MetaType));
      ac->keywords = realloc (ac->keywords, nsize * sizeof(char *));
      ac->size = nsize;
    }
  ac->types[ac->count] = type;
  ac->keywords[ac->count++] = strdup (data);
  return 0;
}


/**
 * @return 0 on success, 1 on error.
 */
static int do_fork(struct EXTRACT_Process * proc) {
  int filedes1[2];
  int filedes2[2];
  char buffer[FILENAME_MAX+2];
  size_t pos;
  ssize_t ret;
  size_t slen;
  struct EXTRACTOR_PluginList * list;
  char * filename;
  struct AccuCtx acc_ctx;

#if DEBUG_IPC
  fprintf(stderr, "Forking!\n");
#endif
  if (0 != pipe(filedes1)) {
    proc->my_log(proc->log_ctx,
		 DOODLE_LOG_CRITICAL,
		 _("Call to '%s' failed: %s\n"),
		 "pipe",
		 strerror(errno));
    return 1;
  }
  if (0 != pipe(filedes2)) {
    proc->my_log(proc->log_ctx,
		 DOODLE_LOG_CRITICAL,
		 _("Call to '%s' failed: %s\n"),
		 "pipe",
		 strerror(errno));
    close(filedes1[0]);
    close(filedes1[1]);
    return 1;
  } 
  /* log before fork, just to avoid out-of-process problems
     with my_log */
  if (proc->do_default)
    proc->my_log(proc->log_ctx,
		 DOODLE_LOG_VERY_VERBOSE,
		 (_("Loading default set of libextractor plugins.\n")));
  if (proc->libs != NULL) 
    proc->my_log(proc->log_ctx,
		 DOODLE_LOG_VERY_VERBOSE,
		 _("Loading libextractor plugins: '%s'\n"),
		 proc->libs);  

  proc->pid = fork();
  if (proc->pid == -1) {
    proc->my_log(proc->log_ctx,
		 DOODLE_LOG_CRITICAL,
		 _("Call to '%s' failed: %s\n"),
		 "fork",
		 strerror(errno));
    close(filedes1[0]);
    close(filedes1[1]);
    close(filedes2[0]);
    close(filedes2[1]);
    return 1;
  }
  if (proc->pid != 0) {
    close(filedes1[1]);
    close(filedes2[0]);
    proc->send_pipe = filedes2[1];
    proc->read_pipe = filedes1[0];
    return 0;
  }
  /* we're now in the forked process! */
  close(filedes1[0]);
  close(filedes2[1]);
  list = NULL;

  if (proc->do_default)
    list = EXTRACTOR_plugin_add_defaults(EXTRACTOR_OPTION_DEFAULT_POLICY);
  if (proc->libs != NULL) 
    list = EXTRACTOR_plugin_add_config(list,
				       proc->libs,
				       EXTRACTOR_OPTION_DEFAULT_POLICY);  
  
  pos = 0;
  buffer[FILENAME_MAX + 1] = '\0';
  ret = read(filedes2[0], &buffer[pos], FILENAME_MAX + 1 - pos);
  while ( (-1 != ret) &&
	  (ret != 0) ) {
    pos += ret;
    slen = strlen(buffer) + 1;
    if (slen <= pos) {
      filename = strdup(buffer);
      memmove(buffer,
	      &buffer[slen],
	      pos - slen);
      pos = pos - slen;
      acc_ctx.count = 0;
      acc_ctx.size = 0;
      acc_ctx.keywords = NULL;
      acc_ctx.types = NULL;
      EXTRACTOR_extract(list,
			filename,
			NULL, 0,
			&accumulator,
			&acc_ctx);
      DO_WRITE(filedes1[1], &acc_ctx.count, sizeof(unsigned int));
      while (0 != acc_ctx.count--) {
	DO_WRITE(filedes1[1], 
		 &acc_ctx.types[acc_ctx.count],
		 sizeof(enum EXTRACTOR_MetaType));
	slen = strlen(acc_ctx.keywords[acc_ctx.count]);
	if (slen > MAX_SLEN)
	  slen = MAX_SLEN; /* cut off -- far too large! */
	DO_WRITE(filedes1[1], &slen, sizeof(size_t));
	DO_WRITE(filedes1[1], acc_ctx.keywords[acc_ctx.count], slen);
	free(acc_ctx.keywords[acc_ctx.count]);
      }
      free (acc_ctx.keywords);
      free (acc_ctx.types);
      acc_ctx.keywords = NULL;
      acc_ctx.types = NULL;
      acc_ctx.size = 0;
    }
    ret = read(filedes2[0], &buffer[pos], FILENAME_MAX + 1 - pos);
  }
  /* exit / cleanup */
 ERROR:
  free (acc_ctx.keywords);
  free (acc_ctx.types);
  EXTRACTOR_plugin_remove_all (list);
  close(filedes2[0]);
  close(filedes1[1]);
  /* never return - we were forked! */
  exit(0);
  return 1; /* eh, dead */
}


struct KeywordList {
  /* the keyword that was found */
  char * keyword;
  /* the type of the keyword (classification) */
  enum EXTRACTOR_MetaType keywordType;
  /* the next entry in the list */
  struct KeywordList * next;
};


static void 
freeKeywords (struct KeywordList * keywords)
{
  struct KeywordList *prev;
  while (keywords != NULL)
    {
      prev = keywords;
      keywords = keywords->next;
      free (prev->keyword);
      free (prev);
    }
}



static struct KeywordList * 
getKeywords(struct EXTRACT_Process * eproc,
	    const char * filename) {
  struct KeywordList * pos;
  struct KeywordList * next;
  struct KeywordList * head;
  unsigned int count;
  size_t slen;
  int status;

  if (eproc->pid == -1)
    if (0 != do_fork(eproc))
      return NULL;
  if (eproc->pid == -1)
    return NULL;
  fprintf(stderr, "Processing file %s\n", filename);
  head = NULL;
  pos = NULL;
  next = NULL;
  DO_WRITE(eproc->send_pipe, filename, strlen(filename)+1);
  DO_READ(eproc->read_pipe, &count, sizeof(unsigned int));
  while (count-- > 0) {
    next = malloc(sizeof(struct KeywordList));
    DO_READ(eproc->read_pipe, &next->keywordType, sizeof(enum EXTRACTOR_MetaType));
    DO_READ(eproc->read_pipe, &slen, sizeof(size_t));
    if (slen == 0)
    {
      free(next);
      next = NULL;
      continue;
    }
    if (slen > MAX_SLEN) 
      goto ERROR; /* too large! something must have gone wrong! */
    if (pos == NULL) {
      pos = next;
      next = NULL;
      head = pos;
    } else {
      pos->next = next;
      next = NULL;
      pos = pos->next;
    }
    pos->next = NULL;
    pos->keyword = malloc(slen + 1);
    pos->keyword[slen] = '\0';
    DO_READ(eproc->read_pipe, pos->keyword, slen);
  }  
  return head;
 ERROR:
#if DEBUG_IPC
  fprintf(stderr, "READ ERROR!\n");
#endif
  if (next != NULL)
    free(next);
  freeKeywords(head);
  if (eproc->send_pipe != -1)
    close(eproc->send_pipe);
  eproc->send_pipe = -1;
  if (eproc->read_pipe != -1)
    close(eproc->read_pipe);
  eproc->read_pipe = -1;
  if (eproc->pid != -1) {
    kill(eproc->pid, SIGTERM); 
    waitpid(eproc->pid, &status, 0);
  }
  eproc->pid = -1;
  return NULL;  
}


/**
 * Find keywords in the given file and append
 * the string describing these keywords to the
 * long string (for the suffix tree).
 */
int buildIndex(struct EXTRACT_Process * eproc,
	       FILE * logFile,
	       const char * filename,
	       struct DOODLE_SuffixTree * tree,
	       int do_filenames) {
  struct KeywordList * head;
  struct KeywordList * pos;

  head = getKeywords(eproc, filename); 
  pos  = head;
  while (pos != NULL) {
    char * cpos;
    size_t slen;

    cpos = pos->keyword;
    if (logFile != NULL)
      fprintf(logFile, "%s\n", cpos);
    slen = strlen(cpos);
    if (slen > MAX_LENGTH) {
      char section[MAX_LENGTH+1];
      char * xpos;
      int j;

      section[MAX_LENGTH] = '\0';
      for (j=0;j<slen;j+=MAX_LENGTH/2) {
	strncpy(section,
		&cpos[j],
		MAX_LENGTH);
	xpos = &section[0];
	while (xpos[0] != '\0') {
	  if (0 != DOODLE_tree_expand(tree,
				      xpos,
				      filename)) {
	    freeKeywords(head);
	    return 0;
	  }
	  xpos++;
	}
      }
    } else {
      while (cpos[0] != '\0') {
	if (0 != DOODLE_tree_expand(tree,
				    cpos,
				    filename)) {
	  freeKeywords(head);
	  return 0;
	}
	cpos++;
      }
    }
    pos = pos->next;
  }
  freeKeywords(head);
  if (do_filenames) {
    const char * cpos = filename;
    while (cpos[0] != '\0') {
      if (0 != DOODLE_tree_expand(tree,
				  cpos,
				  filename))
	return 0;
      cpos++;
    }
  }
  return 1;
}
