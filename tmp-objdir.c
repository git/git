#include "git-compat-util.h"
#include "tmp-objdir.h"
#include "abspath.h"
#include "chdir-notify.h"
#include "dir.h"
#include "environment.h"
#include "object-file.h"
#include "path.h"
#include "string-list.h"
#include "strbuf.h"
#include "strvec.h"
#include "quote.h"
#include "odb.h"
#include "repository.h"

struct tmp_objdir {
	struct repository *repo;
	struct strbuf path;
	struct strvec env;
	struct odb_source *prev_source;
	int will_destroy;
};

/*
 * Allow only one tmp_objdir at a time in a running process, which simplifies
 * our atexit cleanup routines.  It's doubtful callers will ever need
 * more than one, and we can expand later if so.  You can have many such
 * tmp_objdirs simultaneously in many processes, of course.
 */
static struct tmp_objdir *the_tmp_objdir;

static void tmp_objdir_free(struct tmp_objdir *t)
{
	strbuf_release(&t->path);
	strvec_clear(&t->env);
	free(t);
}

int tmp_objdir_destroy(struct tmp_objdir *t)
{
	int err;

	if (!t)
		return 0;

	if (t == the_tmp_objdir)
		the_tmp_objdir = NULL;

	if (t->prev_source)
		odb_restore_primary_source(t->repo->objects, t->prev_source, t->path.buf);

	err = remove_dir_recursively(&t->path, 0);

	tmp_objdir_free(t);

	return err;
}

static void remove_tmp_objdir(void)
{
	tmp_objdir_destroy(the_tmp_objdir);
}

void tmp_objdir_discard_objects(struct tmp_objdir *t)
{
	remove_dir_recursively(&t->path, REMOVE_DIR_KEEP_TOPLEVEL);
}

/*
 * These env_* functions are for setting up the child environment; the
 * "replace" variant overrides the value of any existing variable with that
 * "key". The "append" variant puts our new value at the end of a list,
 * separated by PATH_SEP (which is what separate values in
 * GIT_ALTERNATE_OBJECT_DIRECTORIES).
 */
static void env_append(struct strvec *env, const char *key, const char *val)
{
	struct strbuf quoted = STRBUF_INIT;
	const char *old;

	/*
	 * Avoid quoting if it's not necessary, for maximum compatibility
	 * with older parsers which don't understand the quoting.
	 */
	if (*val == '"' || strchr(val, PATH_SEP)) {
		strbuf_addch(&quoted, '"');
		quote_c_style(val, &quoted, NULL, 1);
		strbuf_addch(&quoted, '"');
		val = quoted.buf;
	}

	old = getenv(key);
	if (!old)
		strvec_pushf(env, "%s=%s", key, val);
	else
		strvec_pushf(env, "%s=%s%c%s", key, old, PATH_SEP, val);

	strbuf_release(&quoted);
}

static void env_replace(struct strvec *env, const char *key, const char *val)
{
	strvec_pushf(env, "%s=%s", key, val);
}

static int setup_tmp_objdir(const char *root)
{
	char *path;
	int ret = 0;

	path = xstrfmt("%s/pack", root);
	ret = mkdir(path, 0777);
	free(path);

	return ret;
}

struct tmp_objdir *tmp_objdir_create(struct repository *r,
				     const char *prefix)
{
	static int installed_handlers;
	struct tmp_objdir *t;

	if (the_tmp_objdir)
		BUG("only one tmp_objdir can be used at a time");

	t = xcalloc(1, sizeof(*t));
	t->repo = r;
	strbuf_init(&t->path, 0);
	strvec_init(&t->env);

	/*
	 * Use a string starting with tmp_ so that the builtin/prune.c code
	 * can recognize any stale objdirs left behind by a crash and delete
	 * them.
	 */
	strbuf_addf(&t->path, "%s/tmp_objdir-%s-XXXXXX",
		    repo_get_object_directory(r), prefix);

	if (!mkdtemp(t->path.buf)) {
		/* free, not destroy, as we never touched the filesystem */
		tmp_objdir_free(t);
		return NULL;
	}

	the_tmp_objdir = t;
	if (!installed_handlers) {
		atexit(remove_tmp_objdir);
		installed_handlers++;
	}

	if (setup_tmp_objdir(t->path.buf)) {
		tmp_objdir_destroy(t);
		return NULL;
	}

	env_append(&t->env, ALTERNATE_DB_ENVIRONMENT,
		   absolute_path(repo_get_object_directory(r)));
	env_replace(&t->env, DB_ENVIRONMENT, absolute_path(t->path.buf));
	env_replace(&t->env, GIT_QUARANTINE_ENVIRONMENT,
		    absolute_path(t->path.buf));

	return t;
}

/*
 * Make sure we copy packfiles and their associated metafiles in the correct
 * order. All of these ends_with checks are slightly expensive to do in
 * the midst of a sorting routine, but in practice it shouldn't matter.
 * We will have a relatively small number of packfiles to order, and loose
 * objects exit early in the first line.
 */
static int pack_copy_priority(const char *name)
{
	if (!starts_with(name, "pack"))
		return 0;
	if (ends_with(name, ".keep"))
		return 1;
	if (ends_with(name, ".pack"))
		return 2;
	if (ends_with(name, ".rev"))
		return 3;
	if (ends_with(name, ".idx"))
		return 4;
	return 5;
}

static int pack_copy_cmp(const char *a, const char *b)
{
	return pack_copy_priority(a) - pack_copy_priority(b);
}

static int read_dir_paths(struct string_list *out, const char *path)
{
	DIR *dh;
	struct dirent *de;

	dh = opendir(path);
	if (!dh)
		return -1;

	while ((de = readdir(dh)))
		if (de->d_name[0] != '.')
			string_list_append(out, de->d_name);

	closedir(dh);
	return 0;
}

static int migrate_paths(struct tmp_objdir *t,
			 struct strbuf *src, struct strbuf *dst,
			 enum finalize_object_file_flags flags);

static int migrate_one(struct tmp_objdir *t,
		       struct strbuf *src, struct strbuf *dst,
		       enum finalize_object_file_flags flags)
{
	struct stat st;

	if (stat(src->buf, &st) < 0)
		return -1;
	if (S_ISDIR(st.st_mode)) {
		if (!mkdir(dst->buf, 0777)) {
			if (adjust_shared_perm(t->repo, dst->buf))
				return -1;
		} else if (errno != EEXIST)
			return -1;
		return migrate_paths(t, src, dst, flags);
	}
	return finalize_object_file_flags(src->buf, dst->buf, flags);
}

static int is_loose_object_shard(const char *name)
{
	return strlen(name) == 2 && isxdigit(name[0]) && isxdigit(name[1]);
}

static int migrate_paths(struct tmp_objdir *t,
			 struct strbuf *src, struct strbuf *dst,
			 enum finalize_object_file_flags flags)
{
	size_t src_len = src->len, dst_len = dst->len;
	struct string_list paths = STRING_LIST_INIT_DUP;
	int ret = 0;

	if (read_dir_paths(&paths, src->buf) < 0)
		return -1;
	paths.cmp = pack_copy_cmp;
	string_list_sort(&paths);

	for (size_t i = 0; i < paths.nr; i++) {
		const char *name = paths.items[i].string;
		enum finalize_object_file_flags flags_copy = flags;

		strbuf_addf(src, "/%s", name);
		strbuf_addf(dst, "/%s", name);

		if (is_loose_object_shard(name))
			flags_copy |= FOF_SKIP_COLLISION_CHECK;

		ret |= migrate_one(t, src, dst, flags_copy);

		strbuf_setlen(src, src_len);
		strbuf_setlen(dst, dst_len);
	}

	string_list_clear(&paths, 0);
	return ret;
}

int tmp_objdir_migrate(struct tmp_objdir *t)
{
	struct strbuf src = STRBUF_INIT, dst = STRBUF_INIT;
	int ret;

	if (!t)
		return 0;

	if (t->prev_source) {
		if (t->repo->objects->sources->will_destroy)
			BUG("migrating an ODB that was marked for destruction");
		odb_restore_primary_source(t->repo->objects, t->prev_source, t->path.buf);
		t->prev_source = NULL;
	}

	strbuf_addbuf(&src, &t->path);
	strbuf_addstr(&dst, repo_get_object_directory(t->repo));

	ret = migrate_paths(t, &src, &dst, 0);

	strbuf_release(&src);
	strbuf_release(&dst);

	tmp_objdir_destroy(t);
	return ret;
}

const char **tmp_objdir_env(const struct tmp_objdir *t)
{
	if (!t)
		return NULL;
	return t->env.v;
}

void tmp_objdir_add_as_alternate(const struct tmp_objdir *t)
{
	odb_add_to_alternates_memory(t->repo->objects, t->path.buf);
}

void tmp_objdir_replace_primary_odb(struct tmp_objdir *t, int will_destroy)
{
	if (t->prev_source)
		BUG("the primary object database is already replaced");
	t->prev_source = odb_set_temporary_primary_source(t->repo->objects,
							  t->path.buf, will_destroy);
	t->will_destroy = will_destroy;
}

struct tmp_objdir *tmp_objdir_unapply_primary_odb(void)
{
	if (!the_tmp_objdir || !the_tmp_objdir->prev_source)
		return NULL;

	odb_restore_primary_source(the_tmp_objdir->repo->objects,
				   the_tmp_objdir->prev_source, the_tmp_objdir->path.buf);
	the_tmp_objdir->prev_source = NULL;
	return the_tmp_objdir;
}

void tmp_objdir_reapply_primary_odb(struct tmp_objdir *t, const char *old_cwd,
		const char *new_cwd)
{
	char *path;

	path = reparent_relative_path(old_cwd, new_cwd, t->path.buf);
	strbuf_reset(&t->path);
	strbuf_addstr(&t->path, path);
	free(path);
	tmp_objdir_replace_primary_odb(t, t->will_destroy);
}
