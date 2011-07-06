#include "cache.h"
#include "wt-status.h"
#include "color.h"
#include "object.h"
#include "dir.h"
#include "commit.h"
#include "diff.h"
#include "revision.h"
#include "diffcore.h"
#include "quote.h"
#include "run-command.h"
#include "remote.h"

int wt_status_relative_paths = 1;
int wt_status_use_color = -1;
int wt_status_submodule_summary;
static char wt_status_colors[][COLOR_MAXLEN] = {
	"",         /* WT_STATUS_HEADER: normal */
	"\033[32m", /* WT_STATUS_UPDATED: green */
	"\033[31m", /* WT_STATUS_CHANGED: red */
	"\033[31m", /* WT_STATUS_UNTRACKED: red */
	"\033[31m", /* WT_STATUS_NOBRANCH: red */
};

static const char use_add_msg[] =
"use \"git add <file>...\" to update what will be committed";
static const char use_add_rm_msg[] =
"use \"git add/rm <file>...\" to update what will be committed";
static const char use_add_to_include_msg[] =
"use \"git add <file>...\" to include in what will be committed";
enum untracked_status_type show_untracked_files = SHOW_NORMAL_UNTRACKED_FILES;

static int parse_status_slot(const char *var, int offset)
{
	if (!strcasecmp(var+offset, "header"))
		return WT_STATUS_HEADER;
	if (!strcasecmp(var+offset, "updated")
		|| !strcasecmp(var+offset, "added"))
		return WT_STATUS_UPDATED;
	if (!strcasecmp(var+offset, "changed"))
		return WT_STATUS_CHANGED;
	if (!strcasecmp(var+offset, "untracked"))
		return WT_STATUS_UNTRACKED;
	if (!strcasecmp(var+offset, "nobranch"))
		return WT_STATUS_NOBRANCH;
	die("bad config variable '%s'", var);
}

static const char* color(int slot)
{
	return wt_status_use_color > 0 ? wt_status_colors[slot] : "";
}

void wt_status_prepare(struct wt_status *s)
{
	unsigned char sha1[20];
	const char *head;

	memset(s, 0, sizeof(*s));
	head = resolve_ref("HEAD", sha1, 0, NULL);
	s->branch = head ? xstrdup(head) : NULL;
	s->reference = "HEAD";
	s->fp = stdout;
	s->index_file = get_index_file();
}

static void wt_status_print_cached_header(struct wt_status *s)
{
	const char *c = color(WT_STATUS_HEADER);
	color_fprintf_ln(s->fp, c, "# Changes to be committed:");
	if (!s->is_initial) {
		color_fprintf_ln(s->fp, c, "#   (use \"git reset %s <file>...\" to unstage)", s->reference);
	} else {
		color_fprintf_ln(s->fp, c, "#   (use \"git rm --cached <file>...\" to unstage)");
	}
	color_fprintf_ln(s->fp, c, "#");
}

static void wt_status_print_header(struct wt_status *s,
				   const char *main, const char *sub)
{
	const char *c = color(WT_STATUS_HEADER);
	color_fprintf_ln(s->fp, c, "# %s:", main);
	color_fprintf_ln(s->fp, c, "#   (%s)", sub);
	color_fprintf_ln(s->fp, c, "#");
}

static void wt_status_print_trailer(struct wt_status *s)
{
	color_fprintf_ln(s->fp, color(WT_STATUS_HEADER), "#");
}

#define quote_path quote_path_relative

static void wt_status_print_filepair(struct wt_status *s,
				     int t, struct diff_filepair *p)
{
	const char *c = color(t);
	const char *one, *two;
	struct strbuf onebuf, twobuf;

	strbuf_init(&onebuf, 0);
	strbuf_init(&twobuf, 0);
	one = quote_path(p->one->path, -1, &onebuf, s->prefix);
	two = quote_path(p->two->path, -1, &twobuf, s->prefix);

	color_fprintf(s->fp, color(WT_STATUS_HEADER), "#\t");
	switch (p->status) {
	case DIFF_STATUS_ADDED:
		color_fprintf(s->fp, c, "new file:   %s", one);
		break;
	case DIFF_STATUS_COPIED:
		color_fprintf(s->fp, c, "copied:     %s -> %s", one, two);
		break;
	case DIFF_STATUS_DELETED:
		color_fprintf(s->fp, c, "deleted:    %s", one);
		break;
	case DIFF_STATUS_MODIFIED:
		color_fprintf(s->fp, c, "modified:   %s", one);
		break;
	case DIFF_STATUS_RENAMED:
		color_fprintf(s->fp, c, "renamed:    %s -> %s", one, two);
		break;
	case DIFF_STATUS_TYPE_CHANGED:
		color_fprintf(s->fp, c, "typechange: %s", one);
		break;
	case DIFF_STATUS_UNKNOWN:
		color_fprintf(s->fp, c, "unknown:    %s", one);
		break;
	case DIFF_STATUS_UNMERGED:
		color_fprintf(s->fp, c, "unmerged:   %s", one);
		break;
	default:
		die("bug: unhandled diff status %c", p->status);
	}
	fprintf(s->fp, "\n");
	strbuf_release(&onebuf);
	strbuf_release(&twobuf);
}

static void wt_status_print_updated_cb(struct diff_queue_struct *q,
		struct diff_options *options,
		void *data)
{
	struct wt_status *s = data;
	int shown_header = 0;
	int i;
	for (i = 0; i < q->nr; i++) {
		if (q->queue[i]->status == 'U')
			continue;
		if (!shown_header) {
			wt_status_print_cached_header(s);
			s->commitable = 1;
			shown_header = 1;
		}
		wt_status_print_filepair(s, WT_STATUS_UPDATED, q->queue[i]);
	}
	if (shown_header)
		wt_status_print_trailer(s);
}

static void wt_status_print_changed_cb(struct diff_queue_struct *q,
                        struct diff_options *options,
                        void *data)
{
	struct wt_status *s = data;
	int i;
	if (q->nr) {
		const char *msg = use_add_msg;
		s->workdir_dirty = 1;
		for (i = 0; i < q->nr; i++)
			if (q->queue[i]->status == DIFF_STATUS_DELETED) {
				msg = use_add_rm_msg;
				break;
			}
		wt_status_print_header(s, "Changed but not updated", msg);
	}
	for (i = 0; i < q->nr; i++)
		wt_status_print_filepair(s, WT_STATUS_CHANGED, q->queue[i]);
	if (q->nr)
		wt_status_print_trailer(s);
}

static void wt_status_print_initial(struct wt_status *s)
{
	int i;
	struct strbuf buf;

	strbuf_init(&buf, 0);
	if (active_nr) {
		s->commitable = 1;
		wt_status_print_cached_header(s);
	}
	for (i = 0; i < active_nr; i++) {
		color_fprintf(s->fp, color(WT_STATUS_HEADER), "#\t");
		color_fprintf_ln(s->fp, color(WT_STATUS_UPDATED), "new file: %s",
				quote_path(active_cache[i]->name, -1,
					   &buf, s->prefix));
	}
	if (active_nr)
		wt_status_print_trailer(s);
	strbuf_release(&buf);
}

static void wt_status_print_updated(struct wt_status *s)
{
	struct rev_info rev;
	init_revisions(&rev, NULL);
	setup_revisions(0, NULL, &rev, s->reference);
	rev.diffopt.output_format |= DIFF_FORMAT_CALLBACK;
	rev.diffopt.format_callback = wt_status_print_updated_cb;
	rev.diffopt.format_callback_data = s;
	rev.diffopt.detect_rename = 1;
	rev.diffopt.rename_limit = 200;
	rev.diffopt.break_opt = 0;
	run_diff_index(&rev, 1);
}

static void wt_status_print_changed(struct wt_status *s)
{
	struct rev_info rev;
	init_revisions(&rev, "");
	setup_revisions(0, NULL, &rev, NULL);
	rev.diffopt.output_format |= DIFF_FORMAT_CALLBACK;
	rev.diffopt.format_callback = wt_status_print_changed_cb;
	rev.diffopt.format_callback_data = s;
	run_diff_files(&rev, 0);
}

static void wt_status_print_submodule_summary(struct wt_status *s)
{
	struct child_process sm_summary;
	char summary_limit[64];
	char index[PATH_MAX];
	const char *env[] = { index, NULL };
	const char *argv[] = {
		"submodule",
		"summary",
		"--cached",
		"--for-status",
		"--summary-limit",
		summary_limit,
		s->amend ? "HEAD^" : "HEAD",
		NULL
	};

	sprintf(summary_limit, "%d", wt_status_submodule_summary);
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

static void wt_status_print_untracked(struct wt_status *s)
{
	struct dir_struct dir;
	int i;
	int shown_header = 0;
	struct strbuf buf;

	strbuf_init(&buf, 0);
	memset(&dir, 0, sizeof(dir));

	if (!s->untracked) {
		dir.show_other_directories = 1;
		dir.hide_empty_directories = 1;
	}
	setup_standard_excludes(&dir);

	read_directory(&dir, ".", "", 0, NULL);
	for(i = 0; i < dir.nr; i++) {
		/* check for matching entry, which is unmerged; lifted from
		 * builtin-ls-files:show_other_files */
		struct dir_entry *ent = dir.entries[i];
		int pos = cache_name_pos(ent->name, ent->len);
		struct cache_entry *ce;
		if (0 <= pos)
			die("bug in wt_status_print_untracked");
		pos = -pos - 1;
		if (pos < active_nr) {
			ce = active_cache[pos];
			if (ce_namelen(ce) == ent->len &&
			    !memcmp(ce->name, ent->name, ent->len))
				continue;
		}
		if (!shown_header) {
			s->workdir_untracked = 1;
			wt_status_print_header(s, "Untracked files",
					       use_add_to_include_msg);
			shown_header = 1;
		}
		color_fprintf(s->fp, color(WT_STATUS_HEADER), "#\t");
		color_fprintf_ln(s->fp, color(WT_STATUS_UNTRACKED), "%s",
				quote_path(ent->name, ent->len,
					&buf, s->prefix));
	}
	strbuf_release(&buf);
}

static void wt_status_print_verbose(struct wt_status *s)
{
	struct rev_info rev;

	init_revisions(&rev, NULL);
	setup_revisions(0, NULL, &rev, s->reference);
	rev.diffopt.output_format |= DIFF_FORMAT_PATCH;
	rev.diffopt.detect_rename = 1;
	rev.diffopt.file = s->fp;
	rev.diffopt.close_file = 0;
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
		color_fprintf_ln(s->fp, color(WT_STATUS_HEADER),
				 "# %.*s", (int)(ep - cp), cp);
	color_fprintf_ln(s->fp, color(WT_STATUS_HEADER), "#");
}

void wt_status_print(struct wt_status *s)
{
	unsigned char sha1[20];
	const char *branch_color = color(WT_STATUS_HEADER);

	s->is_initial = get_sha1(s->reference, sha1) ? 1 : 0;
	if (s->branch) {
		const char *on_what = "On branch ";
		const char *branch_name = s->branch;
		if (!prefixcmp(branch_name, "refs/heads/"))
			branch_name += 11;
		else if (!strcmp(branch_name, "HEAD")) {
			branch_name = "";
			branch_color = color(WT_STATUS_NOBRANCH);
			on_what = "Not currently on any branch.";
		}
		color_fprintf(s->fp, color(WT_STATUS_HEADER), "# ");
		color_fprintf_ln(s->fp, branch_color, "%s%s", on_what, branch_name);
		if (!s->is_initial)
			wt_status_print_tracking(s);
	}

	if (s->is_initial) {
		color_fprintf_ln(s->fp, color(WT_STATUS_HEADER), "#");
		color_fprintf_ln(s->fp, color(WT_STATUS_HEADER), "# Initial commit");
		color_fprintf_ln(s->fp, color(WT_STATUS_HEADER), "#");
		wt_status_print_initial(s);
	}
	else {
		wt_status_print_updated(s);
	}

	wt_status_print_changed(s);
	if (wt_status_submodule_summary)
		wt_status_print_submodule_summary(s);
	if (show_untracked_files)
		wt_status_print_untracked(s);
	else if (s->commitable)
		 fprintf(s->fp, "# Untracked files not listed (use -u option to show untracked files)\n");

	if (s->verbose && !s->is_initial)
		wt_status_print_verbose(s);
	if (!s->commitable) {
		if (s->amend)
			fprintf(s->fp, "# No changes\n");
		else if (s->nowarn)
			; /* nothing */
		else if (s->workdir_dirty)
			printf("no changes added to commit (use \"git add\" and/or \"git commit -a\")\n");
		else if (s->workdir_untracked)
			printf("nothing added to commit but untracked files present (use \"git add\" to track)\n");
		else if (s->is_initial)
			printf("nothing to commit (create/copy files and use \"git add\" to track)\n");
		else if (!show_untracked_files)
			printf("nothing to commit (use -u to show untracked files)\n");
		else
			printf("nothing to commit (working directory clean)\n");
	}
}

int git_status_config(const char *k, const char *v, void *cb)
{
	if (!strcmp(k, "status.submodulesummary")) {
		int is_bool;
		wt_status_submodule_summary = git_config_bool_or_int(k, v, &is_bool);
		if (is_bool && wt_status_submodule_summary)
			wt_status_submodule_summary = -1;
		return 0;
	}
	if (!strcmp(k, "status.color") || !strcmp(k, "color.status")) {
		wt_status_use_color = git_config_colorbool(k, v, -1);
		return 0;
	}
	if (!prefixcmp(k, "status.color.") || !prefixcmp(k, "color.status.")) {
		int slot = parse_status_slot(k, 13);
		if (!v)
			return config_error_nonbool(k);
		color_parse(v, k, wt_status_colors[slot]);
		return 0;
	}
	if (!strcmp(k, "status.relativepaths")) {
		wt_status_relative_paths = git_config_bool(k, v);
		return 0;
	}
	if (!strcmp(k, "status.showuntrackedfiles")) {
		if (!v)
			return config_error_nonbool(k);
		else if (!strcmp(v, "no"))
			show_untracked_files = SHOW_NO_UNTRACKED_FILES;
		else if (!strcmp(v, "normal"))
			show_untracked_files = SHOW_NORMAL_UNTRACKED_FILES;
		else if (!strcmp(v, "all"))
			show_untracked_files = SHOW_ALL_UNTRACKED_FILES;
		else
			return error("Invalid untracked files mode '%s'", v);
		return 0;
	}
	return git_color_default_config(k, v, cb);
}
