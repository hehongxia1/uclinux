/* config.h.  Generated by hand for Windows. */
#ifndef CONFIG_H
#define CONFIG_H

/* ===================================================================
 * config.h
 *
 * config.h is derived from config.h.in -- do not edit config.h
 *
 * This contains variables that the configure script checks and
 * then defines or undefines. The source code checks for these
 * variables to know if certain features are present.
 * 
 * by Mark Gates <mgates@nlanr.net>
 *
 * Copyright  1999  The Board of Trustees of the University of Illinois
 * All rights reserved.  See doc/license.txt for complete text.
 *
 * $Id$
 * =================================================================== */

/* Define if threads exist (using pthreads or Win32 threads) */
/* #undef HAVE_POSIX_THREAD */
#define HAVE_WIN32_THREAD 1
/* #undef _REENTRANT */

/* Define if on OSF1 and need special extern "C" around some header files */
/* #undef SPECIAL_OSF1_EXTERN */

/* Define if the strings.h header file exists */
/* #undef HAVE_STRINGS_H */

/* Define the intXX_t, u_intXX_t, size_t, ssize_t, and socklen_t types */
/* On the Cray J90 there is no 4 byte integer, so we define int32_t
 * but it is 8 bytes, and we leave HAVE_INT32_T undefined. */
#define SIZEOF_INT 4
#define HAVE_U_INT16_T 1
#define HAVE_INT32_T   1
#define HAVE_INT64_T   1
#define HAVE_U_INT32_T 1

#define int32_t     LONG32
#define u_int16_t   UINT16
#define u_int32_t   ULONG32
/* #undef  size_t */
#define ssize_t int

/* socklen_t usually defined in <sys/socket.h>. Unfortunately it doesn't
 * work on some systems (like DEC OSF/1), so we'll use our own Socklen_t */
#define Socklen_t int

/* Define if you have these functions. */
#define HAVE_SNPRINTF 1
/* #undef HAVE_INET_PTON */
/* #undef HAVE_INET_NTOP */
/* #undef HAVE_GETTIMEOFDAY */
/* #undef HAVE_PTHREAD_CANCEL */
#define HAVE_USLEEP 1
/* #undef HAVE_QUAD_SUPPORT */
/* #undef HAVE_PRINTF_QD */

/* standard C++, which isn't always... */
/* #undef bool */
#define true   1
#define false  0

/* Define if the host is Big Endian (network byte order) */
/* #undef WORDS_BIGENDIAN */

/* Define if multicast support exists */
#define HAVE_MULTICAST 1

/* Define if all IPv6 headers/structures are present */
#define HAVE_IPV6 1

/* Define if IPv6 multicast support exists */
#define HAVE_IPV6_MULTICAST 1

/* Define if Debugging of sockets is desired */
/* #undef DBG_MJZ */
 
#endif /* CONFIG_H */
