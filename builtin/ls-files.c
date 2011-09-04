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

static const char *prefix;
static int max_prefix_len;
static int prefix_len;
static const char **pathspec;
static int error_unmatch;
static char *ps_matched;
static const char *with_tree;
static int exc_given;

static const char *tag_cached = "";
static const char *tag_unmerged = "";
static const char *tag_removed = "";
static const char *tag_other = "";
static const char *tag_killed = "";
static const char *tag_modified = "";
static const char *tag_skip_worktree = "";
static const char *tag_resolve_undo = "";

static void write_name(const char* name, size_t len)
{
	write_name_quoted_relative(name, len, prefix, prefix_len, stdout,
			line_terminator);
}

static void show_dir_entry(const char *tag, struct dir_entry *ent)
{
	int len = max_prefix_len;

	if (len >= ent->len)
		die("git ls-files: internal error - directory entry not superset of prefix");

	if (!match_pathspec(pathspec, ent->name, ent->len, len, ps_matched))
		return;

	fputs(tag, stdout);
	write_name(ent->name, ent->len);
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
					die("bug in show-killed-files");
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

static void show_ce_entry(const char *tag, struct cache_entry *ce)
{
	int len = max_prefix_len;

	if (len >= ce_namelen(ce))
		die("git ls-files: internal error - cache entry not superset of prefix");

	if (!match_pathspec(pathspec, ce->name, ce_namelen(ce), len, ps_matched))
		return;

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
		       find_unique_abbrev(ce->sha1,abbrev),
		       ce_stage(ce));
	}
	write_name(ce->name, ce_namelen(ce));
	if (debug_mode) {
		printf("  ctime: %d:%d\n", ce->ce_ctime.sec, ce->ce_ctime.nsec);
		printf("  mtime: %d:%d\n", ce->ce_mtime.sec, ce->ce_mtime.nsec);
		printf("  dev: %d\tino: %d\n", ce->ce_dev, ce->ce_ino);
		printf("  uid: %d\tgid: %d\n", ce->ce_uid, ce->ce_gid);
		printf("  size: %d\tflags: %x\n", ce->ce_size, ce->ce_flags);
	}
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
		if (!match_pathspec(pathspec, path, len, max_prefix_len, ps_matched))
			continue; /* uninterested */
		for (i = 0; i < 3; i++) {
			if (!ui->mode[i])
				continue;
			printf("%s%06o %s %d\t", tag_resolve_undo, ui->mode[i],
			       find_unique_abbrev(ui->sha1[i], abbrev),
			       i + 1);
			write_name(path, len);
		}
	}
}

static void show_files(struct dir_struct *dir)
{
	int i;

	/* For cached/deleted files we don't need to even do the readdir */
	if (show_others || show_killed) {
		fill_directory(dir, pathspec);
		if (show_others)
			show_other_files(dir);
		if (show_killed)
			show_killed_files(dir);
	}
	if (show_cached | show_stage) {
		for (i = 0; i < active_nr; i++) {
			struct cache_entry *ce = active_cache[i];
			int dtype = ce_to_dtype(ce);
			if (dir->flags & DIR_SHOW_IGNORED &&
			    !excluded(dir, ce->name, &dtype))
				continue;
			if (show_unmerged && !ce_stage(ce))
				continue;
			if (ce->ce_flags & CE_UPDATE)
				continue;
			show_ce_entry(ce_stage(ce) ? tag_unmerged :
				(ce_skip_worktree(ce) ? tag_skip_worktree : tag_cached), ce);
		}
	}
	if (show_deleted | show_modified) {
		for (i = 0; i < active_nr; i++) {
			struct cache_entry *ce = active_cache[i];
			struct stat st;
			int err;
			int dtype = ce_to_dtype(ce);
			if (dir->flags & DIR_SHOW_IGNORED &&
			    !excluded(dir, ce->name, &dtype))
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
		struct cache_entry *ce = active_cache[next];
		if (!strncmp(ce->name, prefix, max_prefix_len)) {
			first = next+1;
			continue;
		}
		last = next;
	}
	active_nr = last;
}

static void strip_trailing_slash_from_submodules(void)
{
	const char **p;

	for (p = pathspec; *p != NULL; p++) {
		int len = strlen(*p), pos;

		if (len < 1 || (*p)[len - 1] != '/')
			continue;
		pos = cache_name_pos(*p, len - 1);
		if (pos >= 0 && S_ISGITLINK(active_cache[pos]->ce_mode))
			*p = xstrndup(*p, len - 1);
	}
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
		static const char *(matchbuf[2]);
		matchbuf[0] = prefix;
		matchbuf[1] = NULL;
		init_pathspec(&pathspec, matchbuf);
		pathspec.items[0].use_wildcard = 0;
	} else
		init_pathspec(&pathspec, NULL);
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

int report_path_error(const char *ps_matched, const char **pathspec, int prefix_len)
{
	/*
	 * Make sure all pathspec matched; otherwise it is an error.
	 */
	int num, errors = 0;
	for (num = 0; pathspec[num]; num++) {
		int other, found_dup;

		if (ps_matched[num])
			continue;
		/*
		 * The caller might have fed identical pathspec
		 * twice.  Do not barf on such a mistake.
		 */
		for (found_dup = other = 0;
		     !found_dup && pathspec[other];
		     other++) {
			if (other == num || !ps_matched[other])
				continue;
			if (!strcmp(pathspec[other], pathspec[num]))
				/*
				 * Ok, we have a match already.
				 */
				found_dup = 1;
		}
		if (found_dup)
			continue;

		error("pathspec '%s' did not match any file(s) known to git.",
		      pathspec[num] + prefix_len);
		errors++;
	}
	return errors;
}

static const char * const ls_files_usage[] = {
	"git ls-files [options] [<file>...]",
	NULL
};

static int option_parse_z(const struct option *opt,
			  const char *arg, int unset)
{
	line_terminator = unset ? '\n' : '\0';

	return 0;
}

static int option_parse_exclude(const struct option *opt,
				const char *arg, int unset)
{
	struct exclude_list *list = opt->value;

	exc_given = 1;
	add_exclude(arg, "", 0, list);

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
	int require_work_tree = 0, show_tag = 0;
	const char *max_prefix;
	struct dir_struct dir;
	struct option builtin_ls_files_options[] = {
		{ OPTION_CALLBACK, 'z', NULL, NULL, NULL,
			"paths are separated with NUL character",
			PARSE_OPT_NOARG, option_parse_z },
		OPT_BOOLEAN('t', NULL, &show_tag,
			"identify the file status with tags"),
		OPT_BOOLEAN('v', NULL, &show_valid_bit,
			"use lowercase letters for 'assume unchanged' files"),
		OPT_BOOLEAN('c', "cached", &show_cached,
			"show cached files in the output (default)"),
		OPT_BOOLEAN('d', "deleted", &show_deleted,
			"show deleted files in the output"),
		OPT_BOOLEAN('m', "modified", &show_modified,
			"show modified files in the output"),
		OPT_BOOLEAN('o', "others", &show_others,
			"show other files in the output"),
		OPT_BIT('i', "ignored", &dir.flags,
			"show ignored files in the output",
			DIR_SHOW_IGNORED),
		OPT_BOOLEAN('s', "stage", &show_stage,
			"show staged contents' object name in the output"),
		OPT_BOOLEAN('k', "killed", &show_killed,
			"show files on the filesystem that need to be removed"),
		OPT_BIT(0, "directory", &dir.flags,
			"show 'other' directories' name only",
			DIR_SHOW_OTHER_DIRECTORIES),
		OPT_NEGBIT(0, "empty-directory", &dir.flags,
			"don't show empty directories",
			DIR_HIDE_EMPTY_DIRECTORIES),
		OPT_BOOLEAN('u', "unmerged", &show_unmerged,
			"show unmerged files in the output"),
		OPT_BOOLEAN(0, "resolve-undo", &show_resolve_undo,
			    "show resolve-undo information"),
		{ OPTION_CALLBACK, 'x', "exclude", &dir.exclude_list[EXC_CMDL], "pattern",
			"skip files matching pattern",
			0, option_parse_exclude },
		{ OPTION_CALLBACK, 'X', "exclude-from", &dir, "file",
			"exclude patterns are read from <file>",
			0, option_parse_exclude_from },
		OPT_STRING(0, "exclude-per-directory", &dir.exclude_per_dir, "file",
			"read additional per-directory exclude patterns in <file>"),
		{ OPTION_CALLBACK, 0, "exclude-standard", &dir, NULL,
			"add the standard git exclusions",
			PARSE_OPT_NOARG, option_parse_exclude_standard },
		{ OPTION_SET_INT, 0, "full-name", &prefix_len, NULL,
			"make the output relative to the project top directory",
			PARSE_OPT_NOARG | PARSE_OPT_NONEG, NULL },
		OPT_BOOLEAN(0, "error-unmatch", &error_unmatch,
			"if any <file> is not in the index, treat this as an error"),
		OPT_STRING(0, "with-tree", &with_tree, "tree-ish",
			"pretend that paths removed since <tree-ish> are still present"),
		OPT__ABBREV(&abbrev),
		OPT_BOOLEAN(0, "debug", &debug_mode, "show debugging data"),
		OPT_END()
	};

	if (argc == 2 && !strcmp(argv[1], "-h"))
		usage_with_options(ls_files_usage, builtin_ls_files_options);

	memset(&dir, 0, sizeof(dir));
	prefix = cmd_prefix;
	if (prefix)
		prefix_len = strlen(prefix);
	git_config(git_default_config, NULL);

	if (read_cache() < 0)
		die("index file corrupt");

	argc = parse_options(argc, argv, prefix, builtin_ls_files_options,
			ls_files_usage, 0);
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

	pathspec = get_pathspec(prefix, argv);

	/* be nice with submodule paths ending in a slash */
	if (pathspec)
		strip_trailing_slash_from_submodules();

	/* Find common prefix for all pathspec's */
	max_prefix = common_prefix(pathspec);
	max_prefix_len = max_prefix ? strlen(max_prefix) : 0;

	/* Treat unmatching pathspec elements as errors */
	if (pathspec && error_unmatch) {
		int num;
		for (num = 0; pathspec[num]; num++)
			;
		ps_matched = xcalloc(1, num);
	}

	if ((dir.flags & DIR_SHOW_IGNORED) && !exc_given)
		die("ls-files --ignored needs some exclude pattern");

	/* With no flags, we default to showing the cached files */
	if (!(show_stage | show_deleted | show_others | show_unmerged |
	      show_killed | show_modified | show_resolve_undo))
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
		bad = report_path_error(ps_matched, pathspec, prefix_len);
		if (bad)
			fprintf(stderr, "Did you forget to 'git add'?\n");

		return bad ? 1 : 0;
	}

	return 0;
}
