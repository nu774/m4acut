/* 
 * Copyright (C) 2014 nu774
 * For conditions of distribution and use, see copyright notice in COPYING
 */
#if HAVE_CONFIG_H
#  include "config.h"
#endif
#if HAVE_STDINT_H
#  include <stdint.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>
#include <io.h>
#include <fcntl.h>
#include <share.h>
#include "compat.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef struct
{
    int newmode;
} _startupinfo;

extern
int __wgetmainargs(int *, wchar_t ***, wchar_t ***, int, _startupinfo *);

static
int codepage_decode_wchar(int codepage, const char *from, wchar_t **to)
{
    int nc = MultiByteToWideChar(codepage, 0, from, -1, 0, 0);
    if (nc == 0)
        return -1;
    *to = malloc(nc * sizeof(wchar_t));
    MultiByteToWideChar(codepage, 0, from, -1, *to, nc);
    return 0;
}

static
int codepage_encode_wchar(int codepage, const wchar_t *from, char **to)
{
    int nc = WideCharToMultiByte(codepage, 0, from, -1, 0, 0, 0, 0);
    if (nc == 0)
        return -1;
    *to = malloc(nc);
    WideCharToMultiByte(codepage, 0, from, -1, *to, nc, 0, 0);
    return 0;
}

static char **__utf8_argv__;

static
void free_mainargs(void)
{
    char **p = __utf8_argv__;
    for (; *p; ++p)
        free(*p);
    free(__utf8_argv__);
}

void aa_getmainargs(int *argc, char ***argv)
{
    int i;
    wchar_t **wargv, **envp;
    _startupinfo si = { 0 };

    __wgetmainargs(argc, &wargv, &envp, 1, &si);
    *argv = malloc((*argc + 1) * sizeof(char*));
    for (i = 0; i < *argc; ++i)
        codepage_encode_wchar(CP_UTF8, wargv[i], &(*argv)[i]);
    (*argv)[*argc] = 0;
    __utf8_argv__ = *argv;
    atexit(free_mainargs);
}

#if defined(__MINGW32__) && !defined(HAVE__VSCPRINTF)
int _vscprintf(const char *fmt, va_list ap) 
{
    static int (*fp_vscprintf)(const char *, va_list) = 0;
    if (!fp_vscprintf) {
        HANDLE h = GetModuleHandleA("msvcrt.dll");
        FARPROC fp = GetProcAddress(h, "_vscprintf");
        InterlockedCompareExchangePointer(&fp_vscprintf, fp, 0);
    }
    assert(fp_vscprintf);
    return fp_vscprintf(fmt, ap);
}
#endif

int aa_fprintf(FILE *fp, const char *fmt, ...)
{
    va_list ap;
    int cnt;
    HANDLE fh = (HANDLE)_get_osfhandle(_fileno(fp));

    if (GetFileType(fh) != FILE_TYPE_CHAR) {
        va_start(ap, fmt);
        cnt = vfprintf(fp, fmt, ap);
        va_end(ap);
    } else {
        char *s;
        wchar_t *ws;
        DWORD nw;

        va_start(ap, fmt);
        cnt = _vscprintf(fmt, ap);
        va_end(ap);

        s = malloc(cnt + 1);
        
        va_start(ap, fmt);
        cnt = _vsnprintf(s, cnt + 1, fmt, ap);
        va_end(ap);

        codepage_decode_wchar(CP_UTF8, s, &ws);
        free(s);
        fflush(fp);
        WriteConsoleW(fh, ws, cnt, &nw, 0);
        free(ws);
    }
    return cnt;
}
