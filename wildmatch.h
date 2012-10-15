/* wildmatch.h */

#define ABORT_MALFORMED 2
#define NOMATCH 1
#define MATCH 0
#define ABORT_ALL -1
#define ABORT_TO_STARSTAR -2

int wildmatch(const char *pattern, const char *text, int flags);
