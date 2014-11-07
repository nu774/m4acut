AM_CXXFLAGS = -Wall -std=c++0x
AM_CFLAGS   = -Wall

ACLOCAL_AMFLAGS = -I m4
AUTOMAKE_OPTIONS = subdir-objects

bin_PROGRAMS = m4acut

dist_man_MANS = man/m4acut.1

m4acut_SOURCES = src/M4ATrimmer.cpp \
		 src/MP4Edits.cpp \
		 src/StringConverterUTF8.cpp \
		 src/cuesheet.cpp \
		 src/main.cpp

if AAC_HAVE_ICONV
    m4acut_SOURCES += src/StringConverterIConv.cpp
    m4acut_LDADD = @LIBICONV@
endif


if AAC_PLATFORM_POSIX
    m4acut_SOURCES += src/compat_posix.c
endif

if AAC_PLATFORM_WIN32
    m4acut_SOURCES += src/StringConverterWin32.cpp
    m4acut_SOURCES += src/compat_win32.c
endif

if AAC_NO_GETOPT_LONG
    m4acut_SOURCES += missings/getopt.c
    AM_CPPFLAGS = -Imissings
endif


EXTRA_DIST = \
    man/m4acut.1                    \
    m4/ax_cxx_compile_stdcxx_11.m4  \
    src/*.h                         \
    missings/*.c                    \
    missings/*.h                    \
    MSVC/*.vcxproj                  \
    MSVC/*.vcxproj.filters          \
    MSVC/*.sln
