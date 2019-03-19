#include "cache.h"
#include "add-interactive.h"
#include "strbuf.h"
#include "run-command.h"
#include "argv-array.h"
#include "pathspec.h"
#include "color.h"
#include "diff.h"

enum prompt_mode_type {
	PROMPT_MODE_CHANGE = 0, PROMPT_DELETION, PROMPT_HUNK
};

static const char *prompt_mode[] = {
	N_("Stage mode change [y,n,a,d%s,?]? "),
	N_("Stage deletion [y,n,a,d%s,?]? "),
	N_("Stage this hunk [y,n,a,d%s,?]? ")
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
		unsigned deleted:1, mode_change:1;
	} *file_diff;
	size_t file_diff_nr;
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
	struct strbuf *plain = &s->plain, *colored = NULL;
	struct child_process cp = CHILD_PROCESS_INIT;
	char *p, *pend, *colored_p = NULL, *colored_pend = NULL, marker = '\0';
	size_t file_diff_alloc = 0, i, color_arg_index;
	struct file_diff *file_diff = NULL;
	struct hunk *hunk = NULL;
	int res;

	/* Use `--no-color` explicitly, just in case `diff.color = always`. */
	argv_array_pushl(&args, "diff-files", "-p", "--no-color", "--", NULL);
	color_arg_index = args.argc - 2;
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

		setup_child_process(&colored_cp, s, NULL);
		xsnprintf((char *)args.argv[color_arg_index], 8, "--color");
		colored_cp.argv = args.argv;
		colored = &s->colored;
		res = capture_command(&colored_cp, colored, 0);
		argv_array_clear(&args);
		if (res)
			return error(_("could not parse colored diff"));
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
		}

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

		if (!colored) {
			p = s->plain.buf + header->extra_start;
			len = header->extra_end - header->extra_start;
		} else {
			strbuf_addstr(out, s->s.fraginfo_color);
			p = s->colored.buf + header->colored_extra_start;
			len = header->colored_extra_end
				- header->colored_extra_start;
		}

		strbuf_addf(out, "@@ -%lu,%lu +%lu,%lu @@",
			    header->old_offset, header->old_count,
			    (unsigned long)(header->new_offset + delta),
			    header->new_count);
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
		       size_t *hunk_index, struct hunk *temp)
{
	size_t i = *hunk_index;
	struct hunk *hunk = file_diff->hunk + i;
	struct hunk_header *header = &temp->header, *next;

	if (hunk->use != USE_HUNK)
		return 0;

	memcpy(temp, hunk, sizeof(*temp));
	/* We simply skip the colored part (if any) when merging hunks */
	temp->colored_start = temp->colored_end = 0;

	for (; i + 1 < file_diff->hunk_nr; i++) {
		hunk++;
		next = &hunk->header;

		if (hunk->use != USE_HUNK ||
		    header->new_offset >= next->new_offset ||
		    header->new_offset + header->new_count < next->new_offset ||
		    temp->start >= hunk->start ||
		    temp->end < hunk->start)
			break;

		temp->end = hunk->end;
		temp->colored_end = hunk->colored_end;

		header->old_count = next->old_offset + next->old_count
			- header->old_offset;
		header->new_count = next->new_offset + next->new_count
			- header->new_offset;
	}

	if (i == *hunk_index)
		return 0;

	*hunk_index = i;
	return 1;
}

static void reassemble_patch(struct add_p_state *s,
			     struct file_diff *file_diff, struct strbuf *out)
{
	struct hunk *hunk;
	size_t i;
	ssize_t delta = 0;

	render_diff_header(s, file_diff, 0, out);

	for (i = file_diff->mode_change; i < file_diff->hunk_nr; i++) {
		struct hunk temp = { 0 };

		hunk = file_diff->hunk + i;
		if (hunk->use != USE_HUNK)
			delta += hunk->header.old_count
				- hunk->header.new_count;
		else {
			/* merge overlapping hunks into a temporary hunk */
			if (merge_hunks(s, file_diff, &i, &temp))
				hunk = &temp;

			render_hunk(s, hunk, delta, 0, out);
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

static const char help_patch_text[] =
N_("y - stage this hunk\n"
   "n - do not stage this hunk\n"
   "a - stage this and all the remaining hunks\n"
   "d - do not stage this hunk nor any of the remaining hunks\n"
   "j - leave this hunk undecided, see next undecided hunk\n"
   "J - leave this hunk undecided, see next hunk\n"
   "k - leave this hunk undecided, see previous undecided hunk\n"
   "K - leave this hunk undecided, see previous hunk\n"
   "s - split the current hunk into smaller hunks\n"
   "? - print help\n");

static int patch_update_file(struct add_p_state *s,
			     struct file_diff *file_diff)
{
	size_t hunk_index = 0;
	ssize_t i, undecided_previous, undecided_next;
	struct hunk *hunk;
	char ch;
	struct child_process cp = CHILD_PROCESS_INIT;
	int colored = !!s->colored.len;
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
		if (hunk->splittable_into > 1)
			strbuf_addstr(&s->buf, ",s");

		if (file_diff->deleted)
			prompt_mode_type = PROMPT_DELETION;
		else if (file_diff->mode_change && !hunk_index)
			prompt_mode_type = PROMPT_MODE_CHANGE;
		else
			prompt_mode_type = PROMPT_HUNK;

		color_fprintf(stdout, s->s.prompt_color,
			      _(prompt_mode[prompt_mode_type]), s->buf.buf);
		fflush(stdout);
		if (strbuf_getline(&s->answer, stdin) == EOF)
			break;
		strbuf_trim_trailing_newline(&s->answer);

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
		} else if (ch == 'd') {
			for (; hunk_index < file_diff->hunk_nr; hunk_index++) {
				hunk = file_diff->hunk + hunk_index;
				if (hunk->use == UNDECIDED_HUNK)
					hunk->use = SKIP_HUNK;
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
		} else if (s->answer.buf[0] == 's') {
			size_t splittable_into = hunk->splittable_into;
			if (splittable_into < 2)
				err(s, _("Sorry, cannot split this hunk"));
			else if (!split_hunk(s, file_diff,
					     hunk - file_diff->hunk))
				color_fprintf_ln(stdout, s->s.header_color,
						 _("Split into %d hunks."),
						 (int)splittable_into);
		} else
			color_fprintf(stdout, s->s.help_color,
				      _(help_patch_text));
	}

	/* Any hunk to be used? */
	for (i = 0; i < file_diff->hunk_nr; i++)
		if (file_diff->hunk[i].use == USE_HUNK)
			break;

	if (i < file_diff->hunk_nr) {
		/* At least one hunk selected: apply */
		strbuf_reset(&s->buf);
		reassemble_patch(s, file_diff, &s->buf);

		setup_child_process(&cp, s, "apply", "--cached", NULL);
		if (pipe_command(&cp, s->buf.buf, s->buf.len,
				 NULL, 0, NULL, 0))
			error(_("'git apply --cached' failed"));
		repo_refresh_and_write_index(s->s.r, REFRESH_QUIET, 0);
	}

	putchar('\n');
	return 0;
}

int run_add_p(struct repository *r, const struct pathspec *ps)
{
	struct add_p_state s = {
		{ r }, STRBUF_INIT, STRBUF_INIT, STRBUF_INIT, STRBUF_INIT
	};
	size_t i;

	if (init_add_i_state(r, &s.s))
		return error("Could not read `add -i` config");

	if (repo_refresh_and_write_index(r, REFRESH_QUIET, 0) < 0 ||
	    parse_diff(&s, ps) < 0) {
		strbuf_release(&s.plain);
		strbuf_release(&s.colored);
		return -1;
	}

	for (i = 0; i < s.file_diff_nr; i++)
		if (patch_update_file(&s, s.file_diff + i))
			break;

	strbuf_release(&s.answer);
	strbuf_release(&s.buf);
	strbuf_release(&s.plain);
	strbuf_release(&s.colored);
	return 0;
}
