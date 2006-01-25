#include "cache.h"
#include "commit.h"
#include "diff.h"
#include "diffcore.h"
#include "quote.h"

struct path_list {
	struct path_list *next;
	int len;
	char *path;
	unsigned char sha1[20];
	unsigned char parent_sha1[FLEX_ARRAY][20];
};

static int uninteresting(struct diff_filepair *p)
{
	if (diff_unmodified_pair(p))
		return 1;
	if (!S_ISREG(p->one->mode) || !S_ISREG(p->two->mode))
		return 1;
	return 0;
}

static struct path_list *intersect_paths(struct path_list *curr,
					 int n, int num_parent)
{
	struct diff_queue_struct *q = &diff_queued_diff;
	struct path_list *p;
	int i;

	if (!n) {
		struct path_list *list = NULL, **tail = &list;
		for (i = 0; i < q->nr; i++) {
			int len;
			const char *path;
			if (uninteresting(q->queue[i]))
				continue;
			path = q->queue[i]->two->path;
			len = strlen(path);

			p = xmalloc(sizeof(*p) + len + 1 + num_parent * 20);
			p->path = (char*) &(p->parent_sha1[num_parent][0]);
			memcpy(p->path, path, len);
			p->path[len] = 0;
			p->len = len;
			p->next = NULL;
			memcpy(p->sha1, q->queue[i]->two->sha1, 20);
			memcpy(p->parent_sha1[n], q->queue[i]->one->sha1, 20);
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
				memcpy(p->parent_sha1[n],
				       q->queue[i]->one->sha1, 20);
				break;
			}
		}
		if (!found)
			p->len = 0;
	}
	return curr;
}

struct lline {
	struct lline *next;
	int len;
	unsigned long parent_map;
	char line[FLEX_ARRAY];
};

struct sline {
	struct lline *lost_head, **lost_tail;
	char *bol;
	int len;
	unsigned long flag;
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
			 struct sline *sline, int cnt, int n)
{
	FILE *in;
	char parent_tmp[TMPPATHLEN];
	char cmd[TMPPATHLEN * 2 + 1024];
	char line[MAXLINELEN];
	unsigned int lno, ob, on, nb, nn;
	unsigned long pmask = ~(1UL << n);
	struct sline *lost_bucket = NULL;

	write_temp_blob(parent_tmp, parent);
	sprintf(cmd, "diff --unified=0 -La/x -Lb/x '%s' '%s'",
		parent_tmp, ourtmp);
	in = popen(cmd, "r");
	if (!in)
		return;

	lno = 1;
	while (fgets(line, sizeof(line), in) != NULL) {
		int len = strlen(line);
		if (5 < len && !memcmp("@@ -", line, 4)) {
			if (parse_hunk_header(line, len,
					      &ob, &on, &nb, &nn))
				break;
			lno = nb;
			if (!nb) {
				/* @@ -1,2 +0,0 @@ to remove the
				 * first two lines...
				 */
				nb = 1;
			}
			lost_bucket = &sline[nb-1]; /* sline is 0 based */
			continue;
		}
		if (!lost_bucket)
			continue;
		switch (line[0]) {
		case '-':
			append_lost(lost_bucket, n, line+1);
			break;
		case '+':
			sline[lno-1].flag &= pmask;
			lno++;
			break;
		}
	}
	fclose(in);
	unlink(parent_tmp);
}

static unsigned long context = 3;
static char combine_marker = '@';

static int interesting(struct sline *sline, unsigned long all_mask)
{
	return ((sline->flag & all_mask) != all_mask || sline->lost_head);
}

static unsigned long line_diff_parents(struct sline *sline, unsigned long all_mask)
{
	/*
	 * Look at the line and see from which parents we have difference.
	 * Lower bits of sline->flag records if the parent had this line,
	 * so XOR with all_mask gives us on-bits for parents we have
	 * differences with.
	 */
	unsigned long parents = (sline->flag ^ all_mask);
	if (sline->lost_head) {
		struct lline *ll;
		for (ll = sline->lost_head; ll; ll = ll->next)
			parents |= ll->parent_map;
	}
	return parents & all_mask;
}

static void make_hunks(struct sline *sline, unsigned long cnt,
		       int num_parent, int dense)
{
	unsigned long all_mask = (1UL<<num_parent) - 1;
	unsigned long mark = (1UL<<num_parent);
	unsigned long i;

	i = 0;
	while (i < cnt) {
		if (interesting(&sline[i], all_mask)) {
			unsigned long j = (context < i) ? i - context : 0;
			while (j <= i)
				sline[j++].flag |= mark;
			while (++i < cnt) {
				if (!interesting(&sline[i], all_mask))
					break;
				sline[i].flag |= mark;
			}
			j = (i + context < cnt) ? i + context : cnt;
			while (i < j)
				sline[i++].flag |= mark;
			continue;
		}
		i++;
	}
	if (!dense)
		return;

	/* Look at each hunk, and if it contains changes from only
	 * one parent, mark that uninteresting.
	 */
	i = 0;
	while (i < cnt) {
		int j, hunk_end, diffs;
		unsigned long parents;
		while (i < cnt && !(sline[i].flag & mark))
			i++;
		if (cnt <= i)
			break; /* No more interesting hunks */
		for (hunk_end = i + 1; hunk_end < cnt; hunk_end++)
			if (!(sline[hunk_end].flag & mark))
				break;
		/* [i..hunk_end) are interesting.  Now is it from
		 * only one parent?
		 * If lost lines are only from one parent and
		 * remaining lines existed in parents other than
		 * that parent, then the hunk is not that interesting.
		 */
		parents = 0;
		diffs = 0;
		for (j = i; j < hunk_end; j++)
			parents |= line_diff_parents(sline + j, all_mask);
		/* Now, how many bits from [0..num_parent) are on? */
		for (j = 0; j < num_parent; j++) {
			if (parents & (1UL<<j))
				diffs++;
		}
		if (diffs < 2) {
			/* This hunk is not that interesting after all */
			for (j = i; j < hunk_end; j++)
				sline[j].flag &= ~mark;
		}
		i = hunk_end;
	}
}

static void dump_sline(struct sline *sline, int cnt, int num_parent)
{
	unsigned long mark = (1UL<<num_parent);
	int i;
	int lno = 0;

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
		printf(" +%d,%d ", lno+1, hunk_end-lno);
		for (i = 0; i <= num_parent; i++) putchar(combine_marker);
		putchar('\n');
		while (lno < hunk_end) {
			struct lline *ll;
			int j;
			sl = &sline[lno++];
			ll = sl->lost_head;
			while (ll) {
				for (j = 0; j < num_parent; j++) {
					if (ll->parent_map & (1UL<<j))
						putchar('-');
					else
						putchar(' ');
				}
				putchar(' ');
				puts(ll->line);
				ll = ll->next;
			}
			for (j = 0; j < num_parent; j++) {
				if ((1UL<<j) & sl->flag)
					putchar(' ');
				else
					putchar('+');
			}
			printf(" %.*s\n", sl->len, sl->bol);
		}
	}
}

static void show_combined_diff(struct path_list *elem, int num_parent,
			       int dense)
{
	unsigned long size, cnt, lno;
	char *result, *cp, *ep;
	struct sline *sline; /* survived lines */
	int i;
	char ourtmp[TMPPATHLEN];

	/* Read the result of merge first */
	result = grab_blob(elem->sha1, &size);
	write_to_temp_file(ourtmp, result, size);

	for (cnt = 0, cp = result; cp - result < size; cp++) {
		if (*cp == '\n')
			cnt++;
	}
	if (result[size-1] != '\n')
		cnt++; /* incomplete line */

	sline = xcalloc(cnt, sizeof(*sline));
	ep = result;
	sline[0].bol = result;
	for (lno = 0, cp = result; cp - result < size; cp++) {
		if (*cp == '\n') {
			sline[lno].lost_tail = &sline[lno].lost_head;
			sline[lno].len = cp - sline[lno].bol;
			sline[lno].flag = (1UL<<num_parent) - 1;
			lno++;
			if (lno < cnt)
				sline[lno].bol = cp + 1;
		}
	}
	if (result[size-1] != '\n') {
		sline[cnt-1].lost_tail = &sline[cnt-1].lost_head;
		sline[cnt-1].len = size - (sline[cnt-1].bol - result);
		sline[cnt-1].flag = (1UL<<num_parent) - 1;
	}

	for (i = 0; i < num_parent; i++)
		combine_diff(elem->parent_sha1[i], ourtmp, sline, cnt, i);

	make_hunks(sline, cnt, num_parent, dense);

	dump_sline(sline, cnt, num_parent);
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
	free(sline);
}

int diff_tree_combined_merge(const unsigned char *sha1,
			     const char *header,
			     int show_empty_merge, int dense)
{
	struct commit *commit = lookup_commit(sha1);
	struct diff_options diffopts;
	struct commit_list *parents;
	struct path_list *p, *paths = NULL;
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
	if (num_paths || show_empty_merge) {
		puts(header);
		for (p = paths; p; p = p->next) {
			if (!p->len)
				continue;
			printf("diff --combined ");
			if (quote_c_style(p->path, NULL, NULL, 0))
				quote_c_style(p->path, NULL, stdout, 0);
			else
				printf("%s", p->path);
			putchar('\n');
			show_combined_diff(p, num_parent, dense);
		}
	}

	/* Clean things up */
	while (paths) {
		struct path_list *tmp = paths;
		paths = paths->next;
		free(tmp);
	}
	return 0;
}
