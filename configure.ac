# This file is part of doodle.
# (C) 2004, 2005, 2006, 2007, 2010 Christian Grothoff (and other contributing authors)
#
# doodle is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published
# by the Free Software Foundation; either version 2, or (at your
# option) any later version.
#
# doodle is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with doodle; see the file COPYING.  If not, write to the
# Free Software Foundation, Inc., 59 Temple Place - Suite 330,
# Boston, MA 02111-1307, USA.
#
#
# Process this file with autoconf to produce a configure script.
#
#
AC_PREREQ(2.57)
AC_INIT([doodle], [0.7.0],[christian@grothoff.org])
AM_INIT_AUTOMAKE([doodle], [0.7.0])
AM_CONFIG_HEADER([config.h])

# Hack for automake 1.10
export MKDIR_P="mkdir -p"

AH_TOP([#define _GNU_SOURCE  1])

# Checks for programs.
AC_PROG_AWK
AC_PROG_CC
AM_PROG_CC_C_O
AC_PROG_CPP
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET
AC_CANONICAL_HOST
AM_PROG_LIBTOOL
AC_PROG_MKDIR_P

CFLAGS="-Wall $CFLAGS"
# use '-fno-strict-aliasing', but only if the compiler can take it
if gcc -fno-strict-aliasing -S -o /dev/null -xc /dev/null >/dev/null 2>&1; 
then
 CFLAGS="-fno-strict-aliasing $CFLAGS"
fi

# Check system type
case "$host_os" in
*darwin* | *rhapsody* | *macosx*)
     AC_DEFINE_UNQUOTED(OSX,1,[This is an OS X system])
     CFLAGS="-no-cpp-precomp $CFLAGS"
     LDFLAGS="-flat_namespace -undefined suppress $LDFLAGS"
     ;;
linux*)
     AC_DEFINE_UNQUOTED(LINUX,1,[This is a Linux system])
     ;;
freebsd*)
     AC_DEFINE_UNQUOTED(SOMEBSD,1,[This is a BSD system])
     AC_DEFINE_UNQUOTED(FREEBSD,1,[This is a FreeBSD system])
     CFLAGS="-D_THREAD_SAFE $CFLAGS"
     ;;
openbsd*)
     AC_DEFINE_UNQUOTED(SOMEBSD,1,[This is a BSD system])
     AC_DEFINE_UNQUOTED(OPENBSD,1,[This is an OpenBSD system])
     ;;
netbsd*)
     AC_DEFINE_UNQUOTED(SOMEBSD,1,[This is a BSD system])
     AC_DEFINE_UNQUOTED(NETBSD,1,[This is a NetBSD system])
     ;;
*solaris*)
     AC_DEFINE_UNQUOTED(SOLARIS,1,[This is a Solaris system])
     AC_DEFINE_UNQUOTED(_REENTRANT,1,[Need with solaris or errno doesnt work])
     ;;
*arm-linux*)
     AC_DEFINE_UNQUOTED(LINUX,1,[This is a Linux system])
     CFLAGS="-D_REENTRANT -fPIC -pipe $CFLAGS"
     ;;
*cygwin*)
     AC_DEFINE_UNQUOTED(CYGWIN,1,[This is a Cygwin system])
     AC_DEFINE_UNQUOTED(WINDOWS,1,[This is a Windows system])
     LDFLAGS="$LDFLAGS -no-undefined"
     ;;
*mingw*)
     AC_DEFINE_UNQUOTED(MINGW,1,[This is a MinGW system])
     AC_DEFINE_UNQUOTED(WINDOWS,1,[This is a Windows system])
     LDFLAGS="$LDFLAGS -no-undefined -Wl,--export-all-symbols -lws2_32"
     ;;
*)
     AC_MSG_RESULT(Unrecognised OS $host_os)
     AC_DEFINE_UNQUOTED(OTHEROS,1,[Some strange OS])
;;
esac

AM_CONDITIONAL(CYGWIN, test "$build_os" = "cygwin")
AM_CONDITIONAL(MINGW, test "$build_os" = "mingw32")
AM_CONDITIONAL(SOLARIS, test "$build_os" = "solaris")

CHECK_PTHREAD
AC_SUBST(PTHREAD_LIBS)
AC_SUBST(PTHREAD_LDFLAGS)
AC_SUBST(PTHREAD_CPPFLAGS)

# pthread_t is a struct since 11/2004
AC_CHECK_MEMBER(pthread_t.p, AC_DEFINE(HAVE_NEW_PTHREAD_T, 1,
  [Define if you have a post 11/2004 pthread library]), , [#include <pthread.h>])



# test for libextractor
extractor=0
AC_ARG_WITH(extractor,
   [  --with-extractor=PFX    specify libextractor prefix],
   [AC_MSG_RESULT(libextractor prefix specified: $with_extractor)
    if test "$with_extractor" != "no"
    then
     LDFLAGS="-L$with_extractor/lib $LDFLAGS"
     CPPFLAGS="-I$with_extractor/include $CPPFLAGS"
     AC_CHECK_HEADERS(extractor.h,
     AC_CHECK_LIB([extractor], [EXTRACTOR_plugin_add_defaults],
       EXT_LIB_PATH="-L$with_extractor/lib $EXT_LIB_PATH"
       extractor=1))
    fi
   ],     
   [AC_MSG_RESULT([libextractor: --with-extractor not specified])
    AC_CHECK_HEADERS(extractor.h,
     AC_CHECK_LIB([extractor], [EXTRACTOR_plugin_add_defaults],
      extractor=1))])
AM_CONDITIONAL(USE_LIBEXTRACTOR, test x$extractor = x1)
AC_DEFINE_UNQUOTED(USE_LIBEXTRACTOR, $extractor, [libextractor available])
if test "x$extractor" = "x0"
then
  AC_ERROR([Doodle requires libextractor.])
fi

# Checks that are for things we need but that we don't act on
# are commented out...
# Checks for standard header files.
AC_HEADER_DIRENT
AC_HEADER_STDC
AC_HEADER_STAT
AC_CHECK_HEADERS([fcntl.h netinet/in.h limits.h locale.h libintl.h])

AC_CHECK_HEADERS([jni.h])
# jni.h is purely optional!  But if not present => no Java support!

AC_CHECK_HEADERS([fam.h])
# fam.h is optional, but no fam => no doodled

# test for fam 
saveLIBS=$LIBS
famlib=1
AC_CHECK_LIB(fam, FAMOpen,,famlib=0)
FAMLIBS=$LIBS
LIBS=$saveLIBS
AM_CONDITIONAL(HAVE_FAM, test x$famlib = x1)
AC_SUBST(FAMLIBS)

# Checks for standard typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_TYPE_SIZE_T
AC_TYPE_PID_T
AC_TYPE_OFF_T

# large file support
AC_SYS_LARGEFILE
AC_FUNC_FSEEKO

# Checks for library functions.
AC_FUNC_CLOSEDIR_VOID
AC_FUNC_MEMCMP
AC_FUNC_STAT
AC_FUNC_LSTAT
AC_FUNC_VPRINTF
AC_FUNC_FORK
AC_CHECK_FUNCS([fdatasync strstr getcwd memset strchr strdup strerror malloc setlocale])

AM_GNU_GETTEXT_VERSION([0.16.1])
AM_GNU_GETTEXT([external])

AC_SUBST(CPPFLAGS)
AC_SUBST(LIBS)
AC_SUBST(LDFLAGS)
AC_SUBST(EXT_LIB_PATH)
AC_SUBST(EXT_LIBS)

DOODLE_VERSION="0.7.0"
AX_CREATE_PKGCONFIG_INFO([doodle.pc],,[-ldoodle],[suffix-tree indexing library],,)


AC_CONFIG_FILES([ 
m4/Makefile 
Makefile
doc/Makefile
doc/man/Makefile
src/Makefile
src/doodle/Makefile
po/Makefile.in])
AC_OUTPUT
