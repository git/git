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
#include "color.h"
#include "column.h"
#include "diff.h"
#include "diffcore.h"
#include "revision.h"

static int abbrev;
static int show_deleted;
static int show_cached;
static int show_others;
static int show_stage;
static int show_unmerged;
static int show_resolve_undo;
static int show_modified;
static int show_diff_cached;
static int show_killed;
static int show_valid_bit;
static int show_tag;
static int show_dirs;
static int show_indicator;
static int line_terminator = '\n';
static int debug_mode;
static int use_color;
static unsigned int colopts;
static int porcelain;

static const char *prefix;
static int max_prefix_len;
static int prefix_len;
static struct pathspec pathspec;
static int error_unmatch;
static char *ps_matched;
static const char *with_tree;
static int exc_given;
static int exclude_args;
static struct string_list output = STRING_LIST_INIT_NODUP;

static const char *tag_cached = "";
static const char *tag_unmerged = "";
static const char *tag_removed = "";
static const char *tag_other = "";
static const char *tag_killed = "";
static const char *tag_modified = "";
static const char *tag_diff_cached = "";
static const char *tag_skip_worktree = "";
static const char *tag_resolve_undo = "";

static int compare_output(const void *a_, const void *b_)
{
	const struct string_list_item *a = a_;
	const struct string_list_item *b = b_;
	return strcmp(a->util, b->util);
}

static void write_name(struct strbuf *sb, const char *name)
{
	/*
	 * With "--full-name", prefix_len=0; this caller needs to pass
	 * an empty string in that case (a NULL is good for "").
	 */
	const char *real_prefix = prefix_len ? prefix : NULL;
	if (!line_terminator) {
		struct strbuf sb2 = STRBUF_INIT;
		strbuf_addstr(sb, relative_path(name, real_prefix, &sb2));
		strbuf_release(&sb2);
	} else
		quote_path_relative(name, real_prefix, sb);
}

static void append_indicator(struct strbuf *sb, mode_t mode)
{
	char c = 0;
	if (S_ISREG(mode)) {
		if (mode & (S_IXUSR | S_IXGRP | S_IXOTH))
			c = '*';
	} else if (S_ISDIR(mode))
		c = '/';
	else if (S_ISLNK(mode))
		c = '@';
	else if (S_ISFIFO(mode))
		c = '|';
	else if (S_ISSOCK(mode))
		c = '=';
	else if (S_ISGITLINK(mode))
		c = '&';
#ifdef S_ISDOOR
	else if (S_ISDOOR(mode))
		c = '>';
#endif
	if (c)
		strbuf_addch(sb, c);
}

static void strbuf_fputs(struct strbuf *sb, const char *full_name, FILE *fp)
{
	if (column_active(colopts) || porcelain) {
		struct string_list_item *it;
		it = string_list_append(&output, strbuf_detach(sb, NULL));
		it->util = (void *)full_name;
		return;
	}
	fwrite(sb->buf, sb->len, 1, fp);
}

static void write_dir_entry(struct strbuf *sb, const struct dir_entry *ent)
{
	struct strbuf quoted = STRBUF_INIT;
	struct stat st;
	if (stat(ent->name, &st))
		st.st_mode = 0;
	write_name(&quoted, ent->name);
	if (want_color(use_color))
		color_filename(sb, ent->name, quoted.buf, st.st_mode, 1);
	else
		strbuf_addbuf(sb, &quoted);
	if (show_indicator && st.st_mode)
		append_indicator(sb, st.st_mode);
	strbuf_addch(sb, line_terminator);
	strbuf_release(&quoted);
}

static void show_dir_entry(const char *tag, struct dir_entry *ent)
{
	static struct strbuf sb = STRBUF_INIT;
	int len = max_prefix_len;

	if (len >= ent->len)
		die("git ls-files: internal error - directory entry not superset of prefix");

	if (!dir_path_match(ent, &pathspec, len, ps_matched))
		return;

	strbuf_reset(&sb);
	strbuf_addstr(&sb, tag);
	write_dir_entry(&sb, ent);
	strbuf_fputs(&sb, ent->name, stdout);
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

static int show_as_directory(const struct cache_entry *ce)
{
	struct strbuf sb = STRBUF_INIT;
	const char *p;

	strbuf_add(&sb, ce->name, ce_namelen(ce));
	while (sb.len && (p = strrchr(sb.buf, '/')) != NULL) {
		struct strbuf sb2 = STRBUF_INIT;
		strbuf_setlen(&sb, p - sb.buf);
		if (!match_pathspec(&pathspec, sb.buf, sb.len,
				    max_prefix_len, NULL, 1))
			continue;
		write_name(&sb2, sb.buf);
		if (want_color(use_color)) {
			struct strbuf sb3 = STRBUF_INIT;
			color_filename(&sb3, ce->name, sb2.buf, S_IFDIR, 1);
			strbuf_swap(&sb2, &sb3);
			strbuf_release(&sb3);
		}
		if (show_tag)
			strbuf_insert(&sb2, 0, tag_cached, strlen(tag_cached));
		if (show_indicator)
			append_indicator(&sb2, S_IFDIR);
		strbuf_fputs(&sb2, strbuf_detach(&sb, NULL), NULL);
		strbuf_release(&sb2);
		return 1;
	}
	strbuf_release(&sb);
	return 0;
}

static void write_ce_name(struct strbuf *sb, const struct cache_entry *ce)
{
	struct strbuf quoted = STRBUF_INIT;
	write_name(&quoted, ce->name);
	if (want_color(use_color))
		color_filename(sb, ce->name, quoted.buf, ce->ce_mode, 1);
	else
		strbuf_addbuf(sb, &quoted);
	if (show_indicator)
		append_indicator(sb, ce->ce_mode);
	strbuf_addch(sb, line_terminator);
	strbuf_release(&quoted);
}

static int match_pathspec_with_depth(struct pathspec *ps,
				     const char *name, int namelen,
				     int prefix, char *seen, int is_dir,
				     const int *custom_depth)
{
	int saved_depth = ps->max_depth;
	int result;

	if (custom_depth)
		ps->max_depth = *custom_depth;
	result = match_pathspec(ps, name, namelen, prefix, seen, is_dir);
	if (custom_depth)
		ps->max_depth = saved_depth;
	return result;
}

static void show_ce_entry(const char *tag, const struct cache_entry *ce)
{
	static struct strbuf sb = STRBUF_INIT;
	static const int infinite_depth = -1;
	int len = max_prefix_len;

	if (len >= ce_namelen(ce))
		die("git ls-files: internal error - cache entry not superset of prefix");

	if (!match_pathspec_with_depth(&pathspec, ce->name, ce_namelen(ce),
				       len, ps_matched,
				       S_ISDIR(ce->ce_mode) || S_ISGITLINK(ce->ce_mode),
				       show_dirs ? &infinite_depth : NULL))
		return;

	if (show_dirs && strchr(ce->name, '/') &&
	    !match_pathspec(&pathspec, ce->name, ce_namelen(ce), prefix_len, NULL, 1) &&
	    show_as_directory(ce))
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

	strbuf_reset(&sb);
	if (!show_stage) {
		strbuf_addstr(&sb, tag);
	} else {
		strbuf_addf(&sb, "%s%06o %s %d\t",
			    tag,
			    ce->ce_mode,
			    find_unique_abbrev(ce->sha1, abbrev),
			    ce_stage(ce));
	}
	write_ce_name(&sb, ce);
	strbuf_fputs(&sb, ce->name, stdout);
	if (debug_mode) {
		const struct stat_data *sd = &ce->ce_stat_data;

		printf("  ctime: %d:%d\n", sd->sd_ctime.sec, sd->sd_ctime.nsec);
		printf("  mtime: %d:%d\n", sd->sd_mtime.sec, sd->sd_mtime.nsec);
		printf("  dev: %d\tino: %d\n", sd->sd_dev, sd->sd_ino);
		printf("  uid: %d\tgid: %d\n", sd->sd_uid, sd->sd_gid);
		printf("  size: %d\tflags: %x\n", sd->sd_size, ce->ce_flags);
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
		if (!match_pathspec(&pathspec, path, len,
				    max_prefix_len, ps_matched, 0))
			continue; /* uninterested */
		for (i = 0; i < 3; i++) {
			if (!ui->mode[i])
				continue;
			printf("%s%06o %s %d\t", tag_resolve_undo, ui->mode[i],
			       find_unique_abbrev(ui->sha1[i], abbrev),
			       i + 1);
			/*
			 * With "--full-name", prefix_len=0; this caller needs to pass
			 * an empty string in that case (a NULL is good for "").
			 */
			write_name_quoted_relative(path, prefix_len ? prefix : NULL,
						   stdout, line_terminator);
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
			if (show_diff_cached && (ce->ce_flags & CE_MATCHED)) {
				show_ce_entry(tag_diff_cached, ce);
				/*
				 * if we don't clear, it'll confuse write_ce_name()
				 * when show_ce_entry(tag_modified, ce) is called
				 */
				active_cache[i]->ce_flags &= ~CE_MATCHED;
			}
			if (show_modified && (err || ce_modified(ce, &st, 0)))
				show_ce_entry(tag_modified, ce);
		}
	}
}

static void show_files_compact(struct dir_struct *dir)
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
	if (!(show_cached || show_unmerged || show_deleted ||
	      show_modified || show_diff_cached))
		return;
	for (i = 0; i < active_nr; i++) {
		const struct cache_entry *ce = active_cache[i];
		struct stat st;
		int err, shown = 0;
		if ((dir->flags & DIR_SHOW_IGNORED) &&
		    !ce_excluded(dir, ce))
			continue;
		if (show_unmerged && !ce_stage(ce))
			continue;
		if (ce->ce_flags & CE_UPDATE)
			continue;
		if (ce_skip_worktree(ce))
			continue;
		err = lstat(ce->name, &st);
		if (show_deleted && err) {
			show_ce_entry(tag_removed, ce);
			shown = 1;
		}
		if (show_diff_cached && (ce->ce_flags & CE_MATCHED)) {
			show_ce_entry(tag_diff_cached, ce);
			shown = 1;
			/*
			 * if we don't clear, it'll confuse write_ce_name()
			 * when show_ce_entry(tag_modified, ce) is called
			 */
			active_cache[i]->ce_flags &= ~CE_MATCHED;
		}
		if (show_modified && (err || ce_modified(ce, &st, 0))) {
			show_ce_entry(tag_modified, ce);
			shown = 1;
		}
		if (ce_stage(ce)) {
			show_ce_entry(tag_unmerged, ce);
			shown = 1;
		}
		if (!shown && show_cached)
			show_ce_entry(tag_cached, ce);
	}
}

static void mark_diff_cached(struct diff_queue_struct *q,
			     struct diff_options *options,
			     void *data)
{
	int i;

	for (i = 0; i < q->nr; i++) {
		struct diff_filepair *p = q->queue[i];
		int pos = cache_name_pos(p->two->path, strlen(p->two->path));
		if (pos < 0)
			continue;
		active_cache[pos]->ce_flags |= CE_MATCHED;
	}
}

static void diff_cached(struct pathspec *pathspec)
{
	struct rev_info rev;
	const char *argv[] = { "ls-files", "HEAD", NULL };

	init_revisions(&rev, NULL);
	setup_revisions(2, argv, &rev, NULL);

	rev.diffopt.output_format |= DIFF_FORMAT_CALLBACK;
	rev.diffopt.format_callback = mark_diff_cached;
	rev.diffopt.detect_rename = 1;
	rev.diffopt.rename_limit = 200;
	rev.diffopt.break_opt = 0;
	copy_pathspec(&rev.prune_data, pathspec);
	run_diff_index(&rev, 1);
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

static const char * const ls_usage[] = {
	N_("git list-files [options] [<file>...]"),
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

static int git_ls_config(const char *var, const char *value, void *cb)
{
	if (starts_with(var, "column."))
		return git_column_config(var, value, "listfiles", &colopts);
	if (!strcmp(var, "color.listfiles")) {
		use_color = git_config_colorbool(var, value);
		return 0;
	}
	return git_color_default_config(var, value, cb);
}

int cmd_ls_files(int argc, const char **argv, const char *cmd_prefix)
{
	int require_work_tree = 0, i;
	int max_depth = -1;
	const char *max_prefix;
	struct dir_struct dir;
	struct exclude_list *el;
	struct column_options copts;
	struct string_list exclude_list = STRING_LIST_INIT_NODUP;
	struct option builtin_ls_files_options[] = {
		{ OPTION_CALLBACK, 'z', NULL, NULL, NULL,
			N_("paths are separated with NUL character"),
			PARSE_OPT_NOARG, option_parse_z },
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
		OPT_BOOL(0, "error-unmatch", &error_unmatch,
			N_("if any <file> is not in the index, treat this as an error")),
		OPT_STRING(0, "with-tree", &with_tree, N_("tree-ish"),
			N_("pretend that paths removed since <tree-ish> are still present")),
		OPT__COLOR(&use_color, N_("show color")),
		OPT_COLUMN(0, "column", &colopts, N_("show files in columns")),
		{ OPTION_INTEGER, 0, "max-depth", &max_depth, N_("depth"),
			N_("descend at most <depth> levels"), PARSE_OPT_NONEG,
			NULL, 1 },
		OPT__ABBREV(&abbrev),
		OPT_BOOL(0, "debug", &debug_mode, N_("show debugging data")),
		OPT_END()
	};
	struct option builtin_ls_options[] = {
		OPT_BOOL('c', "cached", &show_cached,
			N_("show cached files (default)")),
		OPT_BOOL('d', "deleted", &show_deleted,
			N_("show cached files that are deleted on working directory")),
		OPT_BOOL('m', "modified", &show_modified,
			N_("show cached files that have modification on working directory")),
		OPT_BOOL('M', "modified", &show_diff_cached,
			N_("show modified files in the cache")),
		OPT_BOOL('o', "others", &show_others,
			N_("show untracked files")),
		OPT_SET_INT('R', "recursive", &max_depth,
			    N_("shortcut for --max-depth=-1"), -1),
		OPT_BOOL('t', "tag", &show_tag,
			N_("identify the file status with tags")),
		OPT_BIT('i', "ignored", &dir.flags,
			N_("show ignored files"),
			DIR_SHOW_IGNORED),
		OPT_BOOL('u', "unmerged", &show_unmerged,
			N_("show unmerged files")),
		OPT_BOOL('F', "classify", &show_indicator,
			 N_("append indicator (one of */=>@|) to entries")),
		OPT__COLOR(&use_color, N_("show color")),
		OPT_COLUMN(0, "column", &colopts, N_("show files in columns")),
		OPT_SET_INT('1', NULL, &colopts,
			    N_("shortcut for --no-column"), COL_PARSEOPT),
		{ OPTION_INTEGER, 0, "max-depth", &max_depth, N_("depth"),
			N_("descend at most <depth> levels"), PARSE_OPT_NONEG,
			NULL, 1 },
		OPT__ABBREV(&abbrev),
		OPT_END()
	};
	struct option *options;
	const char * const *help_usage;

	if (!strcmp(argv[0], "list-files")) {
		help_usage = ls_usage;
		options = builtin_ls_options;
		porcelain = 1;
	} else {
		help_usage = ls_files_usage;
		options = builtin_ls_files_options;
	}
	if (argc == 2 && !strcmp(argv[1], "-h"))
		usage_with_options(help_usage, options);

	memset(&dir, 0, sizeof(dir));
	prefix = cmd_prefix;
	if (prefix)
		prefix_len = strlen(prefix);

	if (porcelain) {
		setenv(GIT_GLOB_PATHSPECS_ENVIRONMENT, "1", 1);
		exc_given = 1;
		setup_standard_excludes(&dir);
		use_color = -1;
		max_depth = 0;
		show_tag = -1;
		git_config(git_ls_config, NULL);
	} else
		git_config(git_default_config, NULL);

	if (read_cache() < 0)
		die("index file corrupt");

	argc = parse_options(argc, argv, prefix, options, help_usage, 0);
	el = add_exclude_list(&dir, EXC_CMDL, "--exclude option");
	for (i = 0; i < exclude_list.nr; i++) {
		add_exclude(exclude_list.items[i].string, "", 0, el, --exclude_args);
	}
	if (show_modified || show_others || show_deleted || (dir.flags & DIR_SHOW_IGNORED) || show_killed)
		require_work_tree = 1;
	if (show_unmerged && !porcelain)
		/*
		 * There's no point in showing unmerged unless
		 * you also show the stage information.
		 */
		show_stage = 1;
	if (dir.exclude_per_dir)
		exc_given = 1;

	finalize_colopts(&colopts, -1);
	if (explicitly_enable_column(colopts)) {
		if (!line_terminator)
			die(_("--column and -z are incompatible"));
		if (show_resolve_undo)
			die(_("--column and --resolve-undo are incompatible"));
		if (debug_mode)
			die(_("--column and --debug are incompatible"));
	}
	if (column_active(colopts) || porcelain)
		line_terminator = 0;

	if (require_work_tree && !is_inside_work_tree())
		setup_work_tree();

	if (want_color(use_color))
		parse_ls_color();

	parse_pathspec(&pathspec, 0,
		       PATHSPEC_PREFER_CWD |
		       (max_depth != -1 ? PATHSPEC_MAXDEPTH_VALID : 0) |
		       PATHSPEC_STRIP_SUBMODULE_SLASH_CHEAP,
		       prefix, argv);
	pathspec.max_depth = max_depth;
	pathspec.recursive = 1;
	show_dirs = porcelain && max_depth != -1;


	/* Find common prefix for all pathspec's */
	max_prefix = common_prefix(&pathspec);
	max_prefix_len = max_prefix ? strlen(max_prefix) : 0;

	/* Treat unmatching pathspec elements as errors */
	if (pathspec.nr && error_unmatch)
		ps_matched = xcalloc(1, pathspec.nr);

	if ((dir.flags & DIR_SHOW_IGNORED) && !exc_given)
		die("ls-files --ignored needs some exclude pattern");

	/* With no flags, we default to showing the cached files */
	if (!(show_stage || show_deleted || show_others || show_unmerged ||
	      show_killed || show_modified || show_resolve_undo || show_diff_cached))
		show_cached = 1;

	if (show_tag == -1)
		show_tag = (show_cached + show_deleted + show_others +
			    show_diff_cached +
			    show_unmerged + show_killed + show_modified) > 1;
	if (show_tag || show_valid_bit) {
		tag_cached = porcelain ? "  " : "H ";
		tag_unmerged = "M ";
		tag_removed = "R ";
		tag_modified = "C ";
		tag_other = "? ";
		tag_diff_cached = "X ";
		tag_killed = "K ";
		tag_skip_worktree = "S ";
		tag_resolve_undo = "U ";
	}

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
	if (porcelain) {
		refresh_index(&the_index, REFRESH_QUIET | REFRESH_UNMERGED, &pathspec, NULL, NULL);
		setup_pager();
	}
	if (show_diff_cached)
		diff_cached(&pathspec);
	if (porcelain)
		show_files_compact(&dir);
	else
		show_files(&dir);
	if (show_resolve_undo)
		show_ru_info();

	memset(&copts, 0, sizeof(copts));
	copts.padding = 2;
	if (porcelain) {
		qsort(output.items, output.nr, sizeof(*output.items),
		      compare_output);
		string_list_remove_duplicates(&output, 0);
	}
	print_columns(&output, colopts, &copts);
	string_list_clear(&output, 0);

	if (ps_matched) {
		int bad;
		bad = report_path_error(ps_matched, &pathspec, prefix);
		if (bad)
			fprintf(stderr, "Did you forget to 'git add'?\n");

		return bad ? 1 : 0;
	}

	return 0;
}
