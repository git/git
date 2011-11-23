#include "cache.h"
#include "commit.h"
#include "blob.h"
#include "diff.h"
#include "diffcore.h"
#include "quote.h"
#include "xdiff-interface.h"
#include "log-tree.h"
#include "refs.h"
#include "userdiff.h"

static struct combine_diff_path *intersect_paths(struct combine_diff_path *curr, int n, int num_parent)
{
	struct diff_queue_struct *q = &diff_queued_diff;
	struct combine_diff_path *p;
	int i;

	if (!n) {
		struct combine_diff_path *list = NULL, **tail = &list;
		for (i = 0; i < q->nr; i++) {
			int len;
			const char *path;
			if (diff_unmodified_pair(q->queue[i]))
				continue;
			path = q->queue[i]->two->path;
			len = strlen(path);
			p = xmalloc(combine_diff_path_size(num_parent, len));
			p->path = (char *) &(p->parent[num_parent]);
			memcpy(p->path, path, len);
			p->path[len] = 0;
			p->len = len;
			p->next = NULL;
			memset(p->parent, 0,
			       sizeof(p->parent[0]) * num_parent);

			hashcpy(p->sha1, q->queue[i]->two->sha1);
			p->mode = q->queue[i]->two->mode;
			hashcpy(p->parent[n].sha1, q->queue[i]->one->sha1);
			p->parent[n].mode = q->queue[i]->one->mode;
			p->parent[n].status = q->queue[i]->status;
			*tail = p;
			tail = &p->next;
		}
		return list;
	}

	for (p = curr; p; p = p->next) {
		int found = 0;
		if (!p->len)
			continue;
		for (i = 0; i < q->nr; i++) {
			const char *path;
			int len;

			if (diff_unmodified_pair(q->queue[i]))
				continue;
			path = q->queue[i]->two->path;
			len = strlen(path);
			if (len == p->len && !memcmp(path, p->path, len)) {
				found = 1;
				hashcpy(p->parent[n].sha1, q->queue[i]->one->sha1);
				p->parent[n].mode = q->queue[i]->one->mode;
				p->parent[n].status = q->queue[i]->status;
				break;
			}
		}
		if (!found)
			p->len = 0;
	}
	return curr;
}

/* Lines lost from parent */
struct lline {
	struct lline *next;
	int len;
	unsigned long parent_map;
	char line[FLEX_ARRAY];
};

/* Lines surviving in the merge result */
struct sline {
	struct lline *lost_head, **lost_tail;
	struct lline *next_lost;
	char *bol;
	int len;
	/* bit 0 up to (N-1) are on if the parent has this line (i.e.
	 * we did not change it).
	 * bit N is used for "interesting" lines, including context.
	 * bit (N+1) is used for "do not show deletion before this".
	 */
	unsigned long flag;
	unsigned long *p_lno;
};

static char *grab_blob(const unsigned char *sha1, unsigned int mode,
		       unsigned long *size, struct userdiff_driver *textconv,
		       const char *path)
{
	char *blob;
	enum object_type type;

	if (S_ISGITLINK(mode)) {
		blob = xmalloc(100);
		*size = snprintf(blob, 100,
				 "Subproject commit %s\n", sha1_to_hex(sha1));
	} else if (is_null_sha1(sha1)) {
		/* deleted blob */
		*size = 0;
		return xcalloc(1, 1);
	} else if (textconv) {
		struct diff_filespec *df = alloc_filespec(path);
		fill_filespec(df, sha1, mode);
		*size = fill_textconv(textconv, df, &blob);
		free_filespec(df);
	} else {
		blob = read_sha1_file(sha1, &type, size);
		if (type != OBJ_BLOB)
			die("object '%s' is not a blob!", sha1_to_hex(sha1));
	}
	return blob;
}

static void append_lost(struct sline *sline, int n, const char *line, int len)
{
	struct lline *lline;
	unsigned long this_mask = (1UL<<n);
	if (line[len-1] == '\n')
		len--;

	/* Check to see if we can squash things */
	if (sline->lost_head) {
		lline = sline->next_lost;
		while (lline) {
			if (lline->len == len &&
			    !memcmp(lline->line, line, len)) {
				lline->parent_map |= this_mask;
				sline->next_lost = lline->next;
				return;
			}
			lline = lline->next;
		}
	}

	lline = xmalloc(sizeof(*lline) + len + 1);
	lline->len = len;
	lline->next = NULL;
	lline->parent_map = this_mask;
	memcpy(lline->line, line, len);
	lline->line[len] = 0;
	*sline->lost_tail = lline;
	sline->lost_tail = &lline->next;
	sline->next_lost = NULL;
}

struct combine_diff_state {
	unsigned int lno;
	int ob, on, nb, nn;
	unsigned long nmask;
	int num_parent;
	int n;
	struct sline *sline;
	struct sline *lost_bucket;
};

static void consume_line(void *state_, char *line, unsigned long len)
{
	struct combine_diff_state *state = state_;
	if (5 < len && !memcmp("@@ -", line, 4)) {
		if (parse_hunk_header(line, len,
				      &state->ob, &state->on,
				      &state->nb, &state->nn))
			return;
		state->lno = state->nb;
		if (state->nn == 0) {
			/* @@ -X,Y +N,0 @@ removed Y lines
			 * that would have come *after* line N
			 * in the result.  Our lost buckets hang
			 * to the line after the removed lines,
			 *
			 * Note that this is correct even when N == 0,
			 * in which case the hunk removes the first
			 * line in the file.
			 */
			state->lost_bucket = &state->sline[state->nb];
			if (!state->nb)
				state->nb = 1;
		} else {
			state->lost_bucket = &state->sline[state->nb-1];
		}
		if (!state->sline[state->nb-1].p_lno)
			state->sline[state->nb-1].p_lno =
				xcalloc(state->num_parent,
					sizeof(unsigned long));
		state->sline[state->nb-1].p_lno[state->n] = state->ob;
		state->lost_bucket->next_lost = state->lost_bucket->lost_head;
		return;
	}
	if (!state->lost_bucket)
		return; /* not in any hunk yet */
	switch (line[0]) {
	case '-':
		append_lost(state->lost_bucket, state->n, line+1, len-1);
		break;
	case '+':
		state->sline[state->lno-1].flag |= state->nmask;
		state->lno++;
		break;
	}
}

static void combine_diff(const unsigned char *parent, unsigned int mode,
			 mmfile_t *result_file,
			 struct sline *sline, unsigned int cnt, int n,
			 int num_parent, int result_deleted,
			 struct userdiff_driver *textconv,
			 const char *path)
{
	unsigned int p_lno, lno;
	unsigned long nmask = (1UL << n);
	xpparam_t xpp;
	xdemitconf_t xecfg;
	mmfile_t parent_file;
	struct combine_diff_state state;
	unsigned long sz;

	if (result_deleted)
		return; /* result deleted */

	parent_file.ptr = grab_blob(parent, mode, &sz, textconv, path);
	parent_file.size = sz;
	memset(&xpp, 0, sizeof(xpp));
	xpp.flags = 0;
	memset(&xecfg, 0, sizeof(xecfg));
	memset(&state, 0, sizeof(state));
	state.nmask = nmask;
	state.sline = sline;
	state.lno = 1;
	state.num_parent = num_parent;
	state.n = n;

	xdi_diff_outf(&parent_file, result_file, consume_line, &state,
		      &xpp, &xecfg);
	free(parent_file.ptr);

	/* Assign line numbers for this parent.
	 *
	 * sline[lno].p_lno[n] records the first line number
	 * (counting from 1) for parent N if the final hunk display
	 * started by showing sline[lno] (possibly showing the lost
	 * lines attached to it first).
	 */
	for (lno = 0,  p_lno = 1; lno <= cnt; lno++) {
		struct lline *ll;
		sline[lno].p_lno[n] = p_lno;

		/* How many lines would this sline advance the p_lno? */
		ll = sline[lno].lost_head;
		while (ll) {
			if (ll->parent_map & nmask)
				p_lno++; /* '-' means parent had it */
			ll = ll->next;
		}
		if (lno < cnt && !(sline[lno].flag & nmask))
			p_lno++; /* no '+' means parent had it */
	}
	sline[lno].p_lno[n] = p_lno; /* trailer */
}

static unsigned long context = 3;
static char combine_marker = '@';

static int interesting(struct sline *sline, unsigned long all_mask)
{
	/* If some parents lost lines here, or if we have added to
	 * some parent, it is interesting.
	 */
	return ((sline->flag & all_mask) || sline->lost_head);
}

static unsigned long adjust_hunk_tail(struct sline *sline,
				      unsigned long all_mask,
				      unsigned long hunk_begin,
				      unsigned long i)
{
	/* i points at the first uninteresting line.  If the last line
	 * of the hunk was interesting only because it has some
	 * deletion, then it is not all that interesting for the
	 * purpose of giving trailing context lines.  This is because
	 * we output '-' line and then unmodified sline[i-1] itself in
	 * that case which gives us one extra context line.
	 */
	if ((hunk_begin + 1 <= i) && !(sline[i-1].flag & all_mask))
		i--;
	return i;
}

static unsigned long find_next(struct sline *sline,
			       unsigned long mark,
			       unsigned long i,
			       unsigned long cnt,
			       int look_for_uninteresting)
{
	/* We have examined up to i-1 and are about to look at i.
	 * Find next interesting or uninteresting line.  Here,
	 * "interesting" does not mean interesting(), but marked by
	 * the give_context() function below (i.e. it includes context
	 * lines that are not interesting to interesting() function
	 * that are surrounded by interesting() ones.
	 */
	while (i <= cnt)
		if (look_for_uninteresting
		    ? !(sline[i].flag & mark)
		    : (sline[i].flag & mark))
			return i;
		else
			i++;
	return i;
}

static int give_context(struct sline *sline, unsigned long cnt, int num_parent)
{
	unsigned long all_mask = (1UL<<num_parent) - 1;
	unsigned long mark = (1UL<<num_parent);
	unsigned long no_pre_delete = (2UL<<num_parent);
	unsigned long i;

	/* Two groups of interesting lines may have a short gap of
	 * uninteresting lines.  Connect such groups to give them a
	 * bit of context.
	 *
	 * We first start from what the interesting() function says,
	 * and mark them with "mark", and paint context lines with the
	 * mark.  So interesting() would still say false for such context
	 * lines but they are treated as "interesting" in the end.
	 */
	i = find_next(sline, mark, 0, cnt, 0);
	if (cnt < i)
		return 0;

	while (i <= cnt) {
		unsigned long j = (context < i) ? (i - context) : 0;
		unsigned long k;

		/* Paint a few lines before the first interesting line. */
		while (j < i)
			sline[j++].flag |= mark | no_pre_delete;

	again:
		/* we know up to i is to be included.  where does the
		 * next uninteresting one start?
		 */
		j = find_next(sline, mark, i, cnt, 1);
		if (cnt < j)
			break; /* the rest are all interesting */

		/* lookahead context lines */
		k = find_next(sline, mark, j, cnt, 0);
		j = adjust_hunk_tail(sline, all_mask, i, j);

		if (k < j + context) {
			/* k is interesting and [j,k) are not, but
			 * paint them interesting because the gap is small.
			 */
			while (j < k)
				sline[j++].flag |= mark;
			i = k;
			goto again;
		}

		/* j is the first uninteresting line and there is
		 * no overlap beyond it within context lines.  Paint
		 * the trailing edge a bit.
		 */
		i = k;
		k = (j + context < cnt+1) ? j + context : cnt+1;
		while (j < k)
			sline[j++].flag |= mark;
	}
	return 1;
}

static int make_hunks(struct sline *sline, unsigned long cnt,
		       int num_parent, int dense)
{
	unsigned long all_mask = (1UL<<num_parent) - 1;
	unsigned long mark = (1UL<<num_parent);
	unsigned long i;
	int has_interesting = 0;

	for (i = 0; i <= cnt; i++) {
		if (interesting(&sline[i], all_mask))
			sline[i].flag |= mark;
		else
			sline[i].flag &= ~mark;
	}
	if (!dense)
		return give_context(sline, cnt, num_parent);

	/* Look at each hunk, and if we have changes from only one
	 * parent, or the changes are the same from all but one
	 * parent, mark that uninteresting.
	 */
	i = 0;
	while (i <= cnt) {
		unsigned long j, hunk_begin, hunk_end;
		unsigned long same_diff;
		while (i <= cnt && !(sline[i].flag & mark))
			i++;
		if (cnt < i)
			break; /* No more interesting hunks */
		hunk_begin = i;
		for (j = i + 1; j <= cnt; j++) {
			if (!(sline[j].flag & mark)) {
				/* Look beyond the end to see if there
				 * is an interesting line after this
				 * hunk within context span.
				 */
				unsigned long la; /* lookahead */
				int contin = 0;
				la = adjust_hunk_tail(sline, all_mask,
						     hunk_begin, j);
				la = (la + context < cnt + 1) ?
					(la + context) : cnt + 1;
				while (j <= --la) {
					if (sline[la].flag & mark) {
						contin = 1;
						break;
					}
				}
				if (!contin)
					break;
				j = la;
			}
		}
		hunk_end = j;

		/* [i..hunk_end) are interesting.  Now is it really
		 * interesting?  We check if there are only two versions
		 * and the result matches one of them.  That is, we look
		 * at:
		 *   (+) line, which records lines added to which parents;
		 *       this line appears in the result.
		 *   (-) line, which records from what parents the line
		 *       was removed; this line does not appear in the result.
		 * then check the set of parents the result has difference
		 * from, from all lines.  If there are lines that has
		 * different set of parents that the result has differences
		 * from, that means we have more than two versions.
		 *
		 * Even when we have only two versions, if the result does
		 * not match any of the parents, the it should be considered
		 * interesting.  In such a case, we would have all '+' line.
		 * After passing the above "two versions" test, that would
		 * appear as "the same set of parents" to be "all parents".
		 */
		same_diff = 0;
		has_interesting = 0;
		for (j = i; j < hunk_end && !has_interesting; j++) {
			unsigned long this_diff = sline[j].flag & all_mask;
			struct lline *ll = sline[j].lost_head;
			if (this_diff) {
				/* This has some changes.  Is it the
				 * same as others?
				 */
				if (!same_diff)
					same_diff = this_diff;
				else if (same_diff != this_diff) {
					has_interesting = 1;
					break;
				}
			}
			while (ll && !has_interesting) {
				/* Lost this line from these parents;
				 * who are they?  Are they the same?
				 */
				this_diff = ll->parent_map;
				if (!same_diff)
					same_diff = this_diff;
				else if (same_diff != this_diff) {
					has_interesting = 1;
				}
				ll = ll->next;
			}
		}

		if (!has_interesting && same_diff != all_mask) {
			/* This hunk is not that interesting after all */
			for (j = hunk_begin; j < hunk_end; j++)
				sline[j].flag &= ~mark;
		}
		i = hunk_end;
	}

	has_interesting = give_context(sline, cnt, num_parent);
	return has_interesting;
}

static void show_parent_lno(struct sline *sline, unsigned long l0, unsigned long l1, int n, unsigned long null_context)
{
	l0 = sline[l0].p_lno[n];
	l1 = sline[l1].p_lno[n];
	printf(" -%lu,%lu", l0, l1-l0-null_context);
}

static int hunk_comment_line(const char *bol)
{
	int ch;

	if (!bol)
		return 0;
	ch = *bol & 0xff;
	return (isalpha(ch) || ch == '_' || ch == '$');
}

static void show_line_to_eol(const char *line, int len, const char *reset)
{
	int saw_cr_at_eol = 0;
	if (len < 0)
		len = strlen(line);
	saw_cr_at_eol = (len && line[len-1] == '\r');

	printf("%.*s%s%s\n", len - saw_cr_at_eol, line,
	       reset,
	       saw_cr_at_eol ? "\r" : "");
}

static void dump_sline(struct sline *sline, unsigned long cnt, int num_parent,
		       int use_color, int result_deleted)
{
	unsigned long mark = (1UL<<num_parent);
	unsigned long no_pre_delete = (2UL<<num_parent);
	int i;
	unsigned long lno = 0;
	const char *c_frag = diff_get_color(use_color, DIFF_FRAGINFO);
	const char *c_func = diff_get_color(use_color, DIFF_FUNCINFO);
	const char *c_new = diff_get_color(use_color, DIFF_FILE_NEW);
	const char *c_old = diff_get_color(use_color, DIFF_FILE_OLD);
	const char *c_plain = diff_get_color(use_color, DIFF_PLAIN);
	const char *c_reset = diff_get_color(use_color, DIFF_RESET);

	if (result_deleted)
		return; /* result deleted */

	while (1) {
		unsigned long hunk_end;
		unsigned long rlines;
		const char *hunk_comment = NULL;
		unsigned long null_context = 0;

		while (lno <= cnt && !(sline[lno].flag & mark)) {
			if (hunk_comment_line(sline[lno].bol))
				hunk_comment = sline[lno].bol;
			lno++;
		}
		if (cnt < lno)
			break;
		else {
			for (hunk_end = lno + 1; hunk_end <= cnt; hunk_end++)
				if (!(sline[hunk_end].flag & mark))
					break;
		}
		rlines = hunk_end - lno;
		if (cnt < hunk_end)
			rlines--; /* pointing at the last delete hunk */

		if (!context) {
			/*
			 * Even when running with --unified=0, all
			 * lines in the hunk needs to be processed in
			 * the loop below in order to show the
			 * deletion recorded in lost_head.  However,
			 * we do not want to show the resulting line
			 * with all blank context markers in such a
			 * case.  Compensate.
			 */
			unsigned long j;
			for (j = lno; j < hunk_end; j++)
				if (!(sline[j].flag & (mark-1)))
					null_context++;
			rlines -= null_context;
		}

		fputs(c_frag, stdout);
		for (i = 0; i <= num_parent; i++) putchar(combine_marker);
		for (i = 0; i < num_parent; i++)
			show_parent_lno(sline, lno, hunk_end, i, null_context);
		printf(" +%lu,%lu ", lno+1, rlines);
		for (i = 0; i <= num_parent; i++) putchar(combine_marker);

		if (hunk_comment) {
			int comment_end = 0;
			for (i = 0; i < 40; i++) {
				int ch = hunk_comment[i] & 0xff;
				if (!ch || ch == '\n')
					break;
				if (!isspace(ch))
				    comment_end = i;
			}
			if (comment_end)
				printf("%s%s %s%s", c_reset,
						    c_plain, c_reset,
						    c_func);
			for (i = 0; i < comment_end; i++)
				putchar(hunk_comment[i]);
		}

		printf("%s\n", c_reset);
		while (lno < hunk_end) {
			struct lline *ll;
			int j;
			unsigned long p_mask;
			struct sline *sl = &sline[lno++];
			ll = (sl->flag & no_pre_delete) ? NULL : sl->lost_head;
			while (ll) {
				fputs(c_old, stdout);
				for (j = 0; j < num_parent; j++) {
					if (ll->parent_map & (1UL<<j))
						putchar('-');
					else
						putchar(' ');
				}
				show_line_to_eol(ll->line, -1, c_reset);
				ll = ll->next;
			}
			if (cnt < lno)
				break;
			p_mask = 1;
			if (!(sl->flag & (mark-1))) {
				/*
				 * This sline was here to hang the
				 * lost lines in front of it.
				 */
				if (!context)
					continue;
				fputs(c_plain, stdout);
			}
			else
				fputs(c_new, stdout);
			for (j = 0; j < num_parent; j++) {
				if (p_mask & sl->flag)
					putchar('+');
				else
					putchar(' ');
				p_mask <<= 1;
			}
			show_line_to_eol(sl->bol, sl->len, c_reset);
		}
	}
}

static void reuse_combine_diff(struct sline *sline, unsigned long cnt,
			       int i, int j)
{
	/* We have already examined parent j and we know parent i
	 * and parent j are the same, so reuse the combined result
	 * of parent j for parent i.
	 */
	unsigned long lno, imask, jmask;
	imask = (1UL<<i);
	jmask = (1UL<<j);

	for (lno = 0; lno <= cnt; lno++) {
		struct lline *ll = sline->lost_head;
		sline->p_lno[i] = sline->p_lno[j];
		while (ll) {
			if (ll->parent_map & jmask)
				ll->parent_map |= imask;
			ll = ll->next;
		}
		if (sline->flag & jmask)
			sline->flag |= imask;
		sline++;
	}
	/* the overall size of the file (sline[cnt]) */
	sline->p_lno[i] = sline->p_lno[j];
}

static void dump_quoted_path(const char *head,
			     const char *prefix,
			     const char *path,
			     const char *c_meta, const char *c_reset)
{
	static struct strbuf buf = STRBUF_INIT;

	strbuf_reset(&buf);
	strbuf_addstr(&buf, c_meta);
	strbuf_addstr(&buf, head);
	quote_two_c_style(&buf, prefix, path, 0);
	strbuf_addstr(&buf, c_reset);
	puts(buf.buf);
}

static void show_combined_header(struct combine_diff_path *elem,
				 int num_parent,
				 int dense,
				 struct rev_info *rev,
				 int mode_differs,
				 int show_file_header)
{
	struct diff_options *opt = &rev->diffopt;
	int abbrev = DIFF_OPT_TST(opt, FULL_INDEX) ? 40 : DEFAULT_ABBREV;
	const char *a_prefix = opt->a_prefix ? opt->a_prefix : "a/";
	const char *b_prefix = opt->b_prefix ? opt->b_prefix : "b/";
	const char *c_meta = diff_get_color_opt(opt, DIFF_METAINFO);
	const char *c_reset = diff_get_color_opt(opt, DIFF_RESET);
	const char *abb;
	int added = 0;
	int deleted = 0;
	int i;

	if (rev->loginfo && !rev->no_commit_id)
		show_log(rev);

	dump_quoted_path(dense ? "diff --cc " : "diff --combined ",
			 "", elem->path, c_meta, c_reset);
	printf("%sindex ", c_meta);
	for (i = 0; i < num_parent; i++) {
		abb = find_unique_abbrev(elem->parent[i].sha1,
					 abbrev);
		printf("%s%s", i ? "," : "", abb);
	}
	abb = find_unique_abbrev(elem->sha1, abbrev);
	printf("..%s%s\n", abb, c_reset);

	if (mode_differs) {
		deleted = !elem->mode;

		/* We say it was added if nobody had it */
		added = !deleted;
		for (i = 0; added && i < num_parent; i++)
			if (elem->parent[i].status !=
			    DIFF_STATUS_ADDED)
				added = 0;
		if (added)
			printf("%snew file mode %06o",
			       c_meta, elem->mode);
		else {
			if (deleted)
				printf("%sdeleted file ", c_meta);
			printf("mode ");
			for (i = 0; i < num_parent; i++) {
				printf("%s%06o", i ? "," : "",
				       elem->parent[i].mode);
			}
			if (elem->mode)
				printf("..%06o", elem->mode);
		}
		printf("%s\n", c_reset);
	}

	if (!show_file_header)
		return;

	if (added)
		dump_quoted_path("--- ", "", "/dev/null",
				 c_meta, c_reset);
	else
		dump_quoted_path("--- ", a_prefix, elem->path,
				 c_meta, c_reset);
	if (deleted)
		dump_quoted_path("+++ ", "", "/dev/null",
				 c_meta, c_reset);
	else
		dump_quoted_path("+++ ", b_prefix, elem->path,
				 c_meta, c_reset);
}

static void show_patch_diff(struct combine_diff_path *elem, int num_parent,
			    int dense, int working_tree_file,
			    struct rev_info *rev)
{
	struct diff_options *opt = &rev->diffopt;
	unsigned long result_size, cnt, lno;
	int result_deleted = 0;
	char *result, *cp;
	struct sline *sline; /* survived lines */
	int mode_differs = 0;
	int i, show_hunks;
	mmfile_t result_file;
	struct userdiff_driver *userdiff;
	struct userdiff_driver *textconv = NULL;
	int is_binary;

	context = opt->context;
	userdiff = userdiff_find_by_path(elem->path);
	if (!userdiff)
		userdiff = userdiff_find_by_name("default");
	if (DIFF_OPT_TST(opt, ALLOW_TEXTCONV))
		textconv = userdiff_get_textconv(userdiff);

	/* Read the result of merge first */
	if (!working_tree_file)
		result = grab_blob(elem->sha1, elem->mode, &result_size,
				   textconv, elem->path);
	else {
		/* Used by diff-tree to read from the working tree */
		struct stat st;
		int fd = -1;

		if (lstat(elem->path, &st) < 0)
			goto deleted_file;

		if (S_ISLNK(st.st_mode)) {
			struct strbuf buf = STRBUF_INIT;

			if (strbuf_readlink(&buf, elem->path, st.st_size) < 0) {
				error("readlink(%s): %s", elem->path,
				      strerror(errno));
				return;
			}
			result_size = buf.len;
			result = strbuf_detach(&buf, NULL);
			elem->mode = canon_mode(st.st_mode);
		} else if (S_ISDIR(st.st_mode)) {
			unsigned char sha1[20];
			if (resolve_gitlink_ref(elem->path, "HEAD", sha1) < 0)
				result = grab_blob(elem->sha1, elem->mode,
						   &result_size, NULL, NULL);
			else
				result = grab_blob(sha1, elem->mode,
						   &result_size, NULL, NULL);
		} else if (textconv) {
			struct diff_filespec *df = alloc_filespec(elem->path);
			fill_filespec(df, null_sha1, st.st_mode);
			result_size = fill_textconv(textconv, df, &result);
			free_filespec(df);
		} else if (0 <= (fd = open(elem->path, O_RDONLY))) {
			size_t len = xsize_t(st.st_size);
			ssize_t done;
			int is_file, i;

			elem->mode = canon_mode(st.st_mode);
			/* if symlinks don't work, assume symlink if all parents
			 * are symlinks
			 */
			is_file = has_symlinks;
			for (i = 0; !is_file && i < num_parent; i++)
				is_file = !S_ISLNK(elem->parent[i].mode);
			if (!is_file)
				elem->mode = canon_mode(S_IFLNK);

			result_size = len;
			result = xmalloc(len + 1);

			done = read_in_full(fd, result, len);
			if (done < 0)
				die_errno("read error '%s'", elem->path);
			else if (done < len)
				die("early EOF '%s'", elem->path);

			result[len] = 0;

			/* If not a fake symlink, apply filters, e.g. autocrlf */
			if (is_file) {
				struct strbuf buf = STRBUF_INIT;

				if (convert_to_git(elem->path, result, len, &buf, safe_crlf)) {
					free(result);
					result = strbuf_detach(&buf, &len);
					result_size = len;
				}
			}
		}
		else {
		deleted_file:
			result_deleted = 1;
			result_size = 0;
			elem->mode = 0;
			result = xcalloc(1, 1);
		}

		if (0 <= fd)
			close(fd);
	}

	for (i = 0; i < num_parent; i++) {
		if (elem->parent[i].mode != elem->mode) {
			mode_differs = 1;
			break;
		}
	}

	if (textconv)
		is_binary = 0;
	else if (userdiff->binary != -1)
		is_binary = userdiff->binary;
	else {
		is_binary = buffer_is_binary(result, result_size);
		for (i = 0; !is_binary && i < num_parent; i++) {
			char *buf;
			unsigned long size;
			buf = grab_blob(elem->parent[i].sha1,
					elem->parent[i].mode,
					&size, NULL, NULL);
			if (buffer_is_binary(buf, size))
				is_binary = 1;
			free(buf);
		}
	}
	if (is_binary) {
		show_combined_header(elem, num_parent, dense, rev,
				     mode_differs, 0);
		printf("Binary files differ\n");
		free(result);
		return;
	}

	for (cnt = 0, cp = result; cp < result + result_size; cp++) {
		if (*cp == '\n')
			cnt++;
	}
	if (result_size && result[result_size-1] != '\n')
		cnt++; /* incomplete line */

	sline = xcalloc(cnt+2, sizeof(*sline));
	sline[0].bol = result;
	for (lno = 0; lno <= cnt + 1; lno++) {
		sline[lno].lost_tail = &sline[lno].lost_head;
		sline[lno].flag = 0;
	}
	for (lno = 0, cp = result; cp < result + result_size; cp++) {
		if (*cp == '\n') {
			sline[lno].len = cp - sline[lno].bol;
			lno++;
			if (lno < cnt)
				sline[lno].bol = cp + 1;
		}
	}
	if (result_size && result[result_size-1] != '\n')
		sline[cnt-1].len = result_size - (sline[cnt-1].bol - result);

	result_file.ptr = result;
	result_file.size = result_size;

	/* Even p_lno[cnt+1] is valid -- that is for the end line number
	 * for deletion hunk at the end.
	 */
	sline[0].p_lno = xcalloc((cnt+2) * num_parent, sizeof(unsigned long));
	for (lno = 0; lno <= cnt; lno++)
		sline[lno+1].p_lno = sline[lno].p_lno + num_parent;

	for (i = 0; i < num_parent; i++) {
		int j;
		for (j = 0; j < i; j++) {
			if (!hashcmp(elem->parent[i].sha1,
				     elem->parent[j].sha1)) {
				reuse_combine_diff(sline, cnt, i, j);
				break;
			}
		}
		if (i <= j)
			combine_diff(elem->parent[i].sha1,
				     elem->parent[i].mode,
				     &result_file, sline,
				     cnt, i, num_parent, result_deleted,
				     textconv, elem->path);
	}

	show_hunks = make_hunks(sline, cnt, num_parent, dense);

	if (show_hunks || mode_differs || working_tree_file) {
		show_combined_header(elem, num_parent, dense, rev,
				     mode_differs, 1);
		dump_sline(sline, cnt, num_parent,
			   opt->use_color, result_deleted);
	}
	free(result);

	for (lno = 0; lno < cnt; lno++) {
		if (sline[lno].lost_head) {
			struct lline *ll = sline[lno].lost_head;
			while (ll) {
				struct lline *tmp = ll;
				ll = ll->next;
				free(tmp);
			}
		}
	}
	free(sline[0].p_lno);
	free(sline);
}

#define COLONS "::::::::::::::::::::::::::::::::"

static void show_raw_diff(struct combine_diff_path *p, int num_parent, struct rev_info *rev)
{
	struct diff_options *opt = &rev->diffopt;
	int i, offset;
	const char *prefix;
	int line_termination, inter_name_termination;

	line_termination = opt->line_termination;
	inter_name_termination = '\t';
	if (!line_termination)
		inter_name_termination = 0;

	if (rev->loginfo && !rev->no_commit_id)
		show_log(rev);

	if (opt->output_format & DIFF_FORMAT_RAW) {
		offset = strlen(COLONS) - num_parent;
		if (offset < 0)
			offset = 0;
		prefix = COLONS + offset;

		/* Show the modes */
		for (i = 0; i < num_parent; i++) {
			printf("%s%06o", prefix, p->parent[i].mode);
			prefix = " ";
		}
		printf("%s%06o", prefix, p->mode);

		/* Show sha1's */
		for (i = 0; i < num_parent; i++)
			printf(" %s", diff_unique_abbrev(p->parent[i].sha1,
							 opt->abbrev));
		printf(" %s ", diff_unique_abbrev(p->sha1, opt->abbrev));
	}

	if (opt->output_format & (DIFF_FORMAT_RAW | DIFF_FORMAT_NAME_STATUS)) {
		for (i = 0; i < num_parent; i++)
			putchar(p->parent[i].status);
		putchar(inter_name_termination);
	}

	write_name_quoted(p->path, stdout, line_termination);
}

/*
 * The result (p->elem) is from the working tree and their
 * parents are typically from multiple stages during a merge
 * (i.e. diff-files) or the state in HEAD and in the index
 * (i.e. diff-index).
 */
void show_combined_diff(struct combine_diff_path *p,
		       int num_parent,
		       int dense,
		       struct rev_info *rev)
{
	struct diff_options *opt = &rev->diffopt;
	if (!p->len)
		return;
	if (opt->output_format & (DIFF_FORMAT_RAW |
				  DIFF_FORMAT_NAME |
				  DIFF_FORMAT_NAME_STATUS))
		show_raw_diff(p, num_parent, rev);
	else if (opt->output_format & DIFF_FORMAT_PATCH)
		show_patch_diff(p, num_parent, dense, 1, rev);
}

static void free_combined_pair(struct diff_filepair *pair)
{
	free(pair->two);
	free(pair);
}

/*
 * A combine_diff_path expresses N parents on the LHS against 1 merge
 * result. Synthesize a diff_filepair that has N entries on the "one"
 * side and 1 entry on the "two" side.
 *
 * In the future, we might want to add more data to combine_diff_path
 * so that we can fill fields we are ignoring (most notably, size) here,
 * but currently nobody uses it, so this should suffice for now.
 */
static struct diff_filepair *combined_pair(struct combine_diff_path *p,
					   int num_parent)
{
	int i;
	struct diff_filepair *pair;
	struct diff_filespec *pool;

	pair = xmalloc(sizeof(*pair));
	pool = xcalloc(num_parent + 1, sizeof(struct diff_filespec));
	pair->one = pool + 1;
	pair->two = pool;

	for (i = 0; i < num_parent; i++) {
		pair->one[i].path = p->path;
		pair->one[i].mode = p->parent[i].mode;
		hashcpy(pair->one[i].sha1, p->parent[i].sha1);
		pair->one[i].sha1_valid = !is_null_sha1(p->parent[i].sha1);
		pair->one[i].has_more_entries = 1;
	}
	pair->one[num_parent - 1].has_more_entries = 0;

	pair->two->path = p->path;
	pair->two->mode = p->mode;
	hashcpy(pair->two->sha1, p->sha1);
	pair->two->sha1_valid = !is_null_sha1(p->sha1);
	return pair;
}

static void handle_combined_callback(struct diff_options *opt,
				     struct combine_diff_path *paths,
				     int num_parent,
				     int num_paths)
{
	struct combine_diff_path *p;
	struct diff_queue_struct q;
	int i;

	q.queue = xcalloc(num_paths, sizeof(struct diff_filepair *));
	q.alloc = num_paths;
	q.nr = num_paths;
	for (i = 0, p = paths; p; p = p->next) {
		if (!p->len)
			continue;
		q.queue[i++] = combined_pair(p, num_parent);
	}
	opt->format_callback(&q, opt, opt->format_callback_data);
	for (i = 0; i < num_paths; i++)
		free_combined_pair(q.queue[i]);
	free(q.queue);
}

void diff_tree_combined(const unsigned char *sha1,
			const unsigned char parent[][20],
			int num_parent,
			int dense,
			struct rev_info *rev)
{
	struct diff_options *opt = &rev->diffopt;
	struct diff_options diffopts;
	struct combine_diff_path *p, *paths = NULL;
	int i, num_paths, needsep, show_log_first;

	diffopts = *opt;
	diffopts.output_format = DIFF_FORMAT_NO_OUTPUT;
	DIFF_OPT_SET(&diffopts, RECURSIVE);
	DIFF_OPT_CLR(&diffopts, ALLOW_EXTERNAL);

	show_log_first = !!rev->loginfo && !rev->no_commit_id;
	needsep = 0;
	/* find set of paths that everybody touches */
	for (i = 0; i < num_parent; i++) {
		/* show stat against the first parent even
		 * when doing combined diff.
		 */
		int stat_opt = (opt->output_format &
				(DIFF_FORMAT_NUMSTAT|DIFF_FORMAT_DIFFSTAT));
		if (i == 0 && stat_opt)
			diffopts.output_format = stat_opt;
		else
			diffopts.output_format = DIFF_FORMAT_NO_OUTPUT;
		diff_tree_sha1(parent[i], sha1, "", &diffopts);
		diffcore_std(&diffopts);
		paths = intersect_paths(paths, i, num_parent);

		if (show_log_first && i == 0) {
			show_log(rev);
			if (rev->verbose_header && opt->output_format)
				putchar(opt->line_termination);
		}
		diff_flush(&diffopts);
	}

	/* find out surviving paths */
	for (num_paths = 0, p = paths; p; p = p->next) {
		if (p->len)
			num_paths++;
	}
	if (num_paths) {
		if (opt->output_format & (DIFF_FORMAT_RAW |
					  DIFF_FORMAT_NAME |
					  DIFF_FORMAT_NAME_STATUS)) {
			for (p = paths; p; p = p->next) {
				if (p->len)
					show_raw_diff(p, num_parent, rev);
			}
			needsep = 1;
		}
		else if (opt->output_format &
			 (DIFF_FORMAT_NUMSTAT|DIFF_FORMAT_DIFFSTAT))
			needsep = 1;
		else if (opt->output_format & DIFF_FORMAT_CALLBACK)
			handle_combined_callback(opt, paths, num_parent, num_paths);

		if (opt->output_format & DIFF_FORMAT_PATCH) {
			if (needsep)
				putchar(opt->line_termination);
			for (p = paths; p; p = p->next) {
				if (p->len)
					show_patch_diff(p, num_parent, dense,
							0, rev);
			}
		}
	}

	/* Clean things up */
	while (paths) {
		struct combine_diff_path *tmp = paths;
		paths = paths->next;
		free(tmp);
	}
}

void diff_tree_combined_merge(const unsigned char *sha1,
			     int dense, struct rev_info *rev)
{
	int num_parent;
	const unsigned char (*parent)[20];
	struct commit *commit = lookup_commit(sha1);
	struct commit_list *parents;

	/* count parents */
	for (parents = commit->parents, num_parent = 0;
	     parents;
	     parents = parents->next, num_parent++)
		; /* nothing */

	parent = xmalloc(num_parent * sizeof(*parent));
	for (parents = commit->parents, num_parent = 0;
	     parents;
	     parents = parents->next, num_parent++)
		hashcpy((unsigned char *)(parent + num_parent),
			parents->item->object.sha1);
	diff_tree_combined(sha1, parent, num_parent, dense, rev);
}
