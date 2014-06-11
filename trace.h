#ifndef TRACE_H
#define TRACE_H

#include "git-compat-util.h"
#include "strbuf.h"

__attribute__((format (printf, 1, 2)))
extern void trace_printf(const char *format, ...);
__attribute__((format (printf, 2, 3)))
extern void trace_argv_printf(const char **argv, const char *format, ...);
extern void trace_repo_setup(const char *prefix);
extern int trace_want(const char *key);
__attribute__((format (printf, 2, 3)))
extern void trace_printf_key(const char *key, const char *format, ...);
extern void trace_strbuf(const char *key, const struct strbuf *buf);

#endif /* TRACE_H */
