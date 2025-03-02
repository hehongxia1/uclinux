/*
 * Copyright 1992 by Jutta Degener and Carsten Bormann, Technische
 * Universitaet Berlin.  See the accompanying file "COPYRIGHT" for
 * details.  THERE IS ABSOLUTELY NO WARRANTY FOR THIS SOFTWARE.
 */

/*$Header: /usr/cvsroot/asterisk/codecs/gsm/inc/config.h,v 1.19 2004/06/26 03:50:14 markster Exp $*/

#ifndef	CONFIG_H
#define	CONFIG_H

#if 0
efine	SIGHANDLER_T	int 		/* signal handlers are void	*/
efine HAS_SYSV_SIGNAL	1		/* sigs not blocked/reset?	*/
#endif

#define	HAS_STDLIB_H	1		/* /usr/include/stdlib.h	*/
#if 0
efine	HAS_LIMITS_H	1		/* /usr/include/limits.h	*/
#endif
#define	HAS_FCNTL_H	1		/* /usr/include/fcntl.h		*/
#if 0
efine	HAS_ERRNO_DECL	1		/* errno.h declares errno	*/
#endif

#define	HAS_FSTAT 	1		/* fstat syscall		*/
#define	HAS_FCHMOD 	1		/* fchmod syscall		*/
#define	HAS_CHMOD 	1		/* chmod syscall		*/
#define	HAS_FCHOWN 	1		/* fchown syscall		*/
#define	HAS_CHOWN 	1		/* chown syscall		*/
#if 0
efine	HAS__FSETMODE 	1		/* _fsetmode -- set file mode	*/
#endif

#define	HAS_STRING_H 	1		/* /usr/include/string.h 	*/
#if 0
efine	HAS_STRINGS_H	1		/* /usr/include/strings.h 	*/
#endif

#define	HAS_UNISTD_H	1		/* /usr/include/unistd.h	*/
#define	HAS_UTIME	1		/* POSIX utime(path, times)	*/
#if 0
efine	HAS_UTIMES	1		/* use utimes()	syscall instead	*/
#endif
#define	HAS_UTIME_H	1		/* UTIME header file		*/
#if 0
efine	HAS_UTIMBUF	1		/* struct utimbuf		*/
efine	HAS_UTIMEUSEC   1		/* microseconds in utimbuf?	*/
#endif

#endif	/* CONFIG_H */
