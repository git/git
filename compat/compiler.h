#ifndef COMPILER_H
#define COMPILER_H

#include "git-compat-util.h"
#include "strbuf.h"

#ifdef __GLIBC__
#include <gnu/libc-version.h>
#endif

static inline void get_compiler_info(struct strbuf *info)
{
#ifdef __GLIBC__
	strbuf_addf(info, "glibc: %s\n", gnu_get_libc_version());
#endif

#ifdef __GNUC__
	strbuf_addf(info, "gnuc: %d.%d\n", __GNUC__, __GNUC_MINOR__);
#endif
}

#endif /* COMPILER_H */
