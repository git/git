#include "cache.h"
#include "add-interactive.h"
#include "strbuf.h"
#include "run-command.h"
#include "argv-array.h"
#include "pathspec.h"
#include "color.h"
#include "diff.h"
#include "compat/terminal.h"

enum prompt_mode_type {
	PROMPT_MODE_CHANGE = 0, PROMPT_DELETION, PROMPT_HUNK
};

struct patch_mode {
	const char *diff[4], *apply[4], *apply_check[4];
	unsigned is_reverse:1, apply_for_checkout:1;
	const char *prompt_mode[PROMPT_HUNK + 1];
	const char *edit_hunk_hint, *help_patch_text;
};

static struct patch_mode patch_mode_stage = {
	.diff = { "diff-files", NULL },
	.apply = { "--cached", NULL },
	.apply_check = { "--cached", NULL },
	.is_reverse = 0,
	.prompt_mode = {
		N_("Stage mode change [y,n,q,a,d%s,?]? "),
		N_("Stage deletion [y,n,q,a,d%s,?]? "),
		N_("Stage this hunk [y,n,q,a,d%s,?]? ")
	},
	.edit_hunk_hint = N_("If the patch applies cleanly, the edited hunk "
			     "will immediately be marked for staging."),
	.help_patch_text =
		N_("y - stage this hunk\n"
		   "n - do not stage this hunk\n"
		   "q - quit; do not stage this hunk or any of the remaining "
			"ones\n"
		   "a - stage this hunk and all later hunks in the file\n"
		   "d - do not stage this hunk or any of the later hunks in "
			"the file\n")
};

static struct patch_mode patch_mode_stash = {
	.diff = { "diff-index", "HEAD", NULL },
	.apply = { "--cached", NULL },
	.apply_check = { "--cached", NULL },
	.is_reverse = 0,
	.prompt_mode = {
		N_("Stash mode change [y,n,q,a,d%s,?]? "),
		N_("Stash deletion [y,n,q,a,d%s,?]? "),
		N_("Stash this hunk [y,n,q,a,d%s,?]? "),
	},
	.edit_hunk_hint = N_("If the patch applies cleanly, the edited hunk "
			     "will immediately be marked for stashing."),
	.help_patch_text =
		N_("y - stash this hunk\n"
		   "n - do not stash this hunk\n"
		   "q - quit; do not stash this hunk or any of the remaining "
			"ones\n"
		   "a - stash this hunk and all later hunks in the file\n"
		   "d - do not stash this hunk or any of the later hunks in "
			"the file\n"),
};

static struct patch_mode patch_mode_reset_head = {
	.diff = { "diff-index", "--cached", NULL },
	.apply = { "-R", "--cached", NULL },
	.apply_check = { "-R", "--cached", NULL },
	.is_reverse = 1,
	.prompt_mode = {
		N_("Unstage mode change [y,n,q,a,d%s,?]? "),
		N_("Unstage deletion [y,n,q,a,d%s,?]? "),
		N_("Unstage this hunk [y,n,q,a,d%s,?]? "),
	},
	.edit_hunk_hint = N_("If the patch applies cleanly, the edited hunk "
			     "will immediately be marked for unstaging."),
	.help_patch_text =
		N_("y - unstage this hunk\n"
		   "n - do not unstage this hunk\n"
		   "q - quit; do not unstage this hunk or any of the remaining "
			"ones\n"
		   "a - unstage this hunk and all later hunks in the file\n"
		   "d - do not unstage this hunk or any of the later hunks in "
			"the file\n"),
};

static struct patch_mode patch_mode_reset_nothead = {
	.diff = { "diff-index", "-R", "--cached", NULL },
	.apply = { "--cached", NULL },
	.apply_check = { "--cached", NULL },
	.is_reverse = 0,
	.prompt_mode = {
		N_("Apply mode change to index [y,n,q,a,d%s,?]? "),
		N_("Apply deletion to index [y,n,q,a,d%s,?]? "),
		N_("Apply this hunk to index [y,n,q,a,d%s,?]? "),
	},
	.edit_hunk_hint = N_("If the patch applies cleanly, the edited hunk "
			     "will immediately be marked for applying."),
	.help_patch_text =
		N_("y - apply this hunk to index\n"
		   "n - do not apply this hunk to index\n"
		   "q - quit; do not apply this hunk or any of the remaining "
			"ones\n"
		   "a - apply this hunk and all later hunks in the file\n"
		   "d - do not apply this hunk or any of the later hunks in "
			"the file\n"),
};

static struct patch_mode patch_mode_checkout_index = {
	.diff = { "diff-files", NULL },
	.apply = { "-R", NULL },
	.apply_check = { "-R", NULL },
	.is_reverse = 1,
	.prompt_mode = {
		N_("Discard mode change from worktree [y,n,q,a,d%s,?]? "),
		N_("Discard deletion from worktree [y,n,q,a,d%s,?]? "),
		N_("Discard this hunk from worktree [y,n,q,a,d%s,?]? "),
	},
	.edit_hunk_hint = N_("If the patch applies cleanly, the edited hunk "
			     "will immediately be marked for discarding."),
	.help_patch_text =
		N_("y - discard this hunk from worktree\n"
		   "n - do not discard this hunk from worktree\n"
		   "q - quit; do not discard this hunk or any of the remaining "
			"ones\n"
		   "a - discard this hunk and all later hunks in the file\n"
		   "d - do not discard this hunk or any of the later hunks in "
			"the file\n"),
};

static struct patch_mode patch_mode_checkout_head = {
	.diff = { "diff-index", NULL },
	.apply_for_checkout = 1,
	.apply_check = { "-R", NULL },
	.is_reverse = 1,
	.prompt_mode = {
		N_("Discard mode change from index and worktree [y,n,q,a,d%s,?]? "),
		N_("Discard deletion from index and worktree [y,n,q,a,d%s,?]? "),
		N_("Discard this hunk from index and worktree [y,n,q,a,d%s,?]? "),
	},
	.edit_hunk_hint = N_("If the patch applies cleanly, the edited hunk "
			     "will immediately be marked for discarding."),
	.help_patch_text =
		N_("y - discard this hunk from index and worktree\n"
		   "n - do not discard this hunk from index and worktree\n"
		   "q - quit; do not discard this hunk or any of the remaining "
			"ones\n"
		   "a - discard this hunk and all later hunks in the file\n"
		   "d - do not discard this hunk or any of the later hunks in "
			"the file\n"),
};

static struct patch_mode patch_mode_checkout_nothead = {
	.diff = { "diff-index", "-R", NULL },
	.apply_for_checkout = 1,
	.apply_check = { NULL },
	.is_reverse = 0,
	.prompt_mode = {
		N_("Apply mode change to index and worktree [y,n,q,a,d%s,?]? "),
		N_("Apply deletion to index and worktree [y,n,q,a,d%s,?]? "),
		N_("Apply this hunk to index and worktree [y,n,q,a,d%s,?]? "),
	},
	.edit_hunk_hint = N_("If the patch applies cleanly, the edited hunk "
			     "will immediately be marked for applying."),
	.help_patch_text =
		N_("y - apply this hunk to index and worktree\n"
		   "n - do not apply this hunk to index and worktree\n"
		   "q - quit; do not apply this hunk or any of the remaining "
			"ones\n"
		   "a - apply this hunk and all later hunks in the file\n"
		   "d - do not apply this hunk or any of the later hunks in "
			"the file\n"),
};

static struct patch_mode patch_mode_worktree_head = {
	.diff = { "diff-index", NULL },
	.apply = { "-R", NULL },
	.apply_check = { "-R", NULL },
	.is_reverse = 1,
	.prompt_mode = {
		N_("Discard mode change from index and worktree [y,n,q,a,d%s,?]? "),
		N_("Discard deletion from index and worktree [y,n,q,a,d%s,?]? "),
		N_("Discard this hunk from index and worktree [y,n,q,a,d%s,?]? "),
	},
	.edit_hunk_hint = N_("If the patch applies cleanly, the edited hunk "
			     "will immediately be marked for discarding."),
	.help_patch_text =
		N_("y - discard this hunk from worktree\n"
		   "n - do not discard this hunk from worktree\n"
		   "q - quit; do not discard this hunk or any of the remaining "
			"ones\n"
		   "a - discard this hunk and all later hunks in the file\n"
		   "d - do not discard this hunk or any of the later hunks in "
			"the file\n"),
};

static struct patch_mode patch_mode_worktree_nothead = {
	.diff = { "diff-index", "-R", NULL },
	.apply = { NULL },
	.apply_check = { NULL },
	.is_reverse = 0,
	.prompt_mode = {
		N_("Apply mode change to index and worktree [y,n,q,a,d%s,?]? "),
		N_("Apply deletion to index and worktree [y,n,q,a,d%s,?]? "),
		N_("Apply this hunk to index and worktree [y,n,q,a,d%s,?]? "),
	},
	.edit_hunk_hint = N_("If the patch applies cleanly, the edited hunk "
			     "will immediately be marked for applying."),
	.help_patch_text =
		N_("y - apply this hunk to worktree\n"
		   "n - do not apply this hunk to worktree\n"
		   "q - quit; do not apply this hunk or any of the remaining "
			"ones\n"
		   "a - apply this hunk and all later hunks in the file\n"
		   "d - do not apply this hunk or any of the later hunks in "
			"the file\n"),
};

struct hunk_header {
	unsigned long old_offset, old_count, new_offset, new_count;
	/*
	 * Start/end offsets to the extra text after the second `@@` in the
	 * hunk header, e.g. the function signature. This is expected to
	 * include the newline.
	 */
	size_t extra_start, extra_end, colored_extra_start, colored_extra_end;
};

struct hunk {
	size_t start, end, colored_start, colored_end, splittable_into;
	ssize_t delta;
	enum { UNDECIDED_HUNK = 0, SKIP_HUNK, USE_HUNK } use;
	struct hunk_header header;
};

struct add_p_state {
	struct add_i_state s;
	struct strbuf answer, buf;

	/* parsed diff */
	struct strbuf plain, colored;
	struct file_diff {
		struct hunk head;
		struct hunk *hunk;
		size_t hunk_nr, hunk_alloc;
		unsigned deleted:1, mode_change:1,binary:1;
	} *file_diff;
	size_t file_diff_nr;

	/* patch mode */
	struct patch_mode *mode;
	const char *revision;
};

static void err(struct add_p_state *s, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	fputs(s->s.error_color, stderr);
	vfprintf(stderr, fmt, args);
	fputs(s->s.reset_color, stderr);
	fputc('\n', stderr);
	va_end(args);
}

static void setup_child_process(struct child_process *cp,
				struct add_p_state *s, ...)
{
	va_list ap;
	const char *arg;

	va_start(ap, s);
	while((arg = va_arg(ap, const char *)))
		argv_array_push(&cp->args, arg);
	va_end(ap);

	cp->git_cmd = 1;
	argv_array_pushf(&cp->env_array,
			 INDEX_ENVIRONMENT "=%s", s->s.r->index_file);
}

static int parse_range(const char **p,
		       unsigned long *offset, unsigned long *count)
{
	char *pend;

	*offset = strtoul(*p, &pend, 10);
	if (pend == *p)
		return -1;
	if (*pend != ',') {
		*count = 1;
		*p = pend;
		return 0;
	}
	*count = strtoul(pend + 1, (char **)p, 10);
	return *p == pend + 1 ? -1 : 0;
}

static int parse_hunk_header(struct add_p_state *s, struct hunk *hunk)
{
	struct hunk_header *header = &hunk->header;
	const char *line = s->plain.buf + hunk->start, *p = line;
	char *eol = memchr(p, '\n', s->plain.len - hunk->start);

	if (!eol)
		eol = s->plain.buf + s->plain.len;

	if (!skip_prefix(p, "@@ -", &p) ||
	    parse_range(&p, &header->old_offset, &header->old_count) < 0 ||
	    !skip_prefix(p, " +", &p) ||
	    parse_range(&p, &header->new_offset, &header->new_count) < 0 ||
	    !skip_prefix(p, " @@", &p))
		return error(_("could not parse hunk header '%.*s'"),
			     (int)(eol - line), line);

	hunk->start = eol - s->plain.buf + (*eol == '\n');
	header->extra_start = p - s->plain.buf;
	header->extra_end = hunk->start;

	if (!s->colored.len) {
		header->colored_extra_start = header->colored_extra_end = 0;
		return 0;
	}

	/* Now find the extra text in the colored diff */
	line = s->colored.buf + hunk->colored_start;
	eol = memchr(line, '\n', s->colored.len - hunk->colored_start);
	if (!eol)
		eol = s->colored.buf + s->colored.len;
	p = memmem(line, eol - line, "@@ -", 4);
	if (!p)
		return error(_("could not parse colored hunk header '%.*s'"),
			     (int)(eol - line), line);
	p = memmem(p + 4, eol - p - 4, " @@", 3);
	if (!p)
		return error(_("could not parse colored hunk header '%.*s'"),
			     (int)(eol - line), line);
	hunk->colored_start = eol - s->colored.buf + (*eol == '\n');
	header->colored_extra_start = p + 3 - s->colored.buf;
	header->colored_extra_end = hunk->colored_start;

	return 0;
}

static int is_octal(const char *p, size_t len)
{
	while (len--)
		if (*p < '0' || *(p++) > '7')
			return 0;
	return 1;
}

static int parse_diff(struct add_p_state *s, const struct pathspec *ps)
{
	struct argv_array args = ARGV_ARRAY_INIT;
	const char *diff_algorithm = s->s.interactive_diff_algorithm;
	struct strbuf *plain = &s->plain, *colored = NULL;
	struct child_process cp = CHILD_PROCESS_INIT;
	char *p, *pend, *colored_p = NULL, *colored_pend = NULL, marker = '\0';
	size_t file_diff_alloc = 0, i, color_arg_index;
	struct file_diff *file_diff = NULL;
	struct hunk *hunk = NULL;
	int res;

	argv_array_pushv(&args, s->mode->diff);
	if (diff_algorithm)
		argv_array_pushf(&args, "--diff-algorithm=%s", diff_algorithm);
	if (s->revision) {
		struct object_id oid;
		argv_array_push(&args,
				/* could be on an unborn branch */
				!strcmp("HEAD", s->revision) &&
				get_oid("HEAD", &oid) ?
				empty_tree_oid_hex() : s->revision);
	}
	color_arg_index = args.argc;
	/* Use `--no-color` explicitly, just in case `diff.color = always`. */
	argv_array_pushl(&args, "--no-color", "-p", "--", NULL);
	for (i = 0; i < ps->nr; i++)
		argv_array_push(&args, ps->items[i].original);

	setup_child_process(&cp, s, NULL);
	cp.argv = args.argv;
	res = capture_command(&cp, plain, 0);
	if (res) {
		argv_array_clear(&args);
		return error(_("could not parse diff"));
	}
	if (!plain->len) {
		argv_array_clear(&args);
		return 0;
	}
	strbuf_complete_line(plain);

	if (want_color_fd(1, -1)) {
		struct child_process colored_cp = CHILD_PROCESS_INIT;
		const char *diff_filter = s->s.interactive_diff_filter;

		setup_child_process(&colored_cp, s, NULL);
		xsnprintf((char *)args.argv[color_arg_index], 8, "--color");
		colored_cp.argv = args.argv;
		colored = &s->colored;
		res = capture_command(&colored_cp, colored, 0);
		argv_array_clear(&args);
		if (res)
			return error(_("could not parse colored diff"));

		if (diff_filter) {
			struct child_process filter_cp = CHILD_PROCESS_INIT;

			setup_child_process(&filter_cp, s,
					    diff_filter, NULL);
			filter_cp.git_cmd = 0;
			filter_cp.use_shell = 1;
			strbuf_reset(&s->buf);
			if (pipe_command(&filter_cp,
					 colored->buf, colored->len,
					 &s->buf, colored->len,
					 NULL, 0) < 0)
				return error(_("failed to run '%s'"),
					     diff_filter);
			strbuf_swap(colored, &s->buf);
		}

		strbuf_complete_line(colored);
		colored_p = colored->buf;
		colored_pend = colored_p + colored->len;
	}
	argv_array_clear(&args);

	/* parse files and hunks */
	p = plain->buf;
	pend = p + plain->len;
	while (p != pend) {
		char *eol = memchr(p, '\n', pend - p);
		const char *deleted = NULL, *mode_change = NULL;

		if (!eol)
			eol = pend;

		if (starts_with(p, "diff ")) {
			s->file_diff_nr++;
			ALLOC_GROW(s->file_diff, s->file_diff_nr,
				   file_diff_alloc);
			file_diff = s->file_diff + s->file_diff_nr - 1;
			memset(file_diff, 0, sizeof(*file_diff));
			hunk = &file_diff->head;
			hunk->start = p - plain->buf;
			if (colored_p)
				hunk->colored_start = colored_p - colored->buf;
		} else if (p == plain->buf)
			BUG("diff starts with unexpected line:\n"
			    "%.*s\n", (int)(eol - p), p);
		else if (file_diff->deleted)
			; /* keep the rest of the file in a single "hunk" */
		else if (starts_with(p, "@@ ") ||
			 (hunk == &file_diff->head &&
			  skip_prefix(p, "deleted file", &deleted))) {
			if (marker == '-' || marker == '+')
				/*
				 * Should not happen; previous hunk did not end
				 * in a context line? Handle it anyway.
				 */
				hunk->splittable_into++;

			file_diff->hunk_nr++;
			ALLOC_GROW(file_diff->hunk, file_diff->hunk_nr,
				   file_diff->hunk_alloc);
			hunk = file_diff->hunk + file_diff->hunk_nr - 1;
			memset(hunk, 0, sizeof(*hunk));

			hunk->start = p - plain->buf;
			if (colored)
				hunk->colored_start = colored_p - colored->buf;

			if (deleted)
				file_diff->deleted = 1;
			else if (parse_hunk_header(s, hunk) < 0)
				return -1;

			/*
			 * Start counting into how many hunks this one can be
			 * split
			 */
			marker = *p;
		} else if (hunk == &file_diff->head &&
			   ((skip_prefix(p, "old mode ", &mode_change) ||
			     skip_prefix(p, "new mode ", &mode_change)) &&
			    is_octal(mode_change, eol - mode_change))) {
			if (!file_diff->mode_change) {
				if (file_diff->hunk_nr++)
					BUG("mode change before first hunk");
				ALLOC_GROW(file_diff->hunk, file_diff->hunk_nr,
					   file_diff->hunk_alloc);
				memset(file_diff->hunk, 0, sizeof(struct hunk));
				file_diff->hunk->start = p - plain->buf;
				if (colored_p)
					file_diff->hunk->colored_start =
						colored_p - colored->buf;
				file_diff->mode_change = 1;
			} else if (file_diff->hunk_nr != 1)
				BUG("mode change after first hunk?");
		} else if (hunk == &file_diff->head &&
			   starts_with(p, "Binary files "))
			file_diff->binary = 1;

		if (file_diff->deleted && file_diff->mode_change)
			BUG("diff contains delete *and* a mode change?!?\n%.*s",
			    (int)(eol - (plain->buf + file_diff->head.start)),
			    plain->buf + file_diff->head.start);

		if ((marker == '-' || marker == '+') &&
		    (*p == ' ' || *p == '\\'))
			hunk->splittable_into++;
		if (marker)
			marker = *p;

		p = eol == pend ? pend : eol + 1;
		hunk->end = p - plain->buf;

		if (colored) {
			char *colored_eol = memchr(colored_p, '\n',
						   colored_pend - colored_p);
			if (colored_eol)
				colored_p = colored_eol + 1;
			else if (p != pend)
				/* colored shorter than non-colored? */
				goto mismatched_output;
			else
				colored_p = colored_pend;

			hunk->colored_end = colored_p - colored->buf;
		}

		if (mode_change) {
			file_diff->hunk->end = hunk->end;
			if (colored_p)
				file_diff->hunk->colored_end =
					hunk->colored_end;
		}
	}

	if (marker == '-' || marker == '+')
		/*
		 * Last hunk ended in non-context line (i.e. it appended lines
		 * to the file, so there are no trailing context lines).
		 */
		hunk->splittable_into++;

	/* non-colored shorter than colored? */
	if (colored_p != colored_pend) {
mismatched_output:
		error(_("mismatched output from interactive.diffFilter"));
		advise(_("Your filter must maintain a one-to-one correspondence\n"
			 "between its input and output lines."));
		return -1;
	}

	return 0;
}

static size_t find_next_line(struct strbuf *sb, size_t offset)
{
	char *eol = memchr(sb->buf + offset, '\n', sb->len - offset);

	if (!eol)
		return sb->len;
	return eol - sb->buf + 1;
}

static void render_hunk(struct add_p_state *s, struct hunk *hunk,
			ssize_t delta, int colored, struct strbuf *out)
{
	struct hunk_header *header = &hunk->header;

	if (hunk->header.old_offset != 0 || hunk->header.new_offset != 0) {
		/*
		 * Generate the hunk header dynamically, except for special
		 * hunks (such as the diff header).
		 */
		const char *p;
		size_t len;
		unsigned long old_offset = header->old_offset;
		unsigned long new_offset = header->new_offset;

		if (!colored) {
			p = s->plain.buf + header->extra_start;
			len = header->extra_end - header->extra_start;
		} else {
			strbuf_addstr(out, s->s.fraginfo_color);
			p = s->colored.buf + header->colored_extra_start;
			len = header->colored_extra_end
				- header->colored_extra_start;
		}

		if (s->mode->is_reverse)
			old_offset -= delta;
		else
			new_offset += delta;

		strbuf_addf(out, "@@ -%lu,%lu +%lu,%lu @@",
			    old_offset, header->old_count,
			    new_offset, header->new_count);
		if (len)
			strbuf_add(out, p, len);
		else if (colored)
			strbuf_addf(out, "%s\n", GIT_COLOR_RESET);
		else
			strbuf_addch(out, '\n');
	}

	if (colored)
		strbuf_add(out, s->colored.buf + hunk->colored_start,
			   hunk->colored_end - hunk->colored_start);
	else
		strbuf_add(out, s->plain.buf + hunk->start,
			   hunk->end - hunk->start);
}

static void render_diff_header(struct add_p_state *s,
			       struct file_diff *file_diff, int colored,
			       struct strbuf *out)
{
	/*
	 * If there was a mode change, the first hunk is a pseudo hunk that
	 * corresponds to the mode line in the header. If the user did not want
	 * to stage that "hunk", we actually have to cut it out from the header.
	 */
	int skip_mode_change =
		file_diff->mode_change && file_diff->hunk->use != USE_HUNK;
	struct hunk *head = &file_diff->head, *first = file_diff->hunk;

	if (!skip_mode_change) {
		render_hunk(s, head, 0, colored, out);
		return;
	}

	if (colored) {
		const char *p = s->colored.buf;

		strbuf_add(out, p + head->colored_start,
			    first->colored_start - head->colored_start);
		strbuf_add(out, p + first->colored_end,
			    head->colored_end - first->colored_end);
	} else {
		const char *p = s->plain.buf;

		strbuf_add(out, p + head->start, first->start - head->start);
		strbuf_add(out, p + first->end, head->end - first->end);
	}
}

/* Coalesce hunks again that were split */
static int merge_hunks(struct add_p_state *s, struct file_diff *file_diff,
		       size_t *hunk_index, int use_all, struct hunk *temp)
{
	size_t i = *hunk_index, delta;
	struct hunk *hunk = file_diff->hunk + i;
	struct hunk_header *header = &temp->header, *next;

	if (!use_all && hunk->use != USE_HUNK)
		return 0;

	memcpy(temp, hunk, sizeof(*temp));
	/* We simply skip the colored part (if any) when merging hunks */
	temp->colored_start = temp->colored_end = 0;

	for (; i + 1 < file_diff->hunk_nr; i++) {
		hunk++;
		next = &hunk->header;

		if ((!use_all && hunk->use != USE_HUNK) ||
		    header->new_offset >= next->new_offset + temp->delta ||
		    header->new_offset + header->new_count
		    < next->new_offset + temp->delta)
			break;

		if (temp->start < hunk->start && temp->end > hunk->start) {
			temp->end = hunk->end;
			temp->colored_end = hunk->colored_end;
			delta = 0;
		} else {
			const char *plain = s->plain.buf;
			size_t  overlapping_line_count = header->new_offset
				+ header->new_count - temp->delta
				- next->new_offset;
			size_t overlap_end = hunk->start;
			size_t overlap_start = overlap_end;
			size_t overlap_next, len, i;

			/*
			 * One of the hunks was edited; let's ensure that at
			 * least the last context line of the first hunk
			 * overlaps with the corresponding line of the second
			 * hunk, and then merge.
			 */

			for (i = 0; i < overlapping_line_count; i++) {
				overlap_next = find_next_line(&s->plain,
							      overlap_end);

				if (overlap_next > hunk->end)
					BUG("failed to find %d context lines "
					    "in:\n%.*s",
					    (int)overlapping_line_count,
					    (int)(hunk->end - hunk->start),
					    plain + hunk->start);

				if (plain[overlap_end] != ' ')
					return error(_("expected context line "
						       "#%d in\n%.*s"),
						     (int)(i + 1),
						     (int)(hunk->end
							   - hunk->start),
						     plain + hunk->start);

				overlap_start = overlap_end;
				overlap_end = overlap_next;
			}
			len = overlap_end - overlap_start;

			if (len > temp->end - temp->start ||
			    memcmp(plain + temp->end - len,
				   plain + overlap_start, len))
				return error(_("hunks do not overlap:\n%.*s\n"
					       "\tdoes not end with:\n%.*s"),
					     (int)(temp->end - temp->start),
					     plain + temp->start,
					     (int)len, plain + overlap_start);

			/*
			 * Since the start-end ranges are not adjacent, we
			 * cannot simply take the union of the ranges. To
			 * address that, we temporarily append the union of the
			 * lines to the `plain` strbuf.
			 */
			if (temp->end != s->plain.len) {
				size_t start = s->plain.len;

				strbuf_add(&s->plain, plain + temp->start,
					   temp->end - temp->start);
				plain = s->plain.buf;
				temp->start = start;
				temp->end = s->plain.len;
			}

			strbuf_add(&s->plain,
				   plain + overlap_end,
				   hunk->end - overlap_end);
			temp->end = s->plain.len;
			temp->splittable_into += hunk->splittable_into;
			delta = temp->delta;
			temp->delta += hunk->delta;
		}

		header->old_count = next->old_offset + next->old_count
			- header->old_offset;
		header->new_count = next->new_offset + delta
			+ next->new_count - header->new_offset;
	}

	if (i == *hunk_index)
		return 0;

	*hunk_index = i;
	return 1;
}

static void reassemble_patch(struct add_p_state *s,
			     struct file_diff *file_diff, int use_all,
			     struct strbuf *out)
{
	struct hunk *hunk;
	size_t save_len = s->plain.len, i;
	ssize_t delta = 0;

	render_diff_header(s, file_diff, 0, out);

	for (i = file_diff->mode_change; i < file_diff->hunk_nr; i++) {
		struct hunk temp = { 0 };

		hunk = file_diff->hunk + i;
		if (!use_all && hunk->use != USE_HUNK)
			delta += hunk->header.old_count
				- hunk->header.new_count;
		else {
			/* merge overlapping hunks into a temporary hunk */
			if (merge_hunks(s, file_diff, &i, use_all, &temp))
				hunk = &temp;

			render_hunk(s, hunk, delta, 0, out);

			/*
			 * In case `merge_hunks()` used `plain` as a scratch
			 * pad (this happens when an edited hunk had to be
			 * coalesced with another hunk).
			 */
			strbuf_setlen(&s->plain, save_len);

			delta += hunk->delta;
		}
	}
}

static int split_hunk(struct add_p_state *s, struct file_diff *file_diff,
		       size_t hunk_index)
{
	int colored = !!s->colored.len, first = 1;
	struct hunk *hunk = file_diff->hunk + hunk_index;
	size_t splittable_into;
	size_t end, colored_end, current, colored_current = 0, context_line_count;
	struct hunk_header remaining, *header;
	char marker, ch;

	if (hunk_index >= file_diff->hunk_nr)
		BUG("invalid hunk index: %d (must be >= 0 and < %d)",
		    (int)hunk_index, (int)file_diff->hunk_nr);

	if (hunk->splittable_into < 2)
		return 0;
	splittable_into = hunk->splittable_into;

	end = hunk->end;
	colored_end = hunk->colored_end;

	memcpy(&remaining, &hunk->header, sizeof(remaining));

	file_diff->hunk_nr += splittable_into - 1;
	ALLOC_GROW(file_diff->hunk, file_diff->hunk_nr, file_diff->hunk_alloc);
	if (hunk_index + splittable_into < file_diff->hunk_nr)
		memmove(file_diff->hunk + hunk_index + splittable_into,
			file_diff->hunk + hunk_index + 1,
			(file_diff->hunk_nr - hunk_index - splittable_into)
			* sizeof(*hunk));
	hunk = file_diff->hunk + hunk_index;
	hunk->splittable_into = 1;
	memset(hunk + 1, 0, (splittable_into - 1) * sizeof(*hunk));

	header = &hunk->header;
	header->old_count = header->new_count = 0;

	current = hunk->start;
	if (colored)
		colored_current = hunk->colored_start;
	marker = '\0';
	context_line_count = 0;

	while (splittable_into > 1) {
		ch = s->plain.buf[current];
		if ((marker == '-' || marker == '+') && ch == ' ') {
			first = 0;
			hunk[1].start = current;
			if (colored)
				hunk[1].colored_start = colored_current;
			context_line_count = 0;
		}

		if (marker != ' ' || (ch != '-' && ch != '+')) {
next_hunk_line:
			/* current hunk not done yet */
			if (ch == ' ')
				context_line_count++;
			else if (ch == '-')
				header->old_count++;
			else if (ch == '+')
				header->new_count++;
			else
				BUG("unhandled diff marker: '%c'", ch);
			marker = ch;
			current = find_next_line(&s->plain, current);
			if (colored)
				colored_current =
					find_next_line(&s->colored,
						       colored_current);
			continue;
		}

		if (first) {
			if (header->old_count || header->new_count)
				BUG("counts are off: %d/%d",
				    (int)header->old_count,
				    (int)header->new_count);

			header->old_count = context_line_count;
			header->new_count = context_line_count;
			context_line_count = 0;
			first = 0;
			goto next_hunk_line;
		}

		remaining.old_offset += header->old_count;
		remaining.old_count -= header->old_count;
		remaining.new_offset += header->new_count;
		remaining.new_count -= header->new_count;

		/* initialize next hunk header's offsets */
		hunk[1].header.old_offset =
			header->old_offset + header->old_count;
		hunk[1].header.new_offset =
			header->new_offset + header->new_count;

		/* add one split hunk */
		header->old_count += context_line_count;
		header->new_count += context_line_count;

		hunk->end = current;
		if (colored)
			hunk->colored_end = colored_current;

		hunk++;
		hunk->splittable_into = 1;
		hunk->use = hunk[-1].use;
		header = &hunk->header;

		header->old_count = header->new_count = context_line_count;
		context_line_count = 0;

		splittable_into--;
		marker = ch;
	}

	/* last hunk simply gets the rest */
	if (header->old_offset != remaining.old_offset)
		BUG("miscounted old_offset: %lu != %lu",
		    header->old_offset, remaining.old_offset);
	if (header->new_offset != remaining.new_offset)
		BUG("miscounted new_offset: %lu != %lu",
		    header->new_offset, remaining.new_offset);
	header->old_count = remaining.old_count;
	header->new_count = remaining.new_count;
	hunk->end = end;
	if (colored)
		hunk->colored_end = colored_end;

	return 0;
}

static void recolor_hunk(struct add_p_state *s, struct hunk *hunk)
{
	const char *plain = s->plain.buf;
	size_t current, eol, next;

	if (!s->colored.len)
		return;

	hunk->colored_start = s->colored.len;
	for (current = hunk->start; current < hunk->end; ) {
		for (eol = current; eol < hunk->end; eol++)
			if (plain[eol] == '\n')
				break;
		next = eol + (eol < hunk->end);
		if (eol > current && plain[eol - 1] == '\r')
			eol--;

		strbuf_addstr(&s->colored,
			      plain[current] == '-' ?
			      s->s.file_old_color :
			      plain[current] == '+' ?
			      s->s.file_new_color :
			      s->s.context_color);
		strbuf_add(&s->colored, plain + current, eol - current);
		strbuf_addstr(&s->colored, GIT_COLOR_RESET);
		if (next > eol)
			strbuf_add(&s->colored, plain + eol, next - eol);
		current = next;
	}
	hunk->colored_end = s->colored.len;
}

static int edit_hunk_manually(struct add_p_state *s, struct hunk *hunk)
{
	char *path = xstrdup(git_path("addp-hunk-edit.diff"));
	int fd = xopen(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	struct strbuf buf = STRBUF_INIT;
	size_t i, j;
	int res, copy;

	if (fd < 0) {
		res = error_errno(_("could not open '%s' for writing"), path);
		goto edit_hunk_manually_finish;
	}

	strbuf_commented_addf(&buf, _("Manual hunk edit mode -- see bottom for "
				      "a quick guide.\n"));
	render_hunk(s, hunk, 0, 0, &buf);
	strbuf_commented_addf(&buf,
			      _("---\n"
				"To remove '%c' lines, make them ' ' lines "
				"(context).\n"
				"To remove '%c' lines, delete them.\n"
				"Lines starting with %c will be removed.\n"),
			      s->mode->is_reverse ? '+' : '-',
			      s->mode->is_reverse ? '-' : '+',
			      comment_line_char);
	strbuf_commented_addf(&buf, "%s", _(s->mode->edit_hunk_hint));
	/*
	 * TRANSLATORS: 'it' refers to the patch mentioned in the previous
	 * messages.
	 */
	strbuf_commented_addf(&buf,
			      _("If it does not apply cleanly, you will be "
				"given an opportunity to\n"
				"edit again.  If all lines of the hunk are "
				"removed, then the edit is\n"
				"aborted and the hunk is left unchanged.\n"));
	if (write_in_full(fd, buf.buf, buf.len) < 0) {
		res = error_errno(_("could not write to '%s'"), path);
		goto edit_hunk_manually_finish;
	}

	res = close(fd);
	fd = -1;
	if (res < 0)
		goto edit_hunk_manually_finish;

	hunk->start = s->plain.len;
	if (launch_editor(path, &s->plain, NULL) < 0) {
		res = error_errno(_("could not edit '%s'"), path);
		goto edit_hunk_manually_finish;
	}
	unlink(path);

	/* strip out commented lines */
	copy = s->plain.buf[hunk->start] != comment_line_char;
	for (i = j = hunk->start; i < s->plain.len; ) {
		if (copy)
			s->plain.buf[j++] = s->plain.buf[i];
		if (s->plain.buf[i++] == '\n')
			copy = s->plain.buf[i] != comment_line_char;
	}

	if (j == hunk->start)
		/* User aborted by deleting everything */
		goto edit_hunk_manually_finish;

	res = 1;
	strbuf_setlen(&s->plain, j);
	hunk->end = j;
	recolor_hunk(s, hunk);
	if (s->plain.buf[hunk->start] == '@' &&
	    /* If the hunk header was deleted, simply use the original one. */
	    parse_hunk_header(s, hunk) < 0)
		res = -1;

edit_hunk_manually_finish:
	if (fd >= 0)
		close(fd);
	free(path);
	strbuf_release(&buf);

	return res;
}

static ssize_t recount_edited_hunk(struct add_p_state *s, struct hunk *hunk,
				   size_t orig_old_count, size_t orig_new_count)
{
	struct hunk_header *header = &hunk->header;
	size_t i;

	header->old_count = header->new_count = 0;
	for (i = hunk->start; i < hunk->end; ) {
		switch (s->plain.buf[i]) {
		case '-':
			header->old_count++;
			break;
		case '+':
			header->new_count++;
			break;
		case ' ': case '\r': case '\n':
			header->old_count++;
			header->new_count++;
			break;
		}

		i = find_next_line(&s->plain, i);
	}

	return orig_old_count - orig_new_count
		- header->old_count + header->new_count;
}

static int run_apply_check(struct add_p_state *s,
			   struct file_diff *file_diff)
{
	struct child_process cp = CHILD_PROCESS_INIT;

	strbuf_reset(&s->buf);
	reassemble_patch(s, file_diff, 1, &s->buf);

	setup_child_process(&cp, s,
			    "apply", "--check", NULL);
	argv_array_pushv(&cp.args, s->mode->apply_check);
	if (pipe_command(&cp, s->buf.buf, s->buf.len, NULL, 0, NULL, 0))
		return error(_("'git apply --cached' failed"));

	return 0;
}

static int read_single_character(struct add_p_state *s)
{
	if (s->s.use_single_key) {
		int res = read_key_without_echo(&s->answer);
		printf("%s\n", res == EOF ? "" : s->answer.buf);
		return res;
	}

	if (strbuf_getline(&s->answer, stdin) == EOF)
		return EOF;
	strbuf_trim_trailing_newline(&s->answer);
	return 0;
}

static int prompt_yesno(struct add_p_state *s, const char *prompt)
{
	for (;;) {
		color_fprintf(stdout, s->s.prompt_color, "%s", _(prompt));
		fflush(stdout);
		if (read_single_character(s) == EOF)
			return -1;
		switch (tolower(s->answer.buf[0])) {
		case 'n': return 0;
		case 'y': return 1;
		}
	}
}

static int edit_hunk_loop(struct add_p_state *s,
			  struct file_diff *file_diff, struct hunk *hunk)
{
	size_t plain_len = s->plain.len, colored_len = s->colored.len;
	struct hunk backup;

	memcpy(&backup, hunk, sizeof(backup));

	for (;;) {
		int res = edit_hunk_manually(s, hunk);
		if (res == 0) {
			/* abandonded */
			memcpy(hunk, &backup, sizeof(backup));
			return -1;
		}

		if (res > 0) {
			hunk->delta +=
				recount_edited_hunk(s, hunk,
						    backup.header.old_count,
						    backup.header.new_count);
			if (!run_apply_check(s, file_diff))
				return 0;
		}

		/* Drop edits (they were appended to s->plain) */
		strbuf_setlen(&s->plain, plain_len);
		strbuf_setlen(&s->colored, colored_len);
		memcpy(hunk, &backup, sizeof(backup));

		/*
		 * TRANSLATORS: do not translate [y/n]
		 * The program will only accept that input at this point.
		 * Consider translating (saying "no" discards!) as
		 * (saying "n" for "no" discards!) if the translation
		 * of the word "no" does not start with n.
		 */
		res = prompt_yesno(s, _("Your edited hunk does not apply. "
					"Edit again (saying \"no\" discards!) "
					"[y/n]? "));
		if (res < 1)
			return -1;
	}
}

static int apply_for_checkout(struct add_p_state *s, struct strbuf *diff,
			      int is_reverse)
{
	const char *reverse = is_reverse ? "-R" : NULL;
	struct child_process check_index = CHILD_PROCESS_INIT;
	struct child_process check_worktree = CHILD_PROCESS_INIT;
	struct child_process apply_index = CHILD_PROCESS_INIT;
	struct child_process apply_worktree = CHILD_PROCESS_INIT;
	int applies_index, applies_worktree;

	setup_child_process(&check_index, s,
			    "apply", "--cached", "--check", reverse, NULL);
	applies_index = !pipe_command(&check_index, diff->buf, diff->len,
				      NULL, 0, NULL, 0);

	setup_child_process(&check_worktree, s,
			    "apply", "--check", reverse, NULL);
	applies_worktree = !pipe_command(&check_worktree, diff->buf, diff->len,
					 NULL, 0, NULL, 0);

	if (applies_worktree && applies_index) {
		setup_child_process(&apply_index, s,
				    "apply", "--cached", reverse, NULL);
		pipe_command(&apply_index, diff->buf, diff->len,
			     NULL, 0, NULL, 0);

		setup_child_process(&apply_worktree, s,
				    "apply", reverse, NULL);
		pipe_command(&apply_worktree, diff->buf, diff->len,
			     NULL, 0, NULL, 0);

		return 1;
	}

	if (!applies_index) {
		err(s, _("The selected hunks do not apply to the index!"));
		if (prompt_yesno(s, _("Apply them to the worktree "
					  "anyway? ")) > 0) {
			setup_child_process(&apply_worktree, s,
					    "apply", reverse, NULL);
			return pipe_command(&apply_worktree, diff->buf,
					    diff->len, NULL, 0, NULL, 0);
		}
		err(s, _("Nothing was applied.\n"));
	} else
		/* As a last resort, show the diff to the user */
		fwrite(diff->buf, diff->len, 1, stderr);

	return 0;
}

#define SUMMARY_HEADER_WIDTH 20
#define SUMMARY_LINE_WIDTH 80
static void summarize_hunk(struct add_p_state *s, struct hunk *hunk,
			   struct strbuf *out)
{
	struct hunk_header *header = &hunk->header;
	struct strbuf *plain = &s->plain;
	size_t len = out->len, i;

	strbuf_addf(out, " -%lu,%lu +%lu,%lu ",
		    header->old_offset, header->old_count,
		    header->new_offset, header->new_count);
	if (out->len - len < SUMMARY_HEADER_WIDTH)
		strbuf_addchars(out, ' ',
				SUMMARY_HEADER_WIDTH + len - out->len);
	for (i = hunk->start; i < hunk->end; i = find_next_line(plain, i))
		if (plain->buf[i] != ' ')
			break;
	if (i < hunk->end)
		strbuf_add(out, plain->buf + i, find_next_line(plain, i) - i);
	if (out->len - len > SUMMARY_LINE_WIDTH)
		strbuf_setlen(out, len + SUMMARY_LINE_WIDTH);
	strbuf_complete_line(out);
}

#define DISPLAY_HUNKS_LINES 20
static size_t display_hunks(struct add_p_state *s,
			    struct file_diff *file_diff, size_t start_index)
{
	size_t end_index = start_index + DISPLAY_HUNKS_LINES;

	if (end_index > file_diff->hunk_nr)
		end_index = file_diff->hunk_nr;

	while (start_index < end_index) {
		struct hunk *hunk = file_diff->hunk + start_index++;

		strbuf_reset(&s->buf);
		strbuf_addf(&s->buf, "%c%2d: ", hunk->use == USE_HUNK ? '+'
			    : hunk->use == SKIP_HUNK ? '-' : ' ',
			    (int)start_index);
		summarize_hunk(s, hunk, &s->buf);
		fputs(s->buf.buf, stdout);
	}

	return end_index;
}

static const char help_patch_remainder[] =
N_("j - leave this hunk undecided, see next undecided hunk\n"
   "J - leave this hunk undecided, see next hunk\n"
   "k - leave this hunk undecided, see previous undecided hunk\n"
   "K - leave this hunk undecided, see previous hunk\n"
   "g - select a hunk to go to\n"
   "/ - search for a hunk matching the given regex\n"
   "s - split the current hunk into smaller hunks\n"
   "e - manually edit the current hunk\n"
   "? - print help\n");

static int patch_update_file(struct add_p_state *s,
			     struct file_diff *file_diff)
{
	size_t hunk_index = 0;
	ssize_t i, undecided_previous, undecided_next;
	struct hunk *hunk;
	char ch;
	struct child_process cp = CHILD_PROCESS_INIT;
	int colored = !!s->colored.len, quit = 0;
	enum prompt_mode_type prompt_mode_type;

	if (!file_diff->hunk_nr)
		return 0;

	strbuf_reset(&s->buf);
	render_diff_header(s, file_diff, colored, &s->buf);
	fputs(s->buf.buf, stdout);
	for (;;) {
		if (hunk_index >= file_diff->hunk_nr)
			hunk_index = 0;
		hunk = file_diff->hunk + hunk_index;

		undecided_previous = -1;
		for (i = hunk_index - 1; i >= 0; i--)
			if (file_diff->hunk[i].use == UNDECIDED_HUNK) {
				undecided_previous = i;
				break;
			}

		undecided_next = -1;
		for (i = hunk_index + 1; i < file_diff->hunk_nr; i++)
			if (file_diff->hunk[i].use == UNDECIDED_HUNK) {
				undecided_next = i;
				break;
			}

		/* Everything decided? */
		if (undecided_previous < 0 && undecided_next < 0 &&
		    hunk->use != UNDECIDED_HUNK)
			break;

		strbuf_reset(&s->buf);
		render_hunk(s, hunk, 0, colored, &s->buf);
		fputs(s->buf.buf, stdout);

		strbuf_reset(&s->buf);
		if (undecided_previous >= 0)
			strbuf_addstr(&s->buf, ",k");
		if (hunk_index)
			strbuf_addstr(&s->buf, ",K");
		if (undecided_next >= 0)
			strbuf_addstr(&s->buf, ",j");
		if (hunk_index + 1 < file_diff->hunk_nr)
			strbuf_addstr(&s->buf, ",J");
		if (file_diff->hunk_nr > 1)
			strbuf_addstr(&s->buf, ",g,/");
		if (hunk->splittable_into > 1)
			strbuf_addstr(&s->buf, ",s");
		if (hunk_index + 1 > file_diff->mode_change &&
		    !file_diff->deleted)
			strbuf_addstr(&s->buf, ",e");

		if (file_diff->deleted)
			prompt_mode_type = PROMPT_DELETION;
		else if (file_diff->mode_change && !hunk_index)
			prompt_mode_type = PROMPT_MODE_CHANGE;
		else
			prompt_mode_type = PROMPT_HUNK;

		color_fprintf(stdout, s->s.prompt_color,
			      "(%"PRIuMAX"/%"PRIuMAX") ",
			      (uintmax_t)hunk_index + 1,
			      (uintmax_t)file_diff->hunk_nr);
		color_fprintf(stdout, s->s.prompt_color,
			      _(s->mode->prompt_mode[prompt_mode_type]),
			      s->buf.buf);
		fflush(stdout);
		if (read_single_character(s) == EOF)
			break;

		if (!s->answer.len)
			continue;
		ch = tolower(s->answer.buf[0]);
		if (ch == 'y') {
			hunk->use = USE_HUNK;
soft_increment:
			while (++hunk_index < file_diff->hunk_nr &&
			       file_diff->hunk[hunk_index].use
			       != UNDECIDED_HUNK)
				; /* continue looking */
		} else if (ch == 'n') {
			hunk->use = SKIP_HUNK;
			goto soft_increment;
		} else if (ch == 'a') {
			for (; hunk_index < file_diff->hunk_nr; hunk_index++) {
				hunk = file_diff->hunk + hunk_index;
				if (hunk->use == UNDECIDED_HUNK)
					hunk->use = USE_HUNK;
			}
		} else if (ch == 'd' || ch == 'q') {
			for (; hunk_index < file_diff->hunk_nr; hunk_index++) {
				hunk = file_diff->hunk + hunk_index;
				if (hunk->use == UNDECIDED_HUNK)
					hunk->use = SKIP_HUNK;
			}
			if (ch == 'q') {
				quit = 1;
				break;
			}
		} else if (s->answer.buf[0] == 'K') {
			if (hunk_index)
				hunk_index--;
			else
				err(s, _("No previous hunk"));
		} else if (s->answer.buf[0] == 'J') {
			if (hunk_index + 1 < file_diff->hunk_nr)
				hunk_index++;
			else
				err(s, _("No next hunk"));
		} else if (s->answer.buf[0] == 'k') {
			if (undecided_previous >= 0)
				hunk_index = undecided_previous;
			else
				err(s, _("No previous hunk"));
		} else if (s->answer.buf[0] == 'j') {
			if (undecided_next >= 0)
				hunk_index = undecided_next;
			else
				err(s, _("No next hunk"));
		} else if (s->answer.buf[0] == 'g') {
			char *pend;
			unsigned long response;

			if (file_diff->hunk_nr < 2) {
				err(s, _("No other hunks to goto"));
				continue;
			}
			strbuf_remove(&s->answer, 0, 1);
			strbuf_trim(&s->answer);
			i = hunk_index > 10 ? hunk_index - 10 : 0;
			while (s->answer.len == 0) {
				i = display_hunks(s, file_diff, i);
				printf("%s", i < file_diff->hunk_nr ?
				       _("go to which hunk (<ret> to see "
					 "more)? ") : _("go to which hunk? "));
				fflush(stdout);
				if (strbuf_getline(&s->answer,
						   stdin) == EOF)
					break;
				strbuf_trim_trailing_newline(&s->answer);
			}

			strbuf_trim(&s->answer);
			response = strtoul(s->answer.buf, &pend, 10);
			if (*pend || pend == s->answer.buf)
				err(s, _("Invalid number: '%s'"),
				    s->answer.buf);
			else if (0 < response && response <= file_diff->hunk_nr)
				hunk_index = response - 1;
			else
				err(s, Q_("Sorry, only %d hunk available.",
					  "Sorry, only %d hunks available.",
					  file_diff->hunk_nr),
				    (int)file_diff->hunk_nr);
		} else if (s->answer.buf[0] == '/') {
			regex_t regex;
			int ret;

			if (file_diff->hunk_nr < 2) {
				err(s, _("No other hunks to search"));
				continue;
			}
			strbuf_remove(&s->answer, 0, 1);
			strbuf_trim_trailing_newline(&s->answer);
			if (s->answer.len == 0) {
				printf("%s", _("search for regex? "));
				fflush(stdout);
				if (strbuf_getline(&s->answer,
						   stdin) == EOF)
					break;
				strbuf_trim_trailing_newline(&s->answer);
				if (s->answer.len == 0)
					continue;
			}
			ret = regcomp(&regex, s->answer.buf,
				      REG_EXTENDED | REG_NOSUB | REG_NEWLINE);
			if (ret) {
				char errbuf[1024];

				regerror(ret, &regex, errbuf, sizeof(errbuf));
				err(s, _("Malformed search regexp %s: %s"),
				    s->answer.buf, errbuf);
				continue;
			}
			i = hunk_index;
			for (;;) {
				/* render the hunk into a scratch buffer */
				render_hunk(s, file_diff->hunk + i, 0, 0,
					    &s->buf);
				if (regexec(&regex, s->buf.buf, 0, NULL, 0)
				    != REG_NOMATCH)
					break;
				i++;
				if (i == file_diff->hunk_nr)
					i = 0;
				if (i != hunk_index)
					continue;
				err(s, _("No hunk matches the given pattern"));
				break;
			}
			hunk_index = i;
		} else if (s->answer.buf[0] == 's') {
			size_t splittable_into = hunk->splittable_into;
			if (splittable_into < 2)
				err(s, _("Sorry, cannot split this hunk"));
			else if (!split_hunk(s, file_diff,
					     hunk - file_diff->hunk))
				color_fprintf_ln(stdout, s->s.header_color,
						 _("Split into %d hunks."),
						 (int)splittable_into);
		} else if (s->answer.buf[0] == 'e') {
			if (hunk_index + 1 == file_diff->mode_change)
				err(s, _("Sorry, cannot edit this hunk"));
			else if (edit_hunk_loop(s, file_diff, hunk) >= 0) {
				hunk->use = USE_HUNK;
				goto soft_increment;
			}
		} else {
			const char *p = _(help_patch_remainder), *eol = p;

			color_fprintf(stdout, s->s.help_color, "%s",
				      _(s->mode->help_patch_text));

			/*
			 * Show only those lines of the remainder that are
			 * actually applicable with the current hunk.
			 */
			for (; *p; p = eol + (*eol == '\n')) {
				eol = strchrnul(p, '\n');

				/*
				 * `s->buf` still contains the part of the
				 * commands shown in the prompt that are not
				 * always available.
				 */
				if (*p != '?' && !strchr(s->buf.buf, *p))
					continue;

				color_fprintf_ln(stdout, s->s.help_color,
						 "%.*s", (int)(eol - p), p);
			}
		}
	}

	/* Any hunk to be used? */
	for (i = 0; i < file_diff->hunk_nr; i++)
		if (file_diff->hunk[i].use == USE_HUNK)
			break;

	if (i < file_diff->hunk_nr) {
		/* At least one hunk selected: apply */
		strbuf_reset(&s->buf);
		reassemble_patch(s, file_diff, 0, &s->buf);

		discard_index(s->s.r->index);
		if (s->mode->apply_for_checkout)
			apply_for_checkout(s, &s->buf,
					   s->mode->is_reverse);
		else {
			setup_child_process(&cp, s, "apply", NULL);
			argv_array_pushv(&cp.args, s->mode->apply);
			if (pipe_command(&cp, s->buf.buf, s->buf.len,
					 NULL, 0, NULL, 0))
				error(_("'git apply' failed"));
		}
		if (!repo_read_index(s->s.r))
			repo_refresh_and_write_index(s->s.r, REFRESH_QUIET, 0,
						     1, NULL, NULL, NULL);
	}

	putchar('\n');
	return quit;
}

int run_add_p(struct repository *r, enum add_p_mode mode,
	      const char *revision, const struct pathspec *ps)
{
	struct add_p_state s = {
		{ r }, STRBUF_INIT, STRBUF_INIT, STRBUF_INIT, STRBUF_INIT
	};
	size_t i, binary_count = 0;

	init_add_i_state(&s.s, r);

	if (mode == ADD_P_STASH)
		s.mode = &patch_mode_stash;
	else if (mode == ADD_P_RESET) {
		if (!revision || !strcmp(revision, "HEAD"))
			s.mode = &patch_mode_reset_head;
		else
			s.mode = &patch_mode_reset_nothead;
	} else if (mode == ADD_P_CHECKOUT) {
		if (!revision)
			s.mode = &patch_mode_checkout_index;
		else if (!strcmp(revision, "HEAD"))
			s.mode = &patch_mode_checkout_head;
		else
			s.mode = &patch_mode_checkout_nothead;
	} else if (mode == ADD_P_WORKTREE) {
		if (!revision)
			s.mode = &patch_mode_checkout_index;
		else if (!strcmp(revision, "HEAD"))
			s.mode = &patch_mode_worktree_head;
		else
			s.mode = &patch_mode_worktree_nothead;
	} else
		s.mode = &patch_mode_stage;
	s.revision = revision;

	if (discard_index(r->index) < 0 || repo_read_index(r) < 0 ||
	    repo_refresh_and_write_index(r, REFRESH_QUIET, 0, 1,
					 NULL, NULL, NULL) < 0 ||
	    parse_diff(&s, ps) < 0) {
		strbuf_release(&s.plain);
		strbuf_release(&s.colored);
		clear_add_i_state(&s.s);
		return -1;
	}

	for (i = 0; i < s.file_diff_nr; i++)
		if (s.file_diff[i].binary && !s.file_diff[i].hunk_nr)
			binary_count++;
		else if (patch_update_file(&s, s.file_diff + i))
			break;

	if (s.file_diff_nr == 0)
		fprintf(stderr, _("No changes.\n"));
	else if (binary_count == s.file_diff_nr)
		fprintf(stderr, _("Only binary files changed.\n"));

	strbuf_release(&s.answer);
	strbuf_release(&s.buf);
	strbuf_release(&s.plain);
	strbuf_release(&s.colored);
	clear_add_i_state(&s.s);
	return 0;
}
