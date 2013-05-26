/*
     This file is part of doodle.
     (C) 2004, 2010 Christian Grothoff (and other contributing authors)

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
 * @file doodle/doodle.c
 * @brief main method of doodle (binary).
 * @author Christian Grothoff
 *
 * doodle links against libdoodle to use the code factored into
 * tree.c.  doodle uses GNU getopt and for portability ships with its
 * own version of the getopt code (getopt*).<p>
 *
 * doodle requires libextractor (http://gnunet.org/libextractor/).
 */

#include "config.h"
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
#include <extractor.h>
#include <locale.h>
#include <langinfo.h>

#include "doodle.h"
#include "gettext.h"
#include "getopt.h"
#include "helper2.h"
#include "convert.h"


static int verbose = 0;
static int very_verbose = 0;

/**
 * Print the doodle-specific text for --help.
 */
static void printHelp () {
  static Help help[] = {
    { 'a', "approximate", "DISTANCE",
      gettext_noop("consider strings to match if DISTANCE letters are different") },
    { 'b', "build", NULL,
      gettext_noop("build database (default is to search)") },
    { 'd', "database", "FILENAME",
      gettext_noop("use location FILENAME to store doodle database") },
    { 'e', "extract", NULL,
      gettext_noop("for each matching file, print the extracted keywords") },
    { 'f', "filenames", NULL,
      gettext_noop("add the filename to the list of keywords (use when building database)") },
    { 'h', "help", NULL,
      gettext_noop("print this help page") },
    { 'i', "ignore-case", NULL,
      gettext_noop("be case-insensitive (use when searching)") },
    { 'l', "library", "LIBRARY",
      gettext_noop("load an extractor plugin named LIBRARY") },
    { 'L', "log", "FILENAME",
      gettext_noop("log keywords to a file named FILENAME") },
    { 'n', "nodefault", NULL,
      gettext_noop("do not load default set of extractor plugins") },
    { 'm', "memory", "SIZE",
      gettext_noop("set the memory limit to SIZE MB (for the tree).") },
    { 'p', "print", NULL,
      gettext_noop("print suffix tree (for debugging)") },
    { 'P', "prunepaths", NULL,
      gettext_noop("exclude given paths from building or searching") },
    { 'v', "version", NULL,
      gettext_noop("print the version number") },
    { 'V', "verbose", NULL,
      gettext_noop("be verbose") },
    { 0, NULL, NULL, NULL },
  };
  formatHelp(_("doodle [OPTIONS] ([FILENAMES]*|[KEYWORDS]*)"),
	     _("Index and search meta-data of files."),
	     help);
}

static int do_extract = 0;
static int do_default = 1;
static int do_print   = 0;
static int do_filenames = 0;
static int ignore_case = 0;
static unsigned int do_approx = 0;
static char * prunepaths = "/tmp /usr/tmp /var/tmp /dev /proc /sys";

/* *************** helper functions **************** */

/**
 * Print log-messages to stdout.
 */
static void my_log(void * unused,
		   unsigned int level,
		   const char * msg,
		   ...) {
  va_list args;
  if ( (level == 0) ||
       (verbose && (level == 1)) ||
       (very_verbose && (level == 2) ) ) {
    va_start(args, msg);
    vfprintf(stdout, msg, args);
    va_end(args);
  }
}

static int isPruned(const char * filename,
		    void * cls) {
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
      if (very_verbose)
	printf(_("Pruned: %s\n"), filename);
      return 1;
    }
    last = i;
    i--;
  }
  return 0;
}


/* ************** actual functionality code ************* */


/**
 * @brief closure for do_index
 */
typedef struct {
  struct EXTRACT_Process * elist;
  struct DOODLE_SuffixTree * tree;
  FILE * logFile;
} DIC;


static int do_index(const char * filename,
		    void * cls) {
  DIC * dic = cls;
  int i;
  int j;
  struct stat sbuf;

  if (isPruned(filename, NULL))
    return 0;
  j = -1;
  for (i=DOODLE_getFileCount(dic->tree)-1;i>=0;i--)
    if (0 == strcmp(filename,
		    DOODLE_getFileAt(dic->tree,i)->filename)) {
      j = i;
      break;
    }
  if (j != -1)
    return 0; /* already processed */
  if (0 != stat(filename,
		&sbuf)) {
    printf(_("Call to '%s' for file '%s' failed: %s\n"),
	   "stat",
	   filename,
	   strerror(errno));
    return 0;
  }
  if (S_ISREG(sbuf.st_mode)) {
    if (very_verbose)
      printf(_("Processing '%s'\n"),
	     filename);
    return buildIndex(dic->elist,
		      dic->logFile,
		      filename,
		      dic->tree,
		      do_filenames);
  } else
    return 0;
}

static int build(const char * libraries,
		 const char * dbName,
		 size_t mem_limit,
		 const char * log,
		 int argc,
		 char * argv[]) {
  int i;
  int ret;
  DIC cls;
  char * ename;

  if (dbName == NULL) {
    printf(_("No database specified.  Aborting.\n"));
    return -1;
  }
  for (i=strlen(dbName);i>=0;i--) {
    if (dbName[i] == ':') {
      printf(_("'%s' is an invalid database filename (has a colon) for building database (option '%s').\n"),
	     dbName,
	     "-b");
      return -1;
    }
  }
  ename = expandFileName(dbName);
  if (ename == NULL)
    return -1;
  /* unlink(ename); */
  cls.tree = DOODLE_tree_create(&my_log,
				NULL,
				ename);
  free(ename);
  if (cls.tree == NULL)
    return -1;
  if (mem_limit != 0)
    DOODLE_tree_set_memory_limit(cls.tree,
				 mem_limit);

  DOODLE_tree_truncate_modified(cls.tree,
			       &my_log,
			       NULL);

  cls.elist = forkExtractor(do_default,
			    libraries,
			    &my_log,
			    NULL);
  cls.logFile = NULL;
  if (log != NULL) {
    cls.logFile = fopen(log, "w+");
    if (cls.logFile == NULL)
      printf(_("Could not open '%s' for logging: %s.\n"),
	     log,
	     strerror(errno));
  }

  ret = 0;
  for (i=0;i<argc;i++) {
    char * exp;

    if (verbose)
      printf(_("Indexing '%s'\n"),
	     argv[i]);
    exp = expandFileName(argv[i]);
    if (-1 == scanDirectory(exp,
			    &my_log,
			    NULL,
			    &isPruned,
			    NULL,
			    &do_index,
			    &cls)) {
      ret = -1;
      free(exp);
      break;
    }
    free(exp);
  }
  joinExtractor(cls.elist);  
  DOODLE_tree_destroy(cls.tree);
  if (cls.logFile != NULL)
    fclose(cls.logFile);
  return ret;
}

/**
 * @brief closure for printIt
 */
typedef struct {
  struct EXTRACTOR_PluginList * list;
  char ** filenames_seen;
  unsigned int seen_size;
  unsigned int seen_count;
} PrintItArgs;



static void printIt(const DOODLE_FileInfo * fileinfo,
		    PrintItArgs * args) {
  struct EXTRACTOR_PluginList *list = args->list;
  const char * filename;
  int i;

  filename = fileinfo->filename;
  if (isPruned(filename, NULL))
    return;
  for (i=args->seen_count-1;i>=0;i--)
    if (0 == strcmp(filename,
		    args->filenames_seen[i]))
      return;
  if (args->seen_count == args->seen_size) {
    GROW(args->filenames_seen,
	 args->seen_size,
	 args->seen_size*2 + 128);
  }
  args->filenames_seen[args->seen_count++] = STRDUP(filename);
  if (! access(filename, R_OK | F_OK)) {
    if (do_extract) {
      /* print */
      printf(_("Keywords for matching file '%s':\n"),
	     filename);
      EXTRACTOR_extract (list,
			 filename,
			 NULL, 0,
			 &EXTRACTOR_meta_data_print,
			 stdout);
    } else {
      printf("%s\n",
	     filename);
    }
  }
}

static int print(const char * dbName) {
  struct DOODLE_SuffixTree * tree;
  char * ename;
  struct stat buf;
  int ret;

 if (dbName == NULL) {
    printf(_("No database specified. Aborting.\n"));
    return -1;
  }
  ename = expandFileName(dbName);
  if (0 != stat(ename, &buf)) {
    printf(_("Call to '%s' for file '%s' failed: %s.\n"),
	   "stat",
	   dbName,
	   strerror(errno));
    free(ename);
    return -1;
  }
  tree = DOODLE_tree_open_RDONLY(&my_log,
				 NULL,
				 ename);
  free(ename);
  if (tree == NULL)
    return -1;
  ret = DOODLE_tree_dump(stdout,
			 tree);
  DOODLE_tree_destroy(tree);
  return ret;
}

static int search(const char * libraries,
		  const char * dbName,
		  size_t mem_limit,
		  int argc,
		  char * argv[]) {
  int ret;
  struct stat buf;
  char * ename;
  struct DOODLE_SuffixTree * tree;
  struct EXTRACTOR_PluginList * extractors;
  int i;
  PrintItArgs args;
  char * utf;

  if (dbName == NULL) {
    printf(_("No database specified. Aborting.\n"));
    return -1;
  }
  ename = expandFileName(dbName);
  if (0 != stat(ename, &buf)) {
    printf(_("Call to '%s' for file '%s' failed: %s.\n"),
	   "stat",
	   dbName,
	   strerror(errno));
    free(ename);
    return -1;
  }
  tree = DOODLE_tree_open_RDONLY(&my_log,
				 NULL,
				 ename);
  if (mem_limit != 0)
    DOODLE_tree_set_memory_limit(tree,
				 mem_limit);

  free(ename);
  if (tree == NULL)
    return -1;
  if (do_extract) {
    if (do_default)
      extractors = EXTRACTOR_plugin_add_defaults(EXTRACTOR_OPTION_DEFAULT_POLICY);
    else
      extractors = NULL;
    if (libraries != NULL)
      extractors = EXTRACTOR_plugin_add_config(extractors,
					       libraries,
					       EXTRACTOR_OPTION_DEFAULT_POLICY);
  } else
    extractors = NULL;

  ret = 0;
  args.list = extractors;
  args.filenames_seen = NULL;
  args.seen_size = 0;
  args.seen_count = 0;

  for (i=0;i<argc;i++) {
    printf(_("Searching for '%s':\n"),
	   argv[i]);
    if (strlen(argv[i]) > MAX_LENGTH) {
      printf(_("Warning: search string is longer than %d characters, search will not work.\n"),
	     MAX_LENGTH);
      continue; /* no need to even try... */
    }
    if (strlen(argv[i]) > MAX_LENGTH/2) {
      printf(_("Warning: search string is longer than %d characters, search may not work properly.\n"),
	     MAX_LENGTH/2);
    }
    utf = convertToUtf8(argv[i],
			strlen(argv[i]),
			nl_langinfo(CODESET));
    if ( (do_approx == 0) &&
	 (ignore_case == 0) ) {
      if (0 == DOODLE_tree_search(tree,
				  utf,
				  (DOODLE_ResultCallback) &printIt,
				  &args)) {
	printf(_("\tNot found!\n"));
	ret++;
      }
    } else {
      if (0 == DOODLE_tree_search_approx(tree,
					 do_approx,
					 ignore_case,
					 utf,
					 (DOODLE_ResultCallback) &printIt,
					 &args)) {
	printf(_("\tNot found!\n"));
	ret++;
      }
    }
    free(utf);
  }
  DOODLE_tree_destroy(tree);
  EXTRACTOR_plugin_remove_all(extractors);

  for (i=0;i<args.seen_count;i++)
    free(args.filenames_seen[i]);
  GROW(args.filenames_seen,
       args.seen_size,
       0);
  return ret;
}

int main(int argc,
	 char * argv[]) {
  int c;
  int option_index;
  char * libraries = NULL;
  int do_build = 0;
  unsigned int mem_limit = 0;
  char * dbName;
  char * tmp;
  char * log = NULL;
  int ret;

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
      {"approximate", 1, 0, 'a'},
      {"build", 0, 0, 'b'},
      {"database", 1, 0, 'd'},
      {"extract", 0, 0, 'e'},
      {"filenames", 0, 0, 'f'} ,
      {"help", 0, 0, 'h'},
      {"ignore-case", 0, 0, 'i'},
      {"library", 1, 0, 'l'},
      {"log", 1, 0, 'L'},
      {"memory", 1, 0, 'm'},
      {"nodefault", 1, 0, 'n'},
      {"prunepaths", 1, 0, 'P' },
      {"print", 0, 0, 'p'},
      {"verbose", 0, 0, 'V'},
      {"version", 0, 0, 'v'},
      {NULL, 0, 0, 0}
    };
    option_index = 0;
    c = getopt_long(argc,
		    argv, "a:bd:efhil:L:m:nP:pVv",
		    long_options,
		    &option_index);

    if (c == -1)
      break; /* No more flags to process */
    switch (c) {
    case 'a':
      if (1 != sscanf(optarg, "%ud", &do_approx)) {
	printf(_("You must pass a number to the '%s' option.\n"),
	       "-a");
	return -1;
      }
      if (do_build == 1) {
	printf(_("The options '%s' and '%s' cannot be used together!\n"),
	       "-a", "-b");
	return -1;
      }	
      break;
    case 'b':
      do_build = 1;
      if (do_approx != 0) {
	printf(_("The options '%s' and '%s' cannot be used together!\n"),
	       "-a", "-b");
	return -1;
      }	
      if (do_print == 1) {
	printf(_("The options '%s' and '%s' cannot be used together!\n"),
	       "-b", "-p");
	return -1;
      }	
      if (ignore_case == 1) {
	printf(_("The options '%s' and '%s' cannot be used together!\n"),
	       "-b", "-i");
	return -1;
      }
      break;
    case 'd':
      dbName = optarg;
      break;
    case 'e':
      do_extract = 1;
      break;
    case 'f':
      do_filenames = 1;
      break;
    case 'h':
      printHelp();
      return 0;
    case 'i':
      ignore_case = 1;
      if (do_build == 1) {
	printf(_("The options '%s' and '%s' cannot be used together!\n"),
	       "-b", "-i");
	return -1;
      }
      break;
    case 'l':
      libraries = optarg;
      break;
    case 'L':
      log = optarg;
      break;
    case 'm':
      if (1 != sscanf(optarg, "%u", &mem_limit)) {
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
    case 'p':
      do_print = 1;
      if (do_build == 1) {
	printf(_("The options '%s' and '%s' cannot be used together!\n"),
	       "-b", "-p");
	return -1;
      }	
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

  if ( (do_print == 0) &&
       (argc - optind < 1) ) {
    fprintf(stderr,
	    do_build ?
	    _("Invoke with filenames or directories to index!\n") :
	    _("Invoke with query terms to search for!\n"));
    return -1;
  }

  if (libraries != NULL)
    libraries = strdup(libraries);
  if (do_build) {
    ret = build(libraries,
		dbName,
		mem_limit,
		log,
		argc - optind,
		&argv[optind]);
    if (libraries != NULL)
      free(libraries);
    return ret;
  } else if (do_print) {
    int i;
    char * name;

    ret = 0;
    name = strdup(dbName);
    for (i=strlen(name)-1;i>=0;i--) {
      if (name[i] == ':') {
	ret += print(&name[i+1]);		
	name[i] = '\0';
      }
    }
    ret = print(name);
    free(name);
    if (libraries != NULL)
      free(libraries);
    return ret;
  } else {
    int i;
    char * name;

    ret = 0;
    name = strdup(dbName);
    for (i=strlen(name)-1;i>=0;i--) {
      if (name[i] == ':') {
	ret += search(libraries,
		      &name[i+1],
		      mem_limit,
		      argc - optind,
		      &argv[optind]);
	name[i] = '\0';
      }
    }
    ret += search(libraries,
		  name,
		  mem_limit,
		  argc - optind,
		  &argv[optind]);
    free(name);
    if (libraries != NULL)
      free(libraries);
    return ret;
  }
}

/* end of doodle.c */
