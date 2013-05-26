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
 * @file doodle/doodled.c
 * @brief doodle daemon for instant updates to the database
 * @author Christian Grothoff
 *
 * doodle links against libdoodle to use the code factored into
 * tree.c.  doodle uses GNU getopt and for portability ships with its
 * own version of the getopt code (getopt*).<p>
 *
 * doodle requires libextractor (http://gnunet.org/libextractor/).
 */

#include "config.h"
#include <unistd.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <dirent.h>
#include <signal.h>
#include <fam.h>
#include <locale.h>
#include "doodle.h"
#include "gettext.h"
#include "getopt.h"
#include "helper2.h"
#include "semaphore.h"
#include "shutdown.h"

static int verbose = 0;
static int very_verbose = 0;

/**
 * Print the doodle-specific text for --help.
 */
static void printHelp () {
  static Help help[] = {
    { 'd', "database", "FILENAME",
      gettext_noop("use location FILENAME to store doodle database") },
    { 'D', "debug", NULL,
      gettext_noop("run in debug mode, do not daemonize") },
    { 'f', "filenames", NULL,
      gettext_noop("add the filename to the list of keywords") },
    { 'h', "help", NULL,
      gettext_noop("print this help page") },
    { 'l', "library", "LIBRARY",
      gettext_noop("load an extractor plugin named LIBRARY") },
    { 'L', "log", "FILENAME",
      gettext_noop("log activity to a file named FILENAME") },
    { 'n', "nodefault", NULL,
      gettext_noop("do not load default set of extractor plugins") },
    { 'm', "memory", "SIZE",
      gettext_noop("set the memory limit to SIZE MB (for the tree).") },
    { 'P', "prunepaths", NULL,
      gettext_noop("exclude given paths from building or searching") },
    { 'v', "version", NULL,
      gettext_noop("print the version number") },
    { 'V', "verbose", NULL,
      gettext_noop("be verbose") },
    { 0, NULL, NULL, NULL },
  };
  formatHelp(_("doodled [OPTIONS] [FILENAMES]"),
	     _("Continuously index files in the background."),
	     help);
}

static int do_debug = 0;
static int do_default = 1;
static int do_filenames = 0;
static char * prunepaths = "/tmp /usr/tmp /var/tmp /dev /proc /sys";

/* *************** helper functions **************** */

static int isPruned(const char * filename) {
  int i;
  int last;

  i = strlen(prunepaths);
  last = i;
  while (i > 0) {
    while ( (prunepaths[i] != ' ') && (i >= 0) )
      i--;
    if (0 == strncmp(&prunepaths[i+1],
		     filename,
		     last - (i+1))) {
      return 1;
    }
    last = i;
    i--;
  }
  return 0;
}

/**
 * Print log-messages to the logfile.
 */
static void my_log(void * ctx,
		   unsigned int level,
		   const char * msg,
		   ...) {
  FILE * logfile = ctx;
  va_list args;
  if (logfile == NULL) {
    if (do_debug)
      logfile = stderr;
    else
      return; /* no logfile? no logging! */
  }
  if ( (0) ||
       (level == 0) ||
       (verbose && (level == 1)) ||
       (very_verbose && (level == 2) ) ) {
    va_start(args, msg);
    vfprintf(logfile, msg, args);
    va_end(args);
  }
}

/* ************** actual functionality code ************* */

/**
 * @brief closure for do_index and other processing functions
 */
typedef struct {
  struct EXTRACT_Process * elist;
  struct DOODLE_SuffixTree * tree;
  FAMConnection fc;
  FAMRequest * fr;
  char ** frNames;
  unsigned int frPos;
  unsigned int frSize;
  DOODLE_Logger log;
  void * logContext;
  char * ename; /* name of the DB */
  unsigned int treePresent;
  unsigned int argc;
  char ** argv;
  Mutex lock;
  unsigned int eventCount;
  char ** events;
  int continueRunning;
  unsigned int deferredCount;
  char ** deferredTruncations;
  Semaphore * signal;
} DIC;

/**
 * Process all pending events.
 * Helper thread for the worker thread.  Just there
 * to process FAM events.  Puts all events into the
 * queue for the main worker.
 */
static void * processEvents(void * arg) {
  DIC * cls = arg;
  FAMEvent fe;
  fd_set r;
  fd_set w;
  fd_set e;
  int fd = cls->fc.fd;
  int i;

  cls->log(cls->logContext,
	   DOODLE_LOG_VERY_VERBOSE,
	   _("Event processing thread created.\n"));
  while ( (0 == testShutdown()) &&
	  (cls->continueRunning) ) {
    char * name;

    FD_ZERO(&r);
    FD_ZERO(&w);
    FD_ZERO(&e);
    FD_SET(fd, &r);
    i = select(fd+1, &r, &w, &e, NULL);
    if (i == -1) {
      if (errno == EINTR)
	continue;
      cls->log(cls->logContext,
	       DOODLE_LOG_CRITICAL,
	       _("Call to '%s' failed: %s\n"),
	       "select",
	       strerror(errno));
    }
    if (!FD_ISSET(fd, &r))
      continue;

    if (! FAMPending(&cls->fc)) {
      /* this should only happen if there was a problem with
	 select, and in this case we better add some artificial
	 delay (busy waiting) */
      sleep(1);
      continue;
    }

    MUTEX_LOCK(&cls->lock);
    if (-1 == FAMNextEvent(&cls->fc, &fe)) {
      cls->log(cls->logContext,
	       DOODLE_LOG_CRITICAL,
	       _("Call to '%s' failed: %s\n"),
	       "FAMNextEvent",
	       FamErrlist[FAMErrno]);
      /* avoid fast, persistent errors */
      sleep(1);
      continue;
    }
    name = malloc(strlen(fe.filename) + strlen((char*) fe.userdata) + 2);
    if (fe.filename[0] == '/') {
      strcpy(name, fe.filename);
    } else {
      strcpy(name, (char*) fe.userdata);
      if ( (strlen((char*)fe.userdata) > 0) &&
	   (((char*)fe.userdata)[strlen(fe.userdata)-1] != DIR_SEPARATOR) )
	strcat(name, DIR_SEPARATOR_STR);
      strcat(name, fe.filename);
    }
    /* never process the doodle database itself... */
    if (0 == strncmp(name,
		     cls->ename,
		     strlen(cls->ename))) {
      free(name);
      MUTEX_UNLOCK(&cls->lock);
      continue;
    }


    cls->log(cls->logContext,
	     DOODLE_LOG_INSANELY_VERBOSE,
	     "FAM EVENT (%d,%s,%s) on file '%s'.\n",
	     fe.code,
	     fe.userdata,
	     fe.filename,
	     name);
    switch (fe.code) {
    case FAMCreated:
    case FAMChanged:	
    case FAMDeleted:
    case FAMMoved:
    case FAMAcknowledge:
    case FAMExists:
    case FAMEndExist:
      GROW(cls->events,
	   cls->eventCount,
	   cls->eventCount+1);
      cls->events[cls->eventCount-1] = strdup(name);
      SEMAPHORE_UP(cls->signal);
      break;	
    default:
      break;
    } /* end switch */
    free(name);
    MUTEX_UNLOCK(&cls->lock);
  } /* if event */
  cls->continueRunning = 0;
  SEMAPHORE_UP(cls->signal);

  return NULL;
}

static int do_index(const char * filename,
		    DIC * dic) {
  int i;
  int j;
  int k;
  struct stat sbuf;

  if (0 == strncmp(filename,
		   dic->ename,
		   strlen(dic->ename)))
    return 0; /* database (and database-temp file) is always pruned! */

  if (isPruned(filename))
    return 0;
  for (i=dic->deferredCount-1;i>=0;i--) {
    if (0 == strcmp(filename,
		    dic->deferredTruncations[i])) {
      free(dic->deferredTruncations[i]);
      dic->deferredTruncations[i]
	= dic->deferredTruncations[dic->deferredCount-1];
      GROW(dic->deferredTruncations,
	   dic->deferredCount,
	   dic->deferredCount-1);
    }
  }

  j = -1;
  for (i=DOODLE_getFileCount(dic->tree)-1;i>=0;i--) {
    if (0 == strcmp(filename,
		    DOODLE_getFileAt(dic->tree,i)->filename)) {
      j = i;
      break;
    }
  }

  k = -1;
  for (i=0;i<dic->frPos;i++) {
    if (0 == strcmp(filename,
		    dic->frNames[i])) {
      k=i;
      break;
    }
  }

  if (0 != lstat(filename,
		 &sbuf)) {
    dic->log(dic->logContext,
	     DOODLE_LOG_VERY_VERBOSE,
	     _("Call to '%s' for file '%s' failed: %s\n"),
	     "stat",
	     filename,
	     strerror(errno));
    if (j != -1) {
      /* remove old keywords, file no longer there? */
      GROW(dic->deferredTruncations,
	   dic->deferredCount,
	   dic->deferredCount + 1);
      dic->deferredTruncations[dic->deferredCount-1]
	= strdup(filename);
    }
    if (k != -1) {
      if (-1 == FAMCancelMonitor(&dic->fc,
				 &dic->fr[k])) {
	dic->log(dic->logContext,
		 DOODLE_LOG_CRITICAL,
		 _("Call to '%s' for file '%s' failed: %s\n"),
		 "FAMCancelMonitor",
		 filename,
		 FamErrlist[FAMErrno]);
      }
      free(dic->frNames[k]);
      dic->fr[k] = dic->fr[dic->frPos-1];
      dic->frNames[k] = dic->frNames[dic->frPos-1];
      dic->frNames[dic->frPos-1] = NULL;
      dic->frPos--;
    }
    return 0;
  }

  if ( (S_ISDIR(sbuf.st_mode)) &&
#ifdef S_ISLNK
       (! S_ISLNK(sbuf.st_mode)) &&
#endif
#ifdef S_ISSOCK
       (! S_ISSOCK(sbuf.st_mode)) &&
#endif
       (k == -1) ) {
    char * fn;
    if (dic->frPos == dic->frSize) {
      unsigned int s = dic->frSize;
      GROW(dic->fr,
	   dic->frSize,
	   dic->frSize * 2);
      GROW(dic->frNames,
	   s,
	   s * 2);
    }
    dic->log(dic->logContext,
	     DOODLE_LOG_VERY_VERBOSE,
	     _("Will monitor directory '%s' for changes.\n"),
	     filename);
    fn = STRDUP(filename);
    if (0 == FAMMonitorDirectory(&dic->fc,
				 filename,
				 &dic->fr[dic->frPos],
				 fn)) {
      dic->frNames[dic->frPos] = fn;
      dic->frPos++;
    } else {
      dic->log(dic->logContext,
	       DOODLE_LOG_CRITICAL,
	       _("Call to '%s' for file '%s' failed: %s\n"),
	       "FAMMonitorDirectory",
	       filename,
	       FamErrlist[FAMErrno]);
      free(fn);
    }
  }

  if (j != -1) {
    if (DOODLE_getFileAt(dic->tree,j)->mod_time == (unsigned int) sbuf.st_mtime) {
      return 0; /* already processed! */
    } else {
      /* remove old keywords, file changed! */
      /* we must do the new truncation now, so
	 we also do all of those that were
	 deferred */
      GROW(dic->deferredTruncations,
	   dic->deferredCount,
	   dic->deferredCount + 2);
      dic->deferredTruncations[dic->deferredCount-2]
	= strdup(filename);

      DOODLE_tree_truncate_multiple(dic->tree,
				    (const char**)dic->deferredTruncations);
      for (i=dic->deferredCount-2;i>=0;i--)
	free(dic->deferredTruncations[i]);
      GROW(dic->deferredTruncations,
	   dic->deferredCount,
	   0);
    }
  }

  if (S_ISREG(sbuf.st_mode)) {
    dic->log(dic->logContext,
	     DOODLE_LOG_VERY_VERBOSE,
	     _("Processing file '%s'.\n"),
	     filename);
    return buildIndex(dic->elist,
		      NULL, 
		      filename,
		      dic->tree,
		      do_filenames);
  }

  return 0;
}


/**
 * Main worker thread.  Register FAM events and process.
 */
static void * worker(void * arg) {
  DIC * cls = arg;
  int i;
  int more;
  int wasMore;
  int ret;
  void * unused;
  char * fn;
  PTHREAD_T helperThread;

  cls->log(cls->logContext,
	   DOODLE_LOG_VERY_VERBOSE,
	   _("Main worker thread created.\n"));
  cls->eventCount = 0;
  cls->continueRunning = 1;
  cls->events = NULL;
  cls->signal = SEMAPHORE_NEW(0);
  if (0 != PTHREAD_CREATE(&helperThread,
			  &processEvents,
			  cls,
			  64 * 1024)) {
    cls->log(cls->logContext,
	     DOODLE_LOG_CRITICAL,
	     _("Failed to spawn event processing thread.\n"));
    run_shutdown(0);
    return NULL;
  }

  cls->log(cls->logContext,
	   DOODLE_LOG_VERBOSE,
	   _("Registering with FAM for file system events.\n"));
  for (i=0;i<cls->argc;i++) {
    char * exp;

    cls->log(cls->logContext,
	     DOODLE_LOG_VERY_VERBOSE,
	     _("Indexing '%s'\n"),
	     cls->argv[i]);
    exp = expandFileName(cls->argv[i]);
    if (-1 == do_index(exp,
		       cls)) {
      ret = -1;
      free(exp);
      break;
    }
    free(exp);
  }
  DOODLE_tree_destroy(cls->tree);
  cls->treePresent = 0;
  cls->tree = NULL;

  cls->log(cls->logContext,
	   DOODLE_LOG_VERBOSE,
	   _("doodled startup complete.  Now waiting for FAM events.\n"));

  wasMore = 0;
  while ( (cls->continueRunning) &&
	  (0 == testShutdown()) ) {
    SEMAPHORE_DOWN(cls->signal);
    cls->log(cls->logContext,
	     DOODLE_LOG_INSANELY_VERBOSE,
	     "Received signal to process fam event.\n");
    MUTEX_LOCK(&cls->lock);
    if (cls->eventCount > 0) {
      fn = cls->events[cls->eventCount-1];
      GROW(cls->events,
	   cls->eventCount,
	   cls->eventCount-1);
      more = cls->eventCount > 0;
      cls->log(cls->logContext,
	       DOODLE_LOG_INSANELY_VERBOSE,
	       "Processing fam event '%s'.\n",
	       fn);
    } else {
      fn = NULL;
      more = 0;
    }
    if (! wasMore) {
      cls->treePresent++;
      if (cls->treePresent == 1)
	cls->tree = DOODLE_tree_create((DOODLE_Logger) cls->log,
				       cls->logContext,
				       cls->ename);
    }
    MUTEX_UNLOCK(&cls->lock);
    if (fn != NULL) {
      do_index(fn, cls);
      free(fn);
    }
    MUTEX_LOCK(&cls->lock);
    if (! more) {
      cls->treePresent--;
      if (cls->treePresent == 0)
	DOODLE_tree_destroy(cls->tree);
    }
    MUTEX_UNLOCK(&cls->lock);
    wasMore = more;
  } /* forever (until signal) */

  cls->continueRunning = 0;
  if (0 != FAMClose(&cls->fc)) {
    cls->log(cls->logContext,
	   DOODLE_LOG_CRITICAL,
	   _("Error disconnecting from fam.\n"));
  }
  PTHREAD_KILL(&helperThread, SIGTERM);
  PTHREAD_JOIN(&helperThread, &unused);
  SEMAPHORE_FREE(cls->signal);

  if (cls->treePresent > 0)
    DOODLE_tree_destroy(cls->tree);
  return NULL;
}

/**
 * Make sure the DB is current and then go into FAM-monitored mode
 * updating the DB all the time in the background.  Exits after a
 * signal (i.e.  SIGHUP/SIGINT) is received.
 */
static int build(const char * libraries,
		 const char * dbName,
		 size_t mem_limit,
		 const char * log,
		 int argc,		
		 char * argv[]) {
  int i;
  unsigned int ret;
  DIC cls;
  char * ename;
  FILE * logfile;
  PTHREAD_T workerThread;
  void * unused;

  cls.argc = argc;
  cls.argv = argv;
  cls.deferredCount = 0;
  cls.deferredTruncations = NULL;
  logfile = NULL;
  if (log != NULL) {
    logfile = fopen(log, "w+");
    if (logfile == NULL)
      my_log(stderr,
	     DOODLE_LOG_CRITICAL,
	     _("Could not open '%s' for logging: %s.\n"),
	     log,
	     strerror(errno));
  }
  cls.logContext = logfile;
  cls.log = &my_log;


  if (dbName == NULL) {
    my_log(logfile,
	   DOODLE_LOG_CRITICAL,
	   _("No database specified.  Aborting.\n"));
    return -1;
  }
  for (i=strlen(dbName);i>=0;i--) {
    if (dbName[i] == ':') {
      my_log(logfile,
	     DOODLE_LOG_CRITICAL,
	     _("'%s' is an invalid database filename (has a colon) for building database (option '%s').\n"),
	     dbName,
	     "-b");
      return -1;
    }
  }
  ename = expandFileName(dbName);
  if (ename == NULL)
    return -1;
  cls.ename = ename;
  cls.tree = DOODLE_tree_create(&my_log,
				logfile,
				ename);
  cls.treePresent = 1;
  if (cls.tree == NULL)
    return -1;
  if (mem_limit != 0)
    DOODLE_tree_set_memory_limit(cls.tree,
				 mem_limit);
  cls.elist = forkExtractor(do_default,
			    libraries,
			    &my_log,
			    logfile);
  if (cls.elist == NULL) {
    DOODLE_tree_destroy(cls.tree);
    return -1;
  }
  if (0 != FAMOpen2(&cls.fc, "doodled")) {
    my_log(logfile,
	   DOODLE_LOG_CRITICAL,
	   _("Failed to connect to fam.  Aborting.\n"));
    DOODLE_tree_destroy(cls.tree);
    return -1;
  }
  cls.fr = NULL;
  cls.frPos = 0;
  cls.frSize = 0;
  GROW(cls.fr,
       cls.frSize,
       128);
  cls.frNames = NULL;
  ret = 0;
  GROW(cls.frNames,
       ret,
       128);
  ret = 0;


  MUTEX_CREATE(&cls.lock);
  if (0 != PTHREAD_CREATE(&workerThread,
			  &worker,
			  &cls,
			  64 * 1024)) {
    my_log(logfile,
	   DOODLE_LOG_CRITICAL,
	   _("Failed to create worker thread: %s"),
	   strerror(errno));
    ret = -1;
  } else {
    wait_for_shutdown();
    cls.continueRunning = 0;
    SEMAPHORE_UP(cls.signal);
    PTHREAD_JOIN(&workerThread, &unused);
  }
  MUTEX_DESTROY(&cls.lock);

  my_log(logfile,
	 DOODLE_LOG_VERBOSE,
	 _("doodled is shutting down.\n"));
  if (cls.frPos == 0) {
    my_log(logfile,
	   DOODLE_LOG_CRITICAL,
	   _("No files exist that doodled would monitor for changes.  Exiting.\n"));
  }


  for (i=0;i<cls.frSize;i++) {
    if (cls.frNames[i] != NULL) {
      my_log(logfile,
	     DOODLE_LOG_VERBOSE,
	     _("Cancelling fam monitor '%s'.\n"),
	     cls.frNames[i]);
      free(cls.frNames[i]);
    }
  }

  for (i=cls.deferredCount-1;i>=0;i--)
    free(cls.deferredTruncations[i]);
  GROW(cls.deferredTruncations,
       cls.deferredCount,
       0);
  i = cls.frSize;
  GROW(cls.fr,
       cls.frSize,
       0);
  cls.frSize = i;
  GROW(cls.frNames,
       cls.frSize,
       0);
  my_log(logfile,
	 DOODLE_LOG_VERBOSE,
	 _("Unloading libextractor plugins.\n"));
  joinExtractor(cls.elist);
  free(ename);
  if (logfile != NULL)
    fclose(logfile);
  return ret;
}

/**
 * Fork and start a new session to go into the background
 * in the way a good deamon should.  Code from gnunetd.
 *
 * @param filedes pointer to an array of 2 file descriptors
 *        to complete the detachment protocol (handshake)
 */
static void detachFromTerminal(int * filedes) {
#ifndef MINGW
  pid_t pid;
  int nullfd;
#endif

  /* Don't hold the wrong FS mounted */
  if (chdir("/") < 0) {
    perror("chdir");
    exit(1);
  }

#ifndef MINGW
  pipe(filedes);
  pid = fork();
  if (pid < 0) {
    perror("fork");
    exit(1);
  }
  if (pid) {  /* Parent */
    int ok;
    char c;

    close(filedes[1]); /* we only read */
    ok = -1;
    while (0 < read(filedes[0], &c, sizeof(char))) {
      if (c == '.')
	ok = 0;
    }
    fflush(stdout);
    if (ok == 0)
      exit(0);
    else
      exit(1); /* child reported error */
  }
  close(filedes[0]); /* we only write */
  nullfd = open("/dev/null",
		O_CREAT | O_RDWR | O_APPEND, S_IWUSR | S_IRUSR);
  if (nullfd < 0) {
    perror("/dev/null");
    exit(1);
  }
  /* child - close fds linking to invoking terminal, but
   * close usual incoming fds, but redirect them somewhere
   * useful so the fds don't get reallocated elsewhere.
   */
  if (dup2(nullfd,0) < 0 ||
      dup2(nullfd,1) < 0 ||
      dup2(nullfd,2) < 0) {
    perror("dup2"); /* Should never happen */
    exit(1);
  }
  pid = setsid(); /* Detach from controlling terminal */
#else
 FreeConsole();
#endif
}

static void detachFromTerminalComplete(int * filedes) {
#ifndef MINGW
  char c = '.';
  write(filedes[1], &c, sizeof(char)); /* signal success */
  close(filedes[1]);
#endif
}

int main(int argc,
	 char * argv[]) {
  int c;
  int option_index;
  char * libraries = NULL;
  size_t mem_limit = 0;
  char * dbName;
  char * tmp;
  char * log = NULL;
  int ret;
  int filedes[2]; /* pipe between client and parent */

  setlocale (LC_ALL, "");
  bindtextdomain (PACKAGE, LOCALEDIR);
  textdomain (PACKAGE);

  dbName = getenv("DOODLE_PATH");
  if (NULL == dbName)
    dbName = "~/.doodle";
  tmp = getenv("PRUNEPATHS");
  if (tmp != NULL)
    prunepaths = tmp;

  while (1) {
    static struct option long_options[] = {
      {"database", 1, 0, 'd'},
      {"debug", 0, 0, 'D'},
      {"filenames", 0, 0, 'f'} ,
      {"help", 0, 0, 'h'},
      {"library", 1, 0, 'l'},
      {"log", 1, 0, 'L'},
      {"memory", 1, 0, 'm'},
      {"nodefault", 0, 0, 'n'},
      {"prunepaths", 1, 0, 'P' },
      {"version", 0, 0, 'v'},
      {"verbose", 0, 0, 'V'},
      {NULL, 0, 0, 0}
    };
    option_index = 0;
    c = getopt_long(argc,
		    argv, "d:Dfhl:L:m:nP:vV",
		    long_options,
		    &option_index);

    if (c == -1)
      break; /* No more flags to process */
    switch (c) {
    case 'd':
      dbName = optarg;
      break;
    case 'D':
      do_debug = 1;
      break;
    case 'f':
      do_filenames = 1;
      break;
    case 'h':
      printHelp();
      return 0;
    case 'l':
      libraries = optarg;
      break;
    case 'L':
      log = optarg;
      break;
    case 'm':
      if (1 != sscanf(optarg, "%ud", &mem_limit)) {
	printf(_("You must pass a number to the '%s' option.\n"),
	       "-m");
	return -1;
      }
      if (mem_limit > 0xFFFFFFFF / 1024 / 1024) {
	printf(_("Specified memory limit is too high.\n"));
	return -1;
      }
      mem_limit *= 1024 * 1024;
      break;
    case 'n':
      do_default = 0;
      break;
    case 'P':
      prunepaths = optarg;
      break;
    case 'V':
      if (verbose == 1)
	very_verbose = 1;
      verbose = 1;
      break;
    case 'v':
      printf(_("Version %s\n"),
	     PACKAGE_VERSION);
      return 0;
    default:
      fprintf(stderr,
	      _("Use '--help' to get a list of options.\n"));
      return -1;
    }  /* end of parsing commandline */
  } /* while (1) */

  if (argc - optind < 1) {
    fprintf(stderr,
	    _("Invoke with filenames or directories to monitor!\n"));
    return -1;
  }

  if (libraries != NULL)
    libraries = strdup(libraries);

  if (do_debug == 0)
    detachFromTerminal(filedes);
  initializeShutdownHandlers();
  if (do_debug == 0)
    detachFromTerminalComplete(filedes);

  ret = build(libraries,
	      dbName,
	      mem_limit,
	      log,
	      argc - optind,
	      &argv[optind]);
  doneShutdownHandlers();
  if (libraries != NULL)
    free(libraries);
  return ret;
}

/* end of doodled.c */
