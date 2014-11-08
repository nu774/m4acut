m4_define([VERSION_H],m4_esyscmd([cat src/version.h]))
changequote({{,}})dnl
m4_define({{XX_VERSION}},m4_bregexp(VERSION_H,{{^const.*"\(.+\)";}},{{\1}}))
changequote([,])dnl

AC_INIT([m4acut], [XX_VERSION], [honeycomb77@gmail.com])
AC_CONFIG_SRCDIR([src/main.cpp])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])

m4_esyscmd([mkdir -p build-aux && cp -f config.rpath build-aux])
AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([foreign])

# Checks for programs.
AC_PROG_CXX
AC_PROG_CC
LT_INIT

# Checks for libraries and header files.
AC_CHECK_HEADERS([fcntl.h stdint.h stdlib.h string.h sys/time.h sys/timeb.h])
AC_LANG([C++])
AX_CXX_COMPILE_STDCXX_11(noext,optional)
AS_IF([test -z $HAVE_CXX11],[CXXFLAGS="$CXXFLAGS -std=c++0x"])
AM_CONDITIONAL([AAC_NO_GETOPT_LONG],[test "$ac_cv_func_getopt_long" != "yes"])
AC_SEARCH_LIBS([lsmash_get_tyrant_chapter],[lsmash],,
               [AC_MSG_ERROR(L-SMASH version 1.10.0 or greater required)])
AM_ICONV
AM_CONDITIONAL([AAC_HAVE_ICONV],[test "$am_cv_func_iconv" = "yes"])
AC_CANONICAL_HOST

# Checks for typedefs, structures, and compiler characteristics.
AC_CHECK_HEADER_STDBOOL
AC_C_INLINE
AC_TYPE_INT32_T
AC_TYPE_INT64_T
AC_TYPE_SIZE_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_UINT8_T
AC_CHECK_TYPES([ptrdiff_t])
AC_CHECK_TYPES([struct __timeb64],[],[],[[#include <sys/timeb.h>]])

X_PLATFORM=posix
case ${host} in
*-*-mingw*)
    X_PLATFORM=win32
    ;;
esac
AM_CONDITIONAL([AAC_PLATFORM_POSIX],[test "$X_PLATFORM" = "posix"])
AM_CONDITIONAL([AAC_PLATFORM_WIN32],[test "$X_PLATFORM" = "win32"])

# Checks for library functions.
AC_FUNC_MALLOC
AC_CHECK_FUNCS([_vscprintf getopt_long atexit ftime gettimeofday memset setlocale strchr strerror])

AC_CONFIG_FILES([Makefile])

AC_OUTPUT
