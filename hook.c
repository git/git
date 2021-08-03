#include "cache.h"
#include "hook.h"
#include "run-command.h"
#include "hook-list.h"

static int known_hook(const char *name)
{
	const char **p;
	size_t len = strlen(name);
	for (p = hook_name_list; *p; p++) {
		const char *hook = *p;

		if (!strncmp(name, hook, len) && hook[len] == '\0')
			return 1;
	}

	return 0;
}

const char *find_hook(const char *name)
{
	static struct strbuf path = STRBUF_INIT;

	if (!known_hook(name))
		die(_("the hook '%s' is not known to git, should be in hook-list.h via githooks(5)"),
		    name);

	strbuf_reset(&path);
	strbuf_git_path(&path, "hooks/%s", name);
	if (access(path.buf, X_OK) < 0) {
		int err = errno;

#ifdef STRIP_EXTENSION
		strbuf_addstr(&path, STRIP_EXTENSION);
		if (access(path.buf, X_OK) >= 0)
			return path.buf;
		if (errno == EACCES)
			err = errno;
#endif

		if (err == EACCES && advice_ignored_hook) {
			static struct string_list advise_given = STRING_LIST_INIT_DUP;

			if (!string_list_lookup(&advise_given, name)) {
				string_list_insert(&advise_given, name);
				advise(_("The '%s' hook was ignored because "
					 "it's not set as executable.\n"
					 "You can disable this warning with "
					 "`git config advice.ignoredHook false`."),
				       path.buf);
			}
		}
		return NULL;
	}
	return path.buf;
}

int hook_exists(const char *name)
{
	return !!find_hook(name);
}
