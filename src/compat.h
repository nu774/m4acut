/* 
 * Copyright (C) 2014 nu774
 * For conditions of distribution and use, see copyright notice in COPYING
 */
#ifndef COMPAT_H
#define COMPAT_H

#ifdef __cplusplus
extern "C" {
#endif

int64_t aa_timer(void);

#ifndef _WIN32
# define aa_getmainargs(argc, argv) (void)(0)
# define aa_fprintf fprintf
#else
  void aa_getmainargs(int *argc, char ***argv);
  int aa_fprintf(FILE *fp, const char *fmt, ...);
#endif

#ifdef __cplusplus
}
#endif

#endif
