m4_define([VERSION_H],m4_esyscmd([cat src/version.h]))
changequote({{,}})dnl
m4_define({{XX_VERSION}},m4_bregexp(VERSION_H,{{^const.*"\(.+\)";}},{{\1}}))
changequote([,])dnl

AC_INIT([m4acut], [XX_VERSION], [honeycomb77@gmail.com])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])

AM_INIT_AUTOMAKE([foreign])

AC_PROG_CC
AC_PROG_CXX

AC_LANG([C++])
AX_CXX_COMPILE_STDCXX_11(noext,mandatory)

AC_CHECK_FUNCS([_vscprintf])
AC_CHECK_FUNC(getopt_long)
AM_CONDITIONAL([AAC_NO_GETOPT_LONG],[test "$ac_cv_func_getopt_long" != "yes"])
AC_SEARCH_LIBS([lsmash_get_tyrant_chapter],[lsmash],,
               [AC_MSG_ERROR(L-SMASH version 1.10.0 or greater required)])

AC_CANONICAL_HOST

X_PLATFORM=posix
case ${host} in
*-*-mingw*)
    X_PLATFORM=win32
    ;;
esac
AM_CONDITIONAL([AAC_PLATFORM_POSIX],[test "$X_PLATFORM" = "posix"])
AM_CONDITIONAL([AAC_PLATFORM_WIN32],[test "$X_PLATFORM" = "win32"])

AC_CONFIG_FILES([Makefile])

AC_OUTPUT
