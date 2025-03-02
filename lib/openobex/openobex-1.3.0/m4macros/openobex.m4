dnl Check for openobex library
dnl Written by Pontus Fuchs 2000-08-18
dnl Version checking fixed by Christian W. Zuckschwerdt 2002-10-17

AC_DEFUN([AM_PATH_OPENOBEX], [
	AC_PATH_PROG(OPENOBEX_CONFIG, openobex-config, no)

	if test "$OPENOBEX_CONFIG" = "no" ; then
		AC_MSG_ERROR(openobex-config not found. Pehaps openobex is not installed.)
	fi

	min_obex_version=ifelse([$1], ,0.9.6,$1)
	AC_MSG_CHECKING(for openobex - version >= $min_obex_version)

	OPENOBEX_CFLAGS=`$OPENOBEX_CONFIG --cflags`
	OPENOBEX_LIBS=`$OPENOBEX_CONFIG --libs`

	obex_config_version=`$OPENOBEX_CONFIG --version`

	obex_config_major_version=`$OPENOBEX_CONFIG --version | \
		sed 's/\([[0-9]]*\).\([[0-9]]*\).\([[0-9]]*\)/\1/'`
	obex_config_minor_version=`$OPENOBEX_CONFIG --version | \
		sed 's/\([[0-9]]*\).\([[0-9]]*\).\([[0-9]]*\)/\2/'`
	obex_config_micro_version=`$OPENOBEX_CONFIG --version | \
		sed 's/\([[0-9]]*\).\([[0-9]]*\).\([[0-9]]*\)/\3/'`

	obex_req_major_version=`echo $min_obex_version | \
		sed 's/\([[0-9]]*\).\([[0-9]]*\).\([[0-9]]*\)/\1/'`
	obex_req_minor_version=`echo $min_obex_version | \
		sed 's/\([[0-9]]*\).\([[0-9]]*\).\([[0-9]]*\)/\2/'`
	obex_req_micro_version=`echo $min_obex_version | \
		sed 's/\([[0-9]]*\).\([[0-9]]*\).\([[0-9]]*\)/\3/'`


	if test $obex_req_major_version -lt $obex_config_major_version ; then
       		obex_config_version_ok="yes"
       	fi
	if test $obex_req_major_version -eq $obex_config_major_version ; then
		if test $obex_req_minor_version -lt $obex_config_minor_version ; then
       			obex_config_version_ok="yes"
	       	fi
		if test $obex_req_minor_version -eq $obex_config_minor_version ; then
			if test $obex_req_micro_version -le $obex_config_micro_version ; then
				obex_config_version_ok="yes"
			fi
		fi
	fi
	
	if test "$obex_config_version_ok" != "yes" ; then
		AC_MSG_ERROR(Installed openobex library too old ($obex_config_version))
	fi

	AC_SUBST(OPENOBEX_CFLAGS)
	AC_SUBST(OPENOBEX_LIBS)
	AC_MSG_RESULT(yes)
])
