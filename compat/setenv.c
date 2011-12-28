#include "../git-compat-util.h"

int gitsetenv(const char *name, const char *value, int replace)
{
	int out;
	size_t namelen, valuelen;
	char *envstr;

	if (!name || strchr(name, '=') || !value) {
		errno = EINVAL;
		return -1;
	}
	if (!replace) {
		char *oldval = NULL;
		oldval = getenv(name);
		if (oldval) return 0;
	}

	namelen = strlen(name);
	valuelen = strlen(value);
	envstr = malloc((namelen + valuelen + 2));
	if (!envstr) {
		errno = ENOMEM;
		return -1;
	}

	memcpy(envstr, name, namelen);
	envstr[namelen] = '=';
	memcpy(envstr + namelen + 1, value, valuelen);
	envstr[namelen + valuelen + 1] = 0;

	out = putenv(envstr);
	/* putenv(3) makes the argument string part of the environment,
	 * and changing that string modifies the environment --- which
	 * means we do not own that storage anymore.  Do not free
	 * envstr.
	 */

	return out;
}
