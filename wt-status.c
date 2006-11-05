#include "wt-status.h"
#include "color.h"
#include "cache.h"
#include "object.h"
#include "dir.h"
#include "commit.h"
#include "diff.h"
#include "revision.h"
#include "diffcore.h"

int wt_status_use_color = 0;
static char wt_status_colors[][COLOR_MAXLEN] = {
	"",         /* WT_STATUS_HEADER: normal */
	"\033[32m", /* WT_STATUS_UPDATED: green */
	"\033[31m", /* WT_STATUS_CHANGED: red */
	"\033[31m", /* WT_STATUS_UNTRACKED: red */
};

static int parse_status_slot(const char *var, int offset)
{
	if (!strcasecmp(var+offset, "header"))
		return WT_STATUS_HEADER;
	if (!strcasecmp(var+offset, "updated"))
		return WT_STATUS_UPDATED;
	if (!strcasecmp(var+offset, "changed"))
		return WT_STATUS_CHANGED;
	if (!strcasecmp(var+offset, "untracked"))
		return WT_STATUS_UNTRACKED;
	die("bad config variable '%s'", var);
}

static const char* color(int slot)
{
	return wt_status_use_color ? wt_status_colors[slot] : "";
}

void wt_status_prepare(struct wt_status *s)
{
	unsigned char sha1[20];
	const char *head;

	s->is_initial = get_sha1("HEAD", sha1) ? 1 : 0;

	head = resolve_ref(git_path("HEAD"), sha1, 0);
	s->branch = head ?
		    strdup(head + strlen(get_git_dir()) + 1) :
		    NULL;

	s->reference = "HEAD";
	s->amend = 0;
	s->verbose = 0;
	s->commitable = 0;
	s->untracked = 0;
}

static void wt_status_print_header(const char *main, const char *sub)
{
	const char *c = color(WT_STATUS_HEADER);
	color_printf_ln(c, "# %s:", main);
	color_printf_ln(c, "#   (%s)", sub);
	color_printf_ln(c, "#");
}

static void wt_status_print_trailer(void)
{
	color_printf_ln(color(WT_STATUS_HEADER), "#");
}

static void wt_status_print_filepair(int t, struct diff_filepair *p)
{
	const char *c = color(t);
	color_printf(color(WT_STATUS_HEADER), "#\t");
	switch (p->status) {
	case DIFF_STATUS_ADDED:
		color_printf(c, "new file: %s", p->one->path); break;
	case DIFF_STATUS_COPIED:
		color_printf(c, "copied: %s -> %s",
				p->one->path, p->two->path);
		break;
	case DIFF_STATUS_DELETED:
		color_printf(c, "deleted: %s", p->one->path); break;
	case DIFF_STATUS_MODIFIED:
		color_printf(c, "modified: %s", p->one->path); break;
	case DIFF_STATUS_RENAMED:
		color_printf(c, "renamed: %s -> %s",
				p->one->path, p->two->path);
		break;
	case DIFF_STATUS_TYPE_CHANGED:
		color_printf(c, "typechange: %s", p->one->path); break;
	case DIFF_STATUS_UNKNOWN:
		color_printf(c, "unknown: %s", p->one->path); break;
	case DIFF_STATUS_UNMERGED:
		color_printf(c, "unmerged: %s", p->one->path); break;
	default:
		die("bug: unhandled diff status %c", p->status);
	}
	printf("\n");
}

static void wt_status_print_updated_cb(struct diff_queue_struct *q,
		struct diff_options *options,
		void *data)
{
	struct wt_status *s = data;
	int shown_header = 0;
	int i;
	if (q->nr) {
	}
	for (i = 0; i < q->nr; i++) {
		if (q->queue[i]->status == 'U')
			continue;
		if (!shown_header) {
			wt_status_print_header("Updated but not checked in",
					"will commit");
			s->commitable = 1;
			shown_header = 1;
		}
		wt_status_print_filepair(WT_STATUS_UPDATED, q->queue[i]);
	}
	if (shown_header)
		wt_status_print_trailer();
}

static void wt_status_print_changed_cb(struct diff_queue_struct *q,
                        struct diff_options *options,
                        void *data)
{
	int i;
	if (q->nr)
		wt_status_print_header("Changed but not updated",
				"use git-update-index to mark for commit");
	for (i = 0; i < q->nr; i++)
		wt_status_print_filepair(WT_STATUS_CHANGED, q->queue[i]);
	if (q->nr)
		wt_status_print_trailer();
}

void wt_status_print_initial(struct wt_status *s)
{
	int i;
	read_cache();
	if (active_nr) {
		s->commitable = 1;
		wt_status_print_header("Updated but not checked in",
				"will commit");
	}
	for (i = 0; i < active_nr; i++) {
		color_printf(color(WT_STATUS_HEADER), "#\t");
		color_printf_ln(color(WT_STATUS_UPDATED), "new file: %s",
				active_cache[i]->name);
	}
	if (active_nr)
		wt_status_print_trailer();
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

static void wt_status_print_untracked(const struct wt_status *s)
{
	struct dir_struct dir;
	const char *x;
	int i;
	int shown_header = 0;

	memset(&dir, 0, sizeof(dir));

	dir.exclude_per_dir = ".gitignore";
	if (!s->untracked) {
		dir.show_other_directories = 1;
		dir.hide_empty_directories = 1;
	}
	x = git_path("info/exclude");
	if (file_exists(x))
		add_excludes_from_file(&dir, x);

	read_directory(&dir, ".", "", 0);
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
			wt_status_print_header("Untracked files",
				"use \"git add\" to add to commit");
			shown_header = 1;
		}
		color_printf(color(WT_STATUS_HEADER), "#\t");
		color_printf_ln(color(WT_STATUS_UNTRACKED), "%.*s",
				ent->len, ent->name);
	}
}

static void wt_status_print_verbose(struct wt_status *s)
{
	struct rev_info rev;
	init_revisions(&rev, NULL);
	setup_revisions(0, NULL, &rev, s->reference);
	rev.diffopt.output_format |= DIFF_FORMAT_PATCH;
	rev.diffopt.detect_rename = 1;
	run_diff_index(&rev, 1);
}

void wt_status_print(struct wt_status *s)
{
	if (s->branch && strcmp(s->branch, "refs/heads/master"))
		color_printf_ln(color(WT_STATUS_HEADER),
			"# On branch %s", s->branch);

	if (s->is_initial) {
		color_printf_ln(color(WT_STATUS_HEADER), "#");
		color_printf_ln(color(WT_STATUS_HEADER), "# Initial commit");
		color_printf_ln(color(WT_STATUS_HEADER), "#");
		wt_status_print_initial(s);
	}
	else {
		wt_status_print_updated(s);
		discard_cache();
	}

	wt_status_print_changed(s);
	wt_status_print_untracked(s);

	if (s->verbose && !s->is_initial)
		wt_status_print_verbose(s);
	if (!s->commitable)
		printf("%s\n", s->amend ? "# No changes" : "nothing to commit");
}

int git_status_config(const char *k, const char *v)
{
	if (!strcmp(k, "status.color")) {
		wt_status_use_color = git_config_colorbool(k, v);
		return 0;
	}
	if (!strncmp(k, "status.color.", 13)) {
		int slot = parse_status_slot(k, 13);
		color_parse(v, k, wt_status_colors[slot]);
	}
	return git_default_config(k, v);
}
