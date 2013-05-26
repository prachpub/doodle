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
#ifndef DOODLE_SEMAPHORE_H
#define DOODLE_SEMAPHORE_H
/**
 * @file doodle/semaphore.h
 * @brief functions related to threading and synchronization
 *
 * In particular, functions for mutexes, semaphores
 * and thread creation are provided.
 */


/* we need size_t, and since it can be both unsigned int
   or unsigned long long, this IS platform dependent;
   but "stdlib.h" should be portable 'enough' to be
   unconditionally available... */
#include <stdlib.h>


/**
 * Main method of a thread.
 */
typedef void * (*PThreadMain)(void*);

typedef struct PTHREAD_T {
  void * internal;
} PTHREAD_T;


/**
 * @brief Structure for MUTual EXclusion (Mutex).
 *
 * Essentially a wrapper around pthread_mutex_t.
 */
typedef struct Mutex {
  void * internal;
} Mutex;

/**
 * @brief Semaphore abstraction implemented with pthreads
 */
typedef struct Semaphore {
  int v;
  Mutex mutex;
  /**
   * Wrapper for pthread condition variable.
   */
  void * cond;
} Semaphore;

#define SEMAPHORE_NEW(value) semaphore_new_(value, __FILE__, __LINE__)
#define SEMAPHORE_FREE(s) semaphore_free_(s, __FILE__, __LINE__)
#define SEMAPHORE_DOWN(s) semaphore_down_(s, __FILE__, __LINE__)
#define SEMAPHORE_DOWN_NONBLOCKING(s) semaphore_down_nonblocking_(s, __FILE__, __LINE__)
#define SEMAPHORE_UP(s) semaphore_up_(s, __FILE__, __LINE__)

#if DEBUG_LOCKING
#define MUTEX_CREATE(a) do { \
  fprintf(stderr, \
          "Creating mutex %x at line %d in file %s\n", \
          (int) a, __LINE__, __FILE__); \
  create_mutex_(a); \
}\
while(0)
#define MUTEX_CREATE_RECURSIVE(a) do { \
  fprintf(stderr, \
          "Creating recursive mutex %x at line %d in file %s\n", \
          (int) a, __LINE__, __FILE__); \
  create_recursive_mutex_(a); \
}\
while(0)
#define MUTEX_DESTROY(a) do { \
  fprintf(stderr, \
          "Destroying mutex %x at line %d in file %s\n", \
          (int) a, __LINE__, __FILE__); \
  destroy_mutex_(a); \
}\
while(0)
#define MUTEX_LOCK(a) do { \
  fprintf(stderr, \
          "Aquireing lock %x at %s:%d\n", \
          (int)a, __FILE__, __LINE__); \
  mutex_lock_(a, __FILE__, __LINE__); \
}\
while (0)
#define MUTEX_UNLOCK(a) do { \
  fprintf(stderr, \
         "Releasing lock %x at %s:%d\n", \
	(int)a, __FILE__, __LINE__); \
  mutex_unlock_(a, __FILE__, __LINE__); \
}\
while (0)
#else
#define MUTEX_LOCK(a) mutex_lock_(a, __FILE__, __LINE__)
#define MUTEX_UNLOCK(a) mutex_unlock_(a, __FILE__, __LINE__)
#define MUTEX_CREATE(a) create_mutex_(a)
#define MUTEX_CREATE_RECURSIVE(a) create_recursive_mutex_(a)
#define MUTEX_DESTROY(a) destroy_mutex_(a)
#endif

/**
 * Returns YES if pt is the handle for THIS thread.
 */
int PTHREAD_SELF_TEST(PTHREAD_T * pt);

/**
 * Get the handle for THIS thread.
 */
void PTHREAD_GET_SELF(PTHREAD_T * pt);

/**
 * Release handle for a thread (should have been
 * obtained using PTHREAD_GET_SELF).
 */
void PTHREAD_REL_SELF(PTHREAD_T * pt);

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
int PTHREAD_CREATE(PTHREAD_T * handle,
		   PThreadMain main,
		   void * arg,
		   size_t stackSize);

void PTHREAD_JOIN(PTHREAD_T * handle,
		  void ** ret);

void PTHREAD_KILL(PTHREAD_T * handle,
		  int signal);

void PTHREAD_DETACH(PTHREAD_T * handle);

/**
 * While we must define these globally to make the
 * compiler happy, always use the macros in the sources
 * instead!
 */
void create_mutex_(Mutex * mutex);
void create_recursive_mutex_(Mutex * mutex);
void create_fast_mutex_(Mutex * mutex);
void destroy_mutex_(Mutex * mutex);
void mutex_lock_(Mutex * mutex,
		 const char * filename,
		 const int linenumber);
void mutex_unlock_(Mutex * mutex,
		   const char * filename,
		   const int linenumber);
Semaphore * semaphore_new_(int value,
			   const char * filename,
			   const int linenumber);
void semaphore_free_(Semaphore * s,
		     const char * filename,
		     const int linenumber);
int semaphore_down_(Semaphore * s,
		    const char * filename,
		    const int linenumber);
int semaphore_down_nonblocking_(Semaphore * s,
				const char * filename,
				const int linenumber);
int semaphore_up_(Semaphore * s,
		  const char * filename,
		  const int linenumber);		
#endif
