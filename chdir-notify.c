#include "cache.h"
#include "abspath.h"
#include "chdir-notify.h"
#include "list.h"
#include "strbuf.h"
#include "trace.h"

struct chdir_notify_entry {
	const char *name;
	chdir_notify_callback cb;
	void *data;
	struct list_head list;
};
static LIST_HEAD(chdir_notify_entries);

void chdir_notify_register(const char *name,
			   chdir_notify_callback cb,
			   void *data)
{
	struct chdir_notify_entry *e = xmalloc(sizeof(*e));
	e->name = name;
	e->cb = cb;
	e->data = data;
	list_add_tail(&e->list, &chdir_notify_entries);
}

static void reparent_cb(const char *name,
			const char *old_cwd,
			const char *new_cwd,
			void *data)
{
	char **path = data;
	char *tmp = *path;

	if (!tmp)
		return;

	*path = reparent_relative_path(old_cwd, new_cwd, tmp);
	free(tmp);

	if (name) {
		trace_printf_key(&trace_setup_key,
				 "setup: reparent %s to '%s'",
				 name, *path);
	}
}

void chdir_notify_reparent(const char *name, char **path)
{
	chdir_notify_register(name, reparent_cb, path);
}

int chdir_notify(const char *new_cwd)
{
	struct strbuf old_cwd = STRBUF_INIT;
	struct list_head *pos;

	if (strbuf_getcwd(&old_cwd) < 0)
		return -1;
	if (chdir(new_cwd) < 0) {
		int saved_errno = errno;
		strbuf_release(&old_cwd);
		errno = saved_errno;
		return -1;
	}

	trace_printf_key(&trace_setup_key,
			 "setup: chdir from '%s' to '%s'",
			 old_cwd.buf, new_cwd);

	list_for_each(pos, &chdir_notify_entries) {
		struct chdir_notify_entry *e =
			list_entry(pos, struct chdir_notify_entry, list);
		e->cb(e->name, old_cwd.buf, new_cwd, e->data);
	}

	strbuf_release(&old_cwd);
	return 0;
}

char *reparent_relative_path(const char *old_cwd,
			     const char *new_cwd,
			     const char *path)
{
	char *ret, *full;

	if (is_absolute_path(path))
		return xstrdup(path);

	full = xstrfmt("%s/%s", old_cwd, path);
	ret = xstrdup(remove_leading_path(full, new_cwd));
	free(full);

	return ret;
}
