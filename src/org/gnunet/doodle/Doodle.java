/*
     This file is part of doodle.
     (C) 2004 Christian Grothoff

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

package org.gnunet.doodle;

import java.util.Vector;
import java.util.Iterator;
import java.io.File;
import java.io.IOException;

/**
 * Java Binding for libdoodle.  Requires the C library libdoodle
 * to work properly.  If the library could not be linked, all
 * methods abort with an error code (if possible) or just
 * return without doing anything.  
 *
 * @see org.gnunet.libextractor.Extractor
 * @author Christian Grothoff
 */ 
public final class Doodle {

    /**
     * Does Doodle print warnings? (About loading libdoodle).
     */
    private static boolean warn_;

    /**
     * Doodle version.  0 if libdoodle was compiled without JNI/Java
     * support, in which case we better not call any native methods...
     */
    private final static int version_;

    static {	
	// first, initialize warn_
	boolean warn = false;
	try {
	    if (System.getProperty("libdoodle.warn") != null)
		warn = true;
	} catch (SecurityException se) {
	    // ignore
	} finally {
	    warn_ = warn;
	}

	// next, load library and determine version_
	int ver = 0;
	try {
	    System.loadLibrary("doodle");
	} catch (UnsatisfiedLinkError ule) {
	    ver = -1;
	    warn("Did not find doodle library: " + ule);
	}
	if (ver == 0) {
	    try {
		ver = getVersionInternal();
	    } catch (UnsatisfiedLinkError ule) {
		// warn: libdoodle compiled without Java support
		warn("doodle library compiled without Java support: " + ule);
	    }
	}
	version_ = ver;
    }    

    private static void warn(String warning) {
	if (warn_)
	    System.err.println("WARNING: " + warning);
    }

    /**
     * @return -1 if doodle library was not found, 0 if doodle library
     *  was found but compiled without JNI support, otherwise
     *  the doodle version number
     */
    public static int getVersion() {
	return version_;
    }

    /**
     * Main method for the Java-doodle.  This is just a little
     * piece of code to demo how to use the Doodle class and to
     * test it.  Invoke with the keyword to search for (optionally
     * preceeded with the database name).  Just prints all of the
     * filenames that were found.
     * 
     * This class can currently not be used to update the
     * database or to perform more complicated queries.  If you
     * want to have the full functionality of the C doodle program
     * you probably also want to use the Extractor class from
     * libextractor.
     */
    public static void main(String[] args) throws IOException {
	if (args.length != 2) {
	    System.out.println("Call with DBNAME KEYWORD\n");
	    return;
	}
	String dbName = args[0];
	String keyword = args[1];
	Doodle doo = new Doodle(dbName,
				new Logger() {
				    public void log(int level,
						    String msg) {
					System.err.println(msg);
				    }
				});
	Vector v = doo.search(keyword, 0, false);
	Iterator it = v.iterator();
	while (it.hasNext()) {
	    String s = (String) it.next();
	    File f = new File(s);
	    if (f.exists() && f.canRead())
		System.out.println(s);
	}
    }


    /**
     * Handle to the database (pointer, long for 64-bit architectures).
     */
    private long handle_;

    /**
     * Open a Doodle database.
     *
     * @param dbName the name of the database file
     * @param logger the logging mechanism to use (must not be null)
     */
    public Doodle(String dbName,
		  Logger logger) throws IOException {
	if ( (logger == null) || (dbName == null) )
	    throw new NullPointerException();
	if (version_ > 0) {
	    handle_ = open(dbName, logger);
	    if (handle_ == 0)
		throw new IOException("Failed to open Database " + dbName);
	} else {
	    logger.log(Logger.CRITICAL,
		       "libdoodle version " + version_ + " not supported (library not found?)");
	    handle_ = 0;
	}
    }

    /**
     * What was the last time that a searchString was
     * added for the given filename to the doodle DB?
     */
    public synchronized long getLastUpdateTime(String filename) {
	if (version_ == 0)
	    return 0;
	if (! isOpen())
	    throw new Error("Illegal call: database already closed.\n");
	return lastModified(handle_, filename);
    }

    /**
     * Add the given key-string for the given fileName
     * to the doodle-DB 
     *
     * @param searchString the search string to add
     * @param fileName the value for that search string
     * @return true on success, false on error
     */
    public synchronized boolean expand(String searchString,
				       String fileName) {
	if (version_ == 0)
	    return false;
	if (! isOpen())
	    throw new Error("Illegal call: database already closed.\n");
	return expand(handle_, searchString, fileName);
    }

    /**
     * Remove all searchstrings associated with the given
     * filename from the database.
     */
    public synchronized void remove(String filename) {
	if (version_ == 0)
	    return;
	if (! isOpen())
	    throw new Error("Illegal call: database already closed.\n");
	truncate(handle_, filename);
    }

    /**
     * Search for the given keyword.
     *
     * @return Vector<String> of search results (the filenames),
     *  null on error
     */
    public synchronized Vector search(String searchString,
				      int fuzzyness,
				      boolean ignoreCase) {
	if (version_ == 0)
	    return null;
	if (! isOpen())
	    throw new Error("Illegal call: database already closed.\n");
	Vector v = new Vector();
	if (-1 != search(handle_, searchString, fuzzyness, ignoreCase, v))
	    return v;
	else
	    return null;
    }
		      
    /**
     * Is the database still open?
     */
    public boolean isOpen() {
	return (handle_ != 0);
    }

    /**
     * Explicit closing of the database (otherwise the finalizer
     * should take care of it).
     */ 
    public synchronized void close() {
	if (version_ == 0)
	    return;
	if (! isOpen())
	    throw new Error("Illegal call: database already closed.\n");
	close(handle_);
	handle_ = 0;
    }

    /**
     * Finalizer to take care of 'unclean' shutdowns...
     */
    public void finalize() {
	if (isOpen())
	    close();
    }

    
    /* ********************* native calls ******************** */

    private static native int getVersionInternal();

    private static native long open(String name,
				    Logger log);

    private static native void close(long handle);
    
    private static native void truncate(long handle,
					String filename);
    
    private static native boolean expand(long handle,
					 String searchString,
					 String filename);
    
    private static native long lastModified(long handle,
					    String filename);
    
    private static native int search(long handle,
				     String searchString,
				     int fuzzyness,
				     boolean ignoreCase,
				     Vector v);

    /**
     * Logging interface to capture Doodle error messages.
     *
     * @author Christian Grothoff
     */
    public interface Logger {
	
	public static final int CRITICAL = 0;
	public static final int VERBOSE = 1;
	public static final int VERY_VERBOSE = 2;

	/**
	 * Log an error message of importance 'logLevel'.
	 */
	public void log(int logLevel,
			String message);

    } // end of Doodle.Logger

} // end of Doodle
