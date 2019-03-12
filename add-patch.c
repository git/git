#include "cache.h"
#include "add-interactive.h"
#include "strbuf.h"
#include "run-command.h"
#include "argv-array.h"
#include "pathspec.h"
#include "color.h"

struct hunk {
	size_t start, end, colored_start, colored_end;
	enum { UNDECIDED_HUNK = 0, SKIP_HUNK, USE_HUNK } use;
};

struct add_p_state {
	struct repository *r;
	struct strbuf answer, buf;

	/* parsed diff */
	struct strbuf plain, colored;
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
	struct argv_array args = ARGV_ARRAY_INIT;
	struct strbuf *plain = &s->plain, *colored = NULL;
	struct child_process cp = CHILD_PROCESS_INIT;
	char *p, *pend, *colored_p = NULL, *colored_pend = NULL;
	size_t i, color_arg_index;
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
			if (colored)
				hunk->colored_start = colored_p - colored->buf;
		}

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
	}

	return 0;
}

static void render_hunk(struct add_p_state *s, struct hunk *hunk,
			int colored, struct strbuf *out)
{
	if (colored)
		strbuf_add(out, s->colored.buf + hunk->colored_start,
			   hunk->colored_end - hunk->colored_start);
	else
		strbuf_add(out, s->plain.buf + hunk->start,
			   hunk->end - hunk->start);
}

static void reassemble_patch(struct add_p_state *s, struct strbuf *out)
{
	struct hunk *hunk;
	size_t i;

	render_hunk(s, &s->head, 0, out);

	for (i = 0; i < s->hunk_nr; i++) {
		hunk = s->hunk + i;
		if (hunk->use == USE_HUNK)
			render_hunk(s, hunk, 0, out);
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
	int colored = !!s->colored.len;

	if (!s->hunk_nr)
		return 0;

	strbuf_reset(&s->buf);
	render_hunk(s, &s->head, colored, &s->buf);
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
		render_hunk(s, hunk, colored, &s->buf);
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

		setup_child_process(&cp, s, "apply", "--cached", NULL);
		if (pipe_command(&cp, s->buf.buf, s->buf.len,
				 NULL, 0, NULL, 0))
			error(_("'git apply --cached' failed"));
		repo_refresh_and_write_index(s->r, REFRESH_QUIET, 0);
	}

	putchar('\n');
	return 0;
}

int run_add_p(struct repository *r, const struct pathspec *ps)
{
	struct add_p_state s = { r, STRBUF_INIT, STRBUF_INIT, STRBUF_INIT };

	if (repo_refresh_and_write_index(r, REFRESH_QUIET, 0) < 0 ||
	    parse_diff(&s, ps) < 0) {
		strbuf_release(&s.plain);
		strbuf_release(&s.colored);
		return -1;
	}

	if (s.hunk_nr)
		patch_update_file(&s);

	strbuf_release(&s.answer);
	strbuf_release(&s.buf);
	strbuf_release(&s.plain);
	strbuf_release(&s.colored);
	return 0;
}
