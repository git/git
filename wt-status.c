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
#include "argv-array.h"
#include "remote.h"
#include "refs.h"
#include "submodule.h"
#include "column.h"
#include "strbuf.h"
#include "utf8.h"
#include "worktree.h"
#include "lockfile.h"
#include "sequencer.h"

#define AB_DELAY_WARNING_IN_MS (2 * 1000)

static const char cut_line[] =
"------------------------ >8 ------------------------\n";

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
		if (s->display_comment_prefix) {
			strbuf_addch(&sb, comment_line_char);
			if (!trail)
				strbuf_addch(&sb, ' ');
		}
		color_print_strbuf(s->fp, color, &sb);
		if (trail)
			fprintf(s->fp, "%s", trail);
		strbuf_release(&sb);
		return;
	}
	for (line = sb.buf; *line; line = eol + 1) {
		eol = strchr(line, '\n');

		strbuf_reset(&linebuf);
		if (at_bol && s->display_comment_prefix) {
			strbuf_addch(&linebuf, comment_line_char);
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

void wt_status_prepare(struct repository *r, struct wt_status *s)
{
	memset(s, 0, sizeof(*s));
	s->repo = r;
	memcpy(s->color_palette, default_wt_status_colors,
	       sizeof(default_wt_status_colors));
	s->show_untracked_files = SHOW_NORMAL_UNTRACKED_FILES;
	s->use_color = -1;
	s->relative_paths = 1;
	s->branch = resolve_refdup("HEAD", 0, NULL, NULL);
	s->reference = "HEAD";
	s->fp = stdout;
	s->index_file = get_index_file();
	s->change.strdup_strings = 1;
	s->untracked.strdup_strings = 1;
	s->ignored.strdup_strings = 1;
	s->show_branch = -1;  /* unspecified */
	s->show_stash = 0;
	s->ahead_behind_flags = AHEAD_BEHIND_UNSPECIFIED;
	s->display_comment_prefix = 0;
	s->detect_rename = -1;
	s->rename_score = -1;
	s->rename_limit = -1;
}

static void wt_longstatus_print_unmerged_header(struct wt_status *s)
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

	if (!s->hints)
		return;
	if (s->whence != FROM_COMMIT)
		;
	else if (!s->is_initial) {
		if (!strcmp(s->reference, "HEAD"))
			status_printf_ln(s, c,
					 _("  (use \"git restore --staged <file>...\" to unstage)"));
		else
			status_printf_ln(s, c,
					 _("  (use \"git restore --source=%s --staged <file>...\" to unstage)"),
					 s->reference);
	} else
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
}

static void wt_longstatus_print_cached_header(struct wt_status *s)
{
	const char *c = color(WT_STATUS_HEADER, s);

	status_printf_ln(s, c, _("Changes to be committed:"));
	if (!s->hints)
		return;
	if (s->whence != FROM_COMMIT)
		; /* NEEDSWORK: use "git reset --unresolve"??? */
	else if (!s->is_initial) {
		if (!strcmp(s->reference, "HEAD"))
			status_printf_ln(s, c
					 , _("  (use \"git restore --staged <file>...\" to unstage)"));
		else
			status_printf_ln(s, c,
					 _("  (use \"git restore --source=%s --staged <file>...\" to unstage)"),
					 s->reference);
	} else
		status_printf_ln(s, c, _("  (use \"git rm --cached <file>...\" to unstage)"));
}

static void wt_longstatus_print_dirty_header(struct wt_status *s,
					     int has_deleted,
					     int has_dirty_submodules)
{
	const char *c = color(WT_STATUS_HEADER, s);

	status_printf_ln(s, c, _("Changes not staged for commit:"));
	if (!s->hints)
		return;
	if (!has_deleted)
		status_printf_ln(s, c, _("  (use \"git add <file>...\" to update what will be committed)"));
	else
		status_printf_ln(s, c, _("  (use \"git add/rm <file>...\" to update what will be committed)"));
	status_printf_ln(s, c, _("  (use \"git restore <file>...\" to discard changes in working directory)"));
	if (has_dirty_submodules)
		status_printf_ln(s, c, _("  (commit or discard the untracked or modified content in submodules)"));
}

static void wt_longstatus_print_other_header(struct wt_status *s,
					     const char *what,
					     const char *how)
{
	const char *c = color(WT_STATUS_HEADER, s);
	status_printf_ln(s, c, "%s:", what);
	if (!s->hints)
		return;
	status_printf_ln(s, c, _("  (use \"git %s <file>...\" to include in what will be committed)"), how);
}

static void wt_longstatus_print_trailer(struct wt_status *s)
{
	status_printf_ln(s, color(WT_STATUS_HEADER, s), "%s", "");
}

#define quote_path quote_path_relative

static const char *wt_status_unmerged_status_string(int stagemask)
{
	switch (stagemask) {
	case 1:
		return _("both deleted:");
	case 2:
		return _("added by us:");
	case 3:
		return _("deleted by them:");
	case 4:
		return _("added by them:");
	case 5:
		return _("deleted by us:");
	case 6:
		return _("both added:");
	case 7:
		return _("both modified:");
	default:
		BUG("unhandled unmerged status %x", stagemask);
	}
}

static const char *wt_status_diff_status_string(int status)
{
	switch (status) {
	case DIFF_STATUS_ADDED:
		return _("new file:");
	case DIFF_STATUS_COPIED:
		return _("copied:");
	case DIFF_STATUS_DELETED:
		return _("deleted:");
	case DIFF_STATUS_MODIFIED:
		return _("modified:");
	case DIFF_STATUS_RENAMED:
		return _("renamed:");
	case DIFF_STATUS_TYPE_CHANGED:
		return _("typechange:");
	case DIFF_STATUS_UNKNOWN:
		return _("unknown:");
	case DIFF_STATUS_UNMERGED:
		return _("unmerged:");
	default:
		return NULL;
	}
}

static int maxwidth(const char *(*label)(int), int minval, int maxval)
{
	int result = 0, i;

	for (i = minval; i <= maxval; i++) {
		const char *s = label(i);
		int len = s ? utf8_strwidth(s) : 0;
		if (len > result)
			result = len;
	}
	return result;
}

static void wt_longstatus_print_unmerged_data(struct wt_status *s,
					      struct string_list_item *it)
{
	const char *c = color(WT_STATUS_UNMERGED, s);
	struct wt_status_change_data *d = it->util;
	struct strbuf onebuf = STRBUF_INIT;
	static char *padding;
	static int label_width;
	const char *one, *how;
	int len;

	if (!padding) {
		label_width = maxwidth(wt_status_unmerged_status_string, 1, 7);
		label_width += strlen(" ");
		padding = xmallocz(label_width);
		memset(padding, ' ', label_width);
	}

	one = quote_path(it->string, s->prefix, &onebuf);
	status_printf(s, color(WT_STATUS_HEADER, s), "\t");

	how = wt_status_unmerged_status_string(d->stagemask);
	len = label_width - utf8_strwidth(how);
	status_printf_more(s, c, "%s%.*s%s\n", how, len, padding, one);
	strbuf_release(&onebuf);
}

static void wt_longstatus_print_change_data(struct wt_status *s,
					    int change_type,
					    struct string_list_item *it)
{
	struct wt_status_change_data *d = it->util;
	const char *c = color(change_type, s);
	int status;
	char *one_name;
	char *two_name;
	const char *one, *two;
	struct strbuf onebuf = STRBUF_INIT, twobuf = STRBUF_INIT;
	struct strbuf extra = STRBUF_INIT;
	static char *padding;
	static int label_width;
	const char *what;
	int len;

	if (!padding) {
		/* If DIFF_STATUS_* uses outside the range [A..Z], we're in trouble */
		label_width = maxwidth(wt_status_diff_status_string, 'A', 'Z');
		label_width += strlen(" ");
		padding = xmallocz(label_width);
		memset(padding, ' ', label_width);
	}

	one_name = two_name = it->string;
	switch (change_type) {
	case WT_STATUS_UPDATED:
		status = d->index_status;
		break;
	case WT_STATUS_CHANGED:
		if (d->new_submodule_commits || d->dirty_submodule) {
			strbuf_addstr(&extra, " (");
			if (d->new_submodule_commits)
				strbuf_addstr(&extra, _("new commits, "));
			if (d->dirty_submodule & DIRTY_SUBMODULE_MODIFIED)
				strbuf_addstr(&extra, _("modified content, "));
			if (d->dirty_submodule & DIRTY_SUBMODULE_UNTRACKED)
				strbuf_addstr(&extra, _("untracked content, "));
			strbuf_setlen(&extra, extra.len - 2);
			strbuf_addch(&extra, ')');
		}
		status = d->worktree_status;
		break;
	default:
		BUG("unhandled change_type %d in wt_longstatus_print_change_data",
		    change_type);
	}

	/*
	 * Only pick up the rename it's relevant. If the rename is for
	 * the changed section and we're printing the updated section,
	 * ignore it.
	 */
	if (d->rename_status == status)
		one_name = d->rename_source;

	one = quote_path(one_name, s->prefix, &onebuf);
	two = quote_path(two_name, s->prefix, &twobuf);

	status_printf(s, color(WT_STATUS_HEADER, s), "\t");
	what = wt_status_diff_status_string(status);
	if (!what)
		BUG("unhandled diff status %c", status);
	len = label_width - utf8_strwidth(what);
	assert(len >= 0);
	if (one_name != two_name)
		status_printf_more(s, c, "%s%.*s%s -> %s",
				   what, len, padding, one, two);
	else
		status_printf_more(s, c, "%s%.*s%s",
				   what, len, padding, one);
	if (extra.len) {
		status_printf_more(s, color(WT_STATUS_HEADER, s), "%s", extra.buf);
		strbuf_release(&extra);
	}
	status_printf_more(s, GIT_COLOR_NORMAL, "\n");
	strbuf_release(&onebuf);
	strbuf_release(&twobuf);
}

static char short_submodule_status(struct wt_status_change_data *d)
{
	if (d->new_submodule_commits)
		return 'M';
	if (d->dirty_submodule & DIRTY_SUBMODULE_MODIFIED)
		return 'm';
	if (d->dirty_submodule & DIRTY_SUBMODULE_UNTRACKED)
		return '?';
	return d->worktree_status;
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
		it = string_list_insert(&s->change, p->two->path);
		d = it->util;
		if (!d) {
			d = xcalloc(1, sizeof(*d));
			it->util = d;
		}
		if (!d->worktree_status)
			d->worktree_status = p->status;
		if (S_ISGITLINK(p->two->mode)) {
			d->dirty_submodule = p->two->dirty_submodule;
			d->new_submodule_commits = !oideq(&p->one->oid,
							  &p->two->oid);
			if (s->status_format == STATUS_FORMAT_SHORT)
				d->worktree_status = short_submodule_status(d);
		}

		switch (p->status) {
		case DIFF_STATUS_ADDED:
			d->mode_worktree = p->two->mode;
			break;

		case DIFF_STATUS_DELETED:
			d->mode_index = p->one->mode;
			oidcpy(&d->oid_index, &p->one->oid);
			/* mode_worktree is zero for a delete. */
			break;

		case DIFF_STATUS_COPIED:
		case DIFF_STATUS_RENAMED:
			if (d->rename_status)
				BUG("multiple renames on the same target? how?");
			d->rename_source = xstrdup(p->one->path);
			d->rename_score = p->score * 100 / MAX_SCORE;
			d->rename_status = p->status;
			/* fallthru */
		case DIFF_STATUS_MODIFIED:
		case DIFF_STATUS_TYPE_CHANGED:
		case DIFF_STATUS_UNMERGED:
			d->mode_index = p->one->mode;
			d->mode_worktree = p->two->mode;
			oidcpy(&d->oid_index, &p->one->oid);
			break;

		default:
			BUG("unhandled diff-files status '%c'", p->status);
			break;
		}

	}
}

static int unmerged_mask(struct index_state *istate, const char *path)
{
	int pos, mask;
	const struct cache_entry *ce;

	pos = index_name_pos(istate, path, strlen(path));
	if (0 <= pos)
		return 0;

	mask = 0;
	pos = -pos-1;
	while (pos < istate->cache_nr) {
		ce = istate->cache[pos++];
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
		case DIFF_STATUS_ADDED:
			/* Leave {mode,oid}_head zero for an add. */
			d->mode_index = p->two->mode;
			oidcpy(&d->oid_index, &p->two->oid);
			s->committable = 1;
			break;
		case DIFF_STATUS_DELETED:
			d->mode_head = p->one->mode;
			oidcpy(&d->oid_head, &p->one->oid);
			s->committable = 1;
			/* Leave {mode,oid}_index zero for a delete. */
			break;

		case DIFF_STATUS_COPIED:
		case DIFF_STATUS_RENAMED:
			if (d->rename_status)
				BUG("multiple renames on the same target? how?");
			d->rename_source = xstrdup(p->one->path);
			d->rename_score = p->score * 100 / MAX_SCORE;
			d->rename_status = p->status;
			/* fallthru */
		case DIFF_STATUS_MODIFIED:
		case DIFF_STATUS_TYPE_CHANGED:
			d->mode_head = p->one->mode;
			d->mode_index = p->two->mode;
			oidcpy(&d->oid_head, &p->one->oid);
			oidcpy(&d->oid_index, &p->two->oid);
			s->committable = 1;
			break;
		case DIFF_STATUS_UNMERGED:
			d->stagemask = unmerged_mask(s->repo->index,
						     p->two->path);
			/*
			 * Don't bother setting {mode,oid}_{head,index} since the print
			 * code will output the stage values directly and not use the
			 * values in these fields.
			 */
			break;

		default:
			BUG("unhandled diff-index status '%c'", p->status);
			break;
		}
	}
}

static void wt_status_collect_changes_worktree(struct wt_status *s)
{
	struct rev_info rev;

	repo_init_revisions(s->repo, &rev, NULL);
	setup_revisions(0, NULL, &rev, NULL);
	rev.diffopt.output_format |= DIFF_FORMAT_CALLBACK;
	rev.diffopt.flags.dirty_submodules = 1;
	rev.diffopt.ita_invisible_in_index = 1;
	if (!s->show_untracked_files)
		rev.diffopt.flags.ignore_untracked_in_submodules = 1;
	if (s->ignore_submodule_arg) {
		rev.diffopt.flags.override_submodule_config = 1;
		handle_ignore_submodules_arg(&rev.diffopt, s->ignore_submodule_arg);
	}
	rev.diffopt.format_callback = wt_status_collect_changed_cb;
	rev.diffopt.format_callback_data = s;
	rev.diffopt.detect_rename = s->detect_rename >= 0 ? s->detect_rename : rev.diffopt.detect_rename;
	rev.diffopt.rename_limit = s->rename_limit >= 0 ? s->rename_limit : rev.diffopt.rename_limit;
	rev.diffopt.rename_score = s->rename_score >= 0 ? s->rename_score : rev.diffopt.rename_score;
	copy_pathspec(&rev.prune_data, &s->pathspec);
	run_diff_files(&rev, 0);
}

static void wt_status_collect_changes_index(struct wt_status *s)
{
	struct rev_info rev;
	struct setup_revision_opt opt;

	repo_init_revisions(s->repo, &rev, NULL);
	memset(&opt, 0, sizeof(opt));
	opt.def = s->is_initial ? empty_tree_oid_hex() : s->reference;
	setup_revisions(0, NULL, &rev, &opt);

	rev.diffopt.flags.override_submodule_config = 1;
	rev.diffopt.ita_invisible_in_index = 1;
	if (s->ignore_submodule_arg) {
		handle_ignore_submodules_arg(&rev.diffopt, s->ignore_submodule_arg);
	} else {
		/*
		 * Unless the user did explicitly request a submodule ignore
		 * mode by passing a command line option we do not ignore any
		 * changed submodule SHA-1s when comparing index and HEAD, no
		 * matter what is configured. Otherwise the user won't be
		 * shown any submodules she manually added (and which are
		 * staged to be committed), which would be really confusing.
		 */
		handle_ignore_submodules_arg(&rev.diffopt, "dirty");
	}

	rev.diffopt.output_format |= DIFF_FORMAT_CALLBACK;
	rev.diffopt.format_callback = wt_status_collect_updated_cb;
	rev.diffopt.format_callback_data = s;
	rev.diffopt.detect_rename = s->detect_rename >= 0 ? s->detect_rename : rev.diffopt.detect_rename;
	rev.diffopt.rename_limit = s->rename_limit >= 0 ? s->rename_limit : rev.diffopt.rename_limit;
	rev.diffopt.rename_score = s->rename_score >= 0 ? s->rename_score : rev.diffopt.rename_score;
	copy_pathspec(&rev.prune_data, &s->pathspec);
	run_diff_index(&rev, 1);
}

static void wt_status_collect_changes_initial(struct wt_status *s)
{
	struct index_state *istate = s->repo->index;
	int i;

	for (i = 0; i < istate->cache_nr; i++) {
		struct string_list_item *it;
		struct wt_status_change_data *d;
		const struct cache_entry *ce = istate->cache[i];

		if (!ce_path_match(istate, ce, &s->pathspec, NULL))
			continue;
		if (ce_intent_to_add(ce))
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
			/*
			 * Don't bother setting {mode,oid}_{head,index} since the print
			 * code will output the stage values directly and not use the
			 * values in these fields.
			 */
			s->committable = 1;
		} else {
			d->index_status = DIFF_STATUS_ADDED;
			/* Leave {mode,oid}_head zero for adds. */
			d->mode_index = ce->ce_mode;
			oidcpy(&d->oid_index, &ce->oid);
			s->committable = 1;
		}
	}
}

static void wt_status_collect_untracked(struct wt_status *s)
{
	int i;
	struct dir_struct dir;
	uint64_t t_begin = getnanotime();
	struct index_state *istate = s->repo->index;

	if (!s->show_untracked_files)
		return;

	memset(&dir, 0, sizeof(dir));
	if (s->show_untracked_files != SHOW_ALL_UNTRACKED_FILES)
		dir.flags |=
			DIR_SHOW_OTHER_DIRECTORIES | DIR_HIDE_EMPTY_DIRECTORIES;
	if (s->show_ignored_mode) {
		dir.flags |= DIR_SHOW_IGNORED_TOO;

		if (s->show_ignored_mode == SHOW_MATCHING_IGNORED)
			dir.flags |= DIR_SHOW_IGNORED_TOO_MODE_MATCHING;
	} else {
		dir.untracked = istate->untracked;
	}

	setup_standard_excludes(&dir);

	fill_directory(&dir, istate, &s->pathspec);

	for (i = 0; i < dir.nr; i++) {
		struct dir_entry *ent = dir.entries[i];
		if (index_name_is_other(istate, ent->name, ent->len) &&
		    dir_path_match(istate, ent, &s->pathspec, 0, NULL))
			string_list_insert(&s->untracked, ent->name);
		free(ent);
	}

	for (i = 0; i < dir.ignored_nr; i++) {
		struct dir_entry *ent = dir.ignored[i];
		if (index_name_is_other(istate, ent->name, ent->len) &&
		    dir_path_match(istate, ent, &s->pathspec, 0, NULL))
			string_list_insert(&s->ignored, ent->name);
		free(ent);
	}

	free(dir.entries);
	free(dir.ignored);
	clear_directory(&dir);

	if (advice_status_u_option)
		s->untracked_in_ms = (getnanotime() - t_begin) / 1000000;
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

void wt_status_collect(struct wt_status *s)
{
	trace2_region_enter("status", "worktrees", s->repo);
	wt_status_collect_changes_worktree(s);
	trace2_region_leave("status", "worktrees", s->repo);

	if (s->is_initial) {
		trace2_region_enter("status", "initial", s->repo);
		wt_status_collect_changes_initial(s);
		trace2_region_leave("status", "initial", s->repo);
	} else {
		trace2_region_enter("status", "index", s->repo);
		wt_status_collect_changes_index(s);
		trace2_region_leave("status", "index", s->repo);
	}

	trace2_region_enter("status", "untracked", s->repo);
	wt_status_collect_untracked(s);
	trace2_region_leave("status", "untracked", s->repo);

	wt_status_get_state(s->repo, &s->state, s->branch && !strcmp(s->branch, "HEAD"));
	if (s->state.merge_in_progress && !has_unmerged(s))
		s->committable = 1;
}

void wt_status_collect_free_buffers(struct wt_status *s)
{
	free(s->state.branch);
	free(s->state.onto);
	free(s->state.detached_from);
}

static void wt_longstatus_print_unmerged(struct wt_status *s)
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
			wt_longstatus_print_unmerged_header(s);
			shown_header = 1;
		}
		wt_longstatus_print_unmerged_data(s, it);
	}
	if (shown_header)
		wt_longstatus_print_trailer(s);

}

static void wt_longstatus_print_updated(struct wt_status *s)
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
			wt_longstatus_print_cached_header(s);
			shown_header = 1;
		}
		wt_longstatus_print_change_data(s, WT_STATUS_UPDATED, it);
	}
	if (shown_header)
		wt_longstatus_print_trailer(s);
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

static void wt_longstatus_print_changed(struct wt_status *s)
{
	int i, dirty_submodules;
	int worktree_changes = wt_status_check_worktree_changes(s, &dirty_submodules);

	if (!worktree_changes)
		return;

	wt_longstatus_print_dirty_header(s, worktree_changes < 0, dirty_submodules);

	for (i = 0; i < s->change.nr; i++) {
		struct wt_status_change_data *d;
		struct string_list_item *it;
		it = &(s->change.items[i]);
		d = it->util;
		if (!d->worktree_status ||
		    d->worktree_status == DIFF_STATUS_UNMERGED)
			continue;
		wt_longstatus_print_change_data(s, WT_STATUS_CHANGED, it);
	}
	wt_longstatus_print_trailer(s);
}

static int stash_count_refs(struct object_id *ooid, struct object_id *noid,
			    const char *email, timestamp_t timestamp, int tz,
			    const char *message, void *cb_data)
{
	int *c = cb_data;
	(*c)++;
	return 0;
}

static void wt_longstatus_print_stash_summary(struct wt_status *s)
{
	int stash_count = 0;

	for_each_reflog_ent("refs/stash", stash_count_refs, &stash_count);
	if (stash_count > 0)
		status_printf_ln(s, GIT_COLOR_NORMAL,
				 Q_("Your stash currently has %d entry",
				    "Your stash currently has %d entries", stash_count),
				 stash_count);
}

static void wt_longstatus_print_submodule_summary(struct wt_status *s, int uncommitted)
{
	struct child_process sm_summary = CHILD_PROCESS_INIT;
	struct strbuf cmd_stdout = STRBUF_INIT;
	struct strbuf summary = STRBUF_INIT;
	char *summary_content;

	argv_array_pushf(&sm_summary.env_array, "GIT_INDEX_FILE=%s",
			 s->index_file);

	argv_array_push(&sm_summary.args, "submodule");
	argv_array_push(&sm_summary.args, "summary");
	argv_array_push(&sm_summary.args, uncommitted ? "--files" : "--cached");
	argv_array_push(&sm_summary.args, "--for-status");
	argv_array_push(&sm_summary.args, "--summary-limit");
	argv_array_pushf(&sm_summary.args, "%d", s->submodule_summary);
	if (!uncommitted)
		argv_array_push(&sm_summary.args, s->amend ? "HEAD^" : "HEAD");

	sm_summary.git_cmd = 1;
	sm_summary.no_stdin = 1;

	capture_command(&sm_summary, &cmd_stdout, 1024);

	/* prepend header, only if there's an actual output */
	if (cmd_stdout.len) {
		if (uncommitted)
			strbuf_addstr(&summary, _("Submodules changed but not updated:"));
		else
			strbuf_addstr(&summary, _("Submodule changes to be committed:"));
		strbuf_addstr(&summary, "\n\n");
	}
	strbuf_addbuf(&summary, &cmd_stdout);
	strbuf_release(&cmd_stdout);

	if (s->display_comment_prefix) {
		size_t len;
		summary_content = strbuf_detach(&summary, &len);
		strbuf_add_commented_lines(&summary, summary_content, len);
		free(summary_content);
	}

	fputs(summary.buf, s->fp);
	strbuf_release(&summary);
}

static void wt_longstatus_print_other(struct wt_status *s,
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

	wt_longstatus_print_other_header(s, what, how);

	for (i = 0; i < l->nr; i++) {
		struct string_list_item *it;
		const char *path;
		it = &(l->items[i]);
		path = quote_path(it->string, s->prefix, &buf);
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
		goto conclude;

	strbuf_addf(&buf, "%s%s\t%s",
		    color(WT_STATUS_HEADER, s),
		    s->display_comment_prefix ? "#" : "",
		    color(WT_STATUS_UNTRACKED, s));
	memset(&copts, 0, sizeof(copts));
	copts.padding = 1;
	copts.indent = buf.buf;
	if (want_color(s->use_color))
		copts.nl = GIT_COLOR_RESET "\n";
	print_columns(&output, s->colopts, &copts);
	string_list_clear(&output, 0);
	strbuf_release(&buf);
conclude:
	status_printf_ln(s, GIT_COLOR_NORMAL, "%s", "");
}

size_t wt_status_locate_end(const char *s, size_t len)
{
	const char *p;
	struct strbuf pattern = STRBUF_INIT;

	strbuf_addf(&pattern, "\n%c %s", comment_line_char, cut_line);
	if (starts_with(s, pattern.buf + 1))
		len = 0;
	else if ((p = strstr(s, pattern.buf)))
		len = p - s + 1;
	strbuf_release(&pattern);
	return len;
}

void wt_status_append_cut_line(struct strbuf *buf)
{
	const char *explanation = _("Do not modify or remove the line above.\nEverything below it will be ignored.");

	strbuf_commented_addf(buf, "%s", cut_line);
	strbuf_add_commented_lines(buf, explanation, strlen(explanation));
}

void wt_status_add_cut_line(FILE *fp)
{
	struct strbuf buf = STRBUF_INIT;

	wt_status_append_cut_line(&buf);
	fputs(buf.buf, fp);
	strbuf_release(&buf);
}

static void wt_longstatus_print_verbose(struct wt_status *s)
{
	struct rev_info rev;
	struct setup_revision_opt opt;
	int dirty_submodules;
	const char *c = color(WT_STATUS_HEADER, s);

	repo_init_revisions(s->repo, &rev, NULL);
	rev.diffopt.flags.allow_textconv = 1;
	rev.diffopt.ita_invisible_in_index = 1;

	memset(&opt, 0, sizeof(opt));
	opt.def = s->is_initial ? empty_tree_oid_hex() : s->reference;
	setup_revisions(0, NULL, &rev, &opt);

	rev.diffopt.output_format |= DIFF_FORMAT_PATCH;
	rev.diffopt.detect_rename = s->detect_rename >= 0 ? s->detect_rename : rev.diffopt.detect_rename;
	rev.diffopt.rename_limit = s->rename_limit >= 0 ? s->rename_limit : rev.diffopt.rename_limit;
	rev.diffopt.rename_score = s->rename_score >= 0 ? s->rename_score : rev.diffopt.rename_score;
	rev.diffopt.file = s->fp;
	rev.diffopt.close_file = 0;
	/*
	 * If we're not going to stdout, then we definitely don't
	 * want color, since we are going to the commit message
	 * file (and even the "auto" setting won't work, since it
	 * will have checked isatty on stdout). But we then do want
	 * to insert the scissor line here to reliably remove the
	 * diff before committing.
	 */
	if (s->fp != stdout) {
		rev.diffopt.use_color = 0;
		wt_status_add_cut_line(s->fp);
	}
	if (s->verbose > 1 && s->committable) {
		/* print_updated() printed a header, so do we */
		if (s->fp != stdout)
			wt_longstatus_print_trailer(s);
		status_printf_ln(s, c, _("Changes to be committed:"));
		rev.diffopt.a_prefix = "c/";
		rev.diffopt.b_prefix = "i/";
	} /* else use prefix as per user config */
	run_diff_index(&rev, 1);
	if (s->verbose > 1 &&
	    wt_status_check_worktree_changes(s, &dirty_submodules)) {
		status_printf_ln(s, c,
			"--------------------------------------------------");
		status_printf_ln(s, c, _("Changes not staged for commit:"));
		setup_work_tree();
		rev.diffopt.a_prefix = "i/";
		rev.diffopt.b_prefix = "w/";
		run_diff_files(&rev, 0);
	}
}

static void wt_longstatus_print_tracking(struct wt_status *s)
{
	struct strbuf sb = STRBUF_INIT;
	const char *cp, *ep, *branch_name;
	struct branch *branch;
	char comment_line_string[3];
	int i;
	uint64_t t_begin = 0;

	assert(s->branch && !s->is_initial);
	if (!skip_prefix(s->branch, "refs/heads/", &branch_name))
		return;
	branch = branch_get(branch_name);

	t_begin = getnanotime();

	if (!format_tracking_info(branch, &sb, s->ahead_behind_flags))
		return;

	if (advice_status_ahead_behind_warning &&
	    s->ahead_behind_flags == AHEAD_BEHIND_FULL) {
		uint64_t t_delta_in_ms = (getnanotime() - t_begin) / 1000000;
		if (t_delta_in_ms > AB_DELAY_WARNING_IN_MS) {
			strbuf_addf(&sb, _("\n"
					   "It took %.2f seconds to compute the branch ahead/behind values.\n"
					   "You can use '--no-ahead-behind' to avoid this.\n"),
				    t_delta_in_ms / 1000.0);
		}
	}

	i = 0;
	if (s->display_comment_prefix) {
		comment_line_string[i++] = comment_line_char;
		comment_line_string[i++] = ' ';
	}
	comment_line_string[i] = '\0';

	for (cp = sb.buf; (ep = strchr(cp, '\n')) != NULL; cp = ep + 1)
		color_fprintf_ln(s->fp, color(WT_STATUS_HEADER, s),
				 "%s%.*s", comment_line_string,
				 (int)(ep - cp), cp);
	if (s->display_comment_prefix)
		color_fprintf_ln(s->fp, color(WT_STATUS_HEADER, s), "%c",
				 comment_line_char);
	else
		fputs("\n", s->fp);
	strbuf_release(&sb);
}

static void show_merge_in_progress(struct wt_status *s,
				   const char *color)
{
	if (has_unmerged(s)) {
		status_printf_ln(s, color, _("You have unmerged paths."));
		if (s->hints) {
			status_printf_ln(s, color,
					 _("  (fix conflicts and run \"git commit\")"));
			status_printf_ln(s, color,
					 _("  (use \"git merge --abort\" to abort the merge)"));
		}
	} else {
		status_printf_ln(s, color,
			_("All conflicts fixed but you are still merging."));
		if (s->hints)
			status_printf_ln(s, color,
				_("  (use \"git commit\" to conclude merge)"));
	}
	wt_longstatus_print_trailer(s);
}

static void show_am_in_progress(struct wt_status *s,
				const char *color)
{
	status_printf_ln(s, color,
		_("You are in the middle of an am session."));
	if (s->state.am_empty_patch)
		status_printf_ln(s, color,
			_("The current patch is empty."));
	if (s->hints) {
		if (!s->state.am_empty_patch)
			status_printf_ln(s, color,
				_("  (fix conflicts and then run \"git am --continue\")"));
		status_printf_ln(s, color,
			_("  (use \"git am --skip\" to skip this patch)"));
		status_printf_ln(s, color,
			_("  (use \"git am --abort\" to restore the original branch)"));
	}
	wt_longstatus_print_trailer(s);
}

static char *read_line_from_git_path(const char *filename)
{
	struct strbuf buf = STRBUF_INIT;
	FILE *fp = fopen_or_warn(git_path("%s", filename), "r");

	if (!fp) {
		strbuf_release(&buf);
		return NULL;
	}
	strbuf_getline_lf(&buf, fp);
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
	char *head, *orig_head, *rebase_amend, *rebase_orig_head;

	if ((!s->amend && !s->nowarn && !s->workdir_dirty) ||
	    !s->branch || strcmp(s->branch, "HEAD"))
		return 0;

	head = read_line_from_git_path("HEAD");
	orig_head = read_line_from_git_path("ORIG_HEAD");
	rebase_amend = read_line_from_git_path("rebase-merge/amend");
	rebase_orig_head = read_line_from_git_path("rebase-merge/orig-head");

	if (!head || !orig_head || !rebase_amend || !rebase_orig_head)
		; /* fall through, no split in progress */
	else if (!strcmp(rebase_amend, rebase_orig_head))
		split_in_progress = !!strcmp(head, rebase_amend);
	else if (strcmp(orig_head, rebase_orig_head))
		split_in_progress = 1;

	free(head);
	free(orig_head);
	free(rebase_amend);
	free(rebase_orig_head);

	return split_in_progress;
}

/*
 * Turn
 * "pick d6a2f0303e897ec257dd0e0a39a5ccb709bc2047 some message"
 * into
 * "pick d6a2f03 some message"
 *
 * The function assumes that the line does not contain useless spaces
 * before or after the command.
 */
static void abbrev_sha1_in_line(struct strbuf *line)
{
	struct strbuf **split;
	int i;

	if (starts_with(line->buf, "exec ") ||
	    starts_with(line->buf, "x ") ||
	    starts_with(line->buf, "label ") ||
	    starts_with(line->buf, "l "))
		return;

	split = strbuf_split_max(line, ' ', 3);
	if (split[0] && split[1]) {
		struct object_id oid;

		/*
		 * strbuf_split_max left a space. Trim it and re-add
		 * it after abbreviation.
		 */
		strbuf_trim(split[1]);
		if (!get_oid(split[1]->buf, &oid)) {
			strbuf_reset(split[1]);
			strbuf_add_unique_abbrev(split[1], &oid,
						 DEFAULT_ABBREV);
			strbuf_addch(split[1], ' ');
			strbuf_reset(line);
			for (i = 0; split[i]; i++)
				strbuf_addbuf(line, split[i]);
		}
	}
	strbuf_list_free(split);
}

static int read_rebase_todolist(const char *fname, struct string_list *lines)
{
	struct strbuf line = STRBUF_INIT;
	FILE *f = fopen(git_path("%s", fname), "r");

	if (!f) {
		if (errno == ENOENT)
			return -1;
		die_errno("Could not open file %s for reading",
			  git_path("%s", fname));
	}
	while (!strbuf_getline_lf(&line, f)) {
		if (line.len && line.buf[0] == comment_line_char)
			continue;
		strbuf_trim(&line);
		if (!line.len)
			continue;
		abbrev_sha1_in_line(&line);
		string_list_append(lines, line.buf);
	}
	fclose(f);
	strbuf_release(&line);
	return 0;
}

static void show_rebase_information(struct wt_status *s,
				    const char *color)
{
	if (s->state.rebase_interactive_in_progress) {
		int i;
		int nr_lines_to_show = 2;

		struct string_list have_done = STRING_LIST_INIT_DUP;
		struct string_list yet_to_do = STRING_LIST_INIT_DUP;

		read_rebase_todolist("rebase-merge/done", &have_done);
		if (read_rebase_todolist("rebase-merge/git-rebase-todo",
					 &yet_to_do))
			status_printf_ln(s, color,
				_("git-rebase-todo is missing."));
		if (have_done.nr == 0)
			status_printf_ln(s, color, _("No commands done."));
		else {
			status_printf_ln(s, color,
				Q_("Last command done (%d command done):",
					"Last commands done (%d commands done):",
					have_done.nr),
				have_done.nr);
			for (i = (have_done.nr > nr_lines_to_show)
				? have_done.nr - nr_lines_to_show : 0;
				i < have_done.nr;
				i++)
				status_printf_ln(s, color, "   %s", have_done.items[i].string);
			if (have_done.nr > nr_lines_to_show && s->hints)
				status_printf_ln(s, color,
					_("  (see more in file %s)"), git_path("rebase-merge/done"));
		}

		if (yet_to_do.nr == 0)
			status_printf_ln(s, color,
					 _("No commands remaining."));
		else {
			status_printf_ln(s, color,
				Q_("Next command to do (%d remaining command):",
					"Next commands to do (%d remaining commands):",
					yet_to_do.nr),
				yet_to_do.nr);
			for (i = 0; i < nr_lines_to_show && i < yet_to_do.nr; i++)
				status_printf_ln(s, color, "   %s", yet_to_do.items[i].string);
			if (s->hints)
				status_printf_ln(s, color,
					_("  (use \"git rebase --edit-todo\" to view and edit)"));
		}
		string_list_clear(&yet_to_do, 0);
		string_list_clear(&have_done, 0);
	}
}

static void print_rebase_state(struct wt_status *s,
			       const char *color)
{
	if (s->state.branch)
		status_printf_ln(s, color,
				 _("You are currently rebasing branch '%s' on '%s'."),
				 s->state.branch,
				 s->state.onto);
	else
		status_printf_ln(s, color,
				 _("You are currently rebasing."));
}

static void show_rebase_in_progress(struct wt_status *s,
				    const char *color)
{
	struct stat st;

	show_rebase_information(s, color);
	if (has_unmerged(s)) {
		print_rebase_state(s, color);
		if (s->hints) {
			status_printf_ln(s, color,
				_("  (fix conflicts and then run \"git rebase --continue\")"));
			status_printf_ln(s, color,
				_("  (use \"git rebase --skip\" to skip this patch)"));
			status_printf_ln(s, color,
				_("  (use \"git rebase --abort\" to check out the original branch)"));
		}
	} else if (s->state.rebase_in_progress ||
		   !stat(git_path_merge_msg(s->repo), &st)) {
		print_rebase_state(s, color);
		if (s->hints)
			status_printf_ln(s, color,
				_("  (all conflicts fixed: run \"git rebase --continue\")"));
	} else if (split_commit_in_progress(s)) {
		if (s->state.branch)
			status_printf_ln(s, color,
					 _("You are currently splitting a commit while rebasing branch '%s' on '%s'."),
					 s->state.branch,
					 s->state.onto);
		else
			status_printf_ln(s, color,
					 _("You are currently splitting a commit during a rebase."));
		if (s->hints)
			status_printf_ln(s, color,
				_("  (Once your working directory is clean, run \"git rebase --continue\")"));
	} else {
		if (s->state.branch)
			status_printf_ln(s, color,
					 _("You are currently editing a commit while rebasing branch '%s' on '%s'."),
					 s->state.branch,
					 s->state.onto);
		else
			status_printf_ln(s, color,
					 _("You are currently editing a commit during a rebase."));
		if (s->hints && !s->amend) {
			status_printf_ln(s, color,
				_("  (use \"git commit --amend\" to amend the current commit)"));
			status_printf_ln(s, color,
				_("  (use \"git rebase --continue\" once you are satisfied with your changes)"));
		}
	}
	wt_longstatus_print_trailer(s);
}

static void show_cherry_pick_in_progress(struct wt_status *s,
					 const char *color)
{
	if (is_null_oid(&s->state.cherry_pick_head_oid))
		status_printf_ln(s, color,
			_("Cherry-pick currently in progress."));
	else
		status_printf_ln(s, color,
			_("You are currently cherry-picking commit %s."),
			find_unique_abbrev(&s->state.cherry_pick_head_oid,
					   DEFAULT_ABBREV));

	if (s->hints) {
		if (has_unmerged(s))
			status_printf_ln(s, color,
				_("  (fix conflicts and run \"git cherry-pick --continue\")"));
		else if (is_null_oid(&s->state.cherry_pick_head_oid))
			status_printf_ln(s, color,
				_("  (run \"git cherry-pick --continue\" to continue)"));
		else
			status_printf_ln(s, color,
				_("  (all conflicts fixed: run \"git cherry-pick --continue\")"));
		status_printf_ln(s, color,
			_("  (use \"git cherry-pick --abort\" to cancel the cherry-pick operation)"));
	}
	wt_longstatus_print_trailer(s);
}

static void show_revert_in_progress(struct wt_status *s,
				    const char *color)
{
	if (is_null_oid(&s->state.revert_head_oid))
		status_printf_ln(s, color,
			_("Revert currently in progress."));
	else
		status_printf_ln(s, color,
			_("You are currently reverting commit %s."),
			find_unique_abbrev(&s->state.revert_head_oid,
					   DEFAULT_ABBREV));
	if (s->hints) {
		if (has_unmerged(s))
			status_printf_ln(s, color,
				_("  (fix conflicts and run \"git revert --continue\")"));
		else if (is_null_oid(&s->state.revert_head_oid))
			status_printf_ln(s, color,
				_("  (run \"git revert --continue\" to continue)"));
		else
			status_printf_ln(s, color,
				_("  (all conflicts fixed: run \"git revert --continue\")"));
		status_printf_ln(s, color,
			_("  (use \"git revert --abort\" to cancel the revert operation)"));
	}
	wt_longstatus_print_trailer(s);
}

static void show_bisect_in_progress(struct wt_status *s,
				    const char *color)
{
	if (s->state.branch)
		status_printf_ln(s, color,
				 _("You are currently bisecting, started from branch '%s'."),
				 s->state.branch);
	else
		status_printf_ln(s, color,
				 _("You are currently bisecting."));
	if (s->hints)
		status_printf_ln(s, color,
			_("  (use \"git bisect reset\" to get back to the original branch)"));
	wt_longstatus_print_trailer(s);
}

/*
 * Extract branch information from rebase/bisect
 */
static char *get_branch(const struct worktree *wt, const char *path)
{
	struct strbuf sb = STRBUF_INIT;
	struct object_id oid;
	const char *branch_name;

	if (strbuf_read_file(&sb, worktree_git_path(wt, "%s", path), 0) <= 0)
		goto got_nothing;

	while (sb.len && sb.buf[sb.len - 1] == '\n')
		strbuf_setlen(&sb, sb.len - 1);
	if (!sb.len)
		goto got_nothing;
	if (skip_prefix(sb.buf, "refs/heads/", &branch_name))
		strbuf_remove(&sb, 0, branch_name - sb.buf);
	else if (starts_with(sb.buf, "refs/"))
		;
	else if (!get_oid_hex(sb.buf, &oid)) {
		strbuf_reset(&sb);
		strbuf_add_unique_abbrev(&sb, &oid, DEFAULT_ABBREV);
	} else if (!strcmp(sb.buf, "detached HEAD")) /* rebase */
		goto got_nothing;
	else			/* bisect */
		;
	return strbuf_detach(&sb, NULL);

got_nothing:
	strbuf_release(&sb);
	return NULL;
}

struct grab_1st_switch_cbdata {
	struct strbuf buf;
	struct object_id noid;
};

static int grab_1st_switch(struct object_id *ooid, struct object_id *noid,
			   const char *email, timestamp_t timestamp, int tz,
			   const char *message, void *cb_data)
{
	struct grab_1st_switch_cbdata *cb = cb_data;
	const char *target = NULL, *end;

	if (!skip_prefix(message, "checkout: moving from ", &message))
		return 0;
	target = strstr(message, " to ");
	if (!target)
		return 0;
	target += strlen(" to ");
	strbuf_reset(&cb->buf);
	oidcpy(&cb->noid, noid);
	end = strchrnul(target, '\n');
	strbuf_add(&cb->buf, target, end - target);
	if (!strcmp(cb->buf.buf, "HEAD")) {
		/* HEAD is relative. Resolve it to the right reflog entry. */
		strbuf_reset(&cb->buf);
		strbuf_add_unique_abbrev(&cb->buf, noid, DEFAULT_ABBREV);
	}
	return 1;
}

static void wt_status_get_detached_from(struct repository *r,
					struct wt_status_state *state)
{
	struct grab_1st_switch_cbdata cb;
	struct commit *commit;
	struct object_id oid;
	char *ref = NULL;

	strbuf_init(&cb.buf, 0);
	if (for_each_reflog_ent_reverse("HEAD", grab_1st_switch, &cb) <= 0) {
		strbuf_release(&cb.buf);
		return;
	}

	if (dwim_ref(cb.buf.buf, cb.buf.len, &oid, &ref) == 1 &&
	    /* sha1 is a commit? match without further lookup */
	    (oideq(&cb.noid, &oid) ||
	     /* perhaps sha1 is a tag, try to dereference to a commit */
	     ((commit = lookup_commit_reference_gently(r, &oid, 1)) != NULL &&
	      oideq(&cb.noid, &commit->object.oid)))) {
		const char *from = ref;
		if (!skip_prefix(from, "refs/tags/", &from))
			skip_prefix(from, "refs/remotes/", &from);
		state->detached_from = xstrdup(from);
	} else
		state->detached_from =
			xstrdup(find_unique_abbrev(&cb.noid, DEFAULT_ABBREV));
	oidcpy(&state->detached_oid, &cb.noid);
	state->detached_at = !get_oid("HEAD", &oid) &&
			     oideq(&oid, &state->detached_oid);

	free(ref);
	strbuf_release(&cb.buf);
}

int wt_status_check_rebase(const struct worktree *wt,
			   struct wt_status_state *state)
{
	struct stat st;

	if (!stat(worktree_git_path(wt, "rebase-apply"), &st)) {
		if (!stat(worktree_git_path(wt, "rebase-apply/applying"), &st)) {
			state->am_in_progress = 1;
			if (!stat(worktree_git_path(wt, "rebase-apply/patch"), &st) && !st.st_size)
				state->am_empty_patch = 1;
		} else {
			state->rebase_in_progress = 1;
			state->branch = get_branch(wt, "rebase-apply/head-name");
			state->onto = get_branch(wt, "rebase-apply/onto");
		}
	} else if (!stat(worktree_git_path(wt, "rebase-merge"), &st)) {
		if (!stat(worktree_git_path(wt, "rebase-merge/interactive"), &st))
			state->rebase_interactive_in_progress = 1;
		else
			state->rebase_in_progress = 1;
		state->branch = get_branch(wt, "rebase-merge/head-name");
		state->onto = get_branch(wt, "rebase-merge/onto");
	} else
		return 0;
	return 1;
}

int wt_status_check_bisect(const struct worktree *wt,
			   struct wt_status_state *state)
{
	struct stat st;

	if (!stat(worktree_git_path(wt, "BISECT_LOG"), &st)) {
		state->bisect_in_progress = 1;
		state->branch = get_branch(wt, "BISECT_START");
		return 1;
	}
	return 0;
}

void wt_status_get_state(struct repository *r,
			 struct wt_status_state *state,
			 int get_detached_from)
{
	struct stat st;
	struct object_id oid;
	enum replay_action action;

	if (!stat(git_path_merge_head(r), &st)) {
		wt_status_check_rebase(NULL, state);
		state->merge_in_progress = 1;
	} else if (wt_status_check_rebase(NULL, state)) {
		;		/* all set */
	} else if (!stat(git_path_cherry_pick_head(r), &st) &&
			!get_oid("CHERRY_PICK_HEAD", &oid)) {
		state->cherry_pick_in_progress = 1;
		oidcpy(&state->cherry_pick_head_oid, &oid);
	}
	wt_status_check_bisect(NULL, state);
	if (!stat(git_path_revert_head(r), &st) &&
	    !get_oid("REVERT_HEAD", &oid)) {
		state->revert_in_progress = 1;
		oidcpy(&state->revert_head_oid, &oid);
	}
	if (!sequencer_get_last_command(r, &action)) {
		if (action == REPLAY_PICK) {
			state->cherry_pick_in_progress = 1;
			oidcpy(&state->cherry_pick_head_oid, &null_oid);
		} else {
			state->revert_in_progress = 1;
			oidcpy(&state->revert_head_oid, &null_oid);
		}
	}
	if (get_detached_from)
		wt_status_get_detached_from(r, state);
}

static void wt_longstatus_print_state(struct wt_status *s)
{
	const char *state_color = color(WT_STATUS_HEADER, s);
	struct wt_status_state *state = &s->state;

	if (state->merge_in_progress) {
		if (state->rebase_interactive_in_progress) {
			show_rebase_information(s, state_color);
			fputs("\n", s->fp);
		}
		show_merge_in_progress(s, state_color);
	} else if (state->am_in_progress)
		show_am_in_progress(s, state_color);
	else if (state->rebase_in_progress || state->rebase_interactive_in_progress)
		show_rebase_in_progress(s, state_color);
	else if (state->cherry_pick_in_progress)
		show_cherry_pick_in_progress(s, state_color);
	else if (state->revert_in_progress)
		show_revert_in_progress(s, state_color);
	if (state->bisect_in_progress)
		show_bisect_in_progress(s, state_color);
}

static void wt_longstatus_print(struct wt_status *s)
{
	const char *branch_color = color(WT_STATUS_ONBRANCH, s);
	const char *branch_status_color = color(WT_STATUS_HEADER, s);

	if (s->branch) {
		const char *on_what = _("On branch ");
		const char *branch_name = s->branch;
		if (!strcmp(branch_name, "HEAD")) {
			branch_status_color = color(WT_STATUS_NOBRANCH, s);
			if (s->state.rebase_in_progress ||
			    s->state.rebase_interactive_in_progress) {
				if (s->state.rebase_interactive_in_progress)
					on_what = _("interactive rebase in progress; onto ");
				else
					on_what = _("rebase in progress; onto ");
				branch_name = s->state.onto;
			} else if (s->state.detached_from) {
				branch_name = s->state.detached_from;
				if (s->state.detached_at)
					on_what = HEAD_DETACHED_AT;
				else
					on_what = HEAD_DETACHED_FROM;
			} else {
				branch_name = "";
				on_what = _("Not currently on any branch.");
			}
		} else
			skip_prefix(branch_name, "refs/heads/", &branch_name);
		status_printf(s, color(WT_STATUS_HEADER, s), "%s", "");
		status_printf_more(s, branch_status_color, "%s", on_what);
		status_printf_more(s, branch_color, "%s\n", branch_name);
		if (!s->is_initial)
			wt_longstatus_print_tracking(s);
	}

	wt_longstatus_print_state(s);

	if (s->is_initial) {
		status_printf_ln(s, color(WT_STATUS_HEADER, s), "%s", "");
		status_printf_ln(s, color(WT_STATUS_HEADER, s),
				 s->commit_template
				 ? _("Initial commit")
				 : _("No commits yet"));
		status_printf_ln(s, color(WT_STATUS_HEADER, s), "%s", "");
	}

	wt_longstatus_print_updated(s);
	wt_longstatus_print_unmerged(s);
	wt_longstatus_print_changed(s);
	if (s->submodule_summary &&
	    (!s->ignore_submodule_arg ||
	     strcmp(s->ignore_submodule_arg, "all"))) {
		wt_longstatus_print_submodule_summary(s, 0);  /* staged */
		wt_longstatus_print_submodule_summary(s, 1);  /* unstaged */
	}
	if (s->show_untracked_files) {
		wt_longstatus_print_other(s, &s->untracked, _("Untracked files"), "add");
		if (s->show_ignored_mode)
			wt_longstatus_print_other(s, &s->ignored, _("Ignored files"), "add -f");
		if (advice_status_u_option && 2000 < s->untracked_in_ms) {
			status_printf_ln(s, GIT_COLOR_NORMAL, "%s", "");
			status_printf_ln(s, GIT_COLOR_NORMAL,
					 _("It took %.2f seconds to enumerate untracked files. 'status -uno'\n"
					   "may speed it up, but you have to be careful not to forget to add\n"
					   "new files yourself (see 'git help status')."),
					 s->untracked_in_ms / 1000.0);
		}
	} else if (s->committable)
		status_printf_ln(s, GIT_COLOR_NORMAL, _("Untracked files not listed%s"),
			s->hints
			? _(" (use -u option to show untracked files)") : "");

	if (s->verbose)
		wt_longstatus_print_verbose(s);
	if (!s->committable) {
		if (s->amend)
			status_printf_ln(s, GIT_COLOR_NORMAL, _("No changes"));
		else if (s->nowarn)
			; /* nothing */
		else if (s->workdir_dirty) {
			if (s->hints)
				printf(_("no changes added to commit "
					 "(use \"git add\" and/or \"git commit -a\")\n"));
			else
				printf(_("no changes added to commit\n"));
		} else if (s->untracked.nr) {
			if (s->hints)
				printf(_("nothing added to commit but untracked files "
					 "present (use \"git add\" to track)\n"));
			else
				printf(_("nothing added to commit but untracked files present\n"));
		} else if (s->is_initial) {
			if (s->hints)
				printf(_("nothing to commit (create/copy files "
					 "and use \"git add\" to track)\n"));
			else
				printf(_("nothing to commit\n"));
		} else if (!s->show_untracked_files) {
			if (s->hints)
				printf(_("nothing to commit (use -u to show untracked files)\n"));
			else
				printf(_("nothing to commit\n"));
		} else
			printf(_("nothing to commit, working tree clean\n"));
	}
	if(s->show_stash)
		wt_longstatus_print_stash_summary(s);
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
		one = quote_path(it->string, s->prefix, &onebuf);
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
		if (d->rename_source)
			fprintf(stdout, "%s%c", d->rename_source, 0);
	} else {
		struct strbuf onebuf = STRBUF_INIT;
		const char *one;

		if (d->rename_source) {
			one = quote_path(d->rename_source, s->prefix, &onebuf);
			if (*one != '"' && strchr(one, ' ') != NULL) {
				putchar('"');
				strbuf_addch(&onebuf, '"');
				one = onebuf.buf;
			}
			printf("%s -> ", one);
			strbuf_release(&onebuf);
		}
		one = quote_path(it->string, s->prefix, &onebuf);
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
		one = quote_path(it->string, s->prefix, &onebuf);
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
	char *short_base;
	const char *branch_name;
	int num_ours, num_theirs, sti;
	int upstream_is_gone = 0;

	color_fprintf(s->fp, color(WT_STATUS_HEADER, s), "## ");

	if (!s->branch)
		return;
	branch_name = s->branch;

#define LABEL(string) (s->no_gettext ? (string) : _(string))

	if (s->is_initial)
		color_fprintf(s->fp, header_color, LABEL(N_("No commits yet on ")));

	if (!strcmp(s->branch, "HEAD")) {
		color_fprintf(s->fp, color(WT_STATUS_NOBRANCH, s), "%s",
			      LABEL(N_("HEAD (no branch)")));
		goto conclude;
	}

	skip_prefix(branch_name, "refs/heads/", &branch_name);

	branch = branch_get(branch_name);

	color_fprintf(s->fp, branch_color_local, "%s", branch_name);

	sti = stat_tracking_info(branch, &num_ours, &num_theirs, &base,
				 0, s->ahead_behind_flags);
	if (sti < 0) {
		if (!base)
			goto conclude;

		upstream_is_gone = 1;
	}

	short_base = shorten_unambiguous_ref(base, 0);
	color_fprintf(s->fp, header_color, "...");
	color_fprintf(s->fp, branch_color_remote, "%s", short_base);
	free(short_base);

	if (!upstream_is_gone && !sti)
		goto conclude;

	color_fprintf(s->fp, header_color, " [");
	if (upstream_is_gone) {
		color_fprintf(s->fp, header_color, LABEL(N_("gone")));
	} else if (s->ahead_behind_flags == AHEAD_BEHIND_QUICK) {
		color_fprintf(s->fp, header_color, LABEL(N_("different")));
	} else if (!num_ours) {
		color_fprintf(s->fp, header_color, LABEL(N_("behind ")));
		color_fprintf(s->fp, branch_color_remote, "%d", num_theirs);
	} else if (!num_theirs) {
		color_fprintf(s->fp, header_color, LABEL(N_("ahead ")));
		color_fprintf(s->fp, branch_color_local, "%d", num_ours);
	} else {
		color_fprintf(s->fp, header_color, LABEL(N_("ahead ")));
		color_fprintf(s->fp, branch_color_local, "%d", num_ours);
		color_fprintf(s->fp, header_color, ", %s", LABEL(N_("behind ")));
		color_fprintf(s->fp, branch_color_remote, "%d", num_theirs);
	}

	color_fprintf(s->fp, header_color, "]");
 conclude:
	fputc(s->null_termination ? '\0' : '\n', s->fp);
}

static void wt_shortstatus_print(struct wt_status *s)
{
	struct string_list_item *it;

	if (s->show_branch)
		wt_shortstatus_print_tracking(s);

	for_each_string_list_item(it, &s->change) {
		struct wt_status_change_data *d = it->util;

		if (d->stagemask)
			wt_shortstatus_unmerged(it, s);
		else
			wt_shortstatus_status(it, s);
	}
	for_each_string_list_item(it, &s->untracked)
		wt_shortstatus_other(it, s, "??");

	for_each_string_list_item(it, &s->ignored)
		wt_shortstatus_other(it, s, "!!");
}

static void wt_porcelain_print(struct wt_status *s)
{
	s->use_color = 0;
	s->relative_paths = 0;
	s->prefix = NULL;
	s->no_gettext = 1;
	wt_shortstatus_print(s);
}

/*
 * Print branch information for porcelain v2 output.  These lines
 * are printed when the '--branch' parameter is given.
 *
 *    # branch.oid <commit><eol>
 *    # branch.head <head><eol>
 *   [# branch.upstream <upstream><eol>
 *   [# branch.ab +<ahead> -<behind><eol>]]
 *
 *      <commit> ::= the current commit hash or the the literal
 *                   "(initial)" to indicate an initialized repo
 *                   with no commits.
 *
 *        <head> ::= <branch_name> the current branch name or
 *                   "(detached)" literal when detached head or
 *                   "(unknown)" when something is wrong.
 *
 *    <upstream> ::= the upstream branch name, when set.
 *
 *       <ahead> ::= integer ahead value or '?'.
 *
 *      <behind> ::= integer behind value or '?'.
 *
 * The end-of-line is defined by the -z flag.
 *
 *                 <eol> ::= NUL when -z,
 *                           LF when NOT -z.
 *
 * When an upstream is set and present, the 'branch.ab' line will
 * be printed with the ahead/behind counts for the branch and the
 * upstream.  When AHEAD_BEHIND_QUICK is requested and the branches
 * are different, '?' will be substituted for the actual count.
 */
static void wt_porcelain_v2_print_tracking(struct wt_status *s)
{
	struct branch *branch;
	const char *base;
	const char *branch_name;
	int ab_info, nr_ahead, nr_behind;
	char eol = s->null_termination ? '\0' : '\n';

	fprintf(s->fp, "# branch.oid %s%c",
			(s->is_initial ? "(initial)" : sha1_to_hex(s->sha1_commit)),
			eol);

	if (!s->branch)
		fprintf(s->fp, "# branch.head %s%c", "(unknown)", eol);
	else {
		if (!strcmp(s->branch, "HEAD")) {
			fprintf(s->fp, "# branch.head %s%c", "(detached)", eol);

			if (s->state.rebase_in_progress ||
			    s->state.rebase_interactive_in_progress)
				branch_name = s->state.onto;
			else if (s->state.detached_from)
				branch_name = s->state.detached_from;
			else
				branch_name = "";
		} else {
			branch_name = NULL;
			skip_prefix(s->branch, "refs/heads/", &branch_name);

			fprintf(s->fp, "# branch.head %s%c", branch_name, eol);
		}

		/* Lookup stats on the upstream tracking branch, if set. */
		branch = branch_get(branch_name);
		base = NULL;
		ab_info = stat_tracking_info(branch, &nr_ahead, &nr_behind,
					     &base, 0, s->ahead_behind_flags);
		if (base) {
			base = shorten_unambiguous_ref(base, 0);
			fprintf(s->fp, "# branch.upstream %s%c", base, eol);
			free((char *)base);

			if (ab_info > 0) {
				/* different */
				if (nr_ahead || nr_behind)
					fprintf(s->fp, "# branch.ab +%d -%d%c",
						nr_ahead, nr_behind, eol);
				else
					fprintf(s->fp, "# branch.ab +? -?%c",
						eol);
			} else if (!ab_info) {
				/* same */
				fprintf(s->fp, "# branch.ab +0 -0%c", eol);
			}
		}
	}
}

/*
 * Convert various submodule status values into a
 * fixed-length string of characters in the buffer provided.
 */
static void wt_porcelain_v2_submodule_state(
	struct wt_status_change_data *d,
	char sub[5])
{
	if (S_ISGITLINK(d->mode_head) ||
		S_ISGITLINK(d->mode_index) ||
		S_ISGITLINK(d->mode_worktree)) {
		sub[0] = 'S';
		sub[1] = d->new_submodule_commits ? 'C' : '.';
		sub[2] = (d->dirty_submodule & DIRTY_SUBMODULE_MODIFIED) ? 'M' : '.';
		sub[3] = (d->dirty_submodule & DIRTY_SUBMODULE_UNTRACKED) ? 'U' : '.';
	} else {
		sub[0] = 'N';
		sub[1] = '.';
		sub[2] = '.';
		sub[3] = '.';
	}
	sub[4] = 0;
}

/*
 * Fix-up changed entries before we print them.
 */
static void wt_porcelain_v2_fix_up_changed(struct string_list_item *it)
{
	struct wt_status_change_data *d = it->util;

	if (!d->index_status) {
		/*
		 * This entry is unchanged in the index (relative to the head).
		 * Therefore, the collect_updated_cb was never called for this
		 * entry (during the head-vs-index scan) and so the head column
		 * fields were never set.
		 *
		 * We must have data for the index column (from the
		 * index-vs-worktree scan (otherwise, this entry should not be
		 * in the list of changes)).
		 *
		 * Copy index column fields to the head column, so that our
		 * output looks complete.
		 */
		assert(d->mode_head == 0);
		d->mode_head = d->mode_index;
		oidcpy(&d->oid_head, &d->oid_index);
	}

	if (!d->worktree_status) {
		/*
		 * This entry is unchanged in the worktree (relative to the index).
		 * Therefore, the collect_changed_cb was never called for this entry
		 * (during the index-vs-worktree scan) and so the worktree column
		 * fields were never set.
		 *
		 * We must have data for the index column (from the head-vs-index
		 * scan).
		 *
		 * Copy the index column fields to the worktree column so that
		 * our output looks complete.
		 *
		 * Note that we only have a mode field in the worktree column
		 * because the scan code tries really hard to not have to compute it.
		 */
		assert(d->mode_worktree == 0);
		d->mode_worktree = d->mode_index;
	}
}

/*
 * Print porcelain v2 info for tracked entries with changes.
 */
static void wt_porcelain_v2_print_changed_entry(
	struct string_list_item *it,
	struct wt_status *s)
{
	struct wt_status_change_data *d = it->util;
	struct strbuf buf = STRBUF_INIT;
	struct strbuf buf_from = STRBUF_INIT;
	const char *path = NULL;
	const char *path_from = NULL;
	char key[3];
	char submodule_token[5];
	char sep_char, eol_char;

	wt_porcelain_v2_fix_up_changed(it);
	wt_porcelain_v2_submodule_state(d, submodule_token);

	key[0] = d->index_status ? d->index_status : '.';
	key[1] = d->worktree_status ? d->worktree_status : '.';
	key[2] = 0;

	if (s->null_termination) {
		/*
		 * In -z mode, we DO NOT C-quote pathnames.  Current path is ALWAYS first.
		 * A single NUL character separates them.
		 */
		sep_char = '\0';
		eol_char = '\0';
		path = it->string;
		path_from = d->rename_source;
	} else {
		/*
		 * Path(s) are C-quoted if necessary. Current path is ALWAYS first.
		 * The source path is only present when necessary.
		 * A single TAB separates them (because paths can contain spaces
		 * which are not escaped and C-quoting does escape TAB characters).
		 */
		sep_char = '\t';
		eol_char = '\n';
		path = quote_path(it->string, s->prefix, &buf);
		if (d->rename_source)
			path_from = quote_path(d->rename_source, s->prefix, &buf_from);
	}

	if (path_from)
		fprintf(s->fp, "2 %s %s %06o %06o %06o %s %s %c%d %s%c%s%c",
				key, submodule_token,
				d->mode_head, d->mode_index, d->mode_worktree,
				oid_to_hex(&d->oid_head), oid_to_hex(&d->oid_index),
				d->rename_status, d->rename_score,
				path, sep_char, path_from, eol_char);
	else
		fprintf(s->fp, "1 %s %s %06o %06o %06o %s %s %s%c",
				key, submodule_token,
				d->mode_head, d->mode_index, d->mode_worktree,
				oid_to_hex(&d->oid_head), oid_to_hex(&d->oid_index),
				path, eol_char);

	strbuf_release(&buf);
	strbuf_release(&buf_from);
}

/*
 * Print porcelain v2 status info for unmerged entries.
 */
static void wt_porcelain_v2_print_unmerged_entry(
	struct string_list_item *it,
	struct wt_status *s)
{
	struct wt_status_change_data *d = it->util;
	struct index_state *istate = s->repo->index;
	const struct cache_entry *ce;
	struct strbuf buf_index = STRBUF_INIT;
	const char *path_index = NULL;
	int pos, stage, sum;
	struct {
		int mode;
		struct object_id oid;
	} stages[3];
	char *key;
	char submodule_token[5];
	char unmerged_prefix = 'u';
	char eol_char = s->null_termination ? '\0' : '\n';

	wt_porcelain_v2_submodule_state(d, submodule_token);

	switch (d->stagemask) {
	case 1: key = "DD"; break; /* both deleted */
	case 2: key = "AU"; break; /* added by us */
	case 3: key = "UD"; break; /* deleted by them */
	case 4: key = "UA"; break; /* added by them */
	case 5: key = "DU"; break; /* deleted by us */
	case 6: key = "AA"; break; /* both added */
	case 7: key = "UU"; break; /* both modified */
	default:
		BUG("unhandled unmerged status %x", d->stagemask);
	}

	/*
	 * Disregard d.aux.porcelain_v2 data that we accumulated
	 * for the head and index columns during the scans and
	 * replace with the actual stage data.
	 *
	 * Note that this is a last-one-wins for each the individual
	 * stage [123] columns in the event of multiple cache entries
	 * for same stage.
	 */
	memset(stages, 0, sizeof(stages));
	sum = 0;
	pos = index_name_pos(istate, it->string, strlen(it->string));
	assert(pos < 0);
	pos = -pos-1;
	while (pos < istate->cache_nr) {
		ce = istate->cache[pos++];
		stage = ce_stage(ce);
		if (strcmp(ce->name, it->string) || !stage)
			break;
		stages[stage - 1].mode = ce->ce_mode;
		oidcpy(&stages[stage - 1].oid, &ce->oid);
		sum |= (1 << (stage - 1));
	}
	if (sum != d->stagemask)
		BUG("observed stagemask 0x%x != expected stagemask 0x%x", sum, d->stagemask);

	if (s->null_termination)
		path_index = it->string;
	else
		path_index = quote_path(it->string, s->prefix, &buf_index);

	fprintf(s->fp, "%c %s %s %06o %06o %06o %06o %s %s %s %s%c",
			unmerged_prefix, key, submodule_token,
			stages[0].mode, /* stage 1 */
			stages[1].mode, /* stage 2 */
			stages[2].mode, /* stage 3 */
			d->mode_worktree,
			oid_to_hex(&stages[0].oid), /* stage 1 */
			oid_to_hex(&stages[1].oid), /* stage 2 */
			oid_to_hex(&stages[2].oid), /* stage 3 */
			path_index,
			eol_char);

	strbuf_release(&buf_index);
}

/*
 * Print porcelain V2 status info for untracked and ignored entries.
 */
static void wt_porcelain_v2_print_other(
	struct string_list_item *it,
	struct wt_status *s,
	char prefix)
{
	struct strbuf buf = STRBUF_INIT;
	const char *path;
	char eol_char;

	if (s->null_termination) {
		path = it->string;
		eol_char = '\0';
	} else {
		path = quote_path(it->string, s->prefix, &buf);
		eol_char = '\n';
	}

	fprintf(s->fp, "%c %s%c", prefix, path, eol_char);

	strbuf_release(&buf);
}

/*
 * Print porcelain V2 status.
 *
 * [<v2_branch>]
 * [<v2_changed_items>]*
 * [<v2_unmerged_items>]*
 * [<v2_untracked_items>]*
 * [<v2_ignored_items>]*
 *
 */
static void wt_porcelain_v2_print(struct wt_status *s)
{
	struct wt_status_change_data *d;
	struct string_list_item *it;
	int i;

	if (s->show_branch)
		wt_porcelain_v2_print_tracking(s);

	for (i = 0; i < s->change.nr; i++) {
		it = &(s->change.items[i]);
		d = it->util;
		if (!d->stagemask)
			wt_porcelain_v2_print_changed_entry(it, s);
	}

	for (i = 0; i < s->change.nr; i++) {
		it = &(s->change.items[i]);
		d = it->util;
		if (d->stagemask)
			wt_porcelain_v2_print_unmerged_entry(it, s);
	}

	for (i = 0; i < s->untracked.nr; i++) {
		it = &(s->untracked.items[i]);
		wt_porcelain_v2_print_other(it, s, '?');
	}

	for (i = 0; i < s->ignored.nr; i++) {
		it = &(s->ignored.items[i]);
		wt_porcelain_v2_print_other(it, s, '!');
	}
}

void wt_status_print(struct wt_status *s)
{
	trace2_data_intmax("status", s->repo, "count/changed", s->change.nr);
	trace2_data_intmax("status", s->repo, "count/untracked",
			   s->untracked.nr);
	trace2_data_intmax("status", s->repo, "count/ignored", s->ignored.nr);

	trace2_region_enter("status", "print", s->repo);

	switch (s->status_format) {
	case STATUS_FORMAT_SHORT:
		wt_shortstatus_print(s);
		break;
	case STATUS_FORMAT_PORCELAIN:
		wt_porcelain_print(s);
		break;
	case STATUS_FORMAT_PORCELAIN_V2:
		wt_porcelain_v2_print(s);
		break;
	case STATUS_FORMAT_UNSPECIFIED:
		BUG("finalize_deferred_config() should have been called");
		break;
	case STATUS_FORMAT_NONE:
	case STATUS_FORMAT_LONG:
		wt_longstatus_print(s);
		break;
	}

	trace2_region_leave("status", "print", s->repo);
}

/**
 * Returns 1 if there are unstaged changes, 0 otherwise.
 */
int has_unstaged_changes(struct repository *r, int ignore_submodules)
{
	struct rev_info rev_info;
	int result;

	repo_init_revisions(r, &rev_info, NULL);
	if (ignore_submodules) {
		rev_info.diffopt.flags.ignore_submodules = 1;
		rev_info.diffopt.flags.override_submodule_config = 1;
	}
	rev_info.diffopt.flags.quick = 1;
	diff_setup_done(&rev_info.diffopt);
	result = run_diff_files(&rev_info, 0);
	return diff_result_code(&rev_info.diffopt, result);
}

/**
 * Returns 1 if there are uncommitted changes, 0 otherwise.
 */
int has_uncommitted_changes(struct repository *r,
			    int ignore_submodules)
{
	struct rev_info rev_info;
	int result;

	if (is_index_unborn(r->index))
		return 0;

	repo_init_revisions(r, &rev_info, NULL);
	if (ignore_submodules)
		rev_info.diffopt.flags.ignore_submodules = 1;
	rev_info.diffopt.flags.quick = 1;

	add_head_to_pending(&rev_info);
	if (!rev_info.pending.nr) {
		/*
		 * We have no head (or it's corrupt); use the empty tree,
		 * which will complain if the index is non-empty.
		 */
		struct tree *tree = lookup_tree(r, the_hash_algo->empty_tree);
		add_pending_object(&rev_info, &tree->object, "");
	}

	diff_setup_done(&rev_info.diffopt);
	result = run_diff_index(&rev_info, 1);
	return diff_result_code(&rev_info.diffopt, result);
}

/**
 * If the work tree has unstaged or uncommitted changes, dies with the
 * appropriate message.
 */
int require_clean_work_tree(struct repository *r,
			    const char *action,
			    const char *hint,
			    int ignore_submodules,
			    int gently)
{
	struct lock_file lock_file = LOCK_INIT;
	int err = 0, fd;

	fd = repo_hold_locked_index(r, &lock_file, 0);
	refresh_index(r->index, REFRESH_QUIET, NULL, NULL, NULL);
	if (0 <= fd)
		repo_update_index_if_able(r, &lock_file);
	rollback_lock_file(&lock_file);

	if (has_unstaged_changes(r, ignore_submodules)) {
		/* TRANSLATORS: the action is e.g. "pull with rebase" */
		error(_("cannot %s: You have unstaged changes."), _(action));
		err = 1;
	}

	if (has_uncommitted_changes(r, ignore_submodules)) {
		if (err)
			error(_("additionally, your index contains uncommitted changes."));
		else
			error(_("cannot %s: Your index contains uncommitted changes."),
			      _(action));
		err = 1;
	}

	if (err) {
		if (hint)
			error("%s", hint);
		if (!gently)
			exit(128);
	}

	return err;
}
