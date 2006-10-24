#include "cache.h"
#include "commit.h"
#include "blob.h"
#include "diff.h"
#include "diffcore.h"
#include "quote.h"
#include "xdiff-interface.h"
#include "log-tree.h"

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
			p->path = (char*) &(p->parent[num_parent]);
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
	char *bol;
	int len;
	/* bit 0 up to (N-1) are on if the parent has this line (i.e.
	 * we did not change it).
	 * bit N is used for "interesting" lines, including context.
	 */
	unsigned long flag;
	unsigned long *p_lno;
};

static char *grab_blob(const unsigned char *sha1, unsigned long *size)
{
	char *blob;
	char type[20];
	if (is_null_sha1(sha1)) {
		/* deleted blob */
		*size = 0;
		return xcalloc(1, 1);
	}
	blob = read_sha1_file(sha1, type, size);
	if (strcmp(type, blob_type))
		die("object '%s' is not a blob!", sha1_to_hex(sha1));
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
		struct lline *last_one = NULL;
		/* We cannot squash it with earlier one */
		for (lline = sline->lost_head;
		     lline;
		     lline = lline->next)
			if (lline->parent_map & this_mask)
				last_one = lline;
		lline = last_one ? last_one->next : sline->lost_head;
		while (lline) {
			if (lline->len == len &&
			    !memcmp(lline->line, line, len)) {
				lline->parent_map |= this_mask;
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
}

struct combine_diff_state {
	struct xdiff_emit_state xm;

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
		if (!state->nb)
			/* @@ -1,2 +0,0 @@ to remove the
			 * first two lines...
			 */
			state->nb = 1;
		if (state->nn == 0)
			/* @@ -X,Y +N,0 @@ removed Y lines
			 * that would have come *after* line N
			 * in the result.  Our lost buckets hang
			 * to the line after the removed lines,
			 */
			state->lost_bucket = &state->sline[state->nb];
		else
			state->lost_bucket = &state->sline[state->nb-1];
		if (!state->sline[state->nb-1].p_lno)
			state->sline[state->nb-1].p_lno =
				xcalloc(state->num_parent,
					sizeof(unsigned long));
		state->sline[state->nb-1].p_lno[state->n] = state->ob;
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

static void combine_diff(const unsigned char *parent, mmfile_t *result_file,
			 struct sline *sline, unsigned int cnt, int n,
			 int num_parent)
{
	unsigned int p_lno, lno;
	unsigned long nmask = (1UL << n);
	xpparam_t xpp;
	xdemitconf_t xecfg;
	mmfile_t parent_file;
	xdemitcb_t ecb;
	struct combine_diff_state state;
	unsigned long sz;

	if (!cnt)
		return; /* result deleted */

	parent_file.ptr = grab_blob(parent, &sz);
	parent_file.size = sz;
	xpp.flags = XDF_NEED_MINIMAL;
	xecfg.ctxlen = 0;
	xecfg.flags = 0;
	ecb.outf = xdiff_outf;
	ecb.priv = &state;
	memset(&state, 0, sizeof(state));
	state.xm.consume = consume_line;
	state.nmask = nmask;
	state.sline = sline;
	state.lno = 1;
	state.num_parent = num_parent;
	state.n = n;

	xdl_diff(&parent_file, result_file, &xpp, &xecfg, &ecb);
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
			sline[j++].flag |= mark;

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

static void show_parent_lno(struct sline *sline, unsigned long l0, unsigned long l1, int n)
{
	l0 = sline[l0].p_lno[n];
	l1 = sline[l1].p_lno[n];
	printf(" -%lu,%lu", l0, l1-l0);
}

static void dump_sline(struct sline *sline, unsigned long cnt, int num_parent,
		       int use_color)
{
	unsigned long mark = (1UL<<num_parent);
	int i;
	unsigned long lno = 0;
	const char *c_frag = diff_get_color(use_color, DIFF_FRAGINFO);
	const char *c_new = diff_get_color(use_color, DIFF_FILE_NEW);
	const char *c_old = diff_get_color(use_color, DIFF_FILE_OLD);
	const char *c_plain = diff_get_color(use_color, DIFF_PLAIN);
	const char *c_reset = diff_get_color(use_color, DIFF_RESET);

	if (!cnt)
		return; /* result deleted */

	while (1) {
		struct sline *sl = &sline[lno];
		unsigned long hunk_end;
		unsigned long rlines;
		while (lno <= cnt && !(sline[lno].flag & mark))
			lno++;
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
		fputs(c_frag, stdout);
		for (i = 0; i <= num_parent; i++) putchar(combine_marker);
		for (i = 0; i < num_parent; i++)
			show_parent_lno(sline, lno, hunk_end, i);
		printf(" +%lu,%lu ", lno+1, rlines);
		for (i = 0; i <= num_parent; i++) putchar(combine_marker);
		printf("%s\n", c_reset);
		while (lno < hunk_end) {
			struct lline *ll;
			int j;
			unsigned long p_mask;
			sl = &sline[lno++];
			ll = sl->lost_head;
			while (ll) {
				fputs(c_old, stdout);
				for (j = 0; j < num_parent; j++) {
					if (ll->parent_map & (1UL<<j))
						putchar('-');
					else
						putchar(' ');
				}
				printf("%s%s\n", ll->line, c_reset);
				ll = ll->next;
			}
			if (cnt < lno)
				break;
			p_mask = 1;
			if (!(sl->flag & (mark-1)))
				fputs(c_plain, stdout);
			else
				fputs(c_new, stdout);
			for (j = 0; j < num_parent; j++) {
				if (p_mask & sl->flag)
					putchar('+');
				else
					putchar(' ');
				p_mask <<= 1;
			}
			printf("%.*s%s\n", sl->len, sl->bol, c_reset);
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

static void dump_quoted_path(const char *prefix, const char *path,
			     const char *c_meta, const char *c_reset)
{
	printf("%s%s", c_meta, prefix);
	if (quote_c_style(path, NULL, NULL, 0))
		quote_c_style(path, NULL, stdout, 0);
	else
		printf("%s", path);
	printf("%s\n", c_reset);
}

static void show_patch_diff(struct combine_diff_path *elem, int num_parent,
			    int dense, struct rev_info *rev)
{
	struct diff_options *opt = &rev->diffopt;
	unsigned long result_size, cnt, lno;
	char *result, *cp;
	struct sline *sline; /* survived lines */
	int mode_differs = 0;
	int i, show_hunks;
	int working_tree_file = is_null_sha1(elem->sha1);
	int abbrev = opt->full_index ? 40 : DEFAULT_ABBREV;
	mmfile_t result_file;

	context = opt->context;
	/* Read the result of merge first */
	if (!working_tree_file)
		result = grab_blob(elem->sha1, &result_size);
	else {
		/* Used by diff-tree to read from the working tree */
		struct stat st;
		int fd;
		if (0 <= (fd = open(elem->path, O_RDONLY)) &&
		    !fstat(fd, &st)) {
			int len = st.st_size;
			int sz = 0;

			elem->mode = canon_mode(st.st_mode);
			result_size = len;
			result = xmalloc(len + 1);
			while (sz < len) {
				int done = xread(fd, result+sz, len-sz);
				if (done == 0)
					break;
				if (done < 0)
					die("read error '%s'", elem->path);
				sz += done;
			}
			result[len] = 0;
		}
		else {
			/* deleted file */
			result_size = 0;
			elem->mode = 0;
			result = xcalloc(1, 1);
		}
		if (0 <= fd)
			close(fd);
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
			combine_diff(elem->parent[i].sha1, &result_file, sline,
				     cnt, i, num_parent);
		if (elem->parent[i].mode != elem->mode)
			mode_differs = 1;
	}

	show_hunks = make_hunks(sline, cnt, num_parent, dense);

	if (show_hunks || mode_differs || working_tree_file) {
		const char *abb;
		int use_color = opt->color_diff;
		const char *c_meta = diff_get_color(use_color, DIFF_METAINFO);
		const char *c_reset = diff_get_color(use_color, DIFF_RESET);

		if (rev->loginfo)
			show_log(rev, opt->msg_sep);
		dump_quoted_path(dense ? "diff --cc " : "diff --combined ",
				 elem->path, c_meta, c_reset);
		printf("%sindex ", c_meta);
		for (i = 0; i < num_parent; i++) {
			abb = find_unique_abbrev(elem->parent[i].sha1,
						 abbrev);
			printf("%s%s", i ? "," : "", abb);
		}
		abb = find_unique_abbrev(elem->sha1, abbrev);
		printf("..%s%s\n", abb, c_reset);

		if (mode_differs) {
			int added = !!elem->mode;
			for (i = 0; added && i < num_parent; i++)
				if (elem->parent[i].status !=
				    DIFF_STATUS_ADDED)
					added = 0;
			if (added)
				printf("%snew file mode %06o",
				       c_meta, elem->mode);
			else {
				if (!elem->mode)
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
		dump_quoted_path("--- a/", elem->path, c_meta, c_reset);
		dump_quoted_path("+++ b/", elem->path, c_meta, c_reset);
		dump_sline(sline, cnt, num_parent, opt->color_diff);
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

	if (rev->loginfo)
		show_log(rev, opt->msg_sep);

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

	if (line_termination) {
		if (quote_c_style(p->path, NULL, NULL, 0))
			quote_c_style(p->path, NULL, stdout, 0);
		else
			printf("%s", p->path);
		putchar(line_termination);
	}
	else {
		printf("%s%c", p->path, line_termination);
	}
}

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
		show_patch_diff(p, num_parent, dense, rev);
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
	diffopts.recursive = 1;

	show_log_first = !!rev->loginfo;
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
			show_log(rev, opt->msg_sep);
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
		if (opt->output_format & DIFF_FORMAT_PATCH) {
			if (needsep)
				putchar(opt->line_termination);
			for (p = paths; p; p = p->next) {
				if (p->len)
					show_patch_diff(p, num_parent, dense,
							rev);
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
		hashcpy((unsigned char*)(parent + num_parent),
			parents->item->object.sha1);
	diff_tree_combined(sha1, parent, num_parent, dense, rev);
}
