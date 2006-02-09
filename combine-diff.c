#include "cache.h"
#include "commit.h"
#include "diff.h"
#include "diffcore.h"
#include "quote.h"

static int uninteresting(struct diff_filepair *p)
{
	if (diff_unmodified_pair(p))
		return 1;
	return 0;
}

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
			if (uninteresting(q->queue[i]))
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

			memcpy(p->sha1, q->queue[i]->two->sha1, 20);
			p->mode = q->queue[i]->two->mode;
			memcpy(p->parent[n].sha1, q->queue[i]->one->sha1, 20);
			p->parent[n].mode = q->queue[i]->one->mode;
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

			if (uninteresting(q->queue[i]))
				continue;
			path = q->queue[i]->two->path;
			len = strlen(path);
			if (len == p->len && !memcmp(path, p->path, len)) {
				found = 1;
				memcpy(p->parent[n].sha1,
				       q->queue[i]->one->sha1, 20);
				p->parent[n].mode = q->queue[i]->one->mode;
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
	if (!memcmp(sha1, null_sha1, 20)) {
		/* deleted blob */
		*size = 0;
		return xcalloc(1, 1);
	}
	blob = read_sha1_file(sha1, type, size);
	if (strcmp(type, "blob"))
		die("object '%s' is not a blob!", sha1_to_hex(sha1));
	return blob;
}

#define TMPPATHLEN 50
#define MAXLINELEN 10240

static void write_to_temp_file(char *tmpfile, void *blob, unsigned long size)
{
	int fd = git_mkstemp(tmpfile, TMPPATHLEN, ".diff_XXXXXX");
	if (fd < 0)
		die("unable to create temp-file");
	if (write(fd, blob, size) != size)
		die("unable to write temp-file");
	close(fd);
}

static void write_temp_blob(char *tmpfile, const unsigned char *sha1)
{
	unsigned long size;
	void *blob;
	blob = grab_blob(sha1, &size);
	write_to_temp_file(tmpfile, blob, size);
	free(blob);
}

static int parse_num(char **cp_p, unsigned int *num_p)
{
	char *cp = *cp_p;
	unsigned int num = 0;
	int read_some;

	while ('0' <= *cp && *cp <= '9')
		num = num * 10 + *cp++ - '0';
	if (!(read_some = cp - *cp_p))
		return -1;
	*cp_p = cp;
	*num_p = num;
	return 0;
}

static int parse_hunk_header(char *line, int len,
			     unsigned int *ob, unsigned int *on,
			     unsigned int *nb, unsigned int *nn)
{
	char *cp;
	cp = line + 4;
	if (parse_num(&cp, ob)) {
	bad_line:
		return error("malformed diff output: %s", line);
	}
	if (*cp == ',') {
		cp++;
		if (parse_num(&cp, on))
			goto bad_line;
	}
	else
		*on = 1;
	if (*cp++ != ' ' || *cp++ != '+')
		goto bad_line;
	if (parse_num(&cp, nb))
		goto bad_line;
	if (*cp == ',') {
		cp++;
		if (parse_num(&cp, nn))
			goto bad_line;
	}
	else
		*nn = 1;
	return -!!memcmp(cp, " @@", 3);
}

static void append_lost(struct sline *sline, int n, const char *line)
{
	struct lline *lline;
	int len = strlen(line);
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

static void combine_diff(const unsigned char *parent, const char *ourtmp,
			 struct sline *sline, int cnt, int n, int num_parent)
{
	FILE *in;
	char parent_tmp[TMPPATHLEN];
	char cmd[TMPPATHLEN * 2 + 1024];
	char line[MAXLINELEN];
	unsigned int lno, ob, on, nb, nn, p_lno;
	unsigned long nmask = (1UL << n);
	struct sline *lost_bucket = NULL;

	if (!cnt)
		return; /* result deleted */

	write_temp_blob(parent_tmp, parent);
	sprintf(cmd, "diff --unified=0 -La/x -Lb/x '%s' '%s'",
		parent_tmp, ourtmp);
	in = popen(cmd, "r");
	if (!in)
		die("cannot spawn %s", cmd);

	lno = 1;
	while (fgets(line, sizeof(line), in) != NULL) {
		int len = strlen(line);
		if (5 < len && !memcmp("@@ -", line, 4)) {
			if (parse_hunk_header(line, len,
					      &ob, &on, &nb, &nn))
				break;
			lno = nb;
			if (!nb)
				/* @@ -1,2 +0,0 @@ to remove the
				 * first two lines...
				 */
				nb = 1;
			if (nn == 0)
				/* @@ -X,Y +N,0 @@ removed Y lines
				 * that would have come *after* line N
				 * in the result.  Our lost buckets hang
				 * to the line after the removed lines,
				 */
				lost_bucket = &sline[nb];
			else
				lost_bucket = &sline[nb-1];
			if (!sline[nb-1].p_lno)
				sline[nb-1].p_lno =
					xcalloc(num_parent,
						sizeof(unsigned long));
			sline[nb-1].p_lno[n] = ob;
			continue;
		}
		if (!lost_bucket)
			continue; /* not in any hunk yet */
		switch (line[0]) {
		case '-':
			append_lost(lost_bucket, n, line+1);
			break;
		case '+':
			sline[lno-1].flag |= nmask;
			lno++;
			break;
		}
	}
	fclose(in);
	unlink(parent_tmp);

	/* Assign line numbers for this parent.
	 *
	 * sline[lno].p_lno[n] records the first line number
	 * (counting from 1) for parent N if the final hunk display
	 * started by showing sline[lno] (possibly showing the lost
	 * lines attached to it first).
	 */
	for (lno = 0,  p_lno = 1; lno < cnt; lno++) {
		struct lline *ll;
		sline[lno].p_lno[n] = p_lno;

		/* How many lines would this sline advance the p_lno? */
		ll = sline[lno].lost_head;
		while (ll) {
			if (ll->parent_map & nmask)
				p_lno++; /* '-' means parent had it */
			ll = ll->next;
		}
		if (!(sline[lno].flag & nmask))
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
			       int uninteresting)
{
	/* We have examined up to i-1 and are about to look at i.
	 * Find next interesting or uninteresting line.  Here,
	 * "interesting" does not mean interesting(), but marked by
	 * the give_context() function below (i.e. it includes context
	 * lines that are not interesting to interesting() function
	 * that are surrounded by interesting() ones.
	 */
	while (i < cnt)
		if (uninteresting
		    ? !(sline[i].flag & mark)
		    : (sline[i].flag & mark))
			return i;
		else
			i++;
	return cnt;
}

static int give_context(struct sline *sline, unsigned long cnt, int num_parent)
{
	unsigned long all_mask = (1UL<<num_parent) - 1;
	unsigned long mark = (1UL<<num_parent);
	unsigned long i;

	/* Two groups of interesting lines may have a short gap of
	 * unintersting lines.  Connect such groups to give them a
	 * bit of context.
	 *
	 * We first start from what the interesting() function says,
	 * and mark them with "mark", and paint context lines with the
	 * mark.  So interesting() would still say false for such context
	 * lines but they are treated as "interesting" in the end.
	 */
	i = find_next(sline, mark, 0, cnt, 0);
	if (cnt <= i)
		return 0;

	while (i < cnt) {
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
		if (cnt <= j)
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
		k = (j + context < cnt) ? j + context : cnt;
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

	for (i = 0; i < cnt; i++) {
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
	while (i < cnt) {
		unsigned long j, hunk_begin, hunk_end;
		unsigned long same_diff;
		while (i < cnt && !(sline[i].flag & mark))
			i++;
		if (cnt <= i)
			break; /* No more interesting hunks */
		hunk_begin = i;
		for (j = i + 1; j < cnt; j++) {
			if (!(sline[j].flag & mark)) {
				/* Look beyond the end to see if there
				 * is an interesting line after this
				 * hunk within context span.
				 */
				unsigned long la; /* lookahead */
				int contin = 0;
				la = adjust_hunk_tail(sline, all_mask,
						     hunk_begin, j);
				la = (la + context < cnt) ?
					(la + context) : cnt;
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

static void show_parent_lno(struct sline *sline, unsigned long l0, unsigned long l1, unsigned long cnt, int n)
{
	l0 = sline[l0].p_lno[n];
	l1 = sline[l1].p_lno[n];
	printf(" -%lu,%lu", l0, l1-l0);
}

static void dump_sline(struct sline *sline, unsigned long cnt, int num_parent)
{
	unsigned long mark = (1UL<<num_parent);
	int i;
	unsigned long lno = 0;

	if (!cnt)
		return; /* result deleted */

	while (1) {
		struct sline *sl = &sline[lno];
		int hunk_end;
		while (lno < cnt && !(sline[lno].flag & mark))
			lno++;
		if (cnt <= lno)
			break;
		for (hunk_end = lno + 1; hunk_end < cnt; hunk_end++)
			if (!(sline[hunk_end].flag & mark))
				break;
		for (i = 0; i <= num_parent; i++) putchar(combine_marker);
		for (i = 0; i < num_parent; i++)
			show_parent_lno(sline, lno, hunk_end, cnt, i);
		printf(" +%lu,%lu ", lno+1, hunk_end-lno);
		for (i = 0; i <= num_parent; i++) putchar(combine_marker);
		putchar('\n');
		while (lno < hunk_end) {
			struct lline *ll;
			int j;
			unsigned long p_mask;
			sl = &sline[lno++];
			ll = sl->lost_head;
			while (ll) {
				for (j = 0; j < num_parent; j++) {
					if (ll->parent_map & (1UL<<j))
						putchar('-');
					else
						putchar(' ');
				}
				puts(ll->line);
				ll = ll->next;
			}
			p_mask = 1;
			for (j = 0; j < num_parent; j++) {
				if (p_mask & sl->flag)
					putchar('+');
				else
					putchar(' ');
				p_mask <<= 1;
			}
			printf("%.*s\n", sl->len, sl->bol);
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

	for (lno = 0; lno < cnt; lno++) {
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

int show_combined_diff(struct combine_diff_path *elem, int num_parent,
		       int dense, const char *header)
{
	unsigned long size, cnt, lno;
	char *result, *cp, *ep;
	struct sline *sline; /* survived lines */
	int mode_differs = 0;
	int i, show_hunks, shown_header = 0;
	char ourtmp_buf[TMPPATHLEN];
	char *ourtmp = ourtmp_buf;

	/* Read the result of merge first */
	if (memcmp(elem->sha1, null_sha1, 20)) {
		result = grab_blob(elem->sha1, &size);
		write_to_temp_file(ourtmp, result, size);
	}
	else {
		/* Used by diff-tree to read from the working tree */
		struct stat st;
		int fd;
		ourtmp = elem->path;
		if (0 <= (fd = open(ourtmp, O_RDONLY)) &&
		    !fstat(fd, &st)) {
			int len = st.st_size;
			int cnt = 0;

			size = len;
			result = xmalloc(len + 1);
			while (cnt < len) {
				int done = xread(fd, result+cnt, len-cnt);
				if (done == 0)
					break;
				if (done < 0)
					die("read error '%s'", ourtmp);
				cnt += done;
			}
			result[len] = 0;
		}
		else {
			/* deleted file */
			size = 0;
			result = xmalloc(1);
			result[0] = 0;
			ourtmp = "/dev/null";
		}
		if (0 <= fd)
			close(fd);
	}

	for (cnt = 0, cp = result; cp - result < size; cp++) {
		if (*cp == '\n')
			cnt++;
	}
	if (size && result[size-1] != '\n')
		cnt++; /* incomplete line */

	sline = xcalloc(cnt+1, sizeof(*sline));
	ep = result;
	sline[0].bol = result;
	for (lno = 0; lno <= cnt; lno++) {
		sline[lno].lost_tail = &sline[lno].lost_head;
		sline[lno].flag = 0;
	}
	for (lno = 0, cp = result; cp - result < size; cp++) {
		if (*cp == '\n') {
			sline[lno].len = cp - sline[lno].bol;
			lno++;
			if (lno < cnt)
				sline[lno].bol = cp + 1;
		}
	}
	if (size && result[size-1] != '\n')
		sline[cnt-1].len = size - (sline[cnt-1].bol - result);

	sline[0].p_lno = xcalloc((cnt+1) * num_parent, sizeof(unsigned long));
	for (lno = 0; lno < cnt; lno++)
		sline[lno+1].p_lno = sline[lno].p_lno + num_parent;

	for (i = 0; i < num_parent; i++) {
		int j;
		for (j = 0; j < i; j++) {
			if (!memcmp(elem->parent[i].sha1,
				    elem->parent[j].sha1, 20)) {
				reuse_combine_diff(sline, cnt, i, j);
				break;
			}
		}
		if (i <= j)
			combine_diff(elem->parent[i].sha1, ourtmp, sline,
				     cnt, i, num_parent);
		if (elem->parent[i].mode != elem->mode)
			mode_differs = 1;
	}

	show_hunks = make_hunks(sline, cnt, num_parent, dense);

	if (show_hunks || mode_differs) {
		const char *abb;
		char null_abb[DEFAULT_ABBREV + 1];

		memset(null_abb, '0', DEFAULT_ABBREV);
		null_abb[DEFAULT_ABBREV] = 0;
		if (header) {
			shown_header++;
			puts(header);
		}
		printf("diff --%s ", dense ? "cc" : "combined");
		if (quote_c_style(elem->path, NULL, NULL, 0))
			quote_c_style(elem->path, NULL, stdout, 0);
		else
			printf("%s", elem->path);
		putchar('\n');
		printf("index ");
		for (i = 0; i < num_parent; i++) {
			if (elem->parent[i].mode != elem->mode)
				mode_differs = 1;
			if (memcmp(elem->parent[i].sha1, null_sha1, 20))
				abb = find_unique_abbrev(elem->parent[i].sha1,
							 DEFAULT_ABBREV);
			else
				abb = null_abb;
			printf("%s%s", i ? "," : "", abb);
		}
		if (memcmp(elem->sha1, null_sha1, 20))
			abb = find_unique_abbrev(elem->sha1, DEFAULT_ABBREV);
		else
			abb = null_abb;
		printf("..%s\n", abb);

		if (mode_differs) {
			printf("mode ");
			for (i = 0; i < num_parent; i++) {
				printf("%s%06o", i ? "," : "",
				       elem->parent[i].mode);
			}
			printf("..%06o\n", elem->mode);
		}
		dump_sline(sline, cnt, num_parent);
	}
	if (ourtmp == ourtmp_buf)
		unlink(ourtmp);
	free(result);

	for (i = 0; i < cnt; i++) {
		if (sline[i].lost_head) {
			struct lline *ll = sline[i].lost_head;
			while (ll) {
				struct lline *tmp = ll;
				ll = ll->next;
				free(tmp);
			}
		}
	}
	free(sline[0].p_lno);
	free(sline);
	return shown_header;
}

#define COLONS "::::::::::::::::::::::::::::::::"

static void show_raw_diff(struct combine_diff_path *p, int num_parent, const char *header, struct diff_options *opt)
{
	int i, offset, mod_type = 'A';
	const char *prefix;
	int line_termination, inter_name_termination;

	line_termination = opt->line_termination;
	inter_name_termination = '\t';
	if (!line_termination)
		inter_name_termination = 0;

	if (header)
		puts(header);
	offset = strlen(COLONS) - num_parent;
	if (offset < 0)
		offset = 0;
	prefix = COLONS + offset;

	/* Show the modes */
	for (i = 0; i < num_parent; i++) {
		int mode = p->parent[i].mode;
		if (mode)
			mod_type = 'M';
		printf("%s%06o", prefix, mode);
		prefix = " ";
	}
	printf("%s%06o", prefix, p->mode);
	if (!p->mode)
		mod_type = 'D';

	/* Show sha1's */
	for (i = 0; i < num_parent; i++) {
		printf("%s%s", prefix, diff_unique_abbrev(p->parent[i].sha1, opt->abbrev));
		prefix = " ";
	}
	printf("%s%s", prefix, diff_unique_abbrev(p->sha1, opt->abbrev));

	/* Modification type, terminations, filename */
	printf(" %c%c%s%c", mod_type, inter_name_termination, p->path, line_termination);
}

const char *diff_tree_combined_merge(const unsigned char *sha1,
			     const char *header, int dense,
			     struct diff_options *opt)
{
	struct commit *commit = lookup_commit(sha1);
	struct diff_options diffopts;
	struct commit_list *parents;
	struct combine_diff_path *p, *paths = NULL;
	int num_parent, i, num_paths;

	diff_setup(&diffopts);
	diffopts.output_format = DIFF_FORMAT_NO_OUTPUT;
	diffopts.recursive = 1;

	/* count parents */
	for (parents = commit->parents, num_parent = 0;
	     parents;
	     parents = parents->next, num_parent++)
		; /* nothing */

	/* find set of paths that everybody touches */
	for (parents = commit->parents, i = 0;
	     parents;
	     parents = parents->next, i++) {
		struct commit *parent = parents->item;
		diff_tree_sha1(parent->object.sha1, commit->object.sha1, "",
			       &diffopts);
		paths = intersect_paths(paths, i, num_parent);
		diff_flush(&diffopts);
	}

	/* find out surviving paths */
	for (num_paths = 0, p = paths; p; p = p->next) {
		if (p->len)
			num_paths++;
	}
	if (num_paths) {
		for (p = paths; p; p = p->next) {
			if (!p->len)
				continue;
			if (opt->output_format == DIFF_FORMAT_RAW) {
				show_raw_diff(p, num_parent, header, opt);
				header = NULL;
				continue;
			}
			if (show_combined_diff(p, num_parent, dense, header))
				header = NULL;
		}
	}

	/* Clean things up */
	while (paths) {
		struct combine_diff_path *tmp = paths;
		paths = paths->next;
		free(tmp);
	}
	return header;
}
