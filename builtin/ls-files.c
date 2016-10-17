/*
 * This merges the file listing in the directory cache index
 * with the actual working directory list, and shows different
 * combinations of the two.
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#include "cache.h"
#include "quote.h"
#include "dir.h"
#include "builtin.h"
#include "tree.h"
#include "parse-options.h"
#include "resolve-undo.h"
#include "string-list.h"
#include "pathspec.h"
#include "run-command.h"

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
static int line_terminator = '\n';
static int debug_mode;
static int show_eol;
static int recurse_submodules;
static struct argv_array submodules_options = ARGV_ARRAY_INIT;

static const char *prefix;
static const char *super_prefix;
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

static void write_eolinfo(const struct cache_entry *ce, const char *path)
{
	if (!show_eol)
		return;
	else {
		struct stat st;
		const char *i_txt = "";
		const char *w_txt = "";
		const char *a_txt = get_convert_attr_ascii(path);
		if (ce && S_ISREG(ce->ce_mode))
			i_txt = get_cached_convert_stats_ascii(ce->name);
		if (!lstat(path, &st) && S_ISREG(st.st_mode))
			w_txt = get_wt_convert_stats_ascii(path);
		printf("i/%-5s w/%-5s attr/%-17s\t", i_txt, w_txt, a_txt);
	}
}

static void write_name(const char *name)
{
	/*
	 * Prepend the super_prefix to name to construct the full_name to be
	 * written.
	 */
	struct strbuf full_name = STRBUF_INIT;
	if (super_prefix) {
		strbuf_addstr(&full_name, super_prefix);
		strbuf_addstr(&full_name, name);
		name = full_name.buf;
	}

	/*
	 * With "--full-name", prefix_len=0; this caller needs to pass
	 * an empty string in that case (a NULL is good for "").
	 */
	write_name_quoted_relative(name, prefix_len ? prefix : NULL,
				   stdout, line_terminator);

	strbuf_release(&full_name);
}

static void show_dir_entry(const char *tag, struct dir_entry *ent)
{
	int len = max_prefix_len;

	if (len >= ent->len)
		die("git ls-files: internal error - directory entry not superset of prefix");

	if (!dir_path_match(ent, &pathspec, len, ps_matched))
		return;

	fputs(tag, stdout);
	write_eolinfo(NULL, ent->name);
	write_name(ent->name);
}

static void show_other_files(struct dir_struct *dir)
{
	int i;

	for (i = 0; i < dir->nr; i++) {
		struct dir_entry *ent = dir->entries[i];
		if (!cache_name_is_other(ent->name, ent->len))
			continue;
		show_dir_entry(tag_other, ent);
	}
}

static void show_killed_files(struct dir_struct *dir)
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
				pos = cache_name_pos(ent->name, ent->len);
				if (0 <= pos)
					die("BUG: killed-file %.*s not found",
						ent->len, ent->name);
				pos = -pos - 1;
				while (pos < active_nr &&
				       ce_stage(active_cache[pos]))
					pos++; /* skip unmerged */
				if (active_nr <= pos)
					break;
				/* pos points at a name immediately after
				 * ent->name in the cache.  Does it expect
				 * ent->name to be a directory?
				 */
				len = ce_namelen(active_cache[pos]);
				if ((ent->len < len) &&
				    !strncmp(active_cache[pos]->name,
					     ent->name, ent->len) &&
				    active_cache[pos]->name[ent->len] == '/')
					killed = 1;
				break;
			}
			if (0 <= cache_name_pos(ent->name, sp - ent->name)) {
				/* If any of the leading directories in
				 * ent->name is registered in the cache,
				 * ent->name will be killed.
				 */
				killed = 1;
				break;
			}
		}
		if (killed)
			show_dir_entry(tag_killed, dir->entries[i]);
	}
}

/*
 * Compile an argv_array with all of the options supported by --recurse_submodules
 */
static void compile_submodule_options(const struct dir_struct *dir, int show_tag)
{
	if (line_terminator == '\0')
		argv_array_push(&submodules_options, "-z");
	if (show_tag)
		argv_array_push(&submodules_options, "-t");
	if (show_valid_bit)
		argv_array_push(&submodules_options, "-v");
	if (show_cached)
		argv_array_push(&submodules_options, "--cached");
	if (show_eol)
		argv_array_push(&submodules_options, "--eol");
	if (debug_mode)
		argv_array_push(&submodules_options, "--debug");
}

/**
 * Recursively call ls-files on a submodule
 */
static void show_gitlink(const struct cache_entry *ce)
{
	struct child_process cp = CHILD_PROCESS_INIT;
	int status;
	int i;

	argv_array_pushf(&cp.args, "--super-prefix=%s%s/",
			 super_prefix ? super_prefix : "",
			 ce->name);
	argv_array_push(&cp.args, "ls-files");
	argv_array_push(&cp.args, "--recurse-submodules");

	/* add supported options */
	argv_array_pushv(&cp.args, submodules_options.argv);

	/*
	 * Pass in the original pathspec args.  The submodule will be
	 * responsible for prepending the 'submodule_prefix' prior to comparing
	 * against the pathspec for matches.
	 */
	argv_array_push(&cp.args, "--");
	for (i = 0; i < pathspec.nr; i++)
		argv_array_push(&cp.args, pathspec.items[i].original);

	cp.git_cmd = 1;
	cp.dir = ce->name;
	status = run_command(&cp);
	if (status)
		exit(status);
}

static void show_ce_entry(const char *tag, const struct cache_entry *ce)
{
	struct strbuf name = STRBUF_INIT;
	int len = max_prefix_len;
	if (super_prefix)
		strbuf_addstr(&name, super_prefix);
	strbuf_addstr(&name, ce->name);

	if (len >= ce_namelen(ce))
		die("git ls-files: internal error - cache entry not superset of prefix");

	if (recurse_submodules && S_ISGITLINK(ce->ce_mode) &&
	    submodule_path_match(&pathspec, name.buf, ps_matched)) {
		show_gitlink(ce);
	} else if (match_pathspec(&pathspec, name.buf, name.len,
				  len, ps_matched,
				  S_ISDIR(ce->ce_mode) ||
				  S_ISGITLINK(ce->ce_mode))) {
		if (tag && *tag && show_valid_bit &&
		    (ce->ce_flags & CE_VALID)) {
			static char alttag[4];
			memcpy(alttag, tag, 3);
			if (isalpha(tag[0]))
				alttag[0] = tolower(tag[0]);
			else if (tag[0] == '?')
				alttag[0] = '!';
			else {
				alttag[0] = 'v';
				alttag[1] = tag[0];
				alttag[2] = ' ';
				alttag[3] = 0;
			}
			tag = alttag;
		}

		if (!show_stage) {
			fputs(tag, stdout);
		} else {
			printf("%s%06o %s %d\t",
			       tag,
			       ce->ce_mode,
			       find_unique_abbrev(ce->oid.hash, abbrev),
			       ce_stage(ce));
		}
		write_eolinfo(ce, ce->name);
		write_name(ce->name);
		if (debug_mode) {
			const struct stat_data *sd = &ce->ce_stat_data;

			printf("  ctime: %d:%d\n", sd->sd_ctime.sec, sd->sd_ctime.nsec);
			printf("  mtime: %d:%d\n", sd->sd_mtime.sec, sd->sd_mtime.nsec);
			printf("  dev: %d\tino: %d\n", sd->sd_dev, sd->sd_ino);
			printf("  uid: %d\tgid: %d\n", sd->sd_uid, sd->sd_gid);
			printf("  size: %d\tflags: %x\n", sd->sd_size, ce->ce_flags);
		}
	}

	strbuf_release(&name);
}

static void show_ru_info(void)
{
	struct string_list_item *item;

	if (!the_index.resolve_undo)
		return;

	for_each_string_list_item(item, the_index.resolve_undo) {
		const char *path = item->string;
		struct resolve_undo_info *ui = item->util;
		int i, len;

		len = strlen(path);
		if (len < max_prefix_len)
			continue; /* outside of the prefix */
		if (!match_pathspec(&pathspec, path, len,
				    max_prefix_len, ps_matched, 0))
			continue; /* uninterested */
		for (i = 0; i < 3; i++) {
			if (!ui->mode[i])
				continue;
			printf("%s%06o %s %d\t", tag_resolve_undo, ui->mode[i],
			       find_unique_abbrev(ui->sha1[i], abbrev),
			       i + 1);
			write_name(path);
		}
	}
}

static int ce_excluded(struct dir_struct *dir, const struct cache_entry *ce)
{
	int dtype = ce_to_dtype(ce);
	return is_excluded(dir, ce->name, &dtype);
}

static void show_files(struct dir_struct *dir)
{
	int i;

	/* For cached/deleted files we don't need to even do the readdir */
	if (show_others || show_killed) {
		if (!show_others)
			dir->flags |= DIR_COLLECT_KILLED_ONLY;
		fill_directory(dir, &pathspec);
		if (show_others)
			show_other_files(dir);
		if (show_killed)
			show_killed_files(dir);
	}
	if (show_cached || show_stage) {
		for (i = 0; i < active_nr; i++) {
			const struct cache_entry *ce = active_cache[i];
			if ((dir->flags & DIR_SHOW_IGNORED) &&
			    !ce_excluded(dir, ce))
				continue;
			if (show_unmerged && !ce_stage(ce))
				continue;
			if (ce->ce_flags & CE_UPDATE)
				continue;
			show_ce_entry(ce_stage(ce) ? tag_unmerged :
				(ce_skip_worktree(ce) ? tag_skip_worktree : tag_cached), ce);
		}
	}
	if (show_deleted || show_modified) {
		for (i = 0; i < active_nr; i++) {
			const struct cache_entry *ce = active_cache[i];
			struct stat st;
			int err;
			if ((dir->flags & DIR_SHOW_IGNORED) &&
			    !ce_excluded(dir, ce))
				continue;
			if (ce->ce_flags & CE_UPDATE)
				continue;
			if (ce_skip_worktree(ce))
				continue;
			err = lstat(ce->name, &st);
			if (show_deleted && err)
				show_ce_entry(tag_removed, ce);
			if (show_modified && ce_modified(ce, &st, 0))
				show_ce_entry(tag_modified, ce);
		}
	}
}

/*
 * Prune the index to only contain stuff starting with "prefix"
 */
static void prune_cache(const char *prefix)
{
	int pos = cache_name_pos(prefix, max_prefix_len);
	unsigned int first, last;

	if (pos < 0)
		pos = -pos-1;
	memmove(active_cache, active_cache + pos,
		(active_nr - pos) * sizeof(struct cache_entry *));
	active_nr -= pos;
	first = 0;
	last = active_nr;
	while (last > first) {
		int next = (last + first) >> 1;
		const struct cache_entry *ce = active_cache[next];
		if (!strncmp(ce->name, prefix, max_prefix_len)) {
			first = next+1;
			continue;
		}
		last = next;
	}
	active_nr = last;
}

/*
 * Read the tree specified with --with-tree option
 * (typically, HEAD) into stage #1 and then
 * squash them down to stage #0.  This is used for
 * --error-unmatch to list and check the path patterns
 * that were given from the command line.  We are not
 * going to write this index out.
 */
void overlay_tree_on_cache(const char *tree_name, const char *prefix)
{
	struct tree *tree;
	unsigned char sha1[20];
	struct pathspec pathspec;
	struct cache_entry *last_stage0 = NULL;
	int i;

	if (get_sha1(tree_name, sha1))
		die("tree-ish %s not found.", tree_name);
	tree = parse_tree_indirect(sha1);
	if (!tree)
		die("bad tree-ish %s", tree_name);

	/* Hoist the unmerged entries up to stage #3 to make room */
	for (i = 0; i < active_nr; i++) {
		struct cache_entry *ce = active_cache[i];
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
	if (read_tree(tree, 1, &pathspec))
		die("unable to read tree entries %s", tree_name);

	for (i = 0; i < active_nr; i++) {
		struct cache_entry *ce = active_cache[i];
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

	exc_given = 1;
	string_list_append(exclude_list, arg);

	return 0;
}

static int option_parse_exclude_from(const struct option *opt,
				     const char *arg, int unset)
{
	struct dir_struct *dir = opt->value;

	exc_given = 1;
	add_excludes_from_file(dir, arg);

	return 0;
}

static int option_parse_exclude_standard(const struct option *opt,
					 const char *arg, int unset)
{
	struct dir_struct *dir = opt->value;

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
			0, option_parse_exclude },
		{ OPTION_CALLBACK, 'X', "exclude-from", &dir, N_("file"),
			N_("exclude patterns are read from <file>"),
			0, option_parse_exclude_from },
		OPT_STRING(0, "exclude-per-directory", &dir.exclude_per_dir, N_("file"),
			N_("read additional per-directory exclude patterns in <file>")),
		{ OPTION_CALLBACK, 0, "exclude-standard", &dir, NULL,
			N_("add the standard git exclusions"),
			PARSE_OPT_NOARG, option_parse_exclude_standard },
		{ OPTION_SET_INT, 0, "full-name", &prefix_len, NULL,
			N_("make the output relative to the project top directory"),
			PARSE_OPT_NOARG | PARSE_OPT_NONEG, NULL },
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
	super_prefix = get_super_prefix();
	git_config(git_default_config, NULL);

	if (read_cache() < 0)
		die("index file corrupt");

	argc = parse_options(argc, argv, prefix, builtin_ls_files_options,
			ls_files_usage, 0);
	el = add_exclude_list(&dir, EXC_CMDL, "--exclude option");
	for (i = 0; i < exclude_list.nr; i++) {
		add_exclude(exclude_list.items[i].string, "", 0, el, --exclude_args);
	}
	if (show_tag || show_valid_bit) {
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

	if (recurse_submodules)
		compile_submodule_options(&dir, show_tag);

	if (recurse_submodules &&
	    (show_stage || show_deleted || show_others || show_unmerged ||
	     show_killed || show_modified || show_resolve_undo || with_tree))
		die("ls-files --recurse-submodules unsupported mode");

	if (recurse_submodules && error_unmatch)
		die("ls-files --recurse-submodules does not support "
		    "--error-unmatch");

	parse_pathspec(&pathspec, 0,
		       PATHSPEC_PREFER_CWD |
		       PATHSPEC_STRIP_SUBMODULE_SLASH_CHEAP,
		       prefix, argv);

	/*
	 * Find common prefix for all pathspec's
	 * This is used as a performance optimization which unfortunately cannot
	 * be done when recursing into submodules
	 */
	if (recurse_submodules)
		max_prefix = NULL;
	else
		max_prefix = common_prefix(&pathspec);
	max_prefix_len = max_prefix ? strlen(max_prefix) : 0;

	/* Treat unmatching pathspec elements as errors */
	if (pathspec.nr && error_unmatch)
		ps_matched = xcalloc(pathspec.nr, 1);

	if ((dir.flags & DIR_SHOW_IGNORED) && !exc_given)
		die("ls-files --ignored needs some exclude pattern");

	/* With no flags, we default to showing the cached files */
	if (!(show_stage || show_deleted || show_others || show_unmerged ||
	      show_killed || show_modified || show_resolve_undo))
		show_cached = 1;

	if (max_prefix)
		prune_cache(max_prefix);
	if (with_tree) {
		/*
		 * Basic sanity check; show-stages and show-unmerged
		 * would not make any sense with this option.
		 */
		if (show_stage || show_unmerged)
			die("ls-files --with-tree is incompatible with -s or -u");
		overlay_tree_on_cache(with_tree, max_prefix);
	}
	show_files(&dir);
	if (show_resolve_undo)
		show_ru_info();

	if (ps_matched) {
		int bad;
		bad = report_path_error(ps_matched, &pathspec, prefix);
		if (bad)
			fprintf(stderr, "Did you forget to 'git add'?\n");

		return bad ? 1 : 0;
	}

	return 0;
}
