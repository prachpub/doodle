# Globals
SUBDIRS = .

INCLUDES = \
  -I$(top_srcdir)/src/include

LIBS = \
 @LTLIBINTL@ @LIBS@

# Define a C macro LOCALEDIR indicating where catalogs will be installed.
localedir = $(datadir)/locale
DEFS = -DLOCALEDIR=\"$(localedir)\" @DEFS@

EXTRA_DIST = \
 getopt.h \
 helper1.h \
 helper2.h \
 org_gnunet_doodle_Doodle.h \
 gettext.h


if HAVE_FAM
DOD=doodled
endif


# install
include_HEADERS = \
 doodle.h

bin_PROGRAMS = \
 doodle $(DOD)

lib_LTLIBRARIES = \
 libdoodle.la

# no-inst

noinst_LTLIBRARIES = \
 libhelper1.la \
 libhelper2.la

noinst_PROGRAMS = \
 logreplay

# libdoodle
libdoodle_la_LDFLAGS = \
 -export-dynamic -version-info 2:1:1

libdoodle_la_SOURCES = \
 tree.c 

libdoodle_la_LIBADD = \
 libhelper1.la 

# doodle
doodle_SOURCES = \
 convert.c convert.h \
 doodle.c 

doodle_LDFLAGS = \
 -lextractor 

doodle_LDADD = @LTLIBINTL@ \
 $(top_builddir)/src/doodle/libdoodle.la \
 libhelper2.la 

#doodled
doodled_SOURCES = \
 doodled.c \
 semaphore.c semaphore.h \
 shutdown.c shutdown.h

doodled_LDFLAGS = \
 -lextractor @FAMLIBS@ @PTHREAD_LDFLAGS@
doodled_CPPFLAGS = \
 @PTHREAD_CPPFLAGS@

doodled_LDADD = @LTLIBINTL@ @PTHREAD_LIBS@ \
 $(top_builddir)/src/doodle/libdoodle.la \
 libhelper2.la

#libhelper's
# helper1 is what everyone links against (malloc/grow/strdup)
# helper2 is what doodle and doodled link against
# The split is required since helper2 links against LE, and
# libdoodle should not be linked against LE.
libhelper1_la_SOURCES = \
 grow.c

libhelper2_la_SOURCES = \
 help.c \
 files.c \
 index.c \
 getopt.c \
 getopt1.c 


#logreplay
logreplay_SOURCES = \
 logreplay.c 

logreplay_LDADD = @LTLIBINTL@ \
 $(top_builddir)/src/doodle/libdoodle.la \
 libhelper2.la

#tests
check_PROGRAMS = \
 testio \
 testtree \
 testtree2 \
 testtree3 \
 testtree4 \
 proftree \
 proftree2 \
 proftree3

TESTS = $(check_PROGRAMS)

testio_SOURCES = \
 testio.c 
testio_LDADD = \
 libhelper1.la

testtree_SOURCES = \
 testtree.c 
testtree_LDADD = \
 libhelper1.la

testtree2_SOURCES = \
 testtree2.c
testtree2_LDADD = \
 libhelper1.la

testtree3_SOURCES = \
 testtree3.c
testtree3_LDADD = \
 libhelper1.la

testtree4_SOURCES = \
 testtree4.c 
testtree4_LDADD = \
 libhelper1.la

proftree_SOURCES = \
 proftree.c 
proftree_LDADD = \
 libhelper1.la

proftree2_SOURCES = \
 proftree2.c
proftree2_LDADD = \
 libhelper1.la

proftree3_SOURCES = \
 proftree3.c
proftree3_LDADD = \
 libhelper1.la