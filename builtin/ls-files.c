/*
 * This merges the file listing in the directory cache index
 * with the actual working directory list, and shows different
 * combinations of the two.
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#include "cache.h"
#include "repository.h"
#include "config.h"
#include "quote.h"
#include "dir.h"
#include "builtin.h"
#include "tree.h"
#include "parse-options.h"
#include "resolve-undo.h"
#include "string-list.h"
#include "pathspec.h"
#include "run-command.h"
#include "submodule.h"
#include "submodule-config.h"

static int abbrev;
static int show_deleted;
static int show_cached;
static int show_others;
static int show_stage;
static int show_unmerged;
static int show_resolve_undo;
static int show_modified;
static int show_killed;
static int show_valid_bit;
static int show_fsmonitor_bit;
static int line_terminator = '\n';
static int debug_mode;
static int show_eol;
static int recurse_submodules;

static const char *prefix;
static int max_prefix_len;
static int prefix_len;
static struct pathspec pathspec;
static int error_unmatch;
static char *ps_matched;
static const char *with_tree;
static int exc_given;
static int exclude_args;

static const char *tag_cached = "";
static const char *tag_unmerged = "";
static const char *tag_removed = "";
static const char *tag_other = "";
static const char *tag_killed = "";
static const char *tag_modified = "";
static const char *tag_skip_worktree = "";
static const char *tag_resolve_undo = "";

static void write_eolinfo(const struct index_state *istate,
			  const struct cache_entry *ce, const char *path)
{
	if (show_eol) {
		struct stat st;
		const char *i_txt = "";
		const char *w_txt = "";
		const char *a_txt = get_convert_attr_ascii(istate, path);
		if (ce && S_ISREG(ce->ce_mode))
			i_txt = get_cached_convert_stats_ascii(istate,
							       ce->name);
		if (!lstat(path, &st) && S_ISREG(st.st_mode))
			w_txt = get_wt_convert_stats_ascii(path);
		printf("i/%-5s w/%-5s attr/%-17s\t", i_txt, w_txt, a_txt);
	}
}

static void write_name(const char *name)
{
	/*
	 * With "--full-name", prefix_len=0; this caller needs to pass
	 * an empty string in that case (a NULL is good for "").
	 */
	write_name_quoted_relative(name, prefix_len ? prefix : NULL,
				   stdout, line_terminator);
}

static const char *get_tag(const struct cache_entry *ce, const char *tag)
{
	static char alttag[4];

	if (tag && *tag && ((show_valid_bit && (ce->ce_flags & CE_VALID)) ||
		(show_fsmonitor_bit && (ce->ce_flags & CE_FSMONITOR_VALID)))) {
		memcpy(alttag, tag, 3);

		if (isalpha(tag[0])) {
			alttag[0] = tolower(tag[0]);
		} else if (tag[0] == '?') {
			alttag[0] = '!';
		} else {
			alttag[0] = 'v';
			alttag[1] = tag[0];
			alttag[2] = ' ';
			alttag[3] = 0;
		}

		tag = alttag;
	}

	return tag;
}

static void print_debug(const struct cache_entry *ce)
{
	if (debug_mode) {
		const struct stat_data *sd = &ce->ce_stat_data;

		printf("  ctime: %u:%u\n", sd->sd_ctime.sec, sd->sd_ctime.nsec);
		printf("  mtime: %u:%u\n", sd->sd_mtime.sec, sd->sd_mtime.nsec);
		printf("  dev: %u\tino: %u\n", sd->sd_dev, sd->sd_ino);
		printf("  uid: %u\tgid: %u\n", sd->sd_uid, sd->sd_gid);
		printf("  size: %u\tflags: %x\n", sd->sd_size, ce->ce_flags);
	}
}

static void show_dir_entry(const struct index_state *istate,
			   const char *tag, struct dir_entry *ent)
{
	int len = max_prefix_len;

	if (len > ent->len)
		die("git ls-files: internal error - directory entry not superset of prefix");

	if (!dir_path_match(istate, ent, &pathspec, len, ps_matched))
		return;

	fputs(tag, stdout);
	write_eolinfo(istate, NULL, ent->name);
	write_name(ent->name);
}

static void show_other_files(const struct index_state *istate,
			     const struct dir_struct *dir)
{
	int i;

	for (i = 0; i < dir->nr; i++) {
		struct dir_entry *ent = dir->entries[i];
		if (!index_name_is_other(istate, ent->name, ent->len))
			continue;
		show_dir_entry(istate, tag_other, ent);
	}
}

static void show_killed_files(const struct index_state *istate,
			      const struct dir_struct *dir)
{
	int i;
	for (i = 0; i < dir->nr; i++) {
		struct dir_entry *ent = dir->entries[i];
		char *cp, *sp;
		int pos, len, killed = 0;

		for (cp = ent->name; cp - ent->name < ent->len; cp = sp + 1) {
			sp = strchr(cp, '/');
			if (!sp) {
				/* If ent->name is prefix of an entry in the
				 * cache, it will be killed.
				 */
				pos = index_name_pos(istate, ent->name, ent->len);
				if (0 <= pos)
					BUG("killed-file %.*s not found",
						ent->len, ent->name);
				pos = -pos - 1;
				while (pos < istate->cache_nr &&
				       ce_stage(istate->cache[pos]))
					pos++; /* skip unmerged */
				if (istate->cache_nr <= pos)
					break;
				/* pos points at a name immediately after
				 * ent->name in the cache.  Does it expect
				 * ent->name to be a directory?
				 */
				len = ce_namelen(istate->cache[pos]);
				if ((ent->len < len) &&
				    !strncmp(istate->cache[pos]->name,
					     ent->name, ent->len) &&
				    istate->cache[pos]->name[ent->len] == '/')
					killed = 1;
				break;
			}
			if (0 <= index_name_pos(istate, ent->name, sp - ent->name)) {
				/* If any of the leading directories in
				 * ent->name is registered in the cache,
				 * ent->name will be killed.
				 */
				killed = 1;
				break;
			}
		}
		if (killed)
			show_dir_entry(istate, tag_killed, dir->entries[i]);
	}
}

static void show_files(struct repository *repo, struct dir_struct *dir);

static void show_submodule(struct repository *superproject,
			   struct dir_struct *dir, const char *path)
{
	struct repository subrepo;
	const struct submodule *sub = submodule_from_path(superproject,
							  &null_oid, path);

	if (repo_submodule_init(&subrepo, superproject, sub))
		return;

	if (repo_read_index(&subrepo) < 0)
		die("index file corrupt");

	show_files(&subrepo, dir);

	repo_clear(&subrepo);
}

static void show_ce(struct repository *repo, struct dir_struct *dir,
		    const struct cache_entry *ce, const char *fullname,
		    const char *tag)
{
	if (max_prefix_len > strlen(fullname))
		die("git ls-files: internal error - cache entry not superset of prefix");

	if (recurse_submodules && S_ISGITLINK(ce->ce_mode) &&
	    is_submodule_active(repo, ce->name)) {
		show_submodule(repo, dir, ce->name);
	} else if (match_pathspec(repo->index, &pathspec, fullname, strlen(fullname),
				  max_prefix_len, ps_matched,
				  S_ISDIR(ce->ce_mode) ||
				  S_ISGITLINK(ce->ce_mode))) {
		tag = get_tag(ce, tag);

		if (!show_stage) {
			fputs(tag, stdout);
		} else {
			printf("%s%06o %s %d\t",
			       tag,
			       ce->ce_mode,
			       find_unique_abbrev(&ce->oid, abbrev),
			       ce_stage(ce));
		}
		write_eolinfo(repo->index, ce, fullname);
		write_name(fullname);
		print_debug(ce);
	}
}

static void show_ru_info(const struct index_state *istate)
{
	struct string_list_item *item;

	if (!istate->resolve_undo)
		return;

	for_each_string_list_item(item, istate->resolve_undo) {
		const char *path = item->string;
		struct resolve_undo_info *ui = item->util;
		int i, len;

		len = strlen(path);
		if (len < max_prefix_len)
			continue; /* outside of the prefix */
		if (!match_pathspec(istate, &pathspec, path, len,
				    max_prefix_len, ps_matched, 0))
			continue; /* uninterested */
		for (i = 0; i < 3; i++) {
			if (!ui->mode[i])
				continue;
			printf("%s%06o %s %d\t", tag_resolve_undo, ui->mode[i],
			       find_unique_abbrev(&ui->oid[i], abbrev),
			       i + 1);
			write_name(path);
		}
	}
}

static int ce_excluded(struct dir_struct *dir, struct index_state *istate,
		       const char *fullname, const struct cache_entry *ce)
{
	int dtype = ce_to_dtype(ce);
	return is_excluded(dir, istate, fullname, &dtype);
}

static void construct_fullname(struct strbuf *out, const struct repository *repo,
			       const struct cache_entry *ce)
{
	strbuf_reset(out);
	if (repo->submodule_prefix)
		strbuf_addstr(out, repo->submodule_prefix);
	strbuf_addstr(out, ce->name);
}

static void show_files(struct repository *repo, struct dir_struct *dir)
{
	int i;
	struct strbuf fullname = STRBUF_INIT;

	/* For cached/deleted files we don't need to even do the readdir */
	if (show_others || show_killed) {
		if (!show_others)
			dir->flags |= DIR_COLLECT_KILLED_ONLY;
		fill_directory(dir, repo->index, &pathspec);
		if (show_others)
			show_other_files(repo->index, dir);
		if (show_killed)
			show_killed_files(repo->index, dir);
	}
	if (show_cached || show_stage) {
		for (i = 0; i < repo->index->cache_nr; i++) {
			const struct cache_entry *ce = repo->index->cache[i];

			construct_fullname(&fullname, repo, ce);

			if ((dir->flags & DIR_SHOW_IGNORED) &&
			    !ce_excluded(dir, repo->index, fullname.buf, ce))
				continue;
			if (show_unmerged && !ce_stage(ce))
				continue;
			if (ce->ce_flags & CE_UPDATE)
				continue;
			show_ce(repo, dir, ce, fullname.buf,
				ce_stage(ce) ? tag_unmerged :
				(ce_skip_worktree(ce) ? tag_skip_worktree :
				 tag_cached));
		}
	}
	if (show_deleted || show_modified) {
		for (i = 0; i < repo->index->cache_nr; i++) {
			const struct cache_entry *ce = repo->index->cache[i];
			struct stat st;
			int err;

			construct_fullname(&fullname, repo, ce);

			if ((dir->flags & DIR_SHOW_IGNORED) &&
			    !ce_excluded(dir, repo->index, fullname.buf, ce))
				continue;
			if (ce->ce_flags & CE_UPDATE)
				continue;
			if (ce_skip_worktree(ce))
				continue;
			err = lstat(fullname.buf, &st);
			if (show_deleted && err)
				show_ce(repo, dir, ce, fullname.buf, tag_removed);
			if (show_modified && ie_modified(repo->index, ce, &st, 0))
				show_ce(repo, dir, ce, fullname.buf, tag_modified);
		}
	}

	strbuf_release(&fullname);
}

/*
 * Prune the index to only contain stuff starting with "prefix"
 */
static void prune_index(struct index_state *istate,
			const char *prefix, size_t prefixlen)
{
	int pos;
	unsigned int first, last;

	if (!prefix || !istate->cache_nr)
		return;
	pos = index_name_pos(istate, prefix, prefixlen);
	if (pos < 0)
		pos = -pos-1;
	first = pos;
	last = istate->cache_nr;
	while (last > first) {
		int next = first + ((last - first) >> 1);
		const struct cache_entry *ce = istate->cache[next];
		if (!strncmp(ce->name, prefix, prefixlen)) {
			first = next+1;
			continue;
		}
		last = next;
	}
	MOVE_ARRAY(istate->cache, istate->cache + pos, last - pos);
	istate->cache_nr = last - pos;
}

static int get_common_prefix_len(const char *common_prefix)
{
	int common_prefix_len;

	if (!common_prefix)
		return 0;

	common_prefix_len = strlen(common_prefix);

	/*
	 * If the prefix has a trailing slash, strip it so that submodules wont
	 * be pruned from the index.
	 */
	if (common_prefix[common_prefix_len - 1] == '/')
		common_prefix_len--;

	return common_prefix_len;
}

/*
 * Read the tree specified with --with-tree option
 * (typically, HEAD) into stage #1 and then
 * squash them down to stage #0.  This is used for
 * --error-unmatch to list and check the path patterns
 * that were given from the command line.  We are not
 * going to write this index out.
 */
void overlay_tree_on_index(struct index_state *istate,
			   const char *tree_name, const char *prefix)
{
	struct tree *tree;
	struct object_id oid;
	struct pathspec pathspec;
	struct cache_entry *last_stage0 = NULL;
	int i;

	if (get_oid(tree_name, &oid))
		die("tree-ish %s not found.", tree_name);
	tree = parse_tree_indirect(&oid);
	if (!tree)
		die("bad tree-ish %s", tree_name);

	/* Hoist the unmerged entries up to stage #3 to make room */
	for (i = 0; i < istate->cache_nr; i++) {
		struct cache_entry *ce = istate->cache[i];
		if (!ce_stage(ce))
			continue;
		ce->ce_flags |= CE_STAGEMASK;
	}

	if (prefix) {
		static const char *(matchbuf[1]);
		matchbuf[0] = NULL;
		parse_pathspec(&pathspec, PATHSPEC_ALL_MAGIC,
			       PATHSPEC_PREFER_CWD, prefix, matchbuf);
	} else
		memset(&pathspec, 0, sizeof(pathspec));
	if (read_tree(the_repository, tree, 1, &pathspec, istate))
		die("unable to read tree entries %s", tree_name);

	for (i = 0; i < istate->cache_nr; i++) {
		struct cache_entry *ce = istate->cache[i];
		switch (ce_stage(ce)) {
		case 0:
			last_stage0 = ce;
			/* fallthru */
		default:
			continue;
		case 1:
			/*
			 * If there is stage #0 entry for this, we do not
			 * need to show it.  We use CE_UPDATE bit to mark
			 * such an entry.
			 */
			if (last_stage0 &&
			    !strcmp(last_stage0->name, ce->name))
				ce->ce_flags |= CE_UPDATE;
		}
	}
}

static const char * const ls_files_usage[] = {
	N_("git ls-files [<options>] [<file>...]"),
	NULL
};

static int option_parse_exclude(const struct option *opt,
				const char *arg, int unset)
{
	struct string_list *exclude_list = opt->value;

	BUG_ON_OPT_NEG(unset);

	exc_given = 1;
	string_list_append(exclude_list, arg);

	return 0;
}

static int option_parse_exclude_from(const struct option *opt,
				     const char *arg, int unset)
{
	struct dir_struct *dir = opt->value;

	BUG_ON_OPT_NEG(unset);

	exc_given = 1;
	add_excludes_from_file(dir, arg);

	return 0;
}

static int option_parse_exclude_standard(const struct option *opt,
					 const char *arg, int unset)
{
	struct dir_struct *dir = opt->value;

	BUG_ON_OPT_NEG(unset);
	BUG_ON_OPT_ARG(arg);

	exc_given = 1;
	setup_standard_excludes(dir);

	return 0;
}

int cmd_ls_files(int argc, const char **argv, const char *cmd_prefix)
{
	int require_work_tree = 0, show_tag = 0, i;
	const char *max_prefix;
	struct dir_struct dir;
	struct exclude_list *el;
	struct string_list exclude_list = STRING_LIST_INIT_NODUP;
	struct option builtin_ls_files_options[] = {
		/* Think twice before adding "--nul" synonym to this */
		OPT_SET_INT('z', NULL, &line_terminator,
			N_("paths are separated with NUL character"), '\0'),
		OPT_BOOL('t', NULL, &show_tag,
			N_("identify the file status with tags")),
		OPT_BOOL('v', NULL, &show_valid_bit,
			N_("use lowercase letters for 'assume unchanged' files")),
		OPT_BOOL('f', NULL, &show_fsmonitor_bit,
			N_("use lowercase letters for 'fsmonitor clean' files")),
		OPT_BOOL('c', "cached", &show_cached,
			N_("show cached files in the output (default)")),
		OPT_BOOL('d', "deleted", &show_deleted,
			N_("show deleted files in the output")),
		OPT_BOOL('m', "modified", &show_modified,
			N_("show modified files in the output")),
		OPT_BOOL('o', "others", &show_others,
			N_("show other files in the output")),
		OPT_BIT('i', "ignored", &dir.flags,
			N_("show ignored files in the output"),
			DIR_SHOW_IGNORED),
		OPT_BOOL('s', "stage", &show_stage,
			N_("show staged contents' object name in the output")),
		OPT_BOOL('k', "killed", &show_killed,
			N_("show files on the filesystem that need to be removed")),
		OPT_BIT(0, "directory", &dir.flags,
			N_("show 'other' directories' names only"),
			DIR_SHOW_OTHER_DIRECTORIES),
		OPT_BOOL(0, "eol", &show_eol, N_("show line endings of files")),
		OPT_NEGBIT(0, "empty-directory", &dir.flags,
			N_("don't show empty directories"),
			DIR_HIDE_EMPTY_DIRECTORIES),
		OPT_BOOL('u', "unmerged", &show_unmerged,
			N_("show unmerged files in the output")),
		OPT_BOOL(0, "resolve-undo", &show_resolve_undo,
			    N_("show resolve-undo information")),
		{ OPTION_CALLBACK, 'x', "exclude", &exclude_list, N_("pattern"),
			N_("skip files matching pattern"),
			PARSE_OPT_NONEG, option_parse_exclude },
		{ OPTION_CALLBACK, 'X', "exclude-from", &dir, N_("file"),
			N_("exclude patterns are read from <file>"),
			PARSE_OPT_NONEG, option_parse_exclude_from },
		OPT_STRING(0, "exclude-per-directory", &dir.exclude_per_dir, N_("file"),
			N_("read additional per-directory exclude patterns in <file>")),
		{ OPTION_CALLBACK, 0, "exclude-standard", &dir, NULL,
			N_("add the standard git exclusions"),
			PARSE_OPT_NOARG | PARSE_OPT_NONEG,
			option_parse_exclude_standard },
		OPT_SET_INT_F(0, "full-name", &prefix_len,
			      N_("make the output relative to the project top directory"),
			      0, PARSE_OPT_NONEG),
		OPT_BOOL(0, "recurse-submodules", &recurse_submodules,
			N_("recurse through submodules")),
		OPT_BOOL(0, "error-unmatch", &error_unmatch,
			N_("if any <file> is not in the index, treat this as an error")),
		OPT_STRING(0, "with-tree", &with_tree, N_("tree-ish"),
			N_("pretend that paths removed since <tree-ish> are still present")),
		OPT__ABBREV(&abbrev),
		OPT_BOOL(0, "debug", &debug_mode, N_("show debugging data")),
		OPT_END()
	};

	if (argc == 2 && !strcmp(argv[1], "-h"))
		usage_with_options(ls_files_usage, builtin_ls_files_options);

	memset(&dir, 0, sizeof(dir));
	prefix = cmd_prefix;
	if (prefix)
		prefix_len = strlen(prefix);
	git_config(git_default_config, NULL);

	if (repo_read_index(the_repository) < 0)
		die("index file corrupt");

	argc = parse_options(argc, argv, prefix, builtin_ls_files_options,
			ls_files_usage, 0);
	el = add_exclude_list(&dir, EXC_CMDL, "--exclude option");
	for (i = 0; i < exclude_list.nr; i++) {
		add_exclude(exclude_list.items[i].string, "", 0, el, --exclude_args);
	}
	if (show_tag || show_valid_bit || show_fsmonitor_bit) {
		tag_cached = "H ";
		tag_unmerged = "M ";
		tag_removed = "R ";
		tag_modified = "C ";
		tag_other = "? ";
		tag_killed = "K ";
		tag_skip_worktree = "S ";
		tag_resolve_undo = "U ";
	}
	if (show_modified || show_others || show_deleted || (dir.flags & DIR_SHOW_IGNORED) || show_killed)
		require_work_tree = 1;
	if (show_unmerged)
		/*
		 * There's no point in showing unmerged unless
		 * you also show the stage information.
		 */
		show_stage = 1;
	if (dir.exclude_per_dir)
		exc_given = 1;

	if (require_work_tree && !is_inside_work_tree())
		setup_work_tree();

	if (recurse_submodules &&
	    (show_stage || show_deleted || show_others || show_unmerged ||
	     show_killed || show_modified || show_resolve_undo || with_tree))
		die("ls-files --recurse-submodules unsupported mode");

	if (recurse_submodules && error_unmatch)
		die("ls-files --recurse-submodules does not support "
		    "--error-unmatch");

	parse_pathspec(&pathspec, 0,
		       PATHSPEC_PREFER_CWD,
		       prefix, argv);

	/*
	 * Find common prefix for all pathspec's
	 * This is used as a performance optimization which unfortunately cannot
	 * be done when recursing into submodules because when a pathspec is
	 * given which spans repository boundaries you can't simply remove the
	 * submodule entry because the pathspec may match something inside the
	 * submodule.
	 */
	if (recurse_submodules)
		max_prefix = NULL;
	else
		max_prefix = common_prefix(&pathspec);
	max_prefix_len = get_common_prefix_len(max_prefix);

	prune_index(the_repository->index, max_prefix, max_prefix_len);

	/* Treat unmatching pathspec elements as errors */
	if (pathspec.nr && error_unmatch)
		ps_matched = xcalloc(pathspec.nr, 1);

	if ((dir.flags & DIR_SHOW_IGNORED) && !exc_given)
		die("ls-files --ignored needs some exclude pattern");

	/* With no flags, we default to showing the cached files */
	if (!(show_stage || show_deleted || show_others || show_unmerged ||
	      show_killed || show_modified || show_resolve_undo))
		show_cached = 1;

	if (with_tree) {
		/*
		 * Basic sanity check; show-stages and show-unmerged
		 * would not make any sense with this option.
		 */
		if (show_stage || show_unmerged)
			die("ls-files --with-tree is incompatible with -s or -u");
		overlay_tree_on_index(the_repository->index, with_tree, max_prefix);
	}

	show_files(the_repository, &dir);

	if (show_resolve_undo)
		show_ru_info(the_repository->index);

	if (ps_matched) {
		int bad;
		bad = report_path_error(ps_matched, &pathspec);
		if (bad)
			fprintf(stderr, "Did you forget to 'git add'?\n");

		return bad ? 1 : 0;
	}

	UNLEAK(dir);
	return 0;
}
