#ifndef COMPILER_H
#define COMPILER_H

#include "strbuf.h"

#ifdef __GLIBC__
#include <gnu/libc-version.h>
#endif

static inline void get_compiler_info(struct strbuf *info)
{
	size_t len = info->len;
#ifdef __clang__
	strbuf_addf(info, "clang: %s\n", __clang_version__);
#elif defined(__GNUC__)
	strbuf_addf(info, "gnuc: %d.%d\n", __GNUC__, __GNUC_MINOR__);
#endif

#ifdef _MSC_VER
	strbuf_addf(info, "MSVC version: %02d.%02d.%05d\n",
		    _MSC_VER / 100, _MSC_VER % 100, _MSC_FULL_VER % 100000);
#endif

	if (len == info->len)
		strbuf_addstr(info, _("no compiler information available\n"));
}

static inline void get_libc_info(struct strbuf *info)
{
	size_t len = info->len;

#ifdef __GLIBC__
	strbuf_addf(info, "glibc: %s\n", gnu_get_libc_version());
#endif

	if (len == info->len)
		strbuf_addstr(info, _("no libc information available\n"));
}

#endif /* COMPILER_H */
