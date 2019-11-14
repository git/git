#ifndef COMPILER_H
#define COMPILER_H

#include "git-compat-util.h"
#include "strbuf.h"

#ifdef __GLIBC__
#include <gnu/libc-version.h>

static inline void get_compiler_info(struct strbuf *info)
{
	strbuf_addf(info, "glibc: %s\n", gnu_get_libc_version());
#ifdef __GNUC__
	strbuf_addf(info, "gnuc: %d.%d\n", __GNUC__, __GNUC_MINOR__);
#endif
}

#else

static inline void get_compiler_info(struct strbuf *info)
{
	strbuf_addstr(info, "get_compiler_info() not implemented");
}

#endif

#endif /* COMPILER_H */
