dnl -*- autoconf -*-
AC_DEFUN([LP_SETUP_EXOSIP],[
AC_REQUIRE([AC_CANONICAL_HOST])
AC_REQUIRE([LP_CHECK_OSIP2])

dnl eXosip embeded stuff
EXOSIP_CFLAGS="$OSIP_CFLAGS -DOSIP_MT "
EXOSIP_LIBS="-leXosip2 $OSIP_LIBS "

CPPFLAGS_save=$CPPFLAGS
CPPFLAGS=$OSIP_CFLAGS
AC_CHECK_HEADER([eXosip2/eXosip.h], ,AC_MSG_ERROR([Could not find eXosip2 headers !]))
CPPFLAGS=$CPPFLAGS_save

dnl check for eXosip2 libs
OSIP_LIBS="-L$osip_prefix/lib -losipparser2${osip_legacy_version}"
LDFLAGS_save=$LDFLAGS
LDFLAGS=$OSIP_LIBS
LIBS_save=$LIBS
AC_CHECK_LIB([eXosip2],[eXosip_subscribe_remove],
	[],
	[AC_MSG_ERROR([Could not find eXosip2 library with version >= 3.0.2 !])],
	[-losipparser2 -losip2 -lpthread])
AC_CHECK_LIB([eXosip2],[eXosip_get_version],
	[AC_DEFINE([HAVE_EXOSIP_GET_VERSION],[1],[Defined when eXosip_get_version is available])],
	[],
	[-losipparser2 -losip2 -lpthread])
LIBS=$LIBS_save
LDFLAGS=$LDFLAGS_save

AC_SUBST(EXOSIP_CFLAGS)
AC_SUBST(EXOSIP_LIBS)
])
