/*
     This file is part of doodle.
     (C) 2001, 2002 Christian Grothoff (and other contributing authors)

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
#ifndef DOODLE_SHUTDOWN_H
#define DOODLE_SHUTDOWN_H
/**
 * @file doodle/shutdown.h
 * @brief code to allow clean shutdown of application with signals
 * @author Christian Grothoff
 *
 * Helper code for writing proper termination code when an application
 * receives a SIGTERM/SIGHUP etc.
 */


/**
 * Stop the application.
 * @param signum is ignored
 */
void run_shutdown(int signum);

/**
 * Test if the shutdown has been initiated.
 * @return YES if we are shutting down, NO otherwise
 */
int testShutdown();

/**
 * Initialize the signal handlers, etc.
 */
void initializeShutdownHandlers();

/**
 * Wait until the shutdown has been initiated.
 */
void wait_for_shutdown();

void doneShutdownHandlers();


#endif
