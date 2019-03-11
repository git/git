#include "cache.h"
#include "add-interactive.h"
#include "strbuf.h"
#include "run-command.h"
#include "argv-array.h"
#include "pathspec.h"

struct hunk {
	size_t start, end;
	enum { UNDECIDED_HUNK = 0, SKIP_HUNK, USE_HUNK } use;
};

struct add_p_state {
	struct repository *r;
	struct strbuf answer, buf;

	/* parsed diff */
	struct strbuf plain;
	struct hunk head;
	struct hunk *hunk;
	size_t hunk_nr, hunk_alloc;
};

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
			 INDEX_ENVIRONMENT "=%s", s->r->index_file);
}

static int parse_diff(struct add_p_state *s, const struct pathspec *ps)
{
	struct strbuf *plain = &s->plain;
	struct child_process cp = CHILD_PROCESS_INIT;
	char *p, *pend;
	size_t i;
	struct hunk *hunk = NULL;
	int res;

	/* Use `--no-color` explicitly, just in case `diff.color = always`. */
	setup_child_process(&cp, s,
			 "diff-files", "-p", "--no-color", "--", NULL);
	for (i = 0; i < ps->nr; i++)
		argv_array_push(&cp.args, ps->items[i].original);

	res = capture_command(&cp, plain, 0);
	if (res)
		return error(_("could not parse diff"));
	if (!plain->len)
		return 0;
	strbuf_complete_line(plain);

	/* parse hunks */
	p = plain->buf;
	pend = p + plain->len;
	while (p != pend) {
		char *eol = memchr(p, '\n', pend - p);
		if (!eol)
			eol = pend;

		if (starts_with(p, "diff ")) {
			if (p != plain->buf)
				BUG("multi-file diff not yet handled");
			hunk = &s->head;
		} else if (p == plain->buf)
			BUG("diff starts with unexpected line:\n"
			    "%.*s\n", (int)(eol - p), p);
		else if (starts_with(p, "@@ ")) {
			s->hunk_nr++;
			ALLOC_GROW(s->hunk, s->hunk_nr,
				   s->hunk_alloc);
			hunk = s->hunk + s->hunk_nr - 1;
			memset(hunk, 0, sizeof(*hunk));

			hunk->start = p - plain->buf;
		}

		p = eol == pend ? pend : eol + 1;
		hunk->end = p - plain->buf;
	}

	return 0;
}

static void render_hunk(struct add_p_state *s, struct hunk *hunk,
			struct strbuf *out)
{
	strbuf_add(out, s->plain.buf + hunk->start,
		   hunk->end - hunk->start);
}

static void reassemble_patch(struct add_p_state *s, struct strbuf *out)
{
	struct hunk *hunk;
	size_t i;

	render_hunk(s, &s->head, out);

	for (i = 0; i < s->hunk_nr; i++) {
		hunk = s->hunk + i;
		if (hunk->use == USE_HUNK)
			render_hunk(s, hunk, out);
	}
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
   "? - print help\n");

static int patch_update_file(struct add_p_state *s)
{
	size_t hunk_index = 0;
	ssize_t i, undecided_previous, undecided_next;
	struct hunk *hunk;
	char ch;
	struct child_process cp = CHILD_PROCESS_INIT;

	if (!s->hunk_nr)
		return 0;

	strbuf_reset(&s->buf);
	render_hunk(s, &s->head, &s->buf);
	fputs(s->buf.buf, stdout);
	for (;;) {
		if (hunk_index >= s->hunk_nr)
			hunk_index = 0;
		hunk = s->hunk + hunk_index;

		undecided_previous = -1;
		for (i = hunk_index - 1; i >= 0; i--)
			if (s->hunk[i].use == UNDECIDED_HUNK) {
				undecided_previous = i;
				break;
			}

		undecided_next = -1;
		for (i = hunk_index + 1; i < s->hunk_nr; i++)
			if (s->hunk[i].use == UNDECIDED_HUNK) {
				undecided_next = i;
				break;
			}

		/* Everything decided? */
		if (undecided_previous < 0 && undecided_next < 0 &&
		    hunk->use != UNDECIDED_HUNK)
			break;

		strbuf_reset(&s->buf);
		render_hunk(s, hunk, &s->buf);
		fputs(s->buf.buf, stdout);

		strbuf_reset(&s->buf);
		if (undecided_previous >= 0)
			strbuf_addstr(&s->buf, ",k");
		if (hunk_index)
			strbuf_addstr(&s->buf, ",K");
		if (undecided_next >= 0)
			strbuf_addstr(&s->buf, ",j");
		if (hunk_index + 1 < s->hunk_nr)
			strbuf_addstr(&s->buf, ",J");
		printf("(%"PRIuMAX"/%"PRIuMAX") ",
		       (uintmax_t)hunk_index + 1, (uintmax_t)s->hunk_nr);
		printf(_("Stage this hunk [y,n,a,d%s,?]? "), s->buf.buf);
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
			while (++hunk_index < s->hunk_nr &&
			       s->hunk[hunk_index].use
			       != UNDECIDED_HUNK)
				; /* continue looking */
		} else if (ch == 'n') {
			hunk->use = SKIP_HUNK;
			goto soft_increment;
		} else if (ch == 'a') {
			for (; hunk_index < s->hunk_nr; hunk_index++) {
				hunk = s->hunk + hunk_index;
				if (hunk->use == UNDECIDED_HUNK)
					hunk->use = USE_HUNK;
			}
		} else if (ch == 'd') {
			for (; hunk_index < s->hunk_nr; hunk_index++) {
				hunk = s->hunk + hunk_index;
				if (hunk->use == UNDECIDED_HUNK)
					hunk->use = SKIP_HUNK;
			}
		} else if (hunk_index && s->answer.buf[0] == 'K')
			hunk_index--;
		else if (hunk_index + 1 < s->hunk_nr &&
			 s->answer.buf[0] == 'J')
			hunk_index++;
		else if (undecided_previous >= 0 &&
			 s->answer.buf[0] == 'k')
			hunk_index = undecided_previous;
		else if (undecided_next >= 0 && s->answer.buf[0] == 'j')
			hunk_index = undecided_next;
		else
			puts(_(help_patch_text));
	}

	/* Any hunk to be used? */
	for (i = 0; i < s->hunk_nr; i++)
		if (s->hunk[i].use == USE_HUNK)
			break;

	if (i < s->hunk_nr) {
		/* At least one hunk selected: apply */
		strbuf_reset(&s->buf);
		reassemble_patch(s, &s->buf);

		discard_index(s->r->index);
		setup_child_process(&cp, s, "apply", "--cached", NULL);
		if (pipe_command(&cp, s->buf.buf, s->buf.len,
				 NULL, 0, NULL, 0))
			error(_("'git apply --cached' failed"));
		if (!repo_read_index(s->r))
			repo_refresh_and_write_index(s->r, REFRESH_QUIET, 0,
						     1, NULL, NULL, NULL);
	}

	putchar('\n');
	return 0;
}

int run_add_p(struct repository *r, const struct pathspec *ps)
{
	struct add_p_state s = { r, STRBUF_INIT, STRBUF_INIT, STRBUF_INIT };

	if (discard_index(r->index) < 0 || repo_read_index(r) < 0 ||
	    repo_refresh_and_write_index(r, REFRESH_QUIET, 0, 1,
					 NULL, NULL, NULL) < 0 ||
	    parse_diff(&s, ps) < 0) {
		strbuf_release(&s.plain);
		return -1;
	}

	if (s.hunk_nr)
		patch_update_file(&s);

	strbuf_release(&s.answer);
	strbuf_release(&s.buf);
	strbuf_release(&s.plain);
	return 0;
}
