#include "git-compat-util.h"
#include "clar/clar.h"
#include "clar-decls.h"
#include "strbuf.h"

#define cl_failf(fmt, ...) do { \
	char desc[4096]; \
	snprintf(desc, sizeof(desc), fmt, __VA_ARGS__); \
	clar__fail(__FILE__, __func__, __LINE__, "Test failed.", desc, 1); \
} while (0)
