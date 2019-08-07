#ifndef WILDMATCH_H
#define WILDMATCH_H

#define WM_CASEFOLD 1
#define WM_PATHNAME 2

#define WM_NOMATCH 1
#define WM_MATCH 0
#define WM_ABORT_ALL -1
#define WM_ABORT_TO_STARSTAR -2

int wildmatch(const char *pattern, const char *text, unsigned int flags);
#endif
