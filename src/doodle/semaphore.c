/*
     This file is part of doodle.
     (C) 2001, 2002, 2003, 2004 Christian Grothoff (and other contributing authors)

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
 * @file doodle/semaphore.c
 * @brief functions related to threading and synchronization
 *
 * In particular, functions for mutexes, semaphores
 * and thread creation are provided.
 */

#include "config.h"
#include "semaphore.h"

#include <pthread.h>
#include <signal.h>

#if SOLARIS
#include <semaphore.h>
#endif
#if FREEBSD
#include <semaphore.h>
#endif
#if SOMEBSD
# include <pthread_np.h>
#endif
#if SOMEBSD || OSX
# include <sys/file.h>
#endif

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/types.h>

#if LINUX
# include <sys/ipc.h>
# include <sys/sem.h>
#endif
#ifdef _MSC_VER
#include <pthread.h>
#include <semaphore.h>
#endif

#include "gettext.h"

#define DEBUG_SEMUPDOWN NO

#define GNUNET_ASSERT(a) do { if (!(a)) abort(); } while (0)
#define MALLOC malloc
#define FREE free
#define STRDUP strdup
#define UNLINK unlink
#define FOPEN fopen
#define YES 1
#define OK 1
#define NO 0
#define SYSERR -1


/**
 * Shall we use error-checking (slow)
 * mutexes (e.g. for debugging)
 */
#define USE_CHECKING_MUTEX 1


typedef struct {
#if SOLARIS
  sem_t * internal;
#elif LINUX
  int internal;
  char * filename;
#elif FREEBSD5
  sem_t * internal;
#elif SOMEBSD || OSX || MINGW
  int initialValue;
  int fd;
  Mutex internalLock;
  char * filename;
#elif _MSC_VER
  int internal; /* KLB_FIX */
  char * filename;
#else
  /* FIXME! */
#endif
} IPC_Semaphore_Internal;




#ifndef PTHREAD_MUTEX_NORMAL
#ifdef PTHREAD_MUTEX_TIMED_NP
#define PTHREAD_MUTEX_NORMAL PTHREAD_MUTEX_TIMED_NP
#else
#define PTHREAD_MUTEX_NORMAL NULL
#endif
#endif

/**
 * This prototype is somehow missing in various Linux pthread
 * include files. But we need it and it seems to be available
 * on all pthread-systems so far. Odd.
 */
#ifndef _MSC_VER
extern int pthread_mutexattr_setkind_np(pthread_mutexattr_t *attr, int kind);
#endif
/* ********************* public methods ******************* */

void create_mutex_(Mutex * mutex) {
  pthread_mutexattr_t attr;
  pthread_mutex_t * mut;

#if WINDOWS
  attr = NULL;
#endif

  pthread_mutexattr_init(&attr);
#if USE_CHECKING_MUTEX
#if LINUX
  pthread_mutexattr_setkind_np(&attr,
			       PTHREAD_MUTEX_ERRORCHECK_NP);
#endif
#else
  pthread_mutexattr_setkind_np(&attr,
			       (int)PTHREAD_MUTEX_NORMAL);
#endif
  mut = MALLOC(sizeof(pthread_mutex_t));
  mutex->internal = mut;
  GNUNET_ASSERT(0 == pthread_mutex_init(mut, &attr));
}

void create_recursive_mutex_(Mutex * mutex) {
  pthread_mutexattr_t attr;
  pthread_mutex_t * mut;

  pthread_mutexattr_init(&attr);
#if LINUX
  GNUNET_ASSERT(0 == pthread_mutexattr_setkind_np
		(&attr,
		 PTHREAD_MUTEX_RECURSIVE_NP));
#elif SOMEBSD
  GNUNET_ASSERT(0 == pthread_mutexattr_setkind_np
		(&attr,
		 PTHREAD_MUTEX_RECURSIVE));
#elif SOLARIS
  GNUNET_ASSERT(0 == pthread_mutexattr_settype
		(&attr,
		 PTHREAD_MUTEX_RECURSIVE));
#elif OSX
  GNUNET_ASSERT(0 == pthread_mutexattr_settype
		(&attr,
		 PTHREAD_MUTEX_RECURSIVE));
#elif WINDOWS
  GNUNET_ASSERT(0 == pthread_mutexattr_settype
		(&attr,
		 PTHREAD_MUTEX_RECURSIVE));
#endif
  mut = MALLOC(sizeof(pthread_mutex_t));
  mutex->internal = mut;
  GNUNET_ASSERT(pthread_mutex_init(mut, &attr) == 0);
}

void destroy_mutex_(Mutex * mutex) {
  pthread_mutex_t * mut;
  mut = mutex->internal;
  if (mut == NULL) {
    abort();
    return;
  }
  mutex->internal = NULL;
  errno = 0;
  GNUNET_ASSERT(0 == pthread_mutex_destroy(mut));
  FREE(mut);
}

void mutex_lock_(Mutex * mutex,
		 const char * filename,
		 const int line) {
  pthread_mutex_t * mut;
  int ret;

  mut = mutex->internal;
  if (mut == NULL) {
    abort();
    return;
  }
  ret = pthread_mutex_lock(mut);
  if (ret != 0) {
    if (ret == EINVAL)
      abort();
    if (ret == EDEADLK)
      abort();
    GNUNET_ASSERT(0);
  }
}

void mutex_unlock_(Mutex * mutex,
		   const char * filename,
		   const int line) {
  pthread_mutex_t * mut;
  int ret;

  mut = mutex->internal;
  if (mut == NULL) {
    abort();
    return;
  }

  ret = pthread_mutex_unlock(mut);
  if (ret != 0) {
    if (ret == EINVAL)
      abort();
    if (ret == EPERM)
      abort();
  }
}

/**
 * function must be called prior to semaphore use -- handles
 * setup and initialization.  semaphore destroy (below) should
 * be called when the semaphore is no longer needed.
 */
Semaphore * semaphore_new_(int value,
			   const char * filename,
			   const int linenumber) {
  pthread_cond_t * cond;

  Semaphore * s = (Semaphore*)MALLOC(sizeof(Semaphore));
  s->v = value;
  MUTEX_CREATE(&(s->mutex));
  cond = MALLOC(sizeof(pthread_cond_t));
  s->cond = cond;
  pthread_cond_init(cond, NULL);
  return s;
}

void semaphore_free_(Semaphore * s,
		     const char * filename,
		     const int linenumber) {
  pthread_cond_t * cond;

  MUTEX_DESTROY(&(s->mutex));
  cond = s->cond;
  GNUNET_ASSERT(0 == pthread_cond_destroy(cond));
  FREE(cond);
  FREE(s);
}

/**
 * function increments the semaphore and signals any threads that
 * are blocked waiting a change in the semaphore.
 */
int semaphore_up_(Semaphore * s,
		  const char * filename,
		  const int linenumber) {
  int value_after_op;
  pthread_cond_t * cond;

  cond = s->cond;
  MUTEX_LOCK(&(s->mutex));
  (s->v)++;
  value_after_op = s->v;
  GNUNET_ASSERT(0 == pthread_cond_signal(cond));
  MUTEX_UNLOCK(&(s->mutex));
  return value_after_op;
}

/**
 * function decrements the semaphore and blocks if the semaphore is
 * <= 0 until another thread signals a change.
 */
int semaphore_down_(Semaphore * s,
		    const char * filename,
		    const int linenumber) {
  int value_after_op;
  int return_value;
  pthread_cond_t * cond;

  cond = s->cond;
  MUTEX_LOCK(&(s->mutex));
  while (s->v <= 0) {
    if ((return_value = pthread_cond_wait(cond,
					  (pthread_mutex_t*)s->mutex.internal)) != 0)
      abort();
  }
  (s->v)--;
  value_after_op = s->v;
  MUTEX_UNLOCK(&(s->mutex));
  return value_after_op;
}

/**
 * Function decrements the semaphore. If the semaphore would become
 * negative, the decrement does not happen and the function returns
 * SYSERR. Otherwise OK is returned.
 */
int semaphore_down_nonblocking_(Semaphore * s,
				const char * filename,
				const int linenumber) {
  MUTEX_LOCK(&(s->mutex));
  if (s->v <= 0) {
    MUTEX_UNLOCK(&(s->mutex));
    return SYSERR;
  }
  (s->v)--;
  MUTEX_UNLOCK(&(s->mutex));
  return OK;
}

/**
 * Returns YES if pt is the handle for THIS thread.
 */
int PTHREAD_SELF_TEST(PTHREAD_T * pt) {
  pthread_t * handle;

  handle = pt->internal;
  if (handle == NULL)
    return NO;
#if HAVE_NEW_PTHREAD_T
  if (handle->p == pthread_self().p)
#else
  if (*handle == pthread_self())
#endif
    return YES;
  else
    return NO;
}

/**
 * Get the handle for THIS thread.
 */
void PTHREAD_GET_SELF(PTHREAD_T * pt) {
  pt->internal = MALLOC(sizeof(pthread_t));
  *((pthread_t*)pt->internal) = pthread_self();
}

/**
 * Release handle for a thread.
 */
void PTHREAD_REL_SELF(PTHREAD_T * pt) {
  if (pt->internal != NULL)
    FREE(pt->internal);
  pt->internal = NULL;
}

/**
 * Create a thread. Use this method instead of pthread_create since
 * BSD may only give a 1k stack otherwise.
 *
 * @param handle handle to the pthread (for detaching, join)
 * @param main the main method of the thread
 * @param arg the argument to main
 * @param stackSize the size of the stack of the thread in bytes.
 *        Note that if the stack overflows, some OSes (seen under BSD)
 *        will just segfault and gdb will give a messed-up stacktrace.
 * @return see pthread_create
 */
int PTHREAD_CREATE(PTHREAD_T * pt,
		   PThreadMain main,
		   void * arg,
		   size_t stackSize) {
  pthread_t * handle;
  pthread_attr_t stack_size_custom_attr;
  int ret;

  handle = MALLOC(sizeof(pthread_t));
#ifdef MINGW
  memset(handle, sizeof(pthread_t), 0);
#endif

  pthread_attr_init(&stack_size_custom_attr);
  pthread_attr_setstacksize(&stack_size_custom_attr,
			    stackSize);
  ret = pthread_create(handle,
		       &stack_size_custom_attr,
		       main,
		       arg);			
  if (ret != 0) {
    FREE(handle);
    pt->internal = NULL;
    return ret;
  }
  pt->internal = handle;
  return ret;
}

void PTHREAD_JOIN(PTHREAD_T * pt,
		  void ** ret) {
  int k;
  pthread_t * handle;

  handle = pt->internal;
  GNUNET_ASSERT(handle != NULL);
  switch ((k=pthread_join(*handle, ret))) {
  case 0:
    FREE(handle);
    pt->internal = NULL;
    return;
  case ESRCH:
    abort();
  case EINVAL:
    abort();
  case EDEADLK:
    abort();
  default:
    abort();
  }
}

void PTHREAD_DETACH(PTHREAD_T * pt) {
  pthread_t * handle;

  handle = pt->internal;
  GNUNET_ASSERT(handle != NULL);
  pthread_detach(*handle);
  pt->internal = NULL;
  FREE(handle);
  return;
}

void PTHREAD_KILL(PTHREAD_T * pt,
		  int signal) {
  pthread_t * handle;

  handle = pt->internal;
  if (handle == NULL) {
    abort();
    return;
  }
  pthread_kill(*handle, signal);
}

/* end of semaphore.c */
