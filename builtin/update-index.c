/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#include "cache.h"
#include "quote.h"
#include "cache-tree.h"
#include "tree-walk.h"
#include "builtin.h"
#include "refs.h"
#include "resolve-undo.h"
#include "parse-options.h"

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
#define MARK_FLAG 1
#define UNMARK_FLAG 2

__attribute__((format (printf, 1, 2)))
static void report(const char *fmt, ...)
{
	va_list vp;

	if (!verbose)
		return;

	va_start(vp, fmt);
	vprintf(fmt, vp);
	putchar('\n');
	va_end(vp);
}

static int mark_ce_flags(const char *path, int flag, int mark)
{
	int namelen = strlen(path);
	int pos = cache_name_pos(path, namelen);
	if (0 <= pos) {
		if (mark)
			active_cache[pos]->ce_flags |= flag;
		else
			active_cache[pos]->ce_flags &= ~flag;
		cache_tree_invalidate_path(active_cache_tree, path);
		active_cache_changed = 1;
		return 0;
	}
	return -1;
}

static int remove_one_path(const char *path)
{
	if (!allow_remove)
		return error("%s: does not exist and --remove not passed", path);
	if (remove_file_from_cache(path))
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
	if (err == ENOENT || err == ENOTDIR)
		return remove_one_path(path);
	return error("lstat(\"%s\"): %s", path, strerror(errno));
}

static int add_one_path(struct cache_entry *old, const char *path, int len, struct stat *st)
{
	int option, size;
	struct cache_entry *ce;

	/* Was the old index entry already up-to-date? */
	if (old && !ce_stage(old) && !ce_match_stat(old, st, 0))
		return 0;

	size = cache_entry_size(len);
	ce = xcalloc(1, size);
	memcpy(ce->name, path, len);
	ce->ce_flags = len;
	fill_stat_cache_info(ce, st);
	ce->ce_mode = ce_mode_from_stat(old, st->st_mode);

	if (index_path(ce->sha1, path, st,
		       info_only ? 0 : HASH_WRITE_OBJECT)) {
		free(ce);
		return -1;
	}
	option = allow_add ? ADD_CACHE_OK_TO_ADD : 0;
	option |= allow_replace ? ADD_CACHE_OK_TO_REPLACE : 0;
	if (add_cache_entry(ce, option))
		return error("%s: cannot add to the index - missing --add option?", path);
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
	unsigned char sha1[20];
	int pos = cache_name_pos(path, len);

	/* Exact match: file or existing gitlink */
	if (pos >= 0) {
		struct cache_entry *ce = active_cache[pos];
		if (S_ISGITLINK(ce->ce_mode)) {

			/* Do nothing to the index if there is no HEAD! */
			if (resolve_gitlink_ref(path, "HEAD", sha1) < 0)
				return 0;

			return add_one_path(ce, path, len, st);
		}
		/* Should this be an unconditional error? */
		return remove_one_path(path);
	}

	/* Inexact match: is there perhaps a subdirectory match? */
	pos = -pos-1;
	while (pos < active_nr) {
		struct cache_entry *ce = active_cache[pos++];

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
	if (!resolve_gitlink_ref(path, "HEAD", sha1))
		return add_one_path(NULL, path, len, st);

	/* Error out. */
	return error("%s: is a directory - add files inside instead", path);
}

static int process_path(const char *path)
{
	int pos, len;
	struct stat st;
	struct cache_entry *ce;

	len = strlen(path);
	if (has_symlink_leading_path(path, len))
		return error("'%s' is beyond a symbolic link", path);

	pos = cache_name_pos(path, len);
	ce = pos < 0 ? NULL : active_cache[pos];
	if (ce && ce_skip_worktree(ce)) {
		/*
		 * working directory version is assumed "good"
		 * so updating it does not make sense.
		 * On the other hand, removing it from index should work
		 */
		if (allow_remove && remove_file_from_cache(path))
			return error("%s: cannot remove from the index", path);
		return 0;
	}

	/*
	 * First things first: get the stat information, to decide
	 * what to do about the pathname!
	 */
	if (lstat(path, &st) < 0)
		return process_lstat_error(path, errno);

	if (S_ISDIR(st.st_mode))
		return process_directory(path, len, &st);

	/*
	 * Process a regular file
	 */
	if (ce && S_ISGITLINK(ce->ce_mode))
		return error("%s is already a gitlink, not replacing", path);

	return add_one_path(ce, path, len, &st);
}

static int add_cacheinfo(unsigned int mode, const unsigned char *sha1,
			 const char *path, int stage)
{
	int size, len, option;
	struct cache_entry *ce;

	if (!verify_path(path))
		return error("Invalid path '%s'", path);

	len = strlen(path);
	size = cache_entry_size(len);
	ce = xcalloc(1, size);

	hashcpy(ce->sha1, sha1);
	memcpy(ce->name, path, len);
	ce->ce_flags = create_ce_flags(len, stage);
	ce->ce_mode = create_ce_mode(mode);
	if (assume_unchanged)
		ce->ce_flags |= CE_VALID;
	option = allow_add ? ADD_CACHE_OK_TO_ADD : 0;
	option |= allow_replace ? ADD_CACHE_OK_TO_REPLACE : 0;
	if (add_cache_entry(ce, option))
		return error("%s: cannot add to the index - missing --add option?",
			     path);
	report("add '%s'", path);
	return 0;
}

static void chmod_path(int flip, const char *path)
{
	int pos;
	struct cache_entry *ce;
	unsigned int mode;

	pos = cache_name_pos(path, strlen(path));
	if (pos < 0)
		goto fail;
	ce = active_cache[pos];
	mode = ce->ce_mode;
	if (!S_ISREG(mode))
		goto fail;
	switch (flip) {
	case '+':
		ce->ce_mode |= 0111; break;
	case '-':
		ce->ce_mode &= ~0111; break;
	default:
		goto fail;
	}
	cache_tree_invalidate_path(active_cache_tree, path);
	active_cache_changed = 1;
	report("chmod %cx '%s'", flip, path);
	return;
 fail:
	die("git update-index: cannot chmod %cx '%s'", flip, path);
}

static void update_one(const char *path, const char *prefix, int prefix_length)
{
	const char *p = prefix_path(prefix, prefix_length, path);
	if (!verify_path(p)) {
		fprintf(stderr, "Ignoring path %s\n", path);
		goto free_return;
	}
	if (mark_valid_only) {
		if (mark_ce_flags(p, CE_VALID, mark_valid_only == MARK_FLAG))
			die("Unable to mark file %s", path);
		goto free_return;
	}
	if (mark_skip_worktree_only) {
		if (mark_ce_flags(p, CE_SKIP_WORKTREE, mark_skip_worktree_only == MARK_FLAG))
			die("Unable to mark file %s", path);
		goto free_return;
	}

	if (force_remove) {
		if (remove_file_from_cache(p))
			die("git update-index: unable to remove %s", path);
		report("remove '%s'", path);
		goto free_return;
	}
	if (process_path(p))
		die("Unable to process path %s", path);
	report("add '%s'", path);
 free_return:
	if (p < path || p > path + strlen(path))
		free((char *)p);
}

static void read_index_info(int line_termination)
{
	struct strbuf buf = STRBUF_INIT;
	struct strbuf uq = STRBUF_INIT;

	while (strbuf_getline(&buf, stdin, line_termination) != EOF) {
		char *ptr, *tab;
		char *path_name;
		unsigned char sha1[20];
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
		if (!tab || tab - ptr < 41)
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

		if (get_sha1_hex(tab - 40, sha1) || tab[-41] != ' ')
			goto bad_line;

		path_name = ptr;
		if (line_termination && path_name[0] == '"') {
			strbuf_reset(&uq);
			if (unquote_c_style(&uq, path_name, NULL)) {
				die("git update-index: bad quoting of path name");
			}
			path_name = uq.buf;
		}

		if (!verify_path(path_name)) {
			fprintf(stderr, "Ignoring path %s\n", path_name);
			continue;
		}

		if (!mode) {
			/* mode == 0 means there is no such path -- remove */
			if (remove_file_from_cache(path_name))
				die("git update-index: unable to remove %s",
				    ptr);
		}
		else {
			/* mode ' ' sha1 '\t' name
			 * ptr[-1] points at tab,
			 * ptr[-41] is at the beginning of sha1
			 */
			ptr[-42] = ptr[-1] = 0;
			if (add_cacheinfo(mode, sha1, path_name, stage))
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
	"git update-index [options] [--] [<file>...]",
	NULL
};

static unsigned char head_sha1[20];
static unsigned char merge_head_sha1[20];

static struct cache_entry *read_one_ent(const char *which,
					unsigned char *ent, const char *path,
					int namelen, int stage)
{
	unsigned mode;
	unsigned char sha1[20];
	int size;
	struct cache_entry *ce;

	if (get_tree_entry(ent, path, sha1, &mode)) {
		if (which)
			error("%s: not in %s branch.", path, which);
		return NULL;
	}
	if (mode == S_IFDIR) {
		if (which)
			error("%s: not a blob in %s branch.", path, which);
		return NULL;
	}
	size = cache_entry_size(namelen);
	ce = xcalloc(1, size);

	hashcpy(ce->sha1, sha1);
	memcpy(ce->name, path, namelen);
	ce->ce_flags = create_ce_flags(namelen, stage);
	ce->ce_mode = create_ce_mode(mode);
	return ce;
}

static int unresolve_one(const char *path)
{
	int namelen = strlen(path);
	int pos;
	int ret = 0;
	struct cache_entry *ce_2 = NULL, *ce_3 = NULL;

	/* See if there is such entry in the index. */
	pos = cache_name_pos(path, namelen);
	if (0 <= pos) {
		/* already merged */
		pos = unmerge_cache_entry_at(pos);
		if (pos < active_nr) {
			struct cache_entry *ce = active_cache[pos];
			if (ce_stage(ce) &&
			    ce_namelen(ce) == namelen &&
			    !memcmp(ce->name, path, namelen))
				return 0;
		}
		/* no resolve-undo information; fall back */
	} else {
		/* If there isn't, either it is unmerged, or
		 * resolved as "removed" by mistake.  We do not
		 * want to do anything in the former case.
		 */
		pos = -pos-1;
		if (pos < active_nr) {
			struct cache_entry *ce = active_cache[pos];
			if (ce_namelen(ce) == namelen &&
			    !memcmp(ce->name, path, namelen)) {
				fprintf(stderr,
					"%s: skipping still unmerged path.\n",
					path);
				goto free_return;
			}
		}
	}

	/* Grab blobs from given path from HEAD and MERGE_HEAD,
	 * stuff HEAD version in stage #2,
	 * stuff MERGE_HEAD version in stage #3.
	 */
	ce_2 = read_one_ent("our", head_sha1, path, namelen, 2);
	ce_3 = read_one_ent("their", merge_head_sha1, path, namelen, 3);

	if (!ce_2 || !ce_3) {
		ret = -1;
		goto free_return;
	}
	if (!hashcmp(ce_2->sha1, ce_3->sha1) &&
	    ce_2->ce_mode == ce_3->ce_mode) {
		fprintf(stderr, "%s: identical in both, skipping.\n",
			path);
		goto free_return;
	}

	remove_file_from_cache(path);
	if (add_cache_entry(ce_2, ADD_CACHE_OK_TO_ADD)) {
		error("%s: cannot add our version to the index.", path);
		ret = -1;
		goto free_return;
	}
	if (!add_cache_entry(ce_3, ADD_CACHE_OK_TO_ADD))
		return 0;
	error("%s: cannot add their version to the index.", path);
	ret = -1;
 free_return:
	free(ce_2);
	free(ce_3);
	return ret;
}

static void read_head_pointers(void)
{
	if (read_ref("HEAD", head_sha1))
		die("No HEAD -- no initial commit yet?");
	if (read_ref("MERGE_HEAD", merge_head_sha1)) {
		fprintf(stderr, "Not in the middle of a merge.\n");
		exit(0);
	}
}

static int do_unresolve(int ac, const char **av,
			const char *prefix, int prefix_length)
{
	int i;
	int err = 0;

	/* Read HEAD and MERGE_HEAD; if MERGE_HEAD does not exist, we
	 * are not doing a merge, so exit with success status.
	 */
	read_head_pointers();

	for (i = 1; i < ac; i++) {
		const char *arg = av[i];
		const char *p = prefix_path(prefix, prefix_length, arg);
		err |= unresolve_one(p);
		if (p < arg || p > arg + strlen(arg))
			free((char *)p);
	}
	return err;
}

static int do_reupdate(int ac, const char **av,
		       const char *prefix, int prefix_length)
{
	/* Read HEAD and run update-index on paths that are
	 * merged and already different between index and HEAD.
	 */
	int pos;
	int has_head = 1;
	const char **paths = get_pathspec(prefix, av + 1);
	struct pathspec pathspec;

	init_pathspec(&pathspec, paths);

	if (read_ref("HEAD", head_sha1))
		/* If there is no HEAD, that means it is an initial
		 * commit.  Update everything in the index.
		 */
		has_head = 0;
 redo:
	for (pos = 0; pos < active_nr; pos++) {
		struct cache_entry *ce = active_cache[pos];
		struct cache_entry *old = NULL;
		int save_nr;

		if (ce_stage(ce) || !ce_path_match(ce, &pathspec))
			continue;
		if (has_head)
			old = read_one_ent(NULL, head_sha1,
					   ce->name, ce_namelen(ce), 0);
		if (old && ce->ce_mode == old->ce_mode &&
		    !hashcmp(ce->sha1, old->sha1)) {
			free(old);
			continue; /* unchanged */
		}
		/* Be careful.  The working tree may not have the
		 * path anymore, in which case, under 'allow_remove',
		 * or worse yet 'allow_replace', active_nr may decrease.
		 */
		save_nr = active_nr;
		update_one(ce->name + prefix_length, prefix, prefix_length);
		if (save_nr != active_nr)
			goto redo;
	}
	free_pathspec(&pathspec);
	return 0;
}

struct refresh_params {
	unsigned int flags;
	int *has_errors;
};

static int refresh(struct refresh_params *o, unsigned int flag)
{
	setup_work_tree();
	*o->has_errors |= refresh_cache(o->flags | flag);
	return 0;
}

static int refresh_callback(const struct option *opt,
				const char *arg, int unset)
{
	return refresh(opt->value, 0);
}

static int really_refresh_callback(const struct option *opt,
				const char *arg, int unset)
{
	return refresh(opt->value, REFRESH_REALLY);
}

static int chmod_callback(const struct option *opt,
				const char *arg, int unset)
{
	char *flip = opt->value;
	if ((arg[0] != '-' && arg[0] != '+') || arg[1] != 'x' || arg[2])
		return error("option 'chmod' expects \"+x\" or \"-x\"");
	*flip = arg[0];
	return 0;
}

static int resolve_undo_clear_callback(const struct option *opt,
				const char *arg, int unset)
{
	resolve_undo_clear();
	return 0;
}

static int cacheinfo_callback(struct parse_opt_ctx_t *ctx,
				const struct option *opt, int unset)
{
	unsigned char sha1[20];
	unsigned int mode;

	if (ctx->argc <= 3)
		return error("option 'cacheinfo' expects three arguments");
	if (strtoul_ui(*++ctx->argv, 8, &mode) ||
	    get_sha1_hex(*++ctx->argv, sha1) ||
	    add_cacheinfo(mode, sha1, *++ctx->argv, 0))
		die("git update-index: --cacheinfo cannot add %s", *ctx->argv);
	ctx->argc -= 3;
	return 0;
}

static int stdin_cacheinfo_callback(struct parse_opt_ctx_t *ctx,
			      const struct option *opt, int unset)
{
	int *line_termination = opt->value;

	if (ctx->argc != 1)
		return error("option '%s' must be the last argument", opt->long_name);
	allow_add = allow_replace = allow_remove = 1;
	read_index_info(*line_termination);
	return 0;
}

static int stdin_callback(struct parse_opt_ctx_t *ctx,
				const struct option *opt, int unset)
{
	int *read_from_stdin = opt->value;

	if (ctx->argc != 1)
		return error("option '%s' must be the last argument", opt->long_name);
	*read_from_stdin = 1;
	return 0;
}

static int unresolve_callback(struct parse_opt_ctx_t *ctx,
				const struct option *opt, int flags)
{
	int *has_errors = opt->value;
	const char *prefix = startup_info->prefix;

	/* consume remaining arguments. */
	*has_errors = do_unresolve(ctx->argc, ctx->argv,
				prefix, prefix ? strlen(prefix) : 0);
	if (*has_errors)
		active_cache_changed = 0;

	ctx->argv += ctx->argc - 1;
	ctx->argc = 1;
	return 0;
}

static int reupdate_callback(struct parse_opt_ctx_t *ctx,
				const struct option *opt, int flags)
{
	int *has_errors = opt->value;
	const char *prefix = startup_info->prefix;

	/* consume remaining arguments. */
	setup_work_tree();
	*has_errors = do_reupdate(ctx->argc, ctx->argv,
				prefix, prefix ? strlen(prefix) : 0);
	if (*has_errors)
		active_cache_changed = 0;

	ctx->argv += ctx->argc - 1;
	ctx->argc = 1;
	return 0;
}

int cmd_update_index(int argc, const char **argv, const char *prefix)
{
	int newfd, entries, has_errors = 0, line_termination = '\n';
	int read_from_stdin = 0;
	int prefix_length = prefix ? strlen(prefix) : 0;
	char set_executable_bit = 0;
	struct refresh_params refresh_args = {0, &has_errors};
	int lock_error = 0;
	struct lock_file *lock_file;
	struct parse_opt_ctx_t ctx;
	int parseopt_state = PARSE_OPT_UNKNOWN;
	struct option options[] = {
		OPT_BIT('q', NULL, &refresh_args.flags,
			"continue refresh even when index needs update",
			REFRESH_QUIET),
		OPT_BIT(0, "ignore-submodules", &refresh_args.flags,
			"refresh: ignore submodules",
			REFRESH_IGNORE_SUBMODULES),
		OPT_SET_INT(0, "add", &allow_add,
			"do not ignore new files", 1),
		OPT_SET_INT(0, "replace", &allow_replace,
			"let files replace directories and vice-versa", 1),
		OPT_SET_INT(0, "remove", &allow_remove,
			"notice files missing from worktree", 1),
		OPT_BIT(0, "unmerged", &refresh_args.flags,
			"refresh even if index contains unmerged entries",
			REFRESH_UNMERGED),
		{OPTION_CALLBACK, 0, "refresh", &refresh_args, NULL,
			"refresh stat information",
			PARSE_OPT_NOARG | PARSE_OPT_NONEG,
			refresh_callback},
		{OPTION_CALLBACK, 0, "really-refresh", &refresh_args, NULL,
			"like --refresh, but ignore assume-unchanged setting",
			PARSE_OPT_NOARG | PARSE_OPT_NONEG,
			really_refresh_callback},
		{OPTION_LOWLEVEL_CALLBACK, 0, "cacheinfo", NULL,
			"<mode> <object> <path>",
			"add the specified entry to the index",
			PARSE_OPT_NOARG |	/* disallow --cacheinfo=<mode> form */
			PARSE_OPT_NONEG | PARSE_OPT_LITERAL_ARGHELP,
			(parse_opt_cb *) cacheinfo_callback},
		{OPTION_CALLBACK, 0, "chmod", &set_executable_bit, "(+/-)x",
			"override the executable bit of the listed files",
			PARSE_OPT_NONEG | PARSE_OPT_LITERAL_ARGHELP,
			chmod_callback},
		{OPTION_SET_INT, 0, "assume-unchanged", &mark_valid_only, NULL,
			"mark files as \"not changing\"",
			PARSE_OPT_NOARG | PARSE_OPT_NONEG, NULL, MARK_FLAG},
		{OPTION_SET_INT, 0, "no-assume-unchanged", &mark_valid_only, NULL,
			"clear assumed-unchanged bit",
			PARSE_OPT_NOARG | PARSE_OPT_NONEG, NULL, UNMARK_FLAG},
		{OPTION_SET_INT, 0, "skip-worktree", &mark_skip_worktree_only, NULL,
			"mark files as \"index-only\"",
			PARSE_OPT_NOARG | PARSE_OPT_NONEG, NULL, MARK_FLAG},
		{OPTION_SET_INT, 0, "no-skip-worktree", &mark_skip_worktree_only, NULL,
			"clear skip-worktree bit",
			PARSE_OPT_NOARG | PARSE_OPT_NONEG, NULL, UNMARK_FLAG},
		OPT_SET_INT(0, "info-only", &info_only,
			"add to index only; do not add content to object database", 1),
		OPT_SET_INT(0, "force-remove", &force_remove,
			"remove named paths even if present in worktree", 1),
		OPT_SET_INT('z', NULL, &line_termination,
			"with --stdin: input lines are terminated by null bytes", '\0'),
		{OPTION_LOWLEVEL_CALLBACK, 0, "stdin", &read_from_stdin, NULL,
			"read list of paths to be updated from standard input",
			PARSE_OPT_NONEG | PARSE_OPT_NOARG,
			(parse_opt_cb *) stdin_callback},
		{OPTION_LOWLEVEL_CALLBACK, 0, "index-info", &line_termination, NULL,
			"add entries from standard input to the index",
			PARSE_OPT_NONEG | PARSE_OPT_NOARG,
			(parse_opt_cb *) stdin_cacheinfo_callback},
		{OPTION_LOWLEVEL_CALLBACK, 0, "unresolve", &has_errors, NULL,
			"repopulate stages #2 and #3 for the listed paths",
			PARSE_OPT_NONEG | PARSE_OPT_NOARG,
			(parse_opt_cb *) unresolve_callback},
		{OPTION_LOWLEVEL_CALLBACK, 'g', "again", &has_errors, NULL,
			"only update entries that differ from HEAD",
			PARSE_OPT_NONEG | PARSE_OPT_NOARG,
			(parse_opt_cb *) reupdate_callback},
		OPT_BIT(0, "ignore-missing", &refresh_args.flags,
			"ignore files missing from worktree",
			REFRESH_IGNORE_MISSING),
		OPT_SET_INT(0, "verbose", &verbose,
			"report actions to standard output", 1),
		{OPTION_CALLBACK, 0, "clear-resolve-undo", NULL, NULL,
			"(for porcelains) forget saved unresolved conflicts",
			PARSE_OPT_NOARG | PARSE_OPT_NONEG,
			resolve_undo_clear_callback},
		OPT_END()
	};

	if (argc == 2 && !strcmp(argv[1], "-h"))
		usage(update_index_usage[0]);

	git_config(git_default_config, NULL);

	/* We can't free this memory, it becomes part of a linked list parsed atexit() */
	lock_file = xcalloc(1, sizeof(struct lock_file));

	newfd = hold_locked_index(lock_file, 0);
	if (newfd < 0)
		lock_error = errno;

	entries = read_cache();
	if (entries < 0)
		die("cache corrupted");

	/*
	 * Custom copy of parse_options() because we want to handle
	 * filename arguments as they come.
	 */
	parse_options_start(&ctx, argc, argv, prefix,
			    options, PARSE_OPT_STOP_AT_NON_OPTION);
	while (ctx.argc) {
		if (parseopt_state != PARSE_OPT_DONE)
			parseopt_state = parse_options_step(&ctx, options,
							    update_index_usage);
		if (!ctx.argc)
			break;
		switch (parseopt_state) {
		case PARSE_OPT_HELP:
			exit(129);
		case PARSE_OPT_NON_OPTION:
		case PARSE_OPT_DONE:
		{
			const char *path = ctx.argv[0];
			const char *p;

			setup_work_tree();
			p = prefix_path(prefix, prefix_length, path);
			update_one(p, NULL, 0);
			if (set_executable_bit)
				chmod_path(set_executable_bit, p);
			if (p < path || p > path + strlen(path))
				free((char *)p);
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

	if (read_from_stdin) {
		struct strbuf buf = STRBUF_INIT, nbuf = STRBUF_INIT;

		setup_work_tree();
		while (strbuf_getline(&buf, stdin, line_termination) != EOF) {
			const char *p;
			if (line_termination && buf.buf[0] == '"') {
				strbuf_reset(&nbuf);
				if (unquote_c_style(&nbuf, buf.buf, NULL))
					die("line is badly quoted");
				strbuf_swap(&buf, &nbuf);
			}
			p = prefix_path(prefix, prefix_length, buf.buf);
			update_one(p, NULL, 0);
			if (set_executable_bit)
				chmod_path(set_executable_bit, p);
			if (p < buf.buf || p > buf.buf + buf.len)
				free((char *)p);
		}
		strbuf_release(&nbuf);
		strbuf_release(&buf);
	}

	if (active_cache_changed) {
		if (newfd < 0) {
			if (refresh_args.flags & REFRESH_QUIET)
				exit(128);
			unable_to_lock_index_die(get_index_file(), lock_error);
		}
		if (write_cache(newfd, active_cache, active_nr) ||
		    commit_locked_index(lock_file))
			die("Unable to write new index file");
	}

	rollback_lock_file(lock_file);

	return has_errors ? 1 : 0;
}
