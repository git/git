#include "cache.h"
#include "wt-status.h"
#include "object.h"
#include "dir.h"
#include "commit.h"
#include "diff.h"
#include "revision.h"
#include "diffcore.h"
#include "quote.h"
#include "run-command.h"
#include "remote.h"
#include "refs.h"
#include "submodule.h"
#include "column.h"
#include "strbuf.h"

static char default_wt_status_colors[][COLOR_MAXLEN] = {
	GIT_COLOR_NORMAL, /* WT_STATUS_HEADER */
	GIT_COLOR_GREEN,  /* WT_STATUS_UPDATED */
	GIT_COLOR_RED,    /* WT_STATUS_CHANGED */
	GIT_COLOR_RED,    /* WT_STATUS_UNTRACKED */
	GIT_COLOR_RED,    /* WT_STATUS_NOBRANCH */
	GIT_COLOR_RED,    /* WT_STATUS_UNMERGED */
	GIT_COLOR_GREEN,  /* WT_STATUS_LOCAL_BRANCH */
	GIT_COLOR_RED,    /* WT_STATUS_REMOTE_BRANCH */
	GIT_COLOR_NIL,    /* WT_STATUS_ONBRANCH */
};

static const char *color(int slot, struct wt_status *s)
{
	const char *c = "";
	if (want_color(s->use_color))
		c = s->color_palette[slot];
	if (slot == WT_STATUS_ONBRANCH && color_is_nil(c))
		c = s->color_palette[WT_STATUS_HEADER];
	return c;
}

static void status_vprintf(struct wt_status *s, int at_bol, const char *color,
		const char *fmt, va_list ap, const char *trail)
{
	struct strbuf sb = STRBUF_INIT;
	struct strbuf linebuf = STRBUF_INIT;
	const char *line, *eol;

	strbuf_vaddf(&sb, fmt, ap);
	if (!sb.len) {
		strbuf_addch(&sb, '#');
		if (!trail)
			strbuf_addch(&sb, ' ');
		color_print_strbuf(s->fp, color, &sb);
		if (trail)
			fprintf(s->fp, "%s", trail);
		strbuf_release(&sb);
		return;
	}
	for (line = sb.buf; *line; line = eol + 1) {
		eol = strchr(line, '\n');

		strbuf_reset(&linebuf);
		if (at_bol) {
			strbuf_addch(&linebuf, '#');
			if (*line != '\n' && *line != '\t')
				strbuf_addch(&linebuf, ' ');
		}
		if (eol)
			strbuf_add(&linebuf, line, eol - line);
		else
			strbuf_addstr(&linebuf, line);
		color_print_strbuf(s->fp, color, &linebuf);
		if (eol)
			fprintf(s->fp, "\n");
		else
			break;
		at_bol = 1;
	}
	if (trail)
		fprintf(s->fp, "%s", trail);
	strbuf_release(&linebuf);
	strbuf_release(&sb);
}

void status_printf_ln(struct wt_status *s, const char *color,
			const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	status_vprintf(s, 1, color, fmt, ap, "\n");
	va_end(ap);
}

void status_printf(struct wt_status *s, const char *color,
			const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	status_vprintf(s, 1, color, fmt, ap, NULL);
	va_end(ap);
}

static void status_printf_more(struct wt_status *s, const char *color,
			       const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	status_vprintf(s, 0, color, fmt, ap, NULL);
	va_end(ap);
}

void wt_status_prepare(struct wt_status *s)
{
	unsigned char sha1[20];

	memset(s, 0, sizeof(*s));
	memcpy(s->color_palette, default_wt_status_colors,
	       sizeof(default_wt_status_colors));
	s->show_untracked_files = SHOW_NORMAL_UNTRACKED_FILES;
	s->use_color = -1;
	s->relative_paths = 1;
	s->branch = resolve_refdup("HEAD", sha1, 0, NULL);
	s->reference = "HEAD";
	s->fp = stdout;
	s->index_file = get_index_file();
	s->change.strdup_strings = 1;
	s->untracked.strdup_strings = 1;
	s->ignored.strdup_strings = 1;
}

static void wt_status_print_unmerged_header(struct wt_status *s)
{
	int i;
	int del_mod_conflict = 0;
	int both_deleted = 0;
	int not_deleted = 0;
	const char *c = color(WT_STATUS_HEADER, s);

	status_printf_ln(s, c, _("Unmerged paths:"));

	for (i = 0; i < s->change.nr; i++) {
		struct string_list_item *it = &(s->change.items[i]);
		struct wt_status_change_data *d = it->util;

		switch (d->stagemask) {
		case 0:
			break;
		case 1:
			both_deleted = 1;
			break;
		case 3:
		case 5:
			del_mod_conflict = 1;
			break;
		default:
			not_deleted = 1;
			break;
		}
	}

	if (!advice_status_hints)
		return;
	if (s->whence != FROM_COMMIT)
		;
	else if (!s->is_initial)
		status_printf_ln(s, c, _("  (use \"git reset %s <file>...\" to unstage)"), s->reference);
	else
		status_printf_ln(s, c, _("  (use \"git rm --cached <file>...\" to unstage)"));

	if (!both_deleted) {
		if (!del_mod_conflict)
			status_printf_ln(s, c, _("  (use \"git add <file>...\" to mark resolution)"));
		else
			status_printf_ln(s, c, _("  (use \"git add/rm <file>...\" as appropriate to mark resolution)"));
	} else if (!del_mod_conflict && !not_deleted) {
		status_printf_ln(s, c, _("  (use \"git rm <file>...\" to mark resolution)"));
	} else {
		status_printf_ln(s, c, _("  (use \"git add/rm <file>...\" as appropriate to mark resolution)"));
	}
	status_printf_ln(s, c, "");
}

static void wt_status_print_cached_header(struct wt_status *s)
{
	const char *c = color(WT_STATUS_HEADER, s);

	status_printf_ln(s, c, _("Changes to be committed:"));
	if (!advice_status_hints)
		return;
	if (s->whence != FROM_COMMIT)
		; /* NEEDSWORK: use "git reset --unresolve"??? */
	else if (!s->is_initial)
		status_printf_ln(s, c, _("  (use \"git reset %s <file>...\" to unstage)"), s->reference);
	else
		status_printf_ln(s, c, _("  (use \"git rm --cached <file>...\" to unstage)"));
	status_printf_ln(s, c, "");
}

static void wt_status_print_dirty_header(struct wt_status *s,
					 int has_deleted,
					 int has_dirty_submodules)
{
	const char *c = color(WT_STATUS_HEADER, s);

	status_printf_ln(s, c, _("Changes not staged for commit:"));
	if (!advice_status_hints)
		return;
	if (!has_deleted)
		status_printf_ln(s, c, _("  (use \"git add <file>...\" to update what will be committed)"));
	else
		status_printf_ln(s, c, _("  (use \"git add/rm <file>...\" to update what will be committed)"));
	status_printf_ln(s, c, _("  (use \"git checkout -- <file>...\" to discard changes in working directory)"));
	if (has_dirty_submodules)
		status_printf_ln(s, c, _("  (commit or discard the untracked or modified content in submodules)"));
	status_printf_ln(s, c, "");
}

static void wt_status_print_other_header(struct wt_status *s,
					 const char *what,
					 const char *how)
{
	const char *c = color(WT_STATUS_HEADER, s);
	status_printf_ln(s, c, "%s:", what);
	if (!advice_status_hints)
		return;
	status_printf_ln(s, c, _("  (use \"git %s <file>...\" to include in what will be committed)"), how);
	status_printf_ln(s, c, "");
}

static void wt_status_print_trailer(struct wt_status *s)
{
	status_printf_ln(s, color(WT_STATUS_HEADER, s), "");
}

#define quote_path quote_path_relative

static void wt_status_print_unmerged_data(struct wt_status *s,
					  struct string_list_item *it)
{
	const char *c = color(WT_STATUS_UNMERGED, s);
	struct wt_status_change_data *d = it->util;
	struct strbuf onebuf = STRBUF_INIT;
	const char *one, *how = _("bug");

	one = quote_path(it->string, -1, &onebuf, s->prefix);
	status_printf(s, color(WT_STATUS_HEADER, s), "\t");
	switch (d->stagemask) {
	case 1: how = _("both deleted:"); break;
	case 2: how = _("added by us:"); break;
	case 3: how = _("deleted by them:"); break;
	case 4: how = _("added by them:"); break;
	case 5: how = _("deleted by us:"); break;
	case 6: how = _("both added:"); break;
	case 7: how = _("both modified:"); break;
	}
	status_printf_more(s, c, "%-20s%s\n", how, one);
	strbuf_release(&onebuf);
}

static void wt_status_print_change_data(struct wt_status *s,
					int change_type,
					struct string_list_item *it)
{
	struct wt_status_change_data *d = it->util;
	const char *c = color(change_type, s);
	int status = status;
	char *one_name;
	char *two_name;
	const char *one, *two;
	struct strbuf onebuf = STRBUF_INIT, twobuf = STRBUF_INIT;
	struct strbuf extra = STRBUF_INIT;

	one_name = two_name = it->string;
	switch (change_type) {
	case WT_STATUS_UPDATED:
		status = d->index_status;
		if (d->head_path)
			one_name = d->head_path;
		break;
	case WT_STATUS_CHANGED:
		if (d->new_submodule_commits || d->dirty_submodule) {
			strbuf_addstr(&extra, " (");
			if (d->new_submodule_commits)
				strbuf_addf(&extra, _("new commits, "));
			if (d->dirty_submodule & DIRTY_SUBMODULE_MODIFIED)
				strbuf_addf(&extra, _("modified content, "));
			if (d->dirty_submodule & DIRTY_SUBMODULE_UNTRACKED)
				strbuf_addf(&extra, _("untracked content, "));
			strbuf_setlen(&extra, extra.len - 2);
			strbuf_addch(&extra, ')');
		}
		status = d->worktree_status;
		break;
	}

	one = quote_path(one_name, -1, &onebuf, s->prefix);
	two = quote_path(two_name, -1, &twobuf, s->prefix);

	status_printf(s, color(WT_STATUS_HEADER, s), "\t");
	switch (status) {
	case DIFF_STATUS_ADDED:
		status_printf_more(s, c, _("new file:   %s"), one);
		break;
	case DIFF_STATUS_COPIED:
		status_printf_more(s, c, _("copied:     %s -> %s"), one, two);
		break;
	case DIFF_STATUS_DELETED:
		status_printf_more(s, c, _("deleted:    %s"), one);
		break;
	case DIFF_STATUS_MODIFIED:
		status_printf_more(s, c, _("modified:   %s"), one);
		break;
	case DIFF_STATUS_RENAMED:
		status_printf_more(s, c, _("renamed:    %s -> %s"), one, two);
		break;
	case DIFF_STATUS_TYPE_CHANGED:
		status_printf_more(s, c, _("typechange: %s"), one);
		break;
	case DIFF_STATUS_UNKNOWN:
		status_printf_more(s, c, _("unknown:    %s"), one);
		break;
	case DIFF_STATUS_UNMERGED:
		status_printf_more(s, c, _("unmerged:   %s"), one);
		break;
	default:
		die(_("bug: unhandled diff status %c"), status);
	}
	if (extra.len) {
		status_printf_more(s, color(WT_STATUS_HEADER, s), "%s", extra.buf);
		strbuf_release(&extra);
	}
	status_printf_more(s, GIT_COLOR_NORMAL, "\n");
	strbuf_release(&onebuf);
	strbuf_release(&twobuf);
}

static void wt_status_collect_changed_cb(struct diff_queue_struct *q,
					 struct diff_options *options,
					 void *data)
{
	struct wt_status *s = data;
	int i;

	if (!q->nr)
		return;
	s->workdir_dirty = 1;
	for (i = 0; i < q->nr; i++) {
		struct diff_filepair *p;
		struct string_list_item *it;
		struct wt_status_change_data *d;

		p = q->queue[i];
		it = string_list_insert(&s->change, p->one->path);
		d = it->util;
		if (!d) {
			d = xcalloc(1, sizeof(*d));
			it->util = d;
		}
		if (!d->worktree_status)
			d->worktree_status = p->status;
		d->dirty_submodule = p->two->dirty_submodule;
		if (S_ISGITLINK(p->two->mode))
			d->new_submodule_commits = !!hashcmp(p->one->sha1, p->two->sha1);
	}
}

static int unmerged_mask(const char *path)
{
	int pos, mask;
	struct cache_entry *ce;

	pos = cache_name_pos(path, strlen(path));
	if (0 <= pos)
		return 0;

	mask = 0;
	pos = -pos-1;
	while (pos < active_nr) {
		ce = active_cache[pos++];
		if (strcmp(ce->name, path) || !ce_stage(ce))
			break;
		mask |= (1 << (ce_stage(ce) - 1));
	}
	return mask;
}

static void wt_status_collect_updated_cb(struct diff_queue_struct *q,
					 struct diff_options *options,
					 void *data)
{
	struct wt_status *s = data;
	int i;

	for (i = 0; i < q->nr; i++) {
		struct diff_filepair *p;
		struct string_list_item *it;
		struct wt_status_change_data *d;

		p = q->queue[i];
		it = string_list_insert(&s->change, p->two->path);
		d = it->util;
		if (!d) {
			d = xcalloc(1, sizeof(*d));
			it->util = d;
		}
		if (!d->index_status)
			d->index_status = p->status;
		switch (p->status) {
		case DIFF_STATUS_COPIED:
		case DIFF_STATUS_RENAMED:
			d->head_path = xstrdup(p->one->path);
			break;
		case DIFF_STATUS_UNMERGED:
			d->stagemask = unmerged_mask(p->two->path);
			break;
		}
	}
}

static void wt_status_collect_changes_worktree(struct wt_status *s)
{
	struct rev_info rev;

	init_revisions(&rev, NULL);
	setup_revisions(0, NULL, &rev, NULL);
	rev.diffopt.output_format |= DIFF_FORMAT_CALLBACK;
	DIFF_OPT_SET(&rev.diffopt, DIRTY_SUBMODULES);
	if (!s->show_untracked_files)
		DIFF_OPT_SET(&rev.diffopt, IGNORE_UNTRACKED_IN_SUBMODULES);
	if (s->ignore_submodule_arg) {
		DIFF_OPT_SET(&rev.diffopt, OVERRIDE_SUBMODULE_CONFIG);
		handle_ignore_submodules_arg(&rev.diffopt, s->ignore_submodule_arg);
	}
	rev.diffopt.format_callback = wt_status_collect_changed_cb;
	rev.diffopt.format_callback_data = s;
	init_pathspec(&rev.prune_data, s->pathspec);
	run_diff_files(&rev, 0);
}

static void wt_status_collect_changes_index(struct wt_status *s)
{
	struct rev_info rev;
	struct setup_revision_opt opt;

	init_revisions(&rev, NULL);
	memset(&opt, 0, sizeof(opt));
	opt.def = s->is_initial ? EMPTY_TREE_SHA1_HEX : s->reference;
	setup_revisions(0, NULL, &rev, &opt);

	if (s->ignore_submodule_arg) {
		DIFF_OPT_SET(&rev.diffopt, OVERRIDE_SUBMODULE_CONFIG);
		handle_ignore_submodules_arg(&rev.diffopt, s->ignore_submodule_arg);
	}

	rev.diffopt.output_format |= DIFF_FORMAT_CALLBACK;
	rev.diffopt.format_callback = wt_status_collect_updated_cb;
	rev.diffopt.format_callback_data = s;
	rev.diffopt.detect_rename = 1;
	rev.diffopt.rename_limit = 200;
	rev.diffopt.break_opt = 0;
	init_pathspec(&rev.prune_data, s->pathspec);
	run_diff_index(&rev, 1);
}

static void wt_status_collect_changes_initial(struct wt_status *s)
{
	struct pathspec pathspec;
	int i;

	init_pathspec(&pathspec, s->pathspec);
	for (i = 0; i < active_nr; i++) {
		struct string_list_item *it;
		struct wt_status_change_data *d;
		struct cache_entry *ce = active_cache[i];

		if (!ce_path_match(ce, &pathspec))
			continue;
		it = string_list_insert(&s->change, ce->name);
		d = it->util;
		if (!d) {
			d = xcalloc(1, sizeof(*d));
			it->util = d;
		}
		if (ce_stage(ce)) {
			d->index_status = DIFF_STATUS_UNMERGED;
			d->stagemask |= (1 << (ce_stage(ce) - 1));
		}
		else
			d->index_status = DIFF_STATUS_ADDED;
	}
	free_pathspec(&pathspec);
}

static void wt_status_collect_untracked(struct wt_status *s)
{
	int i;
	struct dir_struct dir;

	if (!s->show_untracked_files)
		return;
	memset(&dir, 0, sizeof(dir));
	if (s->show_untracked_files != SHOW_ALL_UNTRACKED_FILES)
		dir.flags |=
			DIR_SHOW_OTHER_DIRECTORIES | DIR_HIDE_EMPTY_DIRECTORIES;
	setup_standard_excludes(&dir);

	fill_directory(&dir, s->pathspec);
	for (i = 0; i < dir.nr; i++) {
		struct dir_entry *ent = dir.entries[i];
		if (cache_name_is_other(ent->name, ent->len) &&
		    match_pathspec(s->pathspec, ent->name, ent->len, 0, NULL))
			string_list_insert(&s->untracked, ent->name);
		free(ent);
	}

	if (s->show_ignored_files) {
		dir.nr = 0;
		dir.flags = DIR_SHOW_IGNORED;
		if (s->show_untracked_files != SHOW_ALL_UNTRACKED_FILES)
			dir.flags |= DIR_SHOW_OTHER_DIRECTORIES;
		fill_directory(&dir, s->pathspec);
		for (i = 0; i < dir.nr; i++) {
			struct dir_entry *ent = dir.entries[i];
			if (cache_name_is_other(ent->name, ent->len) &&
			    match_pathspec(s->pathspec, ent->name, ent->len, 0, NULL))
				string_list_insert(&s->ignored, ent->name);
			free(ent);
		}
	}

	free(dir.entries);
}

void wt_status_collect(struct wt_status *s)
{
	wt_status_collect_changes_worktree(s);

	if (s->is_initial)
		wt_status_collect_changes_initial(s);
	else
		wt_status_collect_changes_index(s);
	wt_status_collect_untracked(s);
}

static void wt_status_print_unmerged(struct wt_status *s)
{
	int shown_header = 0;
	int i;

	for (i = 0; i < s->change.nr; i++) {
		struct wt_status_change_data *d;
		struct string_list_item *it;
		it = &(s->change.items[i]);
		d = it->util;
		if (!d->stagemask)
			continue;
		if (!shown_header) {
			wt_status_print_unmerged_header(s);
			shown_header = 1;
		}
		wt_status_print_unmerged_data(s, it);
	}
	if (shown_header)
		wt_status_print_trailer(s);

}

static void wt_status_print_updated(struct wt_status *s)
{
	int shown_header = 0;
	int i;

	for (i = 0; i < s->change.nr; i++) {
		struct wt_status_change_data *d;
		struct string_list_item *it;
		it = &(s->change.items[i]);
		d = it->util;
		if (!d->index_status ||
		    d->index_status == DIFF_STATUS_UNMERGED)
			continue;
		if (!shown_header) {
			wt_status_print_cached_header(s);
			s->commitable = 1;
			shown_header = 1;
		}
		wt_status_print_change_data(s, WT_STATUS_UPDATED, it);
	}
	if (shown_header)
		wt_status_print_trailer(s);
}

/*
 * -1 : has delete
 *  0 : no change
 *  1 : some change but no delete
 */
static int wt_status_check_worktree_changes(struct wt_status *s,
					     int *dirty_submodules)
{
	int i;
	int changes = 0;

	*dirty_submodules = 0;

	for (i = 0; i < s->change.nr; i++) {
		struct wt_status_change_data *d;
		d = s->change.items[i].util;
		if (!d->worktree_status ||
		    d->worktree_status == DIFF_STATUS_UNMERGED)
			continue;
		if (!changes)
			changes = 1;
		if (d->dirty_submodule)
			*dirty_submodules = 1;
		if (d->worktree_status == DIFF_STATUS_DELETED)
			changes = -1;
	}
	return changes;
}

static void wt_status_print_changed(struct wt_status *s)
{
	int i, dirty_submodules;
	int worktree_changes = wt_status_check_worktree_changes(s, &dirty_submodules);

	if (!worktree_changes)
		return;

	wt_status_print_dirty_header(s, worktree_changes < 0, dirty_submodules);

	for (i = 0; i < s->change.nr; i++) {
		struct wt_status_change_data *d;
		struct string_list_item *it;
		it = &(s->change.items[i]);
		d = it->util;
		if (!d->worktree_status ||
		    d->worktree_status == DIFF_STATUS_UNMERGED)
			continue;
		wt_status_print_change_data(s, WT_STATUS_CHANGED, it);
	}
	wt_status_print_trailer(s);
}

static void wt_status_print_submodule_summary(struct wt_status *s, int uncommitted)
{
	struct child_process sm_summary;
	char summary_limit[64];
	char index[PATH_MAX];
	const char *env[] = { NULL, NULL };
	const char *argv[8];

	env[0] =	index;
	argv[0] =	"submodule";
	argv[1] =	"summary";
	argv[2] =	uncommitted ? "--files" : "--cached";
	argv[3] =	"--for-status";
	argv[4] =	"--summary-limit";
	argv[5] =	summary_limit;
	argv[6] =	uncommitted ? NULL : (s->amend ? "HEAD^" : "HEAD");
	argv[7] =	NULL;

	sprintf(summary_limit, "%d", s->submodule_summary);
	snprintf(index, sizeof(index), "GIT_INDEX_FILE=%s", s->index_file);

	memset(&sm_summary, 0, sizeof(sm_summary));
	sm_summary.argv = argv;
	sm_summary.env = env;
	sm_summary.git_cmd = 1;
	sm_summary.no_stdin = 1;
	fflush(s->fp);
	sm_summary.out = dup(fileno(s->fp));    /* run_command closes it */
	run_command(&sm_summary);
}

static void wt_status_print_other(struct wt_status *s,
				  struct string_list *l,
				  const char *what,
				  const char *how)
{
	int i;
	struct strbuf buf = STRBUF_INIT;
	static struct string_list output = STRING_LIST_INIT_DUP;
	struct column_options copts;

	if (!l->nr)
		return;

	wt_status_print_other_header(s, what, how);

	for (i = 0; i < l->nr; i++) {
		struct string_list_item *it;
		const char *path;
		it = &(l->items[i]);
		path = quote_path(it->string, strlen(it->string),
				  &buf, s->prefix);
		if (column_active(s->colopts)) {
			string_list_append(&output, path);
			continue;
		}
		status_printf(s, color(WT_STATUS_HEADER, s), "\t");
		status_printf_more(s, color(WT_STATUS_UNTRACKED, s),
				   "%s\n", path);
	}

	strbuf_release(&buf);
	if (!column_active(s->colopts))
		return;

	strbuf_addf(&buf, "%s#\t%s",
		    color(WT_STATUS_HEADER, s),
		    color(WT_STATUS_UNTRACKED, s));
	memset(&copts, 0, sizeof(copts));
	copts.padding = 1;
	copts.indent = buf.buf;
	if (want_color(s->use_color))
		copts.nl = GIT_COLOR_RESET "\n";
	print_columns(&output, s->colopts, &copts);
	string_list_clear(&output, 0);
	strbuf_release(&buf);
}

static void wt_status_print_verbose(struct wt_status *s)
{
	struct rev_info rev;
	struct setup_revision_opt opt;

	init_revisions(&rev, NULL);
	DIFF_OPT_SET(&rev.diffopt, ALLOW_TEXTCONV);

	memset(&opt, 0, sizeof(opt));
	opt.def = s->is_initial ? EMPTY_TREE_SHA1_HEX : s->reference;
	setup_revisions(0, NULL, &rev, &opt);

	rev.diffopt.output_format |= DIFF_FORMAT_PATCH;
	rev.diffopt.detect_rename = 1;
	rev.diffopt.file = s->fp;
	rev.diffopt.close_file = 0;
	/*
	 * If we're not going to stdout, then we definitely don't
	 * want color, since we are going to the commit message
	 * file (and even the "auto" setting won't work, since it
	 * will have checked isatty on stdout).
	 */
	if (s->fp != stdout)
		rev.diffopt.use_color = 0;
	run_diff_index(&rev, 1);
}

static void wt_status_print_tracking(struct wt_status *s)
{
	struct strbuf sb = STRBUF_INIT;
	const char *cp, *ep;
	struct branch *branch;

	assert(s->branch && !s->is_initial);
	if (prefixcmp(s->branch, "refs/heads/"))
		return;
	branch = branch_get(s->branch + 11);
	if (!format_tracking_info(branch, &sb))
		return;

	for (cp = sb.buf; (ep = strchr(cp, '\n')) != NULL; cp = ep + 1)
		color_fprintf_ln(s->fp, color(WT_STATUS_HEADER, s),
				 "# %.*s", (int)(ep - cp), cp);
	color_fprintf_ln(s->fp, color(WT_STATUS_HEADER, s), "#");
}

static int has_unmerged(struct wt_status *s)
{
	int i;

	for (i = 0; i < s->change.nr; i++) {
		struct wt_status_change_data *d;
		d = s->change.items[i].util;
		if (d->stagemask)
			return 1;
	}
	return 0;
}

static void show_merge_in_progress(struct wt_status *s,
				struct wt_status_state *state,
				const char *color)
{
	if (has_unmerged(s)) {
		status_printf_ln(s, color, _("You have unmerged paths."));
		if (advice_status_hints)
			status_printf_ln(s, color,
				_("  (fix conflicts and run \"git commit\")"));
	} else {
		status_printf_ln(s, color,
			_("All conflicts fixed but you are still merging."));
		if (advice_status_hints)
			status_printf_ln(s, color,
				_("  (use \"git commit\" to conclude merge)"));
	}
	wt_status_print_trailer(s);
}

static void show_am_in_progress(struct wt_status *s,
				struct wt_status_state *state,
				const char *color)
{
	status_printf_ln(s, color,
		_("You are in the middle of an am session."));
	if (state->am_empty_patch)
		status_printf_ln(s, color,
			_("The current patch is empty."));
	if (advice_status_hints) {
		if (!state->am_empty_patch)
			status_printf_ln(s, color,
				_("  (fix conflicts and then run \"git am --resolved\")"));
		status_printf_ln(s, color,
			_("  (use \"git am --skip\" to skip this patch)"));
		status_printf_ln(s, color,
			_("  (use \"git am --abort\" to restore the original branch)"));
	}
	wt_status_print_trailer(s);
}

static char *read_line_from_git_path(const char *filename)
{
	struct strbuf buf = STRBUF_INIT;
	FILE *fp = fopen(git_path("%s", filename), "r");
	if (!fp) {
		strbuf_release(&buf);
		return NULL;
	}
	strbuf_getline(&buf, fp, '\n');
	if (!fclose(fp)) {
		return strbuf_detach(&buf, NULL);
	} else {
		strbuf_release(&buf);
		return NULL;
	}
}

static int split_commit_in_progress(struct wt_status *s)
{
	int split_in_progress = 0;
	char *head = read_line_from_git_path("HEAD");
	char *orig_head = read_line_from_git_path("ORIG_HEAD");
	char *rebase_amend = read_line_from_git_path("rebase-merge/amend");
	char *rebase_orig_head = read_line_from_git_path("rebase-merge/orig-head");

	if (!head || !orig_head || !rebase_amend || !rebase_orig_head ||
	    !s->branch || strcmp(s->branch, "HEAD"))
		return split_in_progress;

	if (!strcmp(rebase_amend, rebase_orig_head)) {
		if (strcmp(head, rebase_amend))
			split_in_progress = 1;
	} else if (strcmp(orig_head, rebase_orig_head)) {
		split_in_progress = 1;
	}

	if (!s->amend && !s->nowarn && !s->workdir_dirty)
		split_in_progress = 0;

	free(head);
	free(orig_head);
	free(rebase_amend);
	free(rebase_orig_head);
	return split_in_progress;
}

static void show_rebase_in_progress(struct wt_status *s,
				struct wt_status_state *state,
				const char *color)
{
	struct stat st;

	if (has_unmerged(s)) {
		status_printf_ln(s, color, _("You are currently rebasing."));
		if (advice_status_hints) {
			status_printf_ln(s, color,
				_("  (fix conflicts and then run \"git rebase --continue\")"));
			status_printf_ln(s, color,
				_("  (use \"git rebase --skip\" to skip this patch)"));
			status_printf_ln(s, color,
				_("  (use \"git rebase --abort\" to check out the original branch)"));
		}
	} else if (state->rebase_in_progress || !stat(git_path("MERGE_MSG"), &st)) {
		status_printf_ln(s, color, _("You are currently rebasing."));
		if (advice_status_hints)
			status_printf_ln(s, color,
				_("  (all conflicts fixed: run \"git rebase --continue\")"));
	} else if (split_commit_in_progress(s)) {
		status_printf_ln(s, color, _("You are currently splitting a commit during a rebase."));
		if (advice_status_hints)
			status_printf_ln(s, color,
				_("  (Once your working directory is clean, run \"git rebase --continue\")"));
	} else {
		status_printf_ln(s, color, _("You are currently editing a commit during a rebase."));
		if (advice_status_hints && !s->amend) {
			status_printf_ln(s, color,
				_("  (use \"git commit --amend\" to amend the current commit)"));
			status_printf_ln(s, color,
				_("  (use \"git rebase --continue\" once you are satisfied with your changes)"));
		}
	}
	wt_status_print_trailer(s);
}

static void show_cherry_pick_in_progress(struct wt_status *s,
					struct wt_status_state *state,
					const char *color)
{
	status_printf_ln(s, color, _("You are currently cherry-picking."));
	if (advice_status_hints) {
		if (has_unmerged(s))
			status_printf_ln(s, color,
				_("  (fix conflicts and run \"git commit\")"));
		else
			status_printf_ln(s, color,
				_("  (all conflicts fixed: run \"git commit\")"));
	}
	wt_status_print_trailer(s);
}

static void show_bisect_in_progress(struct wt_status *s,
				struct wt_status_state *state,
				const char *color)
{
	status_printf_ln(s, color, _("You are currently bisecting."));
	if (advice_status_hints)
		status_printf_ln(s, color,
			_("  (use \"git bisect reset\" to get back to the original branch)"));
	wt_status_print_trailer(s);
}

static void wt_status_print_state(struct wt_status *s)
{
	const char *state_color = color(WT_STATUS_HEADER, s);
	struct wt_status_state state;
	struct stat st;

	memset(&state, 0, sizeof(state));

	if (!stat(git_path("MERGE_HEAD"), &st)) {
		state.merge_in_progress = 1;
	} else if (!stat(git_path("rebase-apply"), &st)) {
		if (!stat(git_path("rebase-apply/applying"), &st)) {
			state.am_in_progress = 1;
			if (!stat(git_path("rebase-apply/patch"), &st) && !st.st_size)
				state.am_empty_patch = 1;
		} else {
			state.rebase_in_progress = 1;
		}
	} else if (!stat(git_path("rebase-merge"), &st)) {
		if (!stat(git_path("rebase-merge/interactive"), &st))
			state.rebase_interactive_in_progress = 1;
		else
			state.rebase_in_progress = 1;
	} else if (!stat(git_path("CHERRY_PICK_HEAD"), &st)) {
		state.cherry_pick_in_progress = 1;
	}
	if (!stat(git_path("BISECT_LOG"), &st))
		state.bisect_in_progress = 1;

	if (state.merge_in_progress)
		show_merge_in_progress(s, &state, state_color);
	else if (state.am_in_progress)
		show_am_in_progress(s, &state, state_color);
	else if (state.rebase_in_progress || state.rebase_interactive_in_progress)
		show_rebase_in_progress(s, &state, state_color);
	else if (state.cherry_pick_in_progress)
		show_cherry_pick_in_progress(s, &state, state_color);
	if (state.bisect_in_progress)
		show_bisect_in_progress(s, &state, state_color);
}

void wt_status_print(struct wt_status *s)
{
	const char *branch_color = color(WT_STATUS_ONBRANCH, s);
	const char *branch_status_color = color(WT_STATUS_HEADER, s);

	if (s->branch) {
		const char *on_what = _("On branch ");
		const char *branch_name = s->branch;
		if (!prefixcmp(branch_name, "refs/heads/"))
			branch_name += 11;
		else if (!strcmp(branch_name, "HEAD")) {
			branch_name = "";
			branch_status_color = color(WT_STATUS_NOBRANCH, s);
			on_what = _("Not currently on any branch.");
		}
		status_printf(s, color(WT_STATUS_HEADER, s), "");
		status_printf_more(s, branch_status_color, "%s", on_what);
		status_printf_more(s, branch_color, "%s\n", branch_name);
		if (!s->is_initial)
			wt_status_print_tracking(s);
	}

	wt_status_print_state(s);
	if (s->is_initial) {
		status_printf_ln(s, color(WT_STATUS_HEADER, s), "");
		status_printf_ln(s, color(WT_STATUS_HEADER, s), _("Initial commit"));
		status_printf_ln(s, color(WT_STATUS_HEADER, s), "");
	}

	wt_status_print_updated(s);
	wt_status_print_unmerged(s);
	wt_status_print_changed(s);
	if (s->submodule_summary &&
	    (!s->ignore_submodule_arg ||
	     strcmp(s->ignore_submodule_arg, "all"))) {
		wt_status_print_submodule_summary(s, 0);  /* staged */
		wt_status_print_submodule_summary(s, 1);  /* unstaged */
	}
	if (s->show_untracked_files) {
		wt_status_print_other(s, &s->untracked, _("Untracked files"), "add");
		if (s->show_ignored_files)
			wt_status_print_other(s, &s->ignored, _("Ignored files"), "add -f");
	} else if (s->commitable)
		status_printf_ln(s, GIT_COLOR_NORMAL, _("Untracked files not listed%s"),
			advice_status_hints
			? _(" (use -u option to show untracked files)") : "");

	if (s->verbose)
		wt_status_print_verbose(s);
	if (!s->commitable) {
		if (s->amend)
			status_printf_ln(s, GIT_COLOR_NORMAL, _("No changes"));
		else if (s->nowarn)
			; /* nothing */
		else if (s->workdir_dirty) {
			if (advice_status_hints)
				printf(_("no changes added to commit "
					 "(use \"git add\" and/or \"git commit -a\")\n"));
			else
				printf(_("no changes added to commit\n"));
		} else if (s->untracked.nr) {
			if (advice_status_hints)
				printf(_("nothing added to commit but untracked files "
					 "present (use \"git add\" to track)\n"));
			else
				printf(_("nothing added to commit but untracked files present\n"));
		} else if (s->is_initial) {
			if (advice_status_hints)
				printf(_("nothing to commit (create/copy files "
					 "and use \"git add\" to track)\n"));
			else
				printf(_("nothing to commit\n"));
		} else if (!s->show_untracked_files) {
			if (advice_status_hints)
				printf(_("nothing to commit (use -u to show untracked files)\n"));
			else
				printf(_("nothing to commit\n"));
		} else
			printf(_("nothing to commit, working directory clean\n"));
	}
}

static void wt_shortstatus_unmerged(struct string_list_item *it,
			   struct wt_status *s)
{
	struct wt_status_change_data *d = it->util;
	const char *how = "??";

	switch (d->stagemask) {
	case 1: how = "DD"; break; /* both deleted */
	case 2: how = "AU"; break; /* added by us */
	case 3: how = "UD"; break; /* deleted by them */
	case 4: how = "UA"; break; /* added by them */
	case 5: how = "DU"; break; /* deleted by us */
	case 6: how = "AA"; break; /* both added */
	case 7: how = "UU"; break; /* both modified */
	}
	color_fprintf(s->fp, color(WT_STATUS_UNMERGED, s), "%s", how);
	if (s->null_termination) {
		fprintf(stdout, " %s%c", it->string, 0);
	} else {
		struct strbuf onebuf = STRBUF_INIT;
		const char *one;
		one = quote_path(it->string, -1, &onebuf, s->prefix);
		printf(" %s\n", one);
		strbuf_release(&onebuf);
	}
}

static void wt_shortstatus_status(struct string_list_item *it,
			 struct wt_status *s)
{
	struct wt_status_change_data *d = it->util;

	if (d->index_status)
		color_fprintf(s->fp, color(WT_STATUS_UPDATED, s), "%c", d->index_status);
	else
		putchar(' ');
	if (d->worktree_status)
		color_fprintf(s->fp, color(WT_STATUS_CHANGED, s), "%c", d->worktree_status);
	else
		putchar(' ');
	putchar(' ');
	if (s->null_termination) {
		fprintf(stdout, "%s%c", it->string, 0);
		if (d->head_path)
			fprintf(stdout, "%s%c", d->head_path, 0);
	} else {
		struct strbuf onebuf = STRBUF_INIT;
		const char *one;
		if (d->head_path) {
			one = quote_path(d->head_path, -1, &onebuf, s->prefix);
			if (*one != '"' && strchr(one, ' ') != NULL) {
				putchar('"');
				strbuf_addch(&onebuf, '"');
				one = onebuf.buf;
			}
			printf("%s -> ", one);
			strbuf_release(&onebuf);
		}
		one = quote_path(it->string, -1, &onebuf, s->prefix);
		if (*one != '"' && strchr(one, ' ') != NULL) {
			putchar('"');
			strbuf_addch(&onebuf, '"');
			one = onebuf.buf;
		}
		printf("%s\n", one);
		strbuf_release(&onebuf);
	}
}

static void wt_shortstatus_other(struct string_list_item *it,
				 struct wt_status *s, const char *sign)
{
	if (s->null_termination) {
		fprintf(stdout, "%s %s%c", sign, it->string, 0);
	} else {
		struct strbuf onebuf = STRBUF_INIT;
		const char *one;
		one = quote_path(it->string, -1, &onebuf, s->prefix);
		color_fprintf(s->fp, color(WT_STATUS_UNTRACKED, s), "%s", sign);
		printf(" %s\n", one);
		strbuf_release(&onebuf);
	}
}

static void wt_shortstatus_print_tracking(struct wt_status *s)
{
	struct branch *branch;
	const char *header_color = color(WT_STATUS_HEADER, s);
	const char *branch_color_local = color(WT_STATUS_LOCAL_BRANCH, s);
	const char *branch_color_remote = color(WT_STATUS_REMOTE_BRANCH, s);

	const char *base;
	const char *branch_name;
	int num_ours, num_theirs;

	color_fprintf(s->fp, color(WT_STATUS_HEADER, s), "## ");

	if (!s->branch)
		return;
	branch_name = s->branch;

	if (!prefixcmp(branch_name, "refs/heads/"))
		branch_name += 11;
	else if (!strcmp(branch_name, "HEAD")) {
		branch_name = _("HEAD (no branch)");
		branch_color_local = color(WT_STATUS_NOBRANCH, s);
	}

	branch = branch_get(s->branch + 11);
	if (s->is_initial)
		color_fprintf(s->fp, header_color, _("Initial commit on "));
	if (!stat_tracking_info(branch, &num_ours, &num_theirs)) {
		color_fprintf(s->fp, branch_color_local, "%s", branch_name);
		fputc(s->null_termination ? '\0' : '\n', s->fp);
		return;
	}

	base = branch->merge[0]->dst;
	base = shorten_unambiguous_ref(base, 0);
	color_fprintf(s->fp, branch_color_local, "%s", branch_name);
	color_fprintf(s->fp, header_color, "...");
	color_fprintf(s->fp, branch_color_remote, "%s", base);

	color_fprintf(s->fp, header_color, " [");
	if (!num_ours) {
		color_fprintf(s->fp, header_color, _("behind "));
		color_fprintf(s->fp, branch_color_remote, "%d", num_theirs);
	} else if (!num_theirs) {
		color_fprintf(s->fp, header_color, _("ahead "));
		color_fprintf(s->fp, branch_color_local, "%d", num_ours);
	} else {
		color_fprintf(s->fp, header_color, _("ahead "));
		color_fprintf(s->fp, branch_color_local, "%d", num_ours);
		color_fprintf(s->fp, header_color, _(", behind "));
		color_fprintf(s->fp, branch_color_remote, "%d", num_theirs);
	}

	color_fprintf(s->fp, header_color, "]");
	fputc(s->null_termination ? '\0' : '\n', s->fp);
}

void wt_shortstatus_print(struct wt_status *s)
{
	int i;

	if (s->show_branch)
		wt_shortstatus_print_tracking(s);

	for (i = 0; i < s->change.nr; i++) {
		struct wt_status_change_data *d;
		struct string_list_item *it;

		it = &(s->change.items[i]);
		d = it->util;
		if (d->stagemask)
			wt_shortstatus_unmerged(it, s);
		else
			wt_shortstatus_status(it, s);
	}
	for (i = 0; i < s->untracked.nr; i++) {
		struct string_list_item *it;

		it = &(s->untracked.items[i]);
		wt_shortstatus_other(it, s, "??");
	}
	for (i = 0; i < s->ignored.nr; i++) {
		struct string_list_item *it;

		it = &(s->ignored.items[i]);
		wt_shortstatus_other(it, s, "!!");
	}
}

void wt_porcelain_print(struct wt_status *s)
{
	s->use_color = 0;
	s->relative_paths = 0;
	s->prefix = NULL;
	wt_shortstatus_print(s);
}
