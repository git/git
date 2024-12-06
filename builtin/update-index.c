/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 */

#define USE_THE_REPOSITORY_VARIABLE
#define DISABLE_SIGN_COMPARE_WARNINGS

#include "builtin.h"
#include "bulk-checkin.h"
#include "config.h"
#include "environment.h"
#include "gettext.h"
#include "hash.h"
#include "hex.h"
#include "lockfile.h"
#include "quote.h"
#include "cache-tree.h"
#include "tree-walk.h"
#include "object-file.h"
#include "refs.h"
#include "resolve-undo.h"
#include "parse-options.h"
#include "pathspec.h"
#include "dir.h"
#include "read-cache.h"
#include "setup.h"
#include "sparse-index.h"
#include "split-index.h"
#include "symlinks.h"
#include "fsmonitor.h"
#include "write-or-die.h"

/*
 * Default to not allowing changes to the list of files. The
 * tool doesn't actually care, but this makes it harder to add
 * files to the revision control by mistake by doing something
 * like "git update-index *" and suddenly having all the object
 * files be revision controlled.
 */
static int allow_add;
static int allow_remove;
static int allow_replace;
static int info_only;
static int force_remove;
static int verbose;
static int mark_valid_only;
static int mark_skip_worktree_only;
static int mark_fsmonitor_only;
static int ignore_skip_worktree_entries;
#define MARK_FLAG 1
#define UNMARK_FLAG 2
static struct strbuf mtime_dir = STRBUF_INIT;

/* Untracked cache mode */
enum uc_mode {
	UC_UNSPECIFIED = -1,
	UC_DISABLE = 0,
	UC_ENABLE,
	UC_TEST,
	UC_FORCE
};

__attribute__((format (printf, 1, 2)))
static void report(const char *fmt, ...)
{
	va_list vp;

	if (!verbose)
		return;

	/*
	 * It is possible, though unlikely, that a caller could use the verbose
	 * output to synchronize with addition of objects to the object
	 * database. The current implementation of ODB transactions leaves
	 * objects invisible while a transaction is active, so flush the
	 * transaction here before reporting a change made by update-index.
	 */
	flush_odb_transaction();
	va_start(vp, fmt);
	vprintf(fmt, vp);
	putchar('\n');
	va_end(vp);
}

static void remove_test_directory(void)
{
	if (mtime_dir.len)
		remove_dir_recursively(&mtime_dir, 0);
}

static const char *get_mtime_path(const char *path)
{
	static struct strbuf sb = STRBUF_INIT;
	strbuf_reset(&sb);
	strbuf_addf(&sb, "%s/%s", mtime_dir.buf, path);
	return sb.buf;
}

static void xmkdir(const char *path)
{
	path = get_mtime_path(path);
	if (mkdir(path, 0700))
		die_errno(_("failed to create directory %s"), path);
}

static int xstat_mtime_dir(struct stat *st)
{
	if (stat(mtime_dir.buf, st))
		die_errno(_("failed to stat %s"), mtime_dir.buf);
	return 0;
}

static int create_file(const char *path)
{
	int fd;
	path = get_mtime_path(path);
	fd = xopen(path, O_CREAT | O_RDWR, 0644);
	return fd;
}

static void xunlink(const char *path)
{
	path = get_mtime_path(path);
	if (unlink(path))
		die_errno(_("failed to delete file %s"), path);
}

static void xrmdir(const char *path)
{
	path = get_mtime_path(path);
	if (rmdir(path))
		die_errno(_("failed to delete directory %s"), path);
}

static void avoid_racy(void)
{
	/*
	 * not use if we could usleep(10) if USE_NSEC is defined. The
	 * field nsec could be there, but the OS could choose to
	 * ignore it?
	 */
	sleep(1);
}

static int test_if_untracked_cache_is_supported(void)
{
	struct stat st;
	struct stat_data base;
	int fd, ret = 0;
	char *cwd;

	strbuf_addstr(&mtime_dir, "mtime-test-XXXXXX");
	if (!mkdtemp(mtime_dir.buf))
		die_errno("Could not make temporary directory");

	cwd = xgetcwd();
	fprintf(stderr, _("Testing mtime in '%s' "), cwd);
	free(cwd);

	atexit(remove_test_directory);
	xstat_mtime_dir(&st);
	fill_stat_data(&base, &st);
	fputc('.', stderr);

	avoid_racy();
	fd = create_file("newfile");
	xstat_mtime_dir(&st);
	if (!match_stat_data(&base, &st)) {
		close(fd);
		fputc('\n', stderr);
		fprintf_ln(stderr,_("directory stat info does not "
				    "change after adding a new file"));
		goto done;
	}
	fill_stat_data(&base, &st);
	fputc('.', stderr);

	avoid_racy();
	xmkdir("new-dir");
	xstat_mtime_dir(&st);
	if (!match_stat_data(&base, &st)) {
		close(fd);
		fputc('\n', stderr);
		fprintf_ln(stderr, _("directory stat info does not change "
				     "after adding a new directory"));
		goto done;
	}
	fill_stat_data(&base, &st);
	fputc('.', stderr);

	avoid_racy();
	write_or_die(fd, "data", 4);
	close(fd);
	xstat_mtime_dir(&st);
	if (match_stat_data(&base, &st)) {
		fputc('\n', stderr);
		fprintf_ln(stderr, _("directory stat info changes "
				     "after updating a file"));
		goto done;
	}
	fputc('.', stderr);

	avoid_racy();
	close(create_file("new-dir/new"));
	xstat_mtime_dir(&st);
	if (match_stat_data(&base, &st)) {
		fputc('\n', stderr);
		fprintf_ln(stderr, _("directory stat info changes after "
				     "adding a file inside subdirectory"));
		goto done;
	}
	fputc('.', stderr);

	avoid_racy();
	xunlink("newfile");
	xstat_mtime_dir(&st);
	if (!match_stat_data(&base, &st)) {
		fputc('\n', stderr);
		fprintf_ln(stderr, _("directory stat info does not "
				     "change after deleting a file"));
		goto done;
	}
	fill_stat_data(&base, &st);
	fputc('.', stderr);

	avoid_racy();
	xunlink("new-dir/new");
	xrmdir("new-dir");
	xstat_mtime_dir(&st);
	if (!match_stat_data(&base, &st)) {
		fputc('\n', stderr);
		fprintf_ln(stderr, _("directory stat info does not "
				     "change after deleting a directory"));
		goto done;
	}

	if (rmdir(mtime_dir.buf))
		die_errno(_("failed to delete directory %s"), mtime_dir.buf);
	fprintf_ln(stderr, _(" OK"));
	ret = 1;

done:
	strbuf_release(&mtime_dir);
	return ret;
}

static int mark_ce_flags(const char *path, int flag, int mark)
{
	int namelen = strlen(path);
	int pos = index_name_pos(the_repository->index, path, namelen);
	if (0 <= pos) {
		mark_fsmonitor_invalid(the_repository->index, the_repository->index->cache[pos]);
		if (mark)
			the_repository->index->cache[pos]->ce_flags |= flag;
		else
			the_repository->index->cache[pos]->ce_flags &= ~flag;
		the_repository->index->cache[pos]->ce_flags |= CE_UPDATE_IN_BASE;
		cache_tree_invalidate_path(the_repository->index, path);
		the_repository->index->cache_changed |= CE_ENTRY_CHANGED;
		return 0;
	}
	return -1;
}

static int remove_one_path(const char *path)
{
	if (!allow_remove)
		return error("%s: does not exist and --remove not passed", path);
	if (remove_file_from_index(the_repository->index, path))
		return error("%s: cannot remove from the index", path);
	return 0;
}

/*
 * Handle a path that couldn't be lstat'ed. It's either:
 *  - missing file (ENOENT or ENOTDIR). That's ok if we're
 *    supposed to be removing it and the removal actually
 *    succeeds.
 *  - permission error. That's never ok.
 */
static int process_lstat_error(const char *path, int err)
{
	if (is_missing_file_error(err))
		return remove_one_path(path);
	return error("lstat(\"%s\"): %s", path, strerror(err));
}

static int add_one_path(const struct cache_entry *old, const char *path, int len, struct stat *st)
{
	int option;
	struct cache_entry *ce;

	/* Was the old index entry already up-to-date? */
	if (old && !ce_stage(old) && !ie_match_stat(the_repository->index, old, st, 0))
		return 0;

	ce = make_empty_cache_entry(the_repository->index, len);
	memcpy(ce->name, path, len);
	ce->ce_flags = create_ce_flags(0);
	ce->ce_namelen = len;
	fill_stat_cache_info(the_repository->index, ce, st);
	ce->ce_mode = ce_mode_from_stat(old, st->st_mode);

	if (index_path(the_repository->index, &ce->oid, path, st,
		       info_only ? 0 : HASH_WRITE_OBJECT)) {
		discard_cache_entry(ce);
		return -1;
	}
	option = allow_add ? ADD_CACHE_OK_TO_ADD : 0;
	option |= allow_replace ? ADD_CACHE_OK_TO_REPLACE : 0;
	if (add_index_entry(the_repository->index, ce, option)) {
		discard_cache_entry(ce);
		return error("%s: cannot add to the index - missing --add option?", path);
	}
	return 0;
}

/*
 * Handle a path that was a directory. Four cases:
 *
 *  - it's already a gitlink in the index, and we keep it that
 *    way, and update it if we can (if we cannot find the HEAD,
 *    we're going to keep it unchanged in the index!)
 *
 *  - it's a *file* in the index, in which case it should be
 *    removed as a file if removal is allowed, since it doesn't
 *    exist as such any more. If removal isn't allowed, it's
 *    an error.
 *
 *    (NOTE! This is old and arguably fairly strange behaviour.
 *    We might want to make this an error unconditionally, and
 *    use "--force-remove" if you actually want to force removal).
 *
 *  - it used to exist as a subdirectory (ie multiple files with
 *    this particular prefix) in the index, in which case it's wrong
 *    to try to update it as a directory.
 *
 *  - it doesn't exist at all in the index, but it is a valid
 *    git directory, and it should be *added* as a gitlink.
 */
static int process_directory(const char *path, int len, struct stat *st)
{
	struct object_id oid;
	int pos = index_name_pos(the_repository->index, path, len);

	/* Exact match: file or existing gitlink */
	if (pos >= 0) {
		const struct cache_entry *ce = the_repository->index->cache[pos];
		if (S_ISGITLINK(ce->ce_mode)) {

			/* Do nothing to the index if there is no HEAD! */
			if (repo_resolve_gitlink_ref(the_repository, path,
						     "HEAD", &oid) < 0)
				return 0;

			return add_one_path(ce, path, len, st);
		}
		/* Should this be an unconditional error? */
		return remove_one_path(path);
	}

	/* Inexact match: is there perhaps a subdirectory match? */
	pos = -pos-1;
	while (pos < the_repository->index->cache_nr) {
		const struct cache_entry *ce = the_repository->index->cache[pos++];

		if (strncmp(ce->name, path, len))
			break;
		if (ce->name[len] > '/')
			break;
		if (ce->name[len] < '/')
			continue;

		/* Subdirectory match - error out */
		return error("%s: is a directory - add individual files instead", path);
	}

	/* No match - should we add it as a gitlink? */
	if (!repo_resolve_gitlink_ref(the_repository, path, "HEAD", &oid))
		return add_one_path(NULL, path, len, st);

	/* Error out. */
	return error("%s: is a directory - add files inside instead", path);
}

static int process_path(const char *path, struct stat *st, int stat_errno)
{
	int pos, len;
	const struct cache_entry *ce;

	len = strlen(path);
	if (has_symlink_leading_path(path, len))
		return error("'%s' is beyond a symbolic link", path);

	pos = index_name_pos(the_repository->index, path, len);
	ce = pos < 0 ? NULL : the_repository->index->cache[pos];
	if (ce && ce_skip_worktree(ce)) {
		/*
		 * working directory version is assumed "good"
		 * so updating it does not make sense.
		 * On the other hand, removing it from index should work
		 */
		if (!ignore_skip_worktree_entries && allow_remove &&
		    remove_file_from_index(the_repository->index, path))
			return error("%s: cannot remove from the index", path);
		return 0;
	}

	/*
	 * First things first: get the stat information, to decide
	 * what to do about the pathname!
	 */
	if (stat_errno)
		return process_lstat_error(path, stat_errno);

	if (S_ISDIR(st->st_mode))
		return process_directory(path, len, st);

	return add_one_path(ce, path, len, st);
}

static int add_cacheinfo(unsigned int mode, const struct object_id *oid,
			 const char *path, int stage)
{
	int len, option;
	struct cache_entry *ce;

	if (!verify_path(path, mode))
		return error("Invalid path '%s'", path);

	len = strlen(path);
	ce = make_empty_cache_entry(the_repository->index, len);

	oidcpy(&ce->oid, oid);
	memcpy(ce->name, path, len);
	ce->ce_flags = create_ce_flags(stage);
	ce->ce_namelen = len;
	ce->ce_mode = create_ce_mode(mode);
	if (assume_unchanged)
		ce->ce_flags |= CE_VALID;
	option = allow_add ? ADD_CACHE_OK_TO_ADD : 0;
	option |= allow_replace ? ADD_CACHE_OK_TO_REPLACE : 0;
	if (add_index_entry(the_repository->index, ce, option))
		return error("%s: cannot add to the index - missing --add option?",
			     path);
	report("add '%s'", path);
	return 0;
}

static void chmod_path(char flip, const char *path)
{
	int pos;
	struct cache_entry *ce;

	pos = index_name_pos(the_repository->index, path, strlen(path));
	if (pos < 0)
		goto fail;
	ce = the_repository->index->cache[pos];
	if (chmod_index_entry(the_repository->index, ce, flip) < 0)
		goto fail;

	report("chmod %cx '%s'", flip, path);
	return;
 fail:
	die("git update-index: cannot chmod %cx '%s'", flip, path);
}

static void update_one(const char *path)
{
	int stat_errno = 0;
	struct stat st;

	if (mark_valid_only || mark_skip_worktree_only || force_remove ||
	    mark_fsmonitor_only)
		st.st_mode = 0;
	else if (lstat(path, &st) < 0) {
		st.st_mode = 0;
		stat_errno = errno;
	} /* else stat is valid */

	if (!verify_path(path, st.st_mode)) {
		fprintf(stderr, "Ignoring path %s\n", path);
		return;
	}
	if (mark_valid_only) {
		if (mark_ce_flags(path, CE_VALID, mark_valid_only == MARK_FLAG))
			die("Unable to mark file %s", path);
		return;
	}
	if (mark_skip_worktree_only) {
		if (mark_ce_flags(path, CE_SKIP_WORKTREE, mark_skip_worktree_only == MARK_FLAG))
			die("Unable to mark file %s", path);
		return;
	}
	if (mark_fsmonitor_only) {
		if (mark_ce_flags(path, CE_FSMONITOR_VALID, mark_fsmonitor_only == MARK_FLAG))
			die("Unable to mark file %s", path);
		return;
	}

	if (force_remove) {
		if (remove_file_from_index(the_repository->index, path))
			die("git update-index: unable to remove %s", path);
		report("remove '%s'", path);
		return;
	}
	if (process_path(path, &st, stat_errno))
		die("Unable to process path %s", path);
	report("add '%s'", path);
}

static void read_index_info(int nul_term_line)
{
	const int hexsz = the_hash_algo->hexsz;
	struct strbuf buf = STRBUF_INIT;
	struct strbuf uq = STRBUF_INIT;
	strbuf_getline_fn getline_fn;

	getline_fn = nul_term_line ? strbuf_getline_nul : strbuf_getline_lf;
	while (getline_fn(&buf, stdin) != EOF) {
		char *ptr, *tab;
		char *path_name;
		struct object_id oid;
		unsigned int mode;
		unsigned long ul;
		int stage;

		/* This reads lines formatted in one of three formats:
		 *
		 * (1) mode         SP sha1          TAB path
		 * The first format is what "git apply --index-info"
		 * reports, and used to reconstruct a partial tree
		 * that is used for phony merge base tree when falling
		 * back on 3-way merge.
		 *
		 * (2) mode SP type SP sha1          TAB path
		 * The second format is to stuff "git ls-tree" output
		 * into the index file.
		 *
		 * (3) mode         SP sha1 SP stage TAB path
		 * This format is to put higher order stages into the
		 * index file and matches "git ls-files --stage" output.
		 */
		errno = 0;
		ul = strtoul(buf.buf, &ptr, 8);
		if (ptr == buf.buf || *ptr != ' '
		    || errno || (unsigned int) ul != ul)
			goto bad_line;
		mode = ul;

		tab = strchr(ptr, '\t');
		if (!tab || tab - ptr < hexsz + 1)
			goto bad_line;

		if (tab[-2] == ' ' && '0' <= tab[-1] && tab[-1] <= '3') {
			stage = tab[-1] - '0';
			ptr = tab + 1; /* point at the head of path */
			tab = tab - 2; /* point at tail of sha1 */
		}
		else {
			stage = 0;
			ptr = tab + 1; /* point at the head of path */
		}

		if (get_oid_hex(tab - hexsz, &oid) ||
			tab[-(hexsz + 1)] != ' ')
			goto bad_line;

		path_name = ptr;
		if (!nul_term_line && path_name[0] == '"') {
			strbuf_reset(&uq);
			if (unquote_c_style(&uq, path_name, NULL)) {
				die("git update-index: bad quoting of path name");
			}
			path_name = uq.buf;
		}

		if (!verify_path(path_name, mode)) {
			fprintf(stderr, "Ignoring path %s\n", path_name);
			continue;
		}

		if (!mode) {
			/* mode == 0 means there is no such path -- remove */
			if (remove_file_from_index(the_repository->index, path_name))
				die("git update-index: unable to remove %s",
				    ptr);
		}
		else {
			/* mode ' ' sha1 '\t' name
			 * ptr[-1] points at tab,
			 * ptr[-41] is at the beginning of sha1
			 */
			ptr[-(hexsz + 2)] = ptr[-1] = 0;
			if (add_cacheinfo(mode, &oid, path_name, stage))
				die("git update-index: unable to update %s",
				    path_name);
		}
		continue;

	bad_line:
		die("malformed index info %s", buf.buf);
	}
	strbuf_release(&buf);
	strbuf_release(&uq);
}

static const char * const update_index_usage[] = {
	N_("git update-index [<options>] [--] [<file>...]"),
	NULL
};

static struct cache_entry *read_one_ent(const char *which,
					struct object_id *ent, const char *path,
					int namelen, int stage)
{
	unsigned short mode;
	struct object_id oid;
	struct cache_entry *ce;

	if (get_tree_entry(the_repository, ent, path, &oid, &mode)) {
		if (which)
			error("%s: not in %s branch.", path, which);
		return NULL;
	}
	if (!the_repository->index->sparse_index && mode == S_IFDIR) {
		if (which)
			error("%s: not a blob in %s branch.", path, which);
		return NULL;
	}
	ce = make_empty_cache_entry(the_repository->index, namelen);

	oidcpy(&ce->oid, &oid);
	memcpy(ce->name, path, namelen);
	ce->ce_flags = create_ce_flags(stage);
	ce->ce_namelen = namelen;
	ce->ce_mode = create_ce_mode(mode);
	return ce;
}

static int unresolve_one(const char *path)
{
	struct string_list_item *item;
	int res = 0;

	if (!the_repository->index->resolve_undo)
		return res;
	item = string_list_lookup(the_repository->index->resolve_undo, path);
	if (!item)
		return res; /* no resolve-undo record for the path */
	res = unmerge_index_entry(the_repository->index, path, item->util, 0);
	FREE_AND_NULL(item->util);
	return res;
}

static int do_unresolve(int ac, const char **av,
			const char *prefix, int prefix_length)
{
	int i;
	int err = 0;

	for (i = 1; i < ac; i++) {
		const char *arg = av[i];
		char *p = prefix_path(prefix, prefix_length, arg);
		err |= unresolve_one(p);
		free(p);
	}
	return err;
}

static int do_reupdate(const char **paths,
		       const char *prefix)
{
	/* Read HEAD and run update-index on paths that are
	 * merged and already different between index and HEAD.
	 */
	int pos;
	int has_head = 1;
	struct pathspec pathspec;
	struct object_id head_oid;

	parse_pathspec(&pathspec, 0,
		       PATHSPEC_PREFER_CWD,
		       prefix, paths);

	if (refs_read_ref(get_main_ref_store(the_repository), "HEAD", &head_oid))
		/* If there is no HEAD, that means it is an initial
		 * commit.  Update everything in the index.
		 */
		has_head = 0;
 redo:
	for (pos = 0; pos < the_repository->index->cache_nr; pos++) {
		const struct cache_entry *ce = the_repository->index->cache[pos];
		struct cache_entry *old = NULL;
		int save_nr;
		char *path;

		if (ce_stage(ce) || !ce_path_match(the_repository->index, ce, &pathspec, NULL))
			continue;
		if (has_head)
			old = read_one_ent(NULL, &head_oid,
					   ce->name, ce_namelen(ce), 0);
		if (old && ce->ce_mode == old->ce_mode &&
		    oideq(&ce->oid, &old->oid)) {
			discard_cache_entry(old);
			continue; /* unchanged */
		}

		/* At this point, we know the contents of the sparse directory are
		 * modified with respect to HEAD, so we expand the index and restart
		 * to process each path individually
		 */
		if (S_ISSPARSEDIR(ce->ce_mode)) {
			ensure_full_index(the_repository->index);
			goto redo;
		}

		/* Be careful.  The working tree may not have the
		 * path anymore, in which case, under 'allow_remove',
		 * or worse yet 'allow_replace', active_nr may decrease.
		 */
		save_nr = the_repository->index->cache_nr;
		path = xstrdup(ce->name);
		update_one(path);
		free(path);
		discard_cache_entry(old);
		if (save_nr != the_repository->index->cache_nr)
			goto redo;
	}
	clear_pathspec(&pathspec);
	return 0;
}

struct refresh_params {
	unsigned int flags;
	int *has_errors;
};

static int refresh(struct refresh_params *o, unsigned int flag)
{
	setup_work_tree();
	repo_read_index(the_repository);
	*o->has_errors |= refresh_index(the_repository->index, o->flags | flag, NULL,
					NULL, NULL);
	if (has_racy_timestamp(the_repository->index)) {
		/*
		 * Even if nothing else has changed, updating the file
		 * increases the chance that racy timestamps become
		 * non-racy, helping future run-time performance.
		 * We do that even in case of "errors" returned by
		 * refresh_index() as these are no actual errors.
		 * cmd_status() does the same.
		 */
		the_repository->index->cache_changed |= SOMETHING_CHANGED;
	}
	return 0;
}

static int refresh_callback(const struct option *opt,
				const char *arg, int unset)
{
	BUG_ON_OPT_NEG(unset);
	BUG_ON_OPT_ARG(arg);
	return refresh(opt->value, 0);
}

static int really_refresh_callback(const struct option *opt,
				const char *arg, int unset)
{
	BUG_ON_OPT_NEG(unset);
	BUG_ON_OPT_ARG(arg);
	return refresh(opt->value, REFRESH_REALLY);
}

static int chmod_callback(const struct option *opt,
				const char *arg, int unset)
{
	char *flip = opt->value;
	BUG_ON_OPT_NEG(unset);
	if ((arg[0] != '-' && arg[0] != '+') || arg[1] != 'x' || arg[2])
		return error("option 'chmod' expects \"+x\" or \"-x\"");
	*flip = arg[0];
	return 0;
}

static int resolve_undo_clear_callback(const struct option *opt UNUSED,
				const char *arg, int unset)
{
	BUG_ON_OPT_NEG(unset);
	BUG_ON_OPT_ARG(arg);
	resolve_undo_clear_index(the_repository->index);
	return 0;
}

static int parse_new_style_cacheinfo(const char *arg,
				     unsigned int *mode,
				     struct object_id *oid,
				     const char **path)
{
	unsigned long ul;
	char *endp;
	const char *p;

	if (!arg)
		return -1;

	errno = 0;
	ul = strtoul(arg, &endp, 8);
	if (errno || endp == arg || *endp != ',' || (unsigned int) ul != ul)
		return -1; /* not a new-style cacheinfo */
	*mode = ul;
	endp++;
	if (parse_oid_hex(endp, oid, &p) || *p != ',')
		return -1;
	*path = p + 1;
	return 0;
}

static enum parse_opt_result cacheinfo_callback(
	struct parse_opt_ctx_t *ctx, const struct option *opt UNUSED,
	const char *arg, int unset)
{
	struct object_id oid;
	unsigned int mode;
	const char *path;

	BUG_ON_OPT_NEG(unset);
	BUG_ON_OPT_ARG(arg);

	if (!parse_new_style_cacheinfo(ctx->argv[1], &mode, &oid, &path)) {
		if (add_cacheinfo(mode, &oid, path, 0))
			die("git update-index: --cacheinfo cannot add %s", path);
		ctx->argv++;
		ctx->argc--;
		return 0;
	}
	if (ctx->argc <= 3)
		return error("option 'cacheinfo' expects <mode>,<sha1>,<path>");
	if (strtoul_ui(*++ctx->argv, 8, &mode) ||
	    get_oid_hex(*++ctx->argv, &oid) ||
	    add_cacheinfo(mode, &oid, *++ctx->argv, 0))
		die("git update-index: --cacheinfo cannot add %s", *ctx->argv);
	ctx->argc -= 3;
	return 0;
}

static enum parse_opt_result stdin_cacheinfo_callback(
	struct parse_opt_ctx_t *ctx, const struct option *opt,
	const char *arg, int unset)
{
	int *nul_term_line = opt->value;

	BUG_ON_OPT_NEG(unset);
	BUG_ON_OPT_ARG(arg);

	if (ctx->argc != 1)
		return error("option '%s' must be the last argument", opt->long_name);
	allow_add = allow_replace = allow_remove = 1;
	read_index_info(*nul_term_line);
	return 0;
}

static enum parse_opt_result stdin_callback(
	struct parse_opt_ctx_t *ctx, const struct option *opt,
	const char *arg, int unset)
{
	int *read_from_stdin = opt->value;

	BUG_ON_OPT_NEG(unset);
	BUG_ON_OPT_ARG(arg);

	if (ctx->argc != 1)
		return error("option '%s' must be the last argument", opt->long_name);
	*read_from_stdin = 1;
	return 0;
}

static enum parse_opt_result unresolve_callback(
	struct parse_opt_ctx_t *ctx, const struct option *opt,
	const char *arg, int unset)
{
	int *has_errors = opt->value;
	const char *prefix = startup_info->prefix;

	BUG_ON_OPT_NEG(unset);
	BUG_ON_OPT_ARG(arg);

	/* consume remaining arguments. */
	*has_errors = do_unresolve(ctx->argc, ctx->argv,
				prefix, prefix ? strlen(prefix) : 0);
	if (*has_errors)
		the_repository->index->cache_changed = 0;

	ctx->argv += ctx->argc - 1;
	ctx->argc = 1;
	return 0;
}

static enum parse_opt_result reupdate_callback(
	struct parse_opt_ctx_t *ctx, const struct option *opt,
	const char *arg, int unset)
{
	int *has_errors = opt->value;
	const char *prefix = startup_info->prefix;

	BUG_ON_OPT_NEG(unset);
	BUG_ON_OPT_ARG(arg);

	/* consume remaining arguments. */
	setup_work_tree();
	*has_errors = do_reupdate(ctx->argv + 1, prefix);
	if (*has_errors)
		the_repository->index->cache_changed = 0;

	ctx->argv += ctx->argc - 1;
	ctx->argc = 1;
	return 0;
}

int cmd_update_index(int argc,
		     const char **argv,
		     const char *prefix,
		     struct repository *repo UNUSED)
{
	int newfd, entries, has_errors = 0, nul_term_line = 0;
	enum uc_mode untracked_cache = UC_UNSPECIFIED;
	int read_from_stdin = 0;
	int prefix_length = prefix ? strlen(prefix) : 0;
	int preferred_index_format = 0;
	char set_executable_bit = 0;
	struct refresh_params refresh_args = {0, &has_errors};
	int lock_error = 0;
	int split_index = -1;
	int force_write = 0;
	int fsmonitor = -1;
	struct lock_file lock_file = LOCK_INIT;
	struct parse_opt_ctx_t ctx;
	strbuf_getline_fn getline_fn;
	int parseopt_state = PARSE_OPT_UNKNOWN;
	struct repository *r = the_repository;
	struct option options[] = {
		OPT_BIT('q', NULL, &refresh_args.flags,
			N_("continue refresh even when index needs update"),
			REFRESH_QUIET),
		OPT_BIT(0, "ignore-submodules", &refresh_args.flags,
			N_("refresh: ignore submodules"),
			REFRESH_IGNORE_SUBMODULES),
		OPT_SET_INT(0, "add", &allow_add,
			N_("do not ignore new files"), 1),
		OPT_SET_INT(0, "replace", &allow_replace,
			N_("let files replace directories and vice-versa"), 1),
		OPT_SET_INT(0, "remove", &allow_remove,
			N_("notice files missing from worktree"), 1),
		OPT_BIT(0, "unmerged", &refresh_args.flags,
			N_("refresh even if index contains unmerged entries"),
			REFRESH_UNMERGED),
		OPT_CALLBACK_F(0, "refresh", &refresh_args, NULL,
			N_("refresh stat information"),
			PARSE_OPT_NOARG | PARSE_OPT_NONEG,
			refresh_callback),
		OPT_CALLBACK_F(0, "really-refresh", &refresh_args, NULL,
			N_("like --refresh, but ignore assume-unchanged setting"),
			PARSE_OPT_NOARG | PARSE_OPT_NONEG,
			really_refresh_callback),
		{OPTION_LOWLEVEL_CALLBACK, 0, "cacheinfo", NULL,
			N_("<mode>,<object>,<path>"),
			N_("add the specified entry to the index"),
			PARSE_OPT_NOARG | /* disallow --cacheinfo=<mode> form */
			PARSE_OPT_NONEG | PARSE_OPT_LITERAL_ARGHELP,
			NULL, 0,
			cacheinfo_callback},
		OPT_CALLBACK_F(0, "chmod", &set_executable_bit, "(+|-)x",
			N_("override the executable bit of the listed files"),
			PARSE_OPT_NONEG,
			chmod_callback),
		{OPTION_SET_INT, 0, "assume-unchanged", &mark_valid_only, NULL,
			N_("mark files as \"not changing\""),
			PARSE_OPT_NOARG | PARSE_OPT_NONEG, NULL, MARK_FLAG},
		{OPTION_SET_INT, 0, "no-assume-unchanged", &mark_valid_only, NULL,
			N_("clear assumed-unchanged bit"),
			PARSE_OPT_NOARG | PARSE_OPT_NONEG, NULL, UNMARK_FLAG},
		{OPTION_SET_INT, 0, "skip-worktree", &mark_skip_worktree_only, NULL,
			N_("mark files as \"index-only\""),
			PARSE_OPT_NOARG | PARSE_OPT_NONEG, NULL, MARK_FLAG},
		{OPTION_SET_INT, 0, "no-skip-worktree", &mark_skip_worktree_only, NULL,
			N_("clear skip-worktree bit"),
			PARSE_OPT_NOARG | PARSE_OPT_NONEG, NULL, UNMARK_FLAG},
		OPT_BOOL(0, "ignore-skip-worktree-entries", &ignore_skip_worktree_entries,
			 N_("do not touch index-only entries")),
		OPT_SET_INT(0, "info-only", &info_only,
			N_("add to index only; do not add content to object database"), 1),
		OPT_SET_INT(0, "force-remove", &force_remove,
			N_("remove named paths even if present in worktree"), 1),
		OPT_BOOL('z', NULL, &nul_term_line,
			 N_("with --stdin: input lines are terminated by null bytes")),
		{OPTION_LOWLEVEL_CALLBACK, 0, "stdin", &read_from_stdin, NULL,
			N_("read list of paths to be updated from standard input"),
			PARSE_OPT_NONEG | PARSE_OPT_NOARG,
			NULL, 0, stdin_callback},
		{OPTION_LOWLEVEL_CALLBACK, 0, "index-info", &nul_term_line, NULL,
			N_("add entries from standard input to the index"),
			PARSE_OPT_NONEG | PARSE_OPT_NOARG,
			NULL, 0, stdin_cacheinfo_callback},
		{OPTION_LOWLEVEL_CALLBACK, 0, "unresolve", &has_errors, NULL,
			N_("repopulate stages #2 and #3 for the listed paths"),
			PARSE_OPT_NONEG | PARSE_OPT_NOARG,
			NULL, 0, unresolve_callback},
		{OPTION_LOWLEVEL_CALLBACK, 'g', "again", &has_errors, NULL,
			N_("only update entries that differ from HEAD"),
			PARSE_OPT_NONEG | PARSE_OPT_NOARG,
			NULL, 0, reupdate_callback},
		OPT_BIT(0, "ignore-missing", &refresh_args.flags,
			N_("ignore files missing from worktree"),
			REFRESH_IGNORE_MISSING),
		OPT_SET_INT(0, "verbose", &verbose,
			N_("report actions to standard output"), 1),
		OPT_CALLBACK_F(0, "clear-resolve-undo", NULL, NULL,
			N_("(for porcelains) forget saved unresolved conflicts"),
			PARSE_OPT_NOARG | PARSE_OPT_NONEG,
			resolve_undo_clear_callback),
		OPT_INTEGER(0, "index-version", &preferred_index_format,
			N_("write index in this format")),
		OPT_SET_INT(0, "show-index-version", &preferred_index_format,
			    N_("report on-disk index format version"), -1),
		OPT_BOOL(0, "split-index", &split_index,
			N_("enable or disable split index")),
		OPT_BOOL(0, "untracked-cache", &untracked_cache,
			N_("enable/disable untracked cache")),
		OPT_SET_INT(0, "test-untracked-cache", &untracked_cache,
			    N_("test if the filesystem supports untracked cache"), UC_TEST),
		OPT_SET_INT(0, "force-untracked-cache", &untracked_cache,
			    N_("enable untracked cache without testing the filesystem"), UC_FORCE),
		OPT_SET_INT(0, "force-write-index", &force_write,
			N_("write out the index even if is not flagged as changed"), 1),
		OPT_BOOL(0, "fsmonitor", &fsmonitor,
			N_("enable or disable file system monitor")),
		{OPTION_SET_INT, 0, "fsmonitor-valid", &mark_fsmonitor_only, NULL,
			N_("mark files as fsmonitor valid"),
			PARSE_OPT_NOARG | PARSE_OPT_NONEG, NULL, MARK_FLAG},
		{OPTION_SET_INT, 0, "no-fsmonitor-valid", &mark_fsmonitor_only, NULL,
			N_("clear fsmonitor valid bit"),
			PARSE_OPT_NOARG | PARSE_OPT_NONEG, NULL, UNMARK_FLAG},
		OPT_END()
	};

	if (argc == 2 && !strcmp(argv[1], "-h"))
		usage_with_options(update_index_usage, options);

	git_config(git_default_config, NULL);

	prepare_repo_settings(r);
	the_repository->settings.command_requires_full_index = 0;

	/* we will diagnose later if it turns out that we need to update it */
	newfd = repo_hold_locked_index(the_repository, &lock_file, 0);
	if (newfd < 0)
		lock_error = errno;

	entries = repo_read_index(the_repository);
	if (entries < 0)
		die("cache corrupted");

	the_repository->index->updated_skipworktree = 1;

	/*
	 * Custom copy of parse_options() because we want to handle
	 * filename arguments as they come.
	 */
	parse_options_start(&ctx, argc, argv, prefix,
			    options, PARSE_OPT_STOP_AT_NON_OPTION);

	/*
	 * Allow the object layer to optimize adding multiple objects in
	 * a batch.
	 */
	begin_odb_transaction();
	while (ctx.argc) {
		if (parseopt_state != PARSE_OPT_DONE)
			parseopt_state = parse_options_step(&ctx, options,
							    update_index_usage);
		if (!ctx.argc)
			break;
		switch (parseopt_state) {
		case PARSE_OPT_HELP:
		case PARSE_OPT_ERROR:
			exit(129);
		case PARSE_OPT_COMPLETE:
			exit(0);
		case PARSE_OPT_NON_OPTION:
		case PARSE_OPT_DONE:
		{
			const char *path = ctx.argv[0];
			char *p;

			setup_work_tree();
			p = prefix_path(prefix, prefix_length, path);
			update_one(p);
			if (set_executable_bit)
				chmod_path(set_executable_bit, p);
			free(p);
			ctx.argc--;
			ctx.argv++;
			break;
		}
		case PARSE_OPT_UNKNOWN:
			if (ctx.argv[0][1] == '-')
				error("unknown option '%s'", ctx.argv[0] + 2);
			else
				error("unknown switch '%c'", *ctx.opt);
			usage_with_options(update_index_usage, options);
		}
	}
	argc = parse_options_end(&ctx);

	getline_fn = nul_term_line ? strbuf_getline_nul : strbuf_getline_lf;
	if (preferred_index_format) {
		if (preferred_index_format < 0) {
			printf(_("%d\n"), the_repository->index->version);
		} else if (preferred_index_format < INDEX_FORMAT_LB ||
			   INDEX_FORMAT_UB < preferred_index_format) {
			die("index-version %d not in range: %d..%d",
			    preferred_index_format,
			    INDEX_FORMAT_LB, INDEX_FORMAT_UB);
		} else {
			if (the_repository->index->version != preferred_index_format)
				the_repository->index->cache_changed |= SOMETHING_CHANGED;
			report(_("index-version: was %d, set to %d"),
			       the_repository->index->version, preferred_index_format);
			the_repository->index->version = preferred_index_format;
		}
	}

	if (read_from_stdin) {
		struct strbuf buf = STRBUF_INIT;
		struct strbuf unquoted = STRBUF_INIT;

		setup_work_tree();
		while (getline_fn(&buf, stdin) != EOF) {
			char *p;
			if (!nul_term_line && buf.buf[0] == '"') {
				strbuf_reset(&unquoted);
				if (unquote_c_style(&unquoted, buf.buf, NULL))
					die("line is badly quoted");
				strbuf_swap(&buf, &unquoted);
			}
			p = prefix_path(prefix, prefix_length, buf.buf);
			update_one(p);
			if (set_executable_bit)
				chmod_path(set_executable_bit, p);
			free(p);
		}
		strbuf_release(&unquoted);
		strbuf_release(&buf);
	}

	/*
	 * By now we have added all of the new objects
	 */
	end_odb_transaction();

	if (split_index > 0) {
		if (repo_config_get_split_index(the_repository) == 0)
			warning(_("core.splitIndex is set to false; "
				  "remove or change it, if you really want to "
				  "enable split index"));
		if (the_repository->index->split_index)
			the_repository->index->cache_changed |= SPLIT_INDEX_ORDERED;
		else
			add_split_index(the_repository->index);
	} else if (!split_index) {
		if (repo_config_get_split_index(the_repository) == 1)
			warning(_("core.splitIndex is set to true; "
				  "remove or change it, if you really want to "
				  "disable split index"));
		remove_split_index(the_repository->index);
	}

	prepare_repo_settings(r);
	switch (untracked_cache) {
	case UC_UNSPECIFIED:
		break;
	case UC_DISABLE:
		if (r->settings.core_untracked_cache == UNTRACKED_CACHE_WRITE)
			warning(_("core.untrackedCache is set to true; "
				  "remove or change it, if you really want to "
				  "disable the untracked cache"));
		remove_untracked_cache(the_repository->index);
		report(_("Untracked cache disabled"));
		break;
	case UC_TEST:
		setup_work_tree();
		return !test_if_untracked_cache_is_supported();
	case UC_ENABLE:
	case UC_FORCE:
		if (r->settings.core_untracked_cache == UNTRACKED_CACHE_REMOVE)
			warning(_("core.untrackedCache is set to false; "
				  "remove or change it, if you really want to "
				  "enable the untracked cache"));
		add_untracked_cache(the_repository->index);
		report(_("Untracked cache enabled for '%s'"), repo_get_work_tree(the_repository));
		break;
	default:
		BUG("bad untracked_cache value: %d", untracked_cache);
	}

	if (fsmonitor > 0) {
		enum fsmonitor_mode fsm_mode = fsm_settings__get_mode(r);
		enum fsmonitor_reason reason = fsm_settings__get_reason(r);

		/*
		 * The user wants to turn on FSMonitor using the command
		 * line argument.  (We don't know (or care) whether that
		 * is the IPC or HOOK version.)
		 *
		 * Use one of the __get routines to force load the FSMonitor
		 * config settings into the repo-settings.  That will detect
		 * whether the file system is compatible so that we can stop
		 * here with a nice error message.
		 */
		if (reason > FSMONITOR_REASON_OK)
			die("%s",
			    fsm_settings__get_incompatible_msg(r, reason));

		if (fsm_mode == FSMONITOR_MODE_DISABLED) {
			warning(_("core.fsmonitor is unset; "
				"set it if you really want to "
				"enable fsmonitor"));
		}
		add_fsmonitor(the_repository->index);
		report(_("fsmonitor enabled"));
	} else if (!fsmonitor) {
		enum fsmonitor_mode fsm_mode = fsm_settings__get_mode(r);
		if (fsm_mode > FSMONITOR_MODE_DISABLED)
			warning(_("core.fsmonitor is set; "
				"remove it if you really want to "
				"disable fsmonitor"));
		remove_fsmonitor(the_repository->index);
		report(_("fsmonitor disabled"));
	}

	if (the_repository->index->cache_changed || force_write) {
		if (newfd < 0) {
			if (refresh_args.flags & REFRESH_QUIET)
				exit(128);
			unable_to_lock_die(repo_get_index_file(the_repository), lock_error);
		}
		if (write_locked_index(the_repository->index, &lock_file, COMMIT_LOCK))
			die("Unable to write new index file");
	}

	rollback_lock_file(&lock_file);

	return has_errors ? 1 : 0;
}
