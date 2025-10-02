#include "git-compat-util.h"
#include "clar/clar.h"
#include "strbuf.h"

#ifndef GIT_CLAR_DECLS_H
# include "clar-decls.h"
#else
# include GIT_CLAR_DECLS_H
#endif

#define cl_failf(fmt, ...) do { \
	char desc[4096]; \
	snprintf(desc, sizeof(desc), fmt, __VA_ARGS__); \
	clar__fail(__FILE__, __func__, __LINE__, "Test failed.", desc, 1); \
} while (0)
