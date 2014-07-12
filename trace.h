#ifndef TRACE_H
#define TRACE_H

#include "git-compat-util.h"
#include "strbuf.h"

struct trace_key {
	const char * const key;
	int fd;
	unsigned int initialized : 1;
	unsigned int  need_close : 1;
};

#define TRACE_KEY_INIT(name) { "GIT_TRACE_" #name, 0, 0, 0 }

extern void trace_repo_setup(const char *prefix);
extern int trace_want(struct trace_key *key);
extern void trace_disable(struct trace_key *key);

__attribute__((format (printf, 1, 2)))
extern void trace_printf(const char *format, ...);

__attribute__((format (printf, 2, 3)))
extern void trace_printf_key(struct trace_key *key, const char *format, ...);

__attribute__((format (printf, 2, 3)))
extern void trace_argv_printf(const char **argv, const char *format, ...);

extern void trace_strbuf(struct trace_key *key, const struct strbuf *data);

#endif /* TRACE_H */
